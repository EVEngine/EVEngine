#pragma once

#include "procgen/road/RoadTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen::road {

/**
 * @brief Owning directed road graph with lane connectivity.
 *
 * Mutations are synchronous and owner-thread only. Revision bumps on every
 * successful structural change. Handles/ids are stable until erase.
 */
class EVENGINE_API_DOMAINS RoadNetwork {
public:
    /** @brief Insert a junction node after validating finite coordinates. */
    [[nodiscard]] Result<std::uint32_t> addNode(float x, float y, float z, float junctionRadius = 6.f);
    /**
     * @brief Restore a node with an explicit stable id for undo/import paths.
     * @param node Complete node value with non-zero unused id.
     * @return Success or a structured validation/conflict diagnostic.
     */
    [[nodiscard]] Result<void> restoreNode(RoadNode node);
    /**
     * @brief Insert a non-degenerate directed edge between two distinct live nodes.
     * @return Stable edge id or a structured diagnostic; failure leaves the network unchanged.
     */
    [[nodiscard]] Result<std::uint32_t> addEdge(std::uint32_t from, std::uint32_t to,
                                                std::vector<RoadControlPoint> controlPoints, int lanesForward = 2,
                                                int lanesBackward = 0, RoadStyle style = {});
    /**
     * @brief Restore an edge with an explicit stable id for undo/import paths.
     * @param edge Complete non-degenerate edge value referencing two distinct live endpoint nodes.
     * @return Success or a structured validation/conflict diagnostic.
     */
    [[nodiscard]] Result<void> restoreEdge(RoadEdge edge);
    /** @brief Register one legal turn inside a shared junction node. */
    [[nodiscard]] Result<void> addLaneLink(RoadLaneConnection link);
    /**
     * @brief Remove one exact lane-to-lane connection.
     * @param link Composite connection identity to remove.
     * @return Success or NotFound; failure leaves the network unchanged.
     */
    [[nodiscard]] Result<void> removeLaneLink(const RoadLaneConnection& link);
    /**
     * @brief Persistently forbid one exact legal lane connection and remove it if currently active.
     * @param link Exact directed lane route to suppress during automatic connection.
     * @return Whether an active link was removed; failure leaves the network unchanged.
     */
    [[nodiscard]] Result<bool> blockLaneLink(RoadLaneConnection link);
    /** @brief Remove one exact persistent lane-link prohibition. */
    [[nodiscard]] Result<void> unblockLaneLink(const RoadLaneConnection& link);
    /**
     * @brief Auto-connect all forward and backward lane ports at one node.
     *
     * Immediate U-turns onto the same physical edge are omitted. Repeated calls
     * are idempotent and return the number of newly-created links. Different
     * lane counts preserve normalized lateral rank; an existing authored link
     * overrides automatic mapping for the same incoming lane/outgoing route.
     * @return Number of newly-created links; failure leaves the network unchanged.
     * @note All links for the node commit atomically as one network revision.
     * @cost Quadratic in incident edge count times lane count; intended for authoring and topology updates.
     */
    [[nodiscard]] Result<int> connectAllTurns(std::uint32_t nodeId);
    /**
     * @brief Validate graph, endpoint and lane-link invariants without mutation.
     * @return Success or the first structured topology diagnostic.
     * @note Synchronous and owner-thread-only; no callbacks are invoked.
     */
    [[nodiscard]] Result<void> validate() const;
    /**
     * @brief Move a node and atomically re-anchor every incident edge endpoint.
     * @param nodeId Stable node identity.
     * @param x New finite world X coordinate.
     * @param y New finite world Y coordinate.
     * @param z New finite world Z coordinate.
     * @return Success, NotFound, or a geometry diagnostic; failure leaves the network unchanged.
     */
    [[nodiscard]] Result<void> setNodePosition(std::uint32_t nodeId, float x, float y, float z);
    /**
     * @brief Replace one node's positive junction trim radius.
     * @param nodeId Stable node identity.
     * @param junctionRadius Finite positive radius used by junction and edge trimming.
     * @return Success or a structured diagnostic; failure leaves the network unchanged.
     */
    [[nodiscard]] Result<void> setNodeJunctionRadius(std::uint32_t nodeId, float junctionRadius);
    /**
     * @brief Replace one node's authoritative traffic-control policy.
     * @param nodeId Stable node identity.
     * @param control Valid junction control enum stored with the node and its snapshots.
     * @return Success or a structured diagnostic; failure leaves the network unchanged.
     */
    [[nodiscard]] Result<void> setNodeJunctionControl(std::uint32_t nodeId, RoadJunctionControl control);
    /**
     * @brief Atomically merge one nearby endpoint node into another stable node.
     * @param keepNodeId Node identity and position retained by the merged junction.
     * @param removeNodeId Distinct node whose incident edge endpoints are rewired.
     * @param maxDistance Maximum allowed 3D distance between the two nodes.
     * @return Number of rewired incident edge endpoints.
     * @note A direct edge between the nodes is rejected because merging it would create a self-loop.
     * @cost Linear in road topology and lane-link count; intended for authoring operations.
     */
    [[nodiscard]] Result<int> mergeNodes(std::uint32_t keepNodeId, std::uint32_t removeNodeId,
                                         float maxDistance);
    /**
     * @brief Replace one edge spline while preserving its node-owned endpoint positions.
     * @param edgeId Stable edge identity.
     * @param controlPoints Owning finite control-point sequence containing at least one non-degenerate segment.
     * @return Success or a structured validation diagnostic; failure leaves the edge unchanged.
     */
    [[nodiscard]] Result<void> setEdgeControlPoints(std::uint32_t edgeId, std::vector<RoadControlPoint> controlPoints);
    /**
     * @brief Atomically replace directional lane counts and remove links that become out of range.
     * @param edgeId Stable edge identity.
     * @param lanesForward Forward lane count.
     * @param lanesBackward Reverse lane count.
     * @return Number of removed stale lane links, or a structured failure with no mutation.
     */
    [[nodiscard]] Result<int> setEdgeLaneCounts(std::uint32_t edgeId, int lanesForward, int lanesBackward);
    /**
     * @brief Atomically reverse one edge while preserving its physical lanes, links, and side-object intervals.
     * @param edgeId Stable edge identity retained after reversal.
     * @return Success or NotFound; failure leaves the network unchanged.
     * @note Reverses control points, swaps endpoints/directional lane counts/start-end offsets/left-right flags,
     * and flips every lane-link direction referencing the edge. Applying twice restores the original value.
     * @cost Linear in edge control-point and network lane-link counts; intended for authoring operations.
     */
    [[nodiscard]] Result<void> reverseEdge(std::uint32_t edgeId);
    /**
     * @brief Reconnect one edge endpoint to an existing node and prune connections left at the old junction.
     * @param edgeId Stable edge identity retained by the road.
     * @param fromEndpoint True to reconnect `from`, false to reconnect `to`.
     * @param nodeId Existing replacement endpoint node.
     * @return Number of active and blocked lane connections pruned, or a structured failure with no mutation.
     * @cost Linear in network lane-link count; intended for authoring operations.
     */
    [[nodiscard]] Result<int> reconnectEdgeEndpoint(std::uint32_t edgeId, bool fromEndpoint,
                                                    std::uint32_t nodeId);
    /**
     * @brief Detach one edge endpoint onto a new coincident stable node.
     * @param edgeId Stable edge identity retained by the road.
     * @param fromEndpoint True to detach `from`, false to detach `to`.
     * @return New node identity, or a structured failure with no mutation.
     * @note The selected endpoint must currently be shared by at least one other edge.
     * @cost Linear in network lane-link count; intended for authoring operations.
     */
    [[nodiscard]] Result<std::uint32_t> detachEdgeEndpoint(std::uint32_t edgeId, bool fromEndpoint);
    /** @brief Replay endpoint detachment using an editor-reserved node identity. */
    [[nodiscard]] Result<std::uint32_t> detachEdgeEndpoint(std::uint32_t edgeId, bool fromEndpoint,
                                                           std::uint32_t nodeId);
    /**
     * @brief Split an edge at one authored interior control point as a single topology mutation.
     * @param edgeId Stable edge identity retained by the first half.
     * @param controlPointIndex Interior control-point index in [1, size-2].
     * @param junctionRadius Positive radius for the inserted node.
     * @return Inserted node and both edge identities; failure leaves the network unchanged.
     * @note Existing links at the old destination migrate to the continuation edge; same-direction
     * lane links are inserted between the two halves.
     */
    [[nodiscard]] Result<RoadEdgeSplitResult> splitEdge(std::uint32_t edgeId, std::size_t controlPointIndex,
                                                        float junctionRadius = 6.f);

