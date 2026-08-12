#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/AttributeTypes.h"
#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/Effect.h"
#include "rpg/StatusSystem.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/Settlement.h"
#include "rpg/RPG.h"

#include <cmath>
#include <string>
#include <vector>

using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

// ---------------------------------------------------------------------------
// Attribute math (pure function, no ECS)
// ---------------------------------------------------------------------------

TEST_CASE("rpg.attribute.pureCompute.addMulAddMulMul") {
    AttributeValue av;
    av.base = 100.0;
    av.modifiers.push_back({"m1", "src", "add", 10.0, 0});
    av.modifiers.push_back({"m2", "src", "mulAdd", 0.5, 0});  // +50%
    av.modifiers.push_back({"m3", "src", "mulMul", 0.1, 0});  // *1.1
    // (100 + 10) * (1 + 0.5) * (1 + 0.1) = 181.5
    CHECK(approxEq(computeAttributeValue(av), 181.5));
}

TEST_CASE("rpg.attribute.pureCompute.overridePriorityWins") {
    AttributeValue av;
    av.base = 5.0;
    av.modifiers.push_back({"a", "src", "override", 50.0, 5});
    av.modifiers.push_back({"b", "src", "override", 20.0, 1});  // lower priority: applied first
    // sequential ascending priority: override 20 first, then override 50 -> final 50
    CHECK_EQ(computeAttributeValue(av), 50.0);
}

TEST_CASE("rpg.attribute.pureCompute.clamp") {
    AttributeValue av;
    av.base = 200.0;
    av.modifiers.push_back({"a", "src", "clampMax", 100.0, 0});
    av.modifiers.push_back({"b", "src", "clampMin", 10.0, 0});
    CHECK_EQ(computeAttributeValue(av), 100.0);
}

TEST_CASE("rpg.attribute.pureCompute.customOp") {
    AttributeOpTable table;
    table.registerOp("square", [](double, double value) { return value * value; });
    AttributeValue av;
    av.base = 1.0;
    av.modifiers.push_back({"a", "src", "square", 4.0, 0});
    CHECK_EQ(computeAttributeValue(av, &table), 16.0);

    // Unknown custom op falls back to leaving result untouched.
    AttributeValue av2;
    av2.base = 7.0;
    av2.modifiers.push_back({"a", "src", "totallyUnknownOp", 4.0, 0});
    CHECK_EQ(computeAttributeValue(av2, &table), 7.0);
}

// ---------------------------------------------------------------------------
// AttributeSystem (actor-facing, with caching)
// ---------------------------------------------------------------------------

TEST_CASE("rpg.attributeSystem.setGetFinalWithModifiers") {
    RPGActor *actor = RPGActor::createActor();

    actor->setBaseAttribute("strength", 10.0);
    CHECK_EQ(actor->getBaseAttribute("strength"), 10.0);
    CHECK(actor->hasAttribute("strength"));
    CHECK(!actor->hasAttribute("nope"));
    CHECK_EQ(actor->getFinalAttribute("strength"), 10.0);

    std::string modId = actor->addAttributeModifier("strength", "gear:sword", "add", 5.0);
    CHECK(!modId.empty());
    CHECK_EQ(actor->getFinalAttribute("strength"), 15.0);

    actor->modifyBaseAttribute("strength", 2.0);
    CHECK_EQ(actor->getBaseAttribute("strength"), 12.0);
    CHECK_EQ(actor->getFinalAttribute("strength"), 17.0);

    CHECK(actor->removeAttributeModifier("strength", modId));
    CHECK_EQ(actor->getFinalAttribute("strength"), 12.0);
    // Removing again is a no-op.
    CHECK(!actor->removeAttributeModifier("strength", modId));

    actor->release();
}

TEST_CASE("rpg.attributeSystem.removeBySource") {
    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->addAttributeModifier("hp", "buffA", "add", 10.0);
    actor->addAttributeModifier("hp", "buffA", "add", 5.0);
    actor->addAttributeModifier("mana", "buffA", "add", 20.0);
    actor->setBaseAttribute("mana", 50.0);

    CHECK_EQ(actor->getFinalAttribute("hp"), 115.0);
    CHECK_EQ(actor->removeAttributeModifiersBySource("hp", "buffA"), 2);
    CHECK_EQ(actor->getFinalAttribute("hp"), 100.0);
    CHECK_EQ(actor->getFinalAttribute("mana"), 70.0);

    CHECK_EQ(actor->removeAllAttributeModifiersBySource("buffA"), 1);
    CHECK_EQ(actor->getFinalAttribute("mana"), 50.0);

    actor->release();
}

