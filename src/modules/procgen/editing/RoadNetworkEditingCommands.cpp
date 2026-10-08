#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editing/RoadNetworkEditTargetInternal.inc"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::procgen_editing {
using namespace detail;

editing::Result<void> registerRoadNetworkEditingCommands(editing::IEditingCommandRegistry& registry) {
    auto add = [&registry](const char* id, const char* name, editing::EditingCommandPlanner planner) {
        editing::EditingCommandDescriptor descriptor;
        descriptor.id                = editing::CommandId(id);
        descriptor.ownerModule       = "procgen_editing.road";
        descriptor.displayName       = name;
        descriptor.category          = "Road";
        descriptor.automationAllowed = true;
        return registry.registerPlannedCommand(std::move(descriptor), std::move(planner));
    };
    auto plan = [](EditorResult<editing::DomainOperation> operation) -> editing::Result<editing::CommandPlan> {
        if (!operation.ok()) return editing::Result<editing::CommandPlan>::failure(operation.status());
        editing::CommandPlan result;
        result.operations.push_back(std::move(operation).takeValue());
        return editing::applied(std::move(result));
    };
    auto target = [](editing::IEditableTarget& value) { return dynamic_cast<RoadNetworkEditTarget*>(&value); };

    auto registered = add("road.node.add.v1", "Add road node", [target, plan](editing::IEditableTarget& value,
                                                                                const editing::CommandRequest& request) {
        auto* road = target(value);
        float x = 0.f, y = 0.f, z = 0.f, radius = 6.f;
        if (!road || !readNumber(request.payload, "x", x) || !readNumber(request.payload, "y", y) ||
            !readNumber(request.payload, "z", z) || (field(request.payload, "junctionRadius") &&
                                                       !readNumber(request.payload, "junctionRadius", radius)))
            return commandError<editing::CommandPlan>("editor.road.command.node-add",
                                                       "Road node add requires finite x, y and z");
        return plan(road->makeAddNode(x, y, z, radius));
    });
    if (!registered.ok()) return registered;

    registered = add("road.node.move.v1", "Move road node", [target, plan](editing::IEditableTarget& value,
                                                                              const editing::CommandRequest& request) {
        auto* road = target(value);
        std::uint32_t id = 0;
        float x = 0.f, y = 0.f, z = 0.f;
        if (!road || !readId(request.payload, "id", id) || !readNumber(request.payload, "x", x) ||
            !readNumber(request.payload, "y", y) || !readNumber(request.payload, "z", z))
            return commandError<editing::CommandPlan>("editor.road.command.node-move",
                                                       "Road node move requires id and finite x, y and z");
        return plan(road->makeMoveNode(id, x, y, z));
    });
    if (!registered.ok()) return registered;

    registered = add("road.node.radius.set.v1", "Set road junction radius",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t id = 0;
                         float radius = 0.f;
                         if (!road || !readId(request.payload, "id", id) ||
                             !readNumber(request.payload, "junctionRadius", radius))
                             return commandError<editing::CommandPlan>("editor.road.command.node-radius",
                                                                        "Road node radius requires id and radius");
                         return plan(road->makeSetNodeJunctionRadius(id, radius));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.node.junction-control.set.v1", "Set road junction control",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t id = 0;
                         int control = 0;
                         if (!road || !readId(request.payload, "id", id) ||
                             !readSmallInt(request.payload, "junctionControl", 0, 3, control))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.node-control",
                                 "Road junction control requires id and junctionControl in [0,3]");
                         return plan(road->makeSetNodeJunctionControl(
                             id, static_cast<procgen::road::RoadJunctionControl>(control)));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.node.merge.v1", "Merge nearby road nodes",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t keepNodeId = 0, removeNodeId = 0;
                         float maxDistance = 1.f;
                         if (!road || !readId(request.payload, "keepNodeId", keepNodeId) ||
                             !readId(request.payload, "removeNodeId", removeNodeId) ||
                             (field(request.payload, "maxDistance") &&
                              !readNumber(request.payload, "maxDistance", maxDistance)))
                             return commandError<editing::CommandPlan>("editor.road.command.node-merge",
                                                                        "Road node merge requires two node ids");
                         return plan(road->makeMergeNodes(keepNodeId, removeNodeId, maxDistance));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.node.merge-and-connect.v1", "Merge road nodes and connect turns",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t keepNodeId = 0, removeNodeId = 0;
                         float maxDistance = 1.f;
                         if (!road || !readId(request.payload, "keepNodeId", keepNodeId) ||
                             !readId(request.payload, "removeNodeId", removeNodeId) ||
                             (field(request.payload, "maxDistance") &&
                              !readNumber(request.payload, "maxDistance", maxDistance)))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.node-merge-connect",
                                 "Road node merge and connect requires two node ids");
                         return plan(road->makeMergeNodesAndConnect(keepNodeId, removeNodeId, maxDistance));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.node.remove.v1", "Remove road node", [target, plan](editing::IEditableTarget& value,
                                                                                  const editing::CommandRequest& request) {
        auto* road = target(value);
        std::uint32_t id = 0;
        if (!road || !readId(request.payload, "id", id))
            return commandError<editing::CommandPlan>("editor.road.command.node-remove",
                                                       "Road node remove requires id");
        return plan(road->makeRemoveNode(id));
    });
    if (!registered.ok()) return registered;

    registered = add("road.node.connect-all-turns.v1", "Connect all road turns",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t id = 0;
                         if (!road || !readId(request.payload, "id", id))
                             return commandError<editing::CommandPlan>("editor.road.command.connect-turns",
                                                                        "Connect all turns requires a node id");
                         return plan(road->makeConnectAllTurns(id));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.add.v1", "Add road edge", [target, plan](editing::IEditableTarget& value,
                                                                            const editing::CommandRequest& request) {
        auto* road = target(value);
        std::uint32_t from = 0, to = 0, ignored = 0;
        int forward = 2, backward = 0;
        std::vector<procgen::road::RoadControlPoint> points;
        EditorValue::Object pointPayload{{"id", std::int64_t{1}}};
        if (const auto* encoded = field(request.payload, "points")) pointPayload["points"] = *encoded;
        if (!road || !readId(request.payload, "from", from) || !readId(request.payload, "to", to) ||
            !decodeEdge(EditorValue(std::move(pointPayload)), ignored, points) ||
            (field(request.payload, "lanesForward") &&
             !readSmallInt(request.payload, "lanesForward", 0, 8, forward)) ||
            (field(request.payload, "lanesBackward") &&
             !readSmallInt(request.payload, "lanesBackward", 0, 8, backward)))
            return commandError<editing::CommandPlan>("editor.road.command.edge-add",
                                                       "Road edge add requires endpoints and at least two points");
        return plan(road->makeAddEdge(from, to, std::move(points), forward, backward));
    });
    if (!registered.ok()) return registered;

    registered = add("road.edge.add-and-connect.v1", "Add road edge and connect endpoints",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t from = 0, to = 0, ignored = 0;
                         int forward = 2, backward = 0;
                         std::vector<procgen::road::RoadControlPoint> points;
                         EditorValue::Object pointPayload{{"id", std::int64_t{1}}};
                         if (const auto* encoded = field(request.payload, "points")) pointPayload["points"] = *encoded;
                         if (!road || !readId(request.payload, "from", from) || !readId(request.payload, "to", to) ||
                             !decodeEdge(EditorValue(std::move(pointPayload)), ignored, points) ||
                             (field(request.payload, "lanesForward") &&
                              !readSmallInt(request.payload, "lanesForward", 0, 8, forward)) ||
                             (field(request.payload, "lanesBackward") &&
                              !readSmallInt(request.payload, "lanesBackward", 0, 8, backward)))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-add-connect",
                                 "Road edge add and connect requires endpoints and at least two points");
                         procgen::road::RoadStyle style;
                         if (const auto* encodedStyle = field(request.payload, "style")) {
                             const auto* styleObject = encodedStyle->getIf<EditorValue::Object>();
                             if (!styleObject)
                                 return commandError<editing::CommandPlan>(
                                     "editor.road.command.edge-add-connect-style", "Road edge style is invalid");
                             EditorValue::Object stylePayload = *styleObject;
                             stylePayload["id"] = std::int64_t{1};
                             if (!decodeStyle(EditorValue(std::move(stylePayload)), ignored, style))
                                 return commandError<editing::CommandPlan>(
                                     "editor.road.command.edge-add-connect-style", "Road edge style is invalid");
                         }
                         return plan(road->makeAddEdgeAndConnect(from, to, std::move(points), forward, backward,
                                                                style));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.split.v1", "Split road edge",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t id = 0;
                         int index = 0;
                         float radius = 6.f;
                         if (!road || !readId(request.payload, "id", id) ||
                             !readSmallInt(request.payload, "controlPointIndex", 1, 4095, index) ||
                             (field(request.payload, "junctionRadius") &&
                              !readNumber(request.payload, "junctionRadius", radius)))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-split",
                                                                        "Road split requires edge id and interior control-point index");
                         return plan(road->makeSplitEdge(id, static_cast<std::size_t>(index), radius));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.split-at-position.v1", "Split road edge at world position",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t id = 0;
                         procgen::road::RoadControlPoint position;
                         float maxDistance = 2.f, radius = 6.f;
                         if (!road || !readId(request.payload, "id", id) ||
                             !readNumber(request.payload, "x", position.x) ||
                             !readNumber(request.payload, "y", position.y) ||
                             !readNumber(request.payload, "z", position.z) ||
                             (field(request.payload, "maxDistance") &&
                              !readNumber(request.payload, "maxDistance", maxDistance)) ||
                             (field(request.payload, "junctionRadius") &&
                              !readNumber(request.payload, "junctionRadius", radius)))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-split-position",
                                 "Road position split requires edge id and a finite world position");
                         return plan(road->makeSplitEdgeAtPosition(id, position, maxDistance, radius));
    });
    if (!registered.ok()) return registered;

    registered = add("road.node.connect-at-position.v1", "Connect road node at world position",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t sourceNodeId = 0, edgeId = 0;
                         procgen::road::RoadControlPoint position;
                         float maxDistance = 2.f, radius = 6.f;
                         int forward = 1, backward = 0;
                         if (!road || !readId(request.payload, "sourceNodeId", sourceNodeId) ||
                             !readId(request.payload, "edgeId", edgeId) ||
                             !readNumber(request.payload, "x", position.x) ||
                             !readNumber(request.payload, "y", position.y) ||
                             !readNumber(request.payload, "z", position.z) ||
                             (field(request.payload, "maxDistance") &&
                              !readNumber(request.payload, "maxDistance", maxDistance)) ||
                             (field(request.payload, "junctionRadius") &&
                              !readNumber(request.payload, "junctionRadius", radius)) ||
                             (field(request.payload, "lanesForward") &&
                              !readSmallInt(request.payload, "lanesForward", 0, 8, forward)) ||
                             (field(request.payload, "lanesBackward") &&
                              !readSmallInt(request.payload, "lanesBackward", 0, 8, backward)))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.node-connect-position",
                                 "Road branch connection requires source node, edge and world position");
                         return plan(road->makeConnectNodeAtPosition(sourceNodeId, edgeId, position, maxDistance,
                                                                    radius, forward, backward));
    });
    if (!registered.ok()) return registered;

    registered = add("road.edge.connect-intersection.v1", "Connect crossing road edges",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t firstEdgeId = 0, secondEdgeId = 0;
                         float maximumHeightDelta = 0.5f, radius = 6.f;
                         if (!road || !readId(request.payload, "firstEdgeId", firstEdgeId) ||
                             !readId(request.payload, "secondEdgeId", secondEdgeId) ||
                             (field(request.payload, "maximumHeightDelta") &&
                              !readNumber(request.payload, "maximumHeightDelta", maximumHeightDelta)) ||
                             (field(request.payload, "junctionRadius") &&
                              !readNumber(request.payload, "junctionRadius", radius)))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-intersection",
                                 "Road intersection requires two edge ids and finite tolerances");
                         return plan(road->makeConnectEdgeIntersection(firstEdgeId, secondEdgeId,
                                                                       maximumHeightDelta, radius));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.lanes.set.v1", "Set road edge lanes",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t id = 0;
                         int forward = 0, backward = 0;
                         if (!road || !readId(request.payload, "id", id) ||
                             !readSmallInt(request.payload, "lanesForward", 0, 8, forward) ||
                             !readSmallInt(request.payload, "lanesBackward", 0, 8, backward))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-lanes",
                                                                        "Road lane edit requires id and lane counts");
                         return plan(road->makeSetEdgeLaneCounts(id, forward, backward));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.style.set.v1", "Set road edge style",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t edgeId = 0;
                         procgen::road::RoadStyle style;
                         if (!road || !decodeStyle(request.payload, edgeId, style))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-style",
                                                                        "Road edge style requires a complete style payload");
                         return plan(road->makeSetEdgeStyle(edgeId, style));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.control-points.set.v1", "Set road edge control points",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t edgeId = 0;
                         std::vector<procgen::road::RoadControlPoint> points;
                         if (!road || !decodeEdge(request.payload, edgeId, points))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-control-points",
                                                                        "Road edge control points require id and at least two finite points");
                         return plan(road->makeSetEdgeControlPoints(edgeId, std::move(points)));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.reverse.v1", "Reverse road edge",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t edgeId = 0;
                         if (!road || !readId(request.payload, "id", edgeId))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-reverse",
                                                                        "Road edge reverse requires an id");
                         return plan(road->makeReverseEdge(edgeId));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.reconnect-endpoint.v1", "Reconnect road edge endpoint",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t edgeId = 0, nodeId = 0;
                         const auto* endpointValue = field(request.payload, "fromEndpoint");
                         const auto* fromEndpoint = endpointValue ? endpointValue->getIf<bool>() : nullptr;
                         if (!road || !fromEndpoint || !readId(request.payload, "id", edgeId) ||
                             !readId(request.payload, "nodeId", nodeId))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-reconnect",
                                 "Road edge reconnect requires id, fromEndpoint, and nodeId");
                         return plan(road->makeReconnectEdgeEndpoint(edgeId, *fromEndpoint, nodeId));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.reconnect-endpoint-and-connect.v1", "Reconnect and connect road edge endpoint",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t edgeId = 0, nodeId = 0;
                         const auto* endpointValue = field(request.payload, "fromEndpoint");
                         const auto* fromEndpoint = endpointValue ? endpointValue->getIf<bool>() : nullptr;
                         if (!road || !fromEndpoint || !readId(request.payload, "id", edgeId) ||
                             !readId(request.payload, "nodeId", nodeId))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-reconnect-connect",
                                 "Road edge reconnect-and-connect requires id, fromEndpoint, and nodeId");
                         return plan(road->makeReconnectEdgeEndpointAndConnect(edgeId, *fromEndpoint, nodeId));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.snap-endpoint.v1", "Snap road edge endpoint to nearest node",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t edgeId = 0;
                         float maxDistance = 0.f;
                         bool connectTurns = true;
                         const auto* endpointValue = field(request.payload, "fromEndpoint");
                         const auto* fromEndpoint = endpointValue ? endpointValue->getIf<bool>() : nullptr;
                         const auto* connectValue = field(request.payload, "connectTurns");
                         const auto* connect = connectValue ? connectValue->getIf<bool>() : nullptr;
                         if (!road || !fromEndpoint || (connectValue && !connect) ||
                             !readId(request.payload, "id", edgeId) ||
                             !readNumber(request.payload, "maxDistance", maxDistance))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-snap",
                                 "Road edge snap requires id, fromEndpoint, and a finite maxDistance");
                         if (connect) connectTurns = *connect;
                         return plan(road->makeSnapEdgeEndpoint(edgeId, *fromEndpoint, maxDistance, connectTurns));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.detach-endpoint.v1", "Detach road edge endpoint",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto* road = target(value);
                         std::uint32_t edgeId = 0;
                         const auto* endpointValue = field(request.payload, "fromEndpoint");
                         const auto* fromEndpoint = endpointValue ? endpointValue->getIf<bool>() : nullptr;
                         if (!road || !fromEndpoint || !readId(request.payload, "id", edgeId))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-detach",
                                 "Road edge detach requires id and fromEndpoint");
                         return plan(road->makeDetachEdgeEndpoint(edgeId, *fromEndpoint));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.remove.v1", "Remove road edge", [target, plan](editing::IEditableTarget& value,
                                                                                  const editing::CommandRequest& request) {
        auto* road = target(value);
        std::uint32_t id = 0;
        if (!road || !readId(request.payload, "id", id))
            return commandError<editing::CommandPlan>("editor.road.command.edge-remove",
                                                       "Road edge remove requires id");
        return plan(road->makeRemoveEdge(id));
    });
    if (!registered.ok()) return registered;

    auto laneLinkCommand = [target, plan](bool addLink, editing::IEditableTarget& value,
                                          const editing::CommandRequest& request) {
        auto* road = target(value);
        procgen::road::RoadLaneConnection link;
        if (!road || !decodeLink(request.payload, link))
            return commandError<editing::CommandPlan>("editor.road.command.lane-link",
                                                       "Road lane-link command requires a complete directed link");
        return plan(addLink ? road->makeAddLaneLink(link) : road->makeRemoveLaneLink(link));
    };
    registered = add("road.lane-link.add.v1", "Add road lane link",
                     [laneLinkCommand](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         return laneLinkCommand(true, value, request);
                     });
    if (!registered.ok()) return registered;
    registered = add("road.lane-link.remove.v1", "Remove road lane link",
                     [laneLinkCommand](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         return laneLinkCommand(false, value, request);
                     });
    if (!registered.ok()) return registered;
    auto laneLinkBlockCommand = [target, plan](bool block, editing::IEditableTarget& value,
                                               const editing::CommandRequest& request) {
        auto* road = target(value);
        procgen::road::RoadLaneConnection link;
        if (!road || !decodeLink(request.payload, link))
            return commandError<editing::CommandPlan>("editor.road.command.lane-link-block",
                                                       "Road lane-link block command requires a complete directed link");
        return plan(block ? road->makeBlockLaneLink(link) : road->makeUnblockLaneLink(link));
    };
    registered = add("road.lane-link.block.v1", "Block road lane link",
                     [laneLinkBlockCommand](editing::IEditableTarget& value,
                                            const editing::CommandRequest& request) {
                         return laneLinkBlockCommand(true, value, request);
                     });
    if (!registered.ok()) return registered;
    return add("road.lane-link.unblock.v1", "Unblock road lane link",
               [laneLinkBlockCommand](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                   return laneLinkBlockCommand(false, value, request);
               });
}

}  // namespace eve::procgen_editing
