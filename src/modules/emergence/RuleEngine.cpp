#include "emergence/RuleEngine.h"

#include "emergence/ConditionCodec.h"
#include "emergence/ConditionWatch.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/IEconomy.h"
#include "common/IEmergenceActionHandler.h"

#include <algorithm>
#include <utility>

namespace eve::emergence {
namespace {

template <class T = void>
eve::Result<T> fail(eve::DiagnosticCode code, const char* message, const char* path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, message, path ? path : "", {}, "emergence.rule_engine"));
}

std::int64_t asInt64(const eve::Value& value, std::int64_t fallback = 0) {
    if (value.isInt64()) return value.asInt();
    if (value.isDouble()) return static_cast<std::int64_t>(value.asDouble());
    return fallback;
}

const eve::Value* objField(const eve::Value::Object& object, std::string_view name) {
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &it->second;
}

}  // namespace

eve::Result<RuleDefinition> RuleEngine::validateAndNormalize(RuleDefinition rule) {
    if (rule.id.empty())
        return eve::Result<RuleDefinition>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "rule id must be non-empty", "id", {}, "emergence.rule_engine"));
    if (!rule.condition.isValid())
        return eve::Result<RuleDefinition>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "rule condition is invalid", "condition", {},
            "emergence.rule_engine"));
    for (const auto& action : rule.actions) {
        if (action.kind.empty())
            return eve::Result<RuleDefinition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "action kind must be non-empty", "actions.kind", {},
                "emergence.rule_engine"));
    }
    if (rule.watchKeys.empty()) {
        auto keys = collectWatchKeys(rule.condition);
        if (!keys) return eve::Result<RuleDefinition>::failure(keys.status());
        rule.watchKeys = std::move(keys).takeValue();
    } else {
        std::sort(rule.watchKeys.begin(), rule.watchKeys.end());
        rule.watchKeys.erase(std::unique(rule.watchKeys.begin(), rule.watchKeys.end()), rule.watchKeys.end());
        for (const auto& key : rule.watchKeys) {
            auto parsed = parseFactKey(key);
            if (!parsed)
                return eve::Result<RuleDefinition>::failure(parsed.status());
        }
    }
    if (rule.watchKeys.empty())
        return eve::Result<RuleDefinition>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "rule must declare at least one watch key", "watchKeys", {},
            "emergence.rule_engine"));
    return eve::Result<RuleDefinition>::success(std::move(rule));
}

