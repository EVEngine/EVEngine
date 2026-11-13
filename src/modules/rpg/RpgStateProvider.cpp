#include "rpg/RpgState.h"

#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "rpg/RPGActor.h"
#include "rpg/SkillSystem.h"
#include "rpg/SkillTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::rpg {
namespace {

double toNumber(const Value& v) { return v.isInt64() ? double(v.asInt()) : v.asDouble(); }

Result<void> requireSkillComponent(const RPGActor& actor) {
    const auto* buffer = actor.getComponentManager().getComponentBuffer<RPGActor::Skills>();
    if (!buffer || !buffer->has(actor.id))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "RPG clock state requires a preconfigured Skills component"));
    return Result<void>::success();
}

/** @brief IStateProvider over every live RPGActor (cooldowns + casting). */
class RpgStateProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "rpg"; }

    Result<Value> capture() override { return captureRpgState(); }
    Result<void>  restore(const Value& in) override { return restoreRpgState(in); }
    Result<void>  resetToDefaults() override { return resetRpgStateResult(); }
};

struct Register {
    Register() {
        static RpgStateProvider provider;
        eve::cap::addListener<eve::caps::IStateProvider>(&provider);
    }
} g_register;

}  // namespace

Result<Value> captureRpgState() {
    Value::Array actors;
    for (RPGActor* actor : RPGActor::liveActors()) {
        auto configured = requireSkillComponent(*actor);
        if (!configured) return Result<Value>::failure(configured.status());
        Value::Object skills;
        for (const auto& kv : actor->skills()->known) {
            if (!std::isfinite(kv.second.cooldownRemaining) || kv.second.cooldownRemaining < 0)
                return Result<Value>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid RPG cooldown at capture"));
            skills.emplace(kv.first, Value::number(kv.second.cooldownRemaining));
        }
        Value::Object       casting;
        const CastingState& c       = actor->skills()->casting;
        if (c.active &&
            ((c.target.intent != CastTargetIntent::DefaultSelf && c.target.intent != CastTargetIntent::Retired) ||
             c.target.entity.table))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported, "RPG targeted cast hot reload requires explicit target rebinding"));
        if (c.active && (c.skillId.empty() || !actor->skills()->known.contains(c.skillId) ||
                         !std::isfinite(c.remaining) || !std::isfinite(c.totalCastTime) || c.remaining <= 0 ||
                         c.totalCastTime <= 0 || c.remaining > c.totalCastTime))
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid RPG active cast at capture"));
        casting.emplace("schema", Value("eve.rpg.casting-reload"));
        casting.emplace("version", Value(2));
        casting.emplace("target",
                        c.active && c.target.intent == CastTargetIntent::Retired ? Value("retired") : Value{});
        casting.emplace("active", Value::boolean(c.active));
        casting.emplace("skillId", Value::string(c.skillId));
        casting.emplace("remaining", Value::number(c.remaining));
        casting.emplace("totalCastTime", Value::number(c.totalCastTime));
        actors.emplace_back(
            Value::Object{{"skills", Value(std::move(skills))}, {"casting", Value(std::move(casting))}});
    }
    return Result<Value>::success(Value(std::move(actors)));
}

