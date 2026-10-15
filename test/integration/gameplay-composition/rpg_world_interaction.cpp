#include <string>
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/Tracker.h"
#include "rpg/WorldInteraction.h"
#include "rpg/WorldState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// rpg_world_interaction.cpp
namespace {

void installWorldLootFixture() {
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::inventory::ItemDefinition item;
    item.id       = "cache_item";
    item.maxStack = 10;
    eve::inventory::ItemRegistry::registerItem(item);
    eve::rpg::QuestDefinition quest;
    quest.id             = "quest.cache";
    quest.startPolicy    = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"recover", "interact", "cache", 1});
    eve::rpg::QuestRegistry::registerQuest(quest);
}

eve::rpg::WorldLootRequest cacheRequest() {
    eve::rpg::WorldLootRequest request;
    request.mapId           = "forest";
    request.objectId        = "cache";
    request.requiredQuestId = "quest.cache";
    request.itemId          = "cache_item";
    request.itemQuantity    = 1;
    request.attributeId     = "gold";
    request.attributeAmount = 7.0;
    request.notifyTopic     = "interact";
    request.notifyTarget    = "cache";
    request.notifyAmount    = 1;
    return request;
}

void clearWorldLootFixture() {
    eve::inventory::InventorySystem::unregisterChangeHook("test.world-loot");
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
}

}  // namespace

TEST_CASE("rpg.worldInteraction.commitsWorldQuestStateAndInventoryBeforePublishing") {
    installWorldLootFixture();
    eve::rpg::Tracker tracker;
    REQUIRE(tracker.activate("quest.cache"));
    eve::rpg::GameState  state;
    eve::rpg::WorldState world(state);
    eve::inventory::Bag  bag(2);
    eve::inventory::InventorySystem::clearEvents();
    int  hookCalls         = 0;
    bool hookSawFinalState = false;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.world-loot", [&](const eve::inventory::InventoryChangeEvent &) {
            ++hookCalls;
            hookSawFinalState = world.isObjectConsumed("forest", "cache") &&
                                tracker.getState("quest.cache") == "ready" && state.getVariable("gold") == 7.0 &&
                                bag.countItem("cache_item") == 1;
        });

    auto collected = eve::rpg::WorldInteraction::collectLoot(state, tracker, bag, cacheRequest());
    REQUIRE(collected.ok());
    CHECK_EQ(std::move(collected).takeValue(), 4);
    CHECK(world.isObjectConsumed("forest", "cache"));
    CHECK_EQ(tracker.getState("quest.cache"), std::string("ready"));
    CHECK_EQ(state.getVariable("gold"), 7.0);
    CHECK_EQ(bag.countItem("cache_item"), 1);
    CHECK_EQ(hookCalls, 1);
    CHECK(hookSawFinalState);
    clearWorldLootFixture();
}

TEST_CASE("rpg.worldInteraction.inventoryFailureLeavesEveryOwnerUntouched") {
    installWorldLootFixture();
    eve::rpg::Tracker tracker;
    REQUIRE(tracker.activate("quest.cache"));
    eve::rpg::GameState  state;
    eve::rpg::WorldState world(state);
    eve::inventory::Bag  bag(0);
    eve::inventory::InventorySystem::clearEvents();
    const int objectiveBefore = tracker.getObjectiveCurrent("quest.cache", 0);
    int       hookCalls       = 0;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.world-loot", [&](const eve::inventory::InventoryChangeEvent &) { ++hookCalls; });

    auto collected = eve::rpg::WorldInteraction::collectLoot(state, tracker, bag, cacheRequest());
    REQUIRE(!collected.ok());
    CHECK(!world.isObjectConsumed("forest", "cache"));
    CHECK_EQ(tracker.getState("quest.cache"), std::string("active"));
    CHECK_EQ(tracker.getObjectiveCurrent("quest.cache", 0), objectiveBefore);
    CHECK_EQ(state.getVariable("gold"), 0.0);
    CHECK_EQ(bag.countItem("cache_item"), 0);
    CHECK_EQ(hookCalls, 0);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearWorldLootFixture();
}

TEST_CASE("rpg.worldInteraction.rejectsAlreadyConsumedObjectWithoutDuplicateRewards") {
    installWorldLootFixture();
    eve::rpg::Tracker tracker;
    REQUIRE(tracker.activate("quest.cache"));
    eve::rpg::GameState  state;
    eve::rpg::WorldState world(state);
    eve::inventory::Bag  bag(2);
    auto                 consumed = world.consumeObject("forest", "cache");
    REQUIRE(consumed.ok());
    eve::inventory::InventorySystem::clearEvents();

    auto collected = eve::rpg::WorldInteraction::collectLoot(state, tracker, bag, cacheRequest());
    REQUIRE(!collected.ok());
    CHECK_EQ(tracker.getState("quest.cache"), std::string("active"));
    CHECK_EQ(state.getVariable("gold"), 0.0);
    CHECK_EQ(bag.countItem("cache_item"), 0);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearWorldLootFixture();
}

// rpg_world_state.cpp
TEST_CASE("rpg.worldState.isPersistentIdempotentAndGameStateOwned") {
    eve::rpg::GameState  gameState;
    eve::rpg::WorldState world(gameState);
    CHECK(!world.isObjectConsumed("village", "old_chest"));

    auto consumed = world.consumeObject("village", "old_chest");
    REQUIRE(consumed.ok());
    CHECK_EQ(consumed.code(), eve::StatusCode::Applied);
    CHECK(world.isObjectConsumed("village", "old_chest"));
    auto repeated = world.consumeObject("village", "old_chest");
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.code(), eve::StatusCode::NoOp);

    auto snapshot = gameState.snapshotJson();
    REQUIRE(snapshot.ok());
    eve::rpg::GameState restored;
    auto                restoredResult = restored.restoreSnapshotJson(snapshot.value());
    REQUIRE(restoredResult.ok());
    eve::rpg::WorldState restoredWorld(restored);
    CHECK(restoredWorld.isObjectConsumed("village", "old_chest"));

    auto reset = restoredWorld.resetObject("village", "old_chest");
    REQUIRE(reset.ok());
    CHECK_EQ(reset.code(), eve::StatusCode::Applied);
    CHECK(!restoredWorld.isObjectConsumed("village", "old_chest"));
}

TEST_CASE("rpg.worldState.rejectsInvalidStableIdsWithoutMutation") {
    eve::rpg::GameState  gameState;
    eve::rpg::WorldState world(gameState);
    auto                 emptyMap = world.consumeObject("", "chest");
    CHECK(!emptyMap.ok());
    auto controlObject = world.consumeObject("village", std::string("bad\nobject"));
    CHECK(!controlObject.ok());
    CHECK(!world.isObjectConsumed("village", "chest"));
    CHECK(!gameState.hasSelfVariable("world.object:village", "bad\nobject"));
}
