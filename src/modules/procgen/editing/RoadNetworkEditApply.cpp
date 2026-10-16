#include <algorithm>
#include <cmath>
#include <utility>
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editing/RoadNetworkEditTargetInternal.inc"

// RoadNetworkEditApply.cpp
namespace eve::procgen_editing {
using namespace detail;

EditorResult<void> RoadNetworkEditTarget::applyDomainOperation(const editing::DomainOperation& operation) {
    if (operation.target != targetId())
        return reject<void>("editor.road.target", "Road operation targets another network");
    if (operation.type == kMoveNode) {
        std::uint32_t nodeId = 0;
        float         x = 0.f, y = 0.f, z = 0.f;
        if (!readId(operation.payload, "id", nodeId) || !readNumber(operation.payload, "x", x) ||
            !readNumber(operation.payload, "y", y) || !readNumber(operation.payload, "z", z))
            return reject<void>("editor.road.node-payload", "Road node payload is invalid");
        auto moved = network_->setNodePosition(nodeId, x, y, z);
        if (!moved.ok()) return roadFailure<void>(moved.status());
    } else if (operation.type == kSetNodeRadius) {
        std::uint32_t nodeId         = 0;
        float         junctionRadius = 0.f;
        if (!decodeNodeRadius(operation.payload, nodeId, junctionRadius))
            return reject<void>("editor.road.node-radius-payload", "Road node radius payload is invalid");
        auto changed = network_->setNodeJunctionRadius(nodeId, junctionRadius);
        if (!changed.ok()) return roadFailure<void>(changed.status());
    } else if (operation.type == kSetNodeControl) {
        std::uint32_t                      nodeId  = 0;
        procgen::road::RoadJunctionControl control = procgen::road::RoadJunctionControl::Uncontrolled;
        if (!decodeNodeControl(operation.payload, nodeId, control))
            return reject<void>("editor.road.node-control-payload", "Road node control payload is invalid");
        auto changed = network_->setNodeJunctionControl(nodeId, control);
        if (!changed.ok()) return roadFailure<void>(changed.status());
    } else if (operation.type == kMergeNodes || operation.type == kMergeNodesAndConnect) {
        std::uint32_t keepNodeId = 0, removeNodeId = 0;
        float         maxDistance = 0.f;
        if (!decodeMergeNodes(operation.payload, keepNodeId, removeNodeId, maxDistance))
            return reject<void>("editor.road.merge-payload", "Road node merge payload is invalid");
        auto candidate = *network_;
        auto merged    = candidate.mergeNodes(keepNodeId, removeNodeId, maxDistance);
        if (!merged.ok()) return roadFailure<void>(merged.status());
        if (operation.type == kMergeNodesAndConnect) {
            auto connected = candidate.connectAllTurns(keepNodeId);
            if (!connected.ok()) return roadFailure<void>(connected.status());
        }
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kUnmergeNodes) {
        procgen::road::RoadNode                        removedNode;
        std::vector<procgen::road::RoadEdge>           affectedEdges;
        std::vector<procgen::road::RoadLaneConnection> affectedLinks;
        std::vector<procgen::road::RoadLaneConnection> affectedBlockedLinks;
        if (!decodeUnmergeNodes(operation.payload, removedNode, affectedEdges, affectedLinks, affectedBlockedLinks))
            return reject<void>("editor.road.unmerge-payload", "Road node unmerge payload is invalid");
        auto candidate    = *network_;
        auto restoredNode = candidate.restoreNode(removedNode);
        if (!restoredNode.ok()) return roadFailure<void>(restoredNode.status());
        for (const auto& edge : affectedEdges) {
            auto removedEdge = candidate.removeEdge(edge.id);
            if (!removedEdge.ok()) return roadFailure<void>(removedEdge.status());
            auto restoredEdge = candidate.restoreEdge(edge);
            if (!restoredEdge.ok()) return roadFailure<void>(restoredEdge.status());
        }
        for (const auto& link : affectedLinks) {
            auto restoredLink = candidate.addLaneLink(link);
            if (!restoredLink.ok()) return roadFailure<void>(restoredLink.status());
        }
        for (const auto& link : affectedBlockedLinks) {
            auto blocked = candidate.blockLaneLink(link);
            if (!blocked.ok()) return roadFailure<void>(blocked.status());
        }
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kSetEdgePoints) {
        std::uint32_t                                edgeId = 0;
        std::vector<procgen::road::RoadControlPoint> points;
        if (!decodeEdge(operation.payload, edgeId, points))
            return reject<void>("editor.road.edge-payload", "Road edge payload is invalid");
        auto changed = network_->setEdgeControlPoints(edgeId, std::move(points));
        if (!changed.ok()) return roadFailure<void>(changed.status());
    } else if (operation.type == kSetEdgeStyle) {
        std::uint32_t            edgeId = 0;
        procgen::road::RoadStyle style;
        if (!decodeStyle(operation.payload, edgeId, style))
            return reject<void>("editor.road.style-payload", "Road style payload is invalid");
        auto changed = network_->setEdgeStyle(edgeId, style);
        if (!changed.ok()) return roadFailure<void>(changed.status());
    } else if (operation.type == kSetEdgeLanes) {
        std::uint32_t                                  edgeId       = 0;
        int                                            lanesForward = 0, lanesBackward = 0;
        std::vector<procgen::road::RoadLaneConnection> restoreLinks;
        std::vector<procgen::road::RoadLaneConnection> restoreBlockedLinks;
        if (!decodeLaneCounts(operation.payload, edgeId, lanesForward, lanesBackward, restoreLinks,
                              restoreBlockedLinks))
            return reject<void>("editor.road.lanes-payload", "Road lane-count payload is invalid");
        auto candidate = *network_;
        auto changed   = candidate.setEdgeLaneCounts(edgeId, lanesForward, lanesBackward);
        if (!changed.ok()) return roadFailure<void>(changed.status());
        for (const auto& link : restoreLinks) {
            auto restored = candidate.addLaneLink(link);
            if (!restored.ok()) return roadFailure<void>(restored.status());
        }
        for (const auto& link : restoreBlockedLinks) {
            auto blocked = candidate.blockLaneLink(link);
            if (!blocked.ok()) return roadFailure<void>(blocked.status());
        }
        *network_ = std::move(candidate);
    } else if (operation.type == kReverseEdge) {
        std::uint32_t edgeId = 0;
        if (!readId(operation.payload, "id", edgeId))
            return reject<void>("editor.road.reverse-payload", "Road edge reverse payload is invalid");
        auto reversed = network_->reverseEdge(edgeId);
        if (!reversed.ok()) return roadFailure<void>(reversed.status());
    } else if (operation.type == kReconnectEdgeEndpoint) {
        std::uint32_t                                  edgeId = 0, nodeId = 0;
        bool                                           fromEndpoint = false;
        std::vector<procgen::road::RoadLaneConnection> restoreLinks;
        std::vector<procgen::road::RoadLaneConnection> restoreBlockedLinks;
        if (!decodeReconnectEndpoint(operation.payload, edgeId, fromEndpoint, nodeId, restoreLinks,
                                     restoreBlockedLinks))
            return reject<void>("editor.road.reconnect-payload", "Road endpoint reconnect payload is invalid");
        auto candidate = *network_;
        auto changed   = candidate.reconnectEdgeEndpoint(edgeId, fromEndpoint, nodeId);
        if (!changed.ok()) return roadFailure<void>(changed.status());
        for (const auto& link : restoreLinks) {
            auto restored = candidate.addLaneLink(link);
            if (!restored.ok()) return roadFailure<void>(restored.status());
        }
        for (const auto& link : restoreBlockedLinks) {
            auto restored = candidate.blockLaneLink(link);
            if (!restored.ok()) return roadFailure<void>(restored.status());
        }
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kDetachEdgeEndpoint) {
        std::uint32_t edgeId = 0, nodeId = 0;
        bool          fromEndpoint = false;
        if (!decodeDetachEndpoint(operation.payload, edgeId, fromEndpoint, nodeId))
            return reject<void>("editor.road.detach-payload", "Road endpoint detach payload is invalid");
        auto candidate = *network_;
        auto detached  = candidate.detachEdgeEndpoint(edgeId, fromEndpoint, nodeId);
        if (!detached.ok()) return roadFailure<void>(detached.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kReattachEdgeEndpoint) {
        std::uint32_t                                  edgeId = 0, oldNodeId = 0, detachedNodeId = 0;
        bool                                           fromEndpoint = false;
        std::vector<procgen::road::RoadLaneConnection> restoreLinks;
        std::vector<procgen::road::RoadLaneConnection> restoreBlockedLinks;
        if (!decodeReattachEndpoint(operation.payload, edgeId, fromEndpoint, oldNodeId, detachedNodeId, restoreLinks,
                                    restoreBlockedLinks))
            return reject<void>("editor.road.reattach-payload", "Road endpoint reattach payload is invalid");
        auto candidate = *network_;
        auto changed   = candidate.reconnectEdgeEndpoint(edgeId, fromEndpoint, oldNodeId);
        if (!changed.ok()) return roadFailure<void>(changed.status());
        for (const auto& link : restoreLinks) {
            auto restored = candidate.addLaneLink(link);
            if (!restored.ok()) return roadFailure<void>(restored.status());
        }
        for (const auto& link : restoreBlockedLinks) {
            auto restored = candidate.blockLaneLink(link);
            if (!restored.ok()) return roadFailure<void>(restored.status());
        }
        auto removed = candidate.removeNode(detachedNodeId);
        if (!removed.ok()) return roadFailure<void>(removed.status());
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kSplitEdge) {
        std::uint32_t edgeId = 0, nodeId = 0, secondEdgeId = 0;
        std::size_t   controlPointIndex = 0;
        float         junctionRadius    = 0.f;
        if (!decodeSplit(operation.payload, edgeId, controlPointIndex, junctionRadius, nodeId, secondEdgeId))
            return reject<void>("editor.road.split-payload", "Road split payload is invalid");
        auto candidate = *network_;
        auto split     = candidate.splitEdge(edgeId, controlPointIndex, junctionRadius, nodeId, secondEdgeId);
        if (!split.ok()) return roadFailure<void>(split.status());
        if (split.value().nodeId != nodeId || split.value().secondEdgeId != secondEdgeId)
            return reject<void>("editor.road.split-identity", "Road split reserved identities no longer match",
                                EditorStatus::Conflict);
        *network_ = std::move(candidate);
    } else if (operation.type == kSplitEdgeAtPosition) {
        std::uint32_t                   edgeId = 0, nodeId = 0, secondEdgeId = 0;
        procgen::road::RoadControlPoint position;
        float                           maxDistance = 0.f, junctionRadius = 0.f;
        if (!decodePositionSplit(operation.payload, edgeId, position, maxDistance, junctionRadius, nodeId,
                                 secondEdgeId))
            return reject<void>("editor.road.split-position-payload", "Road position split payload is invalid");
        auto candidate = *network_;
        auto split = candidate.splitEdgeAtPosition(edgeId, position, maxDistance, junctionRadius, nodeId, secondEdgeId);
        if (!split.ok()) return roadFailure<void>(split.status());
        if (split.value().nodeId != nodeId || split.value().secondEdgeId != secondEdgeId)
            return reject<void>("editor.road.split-position-identity",
                                "Road position split reserved identities no longer match", EditorStatus::Conflict);
        *network_ = std::move(candidate);
    } else if (operation.type == kConnectNodeAtPosition) {
        std::uint32_t                   edgeId = 0, nodeId = 0, secondEdgeId = 0;
        procgen::road::RoadControlPoint position;
        procgen::road::RoadEdge         branch;
        float                           maxDistance = 0.f, junctionRadius = 0.f;
        if (!decodeConnectAtPosition(operation.payload, edgeId, position, maxDistance, junctionRadius, nodeId,
                                     secondEdgeId, branch))
            return reject<void>("editor.road.connect-position-payload", "Road branch connection payload is invalid");
        auto candidate = *network_;
        auto split = candidate.splitEdgeAtPosition(edgeId, position, maxDistance, junctionRadius, nodeId, secondEdgeId);
        if (!split.ok()) return roadFailure<void>(split.status());
        auto restoredBranch = candidate.restoreEdge(branch);
        if (!restoredBranch.ok()) return roadFailure<void>(restoredBranch.status());
        auto connected = candidate.connectAllTurns(nodeId);
        if (!connected.ok()) return roadFailure<void>(connected.status());
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kDisconnectNodeAtPosition) {
        procgen::road::RoadEdge                        original;
        std::vector<procgen::road::RoadLaneConnection> originalLinks;
        std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
        std::uint32_t                                  nodeId = 0, secondEdgeId = 0, branchEdgeId = 0;
        if (!decodeDisconnectAtPosition(operation.payload, original, originalLinks, originalBlockedLinks, nodeId,
                                        secondEdgeId, branchEdgeId))
            return reject<void>("editor.road.disconnect-position-payload",
                                "Road branch disconnection payload is invalid");
        auto candidate     = *network_;
        auto removedBranch = candidate.removeEdge(branchEdgeId);
        if (!removedBranch.ok()) return roadFailure<void>(removedBranch.status());
        auto removedSecond = candidate.removeEdge(secondEdgeId);
        if (!removedSecond.ok()) return roadFailure<void>(removedSecond.status());
        auto removedFirst = candidate.removeEdge(original.id);
        if (!removedFirst.ok()) return roadFailure<void>(removedFirst.status());
        auto removedNode = candidate.removeNode(nodeId);
        if (!removedNode.ok()) return roadFailure<void>(removedNode.status());
        auto restoredOriginal = candidate.restoreEdge(original);
        if (!restoredOriginal.ok()) return roadFailure<void>(restoredOriginal.status());
        for (const auto& link : originalLinks) {
            auto restoredLink = candidate.addLaneLink(link);
            if (!restoredLink.ok()) return roadFailure<void>(restoredLink.status());
        }
        for (const auto& link : originalBlockedLinks) {
            auto blocked = candidate.blockLaneLink(link);
            if (!blocked.ok()) return roadFailure<void>(blocked.status());
        }
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kConnectEdgeIntersection) {
        std::uint32_t                   firstEdgeId = 0, secondEdgeId = 0, firstNodeId = 0, firstSecondEdgeId = 0;
        std::uint32_t                   secondNodeId = 0, secondSecondEdgeId = 0;
        procgen::road::RoadControlPoint mergedPoint;
        float                           firstParameter = 0.f, secondParameter = 0.f;
        float                           maximumHeightDelta = 0.f, junctionRadius = 0.f;
        if (!decodeIntersectionConnect(operation.payload, firstEdgeId, secondEdgeId, firstParameter, secondParameter,
                                       mergedPoint, maximumHeightDelta, junctionRadius, firstNodeId, firstSecondEdgeId,
                                       secondNodeId, secondSecondEdgeId))
            return reject<void>("editor.road.intersection-payload", "Road intersection payload is invalid");
        auto candidate  = *network_;
        auto firstSplit = candidate.splitEdgeAtSplineParameter(firstEdgeId, firstParameter, junctionRadius, firstNodeId,
                                                               firstSecondEdgeId);
        if (!firstSplit.ok()) return roadFailure<void>(firstSplit.status());
        auto secondSplit = candidate.splitEdgeAtSplineParameter(secondEdgeId, secondParameter, junctionRadius,
                                                                secondNodeId, secondSecondEdgeId);
        if (!secondSplit.ok()) return roadFailure<void>(secondSplit.status());
        auto moved = candidate.setNodePosition(firstNodeId, mergedPoint.x, mergedPoint.y, mergedPoint.z);
        if (!moved.ok()) return roadFailure<void>(moved.status());
        moved = candidate.setNodePosition(secondNodeId, mergedPoint.x, mergedPoint.y, mergedPoint.z);
        if (!moved.ok()) return roadFailure<void>(moved.status());
        auto merged = candidate.mergeNodes(firstNodeId, secondNodeId, 1e-3f);
        if (!merged.ok()) return roadFailure<void>(merged.status());
        auto connected = candidate.connectAllTurns(firstNodeId);
        if (!connected.ok()) return roadFailure<void>(connected.status());
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kDisconnectEdgeIntersection) {
        procgen::road::RoadEdge                        first, second;
        std::vector<procgen::road::RoadLaneConnection> originalLinks;
        std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
        std::uint32_t                                  sharedNodeId = 0, firstSecondEdgeId = 0, secondSecondEdgeId = 0;
        if (!decodeIntersectionDisconnect(operation.payload, first, second, originalLinks, originalBlockedLinks,
                                          sharedNodeId, firstSecondEdgeId, secondSecondEdgeId))
            return reject<void>("editor.road.intersection-inverse", "Road intersection inverse payload is invalid");
        auto candidate = *network_;
        for (const auto edgeId : {firstSecondEdgeId, secondSecondEdgeId, first.id, second.id}) {
            auto removed = candidate.removeEdge(edgeId);
            if (!removed.ok()) return roadFailure<void>(removed.status());
        }
        auto removedNode = candidate.removeNode(sharedNodeId);
        if (!removedNode.ok()) return roadFailure<void>(removedNode.status());
        auto restoredFirst = candidate.restoreEdge(first);
        if (!restoredFirst.ok()) return roadFailure<void>(restoredFirst.status());
        auto restoredSecond = candidate.restoreEdge(second);
        if (!restoredSecond.ok()) return roadFailure<void>(restoredSecond.status());
        for (const auto& link : originalLinks) {
            auto restored = candidate.addLaneLink(link);
            if (!restored.ok()) return roadFailure<void>(restored.status());
        }
        for (const auto& link : originalBlockedLinks) {
            auto blocked = candidate.blockLaneLink(link);
            if (!blocked.ok()) return roadFailure<void>(blocked.status());
        }
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kUnsplitEdge) {
        procgen::road::RoadEdge                        edge;
        std::vector<procgen::road::RoadLaneConnection> links;
        std::vector<procgen::road::RoadLaneConnection> blockedLinks;
        std::uint32_t                                  nodeId = 0, secondEdgeId = 0;
        if (!decodeUnsplit(operation.payload, edge, links, blockedLinks, nodeId, secondEdgeId))
            return reject<void>("editor.road.unsplit-payload", "Road unsplit payload is invalid");
        auto candidate     = *network_;
        auto removedSecond = candidate.removeEdge(secondEdgeId);
        if (!removedSecond.ok()) return roadFailure<void>(removedSecond.status());
        auto removedFirst = candidate.removeEdge(edge.id);
        if (!removedFirst.ok()) return roadFailure<void>(removedFirst.status());
        auto removedNode = candidate.removeNode(nodeId);
        if (!removedNode.ok()) return roadFailure<void>(removedNode.status());
        auto restoredEdge = candidate.restoreEdge(edge);
        if (!restoredEdge.ok()) return roadFailure<void>(restoredEdge.status());
        for (const auto& link : links) {
            auto restoredLink = candidate.addLaneLink(link);
            if (!restoredLink.ok()) return roadFailure<void>(restoredLink.status());
        }
        for (const auto& link : blockedLinks) {
            auto blocked = candidate.blockLaneLink(link);
            if (!blocked.ok()) return roadFailure<void>(blocked.status());
        }
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kRestoreNode) {
        procgen::road::RoadNode node;
        if (!decodeNodeRecord(operation.payload, node))
            return reject<void>("editor.road.node-payload", "Road node restore payload is invalid");
        auto candidate = *network_;
        auto restored  = candidate.restoreNode(node);
        if (!restored.ok()) return roadFailure<void>(restored.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kRemoveNode) {
        std::uint32_t nodeId = 0;
        if (!readId(operation.payload, "id", nodeId))
            return reject<void>("editor.road.node-payload", "Road node removal payload is invalid");
        auto candidate = *network_;
        auto removed   = candidate.removeNode(nodeId);
        if (!removed.ok()) return roadFailure<void>(removed.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kRestoreEdge) {
        procgen::road::RoadEdge                        edge;
        std::vector<procgen::road::RoadLaneConnection> links;
        std::vector<procgen::road::RoadLaneConnection> blockedLinks;
        if (!decodeEdgeRecord(operation.payload, edge, links, &blockedLinks))
            return reject<void>("editor.road.edge-payload", "Road edge restore payload is invalid");
        auto candidate = *network_;
        auto restored  = candidate.restoreEdge(std::move(edge));
        if (!restored.ok()) return roadFailure<void>(restored.status());
        for (const auto& link : links) {
            auto linked = candidate.addLaneLink(link);
            if (!linked.ok()) return roadFailure<void>(linked.status());
        }
        for (const auto& link : blockedLinks) {
            auto blocked = candidate.blockLaneLink(link);
            if (!blocked.ok()) return roadFailure<void>(blocked.status());
        }
        *network_ = std::move(candidate);
    } else if (operation.type == kRemoveEdge) {
        std::uint32_t edgeId = 0;
        if (!readId(operation.payload, "id", edgeId))
            return reject<void>("editor.road.edge-payload", "Road edge removal payload is invalid");
        auto candidate = *network_;
        auto removed   = candidate.removeEdge(edgeId);
        if (!removed.ok()) return roadFailure<void>(removed.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kRestoreLaneLink || operation.type == kRemoveLaneLink) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(operation.payload, link))
            return reject<void>("editor.road.lane-link-payload", "Road lane-link payload is invalid");
        auto candidate = *network_;
        auto changed =
            operation.type == kRestoreLaneLink ? candidate.addLaneLink(link) : candidate.removeLaneLink(link);
        if (!changed.ok()) return roadFailure<void>(changed.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kBlockLaneLink || operation.type == kUnblockLaneLink) {
        procgen::road::RoadLaneConnection link;
        bool                              restoreActive = false;
        if (!decodeBlockedLink(operation.payload, link, restoreActive))
            return reject<void>("editor.road.lane-link-block-payload", "Road lane-link block payload is invalid");
        auto candidate = *network_;
        if (operation.type == kBlockLaneLink) {
            auto changed = candidate.blockLaneLink(link);
            if (!changed.ok()) return roadFailure<void>(changed.status());
        } else {
            auto changed = candidate.unblockLaneLink(link);
            if (!changed.ok()) return roadFailure<void>(changed.status());
            if (restoreActive) {
                auto restored = candidate.addLaneLink(link);
                if (!restored.ok()) return roadFailure<void>(restored.status());
            }
        }
        *network_ = std::move(candidate);
    } else if (operation.type == kRestoreLaneLinks || operation.type == kRemoveLaneLinks) {
        std::vector<procgen::road::RoadLaneConnection> links;
        if (!decodeLinks(operation.payload, links))
            return reject<void>("editor.road.lane-links-payload", "Road lane-link batch payload is invalid");
        auto candidate = *network_;
        for (const auto& link : links) {
            auto changed =
                operation.type == kRestoreLaneLinks ? candidate.addLaneLink(link) : candidate.removeLaneLink(link);
            if (!changed.ok()) return roadFailure<void>(changed.status());
        }
        *network_ = std::move(candidate);
    } else {
        return reject<void>("editor.road.type", "Unsupported road operation type", EditorStatus::Unsupported);
    }
    dirty_.include(0, 0);
    return editing::applied();
}

}  // namespace eve::procgen_editing

// RoadNetworkEditingCommands.cpp
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

    auto registered = add(
        "road.node.add.v1", "Add road node",
        [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
            auto* road = target(value);
            float x = 0.f, y = 0.f, z = 0.f, radius = 6.f;
            if (!road || !readNumber(request.payload, "x", x) || !readNumber(request.payload, "y", y) ||
                !readNumber(request.payload, "z", z) ||
                (field(request.payload, "junctionRadius") && !readNumber(request.payload, "junctionRadius", radius)))
                return commandError<editing::CommandPlan>("editor.road.command.node-add",
                                                          "Road node add requires finite x, y and z");
            return plan(road->makeAddNode(x, y, z, radius));
        });
    if (!registered.ok()) return registered;

    registered = add("road.node.move.v1", "Move road node",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*         road = target(value);
                         std::uint32_t id   = 0;
                         float         x = 0.f, y = 0.f, z = 0.f;
                         if (!road || !readId(request.payload, "id", id) || !readNumber(request.payload, "x", x) ||
                             !readNumber(request.payload, "y", y) || !readNumber(request.payload, "z", z))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.node-move", "Road node move requires id and finite x, y and z");
                         return plan(road->makeMoveNode(id, x, y, z));
                     });
    if (!registered.ok()) return registered;

    registered = add(
        "road.node.radius.set.v1", "Set road junction radius",
        [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
            auto*         road   = target(value);
            std::uint32_t id     = 0;
            float         radius = 0.f;
            if (!road || !readId(request.payload, "id", id) || !readNumber(request.payload, "junctionRadius", radius))
                return commandError<editing::CommandPlan>("editor.road.command.node-radius",
                                                          "Road node radius requires id and radius");
            return plan(road->makeSetNodeJunctionRadius(id, radius));
        });
    if (!registered.ok()) return registered;

    registered = add(
        "road.node.junction-control.set.v1", "Set road junction control",
        [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
            auto*         road    = target(value);
            std::uint32_t id      = 0;
            int           control = 0;
            if (!road || !readId(request.payload, "id", id) ||
                !readSmallInt(request.payload, "junctionControl", 0, 3, control))
                return commandError<editing::CommandPlan>(
                    "editor.road.command.node-control",
                    "Road junction control requires id and junctionControl in [0,3]");
            return plan(road->makeSetNodeJunctionControl(id, static_cast<procgen::road::RoadJunctionControl>(control)));
        });
    if (!registered.ok()) return registered;

    registered =
        add("road.node.merge.v1", "Merge nearby road nodes",
            [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                auto*         road       = target(value);
                std::uint32_t keepNodeId = 0, removeNodeId = 0;
                float         maxDistance = 1.f;
                if (!road || !readId(request.payload, "keepNodeId", keepNodeId) ||
                    !readId(request.payload, "removeNodeId", removeNodeId) ||
                    (field(request.payload, "maxDistance") && !readNumber(request.payload, "maxDistance", maxDistance)))
                    return commandError<editing::CommandPlan>("editor.road.command.node-merge",
                                                              "Road node merge requires two node ids");
                return plan(road->makeMergeNodes(keepNodeId, removeNodeId, maxDistance));
            });
    if (!registered.ok()) return registered;

    registered =
        add("road.node.merge-and-connect.v1", "Merge road nodes and connect turns",
            [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                auto*         road       = target(value);
                std::uint32_t keepNodeId = 0, removeNodeId = 0;
                float         maxDistance = 1.f;
                if (!road || !readId(request.payload, "keepNodeId", keepNodeId) ||
                    !readId(request.payload, "removeNodeId", removeNodeId) ||
                    (field(request.payload, "maxDistance") && !readNumber(request.payload, "maxDistance", maxDistance)))
                    return commandError<editing::CommandPlan>("editor.road.command.node-merge-connect",
                                                              "Road node merge and connect requires two node ids");
                return plan(road->makeMergeNodesAndConnect(keepNodeId, removeNodeId, maxDistance));
            });
    if (!registered.ok()) return registered;

    registered = add("road.node.remove.v1", "Remove road node",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*         road = target(value);
                         std::uint32_t id   = 0;
                         if (!road || !readId(request.payload, "id", id))
                             return commandError<editing::CommandPlan>("editor.road.command.node-remove",
                                                                       "Road node remove requires id");
                         return plan(road->makeRemoveNode(id));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.node.connect-all-turns.v1", "Connect all road turns",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*         road = target(value);
                         std::uint32_t id   = 0;
                         if (!road || !readId(request.payload, "id", id))
                             return commandError<editing::CommandPlan>("editor.road.command.connect-turns",
                                                                       "Connect all turns requires a node id");
                         return plan(road->makeConnectAllTurns(id));
                     });
    if (!registered.ok()) return registered;

