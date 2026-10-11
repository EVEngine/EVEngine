#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editing/RoadNetworkEditTargetInternal.inc"

#include <algorithm>
#include <cmath>
#include <utility>

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
        std::uint32_t nodeId = 0;
        float junctionRadius = 0.f;
        if (!decodeNodeRadius(operation.payload, nodeId, junctionRadius))
            return reject<void>("editor.road.node-radius-payload", "Road node radius payload is invalid");
        auto changed = network_->setNodeJunctionRadius(nodeId, junctionRadius);
        if (!changed.ok()) return roadFailure<void>(changed.status());
    } else if (operation.type == kSetNodeControl) {
        std::uint32_t nodeId = 0;
        procgen::road::RoadJunctionControl control = procgen::road::RoadJunctionControl::Uncontrolled;
        if (!decodeNodeControl(operation.payload, nodeId, control))
            return reject<void>("editor.road.node-control-payload", "Road node control payload is invalid");
        auto changed = network_->setNodeJunctionControl(nodeId, control);
        if (!changed.ok()) return roadFailure<void>(changed.status());
    } else if (operation.type == kMergeNodes || operation.type == kMergeNodesAndConnect) {
        std::uint32_t keepNodeId = 0, removeNodeId = 0;
        float maxDistance = 0.f;
        if (!decodeMergeNodes(operation.payload, keepNodeId, removeNodeId, maxDistance))
            return reject<void>("editor.road.merge-payload", "Road node merge payload is invalid");
        auto candidate = *network_;
        auto merged = candidate.mergeNodes(keepNodeId, removeNodeId, maxDistance);
        if (!merged.ok()) return roadFailure<void>(merged.status());
        if (operation.type == kMergeNodesAndConnect) {
            auto connected = candidate.connectAllTurns(keepNodeId);
            if (!connected.ok()) return roadFailure<void>(connected.status());
        }
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kUnmergeNodes) {
        procgen::road::RoadNode removedNode;
        std::vector<procgen::road::RoadEdge> affectedEdges;
        std::vector<procgen::road::RoadLaneConnection> affectedLinks;
        std::vector<procgen::road::RoadLaneConnection> affectedBlockedLinks;
        if (!decodeUnmergeNodes(operation.payload, removedNode, affectedEdges, affectedLinks,
                                affectedBlockedLinks))
            return reject<void>("editor.road.unmerge-payload", "Road node unmerge payload is invalid");
        auto candidate = *network_;
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
        std::uint32_t                                edgeId = 0;
        int                                          lanesForward = 0, lanesBackward = 0;
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
        std::uint32_t edgeId = 0, nodeId = 0;
        bool fromEndpoint = false;
        std::vector<procgen::road::RoadLaneConnection> restoreLinks;
        std::vector<procgen::road::RoadLaneConnection> restoreBlockedLinks;
        if (!decodeReconnectEndpoint(operation.payload, edgeId, fromEndpoint, nodeId, restoreLinks,
                                     restoreBlockedLinks))
            return reject<void>("editor.road.reconnect-payload", "Road endpoint reconnect payload is invalid");
        auto candidate = *network_;
        auto changed = candidate.reconnectEdgeEndpoint(edgeId, fromEndpoint, nodeId);
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
        bool fromEndpoint = false;
        if (!decodeDetachEndpoint(operation.payload, edgeId, fromEndpoint, nodeId))
            return reject<void>("editor.road.detach-payload", "Road endpoint detach payload is invalid");
        auto candidate = *network_;
        auto detached = candidate.detachEdgeEndpoint(edgeId, fromEndpoint, nodeId);
        if (!detached.ok()) return roadFailure<void>(detached.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kReattachEdgeEndpoint) {
        std::uint32_t edgeId = 0, oldNodeId = 0, detachedNodeId = 0;
        bool fromEndpoint = false;
        std::vector<procgen::road::RoadLaneConnection> restoreLinks;
        std::vector<procgen::road::RoadLaneConnection> restoreBlockedLinks;
        if (!decodeReattachEndpoint(operation.payload, edgeId, fromEndpoint, oldNodeId, detachedNodeId,
                                    restoreLinks, restoreBlockedLinks))
            return reject<void>("editor.road.reattach-payload", "Road endpoint reattach payload is invalid");
        auto candidate = *network_;
        auto changed = candidate.reconnectEdgeEndpoint(edgeId, fromEndpoint, oldNodeId);
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
        std::size_t controlPointIndex = 0;
        float junctionRadius = 0.f;
        if (!decodeSplit(operation.payload, edgeId, controlPointIndex, junctionRadius, nodeId, secondEdgeId))
            return reject<void>("editor.road.split-payload", "Road split payload is invalid");
        auto candidate = *network_;
        auto split = candidate.splitEdge(edgeId, controlPointIndex, junctionRadius, nodeId, secondEdgeId);
        if (!split.ok()) return roadFailure<void>(split.status());
        if (split.value().nodeId != nodeId || split.value().secondEdgeId != secondEdgeId)
            return reject<void>("editor.road.split-identity", "Road split reserved identities no longer match",
                                EditorStatus::Conflict);
        *network_ = std::move(candidate);
    } else if (operation.type == kSplitEdgeAtPosition) {
        std::uint32_t edgeId = 0, nodeId = 0, secondEdgeId = 0;
        procgen::road::RoadControlPoint position;
        float maxDistance = 0.f, junctionRadius = 0.f;
        if (!decodePositionSplit(operation.payload, edgeId, position, maxDistance, junctionRadius, nodeId,
                                 secondEdgeId))
            return reject<void>("editor.road.split-position-payload", "Road position split payload is invalid");
        auto candidate = *network_;
        auto split = candidate.splitEdgeAtPosition(edgeId, position, maxDistance, junctionRadius, nodeId,
                                                   secondEdgeId);
        if (!split.ok()) return roadFailure<void>(split.status());
        if (split.value().nodeId != nodeId || split.value().secondEdgeId != secondEdgeId)
            return reject<void>("editor.road.split-position-identity",
                                "Road position split reserved identities no longer match", EditorStatus::Conflict);
        *network_ = std::move(candidate);
    } else if (operation.type == kConnectNodeAtPosition) {
        std::uint32_t edgeId = 0, nodeId = 0, secondEdgeId = 0;
        procgen::road::RoadControlPoint position;
        procgen::road::RoadEdge branch;
        float maxDistance = 0.f, junctionRadius = 0.f;
        if (!decodeConnectAtPosition(operation.payload, edgeId, position, maxDistance, junctionRadius, nodeId,
                                     secondEdgeId, branch))
            return reject<void>("editor.road.connect-position-payload",
                                "Road branch connection payload is invalid");
        auto candidate = *network_;
        auto split = candidate.splitEdgeAtPosition(edgeId, position, maxDistance, junctionRadius, nodeId,
                                                   secondEdgeId);
        if (!split.ok()) return roadFailure<void>(split.status());
        auto restoredBranch = candidate.restoreEdge(branch);
        if (!restoredBranch.ok()) return roadFailure<void>(restoredBranch.status());
        auto connected = candidate.connectAllTurns(nodeId);
        if (!connected.ok()) return roadFailure<void>(connected.status());
        auto valid = candidate.validate();
        if (!valid.ok()) return roadFailure<void>(valid.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kDisconnectNodeAtPosition) {
        procgen::road::RoadEdge original;
        std::vector<procgen::road::RoadLaneConnection> originalLinks;
        std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
        std::uint32_t nodeId = 0, secondEdgeId = 0, branchEdgeId = 0;
        if (!decodeDisconnectAtPosition(operation.payload, original, originalLinks, originalBlockedLinks, nodeId,
                                        secondEdgeId, branchEdgeId))
            return reject<void>("editor.road.disconnect-position-payload",
                                "Road branch disconnection payload is invalid");
        auto candidate = *network_;
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
        std::uint32_t firstEdgeId = 0, secondEdgeId = 0, firstNodeId = 0, firstSecondEdgeId = 0;
        std::uint32_t secondNodeId = 0, secondSecondEdgeId = 0;
        procgen::road::RoadControlPoint mergedPoint;
        float firstParameter = 0.f, secondParameter = 0.f;
        float maximumHeightDelta = 0.f, junctionRadius = 0.f;
        if (!decodeIntersectionConnect(operation.payload, firstEdgeId, secondEdgeId, firstParameter,
                                       secondParameter, mergedPoint, maximumHeightDelta, junctionRadius, firstNodeId,
                                       firstSecondEdgeId, secondNodeId, secondSecondEdgeId))
            return reject<void>("editor.road.intersection-payload", "Road intersection payload is invalid");
        auto candidate = *network_;
        auto firstSplit = candidate.splitEdgeAtSplineParameter(firstEdgeId, firstParameter, junctionRadius,
                                                               firstNodeId, firstSecondEdgeId);
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
        procgen::road::RoadEdge first, second;
        std::vector<procgen::road::RoadLaneConnection> originalLinks;
        std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
        std::uint32_t sharedNodeId = 0, firstSecondEdgeId = 0, secondSecondEdgeId = 0;
        if (!decodeIntersectionDisconnect(operation.payload, first, second, originalLinks, originalBlockedLinks,
                                          sharedNodeId,
                                          firstSecondEdgeId, secondSecondEdgeId))
            return reject<void>("editor.road.intersection-inverse",
                                "Road intersection inverse payload is invalid");
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
        std::uint32_t nodeId = 0, secondEdgeId = 0;
        if (!decodeUnsplit(operation.payload, edge, links, blockedLinks, nodeId, secondEdgeId))
            return reject<void>("editor.road.unsplit-payload", "Road unsplit payload is invalid");
        auto candidate = *network_;
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
        auto changed   = operation.type == kRestoreLaneLink ? candidate.addLaneLink(link)
                                                            : candidate.removeLaneLink(link);
        if (!changed.ok()) return roadFailure<void>(changed.status());
        *network_ = std::move(candidate);
    } else if (operation.type == kBlockLaneLink || operation.type == kUnblockLaneLink) {
        procgen::road::RoadLaneConnection link;
        bool restoreActive = false;
        if (!decodeBlockedLink(operation.payload, link, restoreActive))
            return reject<void>("editor.road.lane-link-block-payload",
                                "Road lane-link block payload is invalid");
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
            auto changed = operation.type == kRestoreLaneLinks ? candidate.addLaneLink(link)
                                                               : candidate.removeLaneLink(link);
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
