#include "decision/Decision.h"

#include <cmath>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::decision;

TEST_CASE("decision.blackboardsAndFsmAreExplicit") {
    DecisionContext context;

    auto setResult = context.set("ai", "enemy", "\"tank-1\"");
    REQUIRE(setResult.ok());
    CHECK_EQ(context.get("ai", "enemy", "null"), std::string("\"tank-1\""));

    auto stateResult = context.setState("unit", "idle");
    REQUIRE(stateResult.ok());
    auto transitionResult = context.addTransition("unit", "idle", "enemy_seen", "attack");
    REQUIRE(transitionResult.ok());
    auto triggerResult = context.trigger("unit", "enemy_seen");
    REQUIRE(triggerResult.ok());
    REQUIRE(triggerResult.value());
    CHECK_EQ(context.state("unit"), std::string("attack"));
}

TEST_CASE("decision.utilityIsDeterministic") {
    CHECK(std::abs(DecisionContext::utility("1:2,0:1") - .6666667f) < .001f);
    CHECK_EQ(DecisionContext::choose("retreat=0.8:1;attack=0.8:1"), std::string("attack"));
}

TEST_CASE("decision.influenceAndSnapshotRoundTrip") {
    DecisionContext context;

    auto gridResult = context.newGrid("threat", 2, 2, 10, 0, 0);
    REQUIRE(gridResult.ok());
    auto setCellResult = context.setCell("threat", 1, 0, 2);
    REQUIRE(setCellResult.ok());
    auto addCellResult = context.addCell("threat", 1, 0, 3);
    REQUIRE(addCellResult.ok());
    CHECK_EQ(context.sample("threat", 15, 5, -1), 5.f);

    auto            snapshot = context.snapshotJson();
    DecisionContext restored;
    auto            restoreResult = restored.restoreJson(snapshot);
    REQUIRE(restoreResult.ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);

    auto beforeInvalidRestore = restored.snapshotJson();
    auto rejected             = restored.restoreJson("{}");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Failed);
    CHECK_EQ(restored.snapshotJson(), beforeInvalidRestore);
}
