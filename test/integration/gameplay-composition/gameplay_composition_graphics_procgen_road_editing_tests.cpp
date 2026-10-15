#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_road_editing)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadNetwork.h"
#include "zeroerr/unittest.h"
// procgen_road_editing.cpp
namespace test_procgen_road_editing {
using namespace eve;
using namespace eve::editing;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;

namespace {

class RoadCommandRegistry final : public IEditingCommandRegistry {
public:
    eve::editing::Result<void> registerPlannedCommand(EditingCommandDescriptor descriptor,
                                                      EditingCommandPlanner    planner) override {
        planners[descriptor.id.value()] = std::move(planner);
        owners[descriptor.id.value()]   = std::move(descriptor.ownerModule);
        return eve::editing::Result<void>::success();
    }

    eve::editing::Result<std::size_t> unregisterOwner(const std::string &owner) override {
        std::size_t removed = 0;
        for (auto it = owners.begin(); it != owners.end();) {
            if (it->second != owner) {
                ++it;
                continue;
            }
            planners.erase(it->first);
            it = owners.erase(it);
            ++removed;
        }
        return eve::editing::Result<std::size_t>::success(removed);
    }

    std::unordered_map<std::string, EditingCommandPlanner> planners;
    std::unordered_map<std::string, std::string>           owners;
};

}  // namespace

TEST_CASE("procgen.road.editing.nodeMoveReanchorsIncidentEdges") {
    RoadNetwork network;
    auto        a = network.addNode(-10.f, 0.f, 0.f);
    auto        b = network.addNode(0.f, 0.f, 0.f);
    auto        c = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    auto left  = network.addEdge(a.value(), b.value(), {{-10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 0);
    auto right = network.addEdge(b.value(), c.value(), {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}}, 1, 0);
    REQUIRE(left.ok());
    REQUIRE(right.ok());

    RoadNetworkEditTarget target("road:test", network);
    auto                  operation = target.makeMoveNode(b.value(), 1.f, 2.f, 3.f);
    REQUIRE(operation.ok());
    LocalWorldAuthority authority(&target);
    TransactionSpec     spec;
    spec.id           = eve::editing::TransactionId("road-move-1");
    spec.label        = "Move road junction";
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    const std::array<DomainOperation, 1> operations{operation.value()};
    auto                                 plan = authority.preflight(spec, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());

    auto moved = network.nodeResult(b.value());
    auto l     = network.edgeResult(left.value());
    auto r     = network.edgeResult(right.value());
    REQUIRE(moved.ok());
    REQUIRE(l.ok());
    REQUIRE(r.ok());
    CHECK_EQ(moved.value().x, 1.f);
    CHECK_EQ(l.value().controlPoints.back().z, 3.f);
    CHECK_EQ(r.value().controlPoints.front().y, 2.f);
    REQUIRE(network.validate().ok());

    auto undone = authority.compensate(committed.value());
    REQUIRE(undone.ok());
    auto restored = network.nodeResult(b.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().x, 0.f);
    CHECK_EQ(restored.value().y, 0.f);
    CHECK_EQ(restored.value().z, 0.f);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.edgeControlPointsCommitAtomically") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    const auto            edgeId = network.value().edges().front().id;
    RoadNetworkEditTarget target("road:curve", network.value());
    auto                  operation = target.makeSetEdgeControlPoints(
        edgeId, {{999.f, 9.f, 9.f}, {-3.f, 0.f, 4.f}, {3.f, 0.f, 4.f}, {-999.f, 9.f, 9.f}});
    REQUIRE(operation.ok());

    auto candidate = target.cloneDomainState();
    REQUIRE(candidate != nullptr);
    REQUIRE(candidate->applyDomainOperation(operation.value()).ok());
    REQUIRE(target.commitDomainState(std::move(candidate)).ok());
    auto edge = network.value().edgeResult(edgeId);
    REQUIRE(edge.ok());
    CHECK_EQ(edge.value().controlPoints.size(), std::size_t(4));
    CHECK_EQ(edge.value().controlPoints.front().x, network.value().nodes().front().x);
    CHECK_EQ(edge.value().controlPoints.back().x, network.value().nodes().back().x);
    CHECK_EQ(edge.value().controlPoints[1].z, 4.f);
    REQUIRE(network.value().validate().ok());

    auto invalid = target.makeSetEdgeControlPoints(edgeId, {{0.f, 0.f, 0.f}});
    CHECK(!invalid.ok());
    CHECK_EQ(network.value().edgeResult(edgeId).value().controlPoints.size(), std::size_t(4));
}

TEST_CASE("procgen.road.editing.styleUsesExistingAuthorityUndo") {
    auto network = RoadNetwork::makeStraight(24.f, 2);
    REQUIRE(network.ok());
    const auto            edgeId = network.value().edges().front().id;
    const auto            before = network.value().edges().front().style;
    RoadNetworkEditTarget target("road:style", network.value());
    RoadStyle             style = before;
    style.laneWidth             = 4.25f;
    style.uvMeters              = 6.f;
    style.speedLimitMps         = 22.22f;
    style.trafficPriority       = 7;
    style.sideObjectStartOffset = 2.f;
    style.sideObjectEndOffset   = 3.f;
    style.sideObjectsRight      = false;
    auto operation              = target.makeSetEdgeStyle(edgeId, style);
    REQUIRE(operation.ok());

    LocalWorldAuthority authority(&target);
    TransactionSpec     spec;
    spec.id           = eve::editing::TransactionId("road-style-1");
    spec.label        = "Change road style";
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    const std::array<DomainOperation, 1> operations{operation.value()};
    auto                                 plan = authority.preflight(spec, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    auto changed = network.value().edgeResult(edgeId);
    REQUIRE(changed.ok());
    CHECK_EQ(changed.value().style.laneWidth, 4.25f);
    CHECK_EQ(changed.value().style.uvMeters, 6.f);
    CHECK_EQ(changed.value().style.speedLimitMps, 22.22f);
    CHECK_EQ(changed.value().style.trafficPriority, 7);
    CHECK_EQ(changed.value().style.sideObjectStartOffset, 2.f);
    CHECK_EQ(changed.value().style.sideObjectEndOffset, 3.f);
    CHECK(!changed.value().style.sideObjectsRight);

    auto undone = authority.compensate(committed.value());
    REQUIRE(undone.ok());
    auto restored = network.value().edgeResult(edgeId);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().style.laneWidth, before.laneWidth);
    CHECK_EQ(restored.value().style.uvMeters, before.uvMeters);
    CHECK_EQ(restored.value().style.speedLimitMps, before.speedLimitMps);
    CHECK_EQ(restored.value().style.trafficPriority, before.trafficPriority);
    CHECK_EQ(restored.value().style.sideObjectStartOffset, before.sideObjectStartOffset);
    CHECK_EQ(restored.value().style.sideObjectEndOffset, before.sideObjectEndOffset);
    CHECK_EQ(restored.value().style.sideObjectsRight, before.sideObjectsRight);

    style.speedLimitMps = 0.f;
    CHECK(!target.makeSetEdgeStyle(edgeId, style).ok());
}

TEST_CASE("procgen.road.editing.junctionRadiusUsesExistingAuthorityUndo") {
    RoadNetwork network;
    auto        node = network.addNode(0.f, 0.f, 0.f, 3.f);
    REQUIRE(node.ok());
    RoadNetworkEditTarget target("road:node-radius", network);
    auto                  operation = target.makeSetNodeJunctionRadius(node.value(), 7.5f);
    REQUIRE(operation.ok());

    LocalWorldAuthority authority(&target);
    TransactionSpec     spec;
    spec.id           = eve::editing::TransactionId("road-node-radius-1");
    spec.label        = "Change road junction radius";
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    const std::array<DomainOperation, 1> operations{operation.value()};
    auto                                 plan = authority.preflight(spec, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(network.nodeResult(node.value()).value().junctionRadius, 7.5f);

    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(network.nodeResult(node.value()).value().junctionRadius, 3.f);
    CHECK(!target.makeSetNodeJunctionRadius(node.value(), -1.f).ok());
}

TEST_CASE("procgen.road.editing.junctionControlUsesExistingAuthorityUndo") {
    RoadNetwork network;
    auto        node = network.addNode(0.f, 0.f, 0.f, 3.f);
    REQUIRE(node.ok());
    RoadNetworkEditTarget target("road:node-control", network);
    auto                  operation = target.makeSetNodeJunctionControl(node.value(), RoadJunctionControl::Signal);
    REQUIRE(operation.ok());

    LocalWorldAuthority authority(&target);
    TransactionSpec     spec;
    spec.id           = eve::editing::TransactionId("road-node-control-1");
    spec.label        = "Change road junction control";
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    const std::array<DomainOperation, 1> operations{operation.value()};
    auto                                 plan = authority.preflight(spec, operations);
    REQUIRE(plan.ok());
    auto committed = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(network.nodeResult(node.value()).value().junctionControl, RoadJunctionControl::Signal);

    REQUIRE(authority.compensate(committed.value()).ok());
    CHECK_EQ(network.nodeResult(node.value()).value().junctionControl, RoadJunctionControl::Uncontrolled);
    CHECK(!target.makeSetNodeJunctionControl(node.value(), static_cast<RoadJunctionControl>(255)).ok());
}

TEST_CASE("procgen.road.editing.structuralUndoRestoresStableIdsAndLinks") {
    RoadNetwork network;
    auto        west = network.addNode(-10.f, 0.f, 0.f);
    auto        hub  = network.addNode(0.f, 0.f, 0.f);
    auto        east = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    auto in  = network.addEdge(west.value(), hub.value(), {{-10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 0);
    auto out = network.addEdge(hub.value(), east.value(), {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}}, 1, 0);
    REQUIRE(in.ok());
    REQUIRE(out.ok());
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    const int originalLinks = network.laneLinkCount();
    REQUIRE_GT(originalLinks, 0);

    RoadNetworkEditTarget target("road:structure", network);
    auto                  remove = target.makeRemoveEdge(in.value());
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    CHECK(!network.edgeResult(in.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 0);

    DomainOperation restore = remove.value();
    restore.type            = remove.value().inverseType;
    restore.payload         = remove.value().inverse;
    REQUIRE(target.applyDomainOperation(restore).ok());
    REQUIRE(network.edgeResult(in.value()).ok());
    CHECK_EQ(network.laneLinkCount(), originalLinks);
    REQUIRE(network.validate().ok());

    auto rejectedNodeRemoval = target.makeRemoveNode(hub.value());
    CHECK(!rejectedNodeRemoval.ok());

    auto addNode = target.makeAddNode(20.f, 0.f, 0.f, 2.f);
    REQUIRE(addNode.ok());
    REQUIRE(target.applyDomainOperation(addNode.value()).ok());
    REQUIRE_EQ(network.nodeCount(), 4);
    const auto     &addedNode   = network.nodes().back();
    const auto      addedId     = addedNode.id;
    DomainOperation removeAdded = addNode.value();
    removeAdded.type            = addNode.value().inverseType;
    removeAdded.payload         = addNode.value().inverse;
    REQUIRE(target.applyDomainOperation(removeAdded).ok());
    CHECK(!network.nodeResult(addedId).ok());
    REQUIRE(target.applyDomainOperation(addNode.value()).ok());
    REQUIRE(network.nodeResult(addedId).ok());
}

TEST_CASE("procgen.road.editing.addEdgeUndoKeepsReservedIdentity") {
    RoadNetwork network;
    auto        a = network.addNode(0.f, 0.f, 0.f, 2.f);
    auto        b = network.addNode(12.f, 0.f, 0.f, 2.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadNetworkEditTarget target("road:add-edge", network);
    auto add = target.makeAddEdge(a.value(), b.value(), {{0.f, 0.f, 0.f}, {6.f, 0.f, 2.f}, {12.f, 0.f, 0.f}}, 1, 1);
    REQUIRE(add.ok());
    REQUIRE(target.applyDomainOperation(add.value()).ok());
    REQUIRE_EQ(network.edgeCount(), 1);
    const auto edgeId = network.edges().front().id;

    DomainOperation remove = add.value();
    remove.type            = add.value().inverseType;
    remove.payload         = add.value().inverse;
    REQUIRE(target.applyDomainOperation(remove).ok());
    CHECK_EQ(network.edgeCount(), 0);
    REQUIRE(target.applyDomainOperation(add.value()).ok());
    REQUIRE_EQ(network.edges().front().id, edgeId);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.manualLaneLinksUseExistingTransactionUndo") {
    RoadNetwork network;
    auto        west  = network.addNode(-12.f, 0.f, 0.f, 3.f);
    auto        hub   = network.addNode(0.f, 0.f, 0.f, 3.f);
    auto        north = network.addNode(0.f, 0.f, -12.f, 3.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(north.ok());
    auto incoming = network.addEdge(west.value(), hub.value(), {{-12.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 0);
    auto outgoing = network.addEdge(hub.value(), north.value(), {{0.f, 0.f, 0.f}, {0.f, 0.f, -12.f}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());

    const RoadLaneConnection link{incoming.value(),          0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};
    RoadNetworkEditTarget    target("road:manual-links", network);
    auto                     add = target.makeAddLaneLink(link);
    REQUIRE(add.ok());
    REQUIRE(target.applyDomainOperation(add.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 1);
    CHECK(!target.makeAddLaneLink(link).ok());

    auto remove = target.makeRemoveLaneLink(link);
    REQUIRE(remove.ok());
    REQUIRE(target.applyDomainOperation(remove.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK(!target.makeRemoveLaneLink(link).ok());

    DomainOperation restore = remove.value();
    restore.type            = remove.value().inverseType;
    restore.payload         = remove.value().inverse;
    REQUIRE(target.applyDomainOperation(restore).ok());
    CHECK_EQ(network.laneLinkCount(), 1);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.blockedTurnIsUndoableAndSnapshotPersistent") {
    RoadNetwork network;
    auto        west = network.addNode(-10.f, 0.f, 0.f);
    auto        hub  = network.addNode(0.f, 0.f, 0.f);
    auto        east = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    auto incoming = network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 0);
    auto outgoing = network.addEdge(hub.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    auto connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 1);
    const RoadLaneConnection turn{incoming.value(),          0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};

    RoadNetworkEditTarget target("road:block-turn", network);
    auto                  operation = target.makeBlockLaneLink(turn);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK_EQ(network.blockedLaneLinkCount(), 1);
    connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 0);

    RoadNetwork           restored;
    RoadNetworkEditTarget restoredTarget("road:block-turn-restored", restored);
    REQUIRE(restoredTarget.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(restored.blockedLaneLinkCount(), 1);
    auto restoredConnect = restored.connectAllTurns(hub.value());
    REQUIRE(restoredConnect.ok());
    CHECK_EQ(restoredConnect.value(), 0);

    auto removeEdge = target.makeRemoveEdge(outgoing.value());
    REQUIRE(removeEdge.ok());
    REQUIRE(target.applyDomainOperation(removeEdge.value()).ok());
    CHECK_EQ(network.blockedLaneLinkCount(), 0);
    DomainOperation restoreEdge = removeEdge.value();
    restoreEdge.type            = removeEdge.value().inverseType;
    restoreEdge.payload         = removeEdge.value().inverse;
    REQUIRE(target.applyDomainOperation(restoreEdge).ok());
    CHECK_EQ(network.blockedLaneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinks().front().outEdge, outgoing.value());

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.blockedLaneLinkCount(), 0);
    REQUIRE_EQ(network.laneLinkCount(), 1);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    auto unblock = target.makeUnblockLaneLink(turn);
    REQUIRE(unblock.ok());
    REQUIRE(target.applyDomainOperation(unblock.value()).ok());
    CHECK_EQ(network.blockedLaneLinkCount(), 0);
    CHECK_EQ(network.laneLinkCount(), 0);
    connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 1);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.reconnectEndpointUndoRestoresOldJunctionConnections") {
    RoadNetwork network;
    auto        start       = network.addNode(-12.f, 0.f, 0.f);
    auto        oldHub      = network.addNode(0.f, 0.f, 0.f);
    auto        newHub      = network.addNode(5.f, 0.f, 7.f);
    auto        exit        = network.addNode(0.f, 0.f, 12.f);
    auto        newExit     = network.addNode(14.f, 0.f, 7.f);
    auto        newApproach = network.addNode(5.f, 0.f, -4.f);
    REQUIRE(start.ok());
    REQUIRE(oldHub.ok());
    REQUIRE(newHub.ok());
    REQUIRE(exit.ok());
    REQUIRE(newExit.ok());
    REQUIRE(newApproach.ok());
    auto road             = network.addEdge(start.value(), oldHub.value(), {{}, {}}, 2, 0);
    auto branch           = network.addEdge(oldHub.value(), exit.value(), {{}, {}}, 2, 0);
    auto newBranch        = network.addEdge(newHub.value(), newExit.value(), {{}, {}}, 2, 0);
    auto existingApproach = network.addEdge(newApproach.value(), newHub.value(), {{}, {}}, 2, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());
    REQUIRE(newBranch.ok());
    REQUIRE(existingApproach.ok());
    const RoadLaneConnection active{
        road.value(), 0, branch.value(), 0, RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    const RoadLaneConnection blocked{
        road.value(), 1, branch.value(), 1, RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(active).ok());
    REQUIRE(network.blockLaneLink(blocked).ok());

    RoadNetworkEditTarget target("road:reconnect", network);
    auto                  operation = target.makeReconnectEdgeEndpointAndConnect(road.value(), false, newHub.value());
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, newHub.value());
    CHECK_EQ(network.laneLinkCount(), 2);
    for (const auto &link : network.laneLinks()) {
        const bool referencesMovedRoad = link.inEdge == road.value() || link.outEdge == road.value();
        CHECK(referencesMovedRoad);
    }
    CHECK_EQ(network.blockedLaneLinkCount(), 0);

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, oldHub.value());
    CHECK_EQ(network.laneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinkCount(), 1);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, newHub.value());
    CHECK_EQ(network.laneLinkCount(), 2);
    CHECK_EQ(static_cast<int>(target.makeReconnectEdgeEndpoint(road.value(), false, newHub.value()).code()),
             static_cast<int>(EditorStatus::NoOp));
}

TEST_CASE("procgen.road.editing.snapEndpointChoosesNearestStableNodeAndConnects") {
    RoadNetwork network;
    auto        start     = network.addNode(-10.f, 0.f, 0.f);
    auto        oldHub    = network.addNode(0.f, 0.f, 0.f);
    auto        firstTie  = network.addNode(0.2f, 0.f, 0.f);
    auto        secondTie = network.addNode(-0.2f, 0.f, 0.f);
    auto        exit      = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(start.ok());
    REQUIRE(oldHub.ok());
    REQUIRE(firstTie.ok());
    REQUIRE(secondTie.ok());
    REQUIRE(exit.ok());
    auto road   = network.addEdge(start.value(), oldHub.value(), {{}, {}}, 1, 0);
    auto branch = network.addEdge(firstTie.value(), exit.value(), {{}, {}}, 1, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());

    RoadNetworkEditTarget target("road:snap-endpoint", network);
    CHECK_EQ(static_cast<int>(target.makeSnapEdgeEndpoint(road.value(), false, 0.1f).code()),
             static_cast<int>(EditorStatus::NotFound));
    RoadCommandRegistry registry;
    REQUIRE(registerRoadNetworkEditingCommands(registry).ok());
    CommandRequest request;
    request.id = eve::editing::CommandId("road.edge.snap-endpoint.v1");
    request.payload =
        EditorValue::Object{{"id", std::int64_t{road.value()}}, {"fromEndpoint", false}, {"maxDistance", 0.25}};
    auto planned = registry.planners.at("road.edge.snap-endpoint.v1")(target, request);
    REQUIRE(planned.ok());
    REQUIRE_EQ(planned.value().operations.size(), std::size_t{1});
    const auto operation = planned.value().operations.front();
    REQUIRE(target.applyDomainOperation(operation).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, firstTie.value());
    REQUIRE_EQ(network.laneLinkCount(), 1);
    CHECK_EQ(network.laneLinks().front().outEdge, branch.value());

    DomainOperation undo = operation;
    undo.type            = operation.inverseType;
    undo.payload         = operation.inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, oldHub.value());
    CHECK_EQ(network.laneLinkCount(), 0);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.detachEndpointCommandUndoRestoresJunctionExactly") {
    RoadNetwork network;
    auto        start = network.addNode(-10.f, 0.f, 0.f);
    auto        hub   = network.addNode(0.f, 0.f, 0.f, 4.f);
    auto        exit  = network.addNode(0.f, 0.f, 10.f);
    REQUIRE(start.ok());
    REQUIRE(hub.ok());
    REQUIRE(exit.ok());
    auto road   = network.addEdge(start.value(), hub.value(), {{}, {}}, 2, 0);
    auto branch = network.addEdge(hub.value(), exit.value(), {{}, {}}, 2, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());
    const RoadLaneConnection active{
        road.value(), 0, branch.value(), 0, RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    const RoadLaneConnection blocked{
        road.value(), 1, branch.value(), 1, RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(active).ok());
    REQUIRE(network.blockLaneLink(blocked).ok());

    RoadNetworkEditTarget target("road:detach-endpoint", network);
    RoadCommandRegistry   registry;
    REQUIRE(registerRoadNetworkEditingCommands(registry).ok());
    CommandRequest request;
    request.id      = eve::editing::CommandId("road.edge.detach-endpoint.v1");
    request.payload = EditorValue::Object{{"id", std::int64_t{road.value()}}, {"fromEndpoint", false}};
    auto planned    = registry.planners.at("road.edge.detach-endpoint.v1")(target, request);
    REQUIRE(planned.ok());
    const auto operation = planned.value().operations.front();
    REQUIRE(target.applyDomainOperation(operation).ok());
    REQUIRE_EQ(network.nodeCount(), 4);
    const auto detachedNodeId = network.edgeResult(road.value()).value().to;
    CHECK_NE(detachedNodeId, hub.value());
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK_EQ(network.blockedLaneLinkCount(), 0);

    DomainOperation undo = operation;
    undo.type            = operation.inverseType;
    undo.payload         = operation.inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.nodeCount(), 3);
    CHECK_EQ(network.edgeResult(road.value()).value().to, hub.value());
    CHECK_EQ(network.laneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinkCount(), 1);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, detachedNodeId);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.gizmoUsesStableIdsAndTracksRevision") {
    RoadNetwork network;
    auto        a = network.addNode(0.f, 0.f, 0.f, 2.f);
    auto        b = network.addNode(12.f, 0.f, 0.f, 2.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge = network.addEdge(a.value(), b.value(), {{0.f, 0.f, 0.f}, {6.f, 1.f, 3.f}, {12.f, 0.f, 0.f}}, 1, 1);
    REQUIRE(edge.ok());
    RoadNetworkEditTarget target("road:gizmo", network);

    const auto before = target.gizmo();
    CHECK_EQ(before.status, EditorStatus::Applied);
    CHECK_EQ(before.target, std::string("road:gizmo"));
    CHECK_EQ(before.targetRevision, target.revision());
    bool foundNode = false, foundControl = false;
    for (const auto &primitive : before.primitives) {
        if (primitive.id == "road.node." + std::to_string(a.value())) foundNode = true;
        if (primitive.id == "road.edge." + std::to_string(edge.value()) + ".control.1") foundControl = true;
        CHECK(std::isfinite(primitive.position[0]));
        CHECK(std::isfinite(primitive.position[1]));
        CHECK(std::isfinite(primitive.position[2]));
    }
    CHECK(foundNode);
    CHECK(foundControl);

    const auto rejected = target.gizmo(before.primitives.size() - 1);
    CHECK_EQ(rejected.status, EditorStatus::Rejected);
    CHECK(rejected.primitives.empty());
    CHECK(!rejected.diagnostics.empty());

    const auto oldRevision = target.revision();
    auto       move        = target.makeMoveNode(a.value(), -2.f, 4.f, 1.f);
    REQUIRE(move.ok());
    REQUIRE(target.applyDomainOperation(move.value()).ok());
    const auto after = target.gizmo();
    CHECK_GT(after.targetRevision, oldRevision);
    bool foundMovedNode = false;
    for (const auto &primitive : after.primitives) {
        if (primitive.id != "road.node." + std::to_string(a.value())) continue;
        foundMovedNode = true;
        CHECK_EQ(primitive.position[0], -2.0);
        CHECK_EQ(primitive.position[1], 4.0);
        CHECK_EQ(primitive.position[2], 1.0);
    }
    CHECK(foundMovedNode);
}

TEST_CASE("procgen.road.editing.snapshotRoundTripPreservesStableTopology") {
    RoadNetwork source;
    auto        west  = source.addNode(-12.f, 1.f, 0.f, 3.f);
    auto        hub   = source.addNode(0.f, 1.f, 0.f, 4.f);
    auto        north = source.addNode(0.f, 2.f, -12.f, 3.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(north.ok());
    REQUIRE(source.setNodeJunctionControl(hub.value(), RoadJunctionControl::Yield).ok());
    RoadStyle style;
    style.laneWidth             = 4.1f;
    style.uvMeters              = 7.f;
    style.speedLimitMps         = 13.5f;
    style.trafficPriority       = 9;
    style.sideObjectStartOffset = 1.5f;
    style.sideObjectEndOffset   = 2.5f;
    style.sideObjectsLeft       = false;
    auto incoming =
        source.addEdge(west.value(), hub.value(), {{-12.f, 1.f, 0.f}, {-5.f, 1.5f, 2.f}, {0.f, 1.f, 0.f}}, 2, 1, style);
    auto outgoing =
        source.addEdge(hub.value(), north.value(), {{0.f, 1.f, 0.f}, {2.f, 2.f, -6.f}, {0.f, 2.f, -12.f}}, 1, 1, style);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    const RoadLaneConnection link{incoming.value(),          1, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};
    REQUIRE(source.addLaneLink(link).ok());

    RoadNetworkEditTarget sourceTarget("road:snapshot-source", source);
    EditorValue           snapshot = sourceTarget.snapshotValue();
    auto                 *object   = snapshot.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    (*object)["futureMetadata"] = std::string("ignored-by-v1-reader");

    auto destination = RoadNetwork::makeStraight(12.f, 1);
    REQUIRE(destination.ok());
    RoadNetworkEditTarget destinationTarget("road:snapshot-destination", destination.value());
    REQUIRE(destinationTarget.loadSnapshot(snapshot).ok());
    REQUIRE(destination.value().validate().ok());
    CHECK_EQ(destination.value().nodeCount(), source.nodeCount());
    CHECK_EQ(destination.value().edgeCount(), source.edgeCount());
    CHECK_EQ(destination.value().laneLinkCount(), 1);
    auto restoredHub = destination.value().nodeResult(hub.value());
    REQUIRE(restoredHub.ok());
    CHECK_EQ(restoredHub.value().junctionControl, RoadJunctionControl::Yield);
    auto restored = destination.value().edgeResult(incoming.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().controlPoints.size(), std::size_t{3});
    CHECK_EQ(restored.value().style.laneWidth, 4.1f);
    CHECK_EQ(restored.value().style.speedLimitMps, 13.5f);
    CHECK_EQ(restored.value().style.trafficPriority, 9);
    CHECK_EQ(restored.value().style.sideObjectStartOffset, 1.5f);
    CHECK_EQ(restored.value().style.sideObjectEndOffset, 2.5f);
    CHECK(!restored.value().style.sideObjectsLeft);

    EditorValue legacySnapshot = snapshot;
    auto       *legacyObject   = legacySnapshot.getIf<EditorValue::Object>();
    REQUIRE(legacyObject != nullptr);
    auto *legacyEdges = (*legacyObject)["edges"].getIf<EditorValue::Array>();
    auto *legacyNodes = (*legacyObject)["nodes"].getIf<EditorValue::Array>();
    REQUIRE(legacyEdges != nullptr);
    REQUIRE(legacyNodes != nullptr);
    for (auto &encodedNode : *legacyNodes) {
        auto *nodeObject = encodedNode.getIf<EditorValue::Object>();
        REQUIRE(nodeObject != nullptr);
        nodeObject->erase("junctionControl");
    }
    for (auto &encodedEdge : *legacyEdges) {
        auto *edgeObject = encodedEdge.getIf<EditorValue::Object>();
        REQUIRE(edgeObject != nullptr);
        auto *encodedStyle = (*edgeObject)["style"].getIf<EditorValue::Object>();
        REQUIRE(encodedStyle != nullptr);
        encodedStyle->erase("sideObjectStartOffset");
        encodedStyle->erase("sideObjectEndOffset");
        encodedStyle->erase("sideObjectsLeft");
        encodedStyle->erase("sideObjectsRight");
    }
    auto legacyDestination = RoadNetwork::makeStraight(8.f, 1);
    REQUIRE(legacyDestination.ok());
    RoadNetworkEditTarget legacyTarget("road:snapshot-v1-legacy", legacyDestination.value());
    REQUIRE(legacyTarget.loadSnapshot(legacySnapshot).ok());
    auto legacyEdge = legacyDestination.value().edgeResult(incoming.value());
    REQUIRE(legacyEdge.ok());
    CHECK_EQ(legacyEdge.value().style.sideObjectStartOffset, 0.f);
    CHECK_EQ(legacyEdge.value().style.sideObjectEndOffset, 0.f);
    CHECK(legacyEdge.value().style.sideObjectsLeft);
    CHECK(legacyEdge.value().style.sideObjectsRight);
    auto legacyHub = legacyDestination.value().nodeResult(hub.value());
    REQUIRE(legacyHub.ok());
    CHECK_EQ(legacyHub.value().junctionControl, RoadJunctionControl::Uncontrolled);

    auto nextNode = destination.value().addNode(20.f, 0.f, 0.f);
    REQUIRE(nextNode.ok());
    CHECK_GT(nextNode.value(), north.value());
}

TEST_CASE("procgen.road.editing.snapshotLoadIsAtomicOnInvalidLink") {
    auto network = RoadNetwork::makeStraight(20.f, 1);
    REQUIRE(network.ok());
    RoadNetworkEditTarget target("road:snapshot-atomic", network.value());
    EditorValue           snapshot = target.snapshotValue();
    auto                 *object   = snapshot.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    auto *links = (*object)["laneLinks"].getIf<EditorValue::Array>();
    REQUIRE(links != nullptr);
    links->push_back(EditorValue::Object{{"inEdge", std::int64_t{9999}},
                                         {"inLane", std::int64_t{0}},
                                         {"outEdge", std::int64_t{9998}},
                                         {"outLane", std::int64_t{0}},
                                         {"inDirection", std::int64_t{0}},
                                         {"outDirection", std::int64_t{0}}});
    const auto revision = network.value().revision();
    const auto edgeId   = network.value().edges().front().id;
    CHECK(!target.loadSnapshot(snapshot).ok());
    CHECK_EQ(network.value().revision(), revision);
    REQUIRE(network.value().edgeResult(edgeId).ok());
    REQUIRE(network.value().validate().ok());
}

TEST_CASE("procgen.road.editing.snapshotLoadIsAtomicOnSelfLoop") {
    auto network = RoadNetwork::makeStraight(20.f, 1);
    REQUIRE(network.ok());
    RoadNetworkEditTarget target("road:snapshot-self-loop", network.value());
    EditorValue           snapshot = target.snapshotValue();
    auto                 *object   = snapshot.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    auto *edges = (*object)["edges"].getIf<EditorValue::Array>();
    REQUIRE(edges != nullptr);
    REQUIRE_EQ(edges->size(), std::size_t{1});
    auto *edge = edges->front().getIf<EditorValue::Object>();
    REQUIRE(edge != nullptr);
    (*edge)["to"] = (*edge)["from"];

    const auto revision = network.value().revision();
    const auto edgeId   = network.value().edges().front().id;
    CHECK(!target.loadSnapshot(snapshot).ok());
    CHECK_EQ(network.value().revision(), revision);
    auto unchanged = network.value().edgeResult(edgeId);
    REQUIRE(unchanged.ok());
    CHECK_NE(unchanged.value().from, unchanged.value().to);
    REQUIRE(network.value().validate().ok());
}

TEST_CASE("procgen.road.editing.commandsDriveRegisteredTargetAndExposeSnapshot") {
    RoadCommandRegistry registry;
    REQUIRE(registerRoadNetworkEditingCommands(registry).ok());
    CHECK_EQ(registry.planners.size(), std::size_t{27});
    CHECK(registry.planners.contains("road.node.junction-control.set.v1"));
    CHECK(registry.planners.contains("road.node.merge-and-connect.v1"));
    CHECK(registry.planners.contains("road.edge.add-and-connect.v1"));
    CHECK(registry.planners.contains("road.edge.reconnect-endpoint.v1"));
    CHECK(registry.planners.contains("road.edge.reconnect-endpoint-and-connect.v1"));
    CHECK(registry.planners.contains("road.edge.snap-endpoint.v1"));
    CHECK(registry.planners.contains("road.edge.detach-endpoint.v1"));
    CHECK(registry.planners.contains("road.lane-link.block.v1"));
    CHECK(registry.planners.contains("road.lane-link.unblock.v1"));

    RoadNetwork           network;
    RoadNetworkEditTarget target("road:commands", network);
    CHECK(target.capability<IEditingSnapshotProvider>().has_value());
    CHECK_EQ(target.describe().capabilities.size(), std::size_t{1});

    auto execute = [&](const std::string &command, EditorValue payload) {
        const auto found = registry.planners.find(command);
        REQUIRE(found != registry.planners.end());
        CommandRequest request;
        request.id      = eve::editing::CommandId(command);
        request.payload = std::move(payload);
        auto planned    = found->second(target, request);
        REQUIRE(planned.ok());
        REQUIRE_EQ(planned.value().operations.size(), std::size_t{1});
        REQUIRE(target.applyDomainOperation(planned.value().operations.front()).ok());
    };

    execute("road.node.add.v1", EditorValue::Object{{"x", -10.0}, {"y", 0.0}, {"z", 0.0}});
    execute("road.node.add.v1", EditorValue::Object{{"x", 10.0}, {"y", 0.0}, {"z", 0.0}});
    REQUIRE_EQ(network.nodeCount(), 2);
    const auto a = network.nodes()[0].id;
    const auto b = network.nodes()[1].id;
    execute("road.node.radius.set.v1", EditorValue::Object{{"id", std::int64_t{a}}, {"junctionRadius", 4.5}});
    CHECK_EQ(network.nodeResult(a).value().junctionRadius, 4.5f);
    execute("road.node.junction-control.set.v1",
            EditorValue::Object{{"id", std::int64_t{a}}, {"junctionControl", std::int64_t{3}}});
    CHECK_EQ(network.nodeResult(a).value().junctionControl, RoadJunctionControl::Signal);
    execute("road.edge.add.v1",
            EditorValue::Object{
                {"from", std::int64_t{a}},
                {"to", std::int64_t{b}},
                {"lanesForward", std::int64_t{2}},
                {"lanesBackward", std::int64_t{1}},
                {"points", EditorValue::Array{EditorValue::Array{-10.0, 0.0, 0.0}, EditorValue::Array{0.0, 0.0, 3.0},
                                              EditorValue::Array{10.0, 0.0, 0.0}}}});
    REQUIRE_EQ(network.edgeCount(), 1);
    REQUIRE(network.validate().ok());
    EditorValue styleSnapshot       = target.snapshotValue();
    auto       *styleSnapshotObject = styleSnapshot.getIf<EditorValue::Object>();
    REQUIRE(styleSnapshotObject != nullptr);
    auto *styleEdges = (*styleSnapshotObject)["edges"].getIf<EditorValue::Array>();
    REQUIRE(styleEdges != nullptr);
    REQUIRE_EQ(styleEdges->size(), std::size_t{1});
    auto *styleEdge = styleEdges->front().getIf<EditorValue::Object>();
    REQUIRE(styleEdge != nullptr);
    auto *stylePayload = (*styleEdge)["style"].getIf<EditorValue::Object>();
    REQUIRE(stylePayload != nullptr);
    (*stylePayload)["laneWidth"]             = 4.25;
    (*stylePayload)["sideObjectStartOffset"] = 2.0;
    (*stylePayload)["sideObjectsRight"]      = false;
    execute("road.edge.style.set.v1", EditorValue(*stylePayload));
    CHECK_EQ(network.edges().front().style.laneWidth, 4.25f);
    CHECK_EQ(network.edges().front().style.sideObjectStartOffset, 2.f);
    CHECK(!network.edges().front().style.sideObjectsRight);
    execute("road.edge.control-points.set.v1",
            EditorValue::Object{
                {"id", std::int64_t{network.edges().front().id}},
                {"points", EditorValue::Array{EditorValue::Array{-10.0, 0.0, 0.0}, EditorValue::Array{-4.0, 1.0, 5.0},
                                              EditorValue::Array{4.0, 1.0, 5.0}, EditorValue::Array{10.0, 0.0, 0.0}}}});
    REQUIRE_EQ(network.edges().front().controlPoints.size(), std::size_t{4});
    CHECK_EQ(network.edges().front().controlPoints[1].z, 5.f);
    CHECK_EQ(network.edges().front().controlPoints.front().x, -10.f);
    CHECK_EQ(network.edges().front().controlPoints.back().x, 10.f);
    const auto commandEdgeId = network.edges().front().id;
    const auto commandFrom   = network.edges().front().from;
    const auto commandTo     = network.edges().front().to;
    execute("road.edge.reverse.v1", EditorValue::Object{{"id", std::int64_t{commandEdgeId}}});
    CHECK_EQ(network.edgeResult(commandEdgeId).value().from, commandTo);
    execute("road.edge.reverse.v1", EditorValue::Object{{"id", std::int64_t{commandEdgeId}}});
    CHECK_EQ(network.edgeResult(commandEdgeId).value().from, commandFrom);
    execute("road.edge.lanes.set.v1", EditorValue::Object{{"id", std::int64_t{network.edges().front().id}},
                                                          {"lanesForward", std::int64_t{1}},
                                                          {"lanesBackward", std::int64_t{0}}});
    CHECK_EQ(network.edges().front().lanesForward, 1);
    CHECK_EQ(network.edges().front().lanesBackward, 0);
    execute("road.node.add.v1", EditorValue::Object{{"x", 20.0}, {"y", 0.0}, {"z", 10.0}});
    const auto c = network.nodes().back().id;
    execute("road.edge.add.v1",
            EditorValue::Object{
                {"from", std::int64_t{b}},
                {"to", std::int64_t{c}},
                {"lanesForward", std::int64_t{1}},
                {"lanesBackward", std::int64_t{0}},
                {"points", EditorValue::Array{EditorValue::Array{10.0, 0.0, 0.0}, EditorValue::Array{15.0, 0.0, 4.0},
                                              EditorValue::Array{20.0, 0.0, 10.0}}}});
    const auto branchEdge = network.edges().back().id;
    execute("road.node.connect-all-turns.v1", EditorValue::Object{{"id", std::int64_t{b}}});
    CHECK_EQ(network.laneLinkCount(), 1);
    execute("road.node.add.v1", EditorValue::Object{{"x", 10.0}, {"y", 0.0}, {"z", 12.0}});
    const auto branchSource = network.nodes().back().id;
    execute("road.node.connect-at-position.v1", EditorValue::Object{{"sourceNodeId", std::int64_t{branchSource}},
                                                                    {"edgeId", std::int64_t{branchEdge}},
                                                                    {"x", 15.0},
                                                                    {"y", 1.0},
                                                                    {"z", 4.0},
                                                                    {"maxDistance", 2.0},
                                                                    {"junctionRadius", 4.0}});
    CHECK_EQ(network.nodeCount(), 5);
    CHECK_EQ(network.edgeCount(), 4);
    CHECK_EQ(network.laneLinkCount(), 3);
    REQUIRE(network.validate().ok());
    execute("road.edge.reconnect-endpoint-and-connect.v1",
            EditorValue::Object{
                {"id", std::int64_t{commandEdgeId}}, {"fromEndpoint", true}, {"nodeId", std::int64_t{branchSource}}});
    CHECK_EQ(network.edgeResult(commandEdgeId).value().from, branchSource);
    REQUIRE(network.validate().ok());
    execute("road.node.add.v1", EditorValue::Object{{"x", 30.0}, {"y", 0.0}, {"z", 0.0}});
    execute("road.node.add.v1", EditorValue::Object{{"x", 30.25}, {"y", 0.0}, {"z", 0.0}});
    const auto mergeKeep   = network.nodes()[network.nodes().size() - 2].id;
    const auto mergeRemove = network.nodes().back().id;
    execute("road.node.merge.v1", EditorValue::Object{{"keepNodeId", std::int64_t{mergeKeep}},
                                                      {"removeNodeId", std::int64_t{mergeRemove}},
                                                      {"maxDistance", 0.5}});
    CHECK_EQ(network.nodeCount(), 6);
    CHECK(!network.nodeResult(mergeRemove).ok());
    const auto  snapshot = target.capability<IEditingSnapshotProvider>()->get().snapshotValue();
    const auto *schema   = snapshot.getIf<EditorValue::Object>();
    REQUIRE(schema != nullptr);
    REQUIRE((*schema).find("schema") != (*schema).end());

    auto removed = registry.unregisterOwner("procgen_editing.road");
    REQUIRE(removed.ok());
    CHECK_EQ(removed.value(), std::size_t{14});
    CHECK(registry.planners.empty());
}

TEST_CASE("procgen.road.editing.laneCountShrinkPrunesAndUndoRestoresLinks") {
    RoadNetwork network;
    auto        west = network.addNode(-20.f, 0.f, 0.f);
    auto        hub  = network.addNode(0.f, 0.f, 0.f);
    auto        east = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    auto incoming = network.addEdge(west.value(), hub.value(), {{-20.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 4, 0);
    auto outgoing = network.addEdge(hub.value(), east.value(), {{0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 4, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    REQUIRE_EQ(network.laneLinkCount(), 4);
    const RoadLaneConnection blockedOuterTurn{
        incoming.value(), 3, outgoing.value(), 3, RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.blockLaneLink(blockedOuterTurn).ok());
    REQUIRE_EQ(network.laneLinkCount(), 3);
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);

    RoadNetworkEditTarget target("road:lane-count", network);
    auto                  shrink = target.makeSetEdgeLaneCounts(outgoing.value(), 2, 0);
    REQUIRE(shrink.ok());
    const auto beforeRevision = network.revision();
    REQUIRE(target.applyDomainOperation(shrink.value()).ok());
    CHECK_EQ(network.revision(), beforeRevision + 1);
    CHECK_EQ(network.edgeResult(outgoing.value()).value().lanesForward, 2);
    CHECK_EQ(network.laneLinkCount(), 2);
    CHECK_EQ(network.blockedLaneLinkCount(), 0);
    for (const auto &link : network.laneLinks()) CHECK_LT(link.outLane, 2);
    REQUIRE(network.validate().ok());

    DomainOperation undo = shrink.value();
    undo.payload         = shrink.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.edgeResult(outgoing.value()).value().lanesForward, 4);
    CHECK_EQ(network.laneLinkCount(), 3);
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinks().front().outLane, 3);
    REQUIRE(network.validate().ok());

    const auto stableRevision = network.revision();
    CHECK(!network.setEdgeLaneCounts(outgoing.value(), 0, 0).ok());
    CHECK_EQ(network.revision(), stableRevision);
    CHECK_EQ(network.laneLinkCount(), 3);
    CHECK_EQ(network.blockedLaneLinkCount(), 1);
}

TEST_CASE("procgen.road.editing.connectAllTurnsIsAtomicAndUndoable") {
    RoadNetwork network;
    auto        west  = network.addNode(-12.f, 0.f, 0.f);
    auto        hub   = network.addNode(0.f, 0.f, 0.f);
    auto        north = network.addNode(0.f, 0.f, -12.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(north.ok());
    auto horizontal = network.addEdge(west.value(), hub.value(), {{-12.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 1);
    auto vertical   = network.addEdge(hub.value(), north.value(), {{0.f, 0.f, 0.f}, {0.f, 0.f, -12.f}}, 1, 1);
    REQUIRE(horizontal.ok());
    REQUIRE(vertical.ok());

    RoadNetworkEditTarget target("road:auto-turns", network);
    auto                  connect = target.makeConnectAllTurns(hub.value());
    REQUIRE(connect.ok());
    REQUIRE(target.applyDomainOperation(connect.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 2);
    REQUIRE(network.validate().ok());

    const auto afterConnectRevision = network.revision();
    CHECK(!target.applyDomainOperation(connect.value()).ok());
    CHECK_EQ(network.revision(), afterConnectRevision);
    CHECK_EQ(network.laneLinkCount(), 2);

    DomainOperation undo = connect.value();
    undo.type            = connect.value().inverseType;
    undo.payload         = connect.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.laneLinkCount(), 0);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(connect.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 2);
    CHECK_EQ(static_cast<int>(target.makeConnectAllTurns(hub.value()).code()), static_cast<int>(EditorStatus::NoOp));
}

TEST_CASE("procgen.road.editing.splitEdgeUndoRestoresOriginalIdentityAndLinks") {
    RoadNetwork network;
    auto        a = network.addNode(-16.f, 0.f, 0.f);
    auto        b = network.addNode(16.f, 0.f, 0.f);
    auto        c = network.addNode(16.f, 0.f, -16.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    auto road = network.addEdge(a.value(), b.value(), {{-16.f, 0.f, 0.f}, {0.f, 1.f, 4.f}, {16.f, 0.f, 0.f}}, 2, 1);
    auto exit = network.addEdge(b.value(), c.value(), {{16.f, 0.f, 0.f}, {16.f, 0.f, -16.f}}, 1, 0);
    REQUIRE(road.ok());
    REQUIRE(exit.ok());
    const RoadLaneConnection endpointLink{
        road.value(), 0, exit.value(), 0, RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(endpointLink).ok());
    const RoadLaneConnection blockedEndpointLink{
        road.value(), 1, exit.value(), 0, RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.blockLaneLink(blockedEndpointLink).ok());
    const int originalNodes = network.nodeCount(), originalEdges = network.edgeCount();

    RoadNetworkEditTarget target("road:split", network);
    auto                  split = target.makeSplitEdge(road.value(), 1, 3.5f);
    REQUIRE(split.ok());
    REQUIRE(target.applyDomainOperation(split.value()).ok());
    CHECK_EQ(network.nodeCount(), originalNodes + 1);
    CHECK_EQ(network.edgeCount(), originalEdges + 1);
    REQUIRE(network.validate().ok());
    const auto continuation = std::find_if(network.edges().begin(), network.edges().end(), [&](const auto &edge) {
        return edge.id != road.value() && edge.id != exit.value();
    });
    REQUIRE(continuation != network.edges().end());
    const std::uint32_t continuationId = continuation->id;
    const std::uint32_t insertedNodeId = continuation->from;
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinks().front().inEdge, continuationId);

    DomainOperation undo = split.value();
    undo.type            = split.value().inverseType;
    undo.payload         = split.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.nodeCount(), originalNodes);
    CHECK_EQ(network.edgeCount(), originalEdges);
    CHECK_EQ(network.laneLinkCount(), 1);
    auto restored = network.edgeResult(road.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().from, a.value());
    CHECK_EQ(restored.value().to, b.value());
    CHECK_EQ(restored.value().controlPoints.size(), std::size_t{3});
    CHECK_EQ(network.laneLinks().front().inEdge, road.value());
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinks().front().inEdge, road.value());
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(split.value()).ok());
    REQUIRE(network.nodeResult(insertedNodeId).ok());
    REQUIRE(network.edgeResult(continuationId).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, insertedNodeId);
    CHECK_EQ(network.edgeResult(continuationId).value().from, insertedNodeId);
    CHECK_EQ(network.edgeResult(continuationId).value().to, b.value());
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinks().front().inEdge, continuationId);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.positionSplitUndoRedoKeepsProjectedPointAndIds") {
    RoadNetwork network;
    auto        from = network.addNode(-12.f, 4.f, 0.f);
    auto        to   = network.addNode(12.f, 4.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto road = network.addEdge(from.value(), to.value(), {{-12.f, 4.f, 0.f}, {12.f, 4.f, 0.f}}, 1, 0);
    REQUIRE(road.ok());

    RoadNetworkEditTarget target("road:position-split", network);
    auto                  operation = target.makeSplitEdgeAtPosition(road.value(), {3.f, 5.f, 2.f}, 3.f, 4.f);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.nodeCount(), 3);
    REQUIRE_EQ(network.edgeCount(), 2);
    const auto continuation = std::find_if(network.edges().begin(), network.edges().end(),
                                           [&](const auto &edge) { return edge.id != road.value(); });
    REQUIRE(continuation != network.edges().end());
    const auto continuationId = continuation->id;
    const auto insertedNodeId = continuation->from;
    auto       inserted       = network.nodeResult(insertedNodeId);
    REQUIRE(inserted.ok());
    CHECK(std::fabs(inserted.value().x - 3.f) < 1e-5f);
    CHECK(std::fabs(inserted.value().y - 4.f) < 1e-5f);
    CHECK(std::fabs(inserted.value().z) < 1e-5f);

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE_EQ(network.nodeCount(), 2);
    REQUIRE_EQ(network.edgeCount(), 1);
    CHECK_EQ(network.edgeResult(road.value()).value().controlPoints.size(), std::size_t{2});

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(network.nodeResult(insertedNodeId).ok());
    REQUIRE(network.edgeResult(continuationId).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, insertedNodeId);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.mergeNodesUndoRestoresEdgesAndLaneLinks") {
    RoadNetwork network;
    auto        keep    = network.addNode(0.f, 0.f, 0.f);
    auto        removed = network.addNode(0.2f, 0.f, 0.f);
    auto        north   = network.addNode(0.2f, 0.f, 10.f);
    auto        east    = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(keep.ok());
    REQUIRE(removed.ok());
    REQUIRE(north.ok());
    REQUIRE(east.ok());
    auto incoming = network.addEdge(north.value(), removed.value(), {{}, {}}, 1, 0);
    auto outgoing = network.addEdge(removed.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    const RoadLaneConnection link{incoming.value(),          0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(link).ok());

    RoadNetworkEditTarget target("road:merge", network);
    auto                  operation = target.makeMergeNodes(keep.value(), removed.value(), 0.5f);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 3);
    CHECK_EQ(network.edgeResult(incoming.value()).value().to, keep.value());
    CHECK_EQ(network.edgeResult(outgoing.value()).value().from, keep.value());
    CHECK_EQ(network.laneLinkCount(), 1);
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.nodeCount(), 4);
    auto restoredNode = network.nodeResult(removed.value());
    REQUIRE(restoredNode.ok());
    CHECK(std::fabs(restoredNode.value().x - 0.2f) < 1e-5f);
    CHECK_EQ(network.edgeResult(incoming.value()).value().to, removed.value());
    CHECK_EQ(network.edgeResult(outgoing.value()).value().from, removed.value());
    CHECK_EQ(network.laneLinkCount(), 1);
    CHECK_EQ(network.laneLinks().front().inEdge, incoming.value());
    CHECK_EQ(network.laneLinks().front().outEdge, outgoing.value());
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK(!network.nodeResult(removed.value()).ok());
    CHECK_EQ(network.edgeResult(incoming.value()).value().to, keep.value());
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.mergeNodesAndConnectIsAtomicAndUndoable") {
    RoadNetwork network;
    auto        keep    = network.addNode(0.f, 0.f, 0.f);
    auto        removed = network.addNode(0.2f, 0.f, 0.f);
    auto        west    = network.addNode(-10.f, 0.f, 0.f);
    auto        north   = network.addNode(0.2f, 0.f, -10.f);
    REQUIRE(keep.ok());
    REQUIRE(removed.ok());
    REQUIRE(west.ok());
    REQUIRE(north.ok());
    auto incoming = network.addEdge(west.value(), keep.value(), {{}, {}}, 1, 0);
    auto outgoing = network.addEdge(removed.value(), north.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    REQUIRE_EQ(network.laneLinkCount(), 0);

    RoadNetworkEditTarget target("road:merge-connect", network);
    auto                  operation = target.makeMergeNodesAndConnect(keep.value(), removed.value(), 0.5f);
    REQUIRE(operation.ok());
    CHECK_EQ(operation.value().type, RoadNetworkEditTarget::kMergeNodesAndConnect);
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK(!network.nodeResult(removed.value()).ok());
    CHECK_EQ(network.edgeResult(outgoing.value()).value().from, keep.value());
    REQUIRE_EQ(network.laneLinkCount(), 1);
    CHECK_EQ(network.laneLinks().front().inEdge, incoming.value());
    CHECK_EQ(network.laneLinks().front().outEdge, outgoing.value());
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE(network.nodeResult(removed.value()).ok());
    CHECK_EQ(network.edgeResult(outgoing.value()).value().from, removed.value());
    CHECK_EQ(network.laneLinkCount(), 0);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK(!network.nodeResult(removed.value()).ok());
    REQUIRE_EQ(network.laneLinkCount(), 1);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.addEdgeAndConnectTouchesOnlyNewEdgeRoutes") {
    RoadNetwork network;
    auto        west  = network.addNode(-10.f, 0.f, 0.f);
    auto        from  = network.addNode(0.f, 0.f, 0.f);
    auto        to    = network.addNode(10.f, 0.f, 0.f);
    auto        east  = network.addNode(20.f, 0.f, 0.f);
    auto        north = network.addNode(0.f, 0.f, -10.f);
    REQUIRE(west.ok());
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    REQUIRE(east.ok());
    REQUIRE(north.ok());
    auto incoming          = network.addEdge(west.value(), from.value(), {{}, {}}, 1, 0);
    auto unrelatedOutgoing = network.addEdge(from.value(), north.value(), {{}, {}}, 1, 0);
    auto outgoing          = network.addEdge(to.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(unrelatedOutgoing.ok());
    REQUIRE(outgoing.ok());
    REQUIRE_EQ(network.laneLinkCount(), 0);

    RoadNetworkEditTarget target("road:add-connect", network);
    auto                  operation = target.makeAddEdgeAndConnect(from.value(), to.value(), {{}, {}}, 1, 0);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.edgeCount(), 4);
    const auto newEdgeId = network.edges().back().id;
    REQUIRE_EQ(network.laneLinkCount(), 2);
    CHECK(std::all_of(network.laneLinks().begin(), network.laneLinks().end(),
                      [&](const auto &link) { return link.inEdge == newEdgeId || link.outEdge == newEdgeId; }));
    CHECK(std::none_of(network.laneLinks().begin(), network.laneLinks().end(), [&](const auto &link) {
        return link.inEdge == incoming.value() && link.outEdge == unrelatedOutgoing.value();
    }));
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK(!network.edgeResult(newEdgeId).ok());
    CHECK_EQ(network.laneLinkCount(), 0);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(network.edgeResult(newEdgeId).ok());
    REQUIRE_EQ(network.laneLinkCount(), 2);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.connectNodeAtPositionIsAtomicAndUndoable") {
    RoadNetwork network;
    auto        west   = network.addNode(-12.f, 0.f, 0.f);
    auto        east   = network.addNode(12.f, 0.f, 0.f);
    auto        exit   = network.addNode(12.f, 0.f, -10.f);
    auto        source = network.addNode(0.f, 0.f, 10.f);
    REQUIRE(west.ok());
    REQUIRE(east.ok());
    REQUIRE(exit.ok());
    REQUIRE(source.ok());
    auto mainRoad = network.addEdge(west.value(), east.value(), {{-12.f, 0.f, 0.f}, {12.f, 0.f, 0.f}}, 1, 0);
    auto exitRoad = network.addEdge(east.value(), exit.value(), {{}, {}}, 1, 0);
    REQUIRE(mainRoad.ok());
    REQUIRE(exitRoad.ok());
    const RoadLaneConnection endpointLink{mainRoad.value(),          0, exitRoad.value(), 0, RoadLaneDirection::Forward,
                                          RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(endpointLink).ok());

    RoadNetworkEditTarget target("road:connect-position", network);
    const auto            originalRevision = network.revision();
    auto invalid = target.makeConnectNodeAtPosition(source.value(), mainRoad.value(), {0.f, 8.f, 0.f}, 1.f);
    CHECK(!invalid.ok());
    CHECK_EQ(network.revision(), originalRevision);

    auto operation =
        target.makeConnectNodeAtPosition(source.value(), mainRoad.value(), {2.f, 1.f, 0.f}, 2.f, 3.5f, 1, 1);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 5);
    CHECK_EQ(network.edgeCount(), 4);
    CHECK_EQ(network.laneLinkCount(), 4);
    REQUIRE(network.validate().ok());
    const auto branch = std::find_if(network.edges().begin(), network.edges().end(),
                                     [&](const auto &edge) { return edge.from == source.value(); });
    REQUIRE(branch != network.edges().end());
    const auto branchId       = branch->id;
    const auto insertedNodeId = branch->to;
    CHECK_EQ(branch->lanesForward, 1);
    CHECK_EQ(branch->lanesBackward, 1);

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.nodeCount(), 4);
    CHECK_EQ(network.edgeCount(), 2);
    CHECK_EQ(network.laneLinkCount(), 1);
    CHECK(!network.nodeResult(insertedNodeId).ok());
    CHECK(!network.edgeResult(branchId).ok());
    CHECK_EQ(network.edgeResult(mainRoad.value()).value().controlPoints.size(), std::size_t{2});
    CHECK_EQ(network.laneLinks().front().inEdge, mainRoad.value());
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(network.nodeResult(insertedNodeId).ok());
    REQUIRE(network.edgeResult(branchId).ok());
    CHECK_EQ(network.edgeResult(branchId).value().to, insertedNodeId);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.connectNodeAtPositionSnapsToVisibleCurve") {
    RoadNetwork network;
    auto        curveStart = network.addNode(-10.f, 0.f, -10.f);
    auto        curveEnd   = network.addNode(10.f, 0.f, -10.f);
    auto        source     = network.addNode(0.f, 0.f, 20.f);
    REQUIRE(curveStart.ok());
    REQUIRE(curveEnd.ok());
    REQUIRE(source.ok());
    auto curve = network.addEdge(curveStart.value(), curveEnd.value(),
                                 {{-10.f, 0.f, -10.f}, {-3.f, 0.f, 10.f}, {3.f, 0.f, 10.f}, {10.f, 0.f, -10.f}}, 1, 0);
    REQUIRE(curve.ok());

    RoadNetworkEditTarget target("road:curve-branch", network);
    auto                  operation =
        target.makeConnectNodeAtPosition(source.value(), curve.value(), {0.f, 0.f, 12.5f}, 0.2f, 2.f, 1, 0);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 4);
    CHECK_EQ(network.edgeCount(), 3);
    const auto junction = std::find_if(network.nodes().begin(), network.nodes().end(),
                                       [](const auto &node) { return std::fabs(node.x) < 1e-3f && node.z > 12.f; });
    REQUIRE(junction != network.nodes().end());
    const auto junctionId = junction->id;
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.nodeCount(), 3);
    CHECK_EQ(network.edgeCount(), 1);
    CHECK(!network.nodeResult(junctionId).ok());
    CHECK_EQ(network.edgeResult(curve.value()).value().controlPoints.size(), std::size_t{4});
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(network.nodeResult(junctionId).ok());
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.crossingEdgesBecomeOneUndoableJunction") {
    RoadNetwork network;
    auto        west  = network.addNode(-10.f, 0.f, 0.f);
    auto        east  = network.addNode(10.f, 0.f, 0.f);
    auto        north = network.addNode(0.f, 0.2f, -10.f);
    auto        south = network.addNode(0.f, 0.2f, 10.f);
    REQUIRE(west.ok());
    REQUIRE(east.ok());
    REQUIRE(north.ok());
    REQUIRE(south.ok());
    auto horizontal = network.addEdge(west.value(), east.value(), {{}, {}}, 1, 0);
    auto vertical   = network.addEdge(north.value(), south.value(), {{}, {}}, 1, 0);
    REQUIRE(horizontal.ok());
    REQUIRE(vertical.ok());

    RoadNetworkEditTarget target("road:intersection", network);
    const auto            originalRevision = network.revision();
    auto                  tooFar = target.makeConnectEdgeIntersection(horizontal.value(), vertical.value(), 0.1f, 3.f);
    CHECK(!tooFar.ok());
    CHECK_EQ(network.revision(), originalRevision);

    RoadNetwork ambiguous;
    auto        zigStart  = ambiguous.addNode(-10.f, 0.f, -5.f);
    auto        zigEnd    = ambiguous.addNode(10.f, 0.f, 5.f);
    auto        lineStart = ambiguous.addNode(0.f, 0.f, -10.f);
    auto        lineEnd   = ambiguous.addNode(0.f, 0.f, 10.f);
    REQUIRE(zigStart.ok());
    REQUIRE(zigEnd.ok());
    REQUIRE(lineStart.ok());
    REQUIRE(lineEnd.ok());
    auto zig  = ambiguous.addEdge(zigStart.value(), zigEnd.value(),
                                  {{-10.f, 0.f, -5.f}, {10.f, 0.f, -5.f}, {-10.f, 0.f, 5.f}, {10.f, 0.f, 5.f}}, 1, 0);
    auto line = ambiguous.addEdge(lineStart.value(), lineEnd.value(), {{}, {}}, 1, 0);
    REQUIRE(zig.ok());
    REQUIRE(line.ok());
    RoadNetworkEditTarget ambiguousTarget("road:ambiguous-intersection", ambiguous);
    const auto            ambiguousRevision = ambiguous.revision();
    CHECK(!ambiguousTarget.makeConnectEdgeIntersection(zig.value(), line.value(), 0.1f, 3.f).ok());
    CHECK_EQ(ambiguous.revision(), ambiguousRevision);

    RoadCommandRegistry registry;
    REQUIRE(registerRoadNetworkEditingCommands(registry).ok());
    const auto planner = registry.planners.find("road.edge.connect-intersection.v1");
    REQUIRE(planner != registry.planners.end());
    CommandRequest request;
    request.id      = eve::editing::CommandId("road.edge.connect-intersection.v1");
    request.payload = EditorValue::Object{{"firstEdgeId", std::int64_t{horizontal.value()}},
                                          {"secondEdgeId", std::int64_t{vertical.value()}},
                                          {"maximumHeightDelta", 0.25},
                                          {"junctionRadius", 3.0}};
    auto plan       = planner->second(target, request);
    REQUIRE(plan.ok());
    REQUIRE_EQ(plan.value().operations.size(), std::size_t{1});
    DomainOperation operation = plan.value().operations.front();
    REQUIRE(target.applyDomainOperation(operation).ok());
    CHECK_EQ(network.nodeCount(), 5);
    CHECK_EQ(network.edgeCount(), 4);
    CHECK_EQ(network.laneLinkCount(), 4);
    REQUIRE(network.validate().ok());
    const auto junction = std::find_if(network.nodes().begin(), network.nodes().end(), [](const auto &node) {
        return std::fabs(node.x) < 1e-5f && std::fabs(node.z) < 1e-5f;
    });
    REQUIRE(junction != network.nodes().end());
    const auto junctionId = junction->id;
    CHECK(std::fabs(junction->y - 0.1f) < 1e-5f);

    DomainOperation undo = operation;
    undo.type            = operation.inverseType;
    undo.payload         = operation.inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.nodeCount(), 4);
    CHECK_EQ(network.edgeCount(), 2);
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK(!network.nodeResult(junctionId).ok());
    CHECK_EQ(network.edgeResult(horizontal.value()).value().controlPoints.size(), std::size_t{2});
    CHECK_EQ(network.edgeResult(vertical.value()).value().controlPoints.size(), std::size_t{2});
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation).ok());
    REQUIRE(network.nodeResult(junctionId).ok());
    CHECK_EQ(network.laneLinkCount(), 4);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.curvedVisualCrossingUsesBakedSpline") {
    RoadNetwork network;
    auto        curveStart = network.addNode(-10.f, 0.f, -10.f);
    auto        curveEnd   = network.addNode(10.f, 0.f, -10.f);
    auto        lineStart  = network.addNode(0.f, 0.f, 11.f);
    auto        lineEnd    = network.addNode(0.f, 0.f, 14.f);
    REQUIRE(curveStart.ok());
    REQUIRE(curveEnd.ok());
    REQUIRE(lineStart.ok());
    REQUIRE(lineEnd.ok());
    auto curve = network.addEdge(curveStart.value(), curveEnd.value(),
                                 {{-10.f, 0.f, -10.f}, {-3.f, 0.f, 10.f}, {3.f, 0.f, 10.f}, {10.f, 0.f, -10.f}}, 1, 0);
    auto line  = network.addEdge(lineStart.value(), lineEnd.value(), {{}, {}}, 1, 0);
    REQUIRE(curve.ok());
    REQUIRE(line.ok());

    RoadNetworkEditTarget target("road:curved-intersection", network);
    auto                  operation = target.makeConnectEdgeIntersection(curve.value(), line.value(), 0.1f, 2.f);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 5);
    CHECK_EQ(network.edgeCount(), 4);
    CHECK_EQ(network.laneLinkCount(), 4);
    const auto junction = std::find_if(network.nodes().begin(), network.nodes().end(), [](const auto &node) {
        return std::fabs(node.x) < 1e-3f && node.z > 10.f && node.z < 14.f;
    });
    REQUIRE(junction != network.nodes().end());
    const auto junctionId = junction->id;
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.nodeCount(), 4);
    CHECK_EQ(network.edgeCount(), 2);
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK_EQ(network.edgeResult(curve.value()).value().controlPoints.size(), std::size_t{4});
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(network.nodeResult(junctionId).ok());
    REQUIRE(network.validate().ok());
}
}  // namespace test_procgen_road_editing
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_simulation)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileLayer.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/JsonExport.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_simulation.cpp
namespace test_procgen_simulation {
// Procgen cross-module scenarios — the same surfaces a game would drive:
//   1) WFC dungeon → TileLayer palette → A* / FlowField spawn→stairs delve
//   2) Marching Cubes terrain island + crystal props → mesh budgets (+ optional GPU)
//   3) WFC cave → A* expedition + FOV shadowcast
//   4) WFC terrain overworld → weighted biome travel + seed replay
//   5) Marching Cubes torus/sphere loot prop bake + cache bust


using namespace eve::procgen;
using namespace eve::map;
using namespace eve::graphics;

namespace {

struct ParamsLease {
    ProcgenParamsHandleRef handle{};
    ParamsLease() = default;
    explicit ParamsLease(ProcgenParamsHandleRef value) : handle(value) {}
    ParamsLease(const ParamsLease &)            = delete;
    ParamsLease &operator=(const ParamsLease &) = delete;
    ParamsLease(ParamsLease &&other) noexcept : handle(other.handle) { other.handle = {}; }
    ParamsLease &operator=(ParamsLease &&other) noexcept {
        if (this == &other) return *this;
        reset();
        handle       = other.handle;
        other.handle = {};
        return *this;
    }
    ~ParamsLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("simulation params cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Params> view() const noexcept { return Procgen::resolve(handle); }
};

struct GridLease {
    ProcgenGridHandleRef handle{};
    GridLease() = default;
    explicit GridLease(ProcgenGridHandleRef value) : handle(value) {}
    GridLease(const GridLease &)            = delete;
    GridLease &operator=(const GridLease &) = delete;
    GridLease(GridLease &&other) noexcept : handle(other.handle) { other.handle = {}; }
    GridLease &operator=(GridLease &&other) noexcept {
        if (this == &other) return *this;
        reset();
        handle       = other.handle;
        other.handle = {};
        return *this;
    }
    ~GridLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("simulation grid cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Grid2D> view() const noexcept { return Procgen::resolve(handle); }
};

struct OutputLease {
    Procgen               *owner = nullptr;
    ProcgenOutputHandleRef handle{};
    OutputLease() = default;
    OutputLease(Procgen &proc, ProcgenOutputHandleRef value) : owner(&proc), handle(value) {}
    OutputLease(const OutputLease &)            = delete;
    OutputLease &operator=(const OutputLease &) = delete;
    OutputLease(OutputLease &&other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    OutputLease &operator=(OutputLease &&other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~OutputLease() { reset(); }
    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releaseOutput(handle);
        result.ignore("simulation output cleanup");
        owner  = nullptr;
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<OutputSpec> view() const noexcept {
        return owner ? owner->resolveOutput(handle) : eve::script::Borrowed<OutputSpec>();
    }
};

OutputLease requireOutput(Procgen &proc) {
    auto result = proc.newOutputHandle();
    REQUIRE(result.ok());
    return OutputLease(proc, std::move(result).takeValue());
}

struct MeshBuildLease {
    Procgen                  *owner = nullptr;
    ProcgenMeshBuildHandleRef handle{};
    MeshBuildLease() = default;
    MeshBuildLease(Procgen &proc, ProcgenMeshBuildHandleRef value) : owner(&proc), handle(value) {}
    MeshBuildLease(const MeshBuildLease &)            = delete;
    MeshBuildLease &operator=(const MeshBuildLease &) = delete;
    MeshBuildLease(MeshBuildLease &&other) noexcept : owner(other.owner), handle(other.handle) {
        other.owner  = nullptr;
        other.handle = {};
    }
    MeshBuildLease &operator=(MeshBuildLease &&other) noexcept {
        if (this == &other) return *this;
        reset();
        owner        = other.owner;
        handle       = other.handle;
        other.owner  = nullptr;
        other.handle = {};
        return *this;
    }
    ~MeshBuildLease() { reset(); }
    void reset() noexcept {
        if (!owner || !handle.isValid()) return;
        auto result = owner->releaseMeshBuild(handle);
        result.ignore("simulation mesh build cleanup");
        owner  = nullptr;
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<MeshBuild> view() const noexcept {
        return owner ? owner->resolveMeshBuild(handle) : eve::script::Borrowed<MeshBuild>();
    }
};

ParamsLease requireParams(const Params &source) {
    auto result = Procgen::newParamsHandle();
    REQUIRE(result.ok());
    auto lease = ParamsLease(std::move(result).takeValue());
    auto view  = lease.view();
    REQUIRE(view.isBound());
    *view = source;
    return lease;
}

GridLease requireGrid(Procgen &proc, const std::string &algorithm, ProcgenParamsHandleRef params) {
    auto result = proc.generateHandle(algorithm, params);
    REQUIRE(result.ok());
    return GridLease(std::move(result).takeValue());
}

MeshBuildLease requireMeshBuild(Procgen &proc, const std::string &recipe, ProcgenParamsHandleRef params) {
    auto result = proc.buildMeshHandle(recipe, params);
    REQUIRE(result.ok());
    return MeshBuildLease(proc, std::move(result).takeValue());
}

constexpr int kWallGid     = 1;
constexpr int kFloorGid    = 2;
constexpr int kCorridorGid = 3;
constexpr int kDoorGid     = 4;

bool approxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

void bindDungeonPalette(Procgen *mod, const std::string &name) {
    mod->setPaletteGid(name, "wall", kWallGid);
    mod->setPaletteGid(name, "floor", kFloorGid);
    mod->setPaletteGid(name, "corridor", kCorridorGid);
    mod->setPaletteGid(name, "door", kDoorGid);
}

bool borderIsWallLike(const Grid2D &g) {
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int x = 0; x < w; ++x) {
        if (g.getCell(x, 0) != int(Semantic::Wall)) return false;
        if (g.getCell(x, h - 1) != int(Semantic::Wall)) return false;
    }
    for (int y = 0; y < h; ++y) {
        if (g.getCell(0, y) != int(Semantic::Wall)) return false;
        if (g.getCell(w - 1, y) != int(Semantic::Wall)) return false;
    }
    return true;
}

bool terrainAdjacencyOk(const Grid2D &g) {
    static const int              band[] = {int(Semantic::Water), int(Semantic::Sand),  int(Semantic::Grass),
                                            int(Semantic::Dirt),  int(Semantic::Stone), int(Semantic::Snow)};
    std::set<std::pair<int, int>> allowed;
    for (int i = 0; i < 6; ++i) {
        allowed.insert({band[i], band[i]});
        if (i + 1 < 6) {
            int lo = band[i], hi = band[i + 1];
            if (lo > hi) std::swap(lo, hi);
            allowed.insert({lo, hi});
        }
    }
    auto ok = [&](int a, int b) {
        if (a > b) std::swap(a, b);
        return allowed.count({a, b}) > 0;
    };
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int c = g.getCell(x, y);
            if (x + 1 < w && !ok(c, g.getCell(x + 1, y))) return false;
            if (y + 1 < h && !ok(c, g.getCell(x, y + 1))) return false;
        }
    }
    return true;
}

bool findObject(const Grid2D &g, const std::string &type, int &outX, int &outY) {
    for (int i = 0; i < g.getObjectCount(); ++i) {
        if (g.getObjectType(i) == type) {
            outX = int(g.getObjectX(i));
            outY = int(g.getObjectY(i));
            return true;
        }
    }
    return false;
}

bool pathOnLayer(Path *path, TileLayer *layer) {
    if (!path || !layer || path->getLength() <= 0) return false;
    for (int i = 0; i < path->getLength(); ++i) {
        const int gid = layer->getTile(path->getX(i), path->getY(i));
        if (gid == kWallGid || gid == 0) return false;
    }
    return true;
}

void meshBounds(const MeshBuild &m, float &minX, float &minY, float &minZ, float &maxX, float &maxY, float &maxZ) {
    minX = minY = minZ = 1e9f;
    maxX = maxY = maxZ = -1e9f;
    for (int i = 0; i < m.getVertexCount(); ++i) {
        minX = std::min(minX, m.getPositionX(i));
        minY = std::min(minY, m.getPositionY(i));
        minZ = std::min(minZ, m.getPositionZ(i));
        maxX = std::max(maxX, m.getPositionX(i));
        maxY = std::max(maxY, m.getPositionY(i));
        maxZ = std::max(maxZ, m.getPositionZ(i));
    }
}

float meshRadius(const MeshBuild &m) {
    float minX, minY, minZ, maxX, maxY, maxZ;
    meshBounds(m, minX, minY, minZ, maxX, maxY, maxZ);
    const float cx = 0.5f * (minX + maxX);
    const float cy = 0.5f * (minY + maxY);
    const float cz = 0.5f * (minZ + maxZ);
    float       r2 = 0.f;
    for (int i = 0; i < m.getVertexCount(); ++i) {
        const float dx = m.getPositionX(i) - cx;
        const float dy = m.getPositionY(i) - cy;
        const float dz = m.getPositionZ(i) - cz;
        r2             = std::max(r2, dx * dx + dy * dy + dz * dz);
    }
    return std::sqrt(r2);
}

}  // namespace

/**
 * Scenario 1 — Dungeon delve (pixel RPG loop).
 *
 * Timeline (deterministic, headless):
 *  1) WFC dungeon.simple → semantic Grid2D (border walls, spawn + stairs)
 *  2) Palette → TileLayer GIDs (wall/floor/corridor/door)
 *  3) Pathfinder blocks walls; A* from spawn → stairs must succeed on walkable tiles
 *  4) FlowField from stairs; followFlow from spawn reaches the goal
 *  5) Same seed regenerate is byte-identical; JSON export contains algorithm meta
 */
TEST_CASE("procgen.simulation.dungeonDelve") {
    constexpr int   kMapW = 36;
    constexpr int   kMapH = 24;
    constexpr float kTile = 16.f;

    Procgen *proc   = Procgen::create();
    Map     *mapMod = Map::create();
    REQUIRE(proc != nullptr);
    REQUIRE(mapMod != nullptr);

    bindDungeonPalette(proc, "sim_dungeon");

    Params p;
    p.setSeed(20260812);
    p.setSize(kMapW, kMapH);
    p.setString("preset", "dungeon");
    p.setInt("maxAttempts", 96);
    auto params = requireParams(p);

    // ------------------------------------------------------------------
    // 1) Generate semantic dungeon
    // ------------------------------------------------------------------
    auto gridLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid      = gridLease.view();
    REQUIRE(grid.isBound());
    CHECK_EQ(grid->getWidth(), kMapW);
    CHECK_EQ(grid->getHeight(), kMapH);
    CHECK_EQ(grid->getMeta("algorithm", ""), std::string("wfc.simple"));
    CHECK_EQ(grid->getMeta("preset", ""), std::string("dungeon"));

    // Enclosed border (WFC dungeon contract).
    for (int x = 0; x < kMapW; ++x) {
        CHECK_EQ(grid->getCell(x, 0), int(Semantic::Wall));
        CHECK_EQ(grid->getCell(x, kMapH - 1), int(Semantic::Wall));
    }
    for (int y = 0; y < kMapH; ++y) {
        CHECK_EQ(grid->getCell(0, y), int(Semantic::Wall));
        CHECK_EQ(grid->getCell(kMapW - 1, y), int(Semantic::Wall));
    }

    int spawnX = -1, spawnY = -1, stairsX = -1, stairsY = -1;
    REQUIRE(findObject(*grid, "spawn", spawnX, spawnY));
    REQUIRE(findObject(*grid, "stairs", stairsX, stairsY));
    CHECK(spawnX > 0);
    CHECK(spawnX < kMapW - 1);
    CHECK(spawnY > 0);
    CHECK(spawnY < kMapH - 1);
    CHECK(stairsX > 0);
    CHECK(stairsX < kMapW - 1);
    CHECK(stairsY > 0);
    CHECK(stairsY < kMapH - 1);
    // Spawn/stairs sit on walkable semantics.
    auto walkableSem = [](int s) {
        return s == int(Semantic::Floor) || s == int(Semantic::Corridor) || s == int(Semantic::Door);
    };
    CHECK(walkableSem(grid->getCell(spawnX, spawnY)));
    CHECK(walkableSem(grid->getCell(stairsX, stairsY)));

    // ------------------------------------------------------------------
    // 2) Apply palette → TileLayer (runtime map swap)
    // ------------------------------------------------------------------
    TileLayer *layer = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    REQUIRE(layer != nullptr);
    layer->setOrigin(0.f, 0.f);
    layer->setVisible(true);
    auto applyResult = proc->applyToLayer(gridLease.handle, "sim_dungeon", *layer);
    CHECK(applyResult.ok());
    CHECK_EQ(layer->getTile(0, 0), kWallGid);
    {
        const int  spawnGid      = layer->getTile(spawnX, spawnY);
        const bool spawnWalkable = spawnGid == kFloorGid || spawnGid == kCorridorGid || spawnGid == kDoorGid;
        CHECK(spawnWalkable);
    }

    // generateTo must produce the same GIDs for the same seed.
    TileLayer *layer2  = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    auto       out     = requireOutput(*proc);
    auto       outView = out.view();
    REQUIRE(outView.isBound());
    outView->setTarget("tilelayer");
    outView->setLayer(layer2);
    outView->setPalette("sim_dungeon");
    auto generateToResult = proc->generateTo("wfc.simple", params.handle, out.handle);
    CHECK(generateToResult.ok());
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            CHECK_EQ(layer->getTile(x, y), layer2->getTile(x, y));
        }
    }