// ---------------------------------------------------------------------------
// Effect registry
// ---------------------------------------------------------------------------

TEST_CASE("rpg.effect.registryCRUD") {
    EffectDefinition def;
    def.id = "test.effect.crud";
    def.durationPolicy = "duration";
    def.duration = 3.f;
    def.modifiers.push_back({"attack", "mulAdd", 0.2, 0});
    def.tags = {"buff"};
    EffectRegistry::registerEffect(def);

    const EffectDefinition *found = EffectRegistry::find("test.effect.crud");
    REQUIRE(found != nullptr);
    CHECK_EQ(found->duration, 3.f);
    CHECK(found->hasTag("buff"));

    CHECK(EffectRegistry::remove("test.effect.crud"));
    CHECK(EffectRegistry::find("test.effect.crud") == nullptr);
}

TEST_CASE("rpg.effect.loadFromJsonArray") {
    const char *json = R"([
        {"id":"test.effect.json.a","durationPolicy":"duration","duration":2,
         "modifiers":[{"attribute":"speed","op":"add","value":3}]},
        {"id":"test.effect.json.b","durationPolicy":"infinite","period":1,
         "stackPolicy":"stack","maxStacks":3,"tags":["dot","poison"]}
    ])";
    std::string err;
    int n = EffectRegistry::loadFromJson(json, &err);
    CHECK_EQ(n, 2);
    CHECK(err.empty());

    const EffectDefinition *a = EffectRegistry::find("test.effect.json.a");
    REQUIRE(a != nullptr);
    CHECK_EQ(a->duration, 2.f);
    REQUIRE(a->modifiers.size() == 1);
    CHECK_EQ(a->modifiers[0].attribute, "speed");

    const EffectDefinition *b = EffectRegistry::find("test.effect.json.b");
    REQUIRE(b != nullptr);
    CHECK_EQ(b->period, 1.f);
    CHECK_EQ(b->maxStacks, 3);
    CHECK(b->hasTag("poison"));

    EffectRegistry::remove("test.effect.json.a");
    EffectRegistry::remove("test.effect.json.b");
}

TEST_CASE("rpg.effect.loadFromJsonExtraAndGetExtra") {
    const char *json = R"({
        "id":"test.effect.extra","durationPolicy":"duration","duration":3,
        "tags":["buff"],
        "extra":{"icon":"ui/power.png","vfx":"fx.glow"}
    })";
    std::string err;
    CHECK_EQ(EffectRegistry::loadFromJson(json, &err), 1);
    CHECK(err.empty());

    const EffectDefinition *def = EffectRegistry::find("test.effect.extra");
    REQUIRE(def != nullptr);
    CHECK_EQ(def->getExtra("icon"), "ui/power.png");
    CHECK_EQ(def->getExtra("vfx"), "fx.glow");
    CHECK_EQ(def->getExtra("missing", "fallback"), "fallback");

    EffectRegistry::remove("test.effect.extra");
}

// ---------------------------------------------------------------------------
// StatusSystem: instant / duration / stacking / periodic ticks
// ---------------------------------------------------------------------------

TEST_CASE("rpg.status.instantEffectAppliesImmediatelyNoInstance") {
    EffectDefinition heal;
    heal.id = "test.status.instantHeal";
    heal.durationPolicy = "instant";
    heal.modifiers.push_back({"health", "add", 25.0, 0});
    EffectRegistry::registerEffect(heal);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("health", 50.0);

    int result = actor->applyEffect("test.status.instantHeal");
    CHECK_EQ(result, 0);
    CHECK_EQ(actor->getBaseAttribute("health"), 75.0);
    CHECK_EQ(actor->getStatusCount(), 0);

    actor->release();
    EffectRegistry::remove("test.status.instantHeal");
}

