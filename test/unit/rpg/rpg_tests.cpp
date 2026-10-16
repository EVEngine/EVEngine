#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg)
#include "PrivateTestScope.h"
#include <cmath>
#include <string>
#include <vector>
#include "rpg/AttributeSystem.h"
#include "rpg/AttributeTypes.h"
#include "rpg/Effect.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/Settlement.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg.cpp
namespace test_rpg {
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
    def.id             = "test.effect.crud";
    def.durationPolicy = "duration";
    def.duration       = 3.f;
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
    int         n = EffectRegistry::loadFromJson(json, &err);
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
    heal.id             = "test.status.instantHeal";
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
    buff.id             = "test.status.durationBuff";
    buff.durationPolicy = "duration";
    buff.duration       = 1.0f;
    buff.modifiers.push_back({"attack", "add", 10.0, 0});
    EffectRegistry::registerEffect(buff);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 20.0);

    int instanceId = actor->applyEffect("test.status.durationBuff", "test-source");
    CHECK(instanceId > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 30.0);
    CHECK_EQ(actor->getStatusCount(), 1);
    CHECK(actor->hasEffect("test.status.durationBuff"));

    CHECK(StatusSystem::update(0.5).ok());
    CHECK_EQ(actor->getStatusCount(), 1);
    CHECK_EQ(actor->getFinalAttribute("attack"), 30.0);

    CHECK(StatusSystem::update(0.6).ok());  // total elapsed 1.1s > duration 1.0s
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 20.0);
    CHECK(!actor->hasEffect("test.status.durationBuff"));

    actor->release();
    EffectRegistry::remove("test.status.durationBuff");
}

TEST_CASE("rpg.status.stackPolicyNoneRejectsDuplicate") {
    EffectDefinition def;
    def.id             = "test.status.stackNone";
    def.durationPolicy = "infinite";
    def.stackPolicy    = "none";
    EffectRegistry::registerEffect(def);

    RPGActor *actor = RPGActor::createActor();
    int       id1   = actor->applyEffect("test.status.stackNone");
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
    refreshDef.id             = "test.status.refresh";
    refreshDef.durationPolicy = "duration";
    refreshDef.duration       = 2.0f;
    refreshDef.stackPolicy    = "refresh";
    EffectRegistry::registerEffect(refreshDef);

    EffectDefinition extendDef;
    extendDef.id             = "test.status.extend";
    extendDef.durationPolicy = "duration";
    extendDef.duration       = 2.0f;
    extendDef.stackPolicy    = "extend";
    EffectRegistry::registerEffect(extendDef);

    RPGActor *actor = RPGActor::createActor();

    int r1 = actor->applyEffect("test.status.refresh");
    CHECK(StatusSystem::update(1.5).ok());
    CHECK(approxEq(actor->getStatusRemaining(0), 0.5, 1e-4));
    int r2 = actor->applyEffect("test.status.refresh");
    CHECK_EQ(r1, r2);
    CHECK(approxEq(actor->getStatusRemaining(0), 2.0, 1e-4));

    actor->removeStatusByEffect("test.status.refresh");

    int e1 = actor->applyEffect("test.status.extend");
    CHECK(StatusSystem::update(1.0).ok());
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
    def.id             = "test.status.stack";
    def.durationPolicy = "infinite";
    def.stackPolicy    = "stack";
    def.maxStacks      = 2;
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
    dot.id             = "test.status.periodicDot";
    dot.durationPolicy = "duration";
    dot.duration       = 2.5f;
    dot.period         = 1.0f;
    EffectRegistry::registerEffect(dot);

    RPGActor *actor      = RPGActor::createActor();
    int       instanceId = actor->applyEffect("test.status.periodicDot", "caster-1");
    CHECK(instanceId > 0);
    // Periodic effects do not touch attributes directly.
    CHECK(!actor->hasAttribute("health"));

    std::vector<StatusTickEvent> ticks;
    CHECK(StatusSystem::update(1.0).ok());
    StatusSystem::pollTicks(ticks);
    REQUIRE(ticks.size() == 1);
    CHECK_EQ(ticks[0].actor, actor);
    CHECK_EQ(ticks[0].effectId, "test.status.periodicDot");
    CHECK_EQ(ticks[0].source, "caster-1");

    ticks.clear();
    CHECK(StatusSystem::update(1.0).ok());  // t=2.0 -> another tick
    StatusSystem::pollTicks(ticks);
    REQUIRE(ticks.size() == 1);

    ticks.clear();
    CHECK(StatusSystem::update(1.0).ok());  // t=3.0 > duration 2.5 -> expires; no more ticks after expiry
    StatusSystem::pollTicks(ticks);
    CHECK(ticks.size() <= 1);  // may or may not tick exactly at expiry boundary depending on order
    CHECK_EQ(actor->getStatusCount(), 0);

    ticks.clear();
    CHECK(StatusSystem::update(5.0).ok());
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 0u);  // expired: no further ticks ever

    actor->release();
    EffectRegistry::remove("test.status.periodicDot");
}

TEST_CASE("rpg.status.removeByTagAndBySource") {
    EffectDefinition a;
    a.id             = "test.status.tagA";
    a.durationPolicy = "infinite";
    a.tags           = {"debuff", "curse"};
    EffectRegistry::registerEffect(a);

    EffectDefinition b;
    b.id             = "test.status.tagB";
    b.durationPolicy = "infinite";
    b.tags           = {"debuff"};
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
    buff.id             = "test.buff.immuneTarget";
    buff.durationPolicy = "duration";
    buff.duration       = 5.f;
    buff.tags           = {"buff", "magic"};
    EffectRegistry::registerEffect(buff);

    StatusSystem::registerApplyCondition(
        "blockMagic", [](RPGActor *, const EffectDefinition &def, const std::string &, std::string &reason) {
            if (def.hasTag("magic")) {
                reason = "immune:magic";
                return false;
            }
            return true;
        });
    CHECK(StatusSystem::hasApplyCondition("blockMagic"));

    RPGActor *actor    = RPGActor::createActor();
    const int rejected = actor->applyBuff("test.buff.immuneTarget");
    CHECK_EQ(rejected, -1);
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

    changes.clear();
    StatusSystem::pollChanges(changes);
    REQUIRE(changes.size() == 1);
    CHECK_EQ(changes[0].action, "apply");
    CHECK_EQ(changes[0].instanceId, id);

    CHECK(actor->removeBuff(id));
    changes.clear();
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
    def.id             = "test.buff.customStack";
    def.durationPolicy = "duration";
    def.duration       = 2.f;
    def.stackPolicy    = "additiveDuration";
    def.modifiers.push_back({"attack", "add", 3.0, 0});
    EffectRegistry::registerEffect(def);

    StatusSystem::registerStackPolicy("additiveDuration", [](RPGActor *, StatusInstance &existing,
                                                             const EffectDefinition &effect, const std::string &) {
        // Each re-apply adds duration and one stack, uncapped for the test.
        existing.stacks += 1;
        if (existing.remaining >= 0.f) existing.remaining += effect.duration;
        return existing.instanceId;
    });
    CHECK(StatusSystem::hasStackPolicy("additiveDuration"));

    int         hookCalls = 0;
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

    CHECK(StatusSystem::update(4.1).ok());
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
    int       id    = actor->applyBuff("buff.facade");
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
    def.id        = "test.skill.bolt";
    def.cooldown  = 4.0f;
    def.castTime  = 0.f;
    auto boltCost = eve::resource::CostSpec::single("mana", 10);
    REQUIRE(boltCost.hasValue());
    def.cost = std::move(boltCost).takeValue();
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
    burn.id             = "test.skill.effect.burn";
    burn.durationPolicy = "duration";
    burn.duration       = 5.f;
    burn.modifiers.push_back({"defense", "add", -3.0, 0});
    EffectRegistry::registerEffect(burn);

    SkillDefinition def;
    def.id           = "test.skill.instant";
    def.cooldown     = 3.0f;
    def.castTime     = 0.f;
    auto instantCost = eve::resource::CostSpec::single("mana", 10);
    REQUIRE(instantCost.hasValue());
    def.cost = std::move(instantCost).takeValue();
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
    def.id       = "test.skill.channeled";
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
    def.id       = "test.skill.cancelable";
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

    SkillSystem::registerCastCondition("no_silence", [](RPGActor *a, const SkillDefinition &, std::string &reason) {
        if (a->hasEffect("silence")) {
            reason = "silenced";
            return false;
        }
        return true;
    });

    CHECK(actor->canCastSkill("test.skill.guarded"));

    EffectDefinition silence;
    silence.id             = "silence";
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

    SettlementPipeline::registerStage("test.pipeline.damage", "base", 0, [](SettlementContext &ctx) {
        ctx.set("result", ctx.get("attack") - ctx.get("defense"));
    });
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
}  // namespace test_rpg
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_actor_checkpoint)
#include "PrivateTestScope.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_actor_checkpoint.cpp
namespace test_rpg_actor_checkpoint {
using namespace eve::rpg;

TEST_CASE("rpg.actorCheckpoint.roundTripAndAtomicFailure") {
    SkillRegistry::clear();
    SkillDefinition strike;
    strike.id = "skill.strike";
    SkillRegistry::registerSkill(strike);

    RPGActor *source = RPGActor::createActor();
    REQUIRE(source != nullptr);
    source->setBaseAttribute("attack", 18.0);
    source->setBaseAttribute("hp", 100.0);
    source->setCurrent("hp", 47.0);
    source->learnSkill("skill.strike");
    source->setSkillCooldown("skill.strike", 5.0f);
    REQUIRE(source->restoreProgression(4, 25.0, 120.0).ok());
    auto encoded = source->checkpointJson();
    REQUIRE(encoded.ok());

    RPGActor *restored = RPGActor::createActor();
    REQUIRE(restored != nullptr);
    restored->setBaseAttribute("attack", 1.0);
    restored->setBaseAttribute("hp", 100.0);
    restored->setCurrent("hp", 100.0);
    REQUIRE(restored->restoreCheckpointJson(encoded.value()).ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getCurrent("hp"), 47.0);
    CHECK_EQ(restored->getLevel(), 4);
    CHECK_EQ(restored->getXp(), 25.0);
    CHECK(restored->knowsSkill("skill.strike"));
    CHECK_EQ(restored->getSkillCooldown("skill.strike"), 0.0f);

    auto malformed = restored->restoreCheckpointJson(
        R"({"schema":"eve.rpg.actor-checkpoint","version":1,"baseAttributes":[],"class":{"id":"","skillsSyncedUpTo":0},"learnedSkills":[],"progression":{"level":0,"xp":0,"xpToNext":1},"traits":[],"vitals":{}})");
    CHECK(!malformed.ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getLevel(), 4);
    CHECK_EQ(restored->getCurrent("hp"), 47.0);

    source->release();
    restored->release();
    SkillRegistry::clear();
}

TEST_CASE("rpg.actorCheckpoint.rejectsUnsafeCapture") {
    SkillRegistry::clear();
    SkillDefinition channel;
    channel.id       = "skill.channel";
    channel.castTime = 2.0f;
    SkillRegistry::registerSkill(channel);
    RPGActor *actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill("skill.channel");
    REQUIRE(actor->beginCastSkill("skill.channel", actor));
    CHECK(!actor->checkpointJson().ok());
    actor->cancelCastSkill();
    CHECK(actor->checkpointJson().ok());
    actor->release();
    SkillRegistry::clear();
}
}  // namespace test_rpg_actor_checkpoint
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_battle)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/Effect.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/SettlementAdapter.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/VitalsSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_battle.cpp
namespace test_rpg_battle {
using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

RPGActor *makeFighter(double atk, double def, double hp, double speed) {
    RPGActor *a = RPGActor::createActor();
    a->setBaseAttribute("attack", atk);
    a->setBaseAttribute("defense", def);
    a->setBaseAttribute("hp", hp);
    a->setBaseAttribute("speed", speed);
    a->setCurrent("hp", hp);
    return a;
}

eve::SubjectRef subject(std::uint8_t suffix) {
    eve::PersistentId::Bytes bytes{};
    bytes[15] = suffix;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}
}  // namespace

TEST_CASE("rpg.battle.formulaEvaluation") {
    RPGActor *a = makeFighter(10, 0, 100, 5);
    RPGActor *b = makeFighter(0, 2, 100, 1);
    CHECK(approxEq(BattleSystem::evaluateFormula("a.attack * 4 - b.defense * 2", a, b), 36.0));
    CHECK(approxEq(BattleSystem::evaluateFormula("a.attack * b.defense", a, b), 20.0));
    CHECK(approxEq(BattleSystem::evaluateFormula("(1 + 2) * 3", a, b), 9.0));
    CHECK(approxEq(BattleSystem::evaluateFormula("a.missing + 5", a, b), 5.0));  // 未知参数按 0
    a->release();
    b->release();
}

TEST_CASE("rpg.battle.formulaValidationRejectsMalformedDefinitionsAtomically") {
    RPGActor *a = makeFighter(10, 0, 100, 5);
    RPGActor *b = makeFighter(0, 2, 100, 1);

    auto unary = BattleSystem::evaluateFormulaChecked("-(a.attack - b.defense)", a, b);
    REQUIRE(unary.ok());
    CHECK(approxEq(unary.value(), -8.0));

    auto missingParen = BattleSystem::evaluateFormulaChecked("(a.attack + 2", a, b);
    CHECK(!missingParen);
    REQUIRE(missingParen.error() != nullptr);
    CHECK(missingParen.error()->code() == eve::DiagnosticCode::ParseError);

    auto trailing = BattleSystem::evaluateFormulaChecked("a.attack garbage", a, b);
    CHECK(!trailing);

    auto division = BattleSystem::evaluateFormulaChecked("a.attack / 0", a, b);
    CHECK(!division);

    BattleSystem::clearSkillDamage();
    SkillDamageSpec valid{"hp", "a.attack / b.defense", "", 0.0, 100};
    auto            published = BattleSystem::registerSkillDamageChecked("strike", valid);
    REQUIRE(published.ok());
    CHECK(BattleSystem::findSkillDamage("strike") != nullptr);

    SkillDamageSpec invalid{"hp", "a.attack + )", "", 0.0, 100};
    auto            rejected = BattleSystem::registerSkillDamageChecked("strike", invalid);
    CHECK(!rejected);
    const SkillDamageSpec *retained = BattleSystem::findSkillDamage("strike");
    REQUIRE(retained != nullptr);
    CHECK_EQ(retained->formula, std::string("a.attack / b.defense"));

    BattleSystem::clearSkillDamage();
    a->release();
    b->release();
}

TEST_CASE("rpg.battle.resolveHitCritAndElement") {
    auto *rpg = RPG::create();
    rpg->clearTraitDefinitions();
    int registered =
        rpg->registerTraitsFromJson(R"([{"id":"crit","traits":[{"kind":"exParam","target":"critRate","value":1.0}]}])");
    CHECK_EQ(registered, 1);

    RPGActor *a   = makeFighter(50, 0, 100, 5);
    RPGActor *b   = makeFighter(0, 5, 100, 1);
    int       tid = a->applyTrait("crit", "test");
    CHECK(tid > 0);

    // 必暴击（critRate=1.0）
    SkillDamageSpec spec;
    spec.formula   = "a.attack - b.defense";
    spec.element   = "";
    DamageResult r = BattleSystem::resolveHit(a, b, spec, 42);
    CHECK(r.hit);
    CHECK(r.crit);
    CHECK(approxEq(r.amount, (50 - 5) * 3.0));

    // 元素减伤：火耐性 0.5（用不带暴击特征的攻击方）
    RPGActor *a2 = makeFighter(50, 0, 100, 5);
    RPGActor *c  = makeFighter(0, 0, 100, 1);
    rpg->registerTraitsFromJson(
        R"([{"id":"fire_resist2","traits":[{"kind":"elementRate","target":"fire","value":0.5}]}])");
    c->applyTrait("fire_resist2", "x");
    SkillDamageSpec fireSpec;
    fireSpec.formula = "a.attack";
    fireSpec.element = "fire";
    DamageResult fr  = BattleSystem::resolveHit(a2, c, fireSpec, 7);
    CHECK(fr.hit);
    CHECK(approxEq(fr.elementRate, 0.5));
    CHECK(approxEq(fr.amount, 50.0 * 0.5));  // 未暴击

    rpg->clearTraitDefinitions();
    a->release();
    b->release();
    a2->release();
    c->release();
}

TEST_CASE("rpg.battle.criticalSettlementPreservesMetadataWithoutDoubleScaling") {
    auto *rpg = RPG::create();
    rpg->clearTraitDefinitions();
    REQUIRE_EQ(rpg->registerTraitsFromJson(
                   R"([{"id":"always_crit","traits":[{"kind":"exParam","target":"critRate","value":1.0}]}])"),
               1);
    RPGActor *attacker = makeFighter(20, 0, 100, 10);
    RPGActor *target   = makeFighter(0, 0, 100, 1);
    REQUIRE(attacker->applyTrait("always_crit", "test") > 0);

    Battle battle;
    battle.addActor(attacker, BattleSide::Party);
    battle.addActor(target, BattleSide::Enemies);
    battle.setAction(attacker, "", target);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(target->getCurrent("hp"), 40.0);
    battle.pollEvents();
    bool sawCriticalDamage = false;
    for (int index = 0; index < battle.getEventCount(); ++index) {
        const auto event = battle.getEvent(index);
        if (event.action == "damage") {
            CHECK_EQ(event.amount, 60.0);
            CHECK(event.crit);
            sawCriticalDamage = true;
        }
    }
    CHECK(sawCriticalDamage);

    rpg->clearTraitDefinitions();
    attacker->release();
    target->release();
}

