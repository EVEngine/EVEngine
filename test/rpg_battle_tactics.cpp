#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/BattleTactics.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"

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
    strike.id = "strike";
    strike.targetType = "enemySingle";
    SkillRegistry::registerSkill(strike);
    SkillDefinition aid;
    aid.id = "aid";
    aid.targetType = "allySingle";
    SkillRegistry::registerSkill(aid);
    SkillDamageSpec aidSpec;
    aidSpec.damageType = "hpHeal";
    aidSpec.formula = "20";
    BattleSystem::registerSkillDamage("aid", aidSpec);

    const std::string valid = R"([
      {"id":"ranger","rules":[
        {"skillId":"aid","targetPolicy":"lowestHealthAlly","conditionResource":"hp","belowRatio":0.4},
        {"skillId":"strike","targetPolicy":"lowestHealthEnemy","conditionResource":"","belowRatio":1.0}
      ]}
    ])";
    REQUIRE(BattleTacticsCatalogue::replaceFromJsonStrict(valid).ok());
    CHECK_EQ(BattleTacticsCatalogue::count(), 1);
    CHECK(!BattleTacticsCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","rules":[{"skillId":"aid","targetPolicy":"lowestHealthEnemy","conditionResource":"","belowRatio":1}]}])")
               .ok());
    CHECK_EQ(BattleTacticsCatalogue::count(), 1);

    auto *hero = makeTacticsActor(20.0, 100.0);
    auto *ranger = makeTacticsActor(10.0, 80.0);
    auto *enemy = makeTacticsActor(5.0, 100.0);
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
