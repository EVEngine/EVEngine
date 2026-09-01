#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/GameState.h"
#include "rpg/WorldState.h"

#include <string>

TEST_CASE("rpg.worldState.isPersistentIdempotentAndGameStateOwned") {
    eve::rpg::GameState gameState;
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
    auto restoredResult = restored.restoreSnapshotJson(snapshot.value());
    REQUIRE(restoredResult.ok());
    eve::rpg::WorldState restoredWorld(restored);
    CHECK(restoredWorld.isObjectConsumed("village", "old_chest"));

    auto reset = restoredWorld.resetObject("village", "old_chest");
    REQUIRE(reset.ok());
    CHECK_EQ(reset.code(), eve::StatusCode::Applied);
    CHECK(!restoredWorld.isObjectConsumed("village", "old_chest"));
}

TEST_CASE("rpg.worldState.rejectsInvalidStableIdsWithoutMutation") {
    eve::rpg::GameState gameState;
    eve::rpg::WorldState world(gameState);
    auto emptyMap = world.consumeObject("", "chest");
    CHECK(!emptyMap.ok());
    auto controlObject = world.consumeObject("village", std::string("bad\nobject"));
    CHECK(!controlObject.ok());
    CHECK(!world.isObjectConsumed("village", "chest"));
    CHECK(!gameState.hasSelfVariable("world.object:village", "bad\nobject"));
}