TEST_CASE("rpg.battle.roundVictory") {
    RPGActor *hero  = makeFighter(30, 0, 100, 10);
    RPGActor *slime = makeFighter(5, 0, 20, 1);
    slime->setCurrent("hp", 20);

    Battle b;
    b.addActor(hero, BattleSide::Party);
    b.addActor(slime, BattleSide::Enemies);
    b.setAction(hero, "", nullptr);  // 普攻
    b.autoEnemyActions();
    b.startRound();
    while (b.executeNextAction() && !b.isFinished()) {
    }
    CHECK(b.isFinished());
    CHECK(b.isVictory());
    CHECK(!b.isActorAlive(slime));
    // 普攻 30 - 0 = 30 ≥ 20 → slime 死亡
    CHECK_EQ(b.getTurn(), 1);

    b.pollEvents();
    bool sawDamage = false, sawVictory = false;
    int  deaths = 0, kills = 0;
    for (int i = 0; i < b.getEventCount(); ++i) {
        auto ev = b.getEvent(i);
        if (ev.action == "damage") sawDamage = true;
        if (ev.action == "victory") sawVictory = true;
        if (ev.action == "death") ++deaths;
        if (ev.action == "kill") ++kills;
    }
    CHECK(sawDamage);
    CHECK(sawVictory);
    CHECK_EQ(deaths, 1);
    CHECK_EQ(kills, 1);

    hero->release();
    slime->release();
}

TEST_CASE("rpg.battle.damageUsesSharedSettlementRules") {
    RPGActor *hero  = makeFighter(20, 0, 100, 10);
    RPGActor *slime = makeFighter(1, 0, 100, 1);

    eve::settlement::SettlementRule guard;
    guard.id                  = "rpg.guard";
    guard.source              = "effect:guard";
    guard.stage               = eve::settlement::StageKind::TargetMitigation;
    guard.operation           = eve::settlement::RuleOperation::ResistPercent;
    guard.value               = 0.5;
    guard.filter.kinds        = {"damage"};
    guard.filter.requiredTags = {"rpg:damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({guard}).ok());

    Battle battle;
    REQUIRE(battle.configureSettlementRules(rules).ok());
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(slime, BattleSide::Enemies);
    battle.setAction(hero, "", slime);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(slime->getCurrent("hp"), 90.0);
    std::vector<VitalsEvent> vitalsEvents;
    VitalsSystem::pollEvents(vitalsEvents);
    REQUIRE(!vitalsEvents.empty());
    CHECK_EQ(vitalsEvents.front().action, std::string("damage"));
    CHECK_EQ(vitalsEvents.front().amount, 10.0);

    hero->release();
    slime->release();
}

TEST_CASE("rpg.battle.activeStatusRuleProjectionTracksRemoval") {
    EffectRegistry::clear();
    EffectDefinition guard;
    guard.id             = "rpg.guard.projected";
    guard.durationPolicy = "infinite";
    guard.extra["settlement.rule"] =
        R"({"stage":"target_mitigation","operation":"resist_percent","value":0.5,"kinds":["damage"]})";
    EffectRegistry::registerEffect(guard);

    RPGActor *hero     = makeFighter(20, 0, 100, 10);
    RPGActor *slime    = makeFighter(1, 0, 100, 1);
    const int statusId = slime->applyEffect(guard.id, "spell.guard");
    REQUIRE(statusId > 0);

    {
        Battle battle;
        battle.addActor(hero, BattleSide::Party);
        battle.addActor(slime, BattleSide::Enemies);
        battle.setAction(hero, "", slime);
        battle.startRound();
        REQUIRE(battle.executeNextAction());
        CHECK_EQ(slime->getCurrent("hp"), 90.0);
    }

    REQUIRE(slime->removeStatus(statusId));
    {
        Battle battle;
        battle.addActor(hero, BattleSide::Party);
        battle.addActor(slime, BattleSide::Enemies);
        battle.setAction(hero, "", slime);
        battle.startRound();
        REQUIRE(battle.executeNextAction());
        CHECK_EQ(slime->getCurrent("hp"), 70.0);
    }

    hero->release();
    slime->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.battle.invalidProjectedStatusRuleLeavesVitalsUnchanged") {
    EffectRegistry::clear();
    EffectDefinition malformed;
    malformed.id                       = "rpg.invalid.projected-rule";
    malformed.durationPolicy           = "infinite";
    malformed.extra["settlement.rule"] = "42";
    EffectRegistry::registerEffect(malformed);

    RPGActor *hero  = makeFighter(20, 0, 100, 10);
    RPGActor *slime = makeFighter(1, 0, 100, 1);
    REQUIRE(slime->applyEffect(malformed.id, "test:invalid") > 0);
    std::vector<VitalsEvent> discarded;
    VitalsSystem::pollEvents(discarded);

    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(slime, BattleSide::Enemies);
    battle.setAction(hero, "", slime);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(slime->getCurrent("hp"), 100.0);

    std::vector<VitalsEvent> vitalsEvents;
    VitalsSystem::pollEvents(vitalsEvents);
    CHECK(vitalsEvents.empty());
    battle.pollEvents();
    bool sawFailure = false;
    bool sawDamage  = false;
    for (int index = 0; index < battle.getEventCount(); ++index) {
        const auto event = battle.getEvent(index);
        sawFailure       = sawFailure || event.action == "settlementFailed";
        sawDamage        = sawDamage || event.action == "damage";
    }
    CHECK(sawFailure);
    CHECK(!sawDamage);

    hero->release();
    slime->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.periodicConfigurationBuildsCanonicalSettlementRequest") {
    EffectRegistry::clear();
    EffectDefinition poison;
    poison.id             = "rpg.periodic.poison";
    poison.durationPolicy = "duration";
    poison.duration       = 3.0f;
    poison.period         = 1.0f;
    poison.stackPolicy    = "stack";
    poison.maxStacks      = 2;
    poison.tags           = {"effect:poison"};
    poison.extra["settlement.tick"] =
        R"({"kind":"damage","resource":"hp","magnitude":4,"tags":["damage:poison"],"context":{"element":"poison"}})";
    EffectRegistry::registerEffect(poison);

    RPGActor *actor = makeFighter(1, 0, 100, 1);
    REQUIRE(actor->applyEffect(poison.id, "test:caster") > 0);
    REQUIRE(actor->applyEffect(poison.id, "test:caster") > 0);
    REQUIRE(StatusSystem::update(1.0).ok());
    std::vector<StatusTickEvent> ticks;
    StatusSystem::pollTicks(ticks);
    REQUIRE_EQ(ticks.size(), 1u);

    const auto target  = subject(70);
    auto       request = makeStatusTickSettlementRequest(ticks.front(), subject(69), target, eve::SimulationTick(1));
    REQUIRE(request.ok());
    CHECK_EQ(request.value().magnitude, 8.0);
    CHECK_EQ(request.value().resource, std::string("hp"));
    CHECK(std::find(request.value().tags.begin(), request.value().tags.end(), "rpg:status-tick") !=
          request.value().tags.end());

    auto malformed                     = poison;
    malformed.extra["settlement.tick"] = R"({"kind":"damage","resource":"hp","magnitude":4,"unknown":true})";
    EffectRegistry::registerEffect(malformed);
    auto rejected = makeStatusTickSettlementRequest(ticks.front(), subject(69), target, eve::SimulationTick(1));
    CHECK(!rejected.ok());
    EffectRegistry::registerEffect(poison);

    eve::settlement::SettlementRule resistance;
    resistance.id                  = "rpg.poison-resistance";
    resistance.stage               = eve::settlement::StageKind::TargetMitigation;
    resistance.operation           = eve::settlement::RuleOperation::ResistPercent;
    resistance.value               = 0.25;
    resistance.filter.requiredTags = {"damage:poison"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({resistance}).ok());
    Battle battle;
    battle.addActor(actor, BattleSide::Party);
    REQUIRE(battle.configureSettlementRules(rules).ok());
    auto settled = battle.settleStatusTick(ticks.front(), subject(69), eve::SimulationTick(1));
    REQUIRE(settled.ok());
    REQUIRE_EQ(settled.value().size(), 1u);
    REQUIRE(settled.value().front().result.has_value());
    CHECK_EQ(settled.value().front().result->applied, 6.0);
    CHECK_EQ(actor->getCurrent("hp"), 94.0);
    std::vector<VitalsEvent> vitalsEvents;
    VitalsSystem::pollEvents(vitalsEvents);
    REQUIRE_EQ(vitalsEvents.size(), 1u);
    CHECK_EQ(vitalsEvents.front().source, poison.id);
    battle.pollEvents();
    REQUIRE_EQ(battle.getEventCount(), 1);
    CHECK_EQ(battle.getEvent(0).action, std::string("damage"));
    CHECK_EQ(battle.getEvent(0).skillId, poison.id);

    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.battle.activeStatusLifestealRunsThroughSettlementChain") {
    EffectRegistry::clear();
    EffectDefinition vampiric;
    vampiric.id             = "rpg.vampiric.projected";
    vampiric.durationPolicy = "infinite";
    vampiric.extra["settlement.rule"] =
        R"({"stage":"trigger","operation":"lifesteal","value":0.5,"kinds":["damage"],"required_tags":["rpg:damage"]})";
    EffectRegistry::registerEffect(vampiric);

    RPGActor *hero  = makeFighter(20, 0, 100, 10);
    RPGActor *slime = makeFighter(1, 0, 100, 1);
    hero->setCurrent("hp", 50.0);
    REQUIRE(hero->applyEffect(vampiric.id, "item:sword") > 0);

    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(slime, BattleSide::Enemies);
    battle.setAction(hero, "", slime);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(slime->getCurrent("hp"), 80.0);
    CHECK_EQ(hero->getCurrent("hp"), 60.0);

    battle.pollEvents();
    bool sawDamage = false;
    bool sawHeal   = false;
    for (int index = 0; index < battle.getEventCount(); ++index) {
        const auto event = battle.getEvent(index);
        sawDamage        = sawDamage || event.action == "damage";
        sawHeal          = sawHeal || event.action == "heal";
    }
    CHECK(sawDamage);
    CHECK(sawHeal);

    hero->release();
    slime->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.battle.defeatWhenPartyDies") {
    RPGActor *hero = makeFighter(1, 0, 10, 1);
    RPGActor *boss = makeFighter(100, 0, 100, 20);
    boss->setCurrent("hp", 100);

    Battle b;
    b.addActor(hero, BattleSide::Party);
    b.addActor(boss, BattleSide::Enemies);
    b.setAction(hero, "", nullptr);
    b.autoEnemyActions();
    b.startRound();
    while (b.executeNextAction() && !b.isFinished()) {
    }
    CHECK(b.isFinished());
    CHECK(b.isDefeat());

    hero->release();
    boss->release();
}

TEST_CASE("rpg.battle.skillDamageViaRegistry") {
    auto *rpg = RPG::create();
    rpg->clearSkillDefinitions();
    rpg->clearSkillDamage();
    int sk = rpg->registerSkillsFromJson(R"([{"id":"fireball","targetType":"enemySingle","castTime":0}])");
    CHECK_EQ(sk, 1);

    // 注册 fireball 的伤害：火元素，公式 a.attack*2
    BattleSystem::registerSkillDamage("fireball", SkillDamageSpec{"hp", "a.attack * 2", "fire", 0.0, 100});

    RPGActor *hero  = makeFighter(20, 0, 100, 10);
    RPGActor *slime = makeFighter(5, 0, 40, 1);
    slime->setCurrent("hp", 40);
    hero->learnSkill("fireball");

    Battle b;
    b.addActor(hero, BattleSide::Party);
    b.addActor(slime, BattleSide::Enemies);
    b.setAction(hero, "fireball", slime);
    b.autoEnemyActions();
    b.startRound();
    while (b.executeNextAction() && !b.isFinished()) {
    }
    CHECK(b.isVictory());
    // 40 伤害（无元素修正）→ slime 死
    CHECK(!b.isActorAlive(slime));

    rpg->clearSkillDefinitions();
    rpg->clearSkillDamage();
    hero->release();
    slime->release();
}

TEST_CASE("rpg.battle.dynamicResourceDamage") {
    // damageType 直接用任意资源名；"XHeal" 表示治疗
    auto *rpg = RPG::create();
    rpg->clearSkillDefinitions();
    rpg->clearSkillDamage();
    rpg->registerSkillsFromJson(R"([{"id":"mana_drain","targetType":"enemySingle","castTime":0}])");
    // 伤害到 mana（非 hp/mp 的任意资源）
    BattleSystem::registerSkillDamage("mana_drain", SkillDamageSpec{"mana", "a.attack", "", 0.0, 100});

    RPGActor *caster = makeFighter(60, 0, 100, 10);
    RPGActor *victim = makeFighter(5, 0, 100, 1);
    victim->setBaseAttribute("mana", 50.0);
    victim->setCurrent("mana", 50.0);
    caster->learnSkill("mana_drain");

    Battle b;
    b.addActor(caster, BattleSide::Party);
    b.addActor(victim, BattleSide::Enemies);
    b.setAction(caster, "mana_drain", victim);
    b.autoEnemyActions();
    b.startRound();
    while (b.executeNextAction() && !b.isFinished()) {
    }
    // 非生命资源归零不产生 death/kill，也不影响 hp。
    CHECK(approxEq(victim->getCurrent("mana"), 0.0));
    CHECK(approxEq(victim->getCurrent("hp"), 100.0));
    CHECK(b.isActorAlive(victim));
    b.pollEvents();
    for (int index = 0; index < b.getEventCount(); ++index) {
        CHECK(b.getEvent(index).action != "death");
        CHECK(b.getEvent(index).action != "kill");
    }

    rpg->clearSkillDefinitions();
    rpg->clearSkillDamage();
    caster->release();
    victim->release();
}

TEST_CASE("rpg.battle.multiSideWinner") {
    // 三方 A(0/玩家) B(1) C(2)，逐个互殴，最终仅剩一方 → 由 getWinnerSide 判定
    RPGActor *a = makeFighter(50, 0, 5, 1);
    RPGActor *b = makeFighter(50, 0, 5, 2);
    RPGActor *c = makeFighter(50, 0, 5, 3);

    Battle btl;
    btl.addActor(a, 0);
    btl.addActor(b, 1);
    btl.addActor(c, 2);
    btl.setPlayerSide(0);
    // 先攻：C(3) → B(2) → A(1)
    btl.setAction(a, "", b);  // A 打 B
    btl.setAction(b, "", c);  // B 打 C
    btl.setAction(c, "", a);  // C 打 A
    btl.startRound();
    while (btl.executeNextAction() && !btl.isFinished()) {
    }
    CHECK(btl.isFinished());
    CHECK(btl.getSide(0) == 0);
    CHECK(btl.getSide(1) == 1);
    CHECK(btl.getSide(2) == 2);
    // C 先杀 A(玩家) → A 死；B 再杀 C → 仅 B(1) 存活 → 玩家败
    CHECK(btl.getWinnerSide() == 1);
    CHECK(!btl.isVictory());
    CHECK(btl.isDefeat());

    a->release();
    b->release();
    c->release();
}

TEST_CASE("rpg.battle.playerSideConfigurable") {
    // 玩家侧设为 2（非默认 0）也能正确判定
    RPGActor *p = makeFighter(50, 0, 5, 2);  // 玩家侧 2
    RPGActor *e = makeFighter(50, 0, 5, 1);  // 敌侧 1

    Battle btl;
    btl.addActor(p, 2);
    btl.addActor(e, 1);
    btl.setPlayerSide(2);
    btl.setAction(p, "", e);
    btl.autoEnemyActions();
    btl.startRound();
    while (btl.executeNextAction() && !btl.isFinished()) {
    }
    CHECK(btl.isVictory());
    CHECK(btl.getWinnerSide() == 2);

    p->release();
    e->release();
}

TEST_CASE("rpg.battle.checkedActionRejectsIllegalTargetsAndDuplicatesAtomically") {
    SkillRegistry::clear();
    SkillDefinition heal;
    heal.id         = "self_heal";
    heal.targetType = "self";
    SkillRegistry::registerSkill(heal);

    RPGActor *hero     = makeFighter(30, 0, 100, 10);
    RPGActor *ally     = makeFighter(5, 0, 100, 5);
    RPGActor *enemy    = makeFighter(5, 0, 20, 1);
    RPGActor *outsider = makeFighter(1, 0, 10, 1);
    hero->learnSkill("self_heal");

    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(ally, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);

    auto friendlyFire = battle.setActionChecked(hero, "", ally);
    CHECK(!friendlyFire.ok());
    auto outsiderTarget = battle.setActionChecked(hero, "", outsider);
    CHECK(!outsiderTarget.ok());
    auto invalidSelfTarget = battle.setActionChecked(hero, "self_heal", enemy);
    CHECK(!invalidSelfTarget.ok());

    auto queued = battle.setActionChecked(hero, "", enemy);
    REQUIRE(queued.ok());
    auto duplicate = battle.setActionChecked(hero, "", enemy);
    CHECK(!duplicate.ok());
    battle.autoEnemyActions();
    battle.startRound();
    while (battle.executeNextAction() && !battle.isFinished()) {
    }
    CHECK(battle.isVictory());

    hero->release();
    ally->release();
    enemy->release();
    outsider->release();
    SkillRegistry::clear();
}

TEST_CASE("rpg.battle.policyTargetsLowestHealthLegalParticipantDeterministically") {
    SkillRegistry::clear();
    SkillDefinition aid;
    aid.id         = "ally_aid";
    aid.targetType = "allySingle";
    SkillRegistry::registerSkill(aid);
    SkillDamageSpec aidSpec;
    aidSpec.damageType = "hpHeal";
    aidSpec.formula    = "20";
    BattleSystem::registerSkillDamage("ally_aid", aidSpec);

    RPGActor *hero      = makeFighter(20, 0, 100, 10);
    RPGActor *ally      = makeFighter(10, 0, 100, 8);
    RPGActor *enemyHigh = makeFighter(5, 0, 100, 2);
    RPGActor *enemyLow  = makeFighter(5, 0, 100, 1);
    hero->learnSkill("ally_aid");
    ally->setCurrent("hp", 25.0);
    enemyHigh->setCurrent("hp", 80.0);
    enemyLow->setCurrent("hp", 20.0);

    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(ally, BattleSide::Party);
    battle.addActor(enemyHigh, BattleSide::Enemies);
    battle.addActor(enemyLow, BattleSide::Enemies);
    CHECK(!battle.setActionByPolicyChecked(hero, "ally_aid", BattleTargetPolicy::LowestHealthEnemy).ok());
    REQUIRE(battle.setActionByPolicyChecked(hero, "ally_aid", BattleTargetPolicy::LowestHealthAlly).ok());
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    battle.pollEvents();
    bool healedAlly = false;
    for (int index = 0; index < battle.getEventCount(); ++index)
        if (battle.getEventAction(index) == "heal" && battle.getEventTarget(index) == ally) healedAlly = true;
    CHECK(healedAlly);
    CHECK_EQ(ally->getCurrent("hp"), 45.0);

    hero->release();
    ally->release();
    enemyHigh->release();
    enemyLow->release();
    SkillRegistry::clear();
}
}  // namespace test_rpg_battle
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_battle_tactics)
#include "PrivateTestScope.h"
#include <string>
#include <vector>
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/BattleTactics.h"
#include "rpg/BattleVictory.h"
#include "rpg/GameState.h"
#include "rpg/LevelSystem.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/Tracker.h"
#include "rpg/WorldState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_battle_tactics.cpp
namespace test_rpg_battle_tactics {
// rpg_battle_tactics.cpp
namespace {

eve::rpg::RPGActor *makeTacticsActor(double attack, double hp) {
    auto *actor = eve::rpg::RPGActor::createActor();
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", 0.0);
    actor->setBaseAttribute("hp", hp);
    actor->setCurrent("hp", hp);
    return actor;
}

}  // namespace

TEST_CASE("rpg.battleTactics.strictReplacementAndConditionalAllyHealing") {
    using namespace eve::rpg;
    SkillRegistry::clear();
    BattleTacticsCatalogue::clear();
    SkillDefinition strike;
    strike.id         = "strike";
    strike.targetType = "enemySingle";
    SkillRegistry::registerSkill(strike);
    SkillDefinition aid;
    aid.id         = "aid";
    aid.targetType = "allySingle";
    SkillRegistry::registerSkill(aid);
    SkillDamageSpec aidSpec;
    aidSpec.damageType = "hpHeal";
    aidSpec.formula    = "20";
    BattleSystem::registerSkillDamage("aid", aidSpec);

    const std::string valid = R"([
      {"id":"ranger","rules":[
        {"skillId":"aid","targetPolicy":"lowestHealthAlly","conditionResource":"hp","belowRatio":0.4},
        {"skillId":"strike","targetPolicy":"lowestHealthEnemy","conditionResource":"","belowRatio":1.0}
      ]}
    ])";
    REQUIRE(BattleTacticsCatalogue::replaceFromJsonStrict(valid).ok());
    CHECK_EQ(BattleTacticsCatalogue::count(), 1);
    CHECK(
        !BattleTacticsCatalogue::replaceFromJsonStrict(
             R"([{"id":"bad","rules":[{"skillId":"aid","targetPolicy":"lowestHealthEnemy","conditionResource":"","belowRatio":1}]}])")
             .ok());
    CHECK_EQ(BattleTacticsCatalogue::count(), 1);

    auto *hero   = makeTacticsActor(20.0, 100.0);
    auto *ranger = makeTacticsActor(10.0, 80.0);
    auto *enemy  = makeTacticsActor(5.0, 100.0);
    hero->setCurrent("hp", 25.0);
    ranger->learnSkill("strike");
    ranger->learnSkill("aid");
    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(ranger, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    auto queued = BattleTacticsCatalogue::queueAction(&battle, ranger, "ranger");
    REQUIRE(queued.ok());
    CHECK_EQ(queued.value(), std::string("aid"));
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(hero->getCurrent("hp"), 45.0);

    hero->release();
    ranger->release();
    enemy->release();
    BattleTacticsCatalogue::clear();
    SkillRegistry::clear();
}