TEST_CASE("rpg.status.durationBuffAppliesAndExpires") {
    EffectDefinition buff;
    buff.id = "test.status.durationBuff";
    buff.durationPolicy = "duration";
    buff.duration = 1.0f;
    buff.modifiers.push_back({"attack", "add", 10.0, 0});
    EffectRegistry::registerEffect(buff);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 20.0);

    int instanceId = actor->applyEffect("test.status.durationBuff", "test-source");
    CHECK(instanceId > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 30.0);
    CHECK_EQ(actor->getStatusCount(), 1);
    CHECK(actor->hasEffect("test.status.durationBuff"));

    StatusSystem::update(0.5f);
    CHECK_EQ(actor->getStatusCount(), 1);
    CHECK_EQ(actor->getFinalAttribute("attack"), 30.0);

    StatusSystem::update(0.6f);  // total elapsed 1.1s > duration 1.0s
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 20.0);
    CHECK(!actor->hasEffect("test.status.durationBuff"));

    actor->release();
    EffectRegistry::remove("test.status.durationBuff");
}

TEST_CASE("rpg.status.stackPolicyNoneRejectsDuplicate") {
    EffectDefinition def;
    def.id = "test.status.stackNone";
    def.durationPolicy = "infinite";
    def.stackPolicy = "none";
    EffectRegistry::registerEffect(def);

    RPGActor *actor = RPGActor::createActor();
    int id1 = actor->applyEffect("test.status.stackNone");
    CHECK(id1 > 0);
    int id2 = actor->applyEffect("test.status.stackNone");
    CHECK_EQ(id2, -1);
    CHECK_EQ(actor->getStatusCount(), 1);

    actor->removeStatusByEffect("test.status.stackNone");
    actor->release();
    EffectRegistry::remove("test.status.stackNone");
}

TEST_CASE("rpg.status.stackPolicyRefreshAndExtend") {
    EffectDefinition refreshDef;
    refreshDef.id = "test.status.refresh";
    refreshDef.durationPolicy = "duration";
    refreshDef.duration = 2.0f;
    refreshDef.stackPolicy = "refresh";
    EffectRegistry::registerEffect(refreshDef);

    EffectDefinition extendDef;
    extendDef.id = "test.status.extend";
    extendDef.durationPolicy = "duration";
    extendDef.duration = 2.0f;
    extendDef.stackPolicy = "extend";
    EffectRegistry::registerEffect(extendDef);

    RPGActor *actor = RPGActor::createActor();

    int r1 = actor->applyEffect("test.status.refresh");
    StatusSystem::update(1.5f);
    CHECK(approxEq(actor->getStatusRemaining(0), 0.5, 1e-4));
    int r2 = actor->applyEffect("test.status.refresh");
    CHECK_EQ(r1, r2);
    CHECK(approxEq(actor->getStatusRemaining(0), 2.0, 1e-4));

    actor->removeStatusByEffect("test.status.refresh");

    int e1 = actor->applyEffect("test.status.extend");
    StatusSystem::update(1.0f);
    int e2 = actor->applyEffect("test.status.extend");
    CHECK_EQ(e1, e2);
    // remaining was ~1.0 before extend, +2.0 duration = ~3.0
    CHECK(approxEq(actor->getStatusRemaining(0), 3.0, 1e-4));

    actor->removeStatusByEffect("test.status.extend");
    actor->release();
    EffectRegistry::remove("test.status.refresh");
    EffectRegistry::remove("test.status.extend");
}

TEST_CASE("rpg.status.stackPolicyStackScalesModifierAndCaps") {
    EffectDefinition def;
    def.id = "test.status.stack";
    def.durationPolicy = "infinite";
    def.stackPolicy = "stack";
    def.maxStacks = 2;
    def.modifiers.push_back({"armor", "add", 5.0, 0});
    EffectRegistry::registerEffect(def);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("armor", 0.0);

    int id1 = actor->applyEffect("test.status.stack");
    CHECK_EQ(actor->getFinalAttribute("armor"), 5.0);
    CHECK_EQ(actor->getStatusStacks(0), 1);

    int id2 = actor->applyEffect("test.status.stack");
    CHECK_EQ(id1, id2);
    CHECK_EQ(actor->getStatusStacks(0), 2);
    CHECK_EQ(actor->getFinalAttribute("armor"), 10.0);

    // Already at maxStacks: stays capped.
    actor->applyEffect("test.status.stack");
    CHECK_EQ(actor->getStatusStacks(0), 2);
    CHECK_EQ(actor->getFinalAttribute("armor"), 10.0);

    actor->removeStatus(id1);
    CHECK_EQ(actor->getFinalAttribute("armor"), 0.0);
    CHECK_EQ(actor->getStatusCount(), 0);

    actor->release();
    EffectRegistry::remove("test.status.stack");
}