Result<void> restoreRpgState(const Value& in) {
    if (!in.isArray()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "rpg: expected array of actors"));
    }
    const std::vector<RPGActor*>& actors = RPGActor::liveActors();
    if (in.arraySize() != actors.size())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "rpg: actor count changed during reload"));
    struct Pending {
        std::vector<std::pair<std::string, float>> cooldowns;
        CastingState                               casting;
    };
    std::vector<Pending> pending(in.arraySize());
    for (size_t i = 0; i < in.arraySize(); ++i) {
        const Value& entry = in.at(i);
        if (!entry.isObject()) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "rpg: actor state is not an object"));
        }
        if (!entry.find("skills") || !entry.find("skills")->isObject() || !entry.find("casting") ||
            !entry.find("casting")->isObject())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "RPG actor clock participants are missing or malformed"));
        RPGActor* actor = actors[i];
        auto      configured = requireSkillComponent(*actor);
        if (!configured) return Result<void>::failure(configured.status());
        if (const Value* skills = entry.find("skills"); skills && skills->isObject()) {
            for (const auto& key : skills->keys()) {
                const Value* v = skills->find(key);
                if (!v || !v->isNumeric()) continue;
                if (SkillSystem::knows(actor, key)) {
                    const double seconds = toNumber(*v);
                    if (!std::isfinite(seconds) || seconds < 0 || seconds > std::numeric_limits<float>::max() ||
                        double(static_cast<float>(seconds)) != seconds)
                        return Result<void>::failure(
                            Diagnostic::error(DiagnosticCode::InvalidArgument,
                                              "RPG cooldown must be a finite nonnegative runtime float"));
                    pending[i].cooldowns.emplace_back(key, static_cast<float>(toNumber(*v)));
                }
            }
        }
        if (const Value* c = entry.find("casting"); c && c->isObject()) {
            CastTargetIntent intent = CastTargetIntent::DefaultSelf;
            if (const auto* version = c->find("version")) {
                if (!version->isInt64() || version->asInt() != 2)
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown RPG casting projection version"));
                const auto* target = c->find("target");
                if (!target || (!target->isNull() && (!target->isString() || target->asString() != "retired")))
                    return Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::Unsupported, "RPG casting projection requires self or retired intent"));
                if (target->isString()) intent = CastTargetIntent::Retired;
                const auto* schema = c->find("schema");
                if (!schema || !schema->isString() || schema->asString() != "eve.rpg.casting-reload")
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::Unsupported, "Unknown RPG casting projection schema"));
                if (c->keys().size() != 7 || !c->find("active") || !c->find("skillId") || !c->find("remaining") ||
                    !c->find("totalCastTime") || !c->find("target"))
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                   "Unknown or missing RPG casting projection fields"));
            }
            const Value* active = c->find("active");
            if (active && !active->isBool())
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "RPG casting active flag must be boolean"));
            if (active && active->isBool() && active->asBool()) {
                const auto* version = c->find("version");
                const auto* target  = c->find("target");
                if (!version || !version->isInt64() || version->asInt() != 2 || !target)
                    return Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::Unsupported, "RPG active cast reload requires an explicit v2 target context"));
                const Value*      skillId = c->find("skillId");
                const std::string id      = skillId && skillId->isString() ? skillId->asString() : "";
                if (id.empty() || !SkillSystem::knows(actor, id))
                    return Result<void>::failure(
                        Diagnostic::error(DiagnosticCode::NotFound, "RPG active cast skill is missing"));
                {
                    pending[i].casting.target.intent = intent;
                    pending[i].casting.active  = true;
                    pending[i].casting.skillId = id;
                    if (const Value* r = c->find("remaining"); r && r->isNumeric())
                        pending[i].casting.remaining = static_cast<float>(toNumber(*r));
                    if (const Value* t = c->find("totalCastTime"); t && t->isNumeric())
                        pending[i].casting.totalCastTime = static_cast<float>(toNumber(*t));
                    const auto& cast = pending[i].casting;
                    const auto* r    = c->find("remaining");
                    const auto* t    = c->find("totalCastTime");
                    if (!r || !t || !r->isNumeric() || !t->isNumeric() || !std::isfinite(cast.remaining) ||
                        !std::isfinite(cast.totalCastTime) || cast.remaining <= 0 || cast.totalCastTime <= 0 ||
                        cast.remaining > cast.totalCastTime || double(cast.remaining) != toNumber(*r) ||
                        double(cast.totalCastTime) != toNumber(*t))
                        return Result<void>::failure(
                            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid RPG active cast clocks"));
                }
            }
        }
    }
    for (size_t i = 0; i < actors.size(); ++i) {
        for (const auto& [skillId, cooldown] : pending[i].cooldowns)
            SkillSystem::setCooldownRemaining(actors[i], skillId, cooldown);
        actors[i]->skills()->casting = std::move(pending[i].casting);
    }
    return Result<void>::success();
}

Result<void> resetRpgStateResult() {
    for (RPGActor* actor : RPGActor::liveActors()) SkillSystem::cancelCast(actor);
    return Result<void>::success();
}

bool captureRpgState(StateValue& out) {
    auto captured = captureRpgState();
    if (!captured.ok()) return false;
    out = toLegacyStateValue(std::move(captured).takeValue());
    return true;
}

bool restoreRpgState(const StateValue& in, std::string* err) {
    auto result = restoreRpgState(toCanonicalStateValue(in));
    if (!result.ok()) {
        if (err) *err = result.diagnostics().front().message();
        return false;
    }
    return true;
}

bool resetRpgState() {
    auto result = resetRpgStateResult();
    if (!result.ok()) return false;
    return true;
}

}  // namespace eve::rpg