// rpg_battle_victory.cpp
namespace {

void installVictoryQuest() {
    eve::rpg::QuestRegistry::clear();
    eve::rpg::QuestDefinition quest;
    quest.id             = "quest.slayer";
    quest.startPolicy    = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"kill", "kill", "slime", 1});
    eve::rpg::QuestRegistry::registerQuest(quest);
}

eve::rpg::BattleVictoryRequest victoryRequest() {
    eve::rpg::BattleVictoryRequest request;
    request.mapId                 = "village";
    request.objectId              = "slime_west";
    request.requiredQuestId       = "quest.slayer";
    request.xpAmount              = 30.0;
    request.xpGrowth              = 1.2;
    request.attributeId           = "gold";
    request.attributeAmount       = 10.0;
    request.notifyTopic           = "kill";
    request.notifyTarget          = "slime";
    request.notifyAmount          = 1;
    request.defeatCounterId       = "kills";
    request.defeatCounterAmount   = 1;
    request.levelPointAttributeId = "statPoints";
    request.pointsPerLevel        = 3;
    return request;
}

}  // namespace

TEST_CASE("rpg.battleVictory.commitsProgressionQuestRewardsAndEncounterTogether") {
    installVictoryQuest();
    eve::rpg::Tracker tracker;
    REQUIRE(tracker.activate("quest.slayer"));
    eve::rpg::GameState  state;
    eve::rpg::WorldState world(state);
    eve::rpg::RPGActor  *actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    auto progression = actor->restoreProgression(1, 80.0, 100.0);
    REQUIRE(progression.ok());
    std::vector<eve::rpg::LevelUpEvent> oldEvents;
    eve::rpg::LevelSystem::pollLevelUps(oldEvents);

    auto settled = eve::rpg::BattleVictory::settle(*actor, state, tracker, victoryRequest());
    REQUIRE(settled.ok());
    const auto receipt = std::move(settled).takeValue();
    CHECK_EQ(receipt.levelsGained, 1);
    CHECK_EQ(actor->getLevel(), 2);
    CHECK_EQ(actor->getXp(), 10.0);
    CHECK_EQ(state.getVariable("gold"), 10.0);
    CHECK_EQ(state.getVariable("kills"), 1.0);
    CHECK_EQ(state.getVariable("statPoints"), 3.0);
    CHECK_EQ(tracker.getState("quest.slayer"), std::string("ready"));
    CHECK(world.isObjectConsumed("village", "slime_west"));
    std::vector<eve::rpg::LevelUpEvent> events;
    eve::rpg::LevelSystem::pollLevelUps(events);
    REQUIRE_EQ(events.size(), std::size_t(1));
    CHECK(events[0].actor == actor);
    CHECK_EQ(events[0].previousLevel, 1);
    CHECK_EQ(events[0].newLevel, 2);
    actor->release();
    eve::rpg::QuestRegistry::clear();
}

TEST_CASE("rpg.battleVictory.duplicateEncounterFailureLeavesEveryOwnerUntouched") {
    installVictoryQuest();
    eve::rpg::Tracker tracker;
    REQUIRE(tracker.activate("quest.slayer"));
    eve::rpg::GameState  state;
    eve::rpg::WorldState world(state);
    auto                 consumed = world.consumeObject("village", "slime_west");
    REQUIRE(consumed.ok());
    eve::rpg::RPGActor *actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    auto progression = actor->restoreProgression(1, 80.0, 100.0);
    REQUIRE(progression.ok());
    std::vector<eve::rpg::LevelUpEvent> oldEvents;
    eve::rpg::LevelSystem::pollLevelUps(oldEvents);

    auto settled = eve::rpg::BattleVictory::settle(*actor, state, tracker, victoryRequest());
    REQUIRE(!settled.ok());
    CHECK_EQ(actor->getLevel(), 1);
    CHECK_EQ(actor->getXp(), 80.0);
    CHECK_EQ(state.getVariable("gold"), 0.0);
    CHECK_EQ(state.getVariable("kills"), 0.0);
    CHECK_EQ(state.getVariable("statPoints"), 0.0);
    CHECK_EQ(tracker.getState("quest.slayer"), std::string("active"));
    std::vector<eve::rpg::LevelUpEvent> events;
    eve::rpg::LevelSystem::pollLevelUps(events);
    CHECK(events.empty());
    actor->release();
    eve::rpg::QuestRegistry::clear();
}

TEST_CASE("rpg.progression.preparedGainRejectsStaleActorWithoutEvents") {
    eve::rpg::RPGActor *actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    auto progression = actor->restoreProgression(1, 80.0, 100.0);
    REQUIRE(progression.ok());
    auto prepared = eve::rpg::LevelSystem::prepareGainXp(actor, 30.0, 1.2);
    REQUIRE(prepared.ok());
    auto changed = actor->restoreProgression(1, 70.0, 100.0);
    REQUIRE(changed.ok());
    auto committed = eve::rpg::LevelSystem::commitGainXp(std::move(prepared).takeValue());
    REQUIRE(!committed.ok());
    CHECK_EQ(actor->getLevel(), 1);
    CHECK_EQ(actor->getXp(), 70.0);
    std::vector<eve::rpg::LevelUpEvent> events;
    eve::rpg::LevelSystem::pollLevelUps(events);
    CHECK(events.empty());
    actor->release();
}
}  // namespace test_rpg_battle_tactics
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_class)
#include "PrivateTestScope.h"
#include <cmath>
#include "rpg/Class.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_class.cpp
namespace test_rpg_class {
using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.class.registryJson") {
    ClassRegistry::clear();
    int n = ClassRegistry::loadFromJson(R"([
      {"id":"warrior","displayName":"战士","tags":["physical"],
       "traits":["mighty"],
       "learnSkills":[{"skillId":"power_strike","level":2},{"skillId":"cleave","level":5}]},
      {"id":"mage","displayName":"法师","learnSkills":[{"skillId":"fireball","level":1}]}
    ])");
    CHECK_EQ(n, 2);
    const ClassDefinition *w = ClassRegistry::find("warrior");
    REQUIRE(w != nullptr);
    CHECK(w->hasTag("physical"));
    REQUIRE(w->learnSkills.size() == 2);
    CHECK_EQ(w->learnSkills[1].skillId, std::string("cleave"));
    CHECK_EQ(w->learnSkills[1].level, 5);
    ClassRegistry::clear();
    CHECK_EQ(ClassRegistry::count(), 0);
}

TEST_CASE("rpg.class.setAppliesTraitsAndLearnsSkills") {
    auto *rpg = RPG::create();
    rpg->clearClassDefinitions();
    rpg->clearTraitDefinitions();
    rpg->clearSkillDefinitions();
    rpg->registerSkillsFromJson(R"([
      {"id":"power_strike"},{"id":"cleave"},{"id":"fireball"}
    ])");
    rpg->registerTraitsFromJson(R"([{"id":"mighty","traits":[{"kind":"paramRate","target":"attack","value":1.5}]}])");
    int classes = rpg->registerClassesFromJson(R"([
      {"id":"warrior","traits":["mighty"],
       "learnSkills":[{"skillId":"power_strike","level":2},{"skillId":"cleave","level":5}]},
      {"id":"mage","learnSkills":[{"skillId":"fireball","level":1}]}
    ])");
    CHECK_EQ(classes, 2);

    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100.0);
    CHECK(actor->setClass("warrior"));
    CHECK(actor->hasClass("warrior"));
    CHECK_EQ(actor->getClassId(), std::string("warrior"));
    // 职业特征 → attack * 1.5
    CHECK(approxEq(actor->getFinalAttribute("attack"), 150.0));
    // 等级 1：power_strike(2) 未解锁，cleave(5) 未解锁
    CHECK(!actor->knowsSkill("power_strike"));
    CHECK_EQ(actor->getClassLearnCount(), 2);

    // 升到 3 级后补学
    actor->setXpToNext(10.0);
    int learned = actor->gainXp(30.0);  // 升多级
    CHECK(learned);
    int n = actor->checkLevelSkills();
    CHECK(n >= 1);
    CHECK(actor->knowsSkill("power_strike"));  // 2 级已解锁
    CHECK(!actor->knowsSkill("cleave"));       // 5 级仍未解锁

    // 换职业撤销旧职业特征
    CHECK(actor->setClass("mage"));
    CHECK(approxEq(actor->getFinalAttribute("attack"), 100.0));  // 职业特征撤销

    rpg->clearClassDefinitions();
    rpg->clearTraitDefinitions();
    rpg->clearSkillDefinitions();
    actor->release();
}

TEST_CASE("rpg.class.checkLevelSkillsIdempotent") {
    auto *rpg = RPG::create();
    rpg->clearClassDefinitions();
    rpg->clearSkillDefinitions();
    rpg->registerSkillsFromJson(R"([{"id":"strike"}])");
    rpg->registerClassesFromJson(R"([{"id":"g","learnSkills":[{"skillId":"strike","level":1}]}])");
    RPGActor *actor = rpg->newActor();
    actor->setClass("g");
    CHECK(actor->knowsSkill("strike"));  // 1 级即时学到
    int again = actor->checkLevelSkills();
    CHECK_EQ(again, 0);  // 幂等，不再重复学
    rpg->clearClassDefinitions();
    rpg->clearSkillDefinitions();
    actor->release();
}
}  // namespace test_rpg_class
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_dialect)
#include "PrivateTestScope.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgDialect.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_dialect.cpp
namespace test_rpg_dialect {
namespace {

using namespace eve::rpg;

/** @brief Borrowed RPG objects one story runs against, plus their owning actor. */
struct StoryFixture {
    GameState                    state;
    Party                        party;
    eve::inventory::Bag          bag{8};
    eve::inventory::EquipmentSet equipment;
    RPGActor                    *hero = nullptr;

    StoryFixture() {
        hero = RPGActor::createActor();
        party.addMember("hero", hero).ignore("story fixture");
        equipment.defineSlot("weapon");
        hero->setBaseAttribute("attack", 10.0);

        eve::inventory::ItemDefinition potion;
        potion.id          = "potion";
        potion.displayName = "Potion";
        potion.maxStack    = 99;
        potion.tags        = {"consumable"};
        eve::inventory::ItemRegistry::registerItem(potion);

        eve::inventory::ItemDefinition sword;
        sword.id          = "iron-sword";
        sword.displayName = "Iron Sword";
        sword.maxStack    = 1;
        sword.equipSlot   = "weapon";
        sword.tags        = {"weapon"};
        eve::inventory::ItemRegistry::registerItem(sword);

        // The `equipment` step equips *from the bag*, so the sword starts there.
        bag.addItem("iron-sword", 1);
    }

    ~StoryFixture() {
        eve::inventory::ItemRegistry::clear();
        if (hero) hero->release();
    }

    StoryFixture(const StoryFixture &)            = delete;
    StoryFixture &operator=(const StoryFixture &) = delete;

    RpgStoryBinding binding() {
        RpgStoryBinding value;
        value.gameState = &state;
        value.party     = &party;
        value.bag       = &bag;
        value.equipment = &equipment;
        return value;
    }
};

/**
 * @brief Reference movement port: a tiny continuous-space world.
 *
 * It deliberately has no grid, no path graph and no tile size. The point of the
 * port contract is that a tile RPG, a tactics board and a continuous world
 * satisfy it identically, so the engine never has to pick one — and a test can
 * prove it by using fractional coordinates a grid model could not represent.
 */
class FakeMoveWorld {
public:
    struct Position {
        double x = 0.0;
        double y = 0.0;
    };

