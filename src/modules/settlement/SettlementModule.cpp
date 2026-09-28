#include "settlement/SettlementModule.h"

#include "common/Identity.h"
#include "common/SnapshotHash.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "common/SubjectRef.h"
#include "settlement/Settlement.h"
#include "settlement/SettlementRules.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace eve::settlement {
namespace {

struct ResourceKey {
    std::string subject;
    std::string resource;
    bool        operator<(const ResourceKey& other) const {
        if (subject != other.subject) return subject < other.subject;
        return resource < other.resource;
    }
};

struct ResourceState {
    double current = 0.0;
    double maximum = 0.0;
};

class LedgerPolicy final : public ISettlementPolicy {
public:
    explicit LedgerPolicy(ResourceState& state) : state_(state) {}

    Result<void> validate(SettlementContext&) override {
        if (!(state_.current >= 0.0) || !(state_.current <= state_.maximum) || !(state_.maximum >= 0.0) ||
            !std::isfinite(state_.current) || !std::isfinite(state_.maximum))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                           "settlement ledger resource is outside bounds", "resource",
                                                           {}, "settlement.squirrel"));
        return Result<void>::success();
    }
    Result<void> sourceModifiers(SettlementContext&) override { return Result<void>::success(); }
    Result<void> targetMitigation(SettlementContext&) override { return Result<void>::success(); }
    Result<void> armorShield(SettlementContext&) override { return Result<void>::success(); }
    Result<void> clamp(SettlementContext& context) override {
        const auto& kind = context.request().kind;
        if (kind == "heal" || kind == "gain") return context.setClampMax(state_.maximum - state_.current);
        if (kind == "damage" || kind == "spend") return context.setClampMax(state_.current);
        return context.setClampMax(context.magnitude());
    }
    Result<PreparedApply> prepareApply(const SettlementContext& context) override {
        const double before = state_.current;
        const auto&  kind   = context.request().kind;
        const double after  = (kind == "heal" || kind == "gain") ? before + context.magnitude()
                                                                : before - context.magnitude();
        return Result<PreparedApply>::success(PreparedApply(
            [this, after]() {
                state_.current = after;
                return Result<void>::success();
            },
            [this, before]() { state_.current = before; }));
    }

private:
    ResourceState& state_;
};