    // ------------------------------------------------------------------
    // 3) A* delve: spawn → stairs on walkable tiles
    // ------------------------------------------------------------------
    Pathfinder *pf = mapMod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(kWallGid);
    pf->setBlockEmpty(true);
    pf->setTopology("ortho4");
    CHECK(pf->isWalkable(spawnX, spawnY));
    CHECK(pf->isWalkable(stairsX, stairsY));

    Path *path = pf->findPath(spawnX, spawnY, stairsX, stairsY);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() >= 2);
    CHECK_EQ(path->getX(0), spawnX);
    CHECK_EQ(path->getY(0), spawnY);
    CHECK_EQ(path->getX(path->getLength() - 1), stairsX);
    CHECK_EQ(path->getY(path->getLength() - 1), stairsY);
    CHECK(pathOnLayer(path, layer));
    // Path should not jump over walls (4-neigh continuity).
    for (int i = 1; i < path->getLength(); ++i) {
        const int dx = std::abs(path->getX(i) - path->getX(i - 1));
        const int dy = std::abs(path->getY(i) - path->getY(i - 1));
        CHECK_EQ(dx + dy, 1);
    }

    // ------------------------------------------------------------------
    // 4) Party follow: FlowField from stairs, follow from spawn
    // ------------------------------------------------------------------
    FlowField *flow = pf->buildFlowField(stairsX, stairsY);
    REQUIRE(flow != nullptr);
    Path *flowPath = pf->followFlow(flow, spawnX, spawnY);
    REQUIRE(flowPath != nullptr);
    CHECK(flowPath->getLength() >= 2);
    CHECK_EQ(flowPath->getX(flowPath->getLength() - 1), stairsX);
    CHECK_EQ(flowPath->getY(flowPath->getLength() - 1), stairsY);
    CHECK(pathOnLayer(flowPath, layer));