TEST_CASE("rpg.status.periodicEffectProducesTickEvents") {
    EffectDefinition dot;
    dot.id = "test.status.periodicDot";
    dot.durationPolicy = "duration";
    dot.duration = 2.5f;
    dot.period = 1.0f;
    EffectRegistry::registerEffect(dot);

    RPGActor *actor = RPGActor::createActor();
    int instanceId = actor->applyEffect("test.status.periodicDot", "caster-1");
    CHECK(instanceId > 0);
    // Periodic effects do not touch attributes directly.
    CHECK(!actor->hasAttribute("health"));

    std::vector<StatusTickEvent> ticks;
    StatusSystem::update(1.0f);
    StatusSystem::pollTicks(ticks);
    REQUIRE(ticks.size() == 1);
    CHECK_EQ(ticks[0].actor, actor);
    CHECK_EQ(ticks[0].effectId, "test.status.periodicDot");
    CHECK_EQ(ticks[0].source, "caster-1");

    ticks.clear();
    StatusSystem::update(1.0f);  // t=2.0 -> another tick
    StatusSystem::pollTicks(ticks);
    REQUIRE(ticks.size() == 1);

    ticks.clear();
    StatusSystem::update(1.0f);  // t=3.0 > duration 2.5 -> expires; no more ticks after expiry
    StatusSystem::pollTicks(ticks);
    CHECK(ticks.size() <= 1);  // may or may not tick exactly at expiry boundary depending on order
    CHECK_EQ(actor->getStatusCount(), 0);

    ticks.clear();
    StatusSystem::update(5.0f);
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 0u);  // expired: no further ticks ever

    actor->release();
    EffectRegistry::remove("test.status.periodicDot");
}

TEST_CASE("rpg.status.removeByTagAndBySource") {
    EffectDefinition a;
    a.id = "test.status.tagA";
    a.durationPolicy = "infinite";
    a.tags = {"debuff", "curse"};
    EffectRegistry::registerEffect(a);

    EffectDefinition b;
    b.id = "test.status.tagB";
    b.durationPolicy = "infinite";
    b.tags = {"debuff"};
    EffectRegistry::registerEffect(b);

    RPGActor *actor = RPGActor::createActor();
    actor->applyEffect("test.status.tagA", "dispellSrc");
    actor->applyEffect("test.status.tagB", "otherSrc");
    CHECK_EQ(actor->getStatusCount(), 2);

    CHECK_EQ(actor->removeStatusByTag("curse"), 1);
    CHECK_EQ(actor->getStatusCount(), 1);
    CHECK(!actor->hasEffect("test.status.tagA"));

    CHECK_EQ(actor->removeStatusBySource("otherSrc"), 1);
    CHECK_EQ(actor->getStatusCount(), 0);

    actor->release();
    EffectRegistry::remove("test.status.tagA");
    EffectRegistry::remove("test.status.tagB");
}

TEST_CASE("rpg.buff.applyConditionRejectsAndEmitsChange") {
    // Drain any leftover change events from prior cases.
    std::vector<StatusChangeEvent> drain;
    StatusSystem::pollChanges(drain);

    EffectDefinition buff;
    buff.id = "test.buff.immuneTarget";
    buff.durationPolicy = "duration";
    buff.duration = 5.f;
    buff.tags = {"buff", "magic"};
    EffectRegistry::registerEffect(buff);

    StatusSystem::registerApplyCondition("blockMagic",
                                         [](RPGActor *, const EffectDefinition &def,
                                            const std::string &, std::string &reason) {
                                             if (def.hasTag("magic")) {
                                                 reason = "immune:magic";
                                                 return false;
                                             }
                                             return true;
                                         });
    CHECK(StatusSystem::hasApplyCondition("blockMagic"));

    RPGActor *actor = RPGActor::createActor();
    CHECK_EQ(actor->applyBuff("test.buff.immuneTarget"), -1);
    CHECK(!actor->hasBuff("test.buff.immuneTarget"));
    CHECK_EQ(actor->getBuffCount(), 0);

    std::vector<StatusChangeEvent> changes;
    StatusSystem::pollChanges(changes);
    REQUIRE(changes.size() == 1);
    CHECK_EQ(changes[0].action, "reject");
    CHECK_EQ(changes[0].effectId, "test.buff.immuneTarget");
    CHECK_EQ(changes[0].reason, "immune:magic");

    StatusSystem::unregisterApplyCondition("blockMagic");
    int id = actor->applyBuff("test.buff.immuneTarget", "caster");
    CHECK(id > 0);
    CHECK(actor->hasBuff("test.buff.immuneTarget"));
    CHECK(actor->hasBuffTag("buff"));
    CHECK_EQ(actor->getBuffSource(0), "caster");

    actor->setBuffProp(id, "iconOverride", "ui/custom.png");
    CHECK_EQ(actor->getBuffProp(id, "iconOverride"), "ui/custom.png");
    CHECK_EQ(actor->getBuffProp(id, "missing", "n/a"), "n/a");

    StatusSystem::pollChanges(changes);
    REQUIRE(changes.size() == 1);
    CHECK_EQ(changes[0].action, "apply");
    CHECK_EQ(changes[0].instanceId, id);

    CHECK(actor->removeBuff(id));
    StatusSystem::pollChanges(changes);
    REQUIRE(changes.size() == 1);
    CHECK_EQ(changes[0].action, "remove");

    actor->release();
    EffectRegistry::remove("test.buff.immuneTarget");
    StatusSystem::clearApplyConditions();
}