    eve::Result<StoryMoveStatus> begin(const StoryMoveRequest &request) {
        requests.push_back(request);
        if (refuse)
            return eve::Result<StoryMoveStatus>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "fake world: the movement service is offline", "move", {}, "test"));
        if (!reachable) return eve::Result<StoryMoveStatus>::success(StoryMoveStatus::Unreachable);
        const Position &current = positions[request.actorId];
        if (current.x == request.x && current.y == request.y)
            return eve::Result<StoryMoveStatus>::success(StoryMoveStatus::Arrived);
        pending[request.actorId] = {request.x, request.y};
        return eve::Result<StoryMoveStatus>::success(StoryMoveStatus::Travelling);
    }

    StoryMoveHandler handler() {
        return [this](const StoryMoveRequest &request) { return begin(request); };
    }

    /** @brief Complete every in-flight move, the way a host update loop would. */
    void finishAll() {
        for (const auto &entry : pending) positions[entry.first] = entry.second;
        pending.clear();
    }

    [[nodiscard]] bool isTravelling(const std::string &actorId) const { return pending.contains(actorId); }

    [[nodiscard]] Position positionOf(const std::string &actorId) const {
        const auto found = positions.find(actorId);
        return found == positions.end() ? Position{} : found->second;
    }

    bool                            reachable = true;
    bool                            refuse    = false;
    std::vector<StoryMoveRequest>   requests;
    std::map<std::string, Position> positions;
    std::map<std::string, Position> pending;
};

/**
 * @brief Reference animation port: a tiny playback stage with no rig at all.
 *
 * It deliberately has no skeleton, no clip registry and no clip format: `clip` is
 * just an opaque string it echoes back. The point of the port contract is that a
 * skeletal stack, a sprite sequence and a Spine setup satisfy it identically, so
 * the engine never has to pick one — and a test can prove it by playing a clip id
 * that no importer could ever resolve.
 *
 * `instant` models the other end of the contract: a host whose non-looping
 * playback is already over when the call returns. A looping clip always reports
 * `Playing`, because a clip with no end has no end for the host to report.
 */
class FakeAnimStage {
public:
    eve::Result<StoryAnimationStatus> begin(const StoryAnimationRequest &request) {
        requests.push_back(request);
        if (refuse)
            return eve::Result<StoryAnimationStatus>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "fake stage: the clip library is still streaming",
                                       "animation", {}, "test"));
        if (!available) return eve::Result<StoryAnimationStatus>::success(StoryAnimationStatus::Unavailable);
        if (!request.loop && instant) return eve::Result<StoryAnimationStatus>::success(StoryAnimationStatus::Finished);
        playing[request.targetId] = request.clip;
        return eve::Result<StoryAnimationStatus>::success(StoryAnimationStatus::Playing);
    }

    StoryAnimationHandler handler() {
        return [this](const StoryAnimationRequest &request) { return begin(request); };
    }

    /** @brief Finish every in-flight clip, the way a host update loop would. */
    void finishAll() { playing.clear(); }

    [[nodiscard]] bool isPlaying(const std::string &targetId) const { return playing.contains(targetId); }

    bool                               available = true;
    bool                               refuse    = false;
    bool                               instant   = false;
    std::vector<StoryAnimationRequest> requests;
    std::map<std::string, std::string> playing;
};

}  // namespace

TEST_CASE("rpg.dnut.runsDomainAndPresentationSteps") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.arrival {
    move actor=hero x=10 y=4 duration=0.5
    skill actor=hero learn=fireball
    attribute actor=hero name=attack op=add value=5
    item add=potion count=2
    equipment actor=hero equip=iron-sword slot=weapon
    flag name=village.visited state=true
    variable name=gold op=add value=25
    message text=Arrived
}
)",
                                                     "village.dnut")
                .ok());
    CHECK_EQ(RpgStoryCatalogue::count(), 1);
    CHECK(RpgStoryCatalogue::contains("village.arrival"));

    RpgStorySession session;
    REQUIRE(session.begin("village.arrival", &binding).ok());
    CHECK(session.isActive());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("move"));
    CHECK_EQ(session.getStepPayload().find("actor")->asString(), std::string("hero"));

    // Presentation step: the domain effects have not run yet.
    CHECK(!fixture.hero->knowsSkill("fireball"));

    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    CHECK(fixture.hero->knowsSkill("fireball"));
    CHECK_EQ(fixture.hero->getBaseAttribute("attack"), 15.0);
    CHECK_EQ(fixture.bag.countItem("potion"), 2);
    CHECK_EQ(fixture.equipment.getSlotItemId("weapon"), std::string("iron-sword"));
    CHECK(fixture.state.isSwitchOn("village.visited"));
    CHECK_EQ(fixture.state.getVariable("gold"), 25.0);

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());
    CHECK(fixture.state.hasSelfVariable("story.village.arrival", "completed"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.rejectsInvalidStepPayloads") {
    RpgStoryCatalogue::clear();
    const auto rejected = [](const std::string &body) {
        return !RpgStoryCatalogue::replaceFromDnutStrict("story probe {\n" + body + "\n}\n", "probe.dnut").ok();
    };

    CHECK(rejected("    teleport actor=hero"));
    CHECK(rejected("    skill actor=hero"));
    CHECK(rejected("    skill actor=hero learn=fireball forget=fireball"));
    CHECK(rejected("    attribute actor=hero name=attack op=bogus value=1"));
    CHECK(rejected("    attribute actor=hero name=attack op=add"));
    CHECK(rejected("    equipment actor=hero equip=iron-sword"));
    CHECK(rejected("    item add=potion remove=potion"));
    CHECK(rejected("    move actor=hero x=1 y=2 z=3"));
    CHECK(rejected("    variable name=gold op=multiply value=2"));
    CHECK(rejected("    if switch.gate == true {\n        teleport\n    }"));

    // A document without any story block must not silently wipe the catalogue.
    CHECK(!RpgStoryCatalogue::replaceFromDnutStrict("pool village {\n    elder: \"hi\"\n}\n", "pool.dnut").ok());
    CHECK_EQ(RpgStoryCatalogue::count(), 0);
}

TEST_CASE("rpg.dnut.gatesNonRepeatableStoriesOnCompletion") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story once {
    variable name=once op=set value=1
}
story again repeatable {
    variable name=again op=add value=1
}
)",
                                                     "gates.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("once", &binding).ok());
    CHECK(!session.isActive());
    CHECK_EQ(fixture.state.getVariable("once"), 1.0);
    auto repeat = session.begin("once", &binding);
    CHECK(!repeat.ok());
    const auto *repeatDiagnostic = repeat.error();
    REQUIRE(repeatDiagnostic != nullptr);
    CHECK(repeatDiagnostic->message().find("already complete") != std::string::npos);

    REQUIRE(session.begin("again", &binding).ok());
    REQUIRE(session.begin("again", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("again"), 2.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.branchesOnGameState") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story gate.check repeatable {
    if switch.gate == true {
        variable name=path op=set value=1
    } else {
        variable name=path op=set value=2
    }
}
)",
                                                     "gate.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("gate.check", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("path"), 2.0);

    fixture.state.switchOn("gate");
    REQUIRE(session.begin("gate.check", &binding).ok());
    CHECK_EQ(fixture.state.getVariable("path"), 1.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.selectsChoiceRoutes") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story shop.buy repeatable {
    choice {
        option "Buy" when variable.gold >= 10 {
            variable name=bought op=set value=1
        }
        option "Leave" {
            variable name=bought op=set value=0
        }
    }
}
)",
                                                     "shop.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("shop.buy", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("choice"));
    REQUIRE_EQ(session.getChoiceLabels().size(), 2u);
    CHECK_EQ(session.getChoiceLabels()[0], std::string("Buy"));

    // The conditional route is rejected while the state does not satisfy it.
    CHECK(!session.select("Buy").ok());
    CHECK(session.isBlocked());
    CHECK(!session.select("Nothing").ok());

    REQUIRE(session.select("Leave").ok());
    CHECK_EQ(fixture.state.getVariable("bought"), 0.0);

    fixture.state.setVariable("gold", 20.0);
    REQUIRE(session.begin("shop.buy", &binding).ok());
    REQUIRE(session.select("Buy").ok());
    CHECK_EQ(fixture.state.getVariable("bought"), 1.0);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.reportFailureWhenDomainStateIsMissing") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story broke {
    item remove=potion count=3
}
)",
                                                     "broke.dnut")
                .ok());

    RpgStorySession session;
    auto            start = session.begin("broke", &binding);
    CHECK(!start.ok());
    CHECK(!session.isActive());
    const auto *diagnostic = start.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("does not hold") != std::string::npos);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.capturesAndRestoresCursor") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story resume.me {
    variable name=step op=set value=1
    message text=pause
    variable name=step op=set value=2
}
)",
                                                     "resume.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("resume.me", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);

    eve::Value captured;
    REQUIRE(session.captureState(captured).ok());

    RpgStorySession restored;
    REQUIRE(restored.restoreState("resume.me", captured, &binding).ok());
    CHECK_EQ(restored.getStepKind(), std::string("message"));
    REQUIRE(restored.advance().ok());
    CHECK_EQ(fixture.state.getVariable("step"), 2.0);
    CHECK(!restored.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.drivesActorMovementThroughTheHostController") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.walk {
    move actor=hero x=10 y=4 duration=0.5
    message text=Arrived
}
)",
                                                     "walk.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.walk", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("move"));

    // Effect verification: the controller received exactly this move, and the
    // actor has not moved yet because the move is still travelling.
    REQUIRE_EQ(world.requests.size(), 1u);
    const StoryMoveRequest &request = world.requests.front();
    CHECK_EQ(request.actorId, std::string("hero"));
    CHECK_EQ(request.x, 10.0);
    CHECK_EQ(request.y, 4.0);
    CHECK(request.hasDuration);
    CHECK_EQ(request.duration, 0.5);
    CHECK(request.actor == fixture.hero);
    CHECK(world.isTravelling("hero"));
    CHECK_EQ(world.positionOf("hero").x, 0.0);
    CHECK_EQ(world.positionOf("hero").y, 0.0);

    // The story stays on the move step until the host reports the arrival.
    world.finishAll();
    REQUIRE(session.advance().ok());
    CHECK_EQ(world.positionOf("hero").x, 10.0);
    CHECK_EQ(world.positionOf("hero").y, 4.0);
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // A move is dispatched once, not once per acknowledgement.
    CHECK_EQ(world.requests.size(), 1u);

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.completesMoveInlineWhenTheActorIsAlreadyThere") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    // Fractional coordinates: the engine must not snap them to a grid.
    world.positions["hero"] = {2.5, 7.25};
    binding.moveActor       = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.snap {
    move actor=hero x=2.5 y=7.25
    variable name=step op=set value=1
}
)",
                                                     "snap.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.snap", &binding).ok());
    // `Arrived` completes the step inline, so the story never suspends on it.
    CHECK(!session.isActive());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    REQUIRE_EQ(world.requests.size(), 1u);
    CHECK_EQ(world.requests.front().x, 2.5);
    CHECK_EQ(world.requests.front().y, 7.25);
    CHECK(!world.requests.front().hasDuration);
    CHECK(!world.isTravelling("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.failsMoveWhenTheDestinationIsUnreachable") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    world.reachable   = false;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.blocked {
    move actor=hero x=99 y=99
}
)",
                                                     "blocked.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.blocked", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto *diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("cannot reach") != std::string::npos);
    CHECK(diagnostic->message().find("99") != std::string::npos);
    CHECK(!world.isTravelling("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.propagatesMovementControllerFailure") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    world.refuse      = true;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.offline {
    move actor=hero x=1 y=1
}
)",
                                                     "offline.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.offline", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto *diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("movement service is offline") != std::string::npos);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.presentsMoveWhenNoControllerIsBound") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.manual {
    move actor=hero x=10 y=4 duration=0.5
    message text=Arrived
}
)",
                                                     "manual.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.manual", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("move"));
    CHECK(!binding.moveActor);

    // The host presentation path is unchanged: read the payload and advance.
    CHECK_EQ(session.getStepPayload().find("actor")->asString(), std::string("hero"));
    CHECK_EQ(session.getStepPayload().find("x")->asInt(), std::int64_t(10));
    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // Nothing reached a movement controller, because none was installed.
    CHECK(world.requests.empty());

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.movesSubjectsOutsideThePartyRoster") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.pan {
    move actor=camera.main x=-2.5 y=7.25
}
)",
                                                     "pan.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.pan", &binding).ok());

    // The subject is not a party member, so the convenience projection is null —
    // but the step still runs, because the host owns the subject space.
    REQUIRE_EQ(world.requests.size(), 1u);
    CHECK_EQ(world.requests.front().actorId, std::string("camera.main"));
    CHECK(world.requests.front().actor == nullptr);
    CHECK_EQ(world.requests.front().x, -2.5);
    CHECK_EQ(world.requests.front().y, 7.25);
    CHECK(world.isTravelling("camera.main"));

    world.finishAll();
    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.keepsTravellingMoveAcrossCaptureAndRestore") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeMoveWorld   world;
    binding.moveActor = world.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.longwalk {
    move actor=hero x=6 y=2
    variable name=step op=set value=1
}
)",
                                                     "longwalk.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.longwalk", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("move"));

    eve::Value captured;
    REQUIRE(session.captureState(captured).ok());

    RpgStorySession restored;
    REQUIRE(restored.restoreState("village.longwalk", captured, &binding).ok());
    CHECK_EQ(restored.getStepKind(), std::string("move"));

    // The restored session does not re-dispatch the move; the host finishes it.
    world.finishAll();
    REQUIRE(restored.advance().ok());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    CHECK(!restored.isActive());
    CHECK_EQ(world.positionOf("hero").x, 6.0);
    CHECK_EQ(world.requests.size(), 1u);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.drivesAnimationThroughTheHostController") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.cheer {
    animation target=hero clip=emote.cheer loop=true hold=true
    message text=Cheered
}
)",
                                                     "cheer.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.cheer", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("animation"));

    // Effect verification: the controller received exactly this request, and the
    // clip has not ended because it is still playing.
    REQUIRE_EQ(stage.requests.size(), 1u);
    const StoryAnimationRequest &request = stage.requests.front();
    CHECK_EQ(request.targetId, std::string("hero"));
    CHECK_EQ(request.clip, std::string("emote.cheer"));
    CHECK(request.loop);
    CHECK(request.hold);
    CHECK(request.actor == fixture.hero);
    CHECK(stage.isPlaying("hero"));

    // The story stays on the animation step until the host reports the end.
    stage.finishAll();
    REQUIRE(session.advance().ok());
    CHECK(!stage.isPlaying("hero"));
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // A clip is dispatched once, not once per acknowledgement.
    CHECK_EQ(stage.requests.size(), 1u);

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.completesAnimationInlineWhenTheHostFinishesImmediately") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    // A host whose playback is instantaneous completes the step without a loop.
    stage.instant         = true;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.blink {
    animation target=hero clip=emote.blink
    variable name=step op=set value=1
}
)",
                                                     "blink.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.blink", &binding).ok());
    // `Finished` completes the step inline, so the story never suspends on it.
    CHECK(!session.isActive());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    REQUIRE_EQ(stage.requests.size(), 1u);
    CHECK_EQ(stage.requests.front().clip, std::string("emote.blink"));
    // Omitted flags default to false rather than to a host-specific guess.
    CHECK(!stage.requests.front().loop);
    CHECK(!stage.requests.front().hold);
    CHECK(!stage.isPlaying("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.failsAnimationWhenTheClipIsUnavailable") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    stage.available       = false;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.missing {
    animation target=hero clip=emote.missing
}
)",
                                                     "missing.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.missing", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto *diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("cannot play clip") != std::string::npos);
    CHECK(diagnostic->message().find("emote.missing") != std::string::npos);
    CHECK(diagnostic->message().find("hero") != std::string::npos);
    CHECK(!stage.isPlaying("hero"));

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.propagatesAnimationControllerFailure") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    stage.refuse          = true;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.streaming {
    animation target=hero clip=emote.cheer
}
)",
                                                     "streaming.dnut")
                .ok());

    RpgStorySession session;
    auto            started = session.begin("village.streaming", &binding);
    CHECK(!started.ok());
    CHECK(!session.isActive());
    const auto *diagnostic = started.error();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->message().find("clip library is still streaming") != std::string::npos);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.presentsAnimationWhenNoControllerIsBound") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.manualclip {
    animation target=hero clip=emote.wave
    message text=Done
}
)",
                                                     "manualclip.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.manualclip", &binding).ok());
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("animation"));
    CHECK(!binding.playAnimation);

    // The host presentation path is unchanged: read the payload and advance.
    CHECK_EQ(session.getStepPayload().find("target")->asString(), std::string("hero"));
    CHECK_EQ(session.getStepPayload().find("clip")->asString(), std::string("emote.wave"));
    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));

    // Nothing reached a playback controller, because none was installed.
    CHECK(stage.requests.empty());

    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.animatesSubjectsOutsideThePartyRoster") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.flash {
    animation target=fx.torch clip=fx.flicker loop=true
}
)",
                                                     "flash.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.flash", &binding).ok());

    // The subject is not a party member, so the convenience projection is null —
    // but the step still runs, because the host owns the subject space.
    REQUIRE_EQ(stage.requests.size(), 1u);
    CHECK_EQ(stage.requests.front().targetId, std::string("fx.torch"));
    CHECK(stage.requests.front().actor == nullptr);
    CHECK_EQ(stage.requests.front().clip, std::string("fx.flicker"));
    CHECK(stage.requests.front().loop);
    CHECK(stage.isPlaying("fx.torch"));

    stage.finishAll();
    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.keepsPlayingAnimationAcrossCaptureAndRestore") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    FakeAnimStage   stage;
    binding.playAnimation = stage.handler();

    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story village.emote {
    animation target=hero clip=emote.dance
    variable name=step op=set value=1
}
)",
                                                     "emote.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("village.emote", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("animation"));

    eve::Value captured;
    REQUIRE(session.captureState(captured).ok());

    RpgStorySession restored;
    REQUIRE(restored.restoreState("village.emote", captured, &binding).ok());
    CHECK_EQ(restored.getStepKind(), std::string("animation"));

    // The restored session does not restart the clip; the host finishes it.
    stage.finishAll();
    REQUIRE(restored.advance().ok());
    CHECK_EQ(fixture.state.getVariable("step"), 1.0);
    CHECK(!restored.isActive());
    CHECK(!stage.isPlaying("hero"));
    CHECK_EQ(stage.requests.size(), 1u);

    RpgStoryCatalogue::clear();
}

