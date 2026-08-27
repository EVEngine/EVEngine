#include "decision/Decision.h"
#include <cmath>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::decision;
TEST_CASE("decision.blackboardsAndFsmAreExplicit") {
    DecisionContext d;
    auto setResult = d.set("ai", "enemy", "\"tank-1\"");
    REQUIRE(setResult.ok());
    CHECK_EQ(d.get("ai", "enemy", "null"), std::string("\"tank-1\""));
    auto stateResult = d.setState("unit", "idle");
    REQUIRE(stateResult.ok());
    auto transitionResult = d.addTransition("unit", "idle", "enemy_seen", "attack");
    REQUIRE(transitionResult.ok());
    auto triggerResult = d.trigger("unit", "enemy_seen");
    REQUIRE(triggerResult.ok());
    REQUIRE(triggerResult.value());
    CHECK_EQ(d.state("unit"), std::string("attack"));
}
TEST_CASE("decision.utilityIsDeterministic") {
    CHECK(std::abs(DecisionContext::utility("1:2,0:1") - .6666667f) < .001f);
    CHECK_EQ(DecisionContext::choose("retreat=0.8:1;attack=0.8:1"), std::string("attack"));
}
TEST_CASE("decision.influenceAndSnapshotRoundTrip") {
    DecisionContext d;
    auto gridResult = d.newGrid("threat", 2, 2, 10, 0, 0);
    REQUIRE(gridResult.ok());
    auto setCellResult = d.setCell("threat", 1, 0, 2);
    REQUIRE(setCellResult.ok());
    auto addCellResult = d.addCell("threat", 1, 0, 3);
    REQUIRE(addCellResult.ok());
    CHECK_EQ(d.sample("threat", 15, 5, -1), 5.f);
    auto            s = d.snapshotJson();
    DecisionContext x;
    auto restoreResult = x.restoreJson(s);
    REQUIRE(restoreResult.ok());
    CHECK_EQ(x.snapshotJson(), s);
    auto before = x.snapshotJson();
    auto rejected = x.restoreJson("{}");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Failed);
    CHECK_EQ(x.snapshotJson(), before);
}
