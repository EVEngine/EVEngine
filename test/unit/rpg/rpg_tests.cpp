#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg)
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "rpg/AttributeSystem.h"
#include "rpg/AttributeTypes.h"
#include "rpg/Effect.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/Settlement.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"

namespace test_rpg_contract {
using namespace eve::rpg;

TEST_CASE("Attribute evaluation applies modifier phases in order and admits custom operations") {
    AttributeValue value;
    value.base = 100;
    SUB_CASE("add then percent add then percent multiply") {
        value.modifiers = {
            {"flat", "gear", "add", 10, 0}, {"percent", "gear", "mulAdd", .5, 0}, {"factor", "gear", "mulMul", .1, 0}};
        CHECK(std::fabs(computeAttributeValue(value) - 181.5) < 1e-9);
    };
    SUB_CASE("higher priority override takes precedence") {
        value.modifiers = {{"high", "script", "override", 50, 5}, {"low", "script", "override", 20, 1}};
        CHECK_EQ(computeAttributeValue(value), 50.0);
    };
    SUB_CASE("range clamps apply to final value") {
        value.base      = 200;
        value.modifiers = {{"max", "rule", "clampMax", 100, 0}, {"min", "rule", "clampMin", 10, 0}};
        CHECK_EQ(computeAttributeValue(value), 100.0);
    };
    SUB_CASE("registered operation is used and unknown operation leaves value unchanged") {
        AttributeOpTable ops;
        ops.registerOp("square", [](double, double amount) { return amount * amount; });
        value.base      = 7;
        value.modifiers = {{"custom", "rule", "square", 4, 0}};
        CHECK_EQ(computeAttributeValue(value, &ops), 16.0);
        value.modifiers = {{"missing", "rule", "unknown", 4, 0}};
        REQUIRE_EQ(computeAttributeValue(value, &ops), 7.0);
    };
}

TEST_CASE("Actor attributes reflect base edits and source scoped modifier removal") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("power", 10);
    actor->setBaseAttribute("mana", 50);
    CHECK(actor->hasAttribute("power"));
    CHECK(!actor->hasAttribute("unknown"));
    const auto gear = actor->addAttributeModifier("power", "gear", "add", 5);
    actor->addAttributeModifier("power", "spell", "add", 2);
    actor->addAttributeModifier("mana", "spell", "add", 20);
    CHECK_EQ(actor->getFinalAttribute("power"), 17.0);
    actor->modifyBaseAttribute("power", 3);
    CHECK_EQ(actor->getFinalAttribute("power"), 20.0);
    CHECK(actor->removeAttributeModifier("power", gear));
    CHECK(!actor->removeAttributeModifier("power", gear));
    CHECK_EQ(actor->getFinalAttribute("power"), 15.0);
    const int removedSpellModifiers = actor->removeAllAttributeModifiersBySource("spell");
    CHECK_EQ(removedSpellModifiers, 2);
    CHECK_EQ(actor->getFinalAttribute("power"), 13.0);
    CHECK_EQ(actor->getFinalAttribute("mana"), 50.0);
    actor->release();
}

TEST_CASE("Effect registry publishes definitions and JSON payloads then removes by id") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.effect.registry";
    effect.durationPolicy = "duration";
    effect.duration       = 3;
    effect.tags           = {"buff"};
    EffectRegistry::registerEffect(effect);
    const auto* found = EffectRegistry::find(effect.id);
    REQUIRE(found != nullptr);
    CHECK_EQ(found->duration, 3.f);
    CHECK(found->hasTag("buff"));
    CHECK(EffectRegistry::remove(effect.id));
    CHECK(EffectRegistry::find(effect.id) == nullptr);
    std::string error;
    const int   loadedEffects = EffectRegistry::loadFromJson(
        R"([{"id":"rewrite.rpg.effect.json","durationPolicy":"infinite","tags":["poison"]}])", &error);
    CHECK_EQ(loadedEffects, 1);
    CHECK(error.empty());
    found = EffectRegistry::find("rewrite.rpg.effect.json");
    REQUIRE(found != nullptr);
    CHECK(found->hasTag("poison"));
    CHECK(EffectRegistry::remove("rewrite.rpg.effect.json"));
}

TEST_CASE("Effect lifetime distinguishes instant application from expiring modifiers") {
    EffectDefinition instant;
    instant.id             = "rewrite.rpg.effect.instant";
    instant.durationPolicy = "instant";
    instant.modifiers.push_back({"health", "add", 25, 0});
    EffectRegistry::registerEffect(instant);
    EffectDefinition duration;
    duration.id             = "rewrite.rpg.effect.duration";
    duration.durationPolicy = "duration";
    duration.duration       = 1;
    duration.modifiers.push_back({"attack", "add", 10, 0});
    EffectRegistry::registerEffect(duration);
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("health", 50);
    actor->setBaseAttribute("attack", 20);
    const int instantApplication = actor->applyEffect(instant.id);
    CHECK_EQ(instantApplication, 0);
    CHECK_EQ(actor->getBaseAttribute("health"), 75.0);
    CHECK_EQ(actor->getStatusCount(), 0);
    REQUIRE(actor->applyEffect(duration.id) > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 30.0);
    CHECK(StatusSystem::update(.5).ok());
    CHECK_EQ(actor->getStatusCount(), 1);
    CHECK(StatusSystem::update(.6).ok());
    CHECK_EQ(actor->getStatusCount(), 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 20.0);
    actor->release();
    EffectRegistry::remove(instant.id);
    EffectRegistry::remove(duration.id);
}

TEST_CASE("Effect stack policies reject duplicates refresh duration or cap modifier stacks") {
    SUB_CASE("none rejects duplicate") {
        EffectDefinition def;
        def.id             = "rewrite.rpg.stack.none";
        def.durationPolicy = "infinite";
        def.stackPolicy    = "none";
        EffectRegistry::registerEffect(def);
        auto* actor = RPGActor::createActor();
        REQUIRE(actor->applyEffect(def.id) > 0);
        const int duplicate = actor->applyEffect(def.id);
        CHECK_EQ(duplicate, -1);
        CHECK_EQ(actor->getStatusCount(), 1);
        actor->release();
        EffectRegistry::remove(def.id);
    };
    SUB_CASE("refresh restores full remaining time") {
        EffectDefinition def;
        def.id             = "rewrite.rpg.stack.refresh";
        def.durationPolicy = "duration";
        def.duration       = 2;
        def.stackPolicy    = "refresh";
        EffectRegistry::registerEffect(def);
        auto*     actor = RPGActor::createActor();
        const int id    = actor->applyEffect(def.id);
        REQUIRE(id > 0);
        CHECK(StatusSystem::update(1.5).ok());
        const int refreshed = actor->applyEffect(def.id);
        CHECK_EQ(refreshed, id);
        CHECK(std::fabs(actor->getStatusRemaining(0) - 2) < 1e-4);
        actor->release();
        EffectRegistry::remove(def.id);
    };
    SUB_CASE("stack caps count and its attribute contribution") {
        EffectDefinition def;
        def.id             = "rewrite.rpg.stack.cap";
        def.durationPolicy = "infinite";
        def.stackPolicy    = "stack";
        def.maxStacks      = 2;
        def.modifiers.push_back({"armor", "add", 5, 0});
        EffectRegistry::registerEffect(def);
        auto* actor = RPGActor::createActor();
        actor->setBaseAttribute("armor", 0);
        const int id = actor->applyEffect(def.id);
        REQUIRE(id > 0);
        const int stacked = actor->applyEffect(def.id);
        CHECK_EQ(stacked, id);
        actor->applyEffect(def.id);
        CHECK_EQ(actor->getStatusStacks(0), 2);
        CHECK_EQ(actor->getFinalAttribute("armor"), 10.0);
        CHECK(actor->removeStatus(id));
        CHECK_EQ(actor->getFinalAttribute("armor"), 0.0);
        actor->release();
        EffectRegistry::remove(def.id);
    };
}