    /**
     * @brief Split an edge while restoring identities reserved by an editor transaction.
     * @param edgeId Existing edge retained by the first half.
     * @param controlPointIndex Interior authored control point used by the split.
     * @param junctionRadius Radius assigned to the inserted node.
     * @param nodeId Reserved non-zero identity for the inserted node.
     * @param secondEdgeId Reserved non-zero identity for the continuation edge.
     * @return Stable identities for the inserted node and both edge halves.
     * @cost Linear in road topology and lane-link count; intended for authoring transactions.
     */
    [[nodiscard]] Result<RoadEdgeSplitResult> splitEdge(std::uint32_t edgeId, std::size_t controlPointIndex,
                                                        float junctionRadius, std::uint32_t nodeId,
                                                        std::uint32_t secondEdgeId);
    /**
     * @brief Project a world position onto the baked Catmull-Rom centerline and split at the nearest point.
     * @param edgeId Existing edge retained by the first half.
     * @param position Finite world-space position used for nearest-point projection.
     * @param maxDistance Maximum 3D snap distance from position to centerline.
     * @param junctionRadius Positive radius assigned to the inserted node.
     * @return Stable identities for the inserted node and both edge halves.
     * @cost Linear in at most 4096 sampled spline intervals plus topology and lane-link count.
     */
    [[nodiscard]] Result<RoadEdgeSplitResult> splitEdgeAtPosition(std::uint32_t edgeId, RoadControlPoint position,
                                                                  float maxDistance, float junctionRadius = 6.f);
    /** @brief Replay a position split using explicit editor-reserved identities. */
    [[nodiscard]] Result<RoadEdgeSplitResult> splitEdgeAtPosition(std::uint32_t edgeId, RoadControlPoint position,
                                                                  float maxDistance, float junctionRadius,
                                                                  std::uint32_t nodeId,
                                                                  std::uint32_t secondEdgeId);
    /**
     * @brief Split at a normalized parameter on the same Catmull-Rom centerline used by road baking.
     * @param edgeId Existing edge retained by the first half.
     * @param parameter Normalized open interval (0,1) along spline segment parameterization.
     * @param junctionRadius Positive radius assigned to the inserted node.
     * @param nodeId Optional reserved node identity, or zero for automatic allocation.
     * @param secondEdgeId Optional reserved continuation edge identity, or zero for automatic allocation.
     * @return Stable split identities; failure leaves the network unchanged.
     * @note An evaluated point is inserted only when the parameter is not already an authored control point.
     * @cost Linear in edge control points plus topology and lane-link count; authoring only.
     */
    [[nodiscard]] Result<RoadEdgeSplitResult> splitEdgeAtSplineParameter(std::uint32_t edgeId, float parameter,
                                                                         float junctionRadius = 6.f,
                                                                         std::uint32_t nodeId = 0,
                                                                         std::uint32_t secondEdgeId = 0);
    /** @brief Replace edge style atomically. */
    [[nodiscard]] Result<void> setEdgeStyle(std::uint32_t edgeId, RoadStyle style);
    /**
     * @brief Remove an edge and every lane link that references it.
     * @param edgeId Stable edge identity.
     * @return Success or NotFound; failure leaves the network unchanged.
     */
    [[nodiscard]] Result<void> removeEdge(std::uint32_t edgeId);
    /**
     * @brief Remove an isolated node.
     * @param nodeId Stable node identity with no incident edges.
     * @return Success, NotFound, or PreconditionViolation without partial mutation.
     */
    [[nodiscard]] Result<void> removeNode(std::uint32_t nodeId);
    /** @brief Clear every node, edge and link. */
    void clear();