    registered =
        add("road.edge.add.v1", "Add road edge",
            [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                auto*                                        road = target(value);
                std::uint32_t                                from = 0, to = 0, ignored = 0;
                int                                          forward = 2, backward = 0;
                std::vector<procgen::road::RoadControlPoint> points;
                EditorValue::Object                          pointPayload{{"id", std::int64_t{1}}};
                if (const auto* encoded = field(request.payload, "points")) pointPayload["points"] = *encoded;
                if (!road || !readId(request.payload, "from", from) || !readId(request.payload, "to", to) ||
                    !decodeEdge(EditorValue(std::move(pointPayload)), ignored, points) ||
                    (field(request.payload, "lanesForward") &&
                     !readSmallInt(request.payload, "lanesForward", 0, 8, forward)) ||
                    (field(request.payload, "lanesBackward") &&
                     !readSmallInt(request.payload, "lanesBackward", 0, 8, backward)))
                    return commandError<editing::CommandPlan>(
                        "editor.road.command.edge-add", "Road edge add requires endpoints and at least two points");
                return plan(road->makeAddEdge(from, to, std::move(points), forward, backward));
            });
    if (!registered.ok()) return registered;

    registered =
        add("road.edge.add-and-connect.v1", "Add road edge and connect endpoints",
            [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                auto*                                        road = target(value);
                std::uint32_t                                from = 0, to = 0, ignored = 0;
                int                                          forward = 2, backward = 0;
                std::vector<procgen::road::RoadControlPoint> points;
                EditorValue::Object                          pointPayload{{"id", std::int64_t{1}}};
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
                        return commandError<editing::CommandPlan>("editor.road.command.edge-add-connect-style",
                                                                  "Road edge style is invalid");
                    EditorValue::Object stylePayload = *styleObject;
                    stylePayload["id"]               = std::int64_t{1};
                    if (!decodeStyle(EditorValue(std::move(stylePayload)), ignored, style))
                        return commandError<editing::CommandPlan>("editor.road.command.edge-add-connect-style",
                                                                  "Road edge style is invalid");
                }
                return plan(road->makeAddEdgeAndConnect(from, to, std::move(points), forward, backward, style));
            });
    if (!registered.ok()) return registered;

    registered = add(
        "road.edge.split.v1", "Split road edge",
        [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
            auto*         road   = target(value);
            std::uint32_t id     = 0;
            int           index  = 0;
            float         radius = 6.f;
            if (!road || !readId(request.payload, "id", id) ||
                !readSmallInt(request.payload, "controlPointIndex", 1, 4095, index) ||
                (field(request.payload, "junctionRadius") && !readNumber(request.payload, "junctionRadius", radius)))
                return commandError<editing::CommandPlan>(
                    "editor.road.command.edge-split", "Road split requires edge id and interior control-point index");
            return plan(road->makeSplitEdge(id, static_cast<std::size_t>(index), radius));
        });
    if (!registered.ok()) return registered;

    registered = add(
        "road.edge.split-at-position.v1", "Split road edge at world position",
        [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
            auto*                           road = target(value);
            std::uint32_t                   id   = 0;
            procgen::road::RoadControlPoint position;
            float                           maxDistance = 2.f, radius = 6.f;
            if (!road || !readId(request.payload, "id", id) || !readNumber(request.payload, "x", position.x) ||
                !readNumber(request.payload, "y", position.y) || !readNumber(request.payload, "z", position.z) ||
                (field(request.payload, "maxDistance") && !readNumber(request.payload, "maxDistance", maxDistance)) ||
                (field(request.payload, "junctionRadius") && !readNumber(request.payload, "junctionRadius", radius)))
                return commandError<editing::CommandPlan>(
                    "editor.road.command.edge-split-position",
                    "Road position split requires edge id and a finite world position");
            return plan(road->makeSplitEdgeAtPosition(id, position, maxDistance, radius));
        });
    if (!registered.ok()) return registered;

    registered = add(
        "road.node.connect-at-position.v1", "Connect road node at world position",
        [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
            auto*                           road         = target(value);
            std::uint32_t                   sourceNodeId = 0, edgeId = 0;
            procgen::road::RoadControlPoint position;
            float                           maxDistance = 2.f, radius = 6.f;
            int                             forward = 1, backward = 0;
            if (!road || !readId(request.payload, "sourceNodeId", sourceNodeId) ||
                !readId(request.payload, "edgeId", edgeId) || !readNumber(request.payload, "x", position.x) ||
                !readNumber(request.payload, "y", position.y) || !readNumber(request.payload, "z", position.z) ||
                (field(request.payload, "maxDistance") && !readNumber(request.payload, "maxDistance", maxDistance)) ||
                (field(request.payload, "junctionRadius") && !readNumber(request.payload, "junctionRadius", radius)) ||
                (field(request.payload, "lanesForward") &&
                 !readSmallInt(request.payload, "lanesForward", 0, 8, forward)) ||
                (field(request.payload, "lanesBackward") &&
                 !readSmallInt(request.payload, "lanesBackward", 0, 8, backward)))
                return commandError<editing::CommandPlan>(
                    "editor.road.command.node-connect-position",
                    "Road branch connection requires source node, edge and world position");
            return plan(road->makeConnectNodeAtPosition(sourceNodeId, edgeId, position, maxDistance, radius, forward,
                                                        backward));
        });
    if (!registered.ok()) return registered;

    registered = add(
        "road.edge.connect-intersection.v1", "Connect crossing road edges",
        [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
            auto*         road        = target(value);
            std::uint32_t firstEdgeId = 0, secondEdgeId = 0;
            float         maximumHeightDelta = 0.5f, radius = 6.f;
            if (!road || !readId(request.payload, "firstEdgeId", firstEdgeId) ||
                !readId(request.payload, "secondEdgeId", secondEdgeId) ||
                (field(request.payload, "maximumHeightDelta") &&
                 !readNumber(request.payload, "maximumHeightDelta", maximumHeightDelta)) ||
                (field(request.payload, "junctionRadius") && !readNumber(request.payload, "junctionRadius", radius)))
                return commandError<editing::CommandPlan>(
                    "editor.road.command.edge-intersection",
                    "Road intersection requires two edge ids and finite tolerances");
            return plan(road->makeConnectEdgeIntersection(firstEdgeId, secondEdgeId, maximumHeightDelta, radius));
        });
    if (!registered.ok()) return registered;

    registered = add("road.edge.lanes.set.v1", "Set road edge lanes",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*         road    = target(value);
                         std::uint32_t id      = 0;
                         int           forward = 0, backward = 0;
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
                         auto*                    road   = target(value);
                         std::uint32_t            edgeId = 0;
                         procgen::road::RoadStyle style;
                         if (!road || !decodeStyle(request.payload, edgeId, style))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-style", "Road edge style requires a complete style payload");
                         return plan(road->makeSetEdgeStyle(edgeId, style));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.control-points.set.v1", "Set road edge control points",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*                                        road   = target(value);
                         std::uint32_t                                edgeId = 0;
                         std::vector<procgen::road::RoadControlPoint> points;
                         if (!road || !decodeEdge(request.payload, edgeId, points))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-control-points",
                                 "Road edge control points require id and at least two finite points");
                         return plan(road->makeSetEdgeControlPoints(edgeId, std::move(points)));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.reverse.v1", "Reverse road edge",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*         road   = target(value);
                         std::uint32_t edgeId = 0;
                         if (!road || !readId(request.payload, "id", edgeId))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-reverse",
                                                                       "Road edge reverse requires an id");
                         return plan(road->makeReverseEdge(edgeId));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.reconnect-endpoint.v1", "Reconnect road edge endpoint",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*         road   = target(value);
                         std::uint32_t edgeId = 0, nodeId = 0;
                         const auto*   endpointValue = field(request.payload, "fromEndpoint");
                         const auto*   fromEndpoint  = endpointValue ? endpointValue->getIf<bool>() : nullptr;
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
                         auto*         road   = target(value);
                         std::uint32_t edgeId = 0, nodeId = 0;
                         const auto*   endpointValue = field(request.payload, "fromEndpoint");
                         const auto*   fromEndpoint  = endpointValue ? endpointValue->getIf<bool>() : nullptr;
                         if (!road || !fromEndpoint || !readId(request.payload, "id", edgeId) ||
                             !readId(request.payload, "nodeId", nodeId))
                             return commandError<editing::CommandPlan>(
                                 "editor.road.command.edge-reconnect-connect",
                                 "Road edge reconnect-and-connect requires id, fromEndpoint, and nodeId");
                         return plan(road->makeReconnectEdgeEndpointAndConnect(edgeId, *fromEndpoint, nodeId));
                     });
    if (!registered.ok()) return registered;

    registered =
        add("road.edge.snap-endpoint.v1", "Snap road edge endpoint to nearest node",
            [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                auto*         road          = target(value);
                std::uint32_t edgeId        = 0;
                float         maxDistance   = 0.f;
                bool          connectTurns  = true;
                const auto*   endpointValue = field(request.payload, "fromEndpoint");
                const auto*   fromEndpoint  = endpointValue ? endpointValue->getIf<bool>() : nullptr;
                const auto*   connectValue  = field(request.payload, "connectTurns");
                const auto*   connect       = connectValue ? connectValue->getIf<bool>() : nullptr;
                if (!road || !fromEndpoint || (connectValue && !connect) || !readId(request.payload, "id", edgeId) ||
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
                         auto*         road          = target(value);
                         std::uint32_t edgeId        = 0;
                         const auto*   endpointValue = field(request.payload, "fromEndpoint");
                         const auto*   fromEndpoint  = endpointValue ? endpointValue->getIf<bool>() : nullptr;
                         if (!road || !fromEndpoint || !readId(request.payload, "id", edgeId))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-detach",
                                                                       "Road edge detach requires id and fromEndpoint");
                         return plan(road->makeDetachEdgeEndpoint(edgeId, *fromEndpoint));
                     });
    if (!registered.ok()) return registered;

    registered = add("road.edge.remove.v1", "Remove road edge",
                     [target, plan](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         auto*         road = target(value);
                         std::uint32_t id   = 0;
                         if (!road || !readId(request.payload, "id", id))
                             return commandError<editing::CommandPlan>("editor.road.command.edge-remove",
                                                                       "Road edge remove requires id");
                         return plan(road->makeRemoveEdge(id));
                     });
    if (!registered.ok()) return registered;

    auto laneLinkCommand = [target, plan](bool addLink, editing::IEditableTarget& value,
                                          const editing::CommandRequest& request) {
        auto*                             road = target(value);
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
        auto*                             road = target(value);
        procgen::road::RoadLaneConnection link;
        if (!road || !decodeLink(request.payload, link))
            return commandError<editing::CommandPlan>("editor.road.command.lane-link-block",
                                                      "Road lane-link block command requires a complete directed link");
        return plan(block ? road->makeBlockLaneLink(link) : road->makeUnblockLaneLink(link));
    };
    registered = add("road.lane-link.block.v1", "Block road lane link",
                     [laneLinkBlockCommand](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                         return laneLinkBlockCommand(true, value, request);
                     });
    if (!registered.ok()) return registered;
    return add("road.lane-link.unblock.v1", "Unblock road lane link",
               [laneLinkBlockCommand](editing::IEditableTarget& value, const editing::CommandRequest& request) {
                   return laneLinkBlockCommand(false, value, request);
               });
}

}  // namespace eve::procgen_editing