    // ------------------------------------------------------------------
    // 5) Reproducibility + JSON export a designer/debug tool would save
    // ------------------------------------------------------------------
    auto gridBLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto gridB      = gridBLease.view();
    REQUIRE(gridB.isBound());
    CHECK(grid->cells() == gridB->cells());
    auto jsonResult = proc->gridToJson(gridLease.handle);
    REQUIRE(jsonResult.ok());
    const std::string json = std::move(jsonResult).takeValue();
    CHECK(json.find("wfc.simple") != std::string::npos);
    CHECK(json.find("spawn") != std::string::npos);
    CHECK(json.find("stairs") != std::string::npos);

    delete path;
    delete flowPath;
    delete flow;
    delete pf;
    layer->setVisible(false);
    layer2->setVisible(false);
}

/**
 * Scenario 2 — Crystal cave prop kit (3D mesh authoring loop).
 *
 * Timeline (deterministic, CPU-first):
 *  1) Marching Cubes terrain island = cave floor / rocky base
 *  2) Marching Cubes spheres = crystal clusters (different seeds/radii)
 *  3) Assert game-ready budgets: triangle counts, finite normals, AABB in unit cube
 *  4) Combined "scene" vertex budget stays under a soft real-time cap
 *  5) Optional GPU: upload island mesh via newMeshFromArrays (soft-skip without Vulkan window)
 */