TEST_CASE("Periodic effects emit due ticks and stop emitting after expiry") {
    EffectDefinition def;
    def.id             = "rewrite.rpg.periodic";
    def.durationPolicy = "duration";
    def.duration       = 2.5f;
    def.period         = 1.f;
    EffectRegistry::registerEffect(def);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor->applyEffect(def.id, "caster") > 0);
    std::vector<StatusTickEvent> ticks;
    CHECK(StatusSystem::update(1).ok());
    StatusSystem::pollTicks(ticks);
    REQUIRE_EQ(ticks.size(), 1u);
    CHECK_EQ(ticks.front().actor, actor);
    CHECK_EQ(ticks.front().source, "caster");
    ticks.clear();
    CHECK(StatusSystem::update(1).ok());
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(ticks.size(), 1u);
    ticks.clear();
    CHECK(StatusSystem::update(1).ok());
    StatusSystem::pollTicks(ticks);
    CHECK_EQ(actor->getStatusCount(), 0);
    ticks.clear();
    CHECK(StatusSystem::update(5).ok());
    StatusSystem::pollTicks(ticks);
    CHECK(ticks.empty());
    actor->release();
    EffectRegistry::remove(def.id);
}

TEST_CASE("Skill admission reports prerequisites then instant cast spends resource and applies effect") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.skill.effect";
    effect.durationPolicy = "duration";
    effect.duration       = 5;
    effect.modifiers.push_back({"defense", "add", -3, 0});
    EffectRegistry::registerEffect(effect);
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.skill.instant";
    skill.cooldown = 3;
    auto cost      = eve::resource::CostSpec::single("mana", 10);
    REQUIRE(cost.hasValue());
    skill.cost = std::move(cost).takeValue();
    skill.grantedEffects.push_back(effect.id);
    SkillRegistry::registerSkill(skill);
    auto* caster = RPGActor::createActor();
    auto* target = RPGActor::createActor();
    caster->setBaseAttribute("mana", 5);
    target->setBaseAttribute("defense", 10);
    CHECK_EQ(caster->canCastSkillReason(skill.id), "not_learned");
    caster->learnSkill(skill.id);
    CHECK_EQ(caster->canCastSkillReason(skill.id), "insufficient_cost");
    caster->setBaseAttribute("mana", 20);
    REQUIRE(caster->beginCastSkill(skill.id, target));
    CHECK_EQ(caster->getBaseAttribute("mana"), 10.0);
    CHECK_EQ(caster->getSkillCooldown(skill.id), 3.f);
    CHECK_EQ(target->getFinalAttribute("defense"), 7.0);
    CHECK_EQ(caster->canCastSkillReason(skill.id), "on_cooldown");
    std::vector<SkillCastEvent> casts;
    SkillSystem::pollCastEvents(casts);
    REQUIRE_EQ(casts.size(), 1u);
    CHECK_EQ(casts.front().target, target);
    SkillSystem::update(3.1f);
    CHECK(caster->canCastSkill(skill.id));
    caster->release();
    target->release();
    SkillRegistry::remove(skill.id);
    EffectRegistry::remove(effect.id);
}

TEST_CASE("Timed skill emits only when completed and cancellation suppresses resolution") {
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.skill.timed";
    skill.castTime = 2;
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    actor->learnSkill(skill.id);
    REQUIRE(actor->beginCastSkill(skill.id));
    SkillSystem::update(1);
    CHECK(actor->isCastingSkill());
    CHECK(std::fabs(actor->getCastProgress() - .5f) < 1e-4f);
    std::vector<SkillCastEvent> casts;
    SkillSystem::pollCastEvents(casts);
    CHECK(casts.empty());
    actor->cancelCastSkill();
    SkillSystem::update(5);
    SkillSystem::pollCastEvents(casts);
    CHECK(casts.empty());
    REQUIRE(actor->beginCastSkill(skill.id));
    SkillSystem::update(2.1f);
    SkillSystem::pollCastEvents(casts);
    REQUIRE_EQ(casts.size(), 1u);
    CHECK_EQ(casts.front().skillId, skill.id);
    actor->release();
    SkillRegistry::remove(skill.id);
}

