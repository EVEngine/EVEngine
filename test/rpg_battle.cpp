#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/RPGActor.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/RPG.h"

#include <cmath>

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

TEST_CASE("rpg.battle.resolveHitCritAndElement") {
    auto *rpg = RPG::create();
    rpg->clearTraitDefinitions();
    int registered = rpg->registerTraitsFromJson(
        R"([{"id":"crit","traits":[{"kind":"exParam","target":"critRate","value":1.0}]}])");
    CHECK_EQ(registered, 1);

    RPGActor *a = makeFighter(50, 0, 100, 5);
    RPGActor *b = makeFighter(0, 5, 100, 1);
    int tid = a->applyTrait("crit", "test");
    CHECK(tid > 0);

    // 必暴击（critRate=1.0）
    SkillDamageSpec spec;
    spec.formula = "a.attack - b.defense";
    spec.element = "";
    DamageResult r = BattleSystem::resolveHit(a, b, spec, 42);
    CHECK(r.hit);
    CHECK(r.crit);
    CHECK(approxEq(r.amount, (50 - 5) * 3.0));

    // 元素减伤：火耐性 0.5（用不带暴击特征的攻击方）
    RPGActor *a2 = makeFighter(50, 0, 100, 5);
    RPGActor *c = makeFighter(0, 0, 100, 1);
    rpg->registerTraitsFromJson(R"([{"id":"fire_resist2","traits":[{"kind":"elementRate","target":"fire","value":0.5}]}])");
    c->applyTrait("fire_resist2", "x");
    SkillDamageSpec fireSpec;
    fireSpec.formula = "a.attack";
    fireSpec.element = "fire";
    DamageResult fr = BattleSystem::resolveHit(a2, c, fireSpec, 7);
    CHECK(fr.hit);
    CHECK(approxEq(fr.elementRate, 0.5));
    CHECK(approxEq(fr.amount, 50.0 * 0.5));  // 未暴击

    rpg->clearTraitDefinitions();
    a->release();
    b->release();
    a2->release();
    c->release();
}

TEST_CASE("rpg.battle.roundVictory") {
    RPGActor *hero = makeFighter(30, 0, 100, 10);
    RPGActor *slime = makeFighter(5, 0, 20, 1);
    slime->setCurrent("hp", 20);

    Battle b;
    b.addActor(hero, BattleSide::Party);
    b.addActor(slime, BattleSide::Enemies);
    b.setAction(hero, "", nullptr);  // 普攻
    b.autoEnemyActions();
    b.startRound();
    while (b.executeNextAction() && !b.isFinished()) {}
    CHECK(b.isFinished());
    CHECK(b.isVictory());
    CHECK(!b.isActorAlive(slime));
    // 普攻 30 - 0 = 30 ≥ 20 → slime 死亡
    CHECK_EQ(b.getTurn(), 1);

    b.pollEvents();
    bool sawDamage = false, sawVictory = false;
    for (int i = 0; i < b.getEventCount(); ++i) {
        auto ev = b.getEvent(i);
        if (ev.action == "damage") sawDamage = true;
        if (ev.action == "victory") sawVictory = true;
    }
    CHECK(sawDamage);
    CHECK(sawVictory);

    hero->release();
    slime->release();
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
    while (b.executeNextAction() && !b.isFinished()) {}
    CHECK(b.isFinished());
    CHECK(b.isDefeat());

    hero->release();
    boss->release();
}

TEST_CASE("rpg.battle.skillDamageViaRegistry") {
    auto *rpg = RPG::create();
    rpg->clearSkillDefinitions();
    rpg->clearSkillDamage();
    int sk = rpg->registerSkillsFromJson(
        R"([{"id":"fireball","targetType":"enemySingle","castTime":0}])");
    CHECK_EQ(sk, 1);

    // 注册 fireball 的伤害：火元素，公式 a.attack*2
    BattleSystem::registerSkillDamage(
        "fireball", SkillDamageSpec{"hp", "a.attack * 2", "fire", 0.0, 100});

    RPGActor *hero = makeFighter(20, 0, 100, 10);
    RPGActor *slime = makeFighter(5, 0, 40, 1);
    slime->setCurrent("hp", 40);
    hero->learnSkill("fireball");

    Battle b;
    b.addActor(hero, BattleSide::Party);
    b.addActor(slime, BattleSide::Enemies);
    b.setAction(hero, "fireball", slime);
    b.autoEnemyActions();
    b.startRound();
    while (b.executeNextAction() && !b.isFinished()) {}
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
    BattleSystem::registerSkillDamage("mana_drain",
                                      SkillDamageSpec{"mana", "a.attack", "", 0.0, 100});

    RPGActor *caster = makeFighter(30, 0, 100, 10);
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
    while (b.executeNextAction() && !b.isFinished()) {}
    // 对 mana 造成 30 伤害，hp 不受影响
    CHECK(approxEq(victim->getCurrent("mana"), 20.0));
    CHECK(approxEq(victim->getCurrent("hp"), 100.0));

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
    while (btl.executeNextAction() && !btl.isFinished()) {}
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
    RPGActor *p = makeFighter(50, 0, 5, 2);   // 玩家侧 2
    RPGActor *e = makeFighter(50, 0, 5, 1);   // 敌侧 1

    Battle btl;
    btl.addActor(p, 2);
    btl.addActor(e, 1);
    btl.setPlayerSide(2);
    btl.setAction(p, "", e);
    btl.autoEnemyActions();
    btl.startRound();
    while (btl.executeNextAction() && !btl.isFinished()) {}
    CHECK(btl.isVictory());
    CHECK(btl.getWinnerSide() == 2);

    p->release();
    e->release();
}