TEST_CASE("procgen.simulation.crystalCave") {
    Procgen *proc = Procgen::create();
    REQUIRE(proc != nullptr);
    CHECK(proc->hasMeshRecipe("mesh.marchingcubes"));

    // ------------------------------------------------------------------
    // 1) Rocky island / cave floor
    // ------------------------------------------------------------------
    Params islandP;
    islandP.setSeed(77);
    islandP.setInt("resolution", 28);
    islandP.setString("field", "terrain");
    islandP.setFloat("scale", 1.8f);
    islandP.setInt("octaves", 3);
    islandP.setFloat("isolevel", 0.f);

    auto islandParams = requireParams(islandP);
    auto islandLease  = requireMeshBuild(*proc, "mesh.marchingcubes", islandParams.handle);
    auto island       = islandLease.view();
    REQUIRE(island.isBound());
    CHECK(island->getVertexCount() > 200);
    CHECK_EQ(island->getIndexCount() % 3, 0);
    CHECK_EQ(island->getMeta("field", ""), std::string("terrain"));

    float iminX, iminY, iminZ, imaxX, imayY, imaxZ;
    meshBounds(*island, iminX, iminY, iminZ, imaxX, imayY, imaxZ);
    CHECK(iminX >= -0.55f);
    CHECK(imaxX <= 0.55f);
    CHECK(iminY >= -0.55f);
    CHECK(imayY <= 0.55f);
    CHECK(iminZ >= -0.55f);
    CHECK(imaxZ <= 0.55f);
    // Terrain island should have some vertical extent.
    CHECK(imayY - iminY > 0.05f);

    for (int i = 0; i < island->getVertexCount(); ++i) {
        CHECK(std::isfinite(island->getPositionX(i)));
        CHECK(std::isfinite(island->getNormalX(i)));
        const float nx  = island->getNormalX(i);
        const float ny  = island->getNormalY(i);
        const float nz  = island->getNormalZ(i);
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        CHECK(std::fabs(len - 1.f) < 0.2f);
        // Indices in range (spot-check via triangle walk below).
    }
    for (int t = 0; t < island->getIndexCount(); ++t) {
        CHECK(island->getIndex(t) >= 0);
        CHECK(island->getIndex(t) < island->getVertexCount());
    }

    // ------------------------------------------------------------------
    // 2) Crystal clusters (spheres) scattered as prop meshes
    // ------------------------------------------------------------------
    struct CrystalSpec {
        uint32_t seed;
        float    radius;
        int      resolution;
    };
    // Radii must span multiple voxels at the chosen resolution (unit field in [-1,1]).
    const CrystalSpec crystals[] = {
        {101u, 0.45f, 18},
        {202u, 0.35f, 16},
        {303u, 0.55f, 20},
    };

    std::vector<MeshBuild *> crystalMeshes;
    crystalMeshes.reserve(3);
    int crystalTris = 0;
    for (const CrystalSpec &spec : crystals) {
        Params cp;
        cp.setSeed(spec.seed);
        cp.setInt("resolution", spec.resolution);
        cp.setString("field", "sphere");
        cp.setFloat("radius", spec.radius);
        std::string err;
        MeshBuild   mesh;
        const bool  ok = MeshRecipeRegistry::instance().generate("mesh.marchingcubes", cp, mesh, err);
        CHECK(ok);
        if (!ok) {
            CHECK_EQ(err, std::string());  // surface the recipe error in the log
            continue;
        }
        MeshBuild *c = new MeshBuild(std::move(mesh));
        CHECK(c->getVertexCount() > 40);
        CHECK_EQ(c->getIndexCount() % 3, 0);
        // Crystals stay inside the unit cube prop volume.
        CHECK(meshRadius(*c) < 0.55f);
        crystalTris += c->getIndexCount() / 3;
        crystalMeshes.push_back(c);
    }
    REQUIRE(crystalMeshes.size() == 3);
    // Different seeds/radii should not be identical meshes.
    CHECK(crystalMeshes[0]->positions() != crystalMeshes[1]->positions());

    // ------------------------------------------------------------------
    // 3–4) Combined scene budget (island + crystals) for a small prop kit
    // ------------------------------------------------------------------
    const int islandTris = island->getIndexCount() / 3;
    const int totalTris  = islandTris + crystalTris;
    CHECK(islandTris > 50);
    CHECK(crystalTris > 50);
    // Soft real-time prop kit budget (well under a typical mobile draw call).
    CHECK(totalTris < 50000);
    CHECK(totalTris > 200);

    // Same island params ⇒ identical bake (cache key for a content pipeline).
    auto islandBLease = requireMeshBuild(*proc, "mesh.marchingcubes", islandParams.handle);
    auto islandB      = islandBLease.view();
    REQUIRE(islandB.isBound());
    CHECK(island->positions() == islandB->positions());
    CHECK(island->indices() == islandB->indices());

    // Noise cave variant also usable as an alternate room shell.
    Params caveP;
    caveP.setSeed(55);
    caveP.setInt("resolution", 20);
    caveP.setString("field", "noise");
    caveP.setFloat("scale", 2.2f);
    caveP.setFloat("threshold", 0.0f);
    auto caveParams     = requireParams(caveP);
    auto caveShellLease = requireMeshBuild(*proc, "mesh.marchingcubes", caveParams.handle);
    auto caveShell      = caveShellLease.view();
    REQUIRE(caveShell.isBound());
    CHECK(caveShell->getVertexCount() > 100);
    CHECK(caveShell->positions() != island->positions());

    // ------------------------------------------------------------------
    // 5) Optional GPU upload (soft-skip without window / Vulkan surface)
    // ------------------------------------------------------------------
    try {
        auto *win = eve::window::Window::create();
        auto *gfx = Graphics::create();
        if (win && gfx) {
            eve::window::WindowSettings ws;
            ws.width    = 320;
            ws.height   = 240;
            ws.centered = true;
            if (win->setWindowSettings(ws)) {
                Mesh *gpuIsland =
                    gfx->newMeshFromArrays(island->positions().data(), island->normals().data(), island->uvs().data(),
                                           island->getVertexCount(), island->indices().data(), island->getIndexCount());
                REQUIRE(gpuIsland != nullptr);
                CHECK(gpuIsland->indexCount == island->getIndexCount());

                // Also exercise Procgen::generateMesh convenience path.
                auto gpuCrystal = proc->generateMeshBorrowed("mesh.marchingcubes", islandParams.handle, gfx);
                REQUIRE(gpuCrystal.isBound());
                CHECK(gpuCrystal->indexCount == island->getIndexCount());
                win->close();
            }
        }
    } catch (const std::exception &ex) {
        // Headless / no VK_KHR_surface — CPU path above is the contract.
        (void)ex;
    }

    for (MeshBuild *c : crystalMeshes) delete c;
}