TEST_CASE("rpg.buff.customStackPolicyAndLifecycleHook") {
    std::vector<StatusChangeEvent> drain;
    StatusSystem::pollChanges(drain);

    EffectDefinition def;
    def.id = "test.buff.customStack";
    def.durationPolicy = "duration";
    def.duration = 2.f;
    def.stackPolicy = "additiveDuration";
    def.modifiers.push_back({"attack", "add", 3.0, 0});
    EffectRegistry::registerEffect(def);

    StatusSystem::registerStackPolicy(
        "additiveDuration",
        [](RPGActor *, StatusInstance &existing, const EffectDefinition &effect,
           const std::string &) {
            // Each re-apply adds duration and one stack, uncapped for the test.
            existing.stacks += 1;
            if (existing.remaining >= 0.f) existing.remaining += effect.duration;
            return existing.instanceId;
        });
    CHECK(StatusSystem::hasStackPolicy("additiveDuration"));

    int hookCalls = 0;
    std::string lastAction;
    StatusSystem::registerLifecycleHook("counter", [&](const StatusChangeEvent &ev) {
        ++hookCalls;
        lastAction = ev.action;
    });

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 10.0);

    int id1 = actor->applyBuff("test.buff.customStack");
    CHECK(id1 > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 13.0);
    CHECK_EQ(hookCalls, 1);
    CHECK_EQ(lastAction, "apply");

    int id2 = actor->applyBuff("test.buff.customStack");
    CHECK_EQ(id2, id1);
    CHECK_EQ(actor->getBuffStacks(0), 2);
    CHECK(approxEq(actor->getBuffRemaining(0), 4.0, 1e-4));
    CHECK_EQ(actor->getFinalAttribute("attack"), 16.0);  // 3 * 2 stacks
    CHECK_EQ(lastAction, "stack");

    StatusSystem::update(4.1f);
    CHECK_EQ(actor->getBuffCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 10.0);
    CHECK_EQ(lastAction, "expire");

    std::vector<StatusChangeEvent> changes;
    StatusSystem::pollChanges(changes);
    CHECK(changes.size() >= 3);
    CHECK_EQ(changes.back().action, "expire");

    actor->release();
    EffectRegistry::remove("test.buff.customStack");
    StatusSystem::clearStackPolicies();
    StatusSystem::clearLifecycleHooks();
}

TEST_CASE("rpg.buff.facadeStatusChangeEvents") {
    std::vector<StatusChangeEvent> drain;
    StatusSystem::pollChanges(drain);

    RPG *rpg = RPG::create();
    rpg->clearEffectDefinitions();
    CHECK_EQ(rpg->registerEffectsFromJson(R"([
        {"id":"buff.facade","durationPolicy":"duration","duration":0.5,
         "stackPolicy":"refresh","tags":["buff"],
         "extra":{"icon":"ui/buff.png"}}
    ])"),
             1);

    RPGActor *actor = rpg->newActor();
    int id = actor->applyBuff("buff.facade");
    CHECK(id > 0);

    // apply happens before update; changes are polled inside update().
    rpg->update(0.f);
    CHECK_EQ(rpg->getStatusChangeEventCount(), 1);
    CHECK_EQ(rpg->getStatusChangeEventAction(0), "apply");
    CHECK_EQ(rpg->getStatusChangeEventEffectId(0), "buff.facade");
    CHECK_EQ(rpg->getStatusChangeEventInstanceId(0), id);

    rpg->update(0.6f);
    CHECK_EQ(rpg->getStatusChangeEventCount(), 1);
    CHECK_EQ(rpg->getStatusChangeEventAction(0), "expire");
    CHECK_EQ(actor->getBuffCount(), 0);

    actor->release();
    rpg->clearEffectDefinitions();
}

