#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingTargetOperations.h"
#include "procgen/editing/ProcgenEditingTypes.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::procgen_editing {

/** @brief Authoring junction node with a stable id. */
struct RoadNodeRecord {
    StableId id;
    double   x = 0.0, y = 0.0, z = 0.0;
    double   junctionRadius = 6.0;
};

/** @brief One authored centerline sample on a road edge. */
struct RoadControlPointRecord {
    double x = 0.0, y = 0.0, z = 0.0;
};

/** @brief Authoring directed edge with control polyline and lane counts. */
struct RoadEdgeRecord {
    StableId                             id;
    StableId                             from;
    StableId                             to;
    std::vector<RoadControlPointRecord>  controlPoints;
    int                                  lanesForward  = 2;
    int                                  lanesBackward = 0;
    double                               laneWidth     = 3.5;
};

/** @brief Owning bake preview snapshot (mesh groups, no network pointers). */
struct RoadBakePreviewResult {
    procgen::MeshBuild mesh;
    int                edgeCount = 0;
    int                nodeCount = 0;
};

/** @brief UI-neutral reversible road-network editing capability. */
class IRoadNetworkDocumentEditTarget {
public:
    virtual ~IRoadNetworkDocumentEditTarget() = default;
    /** @brief Return the stable capability id. */
    static CapabilityId editingCapabilityId() {
        return CapabilityId("eve.procgen.target.road-network-document");
    }
    /** @brief Plan creation or replacement of one junction node. */
    virtual EditorResult<DomainOperation> makeSetNode(const RoadNodeRecord& node) const = 0;
    /** @brief Plan removal of one junction node (edges using it must already be gone). */
    virtual EditorResult<DomainOperation> makeDeleteNode(const StableId& node) const = 0;
    /** @brief Plan creation or replacement of one directed edge. */
    virtual EditorResult<DomainOperation> makeSetEdge(const RoadEdgeRecord& edge) const = 0;
    /** @brief Plan removal of one directed edge. */
    virtual EditorResult<DomainOperation> makeDeleteEdge(const StableId& edge) const = 0;
};

/**
 * @brief Owning road-network authoring document with transactional operations.
 *
 * Schema: `eve.procgen.roadNetwork` v1. Unknown snapshot fields are ignored.
 * Compile produces an owning runtime `RoadNetwork` with no retained document pointers.
 */
class EVENGINE_API_ORCHESTRATION RoadNetworkDocument final : public ::eve::editing::EditableTargetState,
                                                             public virtual IEditableTarget,
                                                             public IDomainOperationTarget,
                                                             public IDomainOperationTargetStaging,
                                                             public IRoadNetworkDocumentEditTarget {
public:
    /** @brief Construct an empty document with a stable target id. */
    explicit RoadNetworkDocument(std::string id);

    TargetId         targetId() const override { return TargetId(id_); }
    TargetDescriptor describe() const override;
    void*            queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

    EditorResult<DomainOperation> makeSetNode(const RoadNodeRecord& node) const override;
    EditorResult<DomainOperation> makeDeleteNode(const StableId& node) const override;
    EditorResult<DomainOperation> makeSetEdge(const RoadEdgeRecord& edge) const override;
    EditorResult<DomainOperation> makeDeleteEdge(const StableId& edge) const override;
    /**
     * @brief Move a junction and sync incident edge endpoint control points in one undo unit.
     * @param node Stable node id to relocate.
     * @param x New world X.
     * @param y New world Y (up).
     * @param z New world Z.
     * @return Snapshot-replace operation, or structured failure when the node is missing.
     */
    [[nodiscard]] EditorResult<DomainOperation> makeMoveNode(const StableId& node, double x, double y,
                                                             double z) const;

    /** @brief Return nodes in stable-id order. */
    [[nodiscard]] std::vector<RoadNodeRecord> nodes() const;
    /** @brief Return edges in stable-id order. */
    [[nodiscard]] std::vector<RoadEdgeRecord> edges() const;
    /** @brief Compile an owning runtime network snapshot. */
    [[nodiscard]] EditorResult<procgen::road::RoadNetwork> compileNetwork() const;
    /**
     * @brief Bake a lightweight mesh preview from the compiled network.
     * @param options Bake knobs; pier/navigation default off for interactive loops.
     */
    [[nodiscard]] EditorResult<RoadBakePreviewResult> previewBake(
        procgen::road::RoadBakeOptions options = {}) const;
    /** @brief Capture schema `eve.procgen.roadNetwork` version one. */
    [[nodiscard]] EditorValue snapshotValue() const;
    /** @brief Atomically load a validated version-one snapshot. */
    [[nodiscard]] EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string                         id_;
    std::map<StableId, RoadNodeRecord>  nodes_;
    std::map<StableId, RoadEdgeRecord>  edges_;
};

}  // namespace eve::procgen_editing