/**
 * Scenario 3 — Cave expedition (WFC cave + pathfinding + FOV).
 *
 * Timeline:
 *  1) WFC cave → TileLayer (wall/floor)
 *  2) Find two distant floor cells in the same connected component
 *  3) A* path between them; path length ≥ manhattan distance
 *  4) FOV from the start cell: walls block sight, nearby floor is visible
 */
TEST_CASE("procgen.simulation.caveExpedition") {
    constexpr int   kMapW = 32;
    constexpr int   kMapH = 24;
    constexpr float kTile = 16.f;

    Procgen *proc   = Procgen::create();
    Map     *mapMod = Map::create();
    bindDungeonPalette(proc, "sim_cave");

    Params p;
    p.setSeed(4242);
    p.setSize(kMapW, kMapH);
    p.setString("preset", "cave");
    p.setInt("maxAttempts", 64);

    auto params    = requireParams(p);
    auto gridLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid      = gridLease.view();
    REQUIRE(grid.isBound());
    CHECK(borderIsWallLike(*grid));

    TileLayer *layer       = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    auto       applyResult = proc->applyToLayer(gridLease.handle, "sim_cave", *layer);
    CHECK(applyResult.ok());

    // Collect floor cells.
    std::vector<std::pair<int, int>> floors;
    for (int y = 1; y < kMapH - 1; ++y) {
        for (int x = 1; x < kMapW - 1; ++x) {
            if (layer->getTile(x, y) == kFloorGid) floors.emplace_back(x, y);
        }
    }
    REQUIRE(floors.size() >= 8);

    // BFS components; pick two cells from the largest component that are far apart.
    auto                             idx = [&](int x, int y) { return y * kMapW + x; };
    std::vector<int>                 comp(size_t(kMapW * kMapH), -1);
    int                              bestComp = -1, bestSize = 0;
    std::vector<std::pair<int, int>> bestCells;
    int                              nextId = 0;
    const int                        dx4[4] = {1, -1, 0, 0};
    const int                        dy4[4] = {0, 0, 1, -1};
    for (auto [sx, sy] : floors) {
        if (comp[size_t(idx(sx, sy))] >= 0) continue;
        std::vector<std::pair<int, int>> cells;
        std::vector<std::pair<int, int>> q;
        q.emplace_back(sx, sy);
        comp[size_t(idx(sx, sy))] = nextId;
        size_t qi                 = 0;
        while (qi < q.size()) {
            auto [cx, cy] = q[qi++];
            cells.emplace_back(cx, cy);
            for (int d = 0; d < 4; ++d) {
                const int nx = cx + dx4[d], ny = cy + dy4[d];
                if (nx < 0 || ny < 0 || nx >= kMapW || ny >= kMapH) continue;
                if (layer->getTile(nx, ny) != kFloorGid) continue;
                if (comp[size_t(idx(nx, ny))] >= 0) continue;
                comp[size_t(idx(nx, ny))] = nextId;
                q.emplace_back(nx, ny);
            }
        }
        if (int(cells.size()) > bestSize) {
            bestSize  = int(cells.size());
            bestComp  = nextId;
            bestCells = std::move(cells);
        }
        ++nextId;
    }
    REQUIRE(bestSize >= 4);
    CHECK(bestComp >= 0);

    // Farthest pair (approx) for a meaningful expedition.
    int a = 0, b = 0, bestDist = -1;
    for (int i = 0; i < int(bestCells.size()); ++i) {
        for (int j = i + 1; j < int(bestCells.size()); ++j) {
            const int dist = std::abs(bestCells[size_t(i)].first - bestCells[size_t(j)].first) +
                             std::abs(bestCells[size_t(i)].second - bestCells[size_t(j)].second);
            if (dist > bestDist) {
                bestDist = dist;
                a        = i;
                b        = j;
            }
        }
    }
    CHECK(bestDist >= 2);
    const int ax = bestCells[size_t(a)].first, ay = bestCells[size_t(a)].second;
    const int bx = bestCells[size_t(b)].first, by = bestCells[size_t(b)].second;

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->blockGid(kWallGid);
    pf->setBlockEmpty(true);
    pf->setTopology("ortho4");
    Path *path = pf->findPath(ax, ay, bx, by);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() >= bestDist + 1);  // inclusive endpoints
    CHECK(pathOnLayer(path, layer));
    CHECK_EQ(path->getX(0), ax);
    CHECK_EQ(path->getY(path->getLength() - 1), by);

    // FOV: walls opaque; floor around start is visible; far goal may be hidden.
    Fov *fov = mapMod->newFov(layer);
    REQUIRE(fov != nullptr);
    fov->blockOpaqueGid(kWallGid);
    fov->setAlgorithm("shadowcast");
    const int rev = fov->addRevealer(ax, ay, 6);
    CHECK(rev >= 0);
    fov->compute();
    CHECK(fov->isVisible(ax, ay));
    CHECK(fov->isExplored(ax, ay));
    // Immediate floor neighbors (if any) should be visible.
    for (int d = 0; d < 4; ++d) {
        const int nx = ax + dx4[d], ny = ay + dy4[d];
        if (nx < 0 || ny < 0 || nx >= kMapW || ny >= kMapH) continue;
        if (layer->getTile(nx, ny) == kFloorGid) CHECK(fov->isVisible(nx, ny));
    }

    delete path;
    delete pf;
    delete fov;
    layer->setVisible(false);
}

/**
 * Scenario 4 — Overworld biome travel (WFC terrain + weighted path).
 *
 * Timeline:
 *  1) WFC terrain → biome Grid2D with adjacency invariant
 *  2) Palette GIDs per biome; TileLayer filled
 *  3) Pathfinder with higher cost on water/snow; path prefers mid biomes
 *  4) Same seed regenerate matches for a save-game overworld
 */
TEST_CASE("procgen.simulation.overworldBiomeTravel") {
    constexpr int   kMapW = 28;
    constexpr int   kMapH = 22;
    constexpr float kTile = 16.f;

    Procgen *proc   = Procgen::create();
    Map     *mapMod = Map::create();

    // Biome GIDs
    constexpr int kWater = 10, kSand = 11, kGrass = 12, kDirt = 13, kStone = 14, kSnow = 15;
    proc->setPaletteGid("sim_biome", "water", kWater);
    proc->setPaletteGid("sim_biome", "sand", kSand);
    proc->setPaletteGid("sim_biome", "grass", kGrass);
    proc->setPaletteGid("sim_biome", "dirt", kDirt);
    proc->setPaletteGid("sim_biome", "stone", kStone);
    proc->setPaletteGid("sim_biome", "snow", kSnow);

    Params p;
    p.setSeed(9001);
    p.setSize(kMapW, kMapH);
    p.setString("preset", "terrain");
    p.setInt("maxAttempts", 64);

    auto params    = requireParams(p);
    auto gridLease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid      = gridLease.view();
    REQUIRE(grid.isBound());
    CHECK(terrainAdjacencyOk(*grid));

    TileLayer *layer       = mapMod->newLayer(kMapW, kMapH, kTile, kTile);
    auto       applyResult = proc->applyToLayer(gridLease.handle, "sim_biome", *layer);
    CHECK(applyResult.ok());

    // Pick a grass cell and a dirt/stone cell if possible.
    int sx = -1, sy = -1, gx = -1, gy = -1;
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int gid = layer->getTile(x, y);
            if (sx < 0 && gid == kGrass) {
                sx = x;
                sy = y;
            }
            if (gid == kDirt || gid == kStone) {
                gx = x;
                gy = y;
            }
        }
    }
    if (sx < 0) {
        sx = 1;
        sy = 1;
    }
    if (gx < 0) {
        gx = kMapW - 2;
        gy = kMapH - 2;
    }

    Pathfinder *pf = mapMod->newPathfinder(layer);
    pf->setTopology("ortho4");
    pf->setBlockEmpty(false);
    // Prefer dry land: water/snow expensive.
    for (int y = 0; y < kMapH; ++y) {
        for (int x = 0; x < kMapW; ++x) {
            const int gid  = layer->getTile(x, y);
            float     cost = 1.f;
            if (gid == kWater)
                cost = 8.f;
            else if (gid == kSnow)
                cost = 4.f;
            else if (gid == kSand)
                cost = 1.5f;
            pf->setCellCost(x, y, cost);
        }
    }

    Path *path = pf->findPath(sx, sy, gx, gy);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() >= 2);
    CHECK_EQ(path->getX(0), sx);
    CHECK_EQ(path->getY(path->getLength() - 1), gy);
    // Cost should be finite and positive.
    CHECK(path->getTotalCost() > 0.f);

    // Reproducible overworld for save/load.
    auto grid2Lease = requireGrid(*proc, "wfc.simple", params.handle);
    auto grid2      = grid2Lease.view();
    REQUIRE(grid2.isBound());
    CHECK(grid->cells() == grid2->cells());

    delete path;
    delete pf;
    layer->setVisible(false);
}

