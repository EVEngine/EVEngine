#pragma once

#include "editing/EditingGizmo.h"
#include "editing/EditingCommandRegistry.h"
#include "editing/EditingTargetOperations.h"
#include "procgen/editing/ProcgenEditingTypes.h"
#include "procgen/road/RoadNetwork.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace eve::procgen_editing {

/**
 * @brief Transaction target that edits stable RoadNetwork node/edge ids.
 *
 * The live target borrows one authoritative RoadNetwork. Detached transaction
 * candidates alone own a temporary copy; commit atomically replaces the live
 * value, so this adapter never becomes a second persistent road document.
 */
class RoadNetworkEditTarget final : public virtual editing::IEditableTarget,
                                    public editing::IDomainOperationTarget,
                                    public editing::IDomainOperationTargetStaging,
                                    public editing::IEditingSnapshotProvider {
public:
    static constexpr const char* kMoveNode        = "road.node.move.v1";
    static constexpr const char* kSetNodeRadius   = "road.node.set-radius.v1";
    static constexpr const char* kSetNodeControl  = "road.node.set-junction-control.v1";
    static constexpr const char* kSetEdgePoints   = "road.edge.set-control-points.v1";
    static constexpr const char* kSetEdgeStyle    = "road.edge.set-style.v1";
    static constexpr const char* kSetEdgeLanes    = "road.edge.set-lanes.v1";
    static constexpr const char* kReverseEdge     = "road.edge.reverse.v1";
    static constexpr const char* kReconnectEdgeEndpoint = "road.edge.reconnect-endpoint.v1";
    static constexpr const char* kDetachEdgeEndpoint = "road.edge.detach-endpoint.v1";
    static constexpr const char* kReattachEdgeEndpoint = "road.edge.reattach-endpoint.v1";
    static constexpr const char* kSplitEdge       = "road.edge.split.v1";
    static constexpr const char* kSplitEdgeAtPosition = "road.edge.split-at-position.v1";
    static constexpr const char* kUnsplitEdge     = "road.edge.unsplit.v1";
    static constexpr const char* kMergeNodes      = "road.node.merge.v1";
    static constexpr const char* kMergeNodesAndConnect = "road.node.merge-and-connect.v1";
    static constexpr const char* kUnmergeNodes    = "road.node.unmerge.v1";
    static constexpr const char* kConnectNodeAtPosition = "road.node.connect-at-position.v1";
    static constexpr const char* kDisconnectNodeAtPosition = "road.node.disconnect-at-position.v1";
    static constexpr const char* kConnectEdgeIntersection = "road.edge.connect-intersection.v1";
    static constexpr const char* kDisconnectEdgeIntersection = "road.edge.disconnect-intersection.v1";
    static constexpr const char* kRestoreNode     = "road.node.restore.v1";
    static constexpr const char* kRemoveNode      = "road.node.remove.v1";
    static constexpr const char* kRestoreEdge     = "road.edge.restore.v1";
    static constexpr const char* kRemoveEdge      = "road.edge.remove.v1";
    static constexpr const char* kRestoreLaneLink = "road.lane-link.restore.v1";
    static constexpr const char* kRemoveLaneLink  = "road.lane-link.remove.v1";
    static constexpr const char* kBlockLaneLink   = "road.lane-link.block.v1";
    static constexpr const char* kUnblockLaneLink = "road.lane-link.unblock.v1";
    static constexpr const char* kRestoreLaneLinks = "road.lane-links.restore.v1";
    static constexpr const char* kRemoveLaneLinks  = "road.lane-links.remove.v1";

    /**
     * @brief Bind an editor target to an externally owned road network.
     * @param id Stable editor target identity.
     * @param network Borrowed authoritative network.
     * @lifetime The network must outlive this target and all in-flight transactions.
     */
    RoadNetworkEditTarget(std::string id, procgen::road::RoadNetwork& network);
    /** @brief Create an editor-owned empty road target for automation/composition hosts. */
    [[nodiscard]] static std::unique_ptr<RoadNetworkEditTarget> createOwned(std::string id);

    editing::TargetId         targetId() const override { return editing::TargetId(id_); }
    std::uint64_t             revision() const override { return network_->revision(); }
    editing::EditRegion       dirtyRegion() const override { return dirty_; }
    void                      clearDirtyRegion() override { dirty_.clear(); }
    editing::TargetDescriptor describe() const override;
    void*                     queryCapability(const editing::CapabilityId& capability) override;

    /**
     * @brief Build a renderer-neutral authoring overlay from stable road identities.
     * @param maximumPrimitives Hard output budget; zero produces a rejected snapshot.
     * @param showLabels Whether node identity labels are included.
     * @return Owning revision-tagged snapshot, or a rejected snapshot when the budget is insufficient.
     * @cost Linear in node and edge control-point count; output is bounded by maximumPrimitives.
     * @thread Call only while the borrowed RoadNetwork is not being mutated.
     */
    [[nodiscard]] editing::GizmoSnapshot gizmo(std::size_t maximumPrimitives = 4096,
                                                bool showLabels = true) const;

    /**
     * @brief Serialize the complete editable network as `eve.procgen.roadNetwork` schema version 1.
     * @return Owning value containing stable nodes, edges, styles, control points, and lane links.
     * @cost Linear in graph size and owning; use for save/reload boundaries, not per frame.
     * @note Readers ignore unknown object fields for forward-compatible additive metadata.
     */
    [[nodiscard]] EditorValue snapshotValue() const override;
    /**
     * @brief Atomically replace the network from a validated version-one snapshot.
     * @param snapshot Serialized road network value; unknown object fields are ignored.
     * @return Applied on success, otherwise a structured rejection without changing the live network.
     * @cost Linear in graph size with bounded node, edge, control-point, and lane-link counts.
     */
    [[nodiscard]] EditorResult<void> loadSnapshot(const EditorValue& snapshot);

    [[nodiscard]] EditorResult<void> applyDomainOperation(const editing::DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<editing::IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void>                               commitDomainState(
        std::unique_ptr<editing::IDomainOperationTarget> candidate) override;

    /**
     * @brief Create an undoable stable-id node move operation.
     * @param nodeId Stable node identity.
     * @param x New finite world X coordinate.
     * @param y New finite world Y coordinate.
     * @param z New finite world Z coordinate.
     * @return Owning forward/inverse operation or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeMoveNode(std::uint32_t nodeId, float x, float y,
                                                                      float z) const;
    /**
     * @brief Create an undoable junction-radius replacement operation.
     * @param nodeId Stable node identity.
     * @param junctionRadius Finite positive junction trim radius.
     * @return Owning forward/inverse operation or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSetNodeJunctionRadius(std::uint32_t nodeId,
                                                                                    float junctionRadius) const;
    /**
     * @brief Create an undoable junction traffic-control replacement operation.
     * @param nodeId Stable node identity.
     * @param control Authoritative control policy for the junction approaches.
     * @return Owning forward/inverse operation or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSetNodeJunctionControl(
        std::uint32_t nodeId, procgen::road::RoadJunctionControl control) const;
    /**
     * @brief Create an undoable endpoint-node merge within an explicit 3D snap distance.
     * @param keepNodeId Stable node identity and position retained by the merge.
     * @param removeNodeId Distinct nearby node rewired into keepNodeId.
     * @param maxDistance Maximum allowed distance between nodes.
     * @return Merge/unmerge operation pair preserving affected edges and lane links.
     * @cost Linear in road topology and affected lane links; intended for authoring transactions.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeMergeNodes(std::uint32_t keepNodeId,
                                                                        std::uint32_t removeNodeId,
                                                                        float maxDistance) const;
    /**
     * @brief Atomically merge nearby endpoint nodes and generate every missing legal turn at the result.
     * @param keepNodeId Stable node identity and position retained by the merge.
     * @param removeNodeId Distinct nearby node rewired into keepNodeId.
     * @param maxDistance Maximum allowed 3D distance between nodes.
     * @return Merge/unmerge operation pair restoring the exact pre-merge edges and lane links.
     * @cost Linear in topology plus quadratic in incident edge count times lane count; authoring only.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeMergeNodesAndConnect(std::uint32_t keepNodeId,
                                                                                  std::uint32_t removeNodeId,
                                                                                  float maxDistance) const;
    /**
     * @brief Create an undoable stable-id spline replacement operation.
     * @param edgeId Stable edge identity.
     * @param points Owning replacement control points.
     * @return Owning forward/inverse operation or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSetEdgeControlPoints(
        std::uint32_t edgeId, std::vector<procgen::road::RoadControlPoint> points) const;
    /**
     * @brief Create an undoable stable-id edge style replacement operation.
     * @param edgeId Stable edge identity.
     * @param style Complete replacement style value.
     * @return Owning forward/inverse operation or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSetEdgeStyle(std::uint32_t            edgeId,
                                                                          procgen::road::RoadStyle style) const;
    /**
     * @brief Create an undoable lane-count replacement, including links invalidated by a shrink.
     * @param edgeId Stable edge identity.
     * @param lanesForward New forward lane count.
     * @param lanesBackward New reverse lane count.
     * @return Owning forward/inverse operation or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSetEdgeLaneCounts(std::uint32_t edgeId,
                                                                               int lanesForward,
                                                                               int lanesBackward) const;
    /**
     * @brief Create a self-inverse operation that reverses an edge and all direction-relative metadata.
     * @param edgeId Stable edge identity.
     * @return Owning operation or a structured rejection.
     * @cost Linear in edge control points and network lane links.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeReverseEdge(std::uint32_t edgeId) const;
    /**
     * @brief Create an undoable operation reconnecting one road endpoint to an existing node.
     * @param edgeId Stable road edge identity.
     * @param fromEndpoint True for the authored start endpoint, false for the end endpoint.
     * @param nodeId Existing replacement node identity.
     * @return Forward/inverse pair that also restores lane connections pruned from the old junction.
     * @cost Linear in network lane-link count; intended for authoring transactions.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeReconnectEdgeEndpoint(
        std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId) const;
    /**
     * @brief Atomically reconnect one endpoint and add missing turns involving that edge at the new junction.
     * @param edgeId Stable road edge identity.
     * @param fromEndpoint True for the authored start endpoint, false for the end endpoint.
     * @param nodeId Existing replacement node identity.
     * @return Reconnect operation whose inverse restores the old junction connections exactly.
     * @cost Linear in topology plus quadratic in new-junction incident edges times lane count; authoring only.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeReconnectEdgeEndpointAndConnect(
        std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId) const;
    /**
     * @brief Snap one endpoint to the nearest distinct node inside an explicit 3D radius.
     * @param edgeId Stable road edge identity.
     * @param fromEndpoint True for the authored start endpoint, false for the end endpoint.
     * @param maxDistance Finite non-negative maximum distance from the current endpoint.
     * @param connectTurns Whether to atomically add missing turns involving the moved edge.
     * @return Existing reconnect operation targeting the deterministic nearest node.
     * @cost Linear in node count plus reconnect cost; intended for authoring interactions.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSnapEdgeEndpoint(
        std::uint32_t edgeId, bool fromEndpoint, float maxDistance, bool connectTurns = true) const;
    /**
     * @brief Create an undoable operation detaching one endpoint onto a new coincident stable node.
     * @param edgeId Stable road edge identity.
     * @param fromEndpoint True for the authored start endpoint, false for the end endpoint.
     * @return Detach/reattach pair restoring the old node and all pruned allowed/blocked turns.
     * @cost Linear in network lane-link count; intended for authoring transactions.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeDetachEdgeEndpoint(
        std::uint32_t edgeId, bool fromEndpoint) const;
    /**
     * @brief Create an undoable split at an authored interior control point.
     * @param edgeId Stable edge identity retained by the first half.
     * @param controlPointIndex Interior control-point index.
     * @param junctionRadius Radius assigned to the inserted junction node.
     * @return Split/unsplit operation pair with stable reserved identities.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSplitEdge(std::uint32_t edgeId,
                                                                       std::size_t controlPointIndex,
                                                                       float junctionRadius = 6.f) const;
    /**
     * @brief Create an undoable nearest-centerline split from a world position.
     * @param edgeId Stable edge identity retained by the first half.
     * @param position Finite world position to project onto the authored centerline.
     * @param maxDistance Maximum allowed 3D snap distance.
     * @param junctionRadius Radius assigned to the inserted junction node.
     * @return Split/unsplit operation pair with stable reserved identities.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeSplitEdgeAtPosition(
        std::uint32_t edgeId, procgen::road::RoadControlPoint position, float maxDistance,
        float junctionRadius = 6.f) const;
    /**
     * @brief Atomically split a road at a world position and add a branch from an existing node.
     * @param sourceNodeId Existing branch source node.
     * @param edgeId Road edge to split and connect.
     * @param position World position projected onto the road centerline.
     * @param maxDistance Maximum allowed 3D snap distance.
     * @param junctionRadius Radius assigned to the inserted junction.
     * @param lanesForward Forward branch lane count.
     * @param lanesBackward Reverse branch lane count.
     * @param style Branch road style.
     * @return One forward/inverse operation covering both structural changes.
     * @cost Linear in road topology and lane-link count; intended for authoring transactions.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeConnectNodeAtPosition(
        std::uint32_t sourceNodeId, std::uint32_t edgeId, procgen::road::RoadControlPoint position,
        float maxDistance, float junctionRadius = 6.f, int lanesForward = 1, int lanesBackward = 0,
        procgen::road::RoadStyle style = {}) const;
    /**
     * @brief Atomically convert one unique same-level crossing into a connected junction.
     * @param firstEdgeId First road edge crossing internally.
     * @param secondEdgeId Distinct second road edge crossing internally.
     * @param maximumHeightDelta Maximum vertical separation that may become a junction.
     * @param junctionRadius Radius assigned to the shared intersection node.
     * @return One operation covering both splits, node merge, and lane turns.
     * @cost Quadratic in bounded sampled spline intervals, then linear in graph and lane-link size.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeConnectEdgeIntersection(
        std::uint32_t firstEdgeId, std::uint32_t secondEdgeId, float maximumHeightDelta = 0.5f,
        float junctionRadius = 6.f) const;
    /**
     * @brief Create an undoable node insertion with a reserved stable id.
     * @param x Finite world X coordinate.
     * @param y Finite world Y coordinate.
     * @param z Finite world Z coordinate.
     * @param junctionRadius Positive junction trim radius.
     * @return Owning restore/remove operation pair or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeAddNode(float x, float y, float z,
                                                                     float junctionRadius = 6.f) const;
    /**
     * @brief Create an undoable removal for an isolated stable-id node.
     * @param nodeId Stable node identity.
     * @return Owning remove/restore operation pair or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeRemoveNode(std::uint32_t nodeId) const;
    /**
     * @brief Create an undoable edge insertion with a reserved stable id.
     * @param from Stable source node identity.
     * @param to Stable destination node identity.
     * @param points Owning spline control points.
     * @param lanesForward Forward lane count.
     * @param lanesBackward Reverse lane count.
     * @param style Complete edge style.
     * @return Owning restore/remove operation pair or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeAddEdge(
        std::uint32_t from, std::uint32_t to, std::vector<procgen::road::RoadControlPoint> points, int lanesForward = 2,
        int lanesBackward = 0, procgen::road::RoadStyle style = {}) const;
    /**
     * @brief Create an edge and every missing endpoint turn involving that edge as one undoable operation.
     * @param from Stable source node identity.
     * @param to Stable destination node identity.
     * @param points Owning spline control points.
     * @param lanesForward Forward lane count.
     * @param lanesBackward Reverse lane count.
     * @param style Complete edge style.
     * @return One restore/remove pair containing the stable edge and its exact generated lane links.
     * @note Existing-to-existing endpoint routes are not generated or changed.
     * @cost Quadratic in endpoint incident edge count times lane count; intended for authoring transactions.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeAddEdgeAndConnect(
        std::uint32_t from, std::uint32_t to, std::vector<procgen::road::RoadControlPoint> points, int lanesForward = 2,
        int lanesBackward = 0, procgen::road::RoadStyle style = {}) const;
    /**
     * @brief Create an undoable edge removal including its affected lane links.
     * @param edgeId Stable edge identity.
     * @return Owning remove/restore operation pair or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeRemoveEdge(std::uint32_t edgeId) const;
    /**
     * @brief Create an undoable manual lane connection.
     * @param link Complete directed lane-to-lane connection.
     * @return Owning restore/remove operation pair or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeAddLaneLink(procgen::road::RoadLaneConnection link) const;
    /**
     * @brief Create an undoable removal of one exact manual lane connection.
     * @param link Composite connection identity to remove.
     * @return Owning remove/restore operation pair or a structured rejection.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeRemoveLaneLink(
        const procgen::road::RoadLaneConnection& link) const;
    /** @brief Plan a persistent prohibited turn, atomically removing an active exact link when present. */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeBlockLaneLink(
        procgen::road::RoadLaneConnection link) const;
    /** @brief Plan removal of one persistent prohibited turn without automatically activating it. */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeUnblockLaneLink(
        const procgen::road::RoadLaneConnection& link) const;
    /**
     * @brief Plan all missing legal turn links at one node as one atomic undoable operation.
     * @param nodeId Stable junction node identity.
     * @return Batch restore/remove operation containing only newly generated links.
     */
    [[nodiscard]] EditorResult<editing::DomainOperation> makeConnectAllTurns(std::uint32_t nodeId) const;

private:
    RoadNetworkEditTarget(std::string id, std::unique_ptr<procgen::road::RoadNetwork> candidate);
    [[nodiscard]] EditorResult<editing::DomainOperation> makeMergeNodesImpl(std::uint32_t keepNodeId,
                                                                            std::uint32_t removeNodeId,
                                                                            float maxDistance,
                                                                            bool connectTurns) const;

    std::string                                 id_;
    std::unique_ptr<procgen::road::RoadNetwork> candidate_;
    procgen::road::RoadNetwork*                 network_ = nullptr;
    editing::EditRegion                         dirty_;
};

/** @brief Register stable road topology commands with an existing editing host. */
[[nodiscard]] editing::Result<void> registerRoadNetworkEditingCommands(editing::IEditingCommandRegistry& registry);

}  // namespace eve::procgen_editing