TEST_CASE("rpg.dnut.persistsCursorIntoGameStateAcrossSaveRestore") {
    StoryFixture    fixture;
    RpgStoryBinding binding = fixture.binding();
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story forest.arrival {
    message text=hello
    wait 0.5
    variable name=done op=set value=1
}
)",
                                                     "persist.dnut")
                .ok());

    RpgStorySession session;
    REQUIRE(session.begin("forest.arrival", &binding).ok());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    CHECK(fixture.state.hasSelfString("story.forest.arrival", "cursor"));
    REQUIRE(session.advance().ok());
    CHECK_EQ(session.getStepKind(), std::string("wait"));

    auto snapshot = fixture.state.snapshotJson();
    REQUIRE(snapshot.ok());

    GameState restoredState;
    REQUIRE(restoredState.restoreSnapshotJson(snapshot.value()).ok());
    CHECK(restoredState.hasSelfString("story.forest.arrival", "cursor"));

    RpgStoryBinding restoredBinding = binding;
    restoredBinding.gameState       = &restoredState;
    RpgStorySession resumed;
    REQUIRE(resumed.begin("forest.arrival", &restoredBinding).ok());
    CHECK_EQ(resumed.getStepKind(), std::string("wait"));
    REQUIRE(resumed.advance().ok());
    CHECK(!resumed.isActive());
    CHECK_EQ(restoredState.getVariable("done"), 1.0);
    CHECK_EQ(restoredState.getSelfVariable("story.forest.arrival", "completed"), 1.0);
    CHECK(!restoredState.hasSelfString("story.forest.arrival", "cursor"));

    RpgStoryCatalogue::clear();
}
}  // namespace test_rpg_dialect
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_gamestate)
#include "PrivateTestScope.h"
#include "rpg/GameState.h"
#include "rpg/RPG.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_gamestate.cpp
namespace test_rpg_gamestate {
using namespace eve::rpg;

TEST_CASE("rpg.gameState.switchesAndVariables") {
    GameState gs;
    CHECK(!gs.isSwitchOn("door"));
    gs.switchOn("door");
    CHECK(gs.isSwitchOn("door"));
    gs.switchOff("door");
    CHECK(!gs.isSwitchOn("door"));
    gs.setSwitch("flag", true);
    CHECK(gs.isSwitchOn("flag"));

    CHECK_EQ(gs.getVariable("gold"), 0.0);
    gs.setVariable("gold", 100.0);
    CHECK_EQ(gs.getVariable("gold"), 100.0);
    gs.addVariable("gold", 25.0);
    CHECK_EQ(gs.getVariable("gold"), 125.0);

    gs.clear();
    CHECK(!gs.isSwitchOn("flag"));
    CHECK_EQ(gs.getVariable("gold"), 0.0);
}

TEST_CASE("rpg.gameState.selfVariablesScoped") {
    GameState gs;
    gs.setSelfVariable("map:1:event:3", "switchA", 1.0);
    CHECK(gs.hasSelfVariable("map:1:event:3", "switchA"));
    CHECK_EQ(gs.getSelfVariable("map:1:event:3", "switchA"), 1.0);
    // 不同 scope 隔离
    CHECK(!gs.hasSelfVariable("map:1:event:4", "switchA"));
    CHECK_EQ(gs.getSelfVariable("map:1:event:3", "missing"), 0.0);

    gs.setSelfString("story.forest.arrival", "cursor", "{\"active\":true}");
    CHECK(gs.hasSelfString("story.forest.arrival", "cursor"));
    CHECK_EQ(gs.getSelfString("story.forest.arrival", "cursor"), std::string("{\"active\":true}"));
    gs.clearSelfString("story.forest.arrival", "cursor");
    CHECK(!gs.hasSelfString("story.forest.arrival", "cursor"));
}

TEST_CASE("rpg.gameState.facadeAndGlobal") {
    auto *rpg = RPG::create();
    // 独立实例
    GameState *gs = rpg->newGameState();
    REQUIRE(gs != nullptr);
    gs->setVariable("hp", 50.0);
    gs->setSwitch("boss_alive", true);
    CHECK_EQ(gs->getVariable("hp"), 50.0);
    CHECK(gs->isSwitchOn("boss_alive"));

    // 全局单例
    GameState *g = rpg->globalGameState();
    REQUIRE(g != nullptr);
    g->clear();
    g->setVariable("level", 7.0);
    CHECK_EQ(GameState::global().getVariable("level"), 7.0);
}

TEST_CASE("rpg.gameState.versionedSnapshotRoundTripAndAtomicFailure") {
    eve::rpg::GameState source;
    source.switchOn("door.open");
    source.setVariable("gold", 125.5);
    source.setSelfVariable("map:1:event:3", "visits", 2.0);
    source.setSelfString("story.forest.arrival", "cursor", "{\"blocked\":true}");

    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    eve::rpg::GameState restored;
    restored.setVariable("sentinel", 7.0);
    auto applied = restored.restoreSnapshotJson(encoded.value());
    REQUIRE(applied.ok());
    CHECK(restored.isSwitchOn("door.open"));
    CHECK_EQ(restored.getVariable("gold"), 125.5);
    CHECK_EQ(restored.getSelfVariable("map:1:event:3", "visits"), 2.0);
    CHECK_EQ(restored.getSelfString("story.forest.arrival", "cursor"), std::string("{\"blocked\":true}"));
    CHECK_EQ(restored.getVariable("sentinel"), 0.0);

    auto withoutStrings = restored.restoreSnapshotJson(
        R"({"schema":"eve.rpg.game-state","version":1,"switches":{},"variables":{"gold":3},"selfVariables":{}})");
    REQUIRE(withoutStrings.ok());
    CHECK_EQ(restored.getVariable("gold"), 3.0);
    CHECK(!restored.hasSelfString("story.forest.arrival", "cursor"));

    auto malformed = restored.restoreSnapshotJson(
        R"({"schema":"eve.rpg.game-state","version":1,"switches":{},"variables":{"gold":"bad"},"selfVariables":{}})");
    CHECK(!malformed.ok());
    CHECK_EQ(restored.getVariable("gold"), 3.0);

    auto future = restored.restoreSnapshotJson(
        R"({"schema":"eve.rpg.game-state","version":2,"switches":{},"variables":{},"selfVariables":{}})");
    CHECK(!future.ok());
    CHECK_EQ(restored.getVariable("gold"), 3.0);
}
}  // namespace test_rpg_gamestate
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_rpg) ||     \
    defined(EVE_TEST_ORIGIN_rpg_encounter_catalogue) || defined(EVE_TEST_ORIGIN_rpg_party) || \
    defined(EVE_TEST_ORIGIN_rpg_state_canonical)
#include "PrivateTestScope.h"
#include <utility>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Value.h"
#include "rpg/Battle.h"
#include "rpg/BattleControl.h"
#include "rpg/EncounterCatalogue.h"
#include "rpg/Party.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: rpg_party_encounters
namespace test_rpg_party_encounters {
// gameplay_control_rpg.cpp
namespace {
namespace gameplay_control_rpg {
namespace {

eve::SubjectRef subject(const char *text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char *text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

eve::rpg::RPGActor *fighter(double attack, double hp, double speed) {
    auto *actor = eve::rpg::RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", 0.0);
    actor->setBaseAttribute("hp", hp);
    actor->setBaseAttribute("speed", speed);
    actor->setCurrent("hp", hp);
    return actor;
}

}  // namespace

TEST_CASE("gameplay.control.rpgBattleUsesTheSameCheckedActionForPlayerAndAutomation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto            *hero  = fighter(50.0, 100.0, 10.0);
    auto            *enemy = fighter(1.0, 20.0, 1.0);
    eve::rpg::Battle battle;
    battle.addActor(hero, eve::rpg::BattleSide::Party);
    battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    const auto              battleSubject = subject("00000000-0000-7000-8000-000000000601");
    const auto              heroSubject   = subject("00000000-0000-7000-8000-000000000602");
    const auto              enemySubject  = subject("00000000-0000-7000-8000-000000000603");
    eve::rpg::BattleControl control(battle, battleSubject);
    REQUIRE(control.bindParticipant(heroSubject, hero).ok());
    REQUIRE(control.bindParticipant(enemySubject, enemy).ok());

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto *provider) {
        if (provider == &control && provider->gameplayDomain() == "rpg.battle") discovered = true;
    });
    CHECK(discovered);

    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {heroSubject}};
    eve::GameplaySession automation{"automation", eve::GameplayAccess::TestDriver, {heroSubject}};
    auto                 playerActions     = control.availableGameplayActions(player, battleSubject, heroSubject);
    auto                 automationActions = control.availableGameplayActions(automation, battleSubject, heroSubject);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    const auto playerDescriptors     = std::move(playerActions).takeValue();
    const auto automationDescriptors = std::move(automationActions).takeValue();
    REQUIRE_EQ(playerDescriptors.size(), automationDescriptors.size());
    for (std::size_t index = 0; index < playerDescriptors.size(); ++index)
        CHECK_EQ(playerDescriptors[index].id, automationDescriptors[index].id);

    auto observed = control.observeGameplay(player, battleSubject);
    REQUIRE(observed.ok());
    const auto           before = std::move(observed).takeValue();
    eve::GameplayCommand attack;
    attack.id               = "rpg-player-command-1";
    attack.action           = action("rpg:attack");
    attack.subject          = heroSubject;
    attack.observedTick     = before.tick;
    attack.expectedRevision = before.revision;
    attack.parameters       = eve::Value(eve::Value::Object{{"target", eve::Value(enemySubject.format())}});
    auto submitted          = control.submitGameplay(player, battleSubject, attack);
    REQUIRE(submitted.ok());
    auto advanced =
        control.advanceGameplay(player, battleSubject, {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)});
    REQUIRE(advanced.ok());
    CHECK(battle.isVictory());

    // 目录能力：该适配器服务的就是这一个实例，`instances` 因此能列出它。
    const auto catalogInstances = control.gameplayInstances();
    REQUIRE_EQ(catalogInstances.size(), std::size_t{1});
    CHECK_EQ(catalogInstances[0].format(), battleSubject.format());
    eve::IGameplayInstanceCatalog *catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto *candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "rpg.battle") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{1});

    auto events = control.gameplayEvents(player, battleSubject, 0);
    REQUIRE(events.ok());
    bool hasCausedDamage = false;
    for (const auto &event : std::move(events).takeValue())
        if (event.type == "rpg.battle.damage" && event.causationCommandId == attack.id) hasCausedDamage = true;
    CHECK(hasCausedDamage);

    hero->release();
    enemy->release();
}

TEST_CASE("gameplay.control.rpgBattleRejectsStaleCommandWithoutQueueMutation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto            *hero  = fighter(10.0, 100.0, 2.0);
    auto            *enemy = fighter(10.0, 100.0, 1.0);
    eve::rpg::Battle battle;
    battle.addActor(hero, eve::rpg::BattleSide::Party);
    battle.addActor(enemy, eve::rpg::BattleSide::Enemies);
    const auto              battleSubject = subject("00000000-0000-7000-8000-000000000611");
    const auto              heroSubject   = subject("00000000-0000-7000-8000-000000000612");
    const auto              enemySubject  = subject("00000000-0000-7000-8000-000000000613");
    eve::rpg::BattleControl control(battle, battleSubject);
    REQUIRE(control.bindParticipant(heroSubject, hero).ok());
    REQUIRE(control.bindParticipant(enemySubject, enemy).ok());
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {heroSubject}};

    eve::GameplayCommand stale;
    stale.id               = "rpg-stale";
    stale.action           = action("rpg:attack");
    stale.subject          = heroSubject;
    stale.expectedRevision = 0;
    stale.parameters       = eve::Value(eve::Value::Object{{"target", eve::Value(enemySubject.format())}});
    auto rejected          = control.submitGameplay(player, battleSubject, stale);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);

    hero->release();
    enemy->release();
}
}  // namespace gameplay_control_rpg
}  // namespace

// rpg_encounter_catalogue.cpp
namespace {
namespace rpg_encounter_catalogue {
namespace {

void registerEncounterDependencies() {
    eve::rpg::SkillRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillDefinition skill;
    skill.id = "skill.enemy_claw";
    eve::rpg::SkillRegistry::registerSkill(skill);
    eve::rpg::QuestDefinition quest;
    quest.id = "quest.slayer";
    eve::rpg::QuestRegistry::registerQuest(quest);
}

constexpr const char *kValidEncounter = R"([
  {
    "id":"slime.west",
    "displayName":"West Slime",
    "members":[{"id":"west","displayName":"West Slime","skillId":"skill.enemy_claw",
      "attack":12,"defense":2.8,"maxHp":42,"speed":5}],
    "xpReward":40,
    "xpGrowth":1.2,
    "goldReward":10,
    "requiredQuestId":"quest.slayer",
    "notifyTopic":"kill",
    "notifyTarget":"slime",
    "notifyAmount":1,
    "defeatCounterId":"kills",
    "defeatCounterAmount":1,
    "levelPointAttributeId":"statPoints",
    "pointsPerLevel":3
  }
])";

}  // namespace

TEST_CASE("rpg.encounter.strictCatalogueCreatesExactActor") {
    registerEncounterDependencies();
    eve::rpg::EncounterCatalogue::clear();

    auto replaced = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(kValidEncounter);
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value(), 1);
    const auto *definition = eve::rpg::EncounterCatalogue::find("slime.west");
    REQUIRE(definition != nullptr);
    CHECK_EQ(definition->displayName, std::string("West Slime"));
    CHECK_EQ(definition->xpReward, 40.0);
    CHECK_EQ(definition->members.size(), std::size_t(1));
    CHECK_EQ(eve::rpg::EncounterCatalogue::memberCount("slime.west"), 1);

    eve::rpg::RPGActor *actor = eve::rpg::EncounterCatalogue::createActor("slime.west");
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->getFinalAttribute("attack"), 12.0);
    CHECK_EQ(actor->getFinalAttribute("defense"), 2.8);
    CHECK_EQ(actor->getFinalAttribute("hp"), 42.0);
    CHECK_EQ(actor->getFinalAttribute("speed"), 5.0);
    CHECK_EQ(actor->getCurrent("hp"), 42.0);
    CHECK(actor->knowsSkill("skill.enemy_claw"));
    actor->release();

    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
}

TEST_CASE("rpg.encounter.invalidReplacementPreservesPublishedCatalogue") {
    registerEncounterDependencies();
    eve::rpg::EncounterCatalogue::clear();
    REQUIRE(eve::rpg::EncounterCatalogue::replaceFromJsonStrict(kValidEncounter).ok());

    auto unknownField = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"bad","displayName":"Bad","skillId":"skill.enemy_claw","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"quest.slayer","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0,"typo":true}])");
    CHECK(!unknownField.ok());
    CHECK(eve::rpg::EncounterCatalogue::find("slime.west") != nullptr);
    CHECK_EQ(eve::rpg::EncounterCatalogue::count(), 1);

    auto missingSkill = eve::rpg::EncounterCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"bad","displayName":"Bad","skillId":"skill.missing","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"quest.slayer","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0}])");
    CHECK(!missingSkill.ok());
    CHECK(eve::rpg::EncounterCatalogue::find("slime.west") != nullptr);

    eve::rpg::EncounterCatalogue::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::SkillRegistry::clear();
}
}  // namespace rpg_encounter_catalogue
}  // namespace

