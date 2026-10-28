#include "rpg/RpgState.h"

#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "rpg/RPGActor.h"
#include "rpg/SkillSystem.h"
#include "rpg/SkillTypes.h"

#include <algorithm>
#include <string>
#include <utility>

namespace eve::rpg {
namespace {

double toNumber(const Value& v) { return v.isInt64() ? double(v.asInt()) : v.asDouble(); }

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
        Value::Object skills;
        for (const auto& kv : actor->skills()->known) {
            skills.emplace(kv.first, Value::number(kv.second.cooldownRemaining));
        }
        Value::Object       casting;
        const CastingState& c       = actor->skills()->casting;
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
        RPGActor* actor = actors[i];
        if (const Value* skills = entry.find("skills"); skills && skills->isObject()) {
            for (const auto& key : skills->keys()) {
                const Value* v = skills->find(key);
                if (!v || !v->isNumeric()) continue;
                if (SkillSystem::knows(actor, key)) {
                    pending[i].cooldowns.emplace_back(key, static_cast<float>(toNumber(*v)));
                }
            }
        }
        if (const Value* c = entry.find("casting"); c && c->isObject()) {
            const Value* active = c->find("active");
            if (active && active->isBool() && active->asBool()) {
                const Value*      skillId = c->find("skillId");
                const std::string id      = skillId && skillId->isString() ? skillId->asString() : "";
                if (!id.empty() && SkillSystem::knows(actor, id)) {
                    pending[i].casting.active  = true;
                    pending[i].casting.skillId = id;
                    if (const Value* r = c->find("remaining"); r && r->isNumeric())
                        pending[i].casting.remaining = static_cast<float>(toNumber(*r));
                    if (const Value* t = c->find("totalCastTime"); t && t->isNumeric())
                        pending[i].casting.totalCastTime = static_cast<float>(toNumber(*t));
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
