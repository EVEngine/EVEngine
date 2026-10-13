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
