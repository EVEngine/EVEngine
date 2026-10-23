#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen::road;
using namespace eve::procgen_editing;
using namespace eve::editing;

TEST_CASE("procgen.road.editing.pointProfilesRoundTripMigrateAndRejectAtomically") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    RoadNetworkEditTarget target("road:profiles", network.value());
    auto                  points = network.value().edges().front().controlPoints;
    points[0].widthScale         = 0.5f;
    points[1].rollDegrees        = 15.f;
    points[2].widthScale         = 2.f;
    auto operation               = target.makeSetEdgeControlPoints(1, points);
    REQUIRE(operation.ok());
    auto candidate = target.cloneDomainState();
    REQUIRE(candidate->applyDomainOperation(operation.value()).ok());
    REQUIRE(target.commitDomainState(std::move(candidate)).ok());
    const auto saved = target.snapshotValue();
    REQUIRE(network.value().edges().front().controlPoints[0].widthScale == 0.5f);
    REQUIRE(network.value().edges().front().controlPoints[1].rollDegrees == 15.f);
    RoadNetwork           restored;
    RoadNetworkEditTarget restoredTarget("road:profiles", restored);
    REQUIRE(restoredTarget.loadSnapshot(saved).ok());
    REQUIRE(restored.edges().front().controlPoints[2].widthScale == 2.f);
    REQUIRE(restored.edges().front().controlPoints[1].rollDegrees == 15.f);

    auto  corrupt  = saved;
    auto& edges    = *corrupt.getIf<Value::Object>()->at("edges").getIf<Value::Array>();
    auto& profiles = *edges[0].getIf<Value::Object>()->at("pointProfiles").getIf<Value::Array>();
    profiles.pop_back();
    REQUIRE(!restoredTarget.loadSnapshot(corrupt).ok());
    REQUIRE(restoredTarget.snapshotValue() == saved);

    auto  legacy      = saved;
    auto& legacyEdges = *legacy.getIf<Value::Object>()->at("edges").getIf<Value::Array>();
    for (auto& edge : legacyEdges) edge.getIf<Value::Object>()->erase("pointProfiles");
    REQUIRE(restoredTarget.loadSnapshot(legacy).ok());
    REQUIRE(restored.edges().front().controlPoints[0].widthScale == 1.f);
    REQUIRE(restored.edges().front().controlPoints[1].rollDegrees == 0.f);
}

TEST_CASE("procgen.road.editing.pointProfilesUndoRestoresExactAuthorValues") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    RoadNetworkEditTarget target("road:profiles", network.value());
    auto                  points = network.value().edges().front().controlPoints;
    points[1].widthScale         = 2.f;
    points[1].rollDegrees        = 25.f;
    auto operation               = target.makeSetEdgeControlPoints(1, points);
    REQUIRE(operation.ok());
    LocalWorldAuthority authority(&target);
    TransactionSpec     transaction;
    transaction.id           = TransactionId("road.profiles.edit");
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    auto plan = authority.preflight(transaction, std::span<const DomainOperation>(&operation.value(), 1));
    REQUIRE(plan.ok());
    auto receipt = authority.commit(plan.value());
    REQUIRE(receipt.ok());
    REQUIRE(network.value().edges().front().controlPoints[1].rollDegrees == 25.f);
    REQUIRE(authority.compensate(receipt.value()).ok());
    REQUIRE(network.value().edges().front().controlPoints[1].widthScale == 1.f);
    REQUIRE(network.value().edges().front().controlPoints[1].rollDegrees == 0.f);
}