TEST_CASE("Settlement stages execute by priority and cancellation prevents later mutation") {
    const std::string pipeline = "rewrite.rpg.settlement";
    SettlementPipeline::clearPipeline(pipeline);
    std::vector<std::string> order;
    SettlementPipeline::registerStage(pipeline, "later", 10, [&](SettlementContext&) { order.push_back("later"); });
    SettlementPipeline::registerStage(pipeline, "first", 0, [&](SettlementContext&) { order.push_back("first"); });
    SettlementContext context;
    SettlementPipeline::run(pipeline, context);
    REQUIRE_EQ(order.size(), 2u);
    CHECK_EQ(order[0], "first");
    CHECK_EQ(order[1], "later");
    SettlementPipeline::clearPipeline(pipeline);
    SettlementPipeline::registerStage(pipeline, "cancel", 0, [](SettlementContext& ctx) { ctx.cancelled = true; });
    SettlementPipeline::registerStage(pipeline, "later", 10,
                                      [&](SettlementContext&) { order.push_back("unexpected"); });
    SettlementPipeline::run(pipeline, context);
    CHECK(context.cancelled);
    CHECK_EQ(order.size(), 2u);
    SettlementPipeline::clearPipeline(pipeline);
}
}  // namespace test_rpg_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_actor_checkpoint)
#include "PrivateTestScope.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
namespace test_rpg_actor_checkpoint_contract {
using namespace eve::rpg;

TEST_CASE("Actor checkpoint restores attributes vitals and progression while rejecting malformed replacement") {
    SkillDefinition skill;
    skill.id = "rewrite.rpg.checkpoint.skill";
    SkillRegistry::registerSkill(skill);
    auto* source   = RPGActor::createActor();
    auto* restored = RPGActor::createActor();
    REQUIRE(source != nullptr);
    REQUIRE(restored != nullptr);
    source->setBaseAttribute("attack", 18);
    source->setBaseAttribute("hp", 100);
    source->setCurrent("hp", 47);
    source->learnSkill(skill.id);
    REQUIRE(source->restoreProgression(4, 25, 120).ok());
    const auto snapshot = source->checkpointJson();
    REQUIRE(snapshot.ok());
    restored->setBaseAttribute("attack", 1);
    restored->setBaseAttribute("hp", 100);
    REQUIRE(restored->restoreCheckpointJson(snapshot.value()).ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getCurrent("hp"), 47.0);
    CHECK_EQ(restored->getLevel(), 4);
    CHECK(restored->knowsSkill(skill.id));
    const auto rejected = restored->restoreCheckpointJson(
        R"({"schema":"eve.rpg.actor-checkpoint","version":1,"baseAttributes":[],"class":{"id":"","skillsSyncedUpTo":0},"learnedSkills":[],"progression":{"level":0,"xp":0,"xpToNext":1},"traits":[],"vitals":{}})");
    CHECK(!rejected.ok());
    CHECK_EQ(restored->getBaseAttribute("attack"), 18.0);
    CHECK_EQ(restored->getLevel(), 4);
    source->release();
    restored->release();
    SkillRegistry::remove(skill.id);
}

TEST_CASE("Actor checkpoint refuses a live cast and succeeds after cancellation") {
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.checkpoint.channel";
    skill.castTime = 2;
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill(skill.id);
    REQUIRE(actor->beginCastSkill(skill.id, actor));
    CHECK(!actor->checkpointJson().ok());
    actor->cancelCastSkill();
    CHECK(actor->checkpointJson().ok());
    actor->release();
    SkillRegistry::remove(skill.id);
}
}  // namespace test_rpg_actor_checkpoint_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_battle)
#include <cstdint>
#include <vector>
#include "PrivateTestScope.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/SettlementAdapter.h"
#include "rpg/VitalsSystem.h"
namespace test_rpg_battle_contract {
using namespace eve::rpg;
namespace {
RPGActor* fighter(double attack, double defense, double hp, double speed) {
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", defense);
    actor->setBaseAttribute("hp", hp);
    actor->setBaseAttribute("speed", speed);
    actor->setCurrent("hp", hp);
    return actor;
}
}  // namespace

TEST_CASE("Damage formula evaluates actor attributes and rejects malformed expressions") {
    auto*      attacker = fighter(10, 0, 100, 5);
    auto*      target   = fighter(0, 2, 100, 1);
    const auto value    = BattleSystem::evaluateFormulaChecked("a.attack * 4 - b.defense * 2", attacker, target);
    REQUIRE(value.ok());
    CHECK_EQ(value.value(), 36.0);
    const auto unary = BattleSystem::evaluateFormulaChecked("-(a.attack - b.defense)", attacker, target);
    REQUIRE(unary.ok());
    CHECK_EQ(unary.value(), -8.0);
    SUB_CASE("missing close parenthesis is rejected") {
        CHECK(!BattleSystem::evaluateFormulaChecked("(a.attack + 2", attacker, target).ok());
    };
    SUB_CASE("trailing tokens are rejected") {
        CHECK(!BattleSystem::evaluateFormulaChecked("a.attack garbage", attacker, target).ok());
    };
    SUB_CASE("division by zero is rejected") {
        CHECK(!BattleSystem::evaluateFormulaChecked("a.attack / 0", attacker, target).ok());
    };
    attacker->release();
    target->release();
}

TEST_CASE("Invalid damage registration retains the previously published skill rule") {
    BattleSystem::clearSkillDamage();
    SkillDamageSpec valid{"hp", "a.attack / b.defense", "", 0, 100};
    REQUIRE(BattleSystem::registerSkillDamageChecked("rewrite.rpg.strike", valid).ok());
    SkillDamageSpec invalid{"hp", "a.attack + )", "", 0, 100};
    CHECK(!BattleSystem::registerSkillDamageChecked("rewrite.rpg.strike", invalid).ok());
    const auto* retained = BattleSystem::findSkillDamage("rewrite.rpg.strike");
    REQUIRE(retained != nullptr);
    CHECK_EQ(retained->formula, std::string("a.attack / b.defense"));
    BattleSystem::clearSkillDamage();
}

TEST_CASE("Battle resolves legal actions into damage victory and life state") {
    auto*  hero  = fighter(30, 0, 100, 10);
    auto*  enemy = fighter(5, 0, 20, 1);
    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    REQUIRE(battle.setActionChecked(hero, "", enemy).ok());
    battle.autoEnemyActions();
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    REQUIRE_EQ(VitalsSystem::getCurrent(enemy, "hp"), 0.0);
    CHECK(battle.isFinished());
    CHECK(battle.isVictory());
    CHECK(!battle.isActorAlive(enemy));
    battle.pollEvents();
    bool damage  = false;
    bool victory = false;
    for (int i = 0; i < battle.getEventCount(); ++i) {
        const auto event = battle.getEvent(i);
        damage |= event.action == "damage";
        victory |= event.action == "victory";
    }
    CHECK(damage);
    REQUIRE(victory);
    hero->release();
    enemy->release();
}

TEST_CASE("Battle damage passes through mitigation rules before changing vitals") {
    auto*                           hero  = fighter(20, 0, 100, 10);
    auto*                           enemy = fighter(0, 0, 100, 1);
    eve::settlement::SettlementRule mitigation;
    mitigation.id                  = "rewrite.rpg.guard";
    mitigation.source              = "effect:guard";
    mitigation.stage               = eve::settlement::StageKind::TargetMitigation;
    mitigation.operation           = eve::settlement::RuleOperation::ResistPercent;
    mitigation.value               = .5;
    mitigation.filter.kinds        = {"damage"};
    mitigation.filter.requiredTags = {"rpg:damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({mitigation}).ok());
    Battle battle;
    REQUIRE(battle.configureSettlementRules(rules).ok());
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    battle.setAction(hero, "", enemy);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(enemy->getCurrent("hp"), 90.0);
    std::vector<VitalsEvent> events;
    VitalsSystem::pollEvents(events);
    REQUIRE(!events.empty());
    CHECK_EQ(events.front().amount, 10.0);
    hero->release();
    enemy->release();
}
}  // namespace test_rpg_battle_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_class)
#include "PrivateTestScope.h"
#include "rpg/Class.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
namespace test_rpg_class_contract {
using namespace eve::rpg;

TEST_CASE("Class catalogue loads level gated skills and class change replaces trait effects") {
    auto* rpg = RPG::create();
    rpg->clearClassDefinitions();
    rpg->clearSkillDefinitions();
    rpg->clearTraitDefinitions();
    const int loadedSkills = rpg->registerSkillsFromJson(R"([{"id":"rewrite.rpg.strike"},{"id":"rewrite.rpg.fire"}])");
    REQUIRE_EQ(loadedSkills, 2);
    const int loadedTraits = rpg->registerTraitsFromJson(
        R"([{"id":"rewrite.rpg.mighty","traits":[{"kind":"paramRate","target":"attack","value":1.5}]}])");
    REQUIRE_EQ(loadedTraits, 1);
    const int loadedClasses = rpg->registerClassesFromJson(R"([
      {"id":"rewrite.rpg.warrior","traits":["rewrite.rpg.mighty"],"learnSkills":[{"skillId":"rewrite.rpg.strike","level":2}]},
      {"id":"rewrite.rpg.mage","learnSkills":[{"skillId":"rewrite.rpg.fire","level":1}]}
    ])");
    REQUIRE_EQ(loadedClasses, 2);
    auto* actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100);
    REQUIRE(actor->setClass("rewrite.rpg.warrior"));
    CHECK_EQ(actor->getFinalAttribute("attack"), 150.0);
    CHECK(!actor->knowsSkill("rewrite.rpg.strike"));
    REQUIRE(actor->restoreProgression(2, 0, 10).ok());
    const int learnedOnLevelUp = actor->checkLevelSkills();
    CHECK_EQ(learnedOnLevelUp, 1);
    CHECK(actor->knowsSkill("rewrite.rpg.strike"));
    const int learnedAgain = actor->checkLevelSkills();
    CHECK_EQ(learnedAgain, 0);
    REQUIRE(actor->setClass("rewrite.rpg.mage"));
    CHECK_EQ(actor->getFinalAttribute("attack"), 100.0);
    CHECK(actor->knowsSkill("rewrite.rpg.fire"));
    actor->release();
    rpg->clearClassDefinitions();
    rpg->clearTraitDefinitions();
    rpg->clearSkillDefinitions();
}
}  // namespace test_rpg_class_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_gamestate)
#include "PrivateTestScope.h"
#include "rpg/GameState.h"
#include "rpg/RPG.h"
namespace test_rpg_game_state_contract {
using namespace eve::rpg;

TEST_CASE("Game state keeps global switches variables and per subject values in distinct scopes") {
    GameState state;
    CHECK(!state.isSwitchOn("door"));
    state.switchOn("door");
    state.setVariable("gold", 100);
    state.addVariable("gold", 25);
    state.setSelfVariable("map:1:event:3", "visits", 2);
    state.setSelfString("story.one", "cursor", "active");
    CHECK(state.isSwitchOn("door"));
    CHECK_EQ(state.getVariable("gold"), 125.0);
    CHECK_EQ(state.getSelfVariable("map:1:event:3", "visits"), 2.0);
    CHECK(!state.hasSelfVariable("map:1:event:4", "visits"));
    CHECK_EQ(state.getSelfString("story.one", "cursor"), std::string("active"));
    state.clearSelfString("story.one", "cursor");
    CHECK(!state.hasSelfString("story.one", "cursor"));
    state.clear();
    CHECK(!state.isSwitchOn("door"));
    REQUIRE_EQ(state.getVariable("gold"), 0.0);
}

TEST_CASE("Game state snapshot round trips and invalid schema or data leaves published state intact") {
    GameState source;
    source.switchOn("door.open");
    source.setVariable("gold", 125.5);
    source.setSelfVariable("map:1", "visits", 2);
    const auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());
    GameState target;
    target.setVariable("sentinel", 7);
    REQUIRE(target.restoreSnapshotJson(encoded.value()).ok());
    CHECK(target.isSwitchOn("door.open"));
    CHECK_EQ(target.getVariable("gold"), 125.5);
    CHECK_EQ(target.getSelfVariable("map:1", "visits"), 2.0);
    CHECK_EQ(target.getVariable("sentinel"), 0.0);
    {
        CHECK(
            !target
                 .restoreSnapshotJson(
                     R"({"schema":"eve.rpg.game-state","version":1,"switches":{},"variables":{"gold":"bad"},"selfVariables":{}})")
                 .ok());
        CHECK_EQ(target.getVariable("gold"), 125.5);
    };
    {
        CHECK(!target
                   .restoreSnapshotJson(
                       R"({"schema":"eve.rpg.game-state","version":2,"switches":{},"variables":{},"selfVariables":{}})")
                   .ok());
        CHECK_EQ(target.getVariable("gold"), 125.5);
    };
}
}  // namespace test_rpg_game_state_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_progression)
#include "PrivateTestScope.h"
#include "rpg/RPGActor.h"
#include "rpg/VitalsSystem.h"
namespace test_rpg_progression_contract {
using namespace eve::rpg;

TEST_CASE("Experience carries overflow across level thresholds and rejects invalid restoration atomically") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setXpToNext(100);
    CHECK(!actor->gainXp(50));
    CHECK_EQ(actor->getLevel(), 1);
    CHECK(actor->gainXp(60));
    CHECK_EQ(actor->getLevel(), 2);
    CHECK_EQ(actor->getXp(), 10.0);
    CHECK_EQ(actor->getXpToNext(), 120.0);
    REQUIRE(actor->restoreProgression(4, 12, 86.4).ok());
    CHECK(!actor->restoreProgression(0, 100, 10).ok());
    CHECK_EQ(actor->getLevel(), 4);
    CHECK_EQ(actor->getXp(), 12.0);
    actor->release();
}

