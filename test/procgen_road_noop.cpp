#include <unordered_map>
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::editing;
using namespace eve::procgen::road;
using namespace eve::procgen_editing;
namespace {
class Registry final : public IEditingCommandRegistry {
public:
    std::unordered_map<std::string, EditingCommandPlanner> planners;
    eve::editing::Result<void>                             registerPlannedCommand(EditingCommandDescriptor descriptor,
                                                                                  EditingCommandPlanner    planner) override {
        planners.emplace(descriptor.id.value(), std::move(planner));
        return eve::editing::Result<void>::success();
    }
    eve::editing::Result<std::size_t> unregisterOwner(const std::string&) override {
        const auto count = planners.size();
        planners.clear();
        return eve::editing::Result<std::size_t>::success(count);
    }
};
}  // namespace
TEST_CASE("procgen.road.editingNoOp.buildersReturnSuccessAndPreserveAuthor") {
    auto made = RoadNetwork::makeStraight(20, 1);
    REQUIRE(made.ok());
    auto                  network = std::move(made).takeValue();
    RoadNetworkEditTarget target("noop", network);
    const auto            edge   = network.edges()[0];
    auto                  before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    const auto revision  = network.revision();
    auto       reconnect = target.makeReconnectEdgeEndpoint(edge.id, true, edge.from);
    REQUIRE(reconnect.ok());
    REQUIRE(reconnect.code() == EditorStatus::NoOp);
    REQUIRE(reconnect.value().hasInverse);
    REQUIRE(!reconnect.value().type.empty());
    auto turns = target.makeConnectAllTurns(edge.from);
    REQUIRE(turns.ok());
    REQUIRE(turns.code() == EditorStatus::NoOp);
    REQUIRE(turns.value().hasInverse);
    REQUIRE_EQ(network.revision(), revision);
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    target.clearDirtyRegion();
    auto appliedReconnect = target.applyDomainOperation(reconnect.value());
    REQUIRE(appliedReconnect.ok());
    REQUIRE(appliedReconnect.code() == EditorStatus::NoOp);
    REQUIRE(target.dirtyRegion().empty());
    auto appliedTurns = target.applyDomainOperation(turns.value());
    REQUIRE(appliedTurns.ok());
    REQUIRE(appliedTurns.code() == EditorStatus::NoOp);
    REQUIRE(target.dirtyRegion().empty());
    REQUIRE_EQ(network.revision(), revision);
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    auto missing = target.makeReconnectEdgeEndpoint(99999, true, edge.from);
    REQUIRE(!missing.ok());
    REQUIRE(missing.code() == EditorStatus::NotFound);
}
TEST_CASE("procgen.road.editingNoOp.sameEndpointStillPlansMissingTurnsAndUndo") {
    RoadNetwork network;
    auto        a = network.addNode(-10, 0, 0), hub = network.addNode(0, 0, 0), b = network.addNode(10, 0, 0);
    REQUIRE(a.ok());
    REQUIRE(hub.ok());
    REQUIRE(b.ok());
    auto first  = network.addEdge(a.value(), hub.value(), {{-10, 0, 0}, {0, 0, 0}}, 1, 1);
    auto second = network.addEdge(hub.value(), b.value(), {{0, 0, 0}, {10, 0, 0}}, 1, 1);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    auto c = network.addNode(0, 0, 10);
    REQUIRE(c.ok());
    auto third = network.addEdge(hub.value(), c.value(), {{0, 0, 0}, {0, 0, 10}}, 1, 1);
    REQUIRE(third.ok());
    REQUIRE(
        network
            .addLaneLink({second.value(), 0, third.value(), 0, RoadLaneDirection::Backward, RoadLaneDirection::Forward})
            .ok());
    REQUIRE(network
                .blockLaneLink(
                    {third.value(), 0, second.value(), 0, RoadLaneDirection::Backward, RoadLaneDirection::Forward})
                .ok());
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    RoadNetworkEditTarget target("noop:combined", network);
    const auto            revision  = network.revision();
    auto                  operation = target.makeReconnectEdgeEndpointAndConnect(first.value(), false, hub.value());
    REQUIRE(operation.ok());
    REQUIRE(operation.code() == EditorStatus::Applied);
    REQUIRE_EQ(network.revision(), revision);
    REQUIRE_EQ(network.laneLinkCount(), 1);
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.laneLinkCount(), 5);
    DomainOperation inverse = operation.value();
    inverse.type            = operation.value().inverseType;
    inverse.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(inverse).ok());
    REQUIRE_EQ(network.laneLinkCount(), 1);
    auto restored = captureRoadNetwork(network);
    REQUIRE(restored.ok());
    REQUIRE(restored.value() == before.value());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.laneLinkCount(), 5);
    const auto stable   = network.revision();
    auto       repeated = target.makeReconnectEdgeEndpointAndConnect(first.value(), false, hub.value());
    REQUIRE(repeated.ok());
    REQUIRE(repeated.code() == EditorStatus::NoOp);
    REQUIRE_EQ(network.revision(), stable);
}
TEST_CASE("procgen.road.editingNoOp.commandAdaptersOmitNoChangeOperations") {
    auto made = RoadNetwork::makeStraight(20, 1);
    REQUIRE(made.ok());
    auto                  network = std::move(made).takeValue();
    RoadNetworkEditTarget target("noop:commands", network);
    Registry              registry;
    REQUIRE(registerRoadNetworkEditingCommands(registry).ok());
    const auto     edge     = network.edges()[0];
    const auto     revision = network.revision();
    CommandRequest reconnect;
    reconnect.id = eve::editing::CommandId("road.edge.reconnect-endpoint.v1");
    reconnect.payload =
        EditorValue::Object{{"id", std::int64_t{edge.id}}, {"fromEndpoint", true}, {"nodeId", std::int64_t{edge.from}}};
    auto planned = registry.planners.at(reconnect.id.value())(target, reconnect);
    REQUIRE(planned.ok());
    REQUIRE(planned.code() == EditorStatus::NoOp);
    REQUIRE(planned.value().operations.empty());
    CommandRequest turns;
    turns.id      = eve::editing::CommandId("road.node.connect-all-turns.v1");
    turns.payload = EditorValue::Object{{"id", std::int64_t{edge.from}}};
    REQUIRE(registry.planners.contains(turns.id.value()));
    auto turnPlan = registry.planners.at(turns.id.value())(target, turns);
    REQUIRE(turnPlan.ok());
    REQUIRE(turnPlan.code() == EditorStatus::NoOp);
    REQUIRE(turnPlan.value().operations.empty());
    REQUIRE_EQ(network.revision(), revision);
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
    REQUIRE_EQ(network.edgeResult(road.value()).value().to, newHub.value());
    REQUIRE_EQ(network.laneLinkCount(), 2);
    for (const auto& link : network.laneLinks()) {
        const bool referencesMovedRoad = link.inEdge == road.value() || link.outEdge == road.value();
        REQUIRE(referencesMovedRoad);
    }
    REQUIRE_EQ(network.blockedLaneLinkCount(), 0);

    DomainOperation undo = operation.value();
    undo.type            = operation.value().inverseType;
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE_EQ(network.edgeResult(road.value()).value().to, oldHub.value());
    REQUIRE_EQ(network.laneLinkCount(), 1);
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(network.edgeResult(road.value()).value().to, newHub.value());
    REQUIRE_EQ(network.laneLinkCount(), 2);
    REQUIRE_EQ(static_cast<int>(target.makeReconnectEdgeEndpoint(road.value(), false, newHub.value()).code()),
               static_cast<int>(EditorStatus::NoOp));
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
    REQUIRE_EQ(network.laneLinkCount(), 2);
    REQUIRE(network.validate().ok());

    const auto afterConnectRevision = network.revision();
    REQUIRE(!target.applyDomainOperation(connect.value()).ok());
    REQUIRE_EQ(network.revision(), afterConnectRevision);
    REQUIRE_EQ(network.laneLinkCount(), 2);

    DomainOperation undo = connect.value();
    undo.type            = connect.value().inverseType;
    undo.payload         = connect.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE_EQ(network.laneLinkCount(), 0);
    REQUIRE(network.validate().ok());

    REQUIRE(target.applyDomainOperation(connect.value()).ok());
    REQUIRE_EQ(network.laneLinkCount(), 2);
    REQUIRE_EQ(static_cast<int>(target.makeConnectAllTurns(hub.value()).code()), static_cast<int>(EditorStatus::NoOp));
}
