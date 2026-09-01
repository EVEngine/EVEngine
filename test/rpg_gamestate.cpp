#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/GameState.h"
#include "rpg/RPG.h"

using namespace eve::rpg;

TEST_CASE("rpg.gameState.switchesAndVariables") {
    GameState gs;
    CHECK(!gs.isSwitchOn("door"));
    gs.switchOn("door");
    CHECK(gs.isSwitchOn("door"));
    gs.switchOff("door");
    CHECK(!gs.isSwitchOn("door"));
    gs.setSwitch("flag", true);
    CHECK(gs.isSwitchOn("flag"));

    CHECK_EQ(gs.getVariable("gold"), 0.0);
    gs.setVariable("gold", 100.0);
    CHECK_EQ(gs.getVariable("gold"), 100.0);
    gs.addVariable("gold", 25.0);
    CHECK_EQ(gs.getVariable("gold"), 125.0);

    gs.clear();
    CHECK(!gs.isSwitchOn("flag"));
    CHECK_EQ(gs.getVariable("gold"), 0.0);
}

TEST_CASE("rpg.gameState.selfVariablesScoped") {
    GameState gs;
    gs.setSelfVariable("map:1:event:3", "switchA", 1.0);
    CHECK(gs.hasSelfVariable("map:1:event:3", "switchA"));
    CHECK_EQ(gs.getSelfVariable("map:1:event:3", "switchA"), 1.0);
    // 不同 scope 隔离
    CHECK(!gs.hasSelfVariable("map:1:event:4", "switchA"));
    CHECK_EQ(gs.getSelfVariable("map:1:event:3", "missing"), 0.0);
}

TEST_CASE("rpg.gameState.facadeAndGlobal") {
    auto *rpg = RPG::create();
    // 独立实例
    GameState *gs = rpg->newGameState();
    REQUIRE(gs != nullptr);
    gs->setVariable("hp", 50.0);
    gs->setSwitch("boss_alive", true);
    CHECK_EQ(gs->getVariable("hp"), 50.0);
    CHECK(gs->isSwitchOn("boss_alive"));

    // 全局单例
    GameState *g = rpg->globalGameState();
    REQUIRE(g != nullptr);
    g->clear();
    g->setVariable("level", 7.0);
    CHECK_EQ(GameState::global().getVariable("level"), 7.0);
}

TEST_CASE("rpg.gameState.versionedSnapshotRoundTripAndAtomicFailure") {
    eve::rpg::GameState source;
    source.switchOn("door.open");
    source.setVariable("gold", 125.5);
    source.setSelfVariable("map:1:event:3", "visits", 2.0);

    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());

    eve::rpg::GameState restored;
    restored.setVariable("sentinel", 7.0);
    auto applied = restored.restoreSnapshotJson(encoded.value());
    REQUIRE(applied.ok());
    CHECK(restored.isSwitchOn("door.open"));
    CHECK_EQ(restored.getVariable("gold"), 125.5);
    CHECK_EQ(restored.getSelfVariable("map:1:event:3", "visits"), 2.0);
    CHECK_EQ(restored.getVariable("sentinel"), 0.0);

    auto malformed = restored.restoreSnapshotJson(
        R"({"schema":"eve.rpg.game-state","version":1,"switches":{},"variables":{"gold":"bad"},"selfVariables":{}})");
    CHECK(!malformed.ok());
    CHECK_EQ(restored.getVariable("gold"), 125.5);

    auto future = restored.restoreSnapshotJson(
        R"({"schema":"eve.rpg.game-state","version":2,"switches":{},"variables":{},"selfVariables":{}})");
    CHECK(!future.ok());
    CHECK_EQ(restored.getVariable("gold"), 125.5);
}