TEST_CASE("Vitals clamp writes damage and healing and revival restores capacity") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("hp", 100);
    actor->setCurrent("hp", 999);
    CHECK_EQ(actor->getCurrent("hp"), 100.0);
    actor->setCurrent("hp", 50);
    const double firstDamage = actor->takeDamage("hp", 30, "enemy");
    CHECK_EQ(firstDamage, 30.0);
    CHECK_EQ(actor->getCurrent("hp"), 20.0);
    const double lethalDamage = actor->takeDamage("hp", 999, "enemy");
    CHECK_EQ(lethalDamage, 20.0);
    CHECK(actor->isDead("hp"));
    const double postMortemDamage = actor->takeDamage("hp", 10, "enemy");
    CHECK_EQ(postMortemDamage, 0.0);
    actor->revive("hp");
    CHECK_EQ(actor->getCurrent("hp"), 100.0);
    actor->setCurrent("hp", 30);
    const double restoredHealth = actor->heal("hp", 999);
    CHECK_EQ(restoredHealth, 70.0);
    CHECK_EQ(actor->getCurrent("hp"), 100.0);
    actor->release();
}
}  // namespace test_rpg_progression_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_quest)
#include "PrivateTestScope.h"
#include "rpg/Quest.h"
#include "rpg/Tracker.h"
namespace test_rpg_quest_contract {
using namespace eve::rpg;

TEST_CASE("Quest catalogue parses objectives rewards and defaults while excluding invalid cycles") {
    QuestRegistry::clear();
    const int accepted = QuestRegistry::loadFromJson(R"([
      {"id":"rewrite.rpg.quest.hunt","completePolicy":"claim","objectives":[{"id":"kill","topic":"kill","target":"slime","count":2}],"rewards":[{"type":"item","id":"potion","amount":3}]},
      {"id":"rewrite.rpg.quest.bad.a","requires":["rewrite.rpg.quest.bad.b"]},
      {"id":"rewrite.rpg.quest.bad.b","requires":["rewrite.rpg.quest.bad.a"]}
    ])");
    CHECK_EQ(accepted, 1);
    const auto* hunt = QuestRegistry::find("rewrite.rpg.quest.hunt");
    REQUIRE(hunt != nullptr);
    CHECK_EQ(hunt->startPolicy, std::string("manual"));
    CHECK_EQ(hunt->completePolicy, std::string("claim"));
    REQUIRE_EQ(hunt->objectives.size(), 1u);
    CHECK_EQ(hunt->objectives.front().count, 2);
    REQUIRE_EQ(hunt->rewards.size(), 1u);
    CHECK_EQ(hunt->rewards.front().amount, 3.0);
    CHECK(QuestRegistry::find("rewrite.rpg.quest.bad.a") == nullptr);
    QuestRegistry::clear();
}