    /** @brief Node count. */
    [[nodiscard]] int nodeCount() const noexcept { return static_cast<int>(nodes_.size()); }
    /** @brief Edge count. */
    [[nodiscard]] int edgeCount() const noexcept { return static_cast<int>(edges_.size()); }
    /** @brief Lane link count. */
    [[nodiscard]] int laneLinkCount() const noexcept { return static_cast<int>(laneLinks_.size()); }
    /** @brief Return persistent exact turn prohibitions owned by this network. */
    [[nodiscard]] int blockedLaneLinkCount() const noexcept { return static_cast<int>(blockedLaneLinks_.size()); }
    /** @brief Revision. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    /** @brief Node result. */
    [[nodiscard]] Result<RoadNode> nodeResult(std::uint32_t id) const;
    /** @brief Edge result. */
    [[nodiscard]] Result<RoadEdge> edgeResult(std::uint32_t id) const;
    /** @brief Nodes. */
    [[nodiscard]] const std::vector<RoadNode>& nodes() const noexcept { return nodes_; }
    /** @brief Edges. */
    [[nodiscard]] const std::vector<RoadEdge>& edges() const noexcept { return edges_; }
    /** @brief Lane links. */
    [[nodiscard]] const std::vector<RoadLaneConnection>& laneLinks() const noexcept { return laneLinks_; }
    /** @brief Borrow deterministic persistent turn prohibitions until the next network mutation. */
    [[nodiscard]] const std::vector<RoadLaneConnection>& blockedLaneLinks() const noexcept {
        return blockedLaneLinks_;
    }

