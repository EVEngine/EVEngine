#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/QuestReward.h"
#include "rpg/Tracker.h"

#include <string>

namespace {

void installRewardFixture() {
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::inventory::ItemDefinition potion;
    potion.id = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);

    eve::rpg::QuestDefinition quest;
    quest.id = "quest.reward";
    quest.startPolicy = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"win", "win", "encounter", 1});
    quest.rewards.push_back({"item", "potion", 2.0});
    quest.rewards.push_back({"attribute", "gold", 5.0});
    eve::rpg::QuestRegistry::registerQuest(quest);
}

void makeReady(eve::rpg::Tracker &tracker) {
    REQUIRE(tracker.activate("quest.reward"));
    tracker.notify("win", "encounter", 1);
    REQUIRE_EQ(tracker.getState("quest.reward"), std::string("ready"));
}

void clearRewardFixture() {
    eve::inventory::InventorySystem::unregisterChangeHook("test.quest-reward");
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
}

}  // namespace

TEST_CASE("rpg.questReward.commitsAllOwnersBeforePublishingInventoryEvents") {
    installRewardFixture();
    eve::rpg::Tracker tracker;
    makeReady(tracker);
    eve::rpg::GameState state;
    state.setVariable("gold", 10.0);
    eve::inventory::Bag bag(2);
    bag.setId("hero");
    eve::inventory::InventorySystem::clearEvents();

    int hookCalls = 0;
    bool hookSawFinalState = false;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.quest-reward", [&](const eve::inventory::InventoryChangeEvent &) {
            ++hookCalls;
            hookSawFinalState = tracker.getState("quest.reward") == "completed" &&
                                state.getVariable("gold") == 15.0 && bag.countItem("potion") == 2;
        });

    auto claimed = eve::rpg::QuestReward::claim(tracker, state, bag, "quest.reward");
    REQUIRE(claimed.ok());
    CHECK_EQ(std::move(claimed).takeValue(), 2);
    CHECK_EQ(tracker.getState("quest.reward"), std::string("completed"));
    CHECK_EQ(state.getVariable("gold"), 15.0);
    CHECK_EQ(bag.countItem("potion"), 2);
    CHECK_EQ(hookCalls, 1);
    CHECK(hookSawFinalState);
    CHECK_EQ(eve::inventory::InventorySystem::events().size(), std::size_t(1));
    clearRewardFixture();
}

TEST_CASE("rpg.questReward.capacityFailureLeavesEveryOwnerAndObserverUntouched") {
    installRewardFixture();
    eve::rpg::Tracker tracker;
    makeReady(tracker);
    eve::rpg::GameState state;
    state.setVariable("gold", 10.0);
    eve::inventory::Bag bag(0);
    eve::inventory::InventorySystem::clearEvents();
    const auto pendingBefore = tracker.pending;
    int hookCalls = 0;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.quest-reward", [&](const eve::inventory::InventoryChangeEvent &) { ++hookCalls; });

    auto claimed = eve::rpg::QuestReward::claim(tracker, state, bag, "quest.reward");
    REQUIRE(!claimed.ok());
    CHECK_EQ(tracker.getState("quest.reward"), std::string("ready"));
    CHECK_EQ(state.getVariable("gold"), 10.0);
    CHECK_EQ(bag.countItem("potion"), 0);
    CHECK_EQ(tracker.pending.size(), pendingBefore.size());
    CHECK_EQ(hookCalls, 0);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearRewardFixture();
}

TEST_CASE("inventory.addBatch.rejectsStalePreparedStateWithoutPublishing") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition potion;
    potion.id = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
    eve::inventory::Bag bag(2);
    eve::inventory::InventorySystem::clearEvents();

    auto prepared = eve::inventory::InventorySystem::prepareAddBatch(
        &bag, std::vector<eve::inventory::InventoryItemGrant>{{"potion", 2}});
    REQUIRE(prepared.ok());
    const int addedOutsideBatch = bag.addItem("potion", 1);
    REQUIRE_EQ(addedOutsideBatch, 1);
    eve::inventory::InventorySystem::clearEvents();
    auto committed = eve::inventory::InventorySystem::commitAddBatch(std::move(prepared).takeValue());
    REQUIRE(!committed.ok());
    CHECK_EQ(bag.countItem("potion"), 1);
    CHECK(eve::inventory::InventorySystem::events().empty());
    eve::inventory::ItemRegistry::clear();
}