TEST_CASE("Quest tracker filters topic and target then caps progress before claim") {
    QuestRegistry::clear();
    REQUIRE_EQ(
        QuestRegistry::loadFromJson(
            R"([{"id":"rewrite.rpg.quest.track","startPolicy":"manual","completePolicy":"claim","objectives":[{"id":"slime","topic":"kill","target":"slime","count":2},{"id":"any","topic":"kill","count":3}]}])"),
        1);
    Tracker tracker;
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.track"), std::string("inactive"));
    REQUIRE(tracker.activate("rewrite.rpg.quest.track"));
    tracker.notify("talk", "slime", 1);
    CHECK_EQ(tracker.getObjectiveCurrent("rewrite.rpg.quest.track", 0), 0);
    tracker.notify("kill", "slime", 1);
    tracker.notify("kill", "goblin", 8);
    CHECK_EQ(tracker.getObjectiveCurrent("rewrite.rpg.quest.track", 0), 1);
    CHECK_EQ(tracker.getObjectiveCurrent("rewrite.rpg.quest.track", 1), 3);
    tracker.notify("kill", "slime", 1);
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.track"), std::string("ready"));
    REQUIRE(tracker.claim("rewrite.rpg.quest.track"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.track"), std::string("completed"));
    QuestRegistry::clear();
}

TEST_CASE("Quest prerequisite unlocks only after claim and failed quests require reset") {
    QuestRegistry::clear();
    const int loadedQuests = QuestRegistry::loadFromJson(R"([
      {"id":"rewrite.rpg.quest.first","startPolicy":"manual","completePolicy":"claim","objectives":[{"id":"x","topic":"kill","count":1}]},
      {"id":"rewrite.rpg.quest.next","startPolicy":"auto","requires":["rewrite.rpg.quest.first"],"objectives":[{"id":"y","topic":"talk","count":1}]}
    ])");
    REQUIRE_EQ(loadedQuests, 2);
    Tracker tracker;
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("locked"));
    REQUIRE(tracker.activate("rewrite.rpg.quest.first"));
    tracker.notify("kill", "slime", 1);
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.first"), std::string("ready"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("locked"));
    REQUIRE(tracker.claim("rewrite.rpg.quest.first"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("active"));
    REQUIRE(tracker.abandon("rewrite.rpg.quest.next"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("failed"));
    CHECK(!tracker.activate("rewrite.rpg.quest.next"));
    REQUIRE(tracker.reset("rewrite.rpg.quest.next"));
    CHECK_EQ(tracker.getState("rewrite.rpg.quest.next"), std::string("active"));
    QuestRegistry::clear();
}
}  // namespace test_rpg_quest_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_reload)
#include "PrivateTestScope.h"
#include "common/StateValue.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgState.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
namespace test_rpg_reload_contract {
using namespace eve::rpg;

TEST_CASE("Runtime RPG state restores an in flight cast and cooldown then reset clears casting") {
    SkillDefinition skill;
    skill.id       = "rewrite.rpg.reload.channel";
    skill.castTime = 1.5f;
    SkillRegistry::registerSkill(skill);
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->learnSkill(skill.id);
    REQUIRE(SkillSystem::beginCast(actor, skill.id));
    actor->setSkillCooldown(skill.id, 2.5f);
    eve::StateValue snapshot;
    REQUIRE(captureRpgState(snapshot));
    SkillSystem::cancelCast(actor);
    actor->setSkillCooldown(skill.id, 0);
    std::string error;
    REQUIRE(restoreRpgState(snapshot, &error));
    CHECK(error.empty());
    CHECK(SkillSystem::isCasting(actor));
    CHECK_EQ(SkillSystem::getCastingSkillId(actor), skill.id);
    CHECK_EQ(SkillSystem::getCooldownRemaining(actor, skill.id), 2.5f);
    REQUIRE(resetRpgState());
    CHECK(!SkillSystem::isCasting(actor));
    CHECK(!restoreRpgState(eve::StateValue::object(), &error));
    CHECK(!error.empty());
    actor->release();
    SkillRegistry::remove(skill.id);
}
}  // namespace test_rpg_reload_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_skill_condition)
#include <optional>
#include "PrivateTestScope.h"
#include "common/ConditionKeys.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/SkillCondition.h"
#include "rpg/SkillSystem.h"
namespace test_rpg_skill_condition_contract {
using namespace eve::rpg;
using namespace eve::condition;

TEST_CASE("Skill condition reads canonical actor attributes tags phase and cooldown") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    actor->setBaseAttribute("mana", 20);
    actor->learnSkill("rewrite.rpg.condition.fire");
    SkillDefinition skill;
    skill.id             = "rewrite.rpg.condition.fire";
    skill.tags           = {"fire"};
    const auto condition = Condition::all({tagPresent("fire"), attributeExists("mana"), resourceExists("mana"),
                                           Condition::compare("skill.cooldown", CompareOperator::Equal, 0),
                                           stateEquals("skill.phase", "idle")});
    CHECK(SkillConditionAdapter::evaluate(actor, skill, condition).passed());
    actor->setSkillCooldown(skill.id, 2);
    const auto blocked =
        SkillConditionAdapter::evaluate(actor, skill, Condition::compare("skill.cooldown", CompareOperator::Equal, 0));
    CHECK(!blocked.passed());
    CHECK_EQ(static_cast<int>(blocked.reasonCode()), static_cast<int>(ConditionReasonCode::ValueMismatch));
    actor->release();
}

TEST_CASE("Skill condition authority and policy query providers are called without mutating actor") {
    auto* actor = RPGActor::createActor();
    REQUIRE(actor != nullptr);
    SkillDefinition skill;
    skill.id                             = "rewrite.rpg.condition.policy";
    int                   authorityCalls = 0;
    int                   policyCalls    = 0;
    SkillConditionQueries queries;
    queries.authority = [&](std::string_view scope) -> std::optional<bool> {
        ++authorityCalls;
        return scope == "cast";
    };
    queries.policy = [&](std::string_view name, const eve::Value&) -> std::optional<ConditionResult> {
        ++policyCalls;
        return name == "safe" ? std::optional<ConditionResult>(ConditionResult::success()) : std::nullopt;
    };
    const auto result = SkillConditionAdapter::evaluate(
        actor, skill, Condition::all({authorityAllow("cast"), Condition::policyCall("safe")}), queries);
    CHECK(result.passed());
    CHECK_EQ(authorityCalls, 1);
    CHECK_EQ(policyCalls, 1);
    actor->release();
}
}  // namespace test_rpg_skill_condition_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_story_event)
#include "PrivateTestScope.h"
#include "rpg/GameState.h"
#include "rpg/StoryEvent.h"
namespace test_rpg_story_event_contract {
using namespace eve::rpg;

TEST_CASE("Story event catalogue replaces atomically and session resumes from persisted cursor") {
    StoryEventCatalogue::clear();
    const std::string content = R"({"schema":"eve.rpg.story-events","version":1,"events":[
      {"id":"rewrite.rpg.story.once","repeatable":false,"steps":[{"kind":"dialogue","reference":"arrival","actorId":"","x":0,"y":0,"duration":0},{"kind":"message","reference":"done","actorId":"","x":0,"y":0,"duration":0}]},
      {"id":"rewrite.rpg.story.repeat","repeatable":true,"steps":[{"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0.25}]}
    ]})";
    REQUIRE(StoryEventCatalogue::replaceFromJsonStrict(content).ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);
    CHECK(
        !StoryEventCatalogue::replaceFromJsonStrict(
             R"({"schema":"eve.rpg.story-events","version":1,"events":[{"id":"bad","repeatable":false,"steps":[{"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0}]}]})")
             .ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);
    GameState         state;
    StoryEventSession session;
    REQUIRE(session.begin("rewrite.rpg.story.once", &state).ok());
    CHECK_EQ(session.getStepKind(), std::string("dialogue"));
    REQUIRE(session.advance(&state).ok());
    const auto snapshot = state.snapshotJson();
    REQUIRE(snapshot.ok());
    GameState restored;
    REQUIRE(restored.restoreSnapshotJson(snapshot.value()).ok());
    StoryEventSession resumed;
    REQUIRE(resumed.begin("rewrite.rpg.story.once", &restored).ok());
    CHECK_EQ(resumed.getStepIndex(), 1);
    CHECK_EQ(resumed.getStepKind(), std::string("message"));
    REQUIRE(resumed.advance(&restored).ok());
    CHECK(resumed.isFinished());
    CHECK(!StoryEventSession().begin("rewrite.rpg.story.once", &restored).ok());
    StoryEventSession repeat;
    REQUIRE(repeat.begin("rewrite.rpg.story.repeat", &restored).ok());
    REQUIRE(repeat.advance(&restored).ok());
    CHECK(repeat.begin("rewrite.rpg.story.repeat", &restored).ok());
    StoryEventCatalogue::clear();
}
}  // namespace test_rpg_story_event_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_trait)
#include "PrivateTestScope.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "rpg/Trait.h"
namespace test_rpg_trait_contract {
using namespace eve::rpg;

TEST_CASE("Trait projection changes combat attributes and source removal retracts only owned effects") {
    auto* rpg = RPG::create();
    rpg->clearTraitDefinitions();
    const int loadedTraits = rpg->registerTraitsFromJson(R"([
      {"id":"rewrite.rpg.trait.mighty","traits":[{"kind":"paramRate","target":"attack","value":1.5},{"kind":"exParam","target":"critRate","value":0.3}]},
      {"id":"rewrite.rpg.trait.resist","traits":[{"kind":"elementRate","target":"fire","value":0.5}]}
    ])");
    REQUIRE_EQ(loadedTraits, 2);
    auto* actor = rpg->newActor();
    actor->setBaseAttribute("attack", 100);
    REQUIRE(actor->applyTrait("rewrite.rpg.trait.mighty", "class") > 0);
    REQUIRE(actor->applyTrait("rewrite.rpg.trait.resist", "equipment") > 0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 150.0);
    CHECK(std::fabs(actor->getExParam("critRate") - .3) < 1e-9);
    CHECK_EQ(actor->getElementRate("fire"), .5);
    const int removedEquipmentTraits = actor->removeTraitsBySource("equipment");
    CHECK_EQ(removedEquipmentTraits, 1);
    CHECK_EQ(actor->getElementRate("fire"), 1.0);
    CHECK_EQ(actor->getFinalAttribute("attack"), 150.0);
    const int removedMightyTraits = actor->removeTraitsByTrait("rewrite.rpg.trait.mighty");
    CHECK_EQ(removedMightyTraits, 1);
    CHECK_EQ(actor->getFinalAttribute("attack"), 100.0);
    actor->release();
    rpg->clearTraitDefinitions();
}
}  // namespace test_rpg_trait_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_status_effect_adapter)
#include "PrivateTestScope.h"
#include "rpg/Effect.h"
#include "rpg/RPGActor.h"
#include "rpg/StatusSystem.h"
namespace test_rpg_status_adapter_contract {
using namespace eve::rpg;

TEST_CASE("Legacy status ids project one canonical effect across stack replacement") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.adapter.stack";
    effect.durationPolicy = "infinite";
    effect.stackPolicy    = "stack";
    effect.maxStacks      = 2;
    effect.modifiers.push_back({"armor", "add", 4, 0});
    EffectRegistry::registerEffect(effect);
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("armor", 10);
    const int legacy = actor->applyEffect(effect.id, "source");
    REQUIRE(legacy > 0);
    REQUIRE_EQ(actor->statuses()->container.effectCount(), 1u);
    const auto* canonical = actor->statuses()->container.effectAt(0);
    REQUIRE(canonical != nullptr);
    const std::string canonicalId = canonical->id;
    CHECK_EQ(actor->statuses()->effectByLegacyId.at(legacy), canonicalId);
    CHECK_EQ(actor->getFinalAttribute("armor"), 14.0);
    const int stackedLegacy = actor->applyEffect(effect.id, "source");
    CHECK_EQ(stackedLegacy, legacy);
    CHECK_EQ(actor->statuses()->container.effectCount(), 1u);
    const auto* stacked = actor->statuses()->container.find(canonicalId);
    REQUIRE(stacked != nullptr);
    CHECK_EQ(stacked->stackCount, 2u);
    CHECK_EQ(actor->getFinalAttribute("armor"), 18.0);
    actor->release();
    EffectRegistry::remove(effect.id);
}

