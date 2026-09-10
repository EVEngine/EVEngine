#include "../examples/agent/Environments.h"
#include "agent/Agent.h"
#include "grid/GridProjection.h"
#include "ui/Layout.h"
#include "zeroerr/unittest.h"

using namespace eve::agent;

TEST_CASE("agent.productionGridGame") {
    agent_examples::GridGame game;
    Config                   c;
    c.featureCount = 2;
    c.actionCount  = 4;
    c.horizon      = 24;
    auto result    = run(c, game);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().failures, 0u);
    REQUIRE(result.value().best.steps.back().observation.outcome == Outcome::Success);
    REQUIRE(replay(result.value().best, game).ok());
    REQUIRE(result.value().trainingSamples > 0);
}

TEST_CASE("agent.productionUiLayout") {
    agent_examples::UiLayout ui;
    Config                   c;
    c.featureCount = 4;
    c.actionCount  = 5;
    c.population   = 12;
    c.elites       = 3;
    c.generations  = 2;
    c.strategy     = Strategy::Random;
    auto result    = run(c, ui);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().failures, 0u);
    REQUIRE(result.value().coverage.size() > 15);
    REQUIRE(replay(result.value().best, ui).ok());
}