// ---------------------------------------------------------------------------
// Skill / SkillSystem
// ---------------------------------------------------------------------------

TEST_CASE("rpg.skill.registryAndCanCastChecks") {
    SkillDefinition def;
    def.id = "test.skill.bolt";
    def.cooldown = 4.0f;
    def.castTime = 0.f;
    def.costs.push_back({"mana", 10.0});
    SkillRegistry::registerSkill(def);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("mana", 5.0);

    CHECK(!actor->canCastSkill("test.skill.bolt"));
    CHECK_EQ(actor->canCastSkillReason("test.skill.bolt"), "not_learned");

    actor->learnSkill("test.skill.bolt");
    CHECK(actor->knowsSkill("test.skill.bolt"));
    CHECK(!actor->canCastSkill("test.skill.bolt"));
    CHECK_EQ(actor->canCastSkillReason("test.skill.bolt"), "insufficient_cost");

    actor->setBaseAttribute("mana", 50.0);
    CHECK(actor->canCastSkill("test.skill.bolt"));

    actor->release();
    SkillRegistry::remove("test.skill.bolt");
}

TEST_CASE("rpg.skill.instantCastDeductsCostSetsCooldownAndAppliesEffect") {
    EffectDefinition burn;
    burn.id = "test.skill.effect.burn";
    burn.durationPolicy = "duration";
    burn.duration = 5.f;
    burn.modifiers.push_back({"defense", "add", -3.0, 0});
    EffectRegistry::registerEffect(burn);

    SkillDefinition def;
    def.id = "test.skill.instant";
    def.cooldown = 3.0f;
    def.castTime = 0.f;
    def.costs.push_back({"mana", 10.0});
    def.grantedEffects.push_back("test.skill.effect.burn");
    SkillRegistry::registerSkill(def);

    RPGActor *caster = RPGActor::createActor();
    RPGActor *target = RPGActor::createActor();
    caster->setBaseAttribute("mana", 20.0);
    target->setBaseAttribute("defense", 10.0);
    caster->learnSkill("test.skill.instant");

    CHECK(caster->beginCastSkill("test.skill.instant", target));
    CHECK_EQ(caster->getBaseAttribute("mana"), 10.0);
    CHECK_EQ(caster->getSkillCooldown("test.skill.instant"), 3.0f);
    CHECK(!caster->canCastSkill("test.skill.instant"));
    CHECK_EQ(caster->canCastSkillReason("test.skill.instant"), "on_cooldown");
    CHECK_EQ(target->getFinalAttribute("defense"), 7.0);

    std::vector<SkillCastEvent> events;
    SkillSystem::pollCastEvents(events);
    REQUIRE(events.size() == 1);
    CHECK_EQ(events[0].caster, caster);
    CHECK_EQ(events[0].target, target);
    CHECK_EQ(events[0].skillId, "test.skill.instant");

    SkillSystem::update(3.1f);
    CHECK(caster->canCastSkill("test.skill.instant"));

    caster->release();
    target->release();
    SkillRegistry::remove("test.skill.instant");
    EffectRegistry::remove("test.skill.effect.burn");
}