TEST_CASE("Rejected custom stack candidate leaves canonical state and emitted events unchanged") {
    EffectDefinition effect;
    effect.id             = "rewrite.rpg.adapter.rollback";
    effect.durationPolicy = "duration";
    effect.duration       = 4;
    effect.stackPolicy    = "rewriteReject";
    effect.modifiers.push_back({"attack", "add", 2, 0});
    EffectRegistry::registerEffect(effect);
    StatusSystem::registerStackPolicy(
        "rewriteReject", [](RPGActor*, StatusInstance& candidate, const EffectDefinition&, const std::string&) {
            candidate.stacks += 1;
            candidate.remaining += 10;
            return -1;
        });
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 5);
    REQUIRE(actor->applyEffect(effect.id) > 0);
    const int events        = actor->statuses()->container.eventCount();
    const int rejectedStack = actor->applyEffect(effect.id);
    CHECK_EQ(rejectedStack, -1);
    CHECK_EQ(actor->getStatusStacks(0), 1);
    CHECK_EQ(actor->getStatusRemaining(0), 4.f);
    CHECK_EQ(actor->getFinalAttribute("attack"), 7.0);
    CHECK_EQ(actor->statuses()->container.eventCount(), events);
    actor->release();
    StatusSystem::unregisterStackPolicy("rewriteReject");
    EffectRegistry::remove(effect.id);
}
}  // namespace test_rpg_status_adapter_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_dialect)
#include "PrivateTestScope.h"
#include "rpg/GameState.h"
#include "rpg/Party.h"
#include "rpg/RPGActor.h"
#include "rpg/RpgDialect.h"
namespace test_rpg_dialect_contract {
using namespace eve::rpg;

TEST_CASE("Dnut story parses a domain step then persists nonrepeatable completion") {
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story rewrite_once {
    flag name=gate.open state=true
    variable name=gold op=add value=25
    message text=Done
}
)",
                                                     "rewrite.dnut")
                .ok());
    GameState       state;
    RpgStoryBinding binding;
    binding.gameState = &state;
    RpgStorySession session;
    REQUIRE(session.begin("rewrite_once", &binding).ok());
    CHECK(state.isSwitchOn("gate.open"));
    CHECK_EQ(state.getVariable("gold"), 25.0);
    CHECK(session.isBlocked());
    CHECK_EQ(session.getStepKind(), std::string("message"));
    REQUIRE(session.advance().ok());
    CHECK(!session.isActive());
    CHECK(state.hasSelfVariable("story.rewrite_once", "completed"));
    CHECK(!session.begin("rewrite_once", &binding).ok());
    RpgStoryCatalogue::clear();
}

