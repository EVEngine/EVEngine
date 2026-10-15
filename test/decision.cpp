#include "decision/Decision.h"
#include "decision_context.pb.h"

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

TEST_CASE("decision.protobufSnapshotRejectsInvalidWithoutMutation") {
    DecisionContext source;
    REQUIRE(source.set("ai", "target", "\"tank\"").ok());
    REQUIRE(source.setState("unit", "idle").ok());
    REQUIRE(source.addTransition("unit", "idle", "seen", "attack").ok());
    REQUIRE(source.newGrid("threat", 2, 1, 4, 0, 0).ok());
    REQUIRE(source.setCell("threat", 1, 0, 3).ok());
    auto bytes = source.snapshotBinary();
    REQUIRE(bytes.ok());
    auto repeatedBytes = source.snapshotBinary();
    REQUIRE(repeatedBytes.ok());
    CHECK_EQ(bytes.value(), repeatedBytes.value());

    DecisionContext restored;
    REQUIRE(restored.restoreBinary(bytes.value()).ok());
    CHECK_EQ(restored.snapshotJson(), source.snapshotJson());
    REQUIRE(restored.trigger("unit", "seen").ok());
    CHECK_EQ(restored.state("unit"), std::string("attack"));

    eve::decision::proto::DecisionContextSave invalid;
    REQUIRE(invalid.ParseFromString(bytes.value()));
    invalid.set_version(3);
    std::string future;
    REQUIRE(invalid.SerializeToString(&future));
    CHECK(!restored.restoreBinary(future).ok());
    CHECK_EQ(restored.state("unit"), std::string("attack"));

    invalid.set_version(2);
    invalid.mutable_grids(0)->set_width(3);
    std::string malformed;
    REQUIRE(invalid.SerializeToString(&malformed));
    CHECK(!restored.restoreBinary(malformed).ok());
    CHECK_EQ(restored.sample("threat", 5, 0, -1), 3.f);

    invalid.mutable_grids(0)->set_width(2);
    invalid.GetReflection()->MutableUnknownFields(&invalid)->AddVarint(100, 1);
    std::string withUnknown;
    REQUIRE(invalid.SerializeToString(&withUnknown));
    CHECK(!restored.restoreBinary(withUnknown).ok());
    CHECK_EQ(restored.sample("threat", 5, 0, -1), 3.f);

    REQUIRE(restored.restoreJson(source.snapshotJson()).ok());
    REQUIRE(
        restored
            .restoreJson(R"({"version":1,"boards":{"ai":{"target":"tank"}},"states":{},"transitions":[],"grids":[]})")
            .ok());
    CHECK_EQ(restored.get("ai", "target", "null"), std::string("\"tank\""));
}