TEST_CASE("rpg.skill.castTimeProgressAndResolution") {
    SkillDefinition def;
    def.id = "test.skill.channeled";
    def.cooldown = 0.f;
    def.castTime = 2.0f;
    SkillRegistry::registerSkill(def);

    RPGActor *actor = RPGActor::createActor();
    actor->learnSkill("test.skill.channeled");

    CHECK(actor->beginCastSkill("test.skill.channeled"));
    CHECK(actor->isCastingSkill());
    CHECK_EQ(actor->getCastingSkillId(), "test.skill.channeled");
    CHECK_EQ(actor->getCastProgress(), 0.f);

    SkillSystem::update(1.0f);
    CHECK(actor->isCastingSkill());
    CHECK(approxEq(actor->getCastProgress(), 0.5, 1e-4));

    std::vector<SkillCastEvent> events;
    SkillSystem::pollCastEvents(events);
    CHECK(events.empty());  // not resolved yet

    SkillSystem::update(1.1f);
    CHECK(!actor->isCastingSkill());
    SkillSystem::pollCastEvents(events);
    REQUIRE(events.size() == 1);
    CHECK_EQ(events[0].skillId, "test.skill.channeled");

    actor->release();
    SkillRegistry::remove("test.skill.channeled");
}

TEST_CASE("rpg.skill.cancelCastStopsResolution") {
    SkillDefinition def;
    def.id = "test.skill.cancelable";
    def.castTime = 5.0f;
    SkillRegistry::registerSkill(def);

    RPGActor *actor = RPGActor::createActor();
    actor->learnSkill("test.skill.cancelable");
    actor->beginCastSkill("test.skill.cancelable");
    CHECK(actor->isCastingSkill());

    actor->cancelCastSkill();
    CHECK(!actor->isCastingSkill());

    SkillSystem::update(10.0f);
    std::vector<SkillCastEvent> events;
    SkillSystem::pollCastEvents(events);
    CHECK(events.empty());

    actor->release();
    SkillRegistry::remove("test.skill.cancelable");
}

TEST_CASE("rpg.skill.customCastConditionBlocksCast") {
    SkillDefinition def;
    def.id = "test.skill.guarded";
    SkillRegistry::registerSkill(def);

    RPGActor *actor = RPGActor::createActor();
    actor->learnSkill("test.skill.guarded");

    SkillSystem::registerCastCondition("no_silence", [](RPGActor *a, const SkillDefinition &,
                                                          std::string &reason) {
        if (a->hasEffect("silence")) {
            reason = "silenced";
            return false;
        }
        return true;
    });

    CHECK(actor->canCastSkill("test.skill.guarded"));

    EffectDefinition silence;
    silence.id = "silence";
    silence.durationPolicy = "infinite";
    EffectRegistry::registerEffect(silence);
    actor->applyEffect("silence");

    CHECK(!actor->canCastSkill("test.skill.guarded"));
    CHECK_EQ(actor->canCastSkillReason("test.skill.guarded"), "silenced");

    SkillSystem::unregisterCastCondition("no_silence");
    CHECK(actor->canCastSkill("test.skill.guarded"));

    actor->removeStatusByEffect("silence");
    actor->release();
    SkillRegistry::remove("test.skill.guarded");
    EffectRegistry::remove("silence");
}

// ---------------------------------------------------------------------------
// Settlement pipeline
// ---------------------------------------------------------------------------

TEST_CASE("rpg.settlement.stagesRunInPriorityOrder") {
    SettlementPipeline::clearPipeline("test.pipeline.order");
    std::vector<std::string> order;

    SettlementPipeline::registerStage("test.pipeline.order", "second", 10,
                                       [&order](SettlementContext &) { order.push_back("second"); });
    SettlementPipeline::registerStage("test.pipeline.order", "first", 1,
                                       [&order](SettlementContext &) { order.push_back("first"); });
    SettlementPipeline::registerStage("test.pipeline.order", "third", 20,
                                       [&order](SettlementContext &) { order.push_back("third"); });

    SettlementContext ctx;
    SettlementPipeline::run("test.pipeline.order", ctx);
    REQUIRE(order.size() == 3);
    CHECK_EQ(order[0], "first");
    CHECK_EQ(order[1], "second");
    CHECK_EQ(order[2], "third");

    SettlementPipeline::clearPipeline("test.pipeline.order");
}

TEST_CASE("rpg.settlement.cancelStopsLaterStages") {
    SettlementPipeline::clearPipeline("test.pipeline.cancel");
    bool laterRan = false;

    SettlementPipeline::registerStage("test.pipeline.cancel", "dodge", 0,
                                       [](SettlementContext &ctx) { ctx.cancelled = true; });
    SettlementPipeline::registerStage("test.pipeline.cancel", "damage", 10,
                                       [&laterRan](SettlementContext &) { laterRan = true; });

    SettlementContext ctx;
    SettlementPipeline::run("test.pipeline.cancel", ctx);
    CHECK(!laterRan);
    CHECK(ctx.cancelled);

    SettlementPipeline::clearPipeline("test.pipeline.cancel");
}