/**
 * Scenario 5 — Loot prop bake (torus ring + gem spheres for a content pipeline).
 */
TEST_CASE("procgen.simulation.lootPropBake") {
    Procgen *proc = Procgen::create();

    Params ringP;
    ringP.setSeed(12);
    ringP.setInt("resolution", 22);
    ringP.setString("field", "torus");
    ringP.setFloat("majorRadius", 0.5f);
    ringP.setFloat("minorRadius", 0.18f);

    auto ringParams = requireParams(ringP);
    auto ringLease  = requireMeshBuild(*proc, "mesh.marchingcubes", ringParams.handle);
    auto ring       = ringLease.view();
    REQUIRE(ring.isBound());
    CHECK(ring->getVertexCount() > 150);
    CHECK_EQ(ring->getIndexCount() % 3, 0);

    Params gemP;
    gemP.setSeed(44);
    gemP.setInt("resolution", 16);
    gemP.setString("field", "sphere");
    gemP.setFloat("radius", 0.4f);
    auto gemParams = requireParams(gemP);
    auto gemLease  = requireMeshBuild(*proc, "mesh.marchingcubes", gemParams.handle);
    auto gem       = gemLease.view();
    REQUIRE(gem.isBound());

    const int totalTris = ring->getIndexCount() / 3 + gem->getIndexCount() / 3;
    CHECK(totalTris > 100);
    CHECK(totalTris < 30000);

    // Content-pipeline cache key: identical params → identical bytes.
    auto ring2Lease = requireMeshBuild(*proc, "mesh.marchingcubes", ringParams.handle);
    auto ring2      = ring2Lease.view();
    REQUIRE(ring2.isBound());
    CHECK(ring->positions() == ring2->positions());
    CHECK(ring->indices() == ring2->indices());

    // Slight param change busts the cache.
    Params ringP2 = ringP;
    ringP2.setFloat("minorRadius", 0.22f);
    auto ring2Params = requireParams(ringP2);
    auto ring3Lease  = requireMeshBuild(*proc, "mesh.marchingcubes", ring2Params.handle);
    auto ring3       = ring3Lease.view();
    REQUIRE(ring3.isBound());
    CHECK(ring->positions() != ring3->positions());
}
}  // namespace test_procgen_simulation
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_resource_account)
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include "PrivateTestScope.h"
#include "attributes/AttributeResourceAccount.h"
#include "attributes/AttributeSetResourceAccount.h"
#include "card/Card.h"
#include "card/CardContainers.h"
#include "card/CardPlay.h"
#include "condition/ConditionKeys.h"
#include "economy/EconomyLedger.h"
#include "economy/EconomyLedgerResourceAccount.h"
#include "economy/ResourceType.h"
#include "inventory/InventoryResourceAccount.h"
#include "inventory/Item.h"
#include "orders/CommandQueue.h"
#include "production/Production.h"
#include "rts/RTSEconomy.h"
#include "rts/RTSProductionAction.h"
#include "transaction/AtomicResourcePayment.h"
#include "transaction/ResourceAccountParticipant.h"
#include "weapon/WeaponAction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// resource_account.cpp
namespace test_resource_account {
// resource_account.cpp
namespace {

using eve::resource::CostInput;
using eve::resource::CostSpec;

static_assert(!std::is_constructible_v<eve::resource::AccountNonce, std::uint64_t>);

CostSpec makeCost(std::initializer_list<CostInput> items) {
    auto result = CostSpec::from(items);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

template <class Account, class Balance>
void runCommonAccountContract(Account &account, Balance &&balance) {
    const CostSpec cost = makeCost({{"mana", 40}});

    auto affordable = account.canAfford(cost);
    REQUIRE(affordable.ok());
    CHECK(affordable.value().affordable);

    auto reservationResult = account.reserve(cost);
    REQUIRE(reservationResult.ok());
    const auto reservation = reservationResult.value();
    CHECK(reservation.isValid());
    CHECK_EQ(balance("mana"), std::int64_t{100});

    const CostSpec blockedCost = makeCost({{"mana", 70}});
    auto           blocked     = account.reserve(blockedCost);
    CHECK(!blocked.ok());
    CHECK_EQ(static_cast<int>(blocked.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(balance("mana"), std::int64_t{100});

    auto committed = account.commit(reservation);
    REQUIRE(committed.ok());
    CHECK(committed.value().isValid());
    CHECK_EQ(static_cast<int>(committed.value().operation), static_cast<int>(eve::resource::ReceiptOperation::Debit));
    CHECK_EQ(balance("mana"), std::int64_t{60});

    // A copied credential has the same account nonce and id, but the account
    // lifecycle still permits only one terminal operation.
    const auto copiedReservation = reservation;
    auto       duplicateCommit   = account.commit(copiedReservation);
    CHECK(!duplicateCommit.ok());
    CHECK_EQ(static_cast<int>(duplicateCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto duplicateRollback = account.rollback(copiedReservation);
    CHECK(!duplicateRollback.ok());
    CHECK_EQ(static_cast<int>(duplicateRollback.code()), static_cast<int>(eve::StatusCode::Conflict));

    const CostSpec credit   = makeCost({{"mana", 5}, {"stamina", 3}});
    auto           credited = account.credit(credit);
    REQUIRE(credited.ok());
    CHECK_EQ(static_cast<int>(credited.value().operation), static_cast<int>(eve::resource::ReceiptOperation::Credit));
    CHECK_EQ(balance("mana"), std::int64_t{65});
    CHECK_EQ(balance("stamina"), std::int64_t{3});
}

class CommitFailureParticipant final : public eve::transaction::ITransactionParticipant {
public:
    std::string_view name() const noexcept override { return "test-commit-failure"; }

    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext &context) override {
        (void)context;
        prepared = true;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext &context) override {
        (void)context;
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected commit failure", "test.commit"));
    }

    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext &context) override {
        (void)context;
        prepared = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext &context) override {
        (void)context;
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "injected participant was never committed", "test.compensate"));
    }

    bool prepared = false;
};

}  // namespace

TEST_CASE("resource.account.attributeSetMatchesCommonDebitCreditReservationContract") {
    eve::attributes::AttributeSet attributes("account.attributes");
    attributes.setBase("mana", 100.0);
    eve::attributes::AttributeSetResourceAccount account(attributes);

    runCommonAccountContract(account, [&attributes](std::string_view resource) {
        return static_cast<std::int64_t>(attributes.getBase(std::string(resource), 0.0));
    });
}

TEST_CASE("resource.account.economyLedgerMatchesCommonDebitCreditReservationContract") {
    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("mana", 100), 100);
    eve::economy::EconomyLedgerResourceAccount account(ledger);

