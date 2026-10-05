#pragma once

#include "editing/EditingGizmo.h"
#include "procgen/editing/RoadNetworkDocument.h"

#include <array>
#include <string>
#include <vector>

namespace eve::procgen_editing {

/** @brief Renderer-neutral appearance settings for road viewport overlays. */
struct RoadGizmoStyle {
    double                nodeSize          = 0.45;
    double                controlPointSize  = 0.22;
    std::array<double, 4> edgeColor{0.25, 0.78, 1.0, 1.0};
    std::array<double, 4> textColor{1.0, 1.0, 1.0, 1.0};
    std::array<double, 4> layPreviewColor{1.0, 0.85, 0.2, 0.85};
    bool                  showLabels        = true;
    bool                  showControlPoints = true;
};

/** @brief Immutable renderer-neutral road drag preview. */
struct RoadDragPreview {
    EditorStatus           status = EditorStatus::Failed;
    std::string            handle;  ///< "node" or "cp"
    StableId               node;
    StableId               edge;
    int                    controlIndex = -1;
    RoadNodeRecord         nodeDraft;
    RoadEdgeRecord         edgeDraft;
    editing::GizmoSnapshot gizmo;
};

/** @brief Defaults used while interactively laying new road segments. */
struct RoadLayDefaults {
    double junctionRadius = 6.0;
    int    lanesForward   = 2;
    int    lanesBackward  = 0;
    double laneWidth      = 3.5;
    double snapRadius     = 4.0;
};

/** @brief One lay click's reversible operations plus live overlay preview. */
struct RoadLayCommit {
    std::vector<DomainOperation> operations;
    editing::GizmoSnapshot       gizmo;
    StableId                     activeFrom;
    StableId                     createdNode;
    StableId                     createdEdge;
};

/** @brief Immutable renderer-neutral road lay hover preview. */
struct RoadLayPreview {
    EditorStatus           status = EditorStatus::Failed;
    StableId               activeFrom;
    StableId               snapNode;
    bool                   hasHover = false;
    std::array<double, 3>  hover{0.0, 0.0, 0.0};
    editing::GizmoSnapshot gizmo;
};

/** @brief Builds junction spheres, edge polylines and interior control-point handles. */
class EVENGINE_API_ORCHESTRATION RoadNetworkGizmoBuilder {
public:
    /** @brief Build an immutable overlay for the authored road graph. */
    [[nodiscard]] editing::GizmoSnapshot build(const RoadNetworkDocument& document,
                                               RoadGizmoStyle             style = {}) const;
};

/**
 * @brief UI-neutral ray picking and transient drag state for a road document.
 * @ownership Borrows the document; finish or cancel before destroying it.
 * @thread Viewport/editor thread only; no callbacks are retained or invoked.
 */
class EVENGINE_API_ORCHESTRATION RoadNetworkDragSession {
public:
    /** @brief Create an idle session borrowing an authoritative road document. */
    explicit RoadNetworkDragSession(RoadNetworkDocument* document, RoadGizmoStyle style = {});
    /** @brief Pick the nearest junction or interior control-point sphere. */
    [[nodiscard]] EditorResult<RoadDragPreview> beginDrag(double rayOriginX, double rayOriginY, double rayOriginZ,
                                                          double rayDirectionX, double rayDirectionY,
                                                          double rayDirectionZ);
    /** @brief Project a pointer ray onto the camera-facing drag plane and update only the draft. */
    [[nodiscard]] EditorResult<RoadDragPreview> updateDrag(double rayOriginX, double rayOriginY, double rayOriginZ,
                                                           double rayDirectionX, double rayDirectionY,
                                                           double rayDirectionZ);
    /** @brief Produce one reversible node-move or edge-set operation without applying it. */
    [[nodiscard]] EditorResult<DomainOperation> finishDrag();
    /** @brief Discard transient state without changing the document. */
    void cancelDrag();
    /** @brief Report whether a drag owns transient state. */
    [[nodiscard]] bool isDragging() const noexcept { return dragging_; }
    /** @brief Return node or cp for the active handle. */
    [[nodiscard]] const std::string& activeHandle() const noexcept { return activeHandle_; }
    /** @brief Return the active node id when dragging a junction. */
    [[nodiscard]] StableId activeNode() const { return activeNode_; }
    /** @brief Return the active edge id when dragging a control point. */
    [[nodiscard]] StableId activeEdge() const { return activeEdge_; }
    /** @brief Return the active control-point index, or -1 when idle/node. */
    [[nodiscard]] int activeControlIndex() const noexcept { return activeControlIndex_; }

private:
    [[nodiscard]] EditorResult<RoadDragPreview> previewCurrent() const;
    RoadNetworkDocument*                        document_           = nullptr;
    RoadGizmoStyle                              style_;
    Revision                                    baseRevision_       = 0;
    std::string                                 activeHandle_;
    StableId                                    activeNode_;
    StableId                                    activeEdge_;
    int                                         activeControlIndex_ = -1;
    RoadNodeRecord                              nodeDraft_;
    RoadEdgeRecord                              edgeDraft_;
    std::array<double, 3>                       dragPlanePoint_{0.0, 0.0, 0.0};
    std::array<double, 3>                       dragPlaneNormal_{0.0, 1.0, 0.0};
    bool                                        dragging_ = false;
};

/**
 * @brief Click-to-lay session that chains junction nodes and directed edges.
 * @ownership Borrows the document; cancel before destroying it.
 * @thread Viewport/editor thread only; no callbacks are retained or invoked.
 *
 * Each `commitClick` returns operations the host must apply. The session tracks
 * the last committed `from` node and never mutates the document itself.
 */
class EVENGINE_API_ORCHESTRATION RoadLaySession {
public:
    /** @brief Create an idle lay session with default lane/junction knobs. */
    explicit RoadLaySession(RoadNetworkDocument* document, RoadLayDefaults defaults = {},
                            RoadGizmoStyle style = {});
    /** @brief Activate laying and optionally seed the first node via a later click. */
    [[nodiscard]] EditorResult<RoadLayPreview> begin();
    /** @brief Start from an existing junction without creating a node. */
    [[nodiscard]] EditorResult<RoadLayPreview> beginFromNode(const StableId& node);
    /** @brief Update the rubber-band hover tip without mutating the document. */
    [[nodiscard]] EditorResult<RoadLayPreview> updateHover(double x, double y, double z);
    /**
     * @brief Commit one lay click at a world point.
     * @param x World X.
     * @param y World Y (up).
     * @param z World Z.
     * @param newNodeId Stable id for a newly created tip node (required when not snapping).
     * @param newEdgeId Stable id for a newly created edge (required when extending from activeFrom).
     */
    [[nodiscard]] EditorResult<RoadLayCommit> commitClick(double x, double y, double z, StableId newNodeId = {},
                                                          StableId newEdgeId = {});
    /** @brief Discard active lay state without changing the document. */
    void cancel();
    /** @brief Report whether laying is active. */
    [[nodiscard]] bool isLaying() const noexcept { return laying_; }
    /** @brief Return the current chain tip node, or empty before the first commit. */
    [[nodiscard]] StableId activeFrom() const { return activeFrom_; }

private:
    [[nodiscard]] EditorResult<RoadLayPreview> previewCurrent(bool withHover, double hx, double hy, double hz) const;
    [[nodiscard]] StableId                     findSnapNode(double x, double y, double z) const;
    RoadNetworkDocument*                       document_ = nullptr;
    RoadLayDefaults                            defaults_;
    RoadGizmoStyle                             style_;
    StableId                                   activeFrom_;
    bool                                       laying_ = false;
};

}  // namespace eve::procgen_editing