// rpg_party.cpp
namespace {
namespace rpg_party {
namespace {

eve::rpg::RPGActor *makePartyActor() {
    auto *actor = eve::rpg::RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", 100.0);
    return actor;
}

}  // namespace

TEST_CASE("rpg.party.orderedRosterRejectsDuplicateIdsAndActors") {
    eve::rpg::Party party;
    auto           *hero      = makePartyActor();
    auto           *companion = makePartyActor();

    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion.ranger", companion).ok());
    CHECK_EQ(party.count(), 2);
    CHECK_EQ(party.getMemberId(0), std::string("hero"));
    CHECK(party.getMemberActor(1) == companion);
    CHECK(party.findMemberActor("hero") == hero);
    CHECK(!party.addMember("hero", companion).ok());
    CHECK(!party.addMember("duplicate.actor", hero).ok());
    CHECK_EQ(party.count(), 2);

    REQUIRE(party.removeMember("companion.ranger").ok());
    CHECK_EQ(party.count(), 1);
    CHECK(companion->getCurrent("hp") == 100.0);
    hero->release();
    companion->release();
}

TEST_CASE("rpg.party.staleRosterFailsBattleCompositionBeforeMutation") {
    eve::rpg::Party party;
    auto           *hero      = makePartyActor();
    auto           *companion = makePartyActor();
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    companion->release();

    CHECK(party.hasStaleMembers());
    eve::rpg::Battle battle;
    auto             composed = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    CHECK(!composed.ok());
    CHECK_EQ(battle.getActorCount(), 0);

    party.clear();
    REQUIRE(party.addMember("hero", hero).ok());
    auto valid = party.addToBattle(&battle, eve::rpg::BattleSide::Party);
    REQUIRE(valid.ok());
    CHECK_EQ(valid.value(), 1);
    CHECK_EQ(battle.getActorCount(), 1);
    hero->release();
}

TEST_CASE("rpg.party.checkpointRestoresAllMembersAtomically") {
    eve::rpg::Party party;
    auto           *hero      = makePartyActor();
    auto           *companion = makePartyActor();
    hero->setBaseAttribute("attack", 20.0);
    companion->setBaseAttribute("attack", 12.0);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());

    auto checkpoint = party.checkpointJson();
    REQUIRE(checkpoint.ok());
    hero->setBaseAttribute("attack", 1.0);
    companion->setBaseAttribute("attack", 2.0);
    REQUIRE(party.restoreCheckpointJson(checkpoint.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 20.0);
    CHECK_EQ(companion->getBaseAttribute("attack"), 12.0);

    REQUIRE(party.removeMember("companion").ok());
    hero->setBaseAttribute("attack", 7.0);
    CHECK(!party.restoreCheckpointJson(checkpoint.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 7.0);

    hero->release();
    companion->release();
}

TEST_CASE("rpg.party.checkpointRecoveryValidatesWholeRosterBeforeReviving") {
    eve::rpg::Party party;
    auto           *hero      = makePartyActor();
    auto           *companion = makePartyActor();
    hero->setBaseAttribute("mp", 40.0);
    companion->setBaseAttribute("mp", 20.0);
    hero->setCurrent("hp", 0.0);
    companion->setCurrent("hp", 0.0);
    hero->setCurrent("mp", 0.0);
    companion->setCurrent("mp", 0.0);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());

    CHECK(!party.recoverAtCheckpoint("hp", 0.0, "mp", 0.5).ok());
    CHECK_EQ(hero->getCurrent("hp"), 0.0);
    CHECK_EQ(companion->getCurrent("hp"), 0.0);
    auto recovered = party.recoverAtCheckpoint("hp", 0.5, "mp", 0.25);
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), 2);
    CHECK_EQ(hero->getCurrent("hp"), 50.0);
    CHECK_EQ(companion->getCurrent("hp"), 50.0);
    CHECK_EQ(hero->getCurrent("mp"), 10.0);
    CHECK_EQ(companion->getCurrent("mp"), 5.0);

    companion->release();
    hero->setCurrent("hp", 3.0);
    CHECK(!party.recoverAtCheckpoint("hp", 1.0, "mp", 1.0).ok());
    CHECK_EQ(hero->getCurrent("hp"), 3.0);
    hero->release();
}
}  // namespace rpg_party
}  // namespace

// rpg_state_canonical.cpp
namespace {
namespace rpg_state_canonical {
TEST_CASE("rpg.state.canonicalRestoreRejectsLaterMalformedActorWithoutMutation") {
    eve::rpg::SkillRegistry::clear();
    auto *first  = eve::rpg::RPGActor::createActor();
    auto *second = eve::rpg::RPGActor::createActor();

    eve::rpg::SkillDefinition definition;
    definition.id = "fireball";
    eve::rpg::SkillRegistry::registerSkill(definition);
    first->learnSkill("fireball");
    first->setSkillCooldown("fireball", 3.f);

    auto captured = eve::rpg::captureRpgState();
    REQUIRE(captured.ok());
    auto state = std::move(captured).takeValue();
    state.at(0).find("skills")->set("fireball", eve::Value::number(9.0));
    state.at(1) = eve::Value::integer(42);

    auto restored = eve::rpg::restoreRpgState(state);
    CHECK(!restored.ok());
    CHECK(eve::rpg::SkillSystem::getCooldownRemaining(first, "fireball") == 3.f);

    second->release();
    first->release();
    eve::rpg::SkillRegistry::clear();
}
}  // namespace rpg_state_canonical
}  // namespace
}  // namespace test_rpg_party_encounters
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_progression)
#include "PrivateTestScope.h"
#include <cmath>
#include "rpg/LevelSystem.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/VitalsSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_progression.cpp
namespace test_rpg_progression {
using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.progression.levelAndXp") {
    RPGActor *actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->getLevel(), 1);
    CHECK_EQ(actor->getXp(), 0.0);
    CHECK_EQ(actor->getXpToNext(), 100.0);

    actor->setXpToNext(100.0);
    actor->gainXp(50.0);
    CHECK_EQ(actor->getLevel(), 1);
    CHECK(approxEq(actor->getXp(), 50.0));

    actor->gainXp(60.0);  // 110 -> 升 1 级，剩 10
    CHECK_EQ(actor->getLevel(), 2);
    CHECK(approxEq(actor->getXp(), 10.0));
    CHECK(approxEq(actor->getXpToNext(), 120.0));  // 100 * 1.2

    actor->release();
}

TEST_CASE("rpg.progression.levelUpEventsViaFacade") {
    auto *rpg = RPG::create();
    rpg->update(0.f);  // 清掉其它用例遗留的静态事件
    RPGActor *actor = rpg->newActor();
    REQUIRE(actor != nullptr);
    actor->setXpToNext(50.0);
    CHECK(!actor->gainXp(20.0));
    CHECK(actor->gainXp(50.0));  // 升到 2

    rpg->update(0.f);
    CHECK(rpg->getLevelUpEventCount() == 1);
    CHECK(rpg->getLevelUpEventActor(0) == actor);
    CHECK_EQ(rpg->getLevelUpEventPreviousLevel(0), 1);
    CHECK_EQ(rpg->getLevelUpEventNewLevel(0), 2);

    actor->release();
}

TEST_CASE("rpg.progression.multiLevelUpSingleGain") {
    RPGActor *actor = RPGActor::createActor();
    actor->setXpToNext(10.0);
    CHECK(actor->gainXp(100.0));  // 连升多级
    CHECK(actor->getLevel() > 1);
    // 不变式：升级后经验必低于当前阈值
    CHECK(actor->getXp() < actor->getXpToNext());
    actor->release();
}

TEST_CASE("rpg.vitals.damageHealDeathRevive") {
    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("hp", 100.0);
    CHECK(approxEq(actor->getMax("hp"), 100.0));

    actor->setCurrent("hp", 80.0);
    CHECK(approxEq(actor->getCurrent("hp"), 80.0));
    CHECK(!actor->isDead("hp"));

    // 超出上限的 set 会被夹到 max
    actor->setCurrent("hp", 999.0);
    CHECK(approxEq(actor->getCurrent("hp"), 100.0));

    // 伤害夹到 0，超量不把当前值打穿负数
    actor->setCurrent("hp", 50.0);
    double applied = actor->takeDamage("hp", 30.0, "slime");
    CHECK(approxEq(applied, 30.0));
    CHECK(approxEq(actor->getCurrent("hp"), 20.0));
    applied = actor->takeDamage("hp", 999.0, "slime");
    CHECK(approxEq(applied, 20.0));
    CHECK(approxEq(actor->getCurrent("hp"), 0.0));
    CHECK(actor->isDead("hp"));

    // 死亡后再受伤不生效
    applied = actor->takeDamage("hp", 10.0, "slime");
    CHECK(approxEq(applied, 0.0));

    // 复活到上限
    actor->revive("hp");
    CHECK(approxEq(actor->getCurrent("hp"), 100.0));
    CHECK(!actor->isDead("hp"));

    // 治疗夹到 max
    actor->setCurrent("hp", 30.0);
    double healed = actor->heal("hp", 50.0);
    CHECK(approxEq(healed, 50.0));
    healed = actor->heal("hp", 999.0);
    CHECK(approxEq(healed, 20.0));
    CHECK(approxEq(actor->getCurrent("hp"), 100.0));

    actor->release();
}

TEST_CASE("rpg.vitals.eventsViaFacade") {
    auto *rpg = RPG::create();
    rpg->update(0.f);  // 清掉其它用例遗留的静态事件
    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("hp", 100.0);
    actor->setCurrent("hp", 100.0);
    actor->takeDamage("hp", 100.0, "boss");  // 归零死亡
    rpg->update(0.f);

    CHECK_EQ(rpg->getVitalsEventCount(), 2);  // damage + death
    bool sawDamage = false, sawDeath = false;
    for (int i = 0; i < rpg->getVitalsEventCount(); ++i) {
        if (rpg->getVitalsEventAction(i) == "damage" && rpg->getVitalsEventResource(i) == "hp") {
            sawDamage = true;
            CHECK(rpg->getVitalsEventActor(i) == actor);
            CHECK(approxEq(rpg->getVitalsEventAmount(i), 100.0));
            CHECK_EQ(rpg->getVitalsEventSource(i), std::string("boss"));
        }
        if (rpg->getVitalsEventAction(i) == "death") sawDeath = true;
    }
    CHECK(sawDamage);
    CHECK(sawDeath);
    actor->release();
}

TEST_CASE("rpg.progression.checkpointRestoreIsValidatedAndAtomic") {
    auto *actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setXpToNext(50.0);

    auto restored = actor->restoreProgression(4, 12.0, 86.4);
    REQUIRE(restored.ok());
    CHECK_EQ(actor->getLevel(), 4);
    CHECK_EQ(actor->getXp(), 12.0);
    CHECK_EQ(actor->getXpToNext(), 86.4);

    auto invalid = actor->restoreProgression(0, 100.0, 10.0);
    CHECK(!invalid.ok());
    CHECK_EQ(actor->getLevel(), 4);
    CHECK_EQ(actor->getXp(), 12.0);
    CHECK_EQ(actor->getXpToNext(), 86.4);
    actor->release();
}
}  // namespace test_rpg_progression
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_quest)
#include "PrivateTestScope.h"
#include "rpg/Quest.h"
#include "rpg/RPG.h"
#include "rpg/Tracker.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_quest.cpp
namespace test_rpg_quest {
using namespace eve::rpg;

TEST_CASE("rpg.quest.registryCRUD") {
    QuestRegistry::clear();
    QuestDefinition def;
    def.id             = "quest.intro";
    def.tags           = {"main"};
    def.extra["title"] = "Intro";
    QuestRegistry::registerQuest(def);
    CHECK_EQ(QuestRegistry::count(), 1);
    const QuestDefinition *found = QuestRegistry::find("quest.intro");
    REQUIRE(found != nullptr);
    CHECK(found->hasTag("main"));
    CHECK_EQ(found->getExtra("title", ""), std::string("Intro"));
    CHECK_EQ(found->startPolicy, std::string("manual"));
    CHECK_EQ(found->completePolicy, std::string("auto"));
    CHECK(QuestRegistry::remove("quest.intro"));
    CHECK_EQ(QuestRegistry::count(), 0);
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.loadFromJsonDefaultsAndRewards") {
    QuestRegistry::clear();
    int n = QuestRegistry::loadFromJson(R"([
      {
        "id": "quest.hunt",
        "startPolicy": "manual",
        "completePolicy": "claim",
        "requires": ["quest.intro"],
        "tags": ["main"],
        "objectives": [
          {"id": "kill_slime", "topic": "kill", "target": "slime", "count": 10},
          {"id": "any_kill", "topic": "kill", "count": 0}
        ],
        "rewards": [
          {"type": "item", "id": "potion", "amount": 3},
          {"type": "attribute", "id": "xp", "amount": 50}
        ],
        "extra": {"title": "Hunt"}
      }
    ])");
    CHECK_EQ(n, 1);
    const QuestDefinition *d = QuestRegistry::find("quest.hunt");
    REQUIRE(d != nullptr);
    CHECK_EQ(d->startPolicy, std::string("manual"));
    CHECK_EQ(d->completePolicy, std::string("claim"));
    REQUIRE(d->requiresIds.size() == 1);
    CHECK_EQ(d->requiresIds[0], std::string("quest.intro"));
    REQUIRE(d->objectives.size() == 2);
    CHECK_EQ(d->objectives[1].count, 1);  // count<=0 → 1
    CHECK(d->objectives[1].target.empty());
    REQUIRE(d->rewards.size() == 2);
    CHECK_EQ(d->rewards[0].type, std::string("item"));
    CHECK_EQ(d->rewards[1].amount, 50.0);
    CHECK_EQ(d->getExtra("title", ""), std::string("Hunt"));
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.loadFromJsonRejectsBadAndCycles") {
    QuestRegistry::clear();
    int n = QuestRegistry::loadFromJson(R"([
      {"startPolicy": "auto"},
      {
        "id": "dup.obj",
        "objectives": [
          {"id": "a", "topic": "kill", "count": 1},
          {"id": "a", "topic": "talk", "count": 1}
        ]
      },
      {"id": "cyc.a", "requires": ["cyc.b"]},
      {"id": "cyc.b", "requires": ["cyc.a"]},
      {"id": "ok.solo", "startPolicy": "weird", "completePolicy": "nope"}
    ])");
    CHECK_EQ(n, 1);
    CHECK(QuestRegistry::find("dup.obj") == nullptr);
    CHECK(QuestRegistry::find("cyc.a") == nullptr);
    CHECK(QuestRegistry::find("cyc.b") == nullptr);
    const QuestDefinition *ok = QuestRegistry::find("ok.solo");
    REQUIRE(ok != nullptr);
    CHECK_EQ(ok->startPolicy, std::string("manual"));
    CHECK_EQ(ok->completePolicy, std::string("auto"));
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.trackerInitialStatesAndSyncAuto") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"q.intro","startPolicy":"manual"},
      {"id":"q.auto","startPolicy":"auto","objectives":[{"id":"o","topic":"kill","target":"x","count":1}]},
      {"id":"q.locked","startPolicy":"auto","requires":["q.missing"]},
      {"id":"q.chain","startPolicy":"manual","requires":["q.intro"]}
    ])");

    Tracker log;
    CHECK_EQ(log.getCount(), 4);
    CHECK_EQ(log.getState("q.intro"), std::string("inactive"));
    CHECK_EQ(log.getState("q.auto"), std::string("active"));
    CHECK_EQ(log.getState("q.locked"), std::string("locked"));
    CHECK_EQ(log.getState("q.chain"), std::string("locked"));
    CHECK_EQ(log.getState("nope"), std::string(""));

    QuestRegistry::loadFromJson(
        R"([{"id":"q.late","startPolicy":"auto","objectives":[{"id":"o","topic":"kill","target":"x","count":1}]}])");
    CHECK_EQ(log.getCount(), 4);
    log.syncAuto();
    CHECK_EQ(log.getCount(), 5);
    CHECK_EQ(log.getState("q.late"), std::string("active"));
    CHECK(log.hasTag("q.intro", "x") == false);

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.activateAndNotify") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"q.lock","startPolicy":"manual","requires":["nope"]},
      {"id":"q.hunt","startPolicy":"manual","completePolicy":"auto",
       "objectives":[
         {"id":"k","topic":"kill","target":"slime","count":2},
         {"id":"any","topic":"kill","count":3}
       ]},
      {"id":"q.flag","startPolicy":"manual","completePolicy":"auto","objectives":[]}
    ])");
    Tracker log;
    CHECK_EQ(log.canActivateReason("q.lock"), std::string("locked"));
    CHECK(!log.activate("q.lock"));
    CHECK_EQ(log.getState("q.lock"), std::string("locked"));

    CHECK(log.canActivate("q.hunt"));
    CHECK(log.activate("q.hunt"));
    CHECK_EQ(log.getState("q.hunt"), std::string("active"));
    CHECK_EQ(log.canActivateReason("q.hunt"), std::string("alreadyActive"));

    log.notify("talk", "npc", 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 0);

    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 1), 1);  // target 空，吃所有 kill
    log.notify("kill", "goblin", 5);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);  // slime 不涨
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 1), 3);  // 封顶 3
    CHECK(log.isObjectiveDone("q.hunt", 1));

    log.notify("kill", "slime", 0);
    CHECK_EQ(log.getObjectiveCurrent("q.hunt", 0), 1);
    log.notify("kill", "slime", 1);
    CHECK(log.isObjectiveDone("q.hunt", 0));
    CHECK_EQ(log.getState("q.hunt"), std::string("completed"));

    CHECK(log.activate("q.flag"));
    CHECK_EQ(log.getState("q.flag"), std::string("completed"));

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.claimEventsAndChain") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"a","startPolicy":"manual","completePolicy":"claim",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]},
      {"id":"b","startPolicy":"auto","completePolicy":"auto","requires":["a"],
       "objectives":[{"id":"t","topic":"talk","target":"npc","count":1}]}
    ])");
    Tracker log;
    CHECK_EQ(log.getState("b"), std::string("locked"));
    CHECK(log.activate("a"));
    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("a"), std::string("ready"));
    CHECK(!log.claim("nope"));
    CHECK(!log.claim("b"));
    CHECK(log.claim("a"));
    CHECK_EQ(log.getState("a"), std::string("completed"));
    CHECK_EQ(log.getState("b"), std::string("active"));

    log.pollEvents();
    bool sawProgress = false, sawReady = false, sawComplete = false, sawActivateB = false;
    for (int i = 0; i < log.getEventCount(); ++i) {
        auto act = log.getEventAction(i);
        auto id  = log.getEventEntryId(i);
        if (act == "progress" && id == "a") sawProgress = true;
        if (act == "ready" && id == "a") sawReady = true;
        if (act == "complete" && id == "a") sawComplete = true;
        if (act == "activate" && id == "b") sawActivateB = true;
    }
    CHECK(sawProgress);
    CHECK(sawReady);
    CHECK(sawComplete);
    CHECK(sawActivateB);

    Tracker ach;
    QuestRegistry::loadFromJson(R"([
      {"id":"ach.kill","startPolicy":"auto","completePolicy":"auto","tags":["achievement"],
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]}
    ])");
    ach.syncAuto();
    log.notify("kill", "slime", 1);
    ach.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("ach.kill"), std::string(""));  // log 未 sync 这条也可；隔离重点是事件
    CHECK_EQ(ach.getState("ach.kill"), std::string("completed"));
    log.pollEvents();
    ach.pollEvents();
    bool logCompletedAch = false;
    for (int i = 0; i < log.getEventCount(); ++i)
        if (log.getEventEntryId(i) == "ach.kill") logCompletedAch = true;
    CHECK(!logCompletedAch);
    CHECK(ach.getEventCount() >= 1);

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.resetAbandonFailNoCascade") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(R"([
      {"id":"daily","startPolicy":"auto","completePolicy":"auto",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}]},
      {"id":"main","startPolicy":"manual","completePolicy":"auto","requires":["daily"],
       "objectives":[{"id":"t","topic":"talk","target":"npc","count":1}]}
    ])");
    Tracker log;
    log.notify("kill", "slime", 1);
    CHECK_EQ(log.getState("daily"), std::string("completed"));
    CHECK(log.activate("main"));
    CHECK_EQ(log.getState("main"), std::string("active"));

    CHECK(log.reset("daily"));
    CHECK_EQ(log.getState("daily"), std::string("active"));
    CHECK_EQ(log.getObjectiveCurrent("daily", 0), 0);
    CHECK_EQ(log.getState("main"), std::string("active"));  // 不级联

    CHECK(log.abandon("main"));
    CHECK_EQ(log.getState("main"), std::string("failed"));
    log.notify("talk", "npc", 1);
    CHECK_EQ(log.getObjectiveCurrent("main", 0), 0);
    CHECK(!log.activate("main"));
    CHECK_EQ(log.canActivateReason("main"), std::string("failed"));
    CHECK(log.reset("main"));
    // daily 已 reset 且未完成，main 的 requires 不满足 → locked
    CHECK_EQ(log.getState("main"), std::string("locked"));

    Tracker t2;
    QuestRegistry::loadFromJson(R"([{"id":"only","startPolicy":"manual"}])");
    t2.syncAuto();
    CHECK(t2.fail("only", "timeout"));
    CHECK_EQ(t2.getState("only"), std::string("failed"));
    t2.pollEvents();
    bool sawTimeout = false;
    for (int i = 0; i < t2.getEventCount(); ++i)
        if (t2.getEventAction(i) == "fail" && t2.getEventReason(i) == "timeout") sawTimeout = true;
    CHECK(sawTimeout);

    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.facadeManualClaim") {
    auto *rpg = RPG::create();
    rpg->clearQuestDefinitions();
    int n = rpg->registerQuestsFromJson(R"([
      {"id":"quest.hunt","startPolicy":"manual","completePolicy":"claim",
       "objectives":[{"id":"k","topic":"kill","target":"slime","count":1}],
       "rewards":[{"type":"item","id":"potion","amount":3}]}
    ])");
    CHECK_EQ(n, 1);
    CHECK_EQ(rpg->getQuestDefinitionCount(), 1);

    Tracker *log = rpg->newTracker();
    REQUIRE(log != nullptr);
    CHECK(log->activate("quest.hunt"));
    log->notify("kill", "slime", 1);
    CHECK_EQ(log->getState("quest.hunt"), std::string("ready"));
    CHECK(log->claim("quest.hunt"));
    CHECK_EQ(log->getState("quest.hunt"), std::string("completed"));
    CHECK_EQ(log->getRewardCount("quest.hunt"), 1);
    CHECK_EQ(log->getRewardId("quest.hunt", 0), std::string("potion"));

    log->pollEvents();
    CHECK(log->getEventCount() >= 1);

    rpg->clearQuestDefinitions();
}