TEST_CASE("rpg.settlement.damageCalcExampleWithCrit") {
    SettlementPipeline::clearPipeline("test.pipeline.damage");

    SettlementPipeline::registerStage(
        "test.pipeline.damage", "base", 0,
        [](SettlementContext &ctx) { ctx.set("result", ctx.get("attack") - ctx.get("defense")); });
    SettlementPipeline::registerStage("test.pipeline.damage", "crit", 10, [](SettlementContext &ctx) {
        if (ctx.hasTag("crit")) ctx.set("result", ctx.get("result") * 2.0);
    });
    SettlementPipeline::registerStage("test.pipeline.damage", "minFloor", 20, [](SettlementContext &ctx) {
        if (ctx.get("result") < 1.0) ctx.set("result", 1.0);
    });

    SettlementContext ctx;
    ctx.set("attack", 50.0);
    ctx.set("defense", 20.0);
    ctx.addTag("crit");
    SettlementPipeline::run("test.pipeline.damage", ctx);
    CHECK_EQ(ctx.get("result"), 60.0);  // (50-20) * 2

    SettlementContext ctx2;
    ctx2.set("attack", 5.0);
    ctx2.set("defense", 50.0);
    SettlementPipeline::run("test.pipeline.damage", ctx2);
    CHECK_EQ(ctx2.get("result"), 1.0);  // floored

    CHECK_EQ(SettlementPipeline::stageCount("test.pipeline.damage"), 3);
    CHECK(SettlementPipeline::setStageEnabled("test.pipeline.damage", "crit", false));
    SettlementContext ctx3;
    ctx3.set("attack", 50.0);
    ctx3.set("defense", 20.0);
    ctx3.addTag("crit");
    SettlementPipeline::run("test.pipeline.damage", ctx3);
    CHECK_EQ(ctx3.get("result"), 30.0);  // crit stage disabled

    CHECK(SettlementPipeline::unregisterStage("test.pipeline.damage", "minFloor"));
    CHECK_EQ(SettlementPipeline::stageCount("test.pipeline.damage"), 2);

    SettlementPipeline::clearPipeline("test.pipeline.damage");
    CHECK_EQ(SettlementPipeline::stageCount("test.pipeline.damage"), 0);
}

// ---------------------------------------------------------------------------
// RPG module facade smoke test (end-to-end via the same entry points scripts use)
// ---------------------------------------------------------------------------

TEST_CASE("rpg.module.endToEndSkillTriggersEffectViaFacade") {
    auto *rpg = RPG::create();
    rpg->clearEffectDefinitions();
    rpg->clearSkillDefinitions();

    int effectsLoaded = rpg->registerEffectsFromJson(R"([
        {"id":"e2e.poison","durationPolicy":"duration","duration":2,"period":1}
    ])");
    CHECK_EQ(effectsLoaded, 1);

    int skillsLoaded = rpg->registerSkillsFromJson(R"([
        {"id":"e2e.dart","cooldown":1,"castTime":0,
         "costs":[{"attribute":"stamina","amount":5}],
         "grantedEffects":["e2e.poison"]}
    ])");
    CHECK_EQ(skillsLoaded, 1);

    RPGActor *caster = rpg->newActor();
    RPGActor *target = rpg->newActor();
    caster->setBaseAttribute("stamina", 10.0);
    caster->learnSkill("e2e.dart");

    CHECK(caster->beginCastSkill("e2e.dart", target));

    rpg->update(1.0f);
    CHECK_EQ(rpg->getCastEventCount(), 1);
    CHECK_EQ(rpg->getCastEventCaster(0), caster);
    CHECK_EQ(rpg->getCastEventTarget(0), target);
    CHECK_EQ(rpg->getCastEventSkillId(0), "e2e.dart");
    CHECK_EQ(rpg->getTickEventCount(), 1);
    CHECK_EQ(rpg->getTickEventActor(0), target);
    CHECK_EQ(rpg->getTickEventEffectId(0), "e2e.poison");

    rpg->update(1.5f);  // effect (duration 2, already 1s elapsed) expires
    CHECK_EQ(target->getStatusCount(), 0);

    caster->release();
    target->release();
    rpg->clearEffectDefinitions();
    rpg->clearSkillDefinitions();
}
