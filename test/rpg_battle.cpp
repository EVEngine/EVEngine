#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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

#include <algorithm>
#include <array>
#include <cmath>

using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

RPGActor* makeFighter(double atk, double def, double hp, double speed) {
    RPGActor* a = RPGActor::createActor();
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
    RPGActor* a = makeFighter(10, 0, 100, 5);
    RPGActor* b = makeFighter(0, 2, 100, 1);
    CHECK(approxEq(BattleSystem::evaluateFormula("a.attack * 4 - b.defense * 2", a, b), 36.0));
    CHECK(approxEq(BattleSystem::evaluateFormula("a.attack * b.defense", a, b), 20.0));
    CHECK(approxEq(BattleSystem::evaluateFormula("(1 + 2) * 3", a, b), 9.0));
    CHECK(approxEq(BattleSystem::evaluateFormula("a.missing + 5", a, b), 5.0));  // 未知参数按 0
    a->release();
    b->release();
}

TEST_CASE("rpg.battle.formulaValidationRejectsMalformedDefinitionsAtomically") {
    RPGActor* a = makeFighter(10, 0, 100, 5);
    RPGActor* b = makeFighter(0, 2, 100, 1);

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
    const SkillDamageSpec* retained = BattleSystem::findSkillDamage("strike");
    REQUIRE(retained != nullptr);
    CHECK_EQ(retained->formula, std::string("a.attack / b.defense"));

    BattleSystem::clearSkillDamage();
    a->release();
    b->release();
}

TEST_CASE("rpg.battle.resolveHitCritAndElement") {
    auto* rpg = RPG::create();
    rpg->clearTraitDefinitions();
    int registered =
        rpg->registerTraitsFromJson(R"([{"id":"crit","traits":[{"kind":"exParam","target":"critRate","value":1.0}]}])");
    CHECK_EQ(registered, 1);

    RPGActor* a   = makeFighter(50, 0, 100, 5);
    RPGActor* b   = makeFighter(0, 5, 100, 1);
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
    RPGActor* a2 = makeFighter(50, 0, 100, 5);
    RPGActor* c  = makeFighter(0, 0, 100, 1);
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
    auto* rpg = RPG::create();
    rpg->clearTraitDefinitions();
    REQUIRE_EQ(rpg->registerTraitsFromJson(
                   R"([{"id":"always_crit","traits":[{"kind":"exParam","target":"critRate","value":1.0}]}])"),
               1);
    RPGActor* attacker = makeFighter(20, 0, 100, 10);
    RPGActor* target   = makeFighter(0, 0, 100, 1);
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
    RPGActor* hero  = makeFighter(30, 0, 100, 10);
    RPGActor* slime = makeFighter(5, 0, 20, 1);
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
    RPGActor* hero  = makeFighter(20, 0, 100, 10);
    RPGActor* slime = makeFighter(1, 0, 100, 1);

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

    RPGActor* hero  = makeFighter(20, 0, 100, 10);
    RPGActor* slime = makeFighter(1, 0, 100, 1);
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
    malformed.id                         = "rpg.invalid.projected-rule";
    malformed.durationPolicy             = "infinite";
    malformed.extra["settlement.rule"] = "42";
    EffectRegistry::registerEffect(malformed);

    RPGActor* hero  = makeFighter(20, 0, 100, 10);
    RPGActor* slime = makeFighter(1, 0, 100, 1);
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

    RPGActor* actor = makeFighter(1, 0, 100, 1);
    REQUIRE(actor->applyEffect(poison.id, "test:caster") > 0);
    REQUIRE(actor->applyEffect(poison.id, "test:caster") > 0);
    REQUIRE(StatusSystem::update(1.0).ok());
    std::vector<StatusTickEvent> ticks;
    StatusSystem::pollTicks(ticks);
    REQUIRE_EQ(ticks.size(), 1u);

    const auto target = subject(70);
    auto request = makeStatusTickSettlementRequest(ticks.front(), subject(69), target, eve::SimulationTick(1));
    REQUIRE(request.ok());
    CHECK_EQ(request.value().magnitude, 8.0);
    CHECK_EQ(request.value().resource, std::string("hp"));
    CHECK(std::find(request.value().tags.begin(), request.value().tags.end(), "rpg:status-tick") !=
          request.value().tags.end());

    auto malformed = poison;
    malformed.extra["settlement.tick"] =
        R"({"kind":"damage","resource":"hp","magnitude":4,"unknown":true})";
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

    RPGActor* hero  = makeFighter(20, 0, 100, 10);
    RPGActor* slime = makeFighter(1, 0, 100, 1);
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
    RPGActor* hero = makeFighter(1, 0, 10, 1);
    RPGActor* boss = makeFighter(100, 0, 100, 20);
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
    auto* rpg = RPG::create();
    rpg->clearSkillDefinitions();
    rpg->clearSkillDamage();
    int sk = rpg->registerSkillsFromJson(R"([{"id":"fireball","targetType":"enemySingle","castTime":0}])");
    CHECK_EQ(sk, 1);

    // 注册 fireball 的伤害：火元素，公式 a.attack*2
    BattleSystem::registerSkillDamage("fireball", SkillDamageSpec{"hp", "a.attack * 2", "fire", 0.0, 100});

    RPGActor* hero  = makeFighter(20, 0, 100, 10);
    RPGActor* slime = makeFighter(5, 0, 40, 1);
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
    auto* rpg = RPG::create();
    rpg->clearSkillDefinitions();
    rpg->clearSkillDamage();
    rpg->registerSkillsFromJson(R"([{"id":"mana_drain","targetType":"enemySingle","castTime":0}])");
    // 伤害到 mana（非 hp/mp 的任意资源）
    BattleSystem::registerSkillDamage("mana_drain", SkillDamageSpec{"mana", "a.attack", "", 0.0, 100});

    RPGActor* caster = makeFighter(60, 0, 100, 10);
    RPGActor* victim = makeFighter(5, 0, 100, 1);
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
    RPGActor* a = makeFighter(50, 0, 5, 1);
    RPGActor* b = makeFighter(50, 0, 5, 2);
    RPGActor* c = makeFighter(50, 0, 5, 3);

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
    RPGActor* p = makeFighter(50, 0, 5, 2);  // 玩家侧 2
    RPGActor* e = makeFighter(50, 0, 5, 1);  // 敌侧 1

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

    RPGActor* hero     = makeFighter(30, 0, 100, 10);
    RPGActor* ally     = makeFighter(5, 0, 100, 5);
    RPGActor* enemy    = makeFighter(5, 0, 20, 1);
    RPGActor* outsider = makeFighter(1, 0, 10, 1);
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

    RPGActor* hero      = makeFighter(20, 0, 100, 10);
    RPGActor* ally      = makeFighter(10, 0, 100, 8);
    RPGActor* enemyHigh = makeFighter(5, 0, 100, 2);
    RPGActor* enemyLow  = makeFighter(5, 0, 100, 1);
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