    runCommonAccountContract(account, [&ledger](std::string_view resource) {
        return static_cast<std::int64_t>(ledger.get(std::string(resource)));
    });
}

TEST_CASE("resource.account.multiResourceDebitFailureIsAtomicForBothAdapters") {
    const CostSpec impossible = makeCost({{"gold", 4}, {"mana", 100}});

    eve::attributes::AttributeSet attributes;
    attributes.setBase("gold", 10.0);
    attributes.setBase("mana", 3.0);
    eve::attributes::AttributeSetResourceAccount attributeAccount(attributes);
    auto                                         attributeDebit = attributeAccount.debit(impossible);
    CHECK(!attributeDebit.ok());
    CHECK_EQ(static_cast<int>(attributeDebit.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(attributes.getBase("gold"), 10.0);
    CHECK_EQ(attributes.getBase("mana"), 3.0);

    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("gold", 10), 10);
    CHECK_EQ(ledger.credit("mana", 3), 3);
    eve::economy::EconomyLedgerResourceAccount economyAccount(ledger);
    auto                                       economyDebit = economyAccount.debit(impossible);
    CHECK(!economyDebit.ok());
    CHECK_EQ(static_cast<int>(economyDebit.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(ledger.get("gold"), 10);
    CHECK_EQ(ledger.get("mana"), 3);
}

TEST_CASE("resource.account.rollbackReleasesReservationWithoutChangingBalance") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("mana", 50.0);
    eve::attributes::AttributeSetResourceAccount account(attributes);
    const CostSpec                               cost = makeCost({{"mana", 30}});

    auto reservationResult = account.reserve(cost);
    REQUIRE(reservationResult.ok());
    const auto reservation = reservationResult.value();
    auto       blocked     = account.canAfford(makeCost({{"mana", 25}}));
    REQUIRE(blocked.ok());
    CHECK(!blocked.value().affordable);
    CHECK_EQ(attributes.getBase("mana"), 50.0);

    auto rolledBack = account.rollback(reservation);
    REQUIRE(rolledBack.ok());
    CHECK_EQ(attributes.getBase("mana"), 50.0);
    auto available = account.canAfford(makeCost({{"mana", 25}}));
    REQUIRE(available.ok());
    CHECK(available.value().affordable);

    const auto copiedReservation = reservation;
    auto       duplicateRollback = account.rollback(copiedReservation);
    CHECK(!duplicateRollback.ok());
    CHECK_EQ(static_cast<int>(duplicateRollback.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto duplicateCommit = account.commit(copiedReservation);
    CHECK(!duplicateCommit.ok());
    CHECK_EQ(static_cast<int>(duplicateCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
}

TEST_CASE("resource.account.sameTypeAccountsRejectForeignCredentialsEvenWithSameIdAndCost") {
    eve::attributes::AttributeSet firstAttributes;
    eve::attributes::AttributeSet secondAttributes;
    firstAttributes.setBase("mana", 10.0);
    secondAttributes.setBase("mana", 10.0);
    eve::attributes::AttributeSetResourceAccount first(firstAttributes);
    eve::attributes::AttributeSetResourceAccount second(secondAttributes);
    const CostSpec                               cost = makeCost({{"mana", 5}});

    auto firstReservationResult  = first.reserve(cost);
    auto secondReservationResult = second.reserve(cost);
    REQUIRE(firstReservationResult.ok());
    REQUIRE(secondReservationResult.ok());
    const auto firstReservation  = firstReservationResult.value();
    const auto secondReservation = secondReservationResult.value();
    CHECK_EQ(firstReservation.id.value(), secondReservation.id.value());
    CHECK_NE(firstReservation.account.value(), secondReservation.account.value());

    auto foreignCommit = second.commit(firstReservation);
    CHECK(!foreignCommit.ok());
    CHECK_EQ(static_cast<int>(foreignCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto foreignRollback = second.rollback(firstReservation);
    CHECK(!foreignRollback.ok());
    CHECK_EQ(static_cast<int>(foreignRollback.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignCommit = first.commit(secondReservation);
    CHECK(!reverseForeignCommit.ok());
    CHECK_EQ(static_cast<int>(reverseForeignCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignRollback = first.rollback(secondReservation);
    CHECK(!reverseForeignRollback.ok());
    CHECK_EQ(static_cast<int>(reverseForeignRollback.code()), static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(secondAttributes.getBase("mana"), 10.0);

    auto ownCommit = first.commit(firstReservation);
    REQUIRE(ownCommit.ok());
    CHECK_EQ(firstAttributes.getBase("mana"), 5.0);
    auto ownRollback = second.rollback(secondReservation);
    REQUIRE(ownRollback.ok());
    CHECK_EQ(secondAttributes.getBase("mana"), 10.0);
}

TEST_CASE("resource.account.economyAccountsRejectForeignCredentialsEvenWithSameIdAndCost") {
    eve::economy::EconomyLedger firstLedger;
    eve::economy::EconomyLedger secondLedger;
    CHECK_EQ(firstLedger.credit("gold", 10), 10);
    CHECK_EQ(secondLedger.credit("gold", 10), 10);
    eve::economy::EconomyLedgerResourceAccount first(firstLedger);
    eve::economy::EconomyLedgerResourceAccount second(secondLedger);
    const CostSpec                             cost = makeCost({{"gold", 5}});

    auto firstReservationResult  = first.reserve(cost);
    auto secondReservationResult = second.reserve(cost);
    REQUIRE(firstReservationResult.ok());
    REQUIRE(secondReservationResult.ok());
    const auto firstReservation  = firstReservationResult.value();
    const auto secondReservation = secondReservationResult.value();
    CHECK_EQ(firstReservation.id.value(), secondReservation.id.value());
    CHECK_NE(firstReservation.account.value(), secondReservation.account.value());

    auto foreignCommit = second.commit(firstReservation);
    CHECK(!foreignCommit.ok());
    CHECK_EQ(static_cast<int>(foreignCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto foreignRollback = second.rollback(firstReservation);
    CHECK(!foreignRollback.ok());
    CHECK_EQ(static_cast<int>(foreignRollback.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignCommit = first.commit(secondReservation);
    CHECK(!reverseForeignCommit.ok());
    CHECK_EQ(static_cast<int>(reverseForeignCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto reverseForeignRollback = first.rollback(secondReservation);
    CHECK(!reverseForeignRollback.ok());
    CHECK_EQ(static_cast<int>(reverseForeignRollback.code()), static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(secondLedger.get("gold"), 10);

    auto ownCommit = first.commit(firstReservation);
    REQUIRE(ownCommit.ok());
    CHECK_EQ(firstLedger.get("gold"), 5);
    auto ownRollback = second.rollback(secondReservation);
    REQUIRE(ownRollback.ok());
    CHECK_EQ(secondLedger.get("gold"), 10);
}

TEST_CASE("resource.account.crossAdapterCredentialsRejectCommitAndRollback") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("gold", 10.0);
    eve::attributes::AttributeSetResourceAccount attributeAccount(attributes);

    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("gold", 10), 10);
    eve::economy::EconomyLedgerResourceAccount economyAccount(ledger);
    const CostSpec                             cost = makeCost({{"gold", 5}});

    auto attributeReservationResult = attributeAccount.reserve(cost);
    auto economyReservationResult   = economyAccount.reserve(cost);
    REQUIRE(attributeReservationResult.ok());
    REQUIRE(economyReservationResult.ok());
    const auto attributeReservation = attributeReservationResult.value();
    const auto economyReservation   = economyReservationResult.value();
    CHECK_EQ(attributeReservation.id.value(), economyReservation.id.value());
    CHECK_NE(attributeReservation.account.value(), economyReservation.account.value());

    auto wrongEconomyCommit = economyAccount.commit(attributeReservation);
    CHECK(!wrongEconomyCommit.ok());
    CHECK_EQ(static_cast<int>(wrongEconomyCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto wrongAttributeRollback = attributeAccount.rollback(economyReservation);
    CHECK(!wrongAttributeRollback.ok());
    CHECK_EQ(static_cast<int>(wrongAttributeRollback.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto wrongAttributeCommit = attributeAccount.commit(economyReservation);
    CHECK(!wrongAttributeCommit.ok());
    CHECK_EQ(static_cast<int>(wrongAttributeCommit.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto wrongEconomyRollback = economyAccount.rollback(attributeReservation);
    CHECK(!wrongEconomyRollback.ok());
    CHECK_EQ(static_cast<int>(wrongEconomyRollback.code()), static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(attributes.getBase("gold"), 10.0);
    CHECK_EQ(ledger.get("gold"), 10);

    REQUIRE(attributeAccount.rollback(attributeReservation).ok());
    REQUIRE(economyAccount.rollback(economyReservation).ok());
}

TEST_CASE("resource.account.capacityFailureDoesNotPartiallyCreditEconomyLedger") {
    eve::economy::ResourceTypeRegistry::clear();
    eve::economy::ResourceTypeDef definition;
    definition.id       = "gold";
    definition.stockMax = 10;
    CHECK(eve::economy::ResourceTypeRegistry::registerType(definition));

    eve::economy::EconomyLedger ledger;
    CHECK_EQ(ledger.credit("gold", 8), 8);
    CHECK_EQ(ledger.credit("wood", 1), 1);
    eve::economy::EconomyLedgerResourceAccount account(ledger);
    const CostSpec                             cost = makeCost({{"gold", 4}, {"wood", 2}});

    auto result = account.credit(cost);
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(ledger.get("gold"), 8);
    CHECK_EQ(ledger.get("wood"), 1);
    eve::economy::ResourceTypeRegistry::clear();
}

TEST_CASE("resource.account.transactionCommitFailureCompensatesCommittedDebit") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("mana", 100.0);
    eve::attributes::AttributeSetResourceAccount               account(attributes);
    const CostSpec                                             cost = makeCost({{"mana", 40}});
    eve::transaction::ResourceDebitParticipant                 debit(account, cost);
    CommitFailureParticipant                                   failing;
    std::array<eve::transaction::ITransactionParticipant *, 2> participants{&debit, &failing};

    eve::transaction::Coordinator coordinator;
    auto                          transaction = coordinator.execute(
        eve::transaction::TransactionContext("resource-atomic"),
        std::span<eve::transaction::ITransactionParticipant *>(participants.data(), participants.size()));
    CHECK(!transaction.ok());
    CHECK_EQ(static_cast<int>(transaction.code()), static_cast<int>(eve::StatusCode::Failed));
    CHECK_EQ(static_cast<int>(debit.state()), static_cast<int>(eve::transaction::ResourceDebitState::Compensated));
    CHECK(!failing.prepared);
    CHECK_EQ(attributes.getBase("mana"), 100.0);
}

// resource_gameplay_adapters.cpp
namespace {

eve::resource::CostSpec makeCost(std::string_view resource, std::int64_t amount) {
    auto result = eve::resource::CostSpec::single(resource, amount);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

eve::Status injectedFailure(std::string_view path) {
    return eve::Status::failure(eve::StatusCode::Rejected,
                                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                       "failure injected by resource adapter test", std::string(path)));
}

class TestEffect final : public eve::transaction::ITransactionParticipant {
public:
    explicit TestEffect(int &visibleValue) : visibleValue_(visibleValue) {}

    std::string_view name() const noexcept override { return "test-effect"; }

    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext &) override {
        if (failPrepare) return eve::Result<void>::failure(injectedFailure("effect.prepare"));
        if (prepared || committed) return eve::Result<void>::failure(injectedFailure("effect.reentry"));
        prepared = true;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext &) override {
        if (!prepared || committed) return eve::Result<void>::failure(injectedFailure("effect.commit.state"));
        if (failCommit) return eve::Result<void>::failure(injectedFailure("effect.commit"));
        ++visibleValue_;
        committed = true;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext &) override {
        if (committed) return eve::Result<void>::failure(injectedFailure("effect.rollback.committed"));
        prepared = false;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext &) override {
        if (!committed) return eve::Result<void>::success();
        --visibleValue_;
        committed = false;
        prepared  = false;
        return eve::Result<void>::success();
    }

    bool failPrepare = false;
    bool failCommit  = false;
    bool prepared    = false;
    bool committed   = false;

private:
    int &visibleValue_;
};

}  // namespace

TEST_CASE("resource.gameplay.attributeViewsReuseOneCanonicalAccount") {
    eve::attributes::AttributeSet attributes;
    attributes.setBase("mana", 10.0);
    attributes.setBase("stamina", 7.0);
    eve::attributes::AttributeSetResourceAccount base(attributes);
    eve::attributes::ManaAccountAdapter          mana(base, eve::attributes::AttributeResourceKind::Mana);
    eve::attributes::StaminaAccountAdapter       stamina(base, eve::attributes::AttributeResourceKind::Stamina);

    const auto manaCost        = makeCost("mana", 3);
    const auto staminaCost     = makeCost("stamina", 2);
    auto       manaReservation = mana.reserve(manaCost);
    REQUIRE(manaReservation.ok());
    auto committed = mana.commit(manaReservation.value());
    REQUIRE(committed.ok());
    CHECK_EQ(attributes.getBase("mana"), 7.0);
    CHECK_EQ(attributes.getBase("stamina"), 7.0);

    auto wrongView = mana.reserve(staminaCost);
    CHECK(!wrongView.ok());
    auto staminaReservation = stamina.reserve(staminaCost);
    REQUIRE(staminaReservation.ok());
    auto staminaRollback = stamina.rollback(staminaReservation.value());
    REQUIRE(staminaRollback.ok());
}

TEST_CASE("resource.gameplay.cardManaPaymentIsAtomicAndUsesPlayerAccount") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::card::CardData *card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    card->state()->phase = eve::card::CardState::Hand;

    eve::card::CardDefinition definition;
    definition.id   = "firebolt";
    definition.cost = 3;
    eve::attributes::AttributeSet playerAttributes;
    playerAttributes.setBase("mana", 10.0);
    eve::attributes::AttributeSetResourceAccount playerAccount(playerAttributes);

    auto paid = eve::card::CardPlayPaymentAdapter::play(*card, definition, playerAccount, "card.firebolt.1");
    REQUIRE(paid.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Played));
    CHECK_EQ(playerAttributes.getBase("mana"), 7.0);

    card->release();
}

TEST_CASE("resource.gameplay.cardInsufficientManaLeavesEffectUnchanged") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::card::CardData *card = eve::card::CardData::createCard();
    REQUIRE(card != nullptr);
    card->state()->phase = eve::card::CardState::Hand;

    eve::card::CardDefinition definition;
    definition.id   = "expensive";
    definition.cost = 5;
    eve::attributes::AttributeSet playerAttributes;
    playerAttributes.setBase("mana", 2.0);
    eve::attributes::AttributeSetResourceAccount playerAccount(playerAttributes);

    auto rejected = eve::card::CardPlayPaymentAdapter::play(*card, definition, playerAccount, "card.expensive.1");
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(card->state()->phase), static_cast<int>(eve::card::CardState::Hand));
    CHECK_EQ(playerAttributes.getBase("mana"), 2.0);

    card->release();
}

TEST_CASE("resource.gameplay.weaponAmmoUsesInventoryAndPrepareFailureDoesNotCharge") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id       = "ammo";
    ammo.maxStack = 99;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag bag(1);
    bag.setId("player.inventory");
    const int ammoAdded = bag.addItem("ammo", 5);
    CHECK_EQ(ammoAdded, 5);
    eve::inventory::InventoryResourceAccount account(bag);

    eve::weapon::WeaponDefinition weapon;
    weapon.id              = "rifle";
    weapon.resource.kind   = eve::weapon::ResourceKind::Ammo;
    weapon.resource.cost   = 1.0f;
    int        effectValue = 0;
    TestEffect effect(effectValue);
    auto       fired = eve::weapon::WeaponActionAdapter::fire(weapon, account, effect, "weapon.rifle.1");
    REQUIRE(fired.ok());
    CHECK_EQ(bag.countItem("ammo"), 4);
    CHECK_EQ(effectValue, 1);

    effect.failPrepare = true;
    auto rejected      = eve::weapon::WeaponActionAdapter::fire(weapon, account, effect, "weapon.rifle.2");
    CHECK(!rejected.ok());
    CHECK_EQ(bag.countItem("ammo"), 4);
    CHECK_EQ(effectValue, 1);

    effect.failPrepare  = false;
    effect.failCommit   = true;
    auto commitRejected = eve::weapon::WeaponActionAdapter::fire(weapon, account, effect, "weapon.rifle.3");
    CHECK(!commitRejected.ok());
    CHECK_EQ(bag.countItem("ammo"), 4);
    CHECK_EQ(effectValue, 1);
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("resource.gameplay.itemCostAndAccountNonceAreNotCrossAccount") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition potion;
    potion.id       = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
    eve::inventory::Bag firstBag(1);
    eve::inventory::Bag secondBag(1);
    const int           potionAdded = firstBag.addItem("potion", 3);
    CHECK_EQ(potionAdded, 3);
    eve::inventory::InventoryResourceAccount first(firstBag);
    eve::inventory::InventoryResourceAccount second(secondBag);
    auto                                     costResult = eve::inventory::ItemCostAdapter::itemCost("potion", 2);
    REQUIRE(costResult.ok());
    const auto cost        = std::move(costResult).takeValue();
    auto       reservation = first.reserve(cost);
    REQUIRE(reservation.ok());
    auto foreignCommit = second.commit(reservation.value());
    CHECK(!foreignCommit.ok());
    auto rollback = first.rollback(reservation.value());
    REQUIRE(rollback.ok());
    CHECK_EQ(firstBag.countItem("potion"), 3);
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("resource.gameplay.rtsEconomyUsesLedgerTransaction") {
    eve::economy::EconomyLedger ledger;
    eve::rts::RTSEconomyAdapter economy(ledger);
    const auto                  income   = makeCost("gold", 10);
    auto                        credited = economy.credit(income);
    REQUIRE(credited.ok());
    CHECK_EQ(ledger.get("gold"), 10);

    const auto price       = makeCost("gold", 4);
    int        effectValue = 0;
    TestEffect effect(effectValue);
    auto       paid = economy.pay(price, effect, "rts.build.1");
    REQUIRE(paid.ok());
    CHECK_EQ(ledger.get("gold"), 6);
    CHECK_EQ(effectValue, 1);

    effect.failPrepare = true;
    auto rejected      = economy.pay(price, effect, "rts.build.2");
    CHECK(!rejected.ok());
    CHECK_EQ(ledger.get("gold"), 6);
}

TEST_CASE("resource.gameplay.cardPlayCompositionIsOneAtomicCrossWrite") {
    eve::card::Card cards;
    REQUIRE_EQ(cards.registerCardsFromJson(R"([{"id":"bolt","kind":"spell","cost":3}])"), 1);
    auto *deck = cards.newDeck();
    auto *hand = cards.newHand(cards.newConfig());
    auto *card = cards.newCard("bolt");
    REQUIRE(deck != nullptr);
    REQUIRE(hand != nullptr);
    REQUIRE(card != nullptr);
    deck->push(card);

    eve::card::CardContainerAdapter source(eve::container::ContainerId("card:atomic-deck"),
                                           eve::card::CardContainerKind::Deck, deck);
    eve::card::CardContainerAdapter destination(eve::container::ContainerId("card:atomic-hand"),
                                                eve::card::CardContainerKind::Hand, nullptr, hand, nullptr,
                                                eve::container::Capacity::fixed(2));
    eve::attributes::AttributeSet   attributes;
    attributes.setBase("mana", 10.0);
    eve::attributes::AttributeSetResourceAccount account(attributes);
    int                                          effectValue = 0;
    TestEffect                                   effect(effectValue);
    eve::card::CardDefinition                    definition;
    definition.id   = "bolt";
    definition.kind = "spell";
    definition.cost = 3;

    eve::card::CardPlayRequest request;
    request.card                        = card;
    request.definition                  = &definition;
    request.playerAccount               = &account;
    request.transactionId               = "card.atomic.success";
    request.composition.source          = &source;
    request.composition.destination     = &destination;
    request.composition.sourceSlot      = eve::container::SlotIndex(0);
    request.composition.destinationSlot = eve::container::SlotIndex(0);
    request.composition.effect          = &effect;
    auto played                         = eve::card::CardPlayPaymentAdapter::play(std::move(request));
    REQUIRE(played.ok());
    CHECK_EQ(deck->count(), 0);
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(attributes.getBase("mana"), 7.0);
    CHECK_EQ(effectValue, 1);

    auto *rejectedCard = cards.newCard("bolt");
    REQUIRE(rejectedCard != nullptr);
    deck->push(rejectedCard);
    TestEffect rejectedEffect(effectValue);
    rejectedEffect.failCommit = true;
    eve::card::CardPlayRequest rejectedRequest;
    rejectedRequest.card                        = rejectedCard;
    rejectedRequest.definition                  = &definition;
    rejectedRequest.playerAccount               = &account;
    rejectedRequest.transactionId               = "card.atomic.effect-failure";
    rejectedRequest.composition.source          = &source;
    rejectedRequest.composition.destination     = &destination;
    rejectedRequest.composition.sourceSlot      = eve::container::SlotIndex(0);
    rejectedRequest.composition.destinationSlot = eve::container::SlotIndex(1);
    rejectedRequest.composition.effect          = &rejectedEffect;
    auto rejected                               = eve::card::CardPlayPaymentAdapter::play(std::move(rejectedRequest));
    CHECK(!rejected.ok());
    CHECK_EQ(deck->count(), 1);
    CHECK_EQ(hand->count(), 1);
    CHECK_EQ(attributes.getBase("mana"), 7.0);
    CHECK_EQ(effectValue, 1);
}

TEST_CASE("resource.gameplay.rtsBuildUsesSharedActionOrdersProductionPayment") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 20), 20);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue  production;
    eve::orders::CommandQueue   orders;
    eve::action::ActionRuntime  action;
    auto                        productionSnapshot = [&production]() {
        auto result = production.snapshot();
        return std::move(result).expect("production snapshot must serialize");
    };
    auto duration = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    const auto cost = makeCost("gold", 5);

    eve::rts::RTSBuildRequest request;
    request.production     = &production;
    request.orders         = &orders;
    request.action         = &action;
    request.account        = &economy.account();
    request.cost           = cost;
    request.owner          = "faction:one";
    request.productionKind = "unit";
    request.product        = "worker";
    request.duration       = std::move(duration).takeValue();
    request.transactionId  = "rts.build.worker";
    auto built             = eve::rts::RTSProductionActionAdapter::build(std::move(request));
    REQUIRE(built.ok());
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(production.taskCount(), 1);
    CHECK_EQ(orders.orderCount(), 1);
    CHECK_EQ(action.executionCount(), 1u);

    const auto                productionBeforeActionFailure = productionSnapshot();
    const auto                ordersBeforeActionFailure     = orders.orderCount();
    eve::rts::RTSBuildRequest actionFailure;
    actionFailure.production                 = &production;
    actionFailure.orders                     = &orders;
    actionFailure.action                     = &action;
    actionFailure.account                    = &economy.account();
    actionFailure.cost                       = cost;
    actionFailure.owner                      = "faction:one";
    actionFailure.productionKind             = "unit";
    actionFailure.product                    = "tank";
    actionFailure.duration                   = eve::Duration::fromNanoseconds(1000000000);
    actionFailure.transactionId              = "rts.build.action-failure";
    actionFailure.actionDefinition.condition = eve::condition::tagPresent("missing");
    auto failedAction                        = eve::rts::RTSProductionActionAdapter::build(std::move(actionFailure));
    CHECK(!failedAction.ok());
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(productionSnapshot(), productionBeforeActionFailure);
    CHECK_EQ(orders.orderCount(), ordersBeforeActionFailure);

    eve::rts::RTSBuildRequest queueFailure;
    queueFailure.production     = &production;
    queueFailure.orders         = &orders;
    queueFailure.action         = &action;
    queueFailure.account        = &economy.account();
    queueFailure.cost           = cost;
    queueFailure.owner          = "faction:one";
    queueFailure.productionKind = "unit";
    queueFailure.product        = "tank";
    queueFailure.duration       = eve::Duration::fromNanoseconds(1000000000);
    queueFailure.orderKind.clear();
    queueFailure.transactionId = "rts.build.queue-failure";
    auto failedQueue           = eve::rts::RTSProductionActionAdapter::build(std::move(queueFailure));
    CHECK(!failedQueue.ok());
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(productionSnapshot(), productionBeforeActionFailure);
    CHECK_EQ(orders.orderCount(), ordersBeforeActionFailure);
}

TEST_CASE("resource.gameplay.rtsGenericProductionCompletesWithoutDomainSettlementConsumer") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue  production;
    eve::orders::CommandQueue   orders;
    eve::action::ActionRuntime  action;
    eve::rts::RTSBuildRequest   request;
    request.production    = &production;
    request.orders        = &orders;
    request.action        = &action;
    request.account       = &economy.account();
    request.cost          = makeCost("gold", 1);
    request.owner         = "faction:generic";
    request.product       = "custom_job";
    request.duration      = eve::Duration::fromNanoseconds(1);
    request.transactionId = "rts.generic.custom-job";
    auto built            = eve::rts::RTSProductionActionAdapter::build(std::move(request));
    REQUIRE(built.ok());
    const auto taskId = built.value().productionTaskId;
    CHECK(!production.find(taskId)->get().settlementRequired);
    REQUIRE(production.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(1)}).ok());
    CHECK_EQ(production.find(taskId)->get().state, eve::production::TaskState::Completed);
}

TEST_CASE("resource.gameplay.rtsProductionCancellationRefundsAllResourcesAtomically") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 20), 20);
    REQUIRE_EQ(ledger.credit("gas", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue  production;
    eve::orders::CommandQueue   orders;
    eve::action::ActionRuntime  action;
    auto                        costResult = eve::resource::CostSpec::from({{"gold", 5}, {"gas", 3}});
    REQUIRE(costResult.ok());
    auto cost = std::move(costResult).takeValue();

    eve::rts::RTSBuildRequest build;
    build.production     = &production;
    build.orders         = &orders;
    build.action         = &action;
    build.account        = &economy.account();
    build.cost           = cost;
    build.owner          = "faction:one";
    build.productionKind = "unit";
    build.product        = "tank";
    build.duration       = eve::Duration::fromNanoseconds(1000000000);
    auto built           = eve::rts::RTSProductionActionAdapter::build(std::move(build));
    REQUIRE(built.ok());
    auto receipt = std::move(built).takeValue();
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(ledger.get("gas"), 7);

    eve::rts::RTSCancelProductionRequest invalid;
    invalid.production          = &production;
    invalid.orders              = &orders;
    invalid.account             = &economy.account();
    invalid.refund              = cost;
    invalid.productionTaskId    = "missing-task";
    invalid.orderId             = receipt.orderId;
    const auto productionBefore = production.snapshot().expect("production cancellation snapshot");
    auto       failed           = eve::rts::RTSProductionActionAdapter::cancel(std::move(invalid));
    CHECK(!failed.ok());
    CHECK_EQ(production.snapshot().expect("unchanged production snapshot"), productionBefore);
    CHECK_EQ(ledger.get("gold"), 15);
    CHECK_EQ(ledger.get("gas"), 7);

    eve::rts::RTSCancelProductionRequest cancel;
    cancel.production       = &production;
    cancel.orders           = &orders;
    cancel.account          = &economy.account();
    cancel.refund           = cost;
    cancel.productionTaskId = receipt.productionTaskId;
    cancel.orderId          = receipt.orderId;
    auto cancelled          = eve::rts::RTSProductionActionAdapter::cancel(std::move(cancel));
    REQUIRE(cancelled.ok());
    CHECK_EQ(ledger.get("gold"), 20);
    CHECK_EQ(ledger.get("gas"), 10);
    auto task  = production.find(receipt.productionTaskId);
    auto order = orders.find(receipt.orderId);
    REQUIRE(task.has_value());
    REQUIRE(order.has_value());
    CHECK_EQ(static_cast<int>(task->get().state), static_cast<int>(eve::production::TaskState::Cancelled));
    CHECK_EQ(static_cast<int>(order->get().state), static_cast<int>(eve::orders::OrderState::Cancelled));
}

TEST_CASE("resource.gameplay.rtsProductionPriorityProtectsCanonicalResourceFloor") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 10), 10);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::production::WorkQueue  production;
    eve::orders::CommandQueue   orders;
    eve::action::ActionRuntime  action;
    auto                        duration    = eve::Duration::fromSeconds(1.0).expect("production duration");
    const auto                  reserveCost = eve::resource::ResourceCost::create("gold", 6).expect("reserve cost");

    const auto makeRequest = [&](int priority, std::string id) {
        eve::rts::RTSBuildRequest request;
        request.production     = &production;
        request.orders         = &orders;
        request.action         = &action;
        request.account        = &economy.account();
        request.cost           = makeCost("gold", 5);
        request.owner          = "faction:one";
        request.productionKind = "unit";
        request.product        = "soldier";
        request.duration       = duration;
        request.priority       = priority;
        request.transactionId  = std::move(id);
        request.resourceReserves.push_back({reserveCost, 10});
        return request;
    };

    auto low = eve::rts::RTSProductionActionAdapter::build(makeRequest(9, "rts.floor.low"));
    CHECK(!low.ok());
    CHECK_EQ(ledger.get("gold"), 10);
    CHECK_EQ(production.taskCount(), 0);
    CHECK_EQ(orders.orderCount(), 0);
    CHECK_EQ(action.executionCount(), 0u);

    auto high = eve::rts::RTSProductionActionAdapter::build(makeRequest(10, "rts.floor.high"));
    REQUIRE(high.ok());
    CHECK_EQ(ledger.get("gold"), 5);
    CHECK_EQ(production.taskCount(), 1);
}

TEST_CASE("resource.gameplay.economySnapshotRestoresBalancesAndAccounting") {
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("gold", 50), 50);
    REQUIRE(ledger.debit("gold", 12));
    REQUIRE_EQ(ledger.credit("gas", 9), 9);
    const auto saved = ledger.snapshot();

    REQUIRE(ledger.debit("gold", 20));
    REQUIRE_EQ(ledger.credit("gas", 4), 4);
    ledger.restore(saved);

    CHECK_EQ(ledger.get("gold"), 38);
    CHECK_EQ(ledger.getIncome("gold"), 50);
    CHECK_EQ(ledger.getExpense("gold"), 12);
    CHECK_EQ(ledger.get("gas"), 9);
    CHECK_EQ(ledger.getIncome("gas"), 9);
    CHECK_EQ(ledger.getExpense("gas"), 0);
}
}  // namespace test_resource_account
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_crafting)
#include "PrivateTestScope.h"
#include <limits>
#include "inventory/Bag.h"
#include "inventory/InventoryResourceAccount.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/Crafting.h"
#include "zeroerr/unittest.h"
// rpg_crafting.cpp
namespace test_rpg_crafting {
using namespace eve;

TEST_CASE("rpg.craftingAlchemyPaysQueuesRetriesAndSettlesOnce") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::clearEvents();
    inventory::InventorySystem::ensureBuiltins();
    for (const auto &id : {"herb", "water", "healing_potion"}) {
        inventory::ItemDefinition item;
        item.id       = id;
        item.maxStack = 99;
        inventory::ItemRegistry::registerItem(item);
    }

    inventory::Bag ingredients(4);
    ingredients.setId("player.ingredients");
    const int herbsAdded = ingredients.addItem("herb", 5);
    const int waterAdded = ingredients.addItem("water", 2);
    REQUIRE_EQ(herbsAdded, 5);
    REQUIRE_EQ(waterAdded, 2);
    inventory::InventoryResourceAccount account(ingredients);

    auto cost = resource::CostSpec::from({{"herb", 2}, {"water", 1}});
    REQUIRE(cost.ok());
    auto recipeRef = DefinitionRef::parse("recipe:healing_potion");
    REQUIRE(recipeRef.ok());
    rpg::CraftingRecipe recipe;
    recipe.id          = "healing_potion";
    recipe.process     = "alchemy";
    recipe.definition  = {std::move(recipeRef).takeValue(), Generation(4)};
    recipe.ingredients = std::move(cost).takeValue();
    recipe.outputs     = {{"healing_potion", 2}};
    recipe.duration    = Duration::fromSeconds(2.0).expect("craft duration");

    production::WorkQueue queue;
    rpg::CraftingRequest  request;
    request.production  = &queue;
    request.ingredients = &account;
    request.owner       = "player:alchemy_table";
    request.recipe      = recipe;
    request.batchSize   = 2;
    auto begun          = rpg::Crafting::begin(std::move(request));
    REQUIRE(begun.ok());
    const std::string taskId = begun.value().productionTaskId;
    CHECK_EQ(ingredients.countItem("herb"), 1);
    CHECK_EQ(ingredients.countItem("water"), 0);
    REQUIRE(queue.advance({SimulationTick(1), recipe.duration}).ok());
    CHECK_EQ(queue.find(taskId)->get().state, production::TaskState::ReadyToSettle);
    CHECK_EQ(queue.find(taskId)->get().definition.generation.value(), std::uint64_t{4});
    CHECK_EQ(queue.find(taskId)->get().batchSize, std::uint32_t{2});

    inventory::Bag full(0);
    full.setId("full.output");
    auto blocked = rpg::Crafting::settle(queue, full, taskId);
    CHECK(!blocked.ok());
    CHECK_EQ(queue.find(taskId)->get().state, production::TaskState::SettlementFailed);

    inventory::Bag output(2);
    output.setId("player.output");
    auto settled = rpg::Crafting::settle(queue, output, taskId);
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value(), 4);
    CHECK_EQ(output.countItem("healing_potion"), 4);
    CHECK_EQ(queue.find(taskId)->get().state, production::TaskState::Completed);

    auto repeated = rpg::Crafting::settle(queue, output, taskId);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.status().code(), StatusCode::NoOp);
    CHECK_EQ(output.countItem("healing_potion"), 4);
}

TEST_CASE("rpg.craftingRejectsInsufficientIngredientsWithoutQueueMutation") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::ensureBuiltins();
    inventory::ItemDefinition herb;
    herb.id       = "herb";
    herb.maxStack = 99;
    inventory::ItemRegistry::registerItem(herb);
    inventory::ItemDefinition potion;
    potion.id       = "healing_potion";
    potion.maxStack = 99;
    inventory::ItemRegistry::registerItem(potion);

    inventory::Bag                      bag(2);
    inventory::InventoryResourceAccount account(bag);
    auto                                cost = resource::CostSpec::single("herb", 1);
    REQUIRE(cost.ok());
    rpg::CraftingRecipe recipe;
    recipe.id          = "healing_potion";
    recipe.ingredients = std::move(cost).takeValue();
    recipe.outputs     = {{"healing_potion", 1}};
    recipe.duration    = Duration::fromSeconds(1.0).expect("craft duration");
    production::WorkQueue queue;
    rpg::CraftingRequest  request{&queue, &account, "player", recipe};
    auto                  result = rpg::Crafting::begin(std::move(request));
    CHECK(!result.ok());
    CHECK_EQ(queue.taskCount(), 0);
}

TEST_CASE("rpg.craftingRejectsAggregateOutputOverflowBeforePayment") {
    inventory::ItemRegistry::clear();
    inventory::InventorySystem::ensureBuiltins();
    inventory::ItemDefinition herb;
    herb.id       = "herb";
    herb.maxStack = 99;
    inventory::ItemRegistry::registerItem(herb);
    inventory::Bag ingredients(1);
    REQUIRE_EQ(ingredients.addItem("herb", 1), 1);
    inventory::InventoryResourceAccount account(ingredients);
    auto                                cost = resource::CostSpec::single("herb", 1);
    REQUIRE(cost.ok());

    rpg::CraftingRecipe recipe;
    recipe.id          = "overflowing_recipe";
    recipe.ingredients = std::move(cost).takeValue();
    recipe.outputs     = {{"first", std::numeric_limits<int>::max()}, {"second", 1}};
    recipe.duration    = Duration::fromNanoseconds(1);
    production::WorkQueue queue;
    rpg::CraftingRequest  request{&queue, &account, "player", recipe};
    auto                  result = rpg::Crafting::begin(std::move(request));
    CHECK(!result.ok());
    CHECK_EQ(ingredients.countItem("herb"), 1);
    CHECK_EQ(queue.taskCount(), 0);
}
}  // namespace test_rpg_crafting
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_equipment)
#include "PrivateTestScope.h"
#include <cmath>
#include <random>
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "inventory/Item.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/LootSystem.h"
#include "rpg/RPG.h"
#include "rpg/RPGActor.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_equipment.cpp
namespace test_rpg_equipment {
using namespace eve::rpg;

namespace {
bool approxEq(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
}  // namespace

TEST_CASE("rpg.equipment.statSyncAppliesAndRemoves") {
    auto *rpg = RPG::create();
    rpg->clearAllItemStats();
    int registered = rpg->registerItemStatsFromJson("sword",
                                                    R"([
           {"attribute":"attack","op":"add","value":15},
           {"attribute":"attack","op":"mulMul","value":0.1}
         ])");
    CHECK_EQ(registered, 2);
    CHECK_EQ(rpg->getItemStatCount(), 1);

    RPGActor *actor = rpg->newActor();
    actor->setBaseAttribute("attack", 10.0);

    eve::inventory::EquipmentSet equip;
    equip.setId("hero");
    equip.defineSlot("weapon");
    equip.defineSlot("armor");

    // 未装备：sync 不应产生加成
    CHECK_EQ(rpg->syncEquipModifiers(actor, &equip), 0);
    CHECK(approxEq(actor->getFinalAttribute("attack"), 10.0));

    // 直接往武器槽塞物品（模拟已装备 sword）
    equip.stackAt("weapon")->itemId   = "sword";
    equip.stackAt("weapon")->quantity = 1;
    int applied                       = rpg->syncEquipModifiers(actor, &equip);
    CHECK_EQ(applied, 2);
    // 10 + 15 = 25，再 * (1 + 0.1) = 27.5
    CHECK(approxEq(actor->getFinalAttribute("attack"), 27.5));

    // 换装/卸下后重新 sync 应移除旧加成
    equip.stackAt("weapon")->clear();
    CHECK_EQ(rpg->syncEquipModifiers(actor, &equip), 0);
    CHECK(approxEq(actor->getFinalAttribute("attack"), 10.0));

    rpg->clearAllItemStats();
    actor->release();
}

TEST_CASE("rpg.loot.rollGuaranteedAndSeeded") {
    auto *rpg = RPG::create();
    rpg->clearLootTables();
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition gold;
    gold.id          = "gold";
    gold.displayName = "Gold";
    gold.maxStack    = 999;
    eve::inventory::ItemDefinition potion;
    potion.id          = "potion";
    potion.displayName = "Potion";
    potion.maxStack    = 20;
    eve::inventory::ItemRegistry::registerItem(gold);
    eve::inventory::ItemRegistry::registerItem(potion);

    int tables = rpg->registerLootTablesFromJson(R"([
      {"id":"goblin","entries":[
        {"itemId":"gold","chance":1.0,"minQty":5,"maxQty":5},
        {"itemId":"potion","chance":0.0,"minQty":1,"maxQty":1}
      ]}
    ])");
    CHECK_EQ(tables, 1);
    CHECK_EQ(rpg->getLootTableCount(), 1);

    eve::inventory::Bag bag(20);
    bag.setId("player");
    int drops = rpg->rollLoot("goblin", &bag, 42);
    CHECK_EQ(drops, 1);  // 只有 gold 必掉
    CHECK_EQ(bag.countItem("gold"), 5);
    CHECK_EQ(bag.countItem("potion"), 0);

    // 未知表返回 0
    eve::inventory::Bag bag2(20);
    int                 missing = rpg->rollLoot("missing", &bag2, 42);
    CHECK_EQ(missing, 0);

    rpg->clearLootTables();
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("rpg.loot.rollDirectWithRng") {
    LootSystem::clear();
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition a;
    a.id          = "a";
    a.displayName = "A";
    a.maxStack    = 999;
    eve::inventory::ItemRegistry::registerItem(a);
    int tables = LootSystem::registerTablesFromJson(R"([
      {"id":"t","entries":[{"itemId":"a","chance":1.0,"minQty":2,"maxQty":2}]}
    ])");
    CHECK_EQ(tables, 1);
    eve::inventory::Bag bag(20);
    std::mt19937        rng(7);
    int                 rolls = LootSystem::roll("t", &bag, rng);
    CHECK_EQ(rolls, 1);
    CHECK_EQ(bag.countItem("a"), 2);
    LootSystem::clear();
    eve::inventory::ItemRegistry::clear();
}
}  // namespace test_rpg_equipment
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_quest_reward)
#include "PrivateTestScope.h"
#include <string>
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/QuestReward.h"
#include "rpg/Tracker.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_quest_reward.cpp
namespace test_rpg_quest_reward {
namespace {

void installRewardFixture() {
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::inventory::ItemDefinition potion;
    potion.id       = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);