class ScriptSettlementRuntime {
public:
    Result<Value> configureRulesJson(const std::string& json) {
        SettlementRuleSet candidate;
        auto              configured = candidate.configureJson(json);
        if (!configured) return Result<Value>::failure(configured.status());
        SettlementPipeline pipeline;
        auto               installed = candidate.install(pipeline);
        if (!installed) return Result<Value>::failure(installed.status());
        rules_    = std::move(candidate);
        pipeline_ = std::move(pipeline);
        Value::Object result;
        result["ruleCount"] = static_cast<std::int64_t>(rules_.size());
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> validateRulesJson(const std::string& json) const {
        auto inspected = SettlementRuleSet::fromJson(json);
        if (!inspected) return Result<Value>::failure(inspected.status());
        Value::Object result;
        result["ruleCount"] = static_cast<std::int64_t>(inspected.value().size());
        return Result<Value>::success(Value(std::move(result)));
    }

    Result<Value> rulesCanonicalJson() const {
        auto json = rules_.canonicalJson();
        if (!json) return Result<Value>::failure(json.status());
        return Result<Value>::success(Value(json.value()));
    }

    Result<Value> rulesDigest() const {
        auto digest = rules_.digest(snapshotContentHashProvider());
        if (!digest) return Result<Value>::failure(digest.status());
        return Result<Value>::success(Value(digest.value().format()));
    }

    std::int64_t ruleCount() const { return static_cast<std::int64_t>(rules_.size()); }

    Result<Value> upsertResource(const std::string& subject, const std::string& resource, double current,
                                 double maximum) {
        auto parsed = PersistentId::parse(subject);
        if (!parsed)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "settlement subject must be a UUID", "subject", {},
                                                            "settlement.squirrel"));
        if (resource.empty() || !std::isfinite(current) || !std::isfinite(maximum) || maximum < 0.0 || current < 0.0 ||
            current > maximum)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "settlement resource bounds are invalid", "resource", {},
                                                            "settlement.squirrel"));
        resources_[{subject, resource}] = ResourceState{current, maximum};
        Value::Object result;
        result["subject"]  = subject;
        result["resource"] = resource;
        result["current"]  = current;
        result["maximum"]  = maximum;
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

    Result<Value> getResource(const std::string& subject, const std::string& resource) const {
        const auto found = resources_.find({subject, resource});
        if (found == resources_.end())
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                            "settlement ledger resource was not found", "resource", {},
                                                            "settlement.squirrel"));
        Value::Object result;
        result["subject"]  = subject;
        result["resource"] = resource;
        result["current"]  = found->second.current;
        result["maximum"]  = found->second.maximum;
        return Result<Value>::success(Value(std::move(result)));
    }

    Result<Value> removeResource(const std::string& subject, const std::string& resource) {
        if (resources_.erase({subject, resource}) == 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                            "settlement ledger resource was not found", "resource", {},
                                                            "settlement.squirrel"));
        return Result<Value>::success(Value(true), Status::success(StatusCode::Applied));
    }

    void clearResources() { resources_.clear(); }

    Result<Value> settle(const std::string& sourceText, const std::string& targetText, const std::string& kind,
                         const std::string& resource, double magnitude, const std::string& tagsCsv,
                         const std::string& contextJson, std::int64_t tick) {
        auto sourceId = PersistentId::parse(sourceText);
        if (!sourceId)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "settlement source must be a UUID", "source", {},
                                                            "settlement.squirrel"));
        auto targetId = PersistentId::parse(targetText);
        if (!targetId)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "settlement target must be a UUID", "target", {},
                                                            "settlement.squirrel"));
        if (kind.empty() || resource.empty() || !std::isfinite(magnitude) || magnitude < 0.0 || tick < 0)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "settlement request fields are invalid", "request", {},
                                                            "settlement.squirrel"));
        auto found = resources_.find({targetText, resource});
        if (found == resources_.end())
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                            "settlement ledger target resource was not found",
                                                            "resource", {}, "settlement.squirrel"));

        SettlementRequest request;
        request.source    = SubjectRef::fromPersistentId(*sourceId);
        request.target    = SubjectRef::fromPersistentId(*targetId);
        request.kind      = kind;
        request.resource  = resource;
        request.magnitude = magnitude;
        request.tick      = SimulationTick(static_cast<std::uint64_t>(tick));
        if (!tagsCsv.empty()) {
            std::string tag;
            for (char ch : tagsCsv) {
                if (ch == ',') {
                    if (!tag.empty()) {
                        request.tags.push_back(tag);
                        tag.clear();
                    }
                } else if (ch != ' ') {
                    tag.push_back(ch);
                }
            }
            if (!tag.empty()) request.tags.push_back(tag);
        }
        if (!contextJson.empty()) {
            auto context = Value::fromJson(contextJson);
            if (!context) return Result<Value>::failure(context.status());
            request.context = std::move(context).takeValue();
        }

        LedgerPolicy policy(found->second);
        auto         settled = pipeline_.settle(request, policy);
        if (!settled) return Result<Value>::failure(settled.status());
        auto json = settlementResultCanonicalJson(settled.value());
        if (!json) return Result<Value>::failure(json.status());
        Value::Object result;
        result["resultJson"]  = json.value();
        result["applied"]     = settled.value().applied;
        result["absorbed"]    = settled.value().absorbed;
        result["resisted"]    = settled.value().resisted;
        result["disposition"] = std::string(settlementDispositionName(settled.value().disposition));
        result["current"]     = found->second.current;
        result["maximum"]     = found->second.maximum;
        return Result<Value>::success(Value(std::move(result)), Status::success(StatusCode::Applied));
    }

