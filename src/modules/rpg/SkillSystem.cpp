#include "rpg/SkillSystem.h"
#include "rpg/SkillCondition.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/AttributeSystem.h"
#include "rpg/StatusSystem.h"

#include <unordered_map>

namespace eve::rpg {

namespace {

std::unordered_map<std::string, SkillSystem::CastCondition> &conditions() {
    static std::unordered_map<std::string, SkillSystem::CastCondition> c;
    return c;
}

std::vector<SkillCastEvent> &castQueue() {
    static std::vector<SkillCastEvent> q;
    return q;
}

bool checkCostsAffordable(RPGActor *actor, const SkillDefinition &def) {
    if (!def.cost) return true;
    for (const auto &cost : def.cost->items())
        if (AttributeSystem::getFinal(actor, cost.resource.value()) <
            static_cast<double>(cost.amount.value()))
            return false;
    return true;
}

void deductCosts(RPGActor *actor, const SkillDefinition &def) {
    if (!def.cost) return;
    for (const auto &cost : def.cost->items())
        AttributeSystem::modifyBase(actor, cost.resource.value(),
                                    -static_cast<double>(cost.amount.value()));
}

void resolveCast(RPGActor *actor, const SkillDefinition &def, RPGActor *target) {
    RPGActor *effectTarget = target ? target : actor;
    for (const std::string &effectId : def.grantedEffects)
        StatusSystem::apply(effectTarget, effectId, "skill:" + def.id)
            .ignore("skill effect application is reported through cast events");

    SkillCastEvent evt;
    evt.caster = actor;
    evt.target = target;
    evt.skillId = def.id;
    castQueue().push_back(std::move(evt));
}

}  // namespace

void SkillSystem::registerCastCondition(const std::string &name, CastCondition fn) {
    conditions()[name] = std::move(fn);
}

void SkillSystem::unregisterCastCondition(const std::string &name) { conditions().erase(name); }

void SkillSystem::clearCastConditions() { conditions().clear(); }

void SkillSystem::learn(RPGActor *actor, const std::string &skillId) {
    if (!actor || skillId.empty()) return;
    actor->skills()->known[skillId];  // default-constructs SkillRuntime if absent
}

bool SkillSystem::knows(RPGActor *actor, const std::string &skillId) {
    if (!actor) return false;
    auto &known = actor->skills()->known;
    return known.find(skillId) != known.end();
}

bool SkillSystem::forget(RPGActor *actor, const std::string &skillId) {
    if (!actor) return false;
    return actor->skills()->known.erase(skillId) > 0;
}

float SkillSystem::getCooldownRemaining(RPGActor *actor, const std::string &skillId) {
    if (!actor) return 0.f;
    auto &known = actor->skills()->known;
    auto it = known.find(skillId);
    return it == known.end() ? 0.f : it->second.cooldownRemaining;
}

void SkillSystem::setCooldownRemaining(RPGActor *actor, const std::string &skillId, float seconds) {
    if (!actor) return;
    actor->skills()->known[skillId].cooldownRemaining = seconds;
}

bool SkillSystem::canCast(RPGActor *actor, const std::string &skillId, std::string *reason) {
    auto fail = [&](const std::string &r) {
        if (reason) *reason = r;
        return false;
    };
    if (!actor) return fail("no_actor");
    const SkillDefinition *def = SkillRegistry::find(skillId);
    if (!def) return fail("unknown_skill");
    if (!knows(actor, skillId)) return fail("not_learned");
    if (isCasting(actor)) return fail("already_casting");
    if (getCooldownRemaining(actor, skillId) > 0.f) return fail("on_cooldown");
    if (!checkCostsAffordable(actor, *def)) return fail("insufficient_cost");

    for (auto &kv : conditions()) {
        std::string r;
        if (!kv.second(actor, *def, r)) return fail(r.empty() ? kv.first : r);
    }
    const auto condition = SkillConditionAdapter::evaluate(actor, *def, def->castCondition);
    if (!condition.passed()) return fail(decision::conditionReasonCodeName(condition.reasonCode()));
    return true;
}

bool SkillSystem::beginCast(RPGActor *actor, const std::string &skillId, RPGActor *target,
                             std::string *reason) {
    if (!canCast(actor, skillId, reason)) return false;
    const SkillDefinition *def = SkillRegistry::find(skillId);

    deductCosts(actor, *def);
    setCooldownRemaining(actor, skillId, def->cooldown);

    if (def->castTime <= 0.f) {
        resolveCast(actor, *def, target);
        return true;
    }

    CastingState &cs = actor->skills()->casting;
    cs.active = true;
    cs.skillId = skillId;
    cs.remaining = def->castTime;
    cs.totalCastTime = def->castTime;
    cs.target = target;
    return true;
}

void SkillSystem::cancelCast(RPGActor *actor) {
    if (!actor) return;
    actor->skills()->casting = CastingState{};
}

bool SkillSystem::isCasting(RPGActor *actor) { return actor && actor->skills()->casting.active; }

std::string SkillSystem::getCastingSkillId(RPGActor *actor) {
    return actor ? actor->skills()->casting.skillId : std::string{};
}

float SkillSystem::getCastProgress(RPGActor *actor) {
    if (!actor) return 0.f;
    const CastingState &cs = actor->skills()->casting;
    if (!cs.active || cs.totalCastTime <= 0.f) return 0.f;
    float p = 1.f - (cs.remaining / cs.totalCastTime);
    return p < 0.f ? 0.f : (p > 1.f ? 1.f : p);
}

void SkillSystem::update(float dt) {
    for (RPGActor *actor : RPGActor::liveActors()) {
        for (auto &kv : actor->skills()->known)
            if (kv.second.cooldownRemaining > 0.f) kv.second.cooldownRemaining -= dt;

        CastingState &cs = actor->skills()->casting;
        if (!cs.active) continue;
        cs.remaining -= dt;
        if (cs.remaining <= 0.f) {
            const SkillDefinition *def = SkillRegistry::find(cs.skillId);
            RPGActor *target = cs.target;
            cs.active = false;  // clear before resolveCast in case it re-enters casting
            if (def) resolveCast(actor, *def, target);
        }
    }
}

void SkillSystem::pollCastEvents(std::vector<SkillCastEvent> &out) {
    auto &q = castQueue();
    for (auto &e : q) out.push_back(std::move(e));
    q.clear();
}

}  // namespace eve::rpg
