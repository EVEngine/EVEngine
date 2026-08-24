#include "decision/Decision.h"
#include <cmath>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::decision;
TEST_CASE("decision.blackboardsAndFsmAreExplicit") {
    DecisionContext d;
    REQUIRE(d.set("ai", "enemy", "\"tank-1\""));
    CHECK_EQ(d.get("ai", "enemy", "null"), std::string("\"tank-1\""));
    REQUIRE(d.setState("unit", "idle"));
    REQUIRE(d.addTransition("unit", "idle", "enemy_seen", "attack"));
    REQUIRE(d.trigger("unit", "enemy_seen"));
    CHECK_EQ(d.state("unit"), std::string("attack"));
}
TEST_CASE("decision.utilityIsDeterministic") {
    CHECK(std::abs(DecisionContext::utility("1:2,0:1") - .6666667f) < .001f);
    CHECK_EQ(DecisionContext::choose("retreat=0.8:1;attack=0.8:1"), std::string("attack"));
}
TEST_CASE("decision.influenceAndSnapshotRoundTrip") {
    DecisionContext d;
    REQUIRE(d.newGrid("threat", 2, 2, 10, 0, 0));
    REQUIRE(d.setCell("threat", 1, 0, 2));
    REQUIRE(d.addCell("threat", 1, 0, 3));
    CHECK_EQ(d.sample("threat", 15, 5, -1), 5.f);
    auto            s = d.snapshotJson();
    DecisionContext x;
    REQUIRE(x.restoreJson(s));
    CHECK_EQ(x.snapshotJson(), s);
    auto before = x.snapshotJson();
    CHECK(!x.restoreJson("{}"));
    CHECK_EQ(x.snapshotJson(), before);
}