TEST_CASE("rpg.quest.versionedSnapshotRoundTripAndRegistryMismatchRollback") {
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.hunt","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"kill","topic":"kill","target":"slime","count":3}]}
    ])"),
               1);
    Tracker source;
    source.notify("kill", "slime", 2);
    source.pollEvents();
    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    Tracker restored;
    restored.notify("kill", "slime", 1);
    REQUIRE(restored.restoreSnapshotJson(encoded.value()).ok());
    CHECK_EQ(restored.getState("quest.hunt"), std::string("active"));
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);
    restored.pollEvents();
    CHECK_EQ(restored.getEventCount(), 0);

    const auto before = restored.snapshotJson();
    REQUIRE(before.ok());
    auto malformed = restored.restoreSnapshotJson(
        R"({"schema":"eve.rpg.quest-tracker","version":1,"entries":[{"id":"quest.hunt","state":"completed","objectives":[{"id":"kill","current":1}]}]})");
    CHECK(!malformed.ok());
    auto afterMalformed = restored.snapshotJson();
    REQUIRE(afterMalformed.ok());
    CHECK_EQ(afterMalformed.value(), before.value());

    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.hunt","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"kill-v2","topic":"kill","target":"slime","count":3}]}
    ])"),
               1);
    auto mismatch = restored.restoreSnapshotJson(encoded.value());
    CHECK(!mismatch.ok());
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.strictReplacementCommitsCompleteCatalogueAtomically") {
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([{"id":"old.quest"}])"), 1);

    auto replaced = QuestRegistry::replaceFromJsonStrict(R"([
      {"id":"quest.intro","startPolicy":"manual","completePolicy":"auto",
       "objectives":[{"id":"talk","topic":"talk","target":"elder","count":1}]},
      {"id":"quest.hunt","startPolicy":"auto","completePolicy":"claim","requires":["quest.intro"],
       "objectives":[{"id":"kill","topic":"kill","target":"slime","count":3}],
       "rewards":[{"type":"item","id":"potion","amount":2}],
       "tags":["main"],"extra":{"title":"Hunt"}}
    ])");
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value(), 2);
    CHECK(QuestRegistry::find("old.quest") == nullptr);
    REQUIRE(QuestRegistry::find("quest.hunt") != nullptr);
    CHECK_EQ(QuestRegistry::find("quest.hunt")->getExtra("title"), std::string("Hunt"));
    QuestRegistry::clear();
}

TEST_CASE("rpg.quest.strictReplacementRejectsInvalidBatchWithoutMutation") {
    QuestRegistry::clear();
    auto initial = QuestRegistry::replaceFromJsonStrict(R"([{"id":"quest.stable"}])");
    REQUIRE(initial.ok());

    auto invalidPolicy = QuestRegistry::replaceFromJsonStrict(R"([{"id":"quest.new","startPolicy":"sometimes"}])");
    CHECK(!invalidPolicy.ok());
    CHECK(QuestRegistry::find("quest.stable") != nullptr);
    CHECK(QuestRegistry::find("quest.new") == nullptr);

    auto missingDependency =
        QuestRegistry::replaceFromJsonStrict(R"([{"id":"quest.new","requires":["quest.missing"]}])");
    CHECK(!missingDependency.ok());
    CHECK(QuestRegistry::find("quest.stable") != nullptr);

    auto partialBatch = QuestRegistry::replaceFromJsonStrict(
        R"([{"id":"quest.good"},{"id":"quest.bad","objectives":[{"id":"o","topic":"kill","count":0}]}])");
    CHECK(!partialBatch.ok());
    CHECK(QuestRegistry::find("quest.good") == nullptr);
    CHECK(QuestRegistry::find("quest.stable") != nullptr);
    QuestRegistry::clear();
}
}  // namespace test_rpg_quest
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_reload)
#include "PrivateTestScope.h"
#include <string>
#include "common/StateValue.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_reload.cpp
namespace test_rpg_reload {
using namespace eve::rpg;

TEST_CASE("rpg.state.captureRestoreCastingAndCooldown") {
    SkillRegistry::clear();
    RPGActor *actor = RPGActor::createActor();

    SkillDefinition def;
    def.id       = "fireball";
    def.castTime = 1.5f;
    SkillRegistry::registerSkill(def);
    actor->learnSkill("fireball");
    actor->setSkillCooldown("fireball", 2.5f);
    CHECK(eve::rpg::SkillSystem::beginCast(actor, "fireball"));
    CHECK(eve::rpg::SkillSystem::isCasting(actor));

    eve::StateValue captured;
    REQUIRE(eve::rpg::captureRpgState(captured));

    // Mutate everything, then restore.
    eve::rpg::SkillSystem::cancelCast(actor);
    actor->setSkillCooldown("fireball", 0.f);
    CHECK(!eve::rpg::SkillSystem::isCasting(actor));

    std::string err;
    CHECK(eve::rpg::restoreRpgState(captured, &err));
    CHECK(err.empty());
    CHECK(eve::rpg::SkillSystem::isCasting(actor));
    CHECK_EQ(eve::rpg::SkillSystem::getCastingSkillId(actor), std::string("fireball"));
    CHECK(eve::rpg::SkillSystem::getCooldownRemaining(actor, "fireball") == 2.5f);

    actor->release();
    SkillRegistry::clear();
}

TEST_CASE("rpg.state.resetCancelsCasting") {
    SkillRegistry::clear();
    RPGActor *actor = RPGActor::createActor();

    SkillDefinition def;
    def.id       = "channel";
    def.castTime = 1.f;
    SkillRegistry::registerSkill(def);
    actor->learnSkill("channel");
    CHECK(eve::rpg::SkillSystem::beginCast(actor, "channel"));
    CHECK(eve::rpg::SkillSystem::isCasting(actor));

    CHECK(eve::rpg::resetRpgState());
    CHECK(!eve::rpg::SkillSystem::isCasting(actor));

    actor->release();
    SkillRegistry::clear();
}

TEST_CASE("rpg.state.restoreRejectsMalformed") {
    std::string err;
    CHECK(!eve::rpg::restoreRpgState(eve::StateValue::object(), &err));
    CHECK(!err.empty());
}
}  // namespace test_rpg_reload
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_skill_condition)
#include <optional>
#include "PrivateTestScope.h"
#include "common/ConditionKeys.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/SkillCondition.h"
#include "rpg/SkillSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_skill_condition.cpp
namespace test_rpg_skill_condition {
TEST_CASE("rpg.skillConditionAdapterReadsCanonicalSkillState") {
    using namespace eve::condition;
    using namespace eve::rpg;

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("mana", 20.0);
    actor->learnSkill("skill.fire");
    SkillDefinition definition;
    definition.id   = "skill.fire";
    definition.tags = {"fire"};

    const auto condition = Condition::all({
        eve::condition::tagPresent("fire"),
        eve::condition::attributeExists("mana"),
        eve::condition::resourceExists("mana"),
        Condition::compare("skill.cooldown", CompareOperator::Equal, 0.0),
        eve::condition::stateEquals("skill.phase", "idle"),
    });
    const auto result    = SkillConditionAdapter::evaluate(actor, definition, condition);
    REQUIRE(result.passed());
    CHECK_EQ(static_cast<int>(result.reasonCode()), static_cast<int>(ConditionReasonCode::Passed));

    actor->setSkillCooldown("skill.fire", 2.0f);
    const auto blocked = SkillConditionAdapter::evaluate(
        actor, definition, Condition::compare("skill.cooldown", CompareOperator::Equal, 0.0));
    REQUIRE(!blocked.passed());
    CHECK_EQ(static_cast<int>(blocked.reasonCode()), static_cast<int>(ConditionReasonCode::ValueMismatch));
    actor->release();
}

TEST_CASE("rpg.skillSystemUsesConditionResultAsCompatibilityReason") {
    using namespace eve::condition;
    using namespace eve::rpg;

    SkillDefinition definition;
    definition.id            = "skill.guard";
    definition.castCondition = eve::condition::tagPresent("silenced");
    SkillRegistry::registerSkill(definition);
    RPGActor *actor = RPGActor::createActor();
    actor->learnSkill(definition.id);

    CHECK(!actor->canCastSkill(definition.id));
    CHECK_EQ(actor->canCastSkillReason(definition.id), "value_mismatch");
    actor->release();
    SkillRegistry::remove(definition.id);
}

TEST_CASE("rpg.skillConditionPolicyAndAuthorityAreReadOnlyProviders") {
    using namespace eve::condition;
    using namespace eve::rpg;

    RPGActor       *actor = RPGActor::createActor();
    SkillDefinition definition;
    definition.id                        = "skill.policy";
    int                   authorityCalls = 0;
    int                   policyCalls    = 0;
    SkillConditionQueries queries;
    queries.authority = [&authorityCalls](std::string_view scope) -> std::optional<bool> {
        ++authorityCalls;
        return scope == "cast";
    };
    queries.policy = [&policyCalls](std::string_view name, const eve::Value &) -> std::optional<ConditionResult> {
        ++policyCalls;
        return name == "safe" ? std::optional<ConditionResult>(ConditionResult::success()) : std::nullopt;
    };
    const auto result = SkillConditionAdapter::evaluate(
        actor, definition, Condition::all({eve::condition::authorityAllow("cast"), Condition::policyCall("safe")}),
        queries);
    REQUIRE(result.passed());
    CHECK_EQ(authorityCalls, 1);
    CHECK_EQ(policyCalls, 1);
    actor->release();
}
}  // namespace test_rpg_skill_condition
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_story_event)
#include "PrivateTestScope.h"
#include "rpg/GameState.h"
#include "rpg/StoryEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_story_event.cpp
namespace test_rpg_story_event {
TEST_CASE("rpg.storyEvent.strictCatalogueAndPersistentResume") {
    using namespace eve::rpg;
    StoryEventCatalogue::clear();
    const std::string content = R"({
      "schema":"eve.rpg.story-events",
      "version":1,
      "events":[
        {"id":"forest.arrival","repeatable":false,"steps":[
          {"kind":"dialogue","reference":"story.forest.arrival","actorId":"","x":0,"y":0,"duration":0},
          {"kind":"message","reference":"林间道路已经开放。","actorId":"","x":0,"y":0,"duration":0}
        ]},
        {"id":"inn.rest","repeatable":true,"steps":[
          {"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0.25}
        ]}
      ]
    })";
    REQUIRE(StoryEventCatalogue::replaceFromJsonStrict(content).ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);
    CHECK(StoryEventCatalogue::contains("forest.arrival"));

