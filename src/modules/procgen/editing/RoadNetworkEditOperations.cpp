#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editing/RoadNetworkEditTargetInternal.inc"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::procgen_editing {
using namespace detail;

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeMoveNode(std::uint32_t nodeId, float x, float y,
                                                                           float z) const {
    auto node = network_->nodeResult(nodeId);
    if (!node.ok())
        return reject<editing::DomainOperation>("editor.road.node", "Road node does not exist", EditorStatus::NotFound);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return reject<editing::DomainOperation>("editor.road.position", "Road node position must be finite");
    editing::DomainOperation operation;
    operation.type        = kMoveNode;
    operation.inverseType = kMoveNode;
    operation.target      = targetId();
    operation.payload     = encodeNode(nodeId, x, y, z);
    operation.inverse     = encodeNode(nodeId, node.value().x, node.value().y, node.value().z);
    operation.hasInverse  = true;
    operation.mergeKey    = "road-node:" + std::to_string(nodeId);
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(nodeId));
    operation.affectedProperties.emplace_back("position");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSetNodeJunctionRadius(
    std::uint32_t nodeId, float junctionRadius) const {
    auto node = network_->nodeResult(nodeId);
    if (!node.ok()) return roadFailure<editing::DomainOperation>(node.status());
    auto candidate = *network_;
    auto changed = candidate.setNodeJunctionRadius(nodeId, junctionRadius);
    if (!changed.ok()) return roadFailure<editing::DomainOperation>(changed.status());
    editing::DomainOperation operation;
    operation.type        = kSetNodeRadius;
    operation.inverseType = kSetNodeRadius;
    operation.target      = targetId();
    operation.payload     = encodeNodeRadius(nodeId, junctionRadius);
    operation.inverse     = encodeNodeRadius(nodeId, node.value().junctionRadius);
    operation.hasInverse  = true;
    operation.mergeKey    = "road-node:" + std::to_string(nodeId) + ":junction-radius";
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(nodeId));
    operation.affectedProperties.emplace_back("junctionRadius");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSetNodeJunctionControl(
    std::uint32_t nodeId, procgen::road::RoadJunctionControl control) const {
    auto node = network_->nodeResult(nodeId);
    if (!node.ok()) return roadFailure<editing::DomainOperation>(node.status());
    auto candidate = *network_;
    auto changed = candidate.setNodeJunctionControl(nodeId, control);
    if (!changed.ok()) return roadFailure<editing::DomainOperation>(changed.status());
    editing::DomainOperation operation;
    operation.type        = kSetNodeControl;
    operation.inverseType = kSetNodeControl;
    operation.target      = targetId();
    operation.payload     = encodeNodeControl(nodeId, control);
    operation.inverse     = encodeNodeControl(nodeId, node.value().junctionControl);
    operation.hasInverse  = true;
    operation.mergeKey    = "road-node:" + std::to_string(nodeId) + ":junction-control";
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(nodeId));
    operation.affectedProperties.emplace_back("junctionControl");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeMergeNodes(std::uint32_t keepNodeId,
                                                                             std::uint32_t removeNodeId,
                                                                             float maxDistance) const {
    return makeMergeNodesImpl(keepNodeId, removeNodeId, maxDistance, false);
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeMergeNodesAndConnect(
    std::uint32_t keepNodeId, std::uint32_t removeNodeId, float maxDistance) const {
    return makeMergeNodesImpl(keepNodeId, removeNodeId, maxDistance, true);
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeMergeNodesImpl(
    std::uint32_t keepNodeId, std::uint32_t removeNodeId, float maxDistance, bool connectTurns) const {
    auto removedNode = network_->nodeResult(removeNodeId);
    if (!removedNode.ok()) return roadFailure<editing::DomainOperation>(removedNode.status());
    std::vector<procgen::road::RoadEdge> affectedEdges;
    for (const auto& edge : network_->edges())
        if (edge.from == removeNodeId || edge.to == removeNodeId) affectedEdges.push_back(edge);
    std::vector<procgen::road::RoadLaneConnection> affectedLinks;
    for (const auto& link : network_->laneLinks()) {
        const bool referencesAffectedEdge = std::any_of(affectedEdges.begin(), affectedEdges.end(), [&](const auto& edge) {
            return link.inEdge == edge.id || link.outEdge == edge.id;
        });
        if (referencesAffectedEdge) affectedLinks.push_back(link);
    }
    std::vector<procgen::road::RoadLaneConnection> affectedBlockedLinks;
    for (const auto& link : network_->blockedLaneLinks()) {
        const bool referencesAffectedEdge = std::any_of(affectedEdges.begin(), affectedEdges.end(), [&](const auto& edge) {
            return link.inEdge == edge.id || link.outEdge == edge.id;
        });
        if (referencesAffectedEdge) affectedBlockedLinks.push_back(link);
    }
    auto candidate = *network_;
    auto merged = candidate.mergeNodes(keepNodeId, removeNodeId, maxDistance);
    if (!merged.ok()) return roadFailure<editing::DomainOperation>(merged.status());
    if (connectTurns) {
        auto connected = candidate.connectAllTurns(keepNodeId);
        if (!connected.ok()) return roadFailure<editing::DomainOperation>(connected.status());
    }

    editing::DomainOperation operation;
    operation.type        = connectTurns ? kMergeNodesAndConnect : kMergeNodes;
    operation.inverseType = kUnmergeNodes;
    operation.target      = targetId();
    operation.payload     = encodeMergeNodes(keepNodeId, removeNodeId, maxDistance);
    operation.inverse = encodeUnmergeNodes(removedNode.value(), affectedEdges, affectedLinks, affectedBlockedLinks);
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(keepNodeId));
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(removeNodeId));
    for (const auto& edge : affectedEdges)
        operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edge.id));
    operation.affectedProperties.emplace_back("topology");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSetEdgeControlPoints(
    std::uint32_t edgeId, std::vector<procgen::road::RoadControlPoint> points) const {
    auto edge = network_->edgeResult(edgeId);
    if (!edge.ok())
        return reject<editing::DomainOperation>("editor.road.edge", "Road edge does not exist", EditorStatus::NotFound);
    auto candidate = *network_;
    auto valid     = candidate.setEdgeControlPoints(edgeId, points);
    if (!valid.ok()) return roadFailure<editing::DomainOperation>(valid.status());
    editing::DomainOperation operation;
    operation.type        = kSetEdgePoints;
    operation.inverseType = kSetEdgePoints;
    operation.target      = targetId();
    operation.payload     = encodeEdge(edgeId, points);
    operation.inverse     = encodeEdge(edgeId, edge.value().controlPoints);
    operation.hasInverse  = true;
    operation.mergeKey    = "road-edge:" + std::to_string(edgeId) + ":control-points";
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedProperties.emplace_back("controlPoints");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSetEdgeStyle(std::uint32_t            edgeId,
                                                                               procgen::road::RoadStyle style) const {
    auto edge = network_->edgeResult(edgeId);
    if (!edge.ok())
        return reject<editing::DomainOperation>("editor.road.edge", "Road edge does not exist", EditorStatus::NotFound);
    auto candidate = *network_;
    auto valid     = candidate.setEdgeStyle(edgeId, style);
    if (!valid.ok()) return roadFailure<editing::DomainOperation>(valid.status());
    editing::DomainOperation operation;
    operation.type        = kSetEdgeStyle;
    operation.inverseType = kSetEdgeStyle;
    operation.target      = targetId();
    operation.payload     = encodeStyle(edgeId, style);
    operation.inverse     = encodeStyle(edgeId, edge.value().style);
    operation.hasInverse  = true;
    operation.mergeKey    = "road-edge:" + std::to_string(edgeId) + ":style";
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedProperties.emplace_back("style");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSetEdgeLaneCounts(
    std::uint32_t edgeId, int lanesForward, int lanesBackward) const {
    auto edge = network_->edgeResult(edgeId);
    if (!edge.ok()) return roadFailure<editing::DomainOperation>(edge.status());
    std::vector<procgen::road::RoadLaneConnection> removedLinks;
    for (const auto& link : network_->laneLinks()) {
        const auto count = [&](procgen::road::RoadLaneDirection direction) {
            return direction == procgen::road::RoadLaneDirection::Forward ? lanesForward : lanesBackward;
        };
        if ((link.inEdge == edgeId && link.inLane >= count(link.inDirection)) ||
            (link.outEdge == edgeId && link.outLane >= count(link.outDirection)))
            removedLinks.push_back(link);
    }
    std::vector<procgen::road::RoadLaneConnection> removedBlockedLinks;
    for (const auto& link : network_->blockedLaneLinks()) {
        const auto count = [&](procgen::road::RoadLaneDirection direction) {
            return direction == procgen::road::RoadLaneDirection::Forward ? lanesForward : lanesBackward;
        };
        if ((link.inEdge == edgeId && link.inLane >= count(link.inDirection)) ||
            (link.outEdge == edgeId && link.outLane >= count(link.outDirection)))
            removedBlockedLinks.push_back(link);
    }
    auto candidate = *network_;
    auto changed   = candidate.setEdgeLaneCounts(edgeId, lanesForward, lanesBackward);
    if (!changed.ok()) return roadFailure<editing::DomainOperation>(changed.status());

    editing::DomainOperation operation;
    operation.type        = kSetEdgeLanes;
    operation.inverseType = kSetEdgeLanes;
    operation.target      = targetId();
    operation.payload = encodeLaneCounts(edgeId, lanesForward, lanesBackward, {}, {});
    operation.inverse = encodeLaneCounts(edgeId, edge.value().lanesForward, edge.value().lanesBackward, removedLinks,
                                         removedBlockedLinks);
    operation.hasInverse = true;
    operation.mergeKey   = "road-edge:" + std::to_string(edgeId) + ":lanes";
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedProperties.emplace_back("lanesForward");
    operation.affectedProperties.emplace_back("lanesBackward");
    operation.affectedProperties.emplace_back("laneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeReverseEdge(std::uint32_t edgeId) const {
    auto candidate = *network_;
    auto reversed = candidate.reverseEdge(edgeId);
    if (!reversed.ok()) return roadFailure<editing::DomainOperation>(reversed.status());
    editing::DomainOperation operation;
    operation.type        = kReverseEdge;
    operation.inverseType = kReverseEdge;
    operation.target      = targetId();
    operation.payload     = EditorValue::Object{{"id", std::int64_t{edgeId}}};
    operation.inverse     = operation.payload;
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedProperties.emplace_back("direction");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeReconnectEdgeEndpoint(
    std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId) const {
    auto edge = network_->edgeResult(edgeId);
    if (!edge.ok()) return roadFailure<editing::DomainOperation>(edge.status());
    const std::uint32_t oldNodeId = fromEndpoint ? edge.value().from : edge.value().to;
    if (oldNodeId == nodeId)
        return reject<editing::DomainOperation>("editor.road.reconnect-noop",
                                                "Road endpoint is already connected to that node",
                                                EditorStatus::NoOp);
    auto candidate = *network_;
    auto changed = candidate.reconnectEdgeEndpoint(edgeId, fromEndpoint, nodeId);
    if (!changed.ok()) return roadFailure<editing::DomainOperation>(changed.status());

    std::vector<procgen::road::RoadLaneConnection> removedLinks;
    std::vector<procgen::road::RoadLaneConnection> removedBlockedLinks;
    for (const auto& link : network_->laneLinks())
        if ((link.inEdge == edgeId || link.outEdge == edgeId) &&
            std::none_of(candidate.laneLinks().begin(), candidate.laneLinks().end(),
                         [&](const auto& remaining) { return sameLink(link, remaining); }))
            removedLinks.push_back(link);
    for (const auto& link : network_->blockedLaneLinks())
        if ((link.inEdge == edgeId || link.outEdge == edgeId) &&
            std::none_of(candidate.blockedLaneLinks().begin(), candidate.blockedLaneLinks().end(),
                         [&](const auto& remaining) { return sameLink(link, remaining); }))
            removedBlockedLinks.push_back(link);

    editing::DomainOperation operation;
    operation.type        = kReconnectEdgeEndpoint;
    operation.inverseType = kReconnectEdgeEndpoint;
    operation.target      = targetId();
    operation.payload     = encodeReconnectEndpoint(edgeId, fromEndpoint, nodeId, {}, {});
    operation.inverse = encodeReconnectEndpoint(edgeId, fromEndpoint, oldNodeId, removedLinks,
                                                 removedBlockedLinks);
    operation.hasInverse = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedProperties.emplace_back(fromEndpoint ? "from" : "to");
    operation.affectedProperties.emplace_back("controlPoints");
    operation.affectedProperties.emplace_back("laneLinks");
    operation.affectedProperties.emplace_back("blockedLaneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeReconnectEdgeEndpointAndConnect(
    std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId) const {
    auto planned = makeReconnectEdgeEndpoint(edgeId, fromEndpoint, nodeId);
    if (!planned.ok()) return planned;
    auto candidate = *network_;
    auto changed = candidate.reconnectEdgeEndpoint(edgeId, fromEndpoint, nodeId);
    if (!changed.ok()) return roadFailure<editing::DomainOperation>(changed.status());
    auto connected = candidate.connectAllTurns(nodeId);
    if (!connected.ok()) return roadFailure<editing::DomainOperation>(connected.status());

    std::vector<procgen::road::RoadLaneConnection> added;
    for (const auto& link : candidate.laneLinks()) {
        if (link.inEdge != edgeId && link.outEdge != edgeId) continue;
        if (std::none_of(network_->laneLinks().begin(), network_->laneLinks().end(),
                         [&](const auto& original) { return sameLink(link, original); }))
            added.push_back(link);
    }
    planned.value().payload = encodeReconnectEndpoint(edgeId, fromEndpoint, nodeId, added, {});
    return planned;
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSnapEdgeEndpoint(
    std::uint32_t edgeId, bool fromEndpoint, float maxDistance, bool connectTurns) const {
    if (!std::isfinite(maxDistance) || maxDistance < 0.f)
        return reject<editing::DomainOperation>("editor.road.snap-distance",
                                                "Road endpoint snap distance must be finite and non-negative");
    auto edge = network_->edgeResult(edgeId);
    if (!edge.ok()) return roadFailure<editing::DomainOperation>(edge.status());
    const std::uint32_t oldNodeId = fromEndpoint ? edge.value().from : edge.value().to;
    const std::uint32_t otherNodeId = fromEndpoint ? edge.value().to : edge.value().from;
    auto endpoint = network_->nodeResult(oldNodeId);
    if (!endpoint.ok()) return roadFailure<editing::DomainOperation>(endpoint.status());

    const float maximumSquared = maxDistance * maxDistance;
    float bestSquared = std::numeric_limits<float>::max();
    std::uint32_t bestNodeId = 0;
    for (const auto& node : network_->nodes()) {
        if (node.id == oldNodeId || node.id == otherNodeId) continue;
        const float dx = node.x - endpoint.value().x;
        const float dy = node.y - endpoint.value().y;
        const float dz = node.z - endpoint.value().z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared > maximumSquared) continue;
        if (distanceSquared < bestSquared || (distanceSquared == bestSquared && node.id < bestNodeId)) {
            bestSquared = distanceSquared;
            bestNodeId = node.id;
        }
    }
    if (bestNodeId == 0)
        return reject<editing::DomainOperation>("editor.road.snap-not-found",
                                                "No distinct road node is inside the endpoint snap distance",
                                                EditorStatus::NotFound);
    return connectTurns ? makeReconnectEdgeEndpointAndConnect(edgeId, fromEndpoint, bestNodeId)
                        : makeReconnectEdgeEndpoint(edgeId, fromEndpoint, bestNodeId);
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeDetachEdgeEndpoint(
    std::uint32_t edgeId, bool fromEndpoint) const {
    auto edge = network_->edgeResult(edgeId);
    if (!edge.ok()) return roadFailure<editing::DomainOperation>(edge.status());
    const std::uint32_t oldNodeId = fromEndpoint ? edge.value().from : edge.value().to;
    auto candidate = *network_;
    auto detached = candidate.detachEdgeEndpoint(edgeId, fromEndpoint);
    if (!detached.ok()) return roadFailure<editing::DomainOperation>(detached.status());

    std::vector<procgen::road::RoadLaneConnection> removedLinks;
    std::vector<procgen::road::RoadLaneConnection> removedBlockedLinks;
    for (const auto& link : network_->laneLinks())
        if ((link.inEdge == edgeId || link.outEdge == edgeId) &&
            std::none_of(candidate.laneLinks().begin(), candidate.laneLinks().end(),
                         [&](const auto& remaining) { return sameLink(link, remaining); }))
            removedLinks.push_back(link);
    for (const auto& link : network_->blockedLaneLinks())
        if ((link.inEdge == edgeId || link.outEdge == edgeId) &&
            std::none_of(candidate.blockedLaneLinks().begin(), candidate.blockedLaneLinks().end(),
                         [&](const auto& remaining) { return sameLink(link, remaining); }))
            removedBlockedLinks.push_back(link);

    editing::DomainOperation operation;
    operation.type        = kDetachEdgeEndpoint;
    operation.inverseType = kReattachEdgeEndpoint;
    operation.target      = targetId();
    operation.payload     = encodeDetachEndpoint(edgeId, fromEndpoint, detached.value());
    operation.inverse = encodeReattachEndpoint(edgeId, fromEndpoint, oldNodeId, detached.value(), removedLinks,
                                                removedBlockedLinks);
    operation.hasInverse = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(detached.value()));
    operation.affectedProperties.emplace_back(fromEndpoint ? "from" : "to");
    operation.affectedProperties.emplace_back("laneLinks");
    operation.affectedProperties.emplace_back("blockedLaneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSplitEdge(std::uint32_t edgeId,
                                                                            std::size_t controlPointIndex,
                                                                            float junctionRadius) const {
    auto original = network_->edgeResult(edgeId);
    if (!original.ok()) return roadFailure<editing::DomainOperation>(original.status());
    std::vector<procgen::road::RoadLaneConnection> originalLinks;
    for (const auto& link : network_->laneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) originalLinks.push_back(link);
    std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
    for (const auto& link : network_->blockedLaneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) originalBlockedLinks.push_back(link);
    auto candidate = *network_;
    auto split     = candidate.splitEdge(edgeId, controlPointIndex, junctionRadius);
    if (!split.ok()) return roadFailure<editing::DomainOperation>(split.status());

    editing::DomainOperation operation;
    operation.type        = kSplitEdge;
    operation.inverseType = kUnsplitEdge;
    operation.target      = targetId();
    operation.payload     = encodeSplit(edgeId, controlPointIndex, junctionRadius, split.value());
    operation.inverse = encodeUnsplit(original.value(), originalLinks, originalBlockedLinks, split.value().nodeId,
                                      split.value().secondEdgeId);
    operation.hasInverse = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(split.value().secondEdgeId));
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(split.value().nodeId));
    operation.affectedProperties.emplace_back("topology");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSplitEdgeAtPosition(
    std::uint32_t edgeId, procgen::road::RoadControlPoint position, float maxDistance, float junctionRadius) const {
    auto original = network_->edgeResult(edgeId);
    if (!original.ok()) return roadFailure<editing::DomainOperation>(original.status());
    std::vector<procgen::road::RoadLaneConnection> originalLinks;
    for (const auto& link : network_->laneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) originalLinks.push_back(link);
    std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
    for (const auto& link : network_->blockedLaneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) originalBlockedLinks.push_back(link);
    auto candidate = *network_;
    auto split     = candidate.splitEdgeAtPosition(edgeId, position, maxDistance, junctionRadius);
    if (!split.ok()) return roadFailure<editing::DomainOperation>(split.status());

    editing::DomainOperation operation;
    operation.type        = kSplitEdgeAtPosition;
    operation.inverseType = kUnsplitEdge;
    operation.target      = targetId();
    operation.payload     = encodePositionSplit(edgeId, position, maxDistance, junctionRadius, split.value());
    operation.inverse = encodeUnsplit(original.value(), originalLinks, originalBlockedLinks, split.value().nodeId,
                                      split.value().secondEdgeId);
    operation.hasInverse = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(split.value().secondEdgeId));
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(split.value().nodeId));
    operation.affectedProperties.emplace_back("topology");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeConnectNodeAtPosition(
    std::uint32_t sourceNodeId, std::uint32_t edgeId, procgen::road::RoadControlPoint position,
    float maxDistance, float junctionRadius, int lanesForward, int lanesBackward,
    procgen::road::RoadStyle style) const {
    auto source = network_->nodeResult(sourceNodeId);
    if (!source.ok()) return roadFailure<editing::DomainOperation>(source.status());
    auto original = network_->edgeResult(edgeId);
    if (!original.ok()) return roadFailure<editing::DomainOperation>(original.status());
    if (original.value().from == sourceNodeId || original.value().to == sourceNodeId)
        return reject<editing::DomainOperation>("editor.road.connect-existing-endpoint",
                                                "Branch source is already an endpoint of the selected road");
    std::vector<procgen::road::RoadLaneConnection> originalLinks;
    for (const auto& link : network_->laneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) originalLinks.push_back(link);
    std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
    for (const auto& link : network_->blockedLaneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) originalBlockedLinks.push_back(link);

    auto candidate = *network_;
    auto split = candidate.splitEdgeAtPosition(edgeId, position, maxDistance, junctionRadius);
    if (!split.ok()) return roadFailure<editing::DomainOperation>(split.status());
    auto inserted = candidate.nodeResult(split.value().nodeId);
    if (!inserted.ok()) return roadFailure<editing::DomainOperation>(inserted.status());
    std::vector<procgen::road::RoadControlPoint> branchPoints{
        {source.value().x, source.value().y, source.value().z},
        {inserted.value().x, inserted.value().y, inserted.value().z}};
    auto branchId = candidate.addEdge(sourceNodeId, split.value().nodeId, std::move(branchPoints), lanesForward,
                                      lanesBackward, style);
    if (!branchId.ok()) return roadFailure<editing::DomainOperation>(branchId.status());
    auto connected = candidate.connectAllTurns(split.value().nodeId);
    if (!connected.ok()) return roadFailure<editing::DomainOperation>(connected.status());
    auto branch = candidate.edgeResult(branchId.value());
    if (!branch.ok()) return roadFailure<editing::DomainOperation>(branch.status());

    editing::DomainOperation operation;
    operation.type        = kConnectNodeAtPosition;
    operation.inverseType = kDisconnectNodeAtPosition;
    operation.target      = targetId();
    operation.payload = encodeConnectAtPosition(edgeId, position, maxDistance, junctionRadius, split.value(),
                                                branch.value());
    operation.inverse = encodeDisconnectAtPosition(original.value(), originalLinks, originalBlockedLinks,
                                                   split.value().nodeId, split.value().secondEdgeId,
                                                   branch.value().id);
    operation.hasInverse = true;
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(sourceNodeId));
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(split.value().nodeId));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(split.value().secondEdgeId));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(branch.value().id));
    operation.affectedProperties.emplace_back("topology");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeConnectEdgeIntersection(
    std::uint32_t firstEdgeId, std::uint32_t secondEdgeId, float maximumHeightDelta,
    float junctionRadius) const {
    if (firstEdgeId == secondEdgeId)
        return reject<editing::DomainOperation>("editor.road.intersection-same-edge",
                                                "Road intersection requires two distinct edges");
    if (!std::isfinite(maximumHeightDelta) || maximumHeightDelta < 0.f)
        return reject<editing::DomainOperation>("editor.road.intersection-height",
                                                "Road intersection height tolerance is invalid");
    auto first = network_->edgeResult(firstEdgeId);
    auto second = network_->edgeResult(secondEdgeId);
    if (!first.ok()) return roadFailure<editing::DomainOperation>(first.status());
    if (!second.ok()) return roadFailure<editing::DomainOperation>(second.status());
    auto intersection = findUniqueInteriorIntersection(first.value(), second.value(), maximumHeightDelta);
    if (!intersection.ok()) return roadFailure<editing::DomainOperation>(intersection.status());
    if (intersection.value().count == 0)
        return reject<editing::DomainOperation>("editor.road.intersection-missing",
                                                "Road edges have no eligible same-level interior intersection");
    if (intersection.value().count > 1)
        return reject<editing::DomainOperation>("editor.road.intersection-ambiguous",
                                                "Road edges intersect more than once; select a specific split instead");

    std::vector<procgen::road::RoadLaneConnection> originalLinks;
    for (const auto& link : network_->laneLinks())
        if (link.inEdge == firstEdgeId || link.outEdge == firstEdgeId || link.inEdge == secondEdgeId ||
            link.outEdge == secondEdgeId)
            originalLinks.push_back(link);
    std::vector<procgen::road::RoadLaneConnection> originalBlockedLinks;
    for (const auto& link : network_->blockedLaneLinks())
        if (link.inEdge == firstEdgeId || link.outEdge == firstEdgeId || link.inEdge == secondEdgeId ||
            link.outEdge == secondEdgeId)
            originalBlockedLinks.push_back(link);
    auto candidate = *network_;
    auto firstSplit = candidate.splitEdgeAtSplineParameter(firstEdgeId, intersection.value().firstParameter,
                                                           junctionRadius);
    if (!firstSplit.ok()) return roadFailure<editing::DomainOperation>(firstSplit.status());
    auto secondSplit = candidate.splitEdgeAtSplineParameter(secondEdgeId, intersection.value().secondParameter,
                                                            junctionRadius);
    if (!secondSplit.ok()) return roadFailure<editing::DomainOperation>(secondSplit.status());
    const auto mergedPoint = intersection.value().mergedPoint;
    auto moved = candidate.setNodePosition(firstSplit.value().nodeId, mergedPoint.x, mergedPoint.y, mergedPoint.z);
    if (!moved.ok()) return roadFailure<editing::DomainOperation>(moved.status());
    moved = candidate.setNodePosition(secondSplit.value().nodeId, mergedPoint.x, mergedPoint.y, mergedPoint.z);
    if (!moved.ok()) return roadFailure<editing::DomainOperation>(moved.status());
    auto merged = candidate.mergeNodes(firstSplit.value().nodeId, secondSplit.value().nodeId, 1e-3f);
    if (!merged.ok()) return roadFailure<editing::DomainOperation>(merged.status());
    auto connected = candidate.connectAllTurns(firstSplit.value().nodeId);
    if (!connected.ok()) return roadFailure<editing::DomainOperation>(connected.status());

    editing::DomainOperation operation;
    operation.type        = kConnectEdgeIntersection;
    operation.inverseType = kDisconnectEdgeIntersection;
    operation.target      = targetId();
    operation.payload = encodeIntersectionConnect(firstEdgeId, secondEdgeId, intersection.value().firstParameter,
                                                  intersection.value().secondParameter, mergedPoint,
                                                  maximumHeightDelta, junctionRadius, firstSplit.value(),
                                                  secondSplit.value());
    operation.inverse = encodeIntersectionDisconnect(first.value(), second.value(), originalLinks,
                                                     originalBlockedLinks,
                                                     firstSplit.value().nodeId, firstSplit.value().secondEdgeId,
                                                     secondSplit.value().secondEdgeId);
    operation.hasInverse = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(firstEdgeId));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(secondEdgeId));
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(firstSplit.value().nodeId));
    operation.affectedProperties.emplace_back("topology");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeAddNode(float x, float y, float z,
                                                                          float junctionRadius) const {
    auto candidate = *network_;
    auto added     = candidate.addNode(x, y, z, junctionRadius);
    if (!added.ok()) return roadFailure<editing::DomainOperation>(added.status());
    auto node = candidate.nodeResult(added.value());
    if (!node.ok()) return roadFailure<editing::DomainOperation>(node.status());
    editing::DomainOperation operation;
    operation.type        = kRestoreNode;
    operation.inverseType = kRemoveNode;
    operation.target      = targetId();
    operation.payload     = encodeNodeRecord(node.value());
    operation.inverse     = EditorValue::Object{{"id", std::int64_t{node.value().id}}};
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(node.value().id));
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeRemoveNode(std::uint32_t nodeId) const {
    auto node = network_->nodeResult(nodeId);
    if (!node.ok()) return roadFailure<editing::DomainOperation>(node.status());
    auto candidate = *network_;
    auto removed   = candidate.removeNode(nodeId);
    if (!removed.ok()) return roadFailure<editing::DomainOperation>(removed.status());
    editing::DomainOperation operation;
    operation.type        = kRemoveNode;
    operation.inverseType = kRestoreNode;
    operation.target      = targetId();
    operation.payload     = EditorValue::Object{{"id", std::int64_t{nodeId}}};
    operation.inverse     = encodeNodeRecord(node.value());
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(nodeId));
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeAddEdge(
    std::uint32_t from, std::uint32_t to, std::vector<procgen::road::RoadControlPoint> points, int lanesForward,
    int lanesBackward, procgen::road::RoadStyle style) const {
    auto candidate = *network_;
    auto added     = candidate.addEdge(from, to, std::move(points), lanesForward, lanesBackward, style);
    if (!added.ok()) return roadFailure<editing::DomainOperation>(added.status());
    auto edge = candidate.edgeResult(added.value());
    if (!edge.ok()) return roadFailure<editing::DomainOperation>(edge.status());
    editing::DomainOperation operation;
    operation.type        = kRestoreEdge;
    operation.inverseType = kRemoveEdge;
    operation.target      = targetId();
    operation.payload     = encodeEdgeRecord(edge.value(), {});
    operation.inverse     = EditorValue::Object{{"id", std::int64_t{edge.value().id}}};
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edge.value().id));
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeAddEdgeAndConnect(
    std::uint32_t from, std::uint32_t to, std::vector<procgen::road::RoadControlPoint> points, int lanesForward,
    int lanesBackward, procgen::road::RoadStyle style) const {
    auto planned = makeAddEdge(from, to, std::move(points), lanesForward, lanesBackward, style);
    if (!planned.ok()) return planned;

    auto operation = std::move(planned).takeValue();
    procgen::road::RoadEdge edge;
    std::vector<procgen::road::RoadLaneConnection> ignored;
    if (!decodeEdgeRecord(operation.payload, edge, ignored))
        return reject<editing::DomainOperation>("editor.road.edge-connect-plan",
                                                "Generated road edge payload is invalid", EditorStatus::Failed);

    auto candidate = *network_;
    auto restored = candidate.restoreEdge(edge);
    if (!restored.ok()) return roadFailure<editing::DomainOperation>(restored.status());
    for (const auto nodeId : {from, to}) {
        auto connected = candidate.connectAllTurns(nodeId);
        if (!connected.ok()) return roadFailure<editing::DomainOperation>(connected.status());
    }

    std::vector<procgen::road::RoadLaneConnection> edgeLinks;
    for (const auto& link : candidate.laneLinks())
        if (link.inEdge == edge.id || link.outEdge == edge.id) edgeLinks.push_back(link);
    operation.payload = encodeEdgeRecord(edge, edgeLinks);
    operation.affectedProperties.emplace_back("laneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeRemoveEdge(std::uint32_t edgeId) const {
    auto edge = network_->edgeResult(edgeId);
    if (!edge.ok()) return roadFailure<editing::DomainOperation>(edge.status());
    std::vector<procgen::road::RoadLaneConnection> links;
    for (const auto& link : network_->laneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) links.push_back(link);
    std::vector<procgen::road::RoadLaneConnection> blockedLinks;
    for (const auto& link : network_->blockedLaneLinks())
        if (link.inEdge == edgeId || link.outEdge == edgeId) blockedLinks.push_back(link);
    editing::DomainOperation operation;
    operation.type        = kRemoveEdge;
    operation.inverseType = kRestoreEdge;
    operation.target      = targetId();
    operation.payload     = EditorValue::Object{{"id", std::int64_t{edgeId}}};
    operation.inverse     = encodeEdgeRecord(edge.value(), links, blockedLinks);
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(edgeId));
    for (const auto& link : links) {
        operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.inEdge));
        operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.outEdge));
    }
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeAddLaneLink(
    procgen::road::RoadLaneConnection link) const {
    auto candidate = *network_;
    auto added     = candidate.addLaneLink(link);
    if (!added.ok()) return roadFailure<editing::DomainOperation>(added.status());
    editing::DomainOperation operation;
    operation.type        = kRestoreLaneLink;
    operation.inverseType = kRemoveLaneLink;
    operation.target      = targetId();
    operation.payload     = encodeLink(link);
    operation.inverse     = encodeLink(link);
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.inEdge));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.outEdge));
    operation.affectedProperties.emplace_back("laneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeRemoveLaneLink(
    const procgen::road::RoadLaneConnection& link) const {
    auto candidate = *network_;
    auto removed   = candidate.removeLaneLink(link);
    if (!removed.ok()) return roadFailure<editing::DomainOperation>(removed.status());
    editing::DomainOperation operation;
    operation.type        = kRemoveLaneLink;
    operation.inverseType = kRestoreLaneLink;
    operation.target      = targetId();
    operation.payload     = encodeLink(link);
    operation.inverse     = encodeLink(link);
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.inEdge));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.outEdge));
    operation.affectedProperties.emplace_back("laneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeBlockLaneLink(
    procgen::road::RoadLaneConnection link) const {
    auto candidate = *network_;
    auto blocked = candidate.blockLaneLink(link);
    if (!blocked.ok()) return roadFailure<editing::DomainOperation>(blocked.status());
    editing::DomainOperation operation;
    operation.type        = kBlockLaneLink;
    operation.inverseType = kUnblockLaneLink;
    operation.target      = targetId();
    operation.payload     = encodeBlockedLink(link, false);
    operation.inverse     = encodeBlockedLink(link, blocked.value());
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.inEdge));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.outEdge));
    operation.affectedProperties.emplace_back("blockedLaneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeUnblockLaneLink(
    const procgen::road::RoadLaneConnection& link) const {
    auto candidate = *network_;
    auto unblocked = candidate.unblockLaneLink(link);
    if (!unblocked.ok()) return roadFailure<editing::DomainOperation>(unblocked.status());
    editing::DomainOperation operation;
    operation.type        = kUnblockLaneLink;
    operation.inverseType = kBlockLaneLink;
    operation.target      = targetId();
    operation.payload     = encodeBlockedLink(link, false);
    operation.inverse     = encodeBlockedLink(link, false);
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.inEdge));
    operation.affectedObjects.emplace_back(targetId(), "edge/" + std::to_string(link.outEdge));
    operation.affectedProperties.emplace_back("blockedLaneLinks");
    return editing::applied(std::move(operation));
}

EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeConnectAllTurns(std::uint32_t nodeId) const {
    auto candidate = *network_;
    auto connected = candidate.connectAllTurns(nodeId);
    if (!connected.ok()) return roadFailure<editing::DomainOperation>(connected.status());
    if (connected.value() == 0)
        return reject<editing::DomainOperation>("editor.road.turns-noop", "Road node has no missing turn links",
                                                EditorStatus::NoOp);

    std::vector<procgen::road::RoadLaneConnection> added;
    added.reserve(static_cast<std::size_t>(connected.value()));
    for (const auto& link : candidate.laneLinks()) {
        const bool existed = std::any_of(network_->laneLinks().begin(), network_->laneLinks().end(),
                                         [&](const auto& current) { return sameLink(current, link); });
        if (!existed) added.push_back(link);
    }
    editing::DomainOperation operation;
    operation.type        = kRestoreLaneLinks;
    operation.inverseType = kRemoveLaneLinks;
    operation.target      = targetId();
    operation.payload     = encodeLinks(added);
    operation.inverse     = encodeLinks(added);
    operation.hasInverse  = true;
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(nodeId));
    operation.affectedProperties.emplace_back("laneLinks");
    return editing::applied(std::move(operation));
}

}  // namespace eve::procgen_editing