    /** @brief Build a multi-level interchange demo graph (ground cross + elevated loop + ramps). */
    [[nodiscard]] static Result<RoadNetwork> makeInterchange(float span = 48.f, float bridgeHeight = 8.f, int lanes = 2,
                                                             std::uint32_t seed = 1);

    /** @brief Scene 1: single flat straight segment (no junction). */
    [[nodiscard]] static Result<RoadNetwork> makeStraight(float length = 36.f, int lanes = 2);
    /** @brief Scene 2: gentle horizontal curve (no junction). */
    [[nodiscard]] static Result<RoadNetwork> makeCurve(float radius = 18.f, int lanes = 2);
    /** @brief Scene 3: elevated straight with piers. */
    [[nodiscard]] static Result<RoadNetwork> makeBridge(float length = 36.f, float height = 6.f, int lanes = 2);
    /** @brief Scene 4: simple ground-level 4-way cross (one junction disc). */
    [[nodiscard]] static Result<RoadNetwork> makeCross(float span = 32.f, int lanes = 2);
    /** @brief Ground-level T-junction with an east-west through road and south stem. */
    [[nodiscard]] static Result<RoadNetwork> makeTee(float span = 32.f, int lanes = 2);
    /** @brief Ground-level Y-junction with three arms separated by 120 degrees. */
    [[nodiscard]] static Result<RoadNetwork> makeY(float span = 32.f, int lanes = 2);
    /**
     * @brief Build a ground-level N-way fan from absolute arm angles in degrees.
     * @param span Overall leaf-to-leaf scene extent; must be at least 16 metres.
     * @param lanes Forward lanes per arm in [1,4].
     * @param armAnglesDeg At least two finite angles; duplicates within one degree are merged.
     * @param intoHub Optional direction per input arm (true means leaf-to-hub); empty alternates after sorting.
     * @return A validated network or a structured input/topology diagnostic.
     */
    [[nodiscard]] static Result<RoadNetwork> makeFan(float span, int lanes, std::vector<float> armAnglesDeg,
                                                     std::vector<bool> intoHub = {});
    /** @brief Three-way scene with a 60-degree acute fork. */
    [[nodiscard]] static Result<RoadNetwork> makeFork(float span = 32.f, int lanes = 2);
    /** @brief Three-way scene with 135-degree skew corners. */
    [[nodiscard]] static Result<RoadNetwork> makeSkew(float span = 32.f, int lanes = 2);
    /** @brief Build a four-entry roundabout from ordinary nodes, curved edges and lane links. */
    [[nodiscard]] static Result<RoadNetwork> makeRoundabout(float span = 48.f, int lanes = 1);

    /**
     * @brief Dispatch a named debug/demo scene.
     * @param scene One of: straight, curve, bridge, cross, tee/t-junction, y/y-junction, fork, skew, sloped-t,
     * curve-uphill, tight-turn, roundabout, interchange.
     */
    [[nodiscard]] static Result<RoadNetwork> makeScene(const std::string& scene, float span = 36.f,
                                                       float bridgeHeight = 6.f, int lanes = 2, std::uint32_t seed = 1);

private:
    [[nodiscard]] Result<void> validateStyle(const RoadStyle& style) const;
    [[nodiscard]] int findNodeIndex(std::uint32_t id) const;
    [[nodiscard]] int findEdgeIndex(std::uint32_t id) const;
    [[nodiscard]] Result<void> validateLaneConnection(const RoadLaneConnection& link) const;

    std::vector<RoadNode>     nodes_;
    std::vector<RoadEdge>     edges_;
    std::vector<RoadLaneConnection> laneLinks_;
    std::vector<RoadLaneConnection> blockedLaneLinks_;
    std::unordered_map<std::uint32_t, int> nodeIndex_;
    std::unordered_map<std::uint32_t, int> edgeIndex_;
    std::uint32_t nextNodeId_ = 1;
    std::uint32_t nextEdgeId_ = 1;
    std::uint64_t revision_   = 0;
};

}  // namespace eve::procgen::road