    CHECK(
        !StoryEventCatalogue::replaceFromJsonStrict(
             R"({"schema":"eve.rpg.story-events","version":1,"events":[{"id":"bad","repeatable":false,"steps":[{"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0}]}]})")
             .ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);

    GameState         state;
    StoryEventSession first;
    REQUIRE(first.begin("forest.arrival", &state).ok());
    CHECK(first.isActive());
    CHECK_EQ(first.getStepKind(), std::string("dialogue"));
    CHECK_EQ(first.getReference(), std::string("story.forest.arrival"));
    auto advanced = first.advance(&state);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value(), 1);
    StoryEventSession stale;
    REQUIRE(stale.begin("forest.arrival", &state).ok());
    REQUIRE(first.advance(&state).ok());
    CHECK(!stale.advance(&state).ok());

    state.setSelfVariable("story.event:forest.arrival", "cursor", 1.0);
    state.setSelfVariable("story.event:forest.arrival", "completed", 0.0);
    auto snapshot = state.snapshotJson();
    REQUIRE(snapshot.ok());
    GameState restored;
    REQUIRE(restored.restoreSnapshotJson(snapshot.value()).ok());
    StoryEventSession resumed;
    REQUIRE(resumed.begin("forest.arrival", &restored).ok());
    CHECK_EQ(resumed.getStepIndex(), 1);
    CHECK_EQ(resumed.getStepKind(), std::string("message"));
    CHECK_EQ(resumed.getReference(), std::string("林间道路已经开放。"));

    StoryEventCatalogue::clear();
    CHECK_EQ(resumed.getStepKind(), std::string("message"));
    auto finished = resumed.advance(&restored);
    REQUIRE(finished.ok());
    CHECK_EQ(finished.value(), 0);
    CHECK(!resumed.isActive());
    CHECK(resumed.isFinished());
    CHECK(!StoryEventSession().begin("forest.arrival", &restored).ok());

    REQUIRE(StoryEventCatalogue::replaceFromJsonStrict(content).ok());
    StoryEventSession completed;
    CHECK(!completed.begin("forest.arrival", &restored).ok());
    StoryEventSession repeatable;
    REQUIRE(repeatable.begin("inn.rest", &restored).ok());
    REQUIRE(repeatable.advance(&restored).ok());
    REQUIRE(repeatable.begin("inn.rest", &restored).ok());
    CHECK_EQ(repeatable.getStepIndex(), 0);
    StoryEventCatalogue::clear();
}
}  // namespace test_rpg_story_event
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_trait)
#include "PrivateTestScope.h"
#include <cmath>
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/Trait.h"
#include "rpg/TraitSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_trait.cpp
namespace test_rpg_trait {
using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.trait.registryJsonAndCRUD") {
    TraitRegistry::clear();
    int n = TraitRegistry::loadFromJson(R"([
      {"id":"fire_resist",
       "traits":[
         {"kind":"elementRate","target":"fire","value":0.5},
         {"kind":"elementRate","target":"ice","value":1.5}
       ],
       "tags":["defense"]},
      {"id":"mighty",
       "traits":[
         {"kind":"paramRate","target":"attack","value":1.5},
         {"kind":"exParam","target":"critRate","value":0.3}
       ]}
    ])");
    CHECK_EQ(n, 2);
    CHECK_EQ(TraitRegistry::count(), 2);
    const TraitDefinition *fr = TraitRegistry::find("fire_resist");
    REQUIRE(fr != nullptr);
    CHECK(fr->hasTag("defense"));
    REQUIRE(fr->traits.size() == 2);
    CHECK_EQ(fr->traits[0].target, std::string("fire"));
    CHECK(approxEq(fr->traits[0].value, 0.5));
    TraitRegistry::clear();
    CHECK_EQ(TraitRegistry::count(), 0);
}

TEST_CASE("rpg.trait.applyQueriesAndModifiers") {
    auto *rpg = RPG::create();
    rpg->clearTraitDefinitions();
    int registered = rpg->registerTraitsFromJson(R"([
      {"id":"mighty",
       "traits":[
         {"kind":"paramRate","target":"attack","value":1.5},
         {"kind":"exParam","target":"critRate","value":0.3}
       ]},
      {"id":"fire_resist","traits":[{"kind":"elementRate","target":"fire","value":0.5}]},
      {"id":"stone_skin","traits":[{"kind":"stateResist","target":"poison"}]},
      {"id":"brutal",
       "traits":[{"kind":"attackTimes","value":1},{"kind":"attackElement","target":"fire"}]}
    ])");
    CHECK_EQ(registered, 4);

    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100.0);

    int id = actor->applyTrait("mighty", "class");
    CHECK(id > 0);
    // paramRate 1.5 → mulAdd +0.5 → attack = 150
    CHECK(approxEq(actor->getFinalAttribute("attack"), 150.0));
    CHECK(approxEq(actor->getParamRate("attack"), 1.5));
    CHECK(approxEq(actor->getExParam("critRate"), 0.3));
    CHECK(actor->hasTrait("mighty"));
    CHECK_EQ(actor->getTraitCount(), 1);

    actor->applyTrait("fire_resist", "equipment");
    CHECK(approxEq(actor->getElementRate("fire"), 0.5));
    CHECK(approxEq(actor->getElementRate("ice"), 1.0));  // 未命中 → 1
    CHECK_EQ(actor->getTraitCount(), 2);

    actor->applyTrait("stone_skin", "race");
    CHECK(actor->isStateResist("poison"));
    CHECK(!actor->isStateResist("sleep"));

    actor->applyTrait("brutal", "passive");
    CHECK_EQ(actor->getAttackTimesAdd(), 1);
    const auto elems = actor->getAttackElements();
    REQUIRE(elems.size() == 1);
    CHECK_EQ(elems[0], std::string("fire"));

    // 移除按 source
    int removedEquip = actor->removeTraitsBySource("equipment");
    CHECK_EQ(removedEquip, 1);
    CHECK(approxEq(actor->getElementRate("fire"), 1.0));
    // 移除 paramRate 特征后属性加成随之撤销
    int removedMighty = actor->removeTraitsByTrait("mighty");
    CHECK_EQ(removedMighty, 1);
    CHECK(approxEq(actor->getFinalAttribute("attack"), 100.0));
    CHECK(!actor->hasTrait("mighty"));

    rpg->clearTraitDefinitions();
    actor->release();
}

TEST_CASE("rpg.trait.facadeEventsIndependent") {
    auto *rpg = RPG::create();
    rpg->clearTraitDefinitions();
    int registered =
        rpg->registerTraitsFromJson(R"([{"id":"t1","traits":[{"kind":"exParam","target":"evasion","value":0.2}]}])");
    CHECK_EQ(registered, 1);
    RPGActor *a  = rpg->newActor();
    int       id = a->applyTrait("t1", "src");
    CHECK(id > 0);
    CHECK(approxEq(a->getExParam("evasion"), 0.2));
    // 两 actor 特征互不影响
    RPGActor *b = rpg->newActor();
    CHECK(approxEq(b->getExParam("evasion"), 0.0));
    rpg->clearTraitDefinitions();
    a->release();
    b->release();
}
}  // namespace test_rpg_trait
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_status_effect_adapter)
#include "PrivateTestScope.h"
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "rpg/Effect.h"
#include "rpg/RPGActor.h"
#include "rpg/StatusSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// status_effect_adapter.cpp
namespace test_status_effect_adapter {
using namespace eve::rpg;

namespace {

bool approx(float left, float right, float epsilon = 1.0e-4f) { return std::abs(left - right) <= epsilon; }

void drainEvents() {
    std::vector<StatusChangeEvent> changes;
    std::vector<StatusTickEvent>   ticks;
    StatusSystem::pollChanges(changes);
    StatusSystem::pollTicks(ticks);
}

}  // namespace

TEST_CASE("rpg.status.adapterUsesCanonicalEffectContainerAndStableLegacyProjection") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.canonical";
    definition.durationPolicy = "infinite";
    definition.stackPolicy    = "stack";
    definition.maxStacks      = 2;
    definition.modifiers.push_back({"armor", "add", 4.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("armor", 10.0);
    const int legacyId = actor->applyEffect(definition.id, "source");
    REQUIRE(legacyId > 0);

    REQUIRE(actor->statuses()->container.effectCount() == 1);
    const auto *canonical = actor->statuses()->container.effectAt(0);
    REQUIRE(canonical != nullptr);
    // effectAt() is an immediate borrow. Re-applying the status stages and
    // publishes a replacement container, so retain the identity value rather
    // than the instance address across that structural mutation.
    const std::string canonicalId = canonical->id;
    CHECK(!canonicalId.empty());
    CHECK_EQ(actor->statuses()->effectByLegacyId.at(legacyId), canonicalId);
    CHECK_EQ(actor->statuses()->legacyIdByEffect.at(canonicalId), legacyId);
    CHECK_EQ(actor->getFinalAttribute("armor"), 14.0);

    const int reapplied = actor->applyEffect(definition.id, "source");
    CHECK_EQ(reapplied, legacyId);
    CHECK_EQ(actor->statuses()->container.effectCount(), 1);
    const auto *reappliedEffect = actor->statuses()->container.find(canonicalId);
    REQUIRE(reappliedEffect != nullptr);
    CHECK_EQ(reappliedEffect->stackCount, 2u);
    CHECK_EQ(actor->getFinalAttribute("armor"), 18.0);

    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterPreservesDurationStackAndModifierParity") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.parity";
    definition.durationPolicy = "duration";
    definition.duration       = 2.0f;
    definition.stackPolicy    = "stack";
    definition.maxStacks      = 2;
    definition.modifiers.push_back({"attack", "add", 3.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 10.0);
    const int id = actor->applyEffect(definition.id);
    REQUIRE(id > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 13.0);

    auto advanced = StatusSystem::update(0.5);
    REQUIRE(advanced.ok());
    const auto summary = std::move(advanced).takeValue();
    CHECK_EQ(summary.expired, 0u);
    CHECK(approx(actor->getStatusRemaining(0), 1.5f));

    CHECK_EQ(actor->applyEffect(definition.id), id);
    CHECK_EQ(actor->getStatusStacks(0), 2);
    CHECK(approx(actor->getStatusRemaining(0), 2.0f));
    CHECK_EQ(actor->getFinalAttribute("attack"), 16.0);

    CHECK_EQ(actor->applyEffect(definition.id), id);
    CHECK_EQ(actor->getStatusStacks(0), 2);
    CHECK_EQ(actor->getFinalAttribute("attack"), 16.0);

    auto expired = StatusSystem::update(2.0);
    REQUIRE(expired.ok());
    CHECK_EQ(std::move(expired).takeValue().expired, 1u);
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 10.0);

    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterPeriodicExecutorProducesTicksAndExpiresWithoutPostExpiryTicks") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.periodic";
    definition.durationPolicy = "duration";
    definition.duration       = 2.5f;
    definition.period         = 1.0f;
    definition.stackPolicy    = "none";
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    const int id    = actor->applyEffect(definition.id, "caster");
    REQUIRE(id > 0);

    auto first = StatusSystem::update(1.0);
    REQUIRE(first.ok());
    CHECK_EQ(std::move(first).takeValue().ticks, 1u);
    std::vector<StatusTickEvent> ticks;
    StatusSystem::pollTicks(ticks);
    REQUIRE(ticks.size() == 1);
    CHECK_EQ(ticks[0].instanceId, id);
    CHECK_EQ(ticks[0].effectId, definition.id);

    auto second = StatusSystem::update(1.0);
    REQUIRE(second.ok());
    CHECK_EQ(std::move(second).takeValue().ticks, 1u);
    ticks.clear();
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 1u);

    auto finalStep = StatusSystem::update(0.6);
    REQUIRE(finalStep.ok());
    const auto finalSummary = std::move(finalStep).takeValue();
    CHECK_EQ(finalSummary.expired, 1u);
    CHECK_EQ(actor->getStatusCount(), 0);
    ticks.clear();
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 0u);

    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.customStackPolicyUsesCandidateAndRollsBackOnFailure") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.custom.rollback";
    definition.durationPolicy = "duration";
    definition.duration       = 4.0f;
    definition.stackPolicy    = "candidatePolicy";
    definition.modifiers.push_back({"attack", "add", 2.0, 0});
    EffectRegistry::registerEffect(definition);

    StatusSystem::registerStackPolicy(
        "candidatePolicy", [](RPGActor *, StatusInstance &candidate, const EffectDefinition &, const std::string &) {
            candidate.stacks += 1;
            candidate.remaining += 10.0f;
            return -1;
        });

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 5.0);
    const int id = actor->applyEffect(definition.id);
    REQUIRE(id > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    CHECK(approx(actor->getStatusRemaining(0), 4.0f));
    const auto *canonical = actor->statuses()->container.effectAt(0);
    REQUIRE(canonical != nullptr);
    const int eventsBefore = actor->statuses()->container.eventCount();

    CHECK_EQ(actor->applyEffect(definition.id), -1);
    CHECK_EQ(actor->getStatusStacks(0), 1);
    CHECK(approx(actor->getStatusRemaining(0), 4.0f));
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    CHECK_EQ(actor->statuses()->container.eventCount(), eventsBefore);
    CHECK_EQ(actor->statuses()->container.effectAt(0)->id, canonical->id);

    StatusSystem::unregisterStackPolicy("candidatePolicy");
    actor->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterCopyIsIndependentAndLegacyIdsRemainStable") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.copy";
    definition.durationPolicy = "infinite";
    definition.stackPolicy    = "stack";
    definition.maxStacks      = 3;
    definition.modifiers.push_back({"armor", "add", 1.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *original = RPGActor::createActor();
    original->setBaseAttribute("armor", 1.0);
    const int originalId = original->applyEffect(definition.id);
    REQUIRE(originalId > 0);
    original->setStatusProp(originalId, "owner", "original");

    RPGActor::Statuses copiedStatuses   = *original->statuses();
    const int          copiedEventCount = copiedStatuses.container.eventCount();
    auto               firstClone       = copiedStatuses.container.clone();
    auto               secondClone      = copiedStatuses.container.clone();
    CHECK_EQ(firstClone.eventCount(), copiedEventCount);
    CHECK_EQ(secondClone.eventCount(), copiedEventCount);
    firstClone.clearEvents();
    CHECK_EQ(firstClone.eventCount(), 0);
    CHECK_EQ(secondClone.eventCount(), copiedEventCount);

    ::eve::effects::EffectDefinition stagingDefinition;
    stagingDefinition.id       = "adapter.copy.staging";
    stagingDefinition.stackKey = stagingDefinition.id;
    auto firstStaged           = firstClone.apply(stagingDefinition, "actor", "staging");
    auto secondStaged          = secondClone.apply(stagingDefinition, "actor", "staging");
    REQUIRE(firstStaged.ok());
    REQUIRE(secondStaged.ok());
    const std::string firstStagedId = std::move(firstStaged).takeValue();
    CHECK_EQ(std::move(secondStaged).takeValue(), firstStagedId);
    REQUIRE(firstClone.eventAt(0) != nullptr);
    REQUIRE(secondClone.eventAt(copiedEventCount) != nullptr);
    CHECK_EQ(firstClone.eventAt(0)->sequence, 2u);
    CHECK_EQ(secondClone.eventAt(copiedEventCount)->sequence, 2u);
    CHECK(copiedStatuses.container.find(firstStagedId) == nullptr);

    auto      copiedAttributes         = original->attributes()->values;
    RPGActor *copy                     = RPGActor::createActor();
    copy->statuses()->container        = copiedStatuses.container.clone();
    copy->statuses()->metadata         = copiedStatuses.metadata;
    copy->statuses()->legacyIdByEffect = copiedStatuses.legacyIdByEffect;
    copy->statuses()->effectByLegacyId = copiedStatuses.effectByLegacyId;
    copy->statuses()->nextInstanceId   = copiedStatuses.nextInstanceId;
    copy->attributes()->values         = std::move(copiedAttributes);

    CHECK_EQ(copy->getStatusCount(), 1);
    CHECK_EQ(copy->getStatusInstanceId(0), originalId);
    CHECK_EQ(copy->getStatusProp(originalId, "owner"), "original");
    CHECK_EQ(copy->getFinalAttribute("armor"), 2.0);

    CHECK_EQ(original->applyEffect(definition.id), originalId);
    CHECK_EQ(original->getStatusStacks(0), 2);
    CHECK_EQ(original->getFinalAttribute("armor"), 3.0);
    CHECK_EQ(copy->getStatusStacks(0), 1);
    CHECK_EQ(copy->getFinalAttribute("armor"), 2.0);

    CHECK(original->removeStatus(originalId));
    const int nextId = original->applyEffect(definition.id);
    CHECK(nextId > originalId);
    CHECK_EQ(copy->getStatusCount(), 1);
    CHECK_EQ(copy->getStatusInstanceId(0), originalId);

    original->release();
    copy->release();
    EffectRegistry::clear();
}

TEST_CASE("rpg.status.adapterRejectsInvalidModifierWithoutPartialState") {
    drainEvents();
    EffectRegistry::clear();

    EffectDefinition definition;
    definition.id             = "adapter.invalid.modifier";
    definition.durationPolicy = "infinite";
    definition.modifiers.push_back({"attack", "not-a-policy", 5.0, 0});
    EffectRegistry::registerEffect(definition);

    RPGActor *actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 10.0);
    auto result = StatusSystem::apply(actor, definition.id);
    CHECK(!result.ok());
    CHECK_EQ(result.code(), eve::StatusCode::Rejected);
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 10.0);
    CHECK_EQ(actor->statuses()->container.eventCount(), 0);

    actor->release();
    EffectRegistry::clear();
}
}  // namespace test_status_effect_adapter
#endif