eve::Result<int> RuleEngine::replaceCatalogue(std::vector<RuleDefinition> rules) {
    if (draining_)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "cannot replace catalogue during drain", {}, {},
            "emergence.rule_engine"));
    std::vector<RuleDefinition> next;
    next.reserve(rules.size());
    std::unordered_map<std::string, std::uint32_t> ids;
    WatchIndex                                    index;
    for (auto& rule : rules) {
        auto normalized = validateAndNormalize(std::move(rule));
        if (!normalized) return eve::Result<int>::failure(normalized.status());
        auto value = std::move(normalized).takeValue();
        if (ids.contains(value.id))
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::AlreadyExists, "duplicate rule id", "id", {}, "emergence.rule_engine"));
        const auto slot = static_cast<std::uint32_t>(next.size());
        ids.emplace(value.id, slot);
        index.addRule(slot, value.watchKeys);
        next.push_back(std::move(value));
    }
    rules_      = std::move(next);
    runtime_.assign(rules_.size(), RuleRuntime{});
    idToIndex_  = std::move(ids);
    watchIndex_ = std::move(index);
    dirty_.clear();
    activations_.clear();
    return eve::Result<int>::success(static_cast<int>(rules_.size()),
                                     eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> RuleEngine::replaceCatalogueJson(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed) return eve::Result<int>::failure(parsed.status());
    const auto* root = parsed.value().getIf<eve::Value::Object>();
    if (root == nullptr)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "rules document must be an object", {}, {},
            "emergence.rule_engine"));
    const auto* schema  = objField(*root, "schema");
    const auto* version = objField(*root, "version");
    if (schema == nullptr || !schema->isString() || schema->asString() != "eve.emergence.rules")
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "unexpected rules schema", "schema", {}, "emergence.rule_engine"));
    if (version == nullptr || !version->isInt64() || version->asInt() != 1)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::UnknownVersion, "unsupported rules version", "version", {}, "emergence.rule_engine"));
    const auto* rulesValue = objField(*root, "rules");
    const auto* rulesArray = rulesValue == nullptr ? nullptr : rulesValue->getIf<eve::Value::Array>();
    if (rulesArray == nullptr)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "rules must be an array", "rules", {}, "emergence.rule_engine"));

    std::vector<RuleDefinition> rules;
    rules.reserve(rulesArray->size());
    for (std::size_t i = 0; i < rulesArray->size(); ++i) {
        const auto* object = (*rulesArray)[i].getIf<eve::Value::Object>();
        if (object == nullptr)
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "rule entry must be an object", "rules", {},
                "emergence.rule_engine"));
        RuleDefinition rule;
        const auto*    id = objField(*object, "id");
        if (id == nullptr || !id->isString() || id->asString().empty())
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "rule id must be a non-empty string", "rules.id", {},
                "emergence.rule_engine"));
        rule.id = id->asString();
        if (const auto* priority = objField(*object, "priority")) rule.priority = static_cast<int>(asInt64(*priority));
        if (const auto* once = objField(*object, "once")) {
            if (!once->isBool())
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "once must be a boolean", "rules.once", {},
                    "emergence.rule_engine"));
            rule.once = once->asBool();
        }
        if (const auto* cooldown = objField(*object, "cooldownTicks"))
            rule.cooldownTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(0, asInt64(*cooldown)));
        if (const auto* fireMode = objField(*object, "fireMode")) {
            if (!fireMode->isString())
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "fireMode must be a string", "rules.fireMode", {},
                    "emergence.rule_engine"));
            if (fireMode->asString() == "rising")
                rule.fireMode = FireMode::Rising;
            else if (fireMode->asString() == "level")
                rule.fireMode = FireMode::Level;
            else
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "unknown fireMode", "rules.fireMode", {},
                    "emergence.rule_engine"));
        }
        const auto* conditionValue = objField(*object, "condition");
        if (conditionValue == nullptr)
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "rule requires condition", "rules.condition", {},
                "emergence.rule_engine"));
        auto condition = decodeCondition(*conditionValue);
        if (!condition) return eve::Result<int>::failure(condition.status());
        rule.condition = std::move(condition).takeValue();
        if (const auto* watch = objField(*object, "watchKeys")) {
            const auto* array = watch->getIf<eve::Value::Array>();
            if (array == nullptr)
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "watchKeys must be an array", "rules.watchKeys", {},
                    "emergence.rule_engine"));
            for (const auto& item : *array) {
                if (!item.isString() || item.asString().empty())
                    return eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "watch key must be a non-empty string",
                        "rules.watchKeys", {}, "emergence.rule_engine"));
                rule.watchKeys.push_back(item.asString());
            }
        }
        if (const auto* actions = objField(*object, "actions")) {
            const auto* array = actions->getIf<eve::Value::Array>();
            if (array == nullptr)
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "actions must be an array", "rules.actions", {},
                    "emergence.rule_engine"));
            for (const auto& item : *array) {
                const auto* actionObject = item.getIf<eve::Value::Object>();
                if (actionObject == nullptr)
                    return eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "action must be an object", "rules.actions", {},
                        "emergence.rule_engine"));
                EmergenceAction action;
                const auto*    kind = objField(*actionObject, "kind");
                if (kind == nullptr || !kind->isString() || kind->asString().empty())
                    return eve::Result<int>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "action kind must be a non-empty string",
                        "rules.actions.kind", {}, "emergence.rule_engine"));
                action.kind = kind->asString();
                if (const auto* args = objField(*actionObject, "args")) action.args = *args;
                rule.actions.push_back(std::move(action));
            }
        }
        rules.push_back(std::move(rule));
    }
    return replaceCatalogue(std::move(rules));
}

void RuleEngine::clear() {
    facts_.clear();
    watchIndex_.clear();
    rules_.clear();
    runtime_.clear();
    idToIndex_.clear();
    dirty_.clear();
    activations_.clear();
    nextSequence_         = 1;
    lastDrainEvaluations_ = 0;
    wakeKeyEvents_        = 0;
}

bool RuleEngine::contains(std::string_view ruleId) const { return idToIndex_.contains(std::string(ruleId)); }

const RuleDefinition* RuleEngine::find(std::string_view ruleId) const {
    auto it = idToIndex_.find(std::string(ruleId));
    if (it == idToIndex_.end()) return nullptr;
    return &rules_[it->second];
}

void RuleEngine::wakeKey(std::string_view key) {
    std::vector<std::uint32_t> hit;
    if (watchIndex_.collect(key, hit) == 0) return;
    ++wakeKeyEvents_;
    for (const auto index : hit) dirty_.insert(index);
}