private:
    SettlementPipeline                 pipeline_;
    SettlementRuleSet                  rules_;
    std::map<ResourceKey, ResourceState> resources_;
};

ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}

ssq::Table nullRuntime(HSQUIRRELVM vm) {
    return project(vm, Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                "settlement runtime must not be null", "runtime", {},
                                                                "settlement.squirrel")));
}

ssq::Table newRuntime(HSQUIRRELVM vm) {
    auto runtime = std::make_unique<ScriptSettlementRuntime>();
    auto object  = script::makeOwnedSquirrelInstance<ScriptSettlementRuntime>(vm, std::move(runtime));
    if (!object) return script::projectStatusResult(vm, object.status());
    auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied));
    result.set("value", std::move(object).takeValue());
    result.set("ownership", std::string("owned"));
    return result;
}

}  // namespace

Module_IMPL(Settlement, new Settlement());

void Settlement::expose(ssq::Table& table) {
    const HSQUIRRELVM vm      = table.getHandle();
    auto              runtime = table.addClass<ScriptSettlementRuntime>(
        "SettlementRuntime", std::function<ScriptSettlementRuntime*()>([] { return nullptr; }), true);
    runtime.addFunc("ownership", [](ScriptSettlementRuntime*) { return std::string("owned"); });
    runtime.addFunc("configureRulesJson", [vm](ScriptSettlementRuntime* self, const std::string& json) {
        return self ? project(vm, self->configureRulesJson(json)) : nullRuntime(vm);
    });
    runtime.addFunc("validateRulesJson", [vm](ScriptSettlementRuntime* self, const std::string& json) {
        return self ? project(vm, self->validateRulesJson(json)) : nullRuntime(vm);
    });
    runtime.addFunc("rulesCanonicalJson", [vm](ScriptSettlementRuntime* self) {
        return self ? project(vm, self->rulesCanonicalJson()) : nullRuntime(vm);
    });
    runtime.addFunc("rulesDigest", [vm](ScriptSettlementRuntime* self) {
        return self ? project(vm, self->rulesDigest()) : nullRuntime(vm);
    });
    runtime.addFunc("ruleCount",
                    [](ScriptSettlementRuntime* self) { return self ? self->ruleCount() : std::int64_t{0}; });
    runtime.addFunc("upsertResource", [vm](ScriptSettlementRuntime* self, const std::string& subject,
                                           const std::string& resource, float current, float maximum) {
        return self ? project(vm, self->upsertResource(subject, resource, current, maximum)) : nullRuntime(vm);
    });
    runtime.addFunc("getResource", [vm](ScriptSettlementRuntime* self, const std::string& subject,
                                        const std::string& resource) {
        return self ? project(vm, self->getResource(subject, resource)) : nullRuntime(vm);
    });
    runtime.addFunc("removeResource", [vm](ScriptSettlementRuntime* self, const std::string& subject,
                                           const std::string& resource) {
        return self ? project(vm, self->removeResource(subject, resource)) : nullRuntime(vm);
    });
    runtime.addFunc("clearResources", [](ScriptSettlementRuntime* self) {
        if (self) self->clearResources();
    });
    runtime.addFunc("settle", [vm](ScriptSettlementRuntime* self, const std::string& source, const std::string& target,
                                   const std::string& kind, const std::string& resource, float magnitude,
                                   const std::string& tagsCsv, const std::string& contextJson, std::int64_t tick) {
        return self ? project(vm, self->settle(source, target, kind, resource, magnitude, tagsCsv, contextJson, tick))
                    : nullRuntime(vm);
    });

    auto module = table.addClass(name, Settlement::create, false);
    module.addFunc("getName", &Settlement::getName);
    module.addFunc("newRuntime", [vm](Settlement*) { return newRuntime(vm); });
}

void Settlement::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Settlement::getName);
    cls.addFunc("newRuntime", [vm = cls.getHandle()](Settlement*) { return newRuntime(vm); });
}

}  // namespace eve::settlement