    eve::rpg::QuestDefinition quest;
    quest.id             = "quest.reward";
    quest.startPolicy    = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"win", "win", "encounter", 1});
    quest.rewards.push_back({"item", "potion", 2.0});
    quest.rewards.push_back({"attribute", "gold", 5.0});
    eve::rpg::QuestRegistry::registerQuest(quest);
}

void makeReady(eve::rpg::Tracker &tracker) {
    REQUIRE(tracker.activate("quest.reward"));
    tracker.notify("win", "encounter", 1);
    REQUIRE_EQ(tracker.getState("quest.reward"), std::string("ready"));
}

void clearRewardFixture() {
    eve::inventory::InventorySystem::unregisterChangeHook("test.quest-reward");
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
}

}  // namespace

TEST_CASE("rpg.questReward.commitsAllOwnersBeforePublishingInventoryEvents") {
    installRewardFixture();
    eve::rpg::Tracker tracker;
    makeReady(tracker);
    eve::rpg::GameState state;
    state.setVariable("gold", 10.0);
    eve::inventory::Bag bag(2);
    bag.setId("hero");
    eve::inventory::InventorySystem::clearEvents();

    int  hookCalls         = 0;
    bool hookSawFinalState = false;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.quest-reward", [&](const eve::inventory::InventoryChangeEvent &) {
            ++hookCalls;
            hookSawFinalState = tracker.getState("quest.reward") == "completed" && state.getVariable("gold") == 15.0 &&
                                bag.countItem("potion") == 2;
        });

    auto claimed = eve::rpg::QuestReward::claim(tracker, state, bag, "quest.reward");
    REQUIRE(claimed.ok());
    CHECK_EQ(std::move(claimed).takeValue(), 2);
    CHECK_EQ(tracker.getState("quest.reward"), std::string("completed"));
    CHECK_EQ(state.getVariable("gold"), 15.0);
    CHECK_EQ(bag.countItem("potion"), 2);
    CHECK_EQ(hookCalls, 1);
    CHECK(hookSawFinalState);
    CHECK_EQ(eve::inventory::InventorySystem::events().size(), std::size_t(1));
    clearRewardFixture();
}

TEST_CASE("rpg.questReward.capacityFailureLeavesEveryOwnerAndObserverUntouched") {
    installRewardFixture();
    eve::rpg::Tracker tracker;
    makeReady(tracker);
    eve::rpg::GameState state;
    state.setVariable("gold", 10.0);
    eve::inventory::Bag bag(0);
    eve::inventory::InventorySystem::clearEvents();
    const auto pendingBefore = tracker.pending;
    int        hookCalls     = 0;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.quest-reward", [&](const eve::inventory::InventoryChangeEvent &) { ++hookCalls; });

    auto claimed = eve::rpg::QuestReward::claim(tracker, state, bag, "quest.reward");
    REQUIRE(!claimed.ok());
    CHECK_EQ(tracker.getState("quest.reward"), std::string("ready"));
    CHECK_EQ(state.getVariable("gold"), 10.0);
    CHECK_EQ(bag.countItem("potion"), 0);
    CHECK_EQ(tracker.pending.size(), pendingBefore.size());
    CHECK_EQ(hookCalls, 0);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearRewardFixture();
}

TEST_CASE("inventory.addBatch.rejectsStalePreparedStateWithoutPublishing") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition potion;
    potion.id       = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
    eve::inventory::Bag bag(2);
    eve::inventory::InventorySystem::clearEvents();

    auto prepared = eve::inventory::InventorySystem::prepareAddBatch(
        &bag, std::vector<eve::inventory::InventoryItemGrant>{{"potion", 2}});
    REQUIRE(prepared.ok());
    const int addedOutsideBatch = bag.addItem("potion", 1);
    REQUIRE_EQ(addedOutsideBatch, 1);
    eve::inventory::InventorySystem::clearEvents();
    auto committed = eve::inventory::InventorySystem::commitAddBatch(std::move(prepared).takeValue());
    REQUIRE(!committed.ok());
    CHECK_EQ(bag.countItem("potion"), 1);
    CHECK(eve::inventory::InventorySystem::events().empty());
    eve::inventory::ItemRegistry::clear();
}
}  // namespace test_rpg_quest_reward
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_save_prepare)
#include "PrivateTestScope.h"
#include <utility>
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Tracker.h"
#include "zeroerr/unittest.h"
// rpg_save_prepare.cpp
namespace test_rpg_save_prepare {
TEST_CASE("rpg.saveSession.preparedRestorePublishesOnlyAtCommit") {
    eve::rpg::QuestRegistry::clear();
    eve::rpg::GameState state;
    state.setVariable("gold", 25.0);
    eve::rpg::Tracker            tracker;
    auto                        *actor = eve::rpg::RPGActor::createActor();
    eve::inventory::Bag          bag(2);
    eve::inventory::EquipmentSet equipment;
    eve::rpg::RPGSaveSession     session;
    session.bind(state, tracker, *actor, bag, equipment);
    REQUIRE(session.setContentVersion("test.prepared.v1").ok());
    auto saved = session.snapshotJson();
    REQUIRE(saved.ok());

    state.setVariable("gold", 99.0);
    auto prepared = session.prepareRestoreSnapshotJson(saved.value());
    REQUIRE(prepared.ok());
    CHECK_EQ(state.getVariable("gold"), 99.0);

    session.commitPrepared(std::move(prepared).takeValue());
    CHECK_EQ(state.getVariable("gold"), 25.0);
    actor->release();
}
}  // namespace test_rpg_save_prepare
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_shop_transaction)
#include "PrivateTestScope.h"
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/ShopCatalogue.h"
#include "rpg/ShopTransaction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rpg_shop_transaction.cpp
namespace test_rpg_shop_transaction {
namespace {

void installShopFixture() {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemDefinition potion;
    potion.id       = "potion";
    potion.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(potion);
}

void clearShopFixture() {
    eve::inventory::InventorySystem::unregisterChangeHook("test.shop");
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::ShopCatalogue::clear();
}

}  // namespace

TEST_CASE("rpg.shop.buyAndSellPublishOnlyCommittedFinalState") {
    installShopFixture();
    auto catalogue = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.potion","itemId":"potion","name":"Potion","desc":"Heal","buyPrice":20,"sellPrice":10}])");
    REQUIRE(catalogue.ok());
    eve::rpg::GameState state;
    state.setVariable("gold", 100.0);
    eve::inventory::Bag bag(2);
    bag.setId("hero");
    int  hookCalls         = 0;
    bool hookSawFinalState = true;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.shop", [&](const eve::inventory::InventoryChangeEvent &event) {
            ++hookCalls;
            if (event.action == "add")
                hookSawFinalState =
                    hookSawFinalState && state.getVariable("gold") == 60.0 && bag.countItem("potion") == 2;
            if (event.action == "remove")
                hookSawFinalState =
                    hookSawFinalState && state.getVariable("gold") == 70.0 && bag.countItem("potion") == 1;
        });

    auto bought = eve::rpg::ShopTransaction::buyOffer(state, bag, "gold", "village.potion", 2);
    REQUIRE(bought.ok());
    CHECK_EQ(std::move(bought).takeValue(), 2);
    auto sold = eve::rpg::ShopTransaction::sellOffer(state, bag, "gold", "village.potion", 1);
    REQUIRE(sold.ok());
    CHECK_EQ(std::move(sold).takeValue(), 1);
    CHECK_EQ(state.getVariable("gold"), 70.0);
    CHECK_EQ(bag.countItem("potion"), 1);
    CHECK_EQ(hookCalls, 2);
    CHECK(hookSawFinalState);
    clearShopFixture();
}

TEST_CASE("rpg.shop.failuresLeaveCurrencyInventoryAndObserversUntouched") {
    installShopFixture();
    auto catalogue = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.potion","itemId":"potion","name":"Potion","desc":"Heal","buyPrice":20,"sellPrice":10}])");
    REQUIRE(catalogue.ok());
    eve::rpg::GameState state;
    state.setVariable("gold", 5.0);
    eve::inventory::Bag bag(0);
    int                 hookCalls = 0;
    eve::inventory::InventorySystem::registerChangeHook(
        "test.shop", [&](const eve::inventory::InventoryChangeEvent &) { ++hookCalls; });

    auto insufficient = eve::rpg::ShopTransaction::buyOffer(state, bag, "gold", "village.potion", 1);
    REQUIRE(!insufficient.ok());
    auto missing = eve::rpg::ShopTransaction::sellOffer(state, bag, "gold", "village.potion", 1);
    REQUIRE(!missing.ok());
    CHECK_EQ(state.getVariable("gold"), 5.0);
    CHECK_EQ(bag.countItem("potion"), 0);
    CHECK_EQ(hookCalls, 0);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearShopFixture();
}

TEST_CASE("rpg.shop.catalogueStrictReplacementIsOrderedAndAtomic") {
    installShopFixture();
    auto initial = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.potion","itemId":"potion","name":"Potion","desc":"Heal","buyPrice":20,"sellPrice":10}])");
    REQUIRE(initial.ok());
    REQUIRE_EQ(eve::rpg::ShopCatalogue::count(), 1);
    REQUIRE(eve::rpg::ShopCatalogue::at(0) != nullptr);
    CHECK_EQ(eve::rpg::ShopCatalogue::at(0)->buyPrice, 20);

    auto invalid = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","itemId":"missing","name":"Bad","desc":"","buyPrice":1,"sellPrice":2}])");
    REQUIRE(!invalid.ok());
    CHECK_EQ(eve::rpg::ShopCatalogue::count(), 1);
    CHECK(eve::rpg::ShopCatalogue::find("village.potion") != nullptr);
    auto unknownField = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"bad","itemId":"potion","name":"Bad","desc":"","buyPrice":1,"sellPrice":0,"typo":1}])");
    REQUIRE(!unknownField.ok());
    CHECK(eve::rpg::ShopCatalogue::find("village.potion") != nullptr);
    clearShopFixture();
}

TEST_CASE("inventory.removePreparedStateRejectsStaleBagWithoutPublishing") {
    installShopFixture();
    eve::inventory::Bag bag(2);
    REQUIRE_EQ(bag.addItem("potion", 2), 2);
    eve::inventory::InventorySystem::clearEvents();
    auto prepared = eve::inventory::InventorySystem::prepareRemove(&bag, "potion", 1);
    REQUIRE(prepared.ok());
    REQUIRE_EQ(bag.addItem("potion", 1), 1);
    eve::inventory::InventorySystem::clearEvents();
    auto committed = eve::inventory::InventorySystem::commitRemove(std::move(prepared).takeValue());
    REQUIRE(!committed.ok());
    CHECK_EQ(bag.countItem("potion"), 3);
    CHECK(eve::inventory::InventorySystem::events().empty());
    clearShopFixture();
}
}  // namespace test_rpg_shop_transaction
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rpg_world_interaction)
#include "PrivateTestScope.h"
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
namespace test_rpg_world_interaction {
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
}  // namespace test_rpg_world_interaction
#endif
