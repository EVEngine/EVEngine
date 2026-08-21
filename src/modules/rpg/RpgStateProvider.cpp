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

double toNumber(const StateValue& v) { return v.isInt() ? double(v.asInt()) : v.asDouble(); }

/** @brief IStateProvider over every live RPGActor (cooldowns + casting). */
class RpgStateProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "rpg"; }

    bool captureState(StateValue& out) override { return captureRpgState(out); }
    bool restoreState(const StateValue& in, std::string* err) override { return restoreRpgState(in, err); }
    bool resetToDefaults() override { return resetRpgState(); }
};

struct Register {
    Register() {
        static RpgStateProvider provider;
        eve::cap::addListener<eve::caps::IStateProvider>(&provider);
    }
} g_register;

}  // namespace

bool captureRpgState(StateValue& out) {
    StateValue actors = StateValue::array();
    for (RPGActor* actor : RPGActor::liveActors()) {
        StateValue entry = StateValue::object();

        StateValue skills = StateValue::object();
        for (const auto& kv : actor->skills()->known) {
            skills.set(kv.first, StateValue::number(kv.second.cooldownRemaining));
        }
        entry.set("skills", std::move(skills));

        StateValue          casting = StateValue::object();
        const CastingState& c       = actor->skills()->casting;
        casting.set("active", StateValue::boolean(c.active));
        casting.set("skillId", StateValue::string(c.skillId));
        casting.set("remaining", StateValue::number(c.remaining));
        casting.set("totalCastTime", StateValue::number(c.totalCastTime));
        entry.set("casting", std::move(casting));

        actors.pushBack(std::move(entry));
    }
    out = std::move(actors);
    return true;
}

bool restoreRpgState(const StateValue& in, std::string* err) {
    if (!in.isArray()) {
        if (err) *err = "rpg: expected array of actors";
        return false;
    }
    const std::vector<RPGActor*>& actors = RPGActor::liveActors();
    const size_t                  n      = std::min(in.arraySize(), actors.size());
    for (size_t i = 0; i < n; ++i) {
        const StateValue& entry = in.at(i);
        if (!entry.isObject()) {
            if (err) *err = "rpg: actor state is not an object";
            return false;
        }
        RPGActor* actor = actors[i];

        if (const StateValue* skills = entry.find("skills"); skills && skills->isObject()) {
            for (const auto& key : skills->keys()) {
                const StateValue* v = skills->find(key);
                if (!v || !(v->isInt() || v->isFloat())) continue;
                if (SkillSystem::knows(actor, key)) {
                    SkillSystem::setCooldownRemaining(actor, key, static_cast<float>(toNumber(*v)));
                }
            }
        }

        CastingState& casting = actor->skills()->casting;
        casting               = CastingState();
        if (const StateValue* c = entry.find("casting"); c && c->isObject()) {
            const StateValue* active = c->find("active");
            if (active && active->isBool() && active->asBool()) {
                const StateValue* skillId = c->find("skillId");
                const std::string id      = skillId && skillId->isString() ? skillId->asString() : "";
                if (!id.empty() && SkillSystem::knows(actor, id)) {
                    casting.active        = true;
                    casting.skillId       = id;
                    casting.remaining     = 0.f;
                    casting.totalCastTime = 0.f;
                    if (const StateValue* r = c->find("remaining"); r && (r->isInt() || r->isFloat()))
                        casting.remaining = static_cast<float>(toNumber(*r));
                    if (const StateValue* t = c->find("totalCastTime"); t && (t->isInt() || t->isFloat()))
                        casting.totalCastTime = static_cast<float>(toNumber(*t));
                    casting.target = nullptr;  // pointers are not serializable
                }
            }
        }
    }
    return true;
}

bool resetRpgState() {
    for (RPGActor* actor : RPGActor::liveActors()) SkillSystem::cancelCast(actor);
    return true;
}

}  // namespace eve::rpg
