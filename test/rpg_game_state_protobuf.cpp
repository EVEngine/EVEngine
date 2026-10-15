#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "../proto/generated/rpg_game_state.pb.h"
#include "rpg/GameState.h"

#include <limits>

TEST_CASE("rpg.gameState.protobufRoundTripAndAtomicRejection") {
    eve::rpg::GameState source;
    source.switchOn("door.open");
    source.setVariable("gold", 125.5);
    source.setSelfVariable("map:1", "visits", 2.0);
    source.setSelfString("story", "cursor", "chapter.2");
    auto bytes = source.snapshotBinary();
    REQUIRE(bytes.ok());

    eve::rpg::GameState restored;
    restored.setVariable("sentinel", 7.0);
    REQUIRE(restored.restoreSnapshotBinary(bytes.value()).ok());
    CHECK(restored.isSwitchOn("door.open"));
    CHECK_EQ(restored.getVariable("gold"), 125.5);
    CHECK_EQ(restored.getSelfVariable("map:1", "visits"), 2.0);
    CHECK_EQ(restored.getSelfString("story", "cursor"), std::string("chapter.2"));
    CHECK_EQ(restored.getVariable("sentinel"), 0.0);

    eve::rpg::proto::GameStateSave invalid;
    REQUIRE(invalid.ParseFromString(bytes.value()));
    invalid.set_version(2);
    std::string future;
    REQUIRE(invalid.SerializeToString(&future));
    CHECK(!restored.restoreSnapshotBinary(future).ok());
    CHECK_EQ(restored.getVariable("gold"), 125.5);

    invalid.set_version(1);
    (*invalid.mutable_variables())["gold"] = std::numeric_limits<double>::infinity();
    std::string nonFinite;
    REQUIRE(invalid.SerializeToString(&nonFinite));
    CHECK(!restored.restoreSnapshotBinary(nonFinite).ok());
    CHECK_EQ(restored.getVariable("gold"), 125.5);

    (*invalid.mutable_variables())["gold"] = 125.5;
    const auto firstScope                  = invalid.self_variables(0);
    *invalid.add_self_variables()          = firstScope;
    std::string duplicateScope;
    REQUIRE(invalid.SerializeToString(&duplicateScope));
    CHECK(!restored.restoreSnapshotBinary(duplicateScope).ok());
    CHECK_EQ(restored.getSelfVariable("map:1", "visits"), 2.0);

    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    REQUIRE(restored.restoreSnapshotJson(json.value()).ok());
    CHECK_EQ(restored.getSelfString("story", "cursor"), std::string("chapter.2"));
    REQUIRE(
        restored
            .restoreSnapshotJson(
                R"({"schema":"eve.rpg.game-state","version":1,"switches":{},"variables":{"gold":3},"selfVariables":{}})")
            .ok());
    CHECK_EQ(restored.getVariable("gold"), 3.0);
}