eve::Result<bool> RuleEngine::setValue(std::string key, eve::Value value) {
    if (key.empty()) return fail<bool>(eve::DiagnosticCode::InvalidArgument, "value key must be non-empty", "key");
    const bool changed = facts_.setValue(key, std::move(value));
    if (changed) wakeKey(makeFactKey(FactDomain::Value, key));
    return eve::Result<bool>::success(changed,
                                      eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<bool> RuleEngine::setTag(std::string tag, bool present) {
    if (tag.empty()) return fail<bool>(eve::DiagnosticCode::InvalidArgument, "tag must be non-empty", "tag");
    const bool changed = facts_.setTag(tag, present);
    if (changed) wakeKey(makeFactKey(FactDomain::Tag, tag));
    return eve::Result<bool>::success(changed,
                                      eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<bool> RuleEngine::setAttribute(std::string key, eve::Value value) {
    if (key.empty())
        return fail<bool>(eve::DiagnosticCode::InvalidArgument, "attribute key must be non-empty", "key");
    const bool changed = facts_.setAttribute(key, std::move(value));
    if (changed) wakeKey(makeFactKey(FactDomain::Attribute, key));
    return eve::Result<bool>::success(changed,
                                      eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<bool> RuleEngine::setResource(std::string key, eve::Value value) {
    if (key.empty()) return fail<bool>(eve::DiagnosticCode::InvalidArgument, "resource key must be non-empty", "key");
    const bool changed = facts_.setResource(key, std::move(value));
    if (changed) wakeKey(makeFactKey(FactDomain::Resource, key));
    return eve::Result<bool>::success(changed,
                                      eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<bool> RuleEngine::setState(std::string key, eve::Value value) {
    if (key.empty()) return fail<bool>(eve::DiagnosticCode::InvalidArgument, "state key must be non-empty", "key");
    const bool changed = facts_.setState(key, std::move(value));
    if (changed) wakeKey(makeFactKey(FactDomain::State, key));
    return eve::Result<bool>::success(changed,
                                      eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<bool> RuleEngine::setAuthority(std::string scope, bool granted) {
    if (scope.empty())
        return fail<bool>(eve::DiagnosticCode::InvalidArgument, "authority scope must be non-empty", "scope");
    const bool changed = facts_.setAuthority(scope, granted);
    if (changed) wakeKey(makeFactKey(FactDomain::Authority, scope));
    return eve::Result<bool>::success(changed,
                                      eve::Status::success(changed ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<void> RuleEngine::executeOne(const EmergenceAction& action, std::uint64_t /*tick*/) {
    if (action.kind == "fact.set") {
        const auto* object = action.args.getIf<eve::Value::Object>();
        if (object == nullptr)
            return fail(eve::DiagnosticCode::InvalidArgument, "fact.set args must be an object", "args");
        const auto* domain = objField(*object, "domain");
        const auto* key    = objField(*object, "key");
        const auto* value  = objField(*object, "value");
        if (domain == nullptr || !domain->isString() || key == nullptr || !key->isString() || value == nullptr)
            return fail(eve::DiagnosticCode::InvalidArgument, "fact.set requires domain, key, value", "args");
        const auto& domainText = domain->asString();
        const auto& keyText    = key->asString();
        if (domainText == "value") return setValue(keyText, *value).andThen([](bool) { return eve::Result<void>::success(); });
        if (domainText == "tag") {
            if (!value->isBool()) return fail(eve::DiagnosticCode::InvalidArgument, "tag value must be bool", "value");
            return setTag(keyText, value->asBool()).andThen([](bool) { return eve::Result<void>::success(); });
        }
        if (domainText == "attr")
            return setAttribute(keyText, *value).andThen([](bool) { return eve::Result<void>::success(); });
        if (domainText == "resource")
            return setResource(keyText, *value).andThen([](bool) { return eve::Result<void>::success(); });
        if (domainText == "state")
            return setState(keyText, *value).andThen([](bool) { return eve::Result<void>::success(); });
        if (domainText == "authority") {
            if (!value->isBool())
                return fail(eve::DiagnosticCode::InvalidArgument, "authority value must be bool", "value");
            return setAuthority(keyText, value->asBool()).andThen([](bool) { return eve::Result<void>::success(); });
        }
        return fail(eve::DiagnosticCode::InvalidArgument, "unknown fact.set domain", "domain");
    }
    if (action.kind == "economy.credit" || action.kind == "economy.debit") {
        auto* economy = eve::cap::query<eve::economy::IEconomy>();
        if (economy == nullptr)
            return fail(eve::DiagnosticCode::PreconditionViolation, "IEconomy provider is absent", action.kind.c_str());
        const auto* object = action.args.getIf<eve::Value::Object>();
        if (object == nullptr)
            return fail(eve::DiagnosticCode::InvalidArgument, "economy action args must be an object", "args");
        const auto* player = objField(*object, "player");
        const auto* type   = objField(*object, "type");
        const auto* amount = objField(*object, "amount");
        if (player == nullptr || type == nullptr || !type->isString() || amount == nullptr)
            return fail(eve::DiagnosticCode::InvalidArgument, "economy action requires player, type, amount", "args");
        const int playerId = static_cast<int>(asInt64(*player));
        const int qty      = static_cast<int>(asInt64(*amount));
        if (qty < 0) return fail(eve::DiagnosticCode::InvalidArgument, "amount must be non-negative", "amount");
        if (action.kind == "economy.credit") {
            economy->credit(playerId, type->asString(), qty);
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        if (!economy->debit(playerId, type->asString(), qty))
            return fail(eve::DiagnosticCode::Conflict, "economy debit rejected", "amount");
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    if (action.kind == "emit") {
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> handlerFailure = eve::Result<void>::success();
    bool              consumed       = false;
    eve::cap::forEach<eve::IEmergenceActionHandler>([&](eve::IEmergenceActionHandler* handler) {
        if (consumed || !handlerFailure.ok()) return;
        auto handled = handler->tryHandle(action.kind, action.args);
        if (!handled) {
            handlerFailure = eve::Result<void>::failure(handled.status());
            return;
        }
        if (handled.value()) consumed = true;
    });
    if (!handlerFailure.ok()) return handlerFailure;
    if (!consumed && action.kind != "emit") {
        // Unhandled domain actions remain in the activation log for scripts.
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> RuleEngine::executeActions(const std::vector<EmergenceAction>& actions, std::uint64_t tick) {
    for (const auto& action : actions) {
        auto executed = executeOne(action, tick);
        if (!executed) return executed;
    }
    return eve::Result<void>::success();
}

eve::Result<int> RuleEngine::drain(std::uint64_t tick) {
    if (draining_)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "nested drain is not allowed", {}, {},
            "emergence.rule_engine"));
    draining_             = true;
    lastDrainEvaluations_ = 0;
    int produced          = 0;

    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{draining_};

    // Fixed-point: evaluate current dirty set, execute activations (which may dirty more), repeat.
    constexpr int kMaxPasses = 64;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        if (dirty_.empty()) break;
        std::vector<std::uint32_t> batch(dirty_.begin(), dirty_.end());
        dirty_.clear();
        std::sort(batch.begin(), batch.end(), [&](std::uint32_t left, std::uint32_t right) {
            if (rules_[left].priority != rules_[right].priority)
                return rules_[left].priority > rules_[right].priority;
            return rules_[left].id < rules_[right].id;
        });

        std::vector<Activation> pending;
        for (const auto index : batch) {
            if (index >= rules_.size()) continue;
            auto&       runtime = runtime_[index];
            const auto& rule    = rules_[index];
            if (runtime.disabled) continue;
            if (runtime.cooldownUntil > tick) continue;
            ++lastDrainEvaluations_;
            const auto result    = rule.condition.evaluate(facts_);
            const bool nowPassed = result.passed();
            bool       fire      = false;
            if (rule.fireMode == FireMode::Rising)
                fire = !runtime.passed && nowPassed;
            else
                fire = nowPassed;
            runtime.passed = nowPassed;
            if (!fire) continue;
            Activation activation;
            activation.sequence = nextSequence_++;
            activation.tick     = tick;
            activation.ruleId   = rule.id;
            activation.priority = rule.priority;
            activation.actions  = rule.actions;
            pending.push_back(std::move(activation));
            runtime.fired = true;
            if (rule.once) runtime.disabled = true;
            if (rule.cooldownTicks > 0) runtime.cooldownUntil = tick + rule.cooldownTicks;
        }

        for (auto& activation : pending) {
            auto executed = executeActions(activation.actions, tick);
            if (!executed) {
                draining_ = false;
                return eve::Result<int>::failure(executed.status());
            }
            activations_.push_back(std::move(activation));
            ++produced;
        }
    }
    if (!dirty_.empty())
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "emergence drain exceeded cascade pass budget", {}, {},
            "emergence.rule_engine"));
    return eve::Result<int>::success(produced, eve::Status::success(produced > 0 ? eve::StatusCode::Applied
                                                                                : eve::StatusCode::NoOp));
}

const Activation* RuleEngine::activationAt(int index) const {
    if (index < 0 || index >= static_cast<int>(activations_.size())) return nullptr;
    return &activations_[static_cast<std::size_t>(index)];
}

void RuleEngine::clearActivations() { activations_.clear(); }

eve::Result<void> RuleEngine::resetRule(std::string_view ruleId) {
    auto it = idToIndex_.find(std::string(ruleId));
    if (it == idToIndex_.end())
        return fail(eve::DiagnosticCode::NotFound, "rule id not found", "ruleId");
    auto& runtime        = runtime_[it->second];
    runtime.disabled     = false;
    runtime.fired        = false;
    runtime.passed       = false;
    runtime.cooldownUntil = 0;
    dirty_.insert(it->second);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::string RuleEngine::snapshotJson() const {
    eve::Value::Object root;
    root.emplace("schema", eve::Value("eve.emergence.runtime"));
    root.emplace("version", eve::Value(std::int64_t{1}));
    auto factsJson = eve::Value::fromJson(facts_.snapshotJson());
    if (factsJson) root.emplace("facts", std::move(factsJson).takeValue());
    eve::Value::Array rules;
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        eve::Value::Object row;
        row.emplace("id", eve::Value(rules_[i].id));
        row.emplace("passed", eve::Value(runtime_[i].passed));
        row.emplace("fired", eve::Value(runtime_[i].fired));
        row.emplace("disabled", eve::Value(runtime_[i].disabled));
        row.emplace("cooldownUntil", eve::Value(static_cast<std::int64_t>(runtime_[i].cooldownUntil)));
        rules.push_back(eve::Value(std::move(row)));
    }
    root.emplace("rules", eve::Value(std::move(rules)));
    auto encoded = eve::Value(std::move(root)).toJson();
    return encoded ? std::move(encoded).takeValue() : std::string("{}");
}

eve::Result<void> RuleEngine::restoreJson(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed) return eve::Result<void>::failure(parsed.status());
    const auto* root = parsed.value().getIf<eve::Value::Object>();
    if (root == nullptr)
        return fail(eve::DiagnosticCode::InvalidArgument, "runtime snapshot must be an object");
    const auto* schema  = objField(*root, "schema");
    const auto* version = objField(*root, "version");
    if (schema == nullptr || !schema->isString() || schema->asString() != "eve.emergence.runtime")
        return fail(eve::DiagnosticCode::InvalidArgument, "unexpected runtime schema", "schema");
    if (version == nullptr || !version->isInt64() || version->asInt() != 1)
        return fail(eve::DiagnosticCode::UnknownVersion, "unsupported runtime version", "version");

    FactStore nextFacts = facts_;
    if (const auto* facts = objField(*root, "facts")) {
        auto encoded = facts->toJson();
        if (!encoded) return eve::Result<void>::failure(encoded.status());
        auto restored = nextFacts.restoreJson(encoded.value());
        if (!restored) return restored;
    }
    std::vector<RuleRuntime> nextRuntime = runtime_;
    if (const auto* rules = objField(*root, "rules")) {
        const auto* array = rules->getIf<eve::Value::Array>();
        if (array == nullptr) return fail(eve::DiagnosticCode::InvalidArgument, "rules must be an array", "rules");
        for (const auto& item : *array) {
            const auto* object = item.getIf<eve::Value::Object>();
            if (object == nullptr) return fail(eve::DiagnosticCode::InvalidArgument, "rule latch must be an object");
            const auto* id = objField(*object, "id");
            if (id == nullptr || !id->isString())
                return fail(eve::DiagnosticCode::InvalidArgument, "rule latch requires id", "id");
            auto it = idToIndex_.find(id->asString());
            if (it == idToIndex_.end())
                return fail(eve::DiagnosticCode::NotFound, "runtime rule id missing from catalogue", "id");
            auto& runtime = nextRuntime[it->second];
            if (const auto* passed = objField(*object, "passed")) {
                if (!passed->isBool()) return fail(eve::DiagnosticCode::InvalidArgument, "passed must be bool");
                runtime.passed = passed->asBool();
            }
            if (const auto* fired = objField(*object, "fired")) {
                if (!fired->isBool()) return fail(eve::DiagnosticCode::InvalidArgument, "fired must be bool");
                runtime.fired = fired->asBool();
            }
            if (const auto* disabled = objField(*object, "disabled")) {
                if (!disabled->isBool()) return fail(eve::DiagnosticCode::InvalidArgument, "disabled must be bool");
                runtime.disabled = disabled->asBool();
            }
            if (const auto* cooldown = objField(*object, "cooldownUntil"))
                runtime.cooldownUntil = static_cast<std::uint64_t>(std::max<std::int64_t>(0, asInt64(*cooldown)));
        }
    }
    facts_   = std::move(nextFacts);
    runtime_ = std::move(nextRuntime);
    dirty_.clear();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::emergence