TEST_CASE("Dnut strict replacement rejects invalid step payload without dropping published story") {
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict("story good {\n    message text=ok\n}\n", "good.dnut").ok());
    CHECK(!RpgStoryCatalogue::replaceFromDnutStrict("story bad {\n    teleport actor=hero\n}\n", "bad.dnut").ok());
    CHECK(RpgStoryCatalogue::contains("good"));
    CHECK(!RpgStoryCatalogue::contains("bad"));
    RpgStoryCatalogue::clear();
}

TEST_CASE("Dnut repeatable branch reads current game state on each run") {
    RpgStoryCatalogue::clear();
    REQUIRE(RpgStoryCatalogue::replaceFromDnutStrict(R"(
story rewrite_branch repeatable {
    if switch.gate == true {
        variable name=path op=set value=1
    } else {
        variable name=path op=set value=2
    }
}
)",
                                                     "branch.dnut")
                .ok());
    GameState       state;
    RpgStoryBinding binding;
    binding.gameState = &state;
    RpgStorySession session;
    REQUIRE(session.begin("rewrite_branch", &binding).ok());
    CHECK_EQ(state.getVariable("path"), 2.0);
    state.switchOn("gate");
    REQUIRE(session.begin("rewrite_branch", &binding).ok());
    CHECK_EQ(state.getVariable("path"), 1.0);
    RpgStoryCatalogue::clear();
}
}  // namespace test_rpg_dialect_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_battle_tactics)
#include <vector>
#include "PrivateTestScope.h"
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
namespace test_rpg_battle_tactics_contract {
using namespace eve::rpg;
namespace {
RPGActor* combatant(double attack, double hp) {
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", 0);
    actor->setBaseAttribute("hp", hp);
    actor->setCurrent("hp", hp);
    return actor;
}
BattleVictoryRequest victoryRequest() {
    BattleVictoryRequest request;
    request.mapId                 = "village";
    request.objectId              = "slime_west";
    request.requiredQuestId       = "rewrite.rpg.victory.quest";
    request.xpAmount              = 30;
    request.xpGrowth              = 1.2;
    request.attributeId           = "gold";
    request.attributeAmount       = 10;
    request.notifyTopic           = "kill";
    request.notifyTarget          = "slime";
    request.notifyAmount          = 1;
    request.defeatCounterId       = "slime_defeats";
    request.defeatCounterAmount   = 1;
    request.levelPointAttributeId = "skill_points";
    request.pointsPerLevel        = 1;
    return request;
}
}  // namespace

TEST_CASE("Tactics chooses injured ally for healing and rejects incompatible replacement") {
    SkillDefinition aid;
    aid.id         = "rewrite.rpg.tactics.aid";
    aid.targetType = "allySingle";
    SkillRegistry::registerSkill(aid);
    SkillDamageSpec healing;
    healing.damageType = "hpHeal";
    healing.formula    = "20";
    BattleSystem::registerSkillDamage(aid.id, healing);
    BattleTacticsCatalogue::clear();
    REQUIRE(
        BattleTacticsCatalogue::replaceFromJsonStrict(
            R"([{"id":"rewrite.rpg.tactics.healer","rules":[{"skillId":"rewrite.rpg.tactics.aid","targetPolicy":"lowestHealthAlly","conditionResource":"hp","belowRatio":0.4},{"skillId":"rewrite.rpg.tactics.aid","targetPolicy":"lowestHealthAlly","conditionResource":"","belowRatio":1.0}]}])")
            .ok());
    CHECK(
        !BattleTacticsCatalogue::replaceFromJsonStrict(
             R"([{"id":"bad","rules":[{"skillId":"rewrite.rpg.tactics.aid","targetPolicy":"lowestHealthEnemy","conditionResource":"","belowRatio":1}]}])")
             .ok());
    CHECK_EQ(BattleTacticsCatalogue::count(), 1);
    auto* hero   = combatant(20, 100);
    auto* healer = combatant(10, 80);
    auto* enemy  = combatant(5, 100);
    hero->setCurrent("hp", 25);
    healer->learnSkill(aid.id);
    Battle battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(healer, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    const auto queued = BattleTacticsCatalogue::queueAction(&battle, healer, "rewrite.rpg.tactics.healer");
    REQUIRE(queued.ok());
    CHECK_EQ(queued.value(), aid.id);
    battle.startRound();
    REQUIRE(battle.executeNextAction());
    CHECK_EQ(hero->getCurrent("hp"), 45.0);
    hero->release();
    healer->release();
    enemy->release();
    BattleTacticsCatalogue::clear();
    SkillRegistry::remove(aid.id);
    BattleSystem::clearSkillDamage();
}

TEST_CASE("Victory settlement commits progression quest reward and encounter consumption together") {
    QuestRegistry::clear();
    QuestDefinition quest;
    quest.id             = "rewrite.rpg.victory.quest";
    quest.startPolicy    = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"kill", "kill", "slime", 1});
    QuestRegistry::registerQuest(quest);
    Tracker tracker;
    REQUIRE(tracker.activate(quest.id));
    GameState  state;
    WorldState world(state);
    auto*      actor = RPGActor::createActor();
    REQUIRE(actor->restoreProgression(1, 80, 100).ok());
    const auto settled = BattleVictory::settle(*actor, state, tracker, victoryRequest());
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value().levelsGained, 1);
    CHECK_EQ(actor->getLevel(), 2);
    CHECK_EQ(state.getVariable("gold"), 10.0);
    CHECK_EQ(tracker.getState(quest.id), std::string("ready"));
    CHECK(world.isObjectConsumed("village", "slime_west"));
    actor->release();
    QuestRegistry::clear();
}

TEST_CASE("Duplicate encounter rejects victory before mutating any owner") {
    QuestRegistry::clear();
    QuestDefinition quest;
    quest.id          = "rewrite.rpg.victory.quest";
    quest.startPolicy = "manual";
    quest.objectives.push_back({"kill", "kill", "slime", 1});
    QuestRegistry::registerQuest(quest);
    Tracker tracker;
    REQUIRE(tracker.activate(quest.id));
    GameState  state;
    WorldState world(state);
    REQUIRE(world.consumeObject("village", "slime_west").ok());
    auto* actor = RPGActor::createActor();
    REQUIRE(actor->restoreProgression(1, 80, 100).ok());
    CHECK(!BattleVictory::settle(*actor, state, tracker, victoryRequest()).ok());
    CHECK_EQ(actor->getLevel(), 1);
    CHECK_EQ(actor->getXp(), 80.0);
    CHECK_EQ(state.getVariable("gold"), 0.0);
    CHECK_EQ(tracker.getState(quest.id), std::string("active"));
    actor->release();
    QuestRegistry::clear();
}
}  // namespace test_rpg_battle_tactics_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_rpg) ||     \
    defined(EVE_TEST_ORIGIN_rpg_encounter_catalogue) || defined(EVE_TEST_ORIGIN_rpg_party) || \
    defined(EVE_TEST_ORIGIN_rpg_state_canonical)
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "common/GameplayControl.h"
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
namespace test_rpg_composition_contract {
using namespace eve::rpg;
namespace {
eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}
RPGActor* fighter(double attack, double hp) {
    auto* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", attack);
    actor->setBaseAttribute("defense", 0);
    actor->setBaseAttribute("hp", hp);
    actor->setBaseAttribute("speed", 10);
    actor->setCurrent("hp", hp);
    return actor;
}
}  // namespace

