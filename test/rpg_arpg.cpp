#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "rpg/Combat.h"
#include "rpg/Loot.h"
#include "rpg/Progression.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/StateCodec.h"
#include "rpg/Tracker.h"
#include "rpg/UtilityAI.h"

#include <cmath>

using namespace eve::rpg;

TEST_CASE("rpg.quest.chainProgressRewardsAndEvents") {
    QuestRegistry::clear();
    CHECK_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"hunt","startPolicy":"auto","completePolicy":"auto",
       "objectives":[{"id":"wolves","topic":"kill","target":"wolf","count":2}]},
      {"id":"return","requires":["hunt"],"startPolicy":"manual",
       "objectives":[{"id":"talk","topic":"talk","target":"elder"}],
       "rewards":[{"type":"item","id":"sword.iron","amount":1}]}
    ])"),
             2);
    Tracker tracker;
    CHECK_EQ(tracker.getState("hunt"), "active");
    CHECK_EQ(tracker.getState("return"), "locked");
    tracker.notify("kill", "wolf", 2);
    CHECK_EQ(tracker.getState("hunt"), "completed");
    CHECK_EQ(tracker.getState("return"), "inactive");
    CHECK(tracker.activate("return"));
    tracker.notify("talk", "elder", 1);
    CHECK_EQ(tracker.getState("return"), "ready");
    CHECK_EQ(tracker.getRewardId("return", 0), "sword.iron");
    CHECK(tracker.claim("return"));
    tracker.pollEvents();
    CHECK(tracker.getEventCount() > 0);
    QuestRegistry::clear();
}

TEST_CASE("rpg.progression.multilevelAndPoints") {
    RPGActor* actor = RPGActor::createActor();
    actor->configureProgression(1, 10, 100.f, 1.f, 2);
    const int levelsGained = actor->gainExperience(250.f);
    CHECK_EQ(levelsGained, 2);
    CHECK_EQ(actor->getLevel(), 3);
    CHECK(std::fabs(actor->getExperience() - 50.f) < 0.001f);
    CHECK_EQ(actor->getUnspentPoints(), 4);
    CHECK(actor->spendPoints(3));
    CHECK_EQ(actor->getUnspentPoints(), 1);
    actor->release();
}

TEST_CASE("rpg.combat.armorResistanceCritAndKill") {
    RPGActor* attacker = RPGActor::createActor();
    RPGActor* target   = RPGActor::createActor();
    attacker->setBaseAttribute("attack", 20.0);
    attacker->setBaseAttribute("critChance", 1.0);
    attacker->setBaseAttribute("critMultiplier", 2.0);
    target->setBaseAttribute("health", 100.0);
    target->setBaseAttribute("armor", 100.0);
    target->setBaseAttribute("resist.fire", 0.5);
    double dealt = CombatSystem::dealDamage(attacker, target, 20.0, "fire");
    CHECK(std::fabs(dealt - 20.0) < 0.001);
    CHECK(std::fabs(target->getFinalAttribute("health") - 80.0) < 0.001);
    CHECK(CombatSystem::dealDamage(attacker, target, 1000.0, "fire") > 0.0);
    std::vector<CombatEvent> events;
    CombatSystem::pollEvents(events);
    CHECK_EQ(events.size(), size_t(2));
    CHECK(events.back().killed);
    attacker->release();
    target->release();
}

TEST_CASE("rpg.loot.weightedDeterministicRoll") {
    LootRegistry::clear();
    CHECK_EQ(LootRegistry::loadFromJson(R"({
      "id":"wolf","rolls":2,"unique":true,"entries":[
        {"itemId":"gold","minQuantity":2,"maxQuantity":2,"weight":1,"chance":1},
        {"itemId":"pelt","minQuantity":1,"maxQuantity":1,"weight":1,"chance":1}
      ]})"),
             1);
    LootSystem::setRandomSeed(7);
    LootRoll* roll = LootSystem::roll("wolf");
    CHECK_EQ(roll->getCount(), 2);
    CHECK(roll->getItemId(0) != roll->getItemId(1));
    roll->destroy();
    LootRegistry::clear();
}

TEST_CASE("rpg.ai.utilityChoosesHighestResponse") {
    AIDecisionRegistry::clear();
    CHECK_EQ(AIDecisionRegistry::loadFromJson(R"({"id":"melee","actions":[
      {"id":"attack","considerations":[{"input":"range","curve":"inverse","min":0,"max":10}]},
      {"id":"flee","considerations":[{"input":"danger","curve":"linear","min":0,"max":1}]}
    ]})"),
             1);
    UtilityBrain brain;
    brain.setDecisionSet("melee");
    brain.setInput("range", 1.f);
    brain.setInput("danger", 0.2f);
    CHECK_EQ(brain.chooseAction(), "attack");
    brain.setInput("range", 10.f);
    brain.setInput("danger", 1.f);
    CHECK_EQ(brain.chooseAction(), "flee");
    AIDecisionRegistry::clear();
}

TEST_CASE("rpg.save.actorAndQuestRoundTrip") {
    QuestRegistry::clear();
    QuestRegistry::loadFromJson(
        R"({"id":"q","startPolicy":"auto","objectives":[{"id":"k","topic":"kill","count":3}]})");
    RPGActor* source = RPGActor::createActor();
    source->setBaseAttribute("health", 42.0);
    source->configureProgression(3, 20, 100.f, 1.2f, 2);
    source->gainExperience(20.f);
    std::string actorJson = StateCodec::serializeActor(source);
    RPGActor*   restored  = RPGActor::createActor();
    CHECK(StateCodec::deserializeActor(restored, actorJson));
    CHECK_EQ(restored->getBaseAttribute("health"), 42.0);
    CHECK_EQ(restored->getLevel(), 3);

    Tracker tracker;
    tracker.notify("kill", "wolf", 2);
    std::string trackerJson = StateCodec::serializeTracker(&tracker);
    Tracker     restoredTracker;
    CHECK(StateCodec::deserializeTracker(&restoredTracker, trackerJson));
    CHECK_EQ(restoredTracker.getObjectiveCurrent("q", 0), 2);
    source->release();
    restored->release();
    QuestRegistry::clear();
}

TEST_CASE("rpg.equipment.syncsItemAttributes") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition sword;
    sword.id                   = "sword";
    sword.equipSlot            = "weapon";
    sword.extra["attr.attack"] = "12.5";
    eve::inventory::ItemRegistry::registerItem(sword);
    eve::inventory::Bag bag(1);
    CHECK_EQ(bag.addItem("sword", 1), 1);
    eve::inventory::EquipmentSet equipment;
    equipment.setId("hero");
    equipment.defineSlot("weapon");
    CHECK(equipment.equipFromBag("weapon", &bag, 0));
    RPGActor* actor = RPGActor::createActor();
    actor->setBaseAttribute("attack", 10.0);
    CHECK_EQ(equipment.synchronizeAttributes(actor), 1);
    CHECK(std::fabs(actor->getFinalAttribute("attack") - 22.5) < 0.001);
    actor->release();
    eve::inventory::ItemRegistry::clear();
}
