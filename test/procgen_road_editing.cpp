#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadNetwork.h"

#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

using namespace eve;
using namespace eve::editing;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;

namespace {

class RoadCommandRegistry final : public IEditingCommandRegistry {
public:
    eve::editing::Result<void> registerPlannedCommand(EditingCommandDescriptor descriptor,
                                                      EditingCommandPlanner planner) override {
        planners[descriptor.id.value()] = std::move(planner);
        owners[descriptor.id.value()]   = std::move(descriptor.ownerModule);
        return eve::editing::Result<void>::success();
    }

    eve::editing::Result<std::size_t> unregisterOwner(const std::string& owner) override {
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
    auto node = network.addNode(0.f, 0.f, 0.f, 3.f);
    REQUIRE(node.ok());
    RoadNetworkEditTarget target("road:node-radius", network);
    auto operation = target.makeSetNodeJunctionRadius(node.value(), 7.5f);
    REQUIRE(operation.ok());

    LocalWorldAuthority authority(&target);
    TransactionSpec spec;
    spec.id           = eve::editing::TransactionId("road-node-radius-1");
    spec.label        = "Change road junction radius";
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    const std::array<DomainOperation, 1> operations{operation.value()};
    auto plan = authority.preflight(spec, operations);
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
    auto node = network.addNode(0.f, 0.f, 0.f, 3.f);
    REQUIRE(node.ok());
    RoadNetworkEditTarget target("road:node-control", network);
    auto operation = target.makeSetNodeJunctionControl(node.value(), RoadJunctionControl::Signal);
    REQUIRE(operation.ok());

    LocalWorldAuthority authority(&target);
    TransactionSpec spec;
    spec.id           = eve::editing::TransactionId("road-node-control-1");
    spec.label        = "Change road junction control";
    spec.target       = target.targetId();
    spec.baseRevision = target.revision();
    const std::array<DomainOperation, 1> operations{operation.value()};
    auto plan = authority.preflight(spec, operations);
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
    const auto&     addedNode   = network.nodes().back();
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
    auto        west = network.addNode(-12.f, 0.f, 0.f, 3.f);
    auto        hub  = network.addNode(0.f, 0.f, 0.f, 3.f);
    auto        north = network.addNode(0.f, 0.f, -12.f, 3.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(north.ok());
    auto incoming = network.addEdge(west.value(), hub.value(), {{-12.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 0);
    auto outgoing = network.addEdge(hub.value(), north.value(), {{0.f, 0.f, 0.f}, {0.f, 0.f, -12.f}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());

    const RoadLaneConnection link{incoming.value(), 0, outgoing.value(), 0,
                                  RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    RoadNetworkEditTarget target("road:manual-links", network);
    auto                  add = target.makeAddLaneLink(link);
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
    auto west = network.addNode(-10.f, 0.f, 0.f);
    auto hub = network.addNode(0.f, 0.f, 0.f);
    auto east = network.addNode(10.f, 0.f, 0.f);
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
    const RoadLaneConnection turn{incoming.value(), 0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};

    RoadNetworkEditTarget target("road:block-turn", network);
    auto operation = target.makeBlockLaneLink(turn);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK_EQ(network.blockedLaneLinkCount(), 1);
    connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 0);

    RoadNetwork restored;
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
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto start = network.addNode(-12.f, 0.f, 0.f);
    auto oldHub = network.addNode(0.f, 0.f, 0.f);
    auto newHub = network.addNode(5.f, 0.f, 7.f);
    auto exit = network.addNode(0.f, 0.f, 12.f);
    auto newExit = network.addNode(14.f, 0.f, 7.f);
    auto newApproach = network.addNode(5.f, 0.f, -4.f);
    REQUIRE(start.ok());
    REQUIRE(oldHub.ok());
    REQUIRE(newHub.ok());
    REQUIRE(exit.ok());
    REQUIRE(newExit.ok());
    REQUIRE(newApproach.ok());
    auto road = network.addEdge(start.value(), oldHub.value(), {{}, {}}, 2, 0);
    auto branch = network.addEdge(oldHub.value(), exit.value(), {{}, {}}, 2, 0);
    auto newBranch = network.addEdge(newHub.value(), newExit.value(), {{}, {}}, 2, 0);
    auto existingApproach = network.addEdge(newApproach.value(), newHub.value(), {{}, {}}, 2, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());
    REQUIRE(newBranch.ok());
    REQUIRE(existingApproach.ok());
    const RoadLaneConnection active{road.value(), 0, branch.value(), 0, RoadLaneDirection::Forward,
                                    RoadLaneDirection::Forward};
    const RoadLaneConnection blocked{road.value(), 1, branch.value(), 1, RoadLaneDirection::Forward,
                                     RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(active).ok());
    REQUIRE(network.blockLaneLink(blocked).ok());

    RoadNetworkEditTarget target("road:reconnect", network);
    auto operation = target.makeReconnectEdgeEndpointAndConnect(road.value(), false, newHub.value());
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, newHub.value());
    CHECK_EQ(network.laneLinkCount(), 2);
    for (const auto& link : network.laneLinks()) {
        const bool referencesMovedRoad = link.inEdge == road.value() || link.outEdge == road.value();
        CHECK(referencesMovedRoad);
    }
    CHECK_EQ(network.blockedLaneLinkCount(), 0);

    DomainOperation undo = operation.value();
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto start = network.addNode(-10.f, 0.f, 0.f);
    auto oldHub = network.addNode(0.f, 0.f, 0.f);
    auto firstTie = network.addNode(0.2f, 0.f, 0.f);
    auto secondTie = network.addNode(-0.2f, 0.f, 0.f);
    auto exit = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(start.ok());
    REQUIRE(oldHub.ok());
    REQUIRE(firstTie.ok());
    REQUIRE(secondTie.ok());
    REQUIRE(exit.ok());
    auto road = network.addEdge(start.value(), oldHub.value(), {{}, {}}, 1, 0);
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
    request.payload = EditorValue::Object{{"id", std::int64_t{road.value()}},
                                          {"fromEndpoint", false},
                                          {"maxDistance", 0.25}};
    auto planned = registry.planners.at("road.edge.snap-endpoint.v1")(target, request);
    REQUIRE(planned.ok());
    REQUIRE_EQ(planned.value().operations.size(), std::size_t{1});
    const auto operation = planned.value().operations.front();
    REQUIRE(target.applyDomainOperation(operation).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, firstTie.value());
    REQUIRE_EQ(network.laneLinkCount(), 1);
    CHECK_EQ(network.laneLinks().front().outEdge, branch.value());

    DomainOperation undo = operation;
    undo.type = operation.inverseType;
    undo.payload = operation.inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(network.edgeResult(road.value()).value().to, oldHub.value());
    CHECK_EQ(network.laneLinkCount(), 0);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.editing.detachEndpointCommandUndoRestoresJunctionExactly") {
    RoadNetwork network;
    auto start = network.addNode(-10.f, 0.f, 0.f);
    auto hub = network.addNode(0.f, 0.f, 0.f, 4.f);
    auto exit = network.addNode(0.f, 0.f, 10.f);
    REQUIRE(start.ok());
    REQUIRE(hub.ok());
    REQUIRE(exit.ok());
    auto road = network.addEdge(start.value(), hub.value(), {{}, {}}, 2, 0);
    auto branch = network.addEdge(hub.value(), exit.value(), {{}, {}}, 2, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());
    const RoadLaneConnection active{road.value(), 0, branch.value(), 0, RoadLaneDirection::Forward,
                                    RoadLaneDirection::Forward};
    const RoadLaneConnection blocked{road.value(), 1, branch.value(), 1, RoadLaneDirection::Forward,
                                     RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(active).ok());
    REQUIRE(network.blockLaneLink(blocked).ok());

    RoadNetworkEditTarget target("road:detach-endpoint", network);
    RoadCommandRegistry registry;
    REQUIRE(registerRoadNetworkEditingCommands(registry).ok());
    CommandRequest request;
    request.id = eve::editing::CommandId("road.edge.detach-endpoint.v1");
    request.payload = EditorValue::Object{{"id", std::int64_t{road.value()}}, {"fromEndpoint", false}};
    auto planned = registry.planners.at("road.edge.detach-endpoint.v1")(target, request);
    REQUIRE(planned.ok());
    const auto operation = planned.value().operations.front();
    REQUIRE(target.applyDomainOperation(operation).ok());
    REQUIRE_EQ(network.nodeCount(), 4);
    const auto detachedNodeId = network.edgeResult(road.value()).value().to;
    CHECK_NE(detachedNodeId, hub.value());
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK_EQ(network.blockedLaneLinkCount(), 0);

    DomainOperation undo = operation;
    undo.type = operation.inverseType;
    undo.payload = operation.inverse;
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
    for (const auto& primitive : before.primitives) {
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
    for (const auto& primitive : after.primitives) {
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
    style.laneWidth       = 4.1f;
    style.uvMeters        = 7.f;
    style.speedLimitMps   = 13.5f;
    style.trafficPriority = 9;
    style.sideObjectStartOffset = 1.5f;
    style.sideObjectEndOffset   = 2.5f;
    style.sideObjectsLeft       = false;
    auto incoming = source.addEdge(west.value(), hub.value(), {{-12.f, 1.f, 0.f}, {-5.f, 1.5f, 2.f}, {0.f, 1.f, 0.f}},
                                   2, 1, style);
    auto outgoing = source.addEdge(hub.value(), north.value(), {{0.f, 1.f, 0.f}, {2.f, 2.f, -6.f}, {0.f, 2.f, -12.f}},
                                   1, 1, style);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    const RoadLaneConnection link{incoming.value(), 1, outgoing.value(), 0,
                                  RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(source.addLaneLink(link).ok());

    RoadNetworkEditTarget sourceTarget("road:snapshot-source", source);
    EditorValue           snapshot = sourceTarget.snapshotValue();
    auto*                 object   = snapshot.getIf<EditorValue::Object>();
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
    auto* legacyObject = legacySnapshot.getIf<EditorValue::Object>();
    REQUIRE(legacyObject != nullptr);
    auto* legacyEdges = (*legacyObject)["edges"].getIf<EditorValue::Array>();
    auto* legacyNodes = (*legacyObject)["nodes"].getIf<EditorValue::Array>();
    REQUIRE(legacyEdges != nullptr);
    REQUIRE(legacyNodes != nullptr);
    for (auto& encodedNode : *legacyNodes) {
        auto* nodeObject = encodedNode.getIf<EditorValue::Object>();
        REQUIRE(nodeObject != nullptr);
        nodeObject->erase("junctionControl");
    }
    for (auto& encodedEdge : *legacyEdges) {
        auto* edgeObject = encodedEdge.getIf<EditorValue::Object>();
        REQUIRE(edgeObject != nullptr);
        auto* encodedStyle = (*edgeObject)["style"].getIf<EditorValue::Object>();
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
    EditorValue snapshot = target.snapshotValue();
    auto* object = snapshot.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    auto* links = (*object)["laneLinks"].getIf<EditorValue::Array>();
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
    EditorValue snapshot = target.snapshotValue();
    auto* object = snapshot.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    auto* edges = (*object)["edges"].getIf<EditorValue::Array>();
    REQUIRE(edges != nullptr);
    REQUIRE_EQ(edges->size(), std::size_t{1});
    auto* edge = edges->front().getIf<EditorValue::Object>();
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

    auto execute = [&](const std::string& command, EditorValue payload) {
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
    execute("road.node.radius.set.v1",
            EditorValue::Object{{"id", std::int64_t{a}}, {"junctionRadius", 4.5}});
    CHECK_EQ(network.nodeResult(a).value().junctionRadius, 4.5f);
    execute("road.node.junction-control.set.v1",
            EditorValue::Object{{"id", std::int64_t{a}}, {"junctionControl", std::int64_t{3}}});
    CHECK_EQ(network.nodeResult(a).value().junctionControl, RoadJunctionControl::Signal);
    execute("road.edge.add.v1",
            EditorValue::Object{{"from", std::int64_t{a}},
                                {"to", std::int64_t{b}},
                                {"lanesForward", std::int64_t{2}},
                                {"lanesBackward", std::int64_t{1}},
                                {"points", EditorValue::Array{EditorValue::Array{-10.0, 0.0, 0.0},
                                                              EditorValue::Array{0.0, 0.0, 3.0},
                                                              EditorValue::Array{10.0, 0.0, 0.0}}}});
    REQUIRE_EQ(network.edgeCount(), 1);
    REQUIRE(network.validate().ok());
    EditorValue styleSnapshot = target.snapshotValue();
    auto* styleSnapshotObject = styleSnapshot.getIf<EditorValue::Object>();
    REQUIRE(styleSnapshotObject != nullptr);
    auto* styleEdges = (*styleSnapshotObject)["edges"].getIf<EditorValue::Array>();
    REQUIRE(styleEdges != nullptr);
    REQUIRE_EQ(styleEdges->size(), std::size_t{1});
    auto* styleEdge = styleEdges->front().getIf<EditorValue::Object>();
    REQUIRE(styleEdge != nullptr);
    auto* stylePayload = (*styleEdge)["style"].getIf<EditorValue::Object>();
    REQUIRE(stylePayload != nullptr);
    (*stylePayload)["laneWidth"] = 4.25;
    (*stylePayload)["sideObjectStartOffset"] = 2.0;
    (*stylePayload)["sideObjectsRight"] = false;
    execute("road.edge.style.set.v1", EditorValue(*stylePayload));
    CHECK_EQ(network.edges().front().style.laneWidth, 4.25f);
    CHECK_EQ(network.edges().front().style.sideObjectStartOffset, 2.f);
    CHECK(!network.edges().front().style.sideObjectsRight);
    execute("road.edge.control-points.set.v1",
            EditorValue::Object{{"id", std::int64_t{network.edges().front().id}},
                                {"points", EditorValue::Array{EditorValue::Array{-10.0, 0.0, 0.0},
                                                              EditorValue::Array{-4.0, 1.0, 5.0},
                                                              EditorValue::Array{4.0, 1.0, 5.0},
                                                              EditorValue::Array{10.0, 0.0, 0.0}}}});
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
    execute("road.edge.lanes.set.v1",
            EditorValue::Object{{"id", std::int64_t{network.edges().front().id}},
                                {"lanesForward", std::int64_t{1}},
                                {"lanesBackward", std::int64_t{0}}});
    CHECK_EQ(network.edges().front().lanesForward, 1);
    CHECK_EQ(network.edges().front().lanesBackward, 0);
    execute("road.node.add.v1", EditorValue::Object{{"x", 20.0}, {"y", 0.0}, {"z", 10.0}});
    const auto c = network.nodes().back().id;
    execute("road.edge.add.v1",
            EditorValue::Object{{"from", std::int64_t{b}},
                                {"to", std::int64_t{c}},
                                {"lanesForward", std::int64_t{1}},
                                {"lanesBackward", std::int64_t{0}},
                                {"points", EditorValue::Array{EditorValue::Array{10.0, 0.0, 0.0},
                                                              EditorValue::Array{15.0, 0.0, 4.0},
                                                              EditorValue::Array{20.0, 0.0, 10.0}}}});
    const auto branchEdge = network.edges().back().id;
    execute("road.node.connect-all-turns.v1", EditorValue::Object{{"id", std::int64_t{b}}});
    CHECK_EQ(network.laneLinkCount(), 1);
    execute("road.node.add.v1", EditorValue::Object{{"x", 10.0}, {"y", 0.0}, {"z", 12.0}});
    const auto branchSource = network.nodes().back().id;
    execute("road.node.connect-at-position.v1",
            EditorValue::Object{{"sourceNodeId", std::int64_t{branchSource}},
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
            EditorValue::Object{{"id", std::int64_t{commandEdgeId}},
                                {"fromEndpoint", true},
                                {"nodeId", std::int64_t{branchSource}}});
    CHECK_EQ(network.edgeResult(commandEdgeId).value().from, branchSource);
    REQUIRE(network.validate().ok());
    execute("road.node.add.v1", EditorValue::Object{{"x", 30.0}, {"y", 0.0}, {"z", 0.0}});
    execute("road.node.add.v1", EditorValue::Object{{"x", 30.25}, {"y", 0.0}, {"z", 0.0}});
    const auto mergeKeep = network.nodes()[network.nodes().size() - 2].id;
    const auto mergeRemove = network.nodes().back().id;
    execute("road.node.merge.v1", EditorValue::Object{{"keepNodeId", std::int64_t{mergeKeep}},
                                                       {"removeNodeId", std::int64_t{mergeRemove}},
                                                       {"maxDistance", 0.5}});
    CHECK_EQ(network.nodeCount(), 6);
    CHECK(!network.nodeResult(mergeRemove).ok());
    const auto snapshot = target.capability<IEditingSnapshotProvider>()->get().snapshotValue();
    const auto* schema  = snapshot.getIf<EditorValue::Object>();
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
    const RoadLaneConnection blockedOuterTurn{incoming.value(), 3, outgoing.value(), 3,
                                              RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.blockLaneLink(blockedOuterTurn).ok());
    REQUIRE_EQ(network.laneLinkCount(), 3);
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);

    RoadNetworkEditTarget target("road:lane-count", network);
    auto shrink = target.makeSetEdgeLaneCounts(outgoing.value(), 2, 0);
    REQUIRE(shrink.ok());
    const auto beforeRevision = network.revision();
    REQUIRE(target.applyDomainOperation(shrink.value()).ok());
    CHECK_EQ(network.revision(), beforeRevision + 1);
    CHECK_EQ(network.edgeResult(outgoing.value()).value().lanesForward, 2);
    CHECK_EQ(network.laneLinkCount(), 2);
    CHECK_EQ(network.blockedLaneLinkCount(), 0);
    for (const auto& link : network.laneLinks()) CHECK_LT(link.outLane, 2);
    REQUIRE(network.validate().ok());

    DomainOperation undo = shrink.value();
    undo.payload          = shrink.value().inverse;
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
    undo.type             = connect.value().inverseType;
    undo.payload          = connect.value().inverse;
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
    const RoadLaneConnection endpointLink{road.value(), 0, exit.value(), 0, RoadLaneDirection::Forward,
                                          RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(endpointLink).ok());
    const RoadLaneConnection blockedEndpointLink{road.value(), 1, exit.value(), 0,
                                                 RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.blockLaneLink(blockedEndpointLink).ok());
    const int originalNodes = network.nodeCount(), originalEdges = network.edgeCount();

    RoadNetworkEditTarget target("road:split", network);
    auto                  split = target.makeSplitEdge(road.value(), 1, 3.5f);
    REQUIRE(split.ok());
    REQUIRE(target.applyDomainOperation(split.value()).ok());
    CHECK_EQ(network.nodeCount(), originalNodes + 1);
    CHECK_EQ(network.edgeCount(), originalEdges + 1);
    REQUIRE(network.validate().ok());
    const auto continuation = std::find_if(network.edges().begin(), network.edges().end(), [&](const auto& edge) {
        return edge.id != road.value() && edge.id != exit.value();
    });
    REQUIRE(continuation != network.edges().end());
    const std::uint32_t continuationId = continuation->id;
    const std::uint32_t insertedNodeId = continuation->from;
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinks().front().inEdge, continuationId);

    DomainOperation undo = split.value();
    undo.type             = split.value().inverseType;
    undo.payload          = split.value().inverse;
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
    auto from = network.addNode(-12.f, 4.f, 0.f);
    auto to = network.addNode(12.f, 4.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto road = network.addEdge(from.value(), to.value(), {{-12.f, 4.f, 0.f}, {12.f, 4.f, 0.f}}, 1, 0);
    REQUIRE(road.ok());

    RoadNetworkEditTarget target("road:position-split", network);
    auto operation = target.makeSplitEdgeAtPosition(road.value(), {3.f, 5.f, 2.f}, 3.f, 4.f);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.nodeCount(), 3);
    REQUIRE_EQ(network.edgeCount(), 2);
    const auto continuation = std::find_if(network.edges().begin(), network.edges().end(),
                                           [&](const auto& edge) { return edge.id != road.value(); });
    REQUIRE(continuation != network.edges().end());
    const auto continuationId = continuation->id;
    const auto insertedNodeId = continuation->from;
    auto inserted = network.nodeResult(insertedNodeId);
    REQUIRE(inserted.ok());
    CHECK(std::fabs(inserted.value().x - 3.f) < 1e-5f);
    CHECK(std::fabs(inserted.value().y - 4.f) < 1e-5f);
    CHECK(std::fabs(inserted.value().z) < 1e-5f);

    DomainOperation undo = operation.value();
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto keep = network.addNode(0.f, 0.f, 0.f);
    auto removed = network.addNode(0.2f, 0.f, 0.f);
    auto north = network.addNode(0.2f, 0.f, 10.f);
    auto east = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(keep.ok());
    REQUIRE(removed.ok());
    REQUIRE(north.ok());
    REQUIRE(east.ok());
    auto incoming = network.addEdge(north.value(), removed.value(), {{}, {}}, 1, 0);
    auto outgoing = network.addEdge(removed.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    const RoadLaneConnection link{incoming.value(), 0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(link).ok());

    RoadNetworkEditTarget target("road:merge", network);
    auto operation = target.makeMergeNodes(keep.value(), removed.value(), 0.5f);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 3);
    CHECK_EQ(network.edgeResult(incoming.value()).value().to, keep.value());
    CHECK_EQ(network.edgeResult(outgoing.value()).value().from, keep.value());
    CHECK_EQ(network.laneLinkCount(), 1);
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto keep = network.addNode(0.f, 0.f, 0.f);
    auto removed = network.addNode(0.2f, 0.f, 0.f);
    auto west = network.addNode(-10.f, 0.f, 0.f);
    auto north = network.addNode(0.2f, 0.f, -10.f);
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
    auto operation = target.makeMergeNodesAndConnect(keep.value(), removed.value(), 0.5f);
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
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto west = network.addNode(-10.f, 0.f, 0.f);
    auto from = network.addNode(0.f, 0.f, 0.f);
    auto to = network.addNode(10.f, 0.f, 0.f);
    auto east = network.addNode(20.f, 0.f, 0.f);
    auto north = network.addNode(0.f, 0.f, -10.f);
    REQUIRE(west.ok());
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    REQUIRE(east.ok());
    REQUIRE(north.ok());
    auto incoming = network.addEdge(west.value(), from.value(), {{}, {}}, 1, 0);
    auto unrelatedOutgoing = network.addEdge(from.value(), north.value(), {{}, {}}, 1, 0);
    auto outgoing = network.addEdge(to.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(unrelatedOutgoing.ok());
    REQUIRE(outgoing.ok());
    REQUIRE_EQ(network.laneLinkCount(), 0);

    RoadNetworkEditTarget target("road:add-connect", network);
    auto operation = target.makeAddEdgeAndConnect(from.value(), to.value(), {{}, {}}, 1, 0);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.edgeCount(), 4);
    const auto newEdgeId = network.edges().back().id;
    REQUIRE_EQ(network.laneLinkCount(), 2);
    CHECK(std::all_of(network.laneLinks().begin(), network.laneLinks().end(), [&](const auto& link) {
        return link.inEdge == newEdgeId || link.outEdge == newEdgeId;
    }));
    CHECK(std::none_of(network.laneLinks().begin(), network.laneLinks().end(), [&](const auto& link) {
        return link.inEdge == incoming.value() && link.outEdge == unrelatedOutgoing.value();
    }));
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto west = network.addNode(-12.f, 0.f, 0.f);
    auto east = network.addNode(12.f, 0.f, 0.f);
    auto exit = network.addNode(12.f, 0.f, -10.f);
    auto source = network.addNode(0.f, 0.f, 10.f);
    REQUIRE(west.ok());
    REQUIRE(east.ok());
    REQUIRE(exit.ok());
    REQUIRE(source.ok());
    auto mainRoad = network.addEdge(west.value(), east.value(), {{-12.f, 0.f, 0.f}, {12.f, 0.f, 0.f}}, 1, 0);
    auto exitRoad = network.addEdge(east.value(), exit.value(), {{}, {}}, 1, 0);
    REQUIRE(mainRoad.ok());
    REQUIRE(exitRoad.ok());
    const RoadLaneConnection endpointLink{mainRoad.value(), 0, exitRoad.value(), 0, RoadLaneDirection::Forward,
                                          RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(endpointLink).ok());

    RoadNetworkEditTarget target("road:connect-position", network);
    const auto originalRevision = network.revision();
    auto invalid = target.makeConnectNodeAtPosition(source.value(), mainRoad.value(), {0.f, 8.f, 0.f}, 1.f);
    CHECK(!invalid.ok());
    CHECK_EQ(network.revision(), originalRevision);

    auto operation = target.makeConnectNodeAtPosition(source.value(), mainRoad.value(), {2.f, 1.f, 0.f}, 2.f,
                                                      3.5f, 1, 1);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 5);
    CHECK_EQ(network.edgeCount(), 4);
    CHECK_EQ(network.laneLinkCount(), 4);
    REQUIRE(network.validate().ok());
    const auto branch = std::find_if(network.edges().begin(), network.edges().end(), [&](const auto& edge) {
        return edge.from == source.value();
    });
    REQUIRE(branch != network.edges().end());
    const auto branchId = branch->id;
    const auto insertedNodeId = branch->to;
    CHECK_EQ(branch->lanesForward, 1);
    CHECK_EQ(branch->lanesBackward, 1);

    DomainOperation undo = operation.value();
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto curveStart = network.addNode(-10.f, 0.f, -10.f);
    auto curveEnd = network.addNode(10.f, 0.f, -10.f);
    auto source = network.addNode(0.f, 0.f, 20.f);
    REQUIRE(curveStart.ok());
    REQUIRE(curveEnd.ok());
    REQUIRE(source.ok());
    auto curve = network.addEdge(curveStart.value(), curveEnd.value(),
                                 {{-10.f, 0.f, -10.f}, {-3.f, 0.f, 10.f},
                                  {3.f, 0.f, 10.f}, {10.f, 0.f, -10.f}}, 1, 0);
    REQUIRE(curve.ok());

    RoadNetworkEditTarget target("road:curve-branch", network);
    auto operation = target.makeConnectNodeAtPosition(source.value(), curve.value(), {0.f, 0.f, 12.5f}, 0.2f,
                                                       2.f, 1, 0);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 4);
    CHECK_EQ(network.edgeCount(), 3);
    const auto junction = std::find_if(network.nodes().begin(), network.nodes().end(), [](const auto& node) {
        return std::fabs(node.x) < 1e-3f && node.z > 12.f;
    });
    REQUIRE(junction != network.nodes().end());
    const auto junctionId = junction->id;
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
    auto west = network.addNode(-10.f, 0.f, 0.f);
    auto east = network.addNode(10.f, 0.f, 0.f);
    auto north = network.addNode(0.f, 0.2f, -10.f);
    auto south = network.addNode(0.f, 0.2f, 10.f);
    REQUIRE(west.ok());
    REQUIRE(east.ok());
    REQUIRE(north.ok());
    REQUIRE(south.ok());
    auto horizontal = network.addEdge(west.value(), east.value(), {{}, {}}, 1, 0);
    auto vertical = network.addEdge(north.value(), south.value(), {{}, {}}, 1, 0);
    REQUIRE(horizontal.ok());
    REQUIRE(vertical.ok());

    RoadNetworkEditTarget target("road:intersection", network);
    const auto originalRevision = network.revision();
    auto tooFar = target.makeConnectEdgeIntersection(horizontal.value(), vertical.value(), 0.1f, 3.f);
    CHECK(!tooFar.ok());
    CHECK_EQ(network.revision(), originalRevision);

    RoadNetwork ambiguous;
    auto zigStart = ambiguous.addNode(-10.f, 0.f, -5.f);
    auto zigEnd = ambiguous.addNode(10.f, 0.f, 5.f);
    auto lineStart = ambiguous.addNode(0.f, 0.f, -10.f);
    auto lineEnd = ambiguous.addNode(0.f, 0.f, 10.f);
    REQUIRE(zigStart.ok());
    REQUIRE(zigEnd.ok());
    REQUIRE(lineStart.ok());
    REQUIRE(lineEnd.ok());
    auto zig = ambiguous.addEdge(zigStart.value(), zigEnd.value(),
                                 {{-10.f, 0.f, -5.f}, {10.f, 0.f, -5.f}, {-10.f, 0.f, 5.f}, {10.f, 0.f, 5.f}},
                                 1, 0);
    auto line = ambiguous.addEdge(lineStart.value(), lineEnd.value(), {{}, {}}, 1, 0);
    REQUIRE(zig.ok());
    REQUIRE(line.ok());
    RoadNetworkEditTarget ambiguousTarget("road:ambiguous-intersection", ambiguous);
    const auto ambiguousRevision = ambiguous.revision();
    CHECK(!ambiguousTarget.makeConnectEdgeIntersection(zig.value(), line.value(), 0.1f, 3.f).ok());
    CHECK_EQ(ambiguous.revision(), ambiguousRevision);

    RoadCommandRegistry registry;
    REQUIRE(registerRoadNetworkEditingCommands(registry).ok());
    const auto planner = registry.planners.find("road.edge.connect-intersection.v1");
    REQUIRE(planner != registry.planners.end());
    CommandRequest request;
    request.id = eve::editing::CommandId("road.edge.connect-intersection.v1");
    request.payload = EditorValue::Object{{"firstEdgeId", std::int64_t{horizontal.value()}},
                                          {"secondEdgeId", std::int64_t{vertical.value()}},
                                          {"maximumHeightDelta", 0.25},
                                          {"junctionRadius", 3.0}};
    auto plan = planner->second(target, request);
    REQUIRE(plan.ok());
    REQUIRE_EQ(plan.value().operations.size(), std::size_t{1});
    DomainOperation operation = plan.value().operations.front();
    REQUIRE(target.applyDomainOperation(operation).ok());
    CHECK_EQ(network.nodeCount(), 5);
    CHECK_EQ(network.edgeCount(), 4);
    CHECK_EQ(network.laneLinkCount(), 4);
    REQUIRE(network.validate().ok());
    const auto junction = std::find_if(network.nodes().begin(), network.nodes().end(), [](const auto& node) {
        return std::fabs(node.x) < 1e-5f && std::fabs(node.z) < 1e-5f;
    });
    REQUIRE(junction != network.nodes().end());
    const auto junctionId = junction->id;
    CHECK(std::fabs(junction->y - 0.1f) < 1e-5f);

    DomainOperation undo = operation;
    undo.type = operation.inverseType;
    undo.payload = operation.inverse;
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
    auto curveStart = network.addNode(-10.f, 0.f, -10.f);
    auto curveEnd = network.addNode(10.f, 0.f, -10.f);
    auto lineStart = network.addNode(0.f, 0.f, 11.f);
    auto lineEnd = network.addNode(0.f, 0.f, 14.f);
    REQUIRE(curveStart.ok());
    REQUIRE(curveEnd.ok());
    REQUIRE(lineStart.ok());
    REQUIRE(lineEnd.ok());
    auto curve = network.addEdge(curveStart.value(), curveEnd.value(),
                                 {{-10.f, 0.f, -10.f}, {-3.f, 0.f, 10.f},
                                  {3.f, 0.f, 10.f}, {10.f, 0.f, -10.f}}, 1, 0);
    auto line = network.addEdge(lineStart.value(), lineEnd.value(), {{}, {}}, 1, 0);
    REQUIRE(curve.ok());
    REQUIRE(line.ok());

    RoadNetworkEditTarget target("road:curved-intersection", network);
    auto operation = target.makeConnectEdgeIntersection(curve.value(), line.value(), 0.1f, 2.f);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(network.nodeCount(), 5);
    CHECK_EQ(network.edgeCount(), 4);
    CHECK_EQ(network.laneLinkCount(), 4);
    const auto junction = std::find_if(network.nodes().begin(), network.nodes().end(), [](const auto& node) {
        return std::fabs(node.x) < 1e-3f && node.z > 10.f && node.z < 14.f;
    });
    REQUIRE(junction != network.nodes().end());
    const auto junctionId = junction->id;
    REQUIRE(network.validate().ok());

    DomainOperation undo = operation.value();
    undo.type = operation.value().inverseType;
    undo.payload = operation.value().inverse;
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