TEST_CASE("Battle control checks revision before queue mutation and executes an authorized attack") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            hero  = fighter(50, 100);
    auto*            enemy = fighter(1, 20);
    Battle           battle;
    battle.addActor(hero, BattleSide::Party);
    battle.addActor(enemy, BattleSide::Enemies);
    const auto    arena   = subject("00000000-0000-7000-8000-000000000701");
    const auto    heroId  = subject("00000000-0000-7000-8000-000000000702");
    const auto    enemyId = subject("00000000-0000-7000-8000-000000000703");
    BattleControl control(battle, arena);
    REQUIRE(control.bindParticipant(heroId, hero).ok());
    REQUIRE(control.bindParticipant(enemyId, enemy).ok());
    eve::GameplaySession player{"player", eve::GameplayAccess::PlayerEquivalent, {heroId}};
    const auto           observed = control.observeGameplay(player, arena);
    REQUIRE(observed.ok());
    eve::GameplayCommand command;
    command.id        = "rewrite-rpg-attack";
    const auto action = eve::LogicalId::parse("rpg:attack");
    REQUIRE(action.has_value());
    command.action           = *action;
    command.subject          = heroId;
    command.observedTick     = observed.value().tick;
    command.expectedRevision = observed.value().revision;
    command.parameters       = eve::Value(eve::Value::Object{{"target", eve::Value(enemyId.format())}});
    {
        command.expectedRevision = 0;
        const auto rejected      = control.submitGameplay(player, arena, command);
        CHECK(!rejected.ok());
        CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
        CHECK_EQ(enemy->getCurrent("hp"), 20.0);
    };
    {
        command.expectedRevision = observed.value().revision;
        REQUIRE(control.submitGameplay(player, arena, command).ok());
        REQUIRE(
            control.advanceGameplay(player, arena, {eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)}).ok());
        CHECK(battle.isVictory());
    };
    hero->release();
    enemy->release();
}

TEST_CASE("Encounter catalogue validates dependencies before publishing actors") {
    SkillDefinition skill;
    skill.id = "rewrite.rpg.encounter.claw";
    SkillRegistry::registerSkill(skill);
    QuestDefinition quest;
    quest.id = "rewrite.rpg.encounter.quest";
    QuestRegistry::registerQuest(quest);
    EncounterCatalogue::clear();
    const std::string valid =
        R"([{"id":"rewrite.rpg.encounter.slime","displayName":"Slime","members":[{"id":"slime","displayName":"Slime","skillId":"rewrite.rpg.encounter.claw","attack":12,"defense":2.8,"maxHp":42,"speed":5}],"xpReward":40,"xpGrowth":1.2,"goldReward":10,"requiredQuestId":"rewrite.rpg.encounter.quest","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":3}])";
    REQUIRE(EncounterCatalogue::replaceFromJsonStrict(valid).ok());
    auto* actor = EncounterCatalogue::createActor("rewrite.rpg.encounter.slime");
    REQUIRE(actor != nullptr);
    CHECK_EQ(actor->getFinalAttribute("attack"), 12.0);
    CHECK_EQ(actor->getCurrent("hp"), 42.0);
    CHECK(actor->knowsSkill(skill.id));
    actor->release();
    const std::string invalid =
        R"([{"id":"bad","displayName":"Bad","members":[{"id":"x","displayName":"X","skillId":"missing","attack":1,"defense":0,"maxHp":1,"speed":0}],"xpReward":1,"xpGrowth":1.2,"goldReward":0,"requiredQuestId":"rewrite.rpg.encounter.quest","notifyTopic":"kill","notifyTarget":"slime","notifyAmount":1,"defeatCounterId":"kills","defeatCounterAmount":1,"levelPointAttributeId":"statPoints","pointsPerLevel":0}])";
    CHECK(!EncounterCatalogue::replaceFromJsonStrict(invalid).ok());
    CHECK(EncounterCatalogue::find("rewrite.rpg.encounter.slime") != nullptr);
    EncounterCatalogue::clear();
    SkillRegistry::remove(skill.id);
    QuestRegistry::remove(quest.id);
}

TEST_CASE("Party preserves roster order and refuses duplicate identity or stale battle composition") {
    Party party;
    auto* hero      = fighter(10, 100);
    auto* companion = fighter(5, 100);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    CHECK_EQ(party.getMemberId(0), std::string("hero"));
    CHECK(!party.addMember("hero", companion).ok());
    CHECK(!party.addMember("alias", hero).ok());
    CHECK_EQ(party.count(), 2);
    companion->release();
    CHECK(party.hasStaleMembers());
    Battle battle;
    CHECK(!party.addToBattle(&battle, BattleSide::Party).ok());
    CHECK_EQ(battle.getActorCount(), 0);
    party.clear();
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addToBattle(&battle, BattleSide::Party).ok());
    CHECK_EQ(battle.getActorCount(), 1);
    hero->release();
}

TEST_CASE("Party checkpoint validates full roster before mutating any actor") {
    Party party;
    auto* hero      = fighter(20, 100);
    auto* companion = fighter(12, 100);
    REQUIRE(party.addMember("hero", hero).ok());
    REQUIRE(party.addMember("companion", companion).ok());
    const auto encoded = party.checkpointJson();
    REQUIRE(encoded.ok());
    hero->setBaseAttribute("attack", 1);
    companion->setBaseAttribute("attack", 2);
    REQUIRE(party.restoreCheckpointJson(encoded.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 20.0);
    CHECK_EQ(companion->getBaseAttribute("attack"), 12.0);
    REQUIRE(party.removeMember("companion").ok());
    hero->setBaseAttribute("attack", 7);
    CHECK(!party.restoreCheckpointJson(encoded.value()).ok());
    CHECK_EQ(hero->getBaseAttribute("attack"), 7.0);
    hero->release();
    companion->release();
}

TEST_CASE("Canonical RPG restore rejects malformed later actor without applying earlier actor changes") {
    SkillDefinition skill;
    skill.id = "rewrite.rpg.canonical.fire";
    SkillRegistry::registerSkill(skill);
    auto* first  = RPGActor::createActor();
    auto* second = RPGActor::createActor();
    first->learnSkill(skill.id);
    first->setSkillCooldown(skill.id, 3);
    const auto captured = captureRpgState();
    REQUIRE(captured.ok());
    auto state = captured.value();
    state.at(0).find("skills")->set(skill.id, eve::Value::number(9));
    state.at(1) = eve::Value::integer(42);
    CHECK(!restoreRpgState(state).ok());
    CHECK_EQ(SkillSystem::getCooldownRemaining(first, skill.id), 3.f);
    second->release();
    first->release();
    SkillRegistry::remove(skill.id);
}
}  // namespace test_rpg_composition_contract
#endif
