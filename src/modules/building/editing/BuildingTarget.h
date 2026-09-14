#pragma once

#include "editing/EditingAuthority.h"
#include "editing/EditableTarget.h"
#include "editing/EditingGizmo.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace eve::building {
class PlacementWorld;
}

namespace eve::building_editing {
using CapabilityId=editing::CapabilityId; using DiagnosticSeverity=editing::DiagnosticSeverity;
using DomainOperation=editing::DomainOperation; using EditRegion=editing::EditRegion;
using EditorDiagnostic=editing::Diagnostic; template<class T>using EditorResult=editing::Result<T>;
using EditorStatus=editing::Status; using EditorValue=editing::Value;
using IDomainOperationTarget=editing::IDomainOperationTarget; using IEditableTarget=editing::IEditableTarget;
using Revision=editing::Revision; using RuleId=editing::RuleId;
using TargetDescriptor=editing::TargetDescriptor; using TargetId=editing::TargetId;

/** @brief Persistent garrison member copied through building editor undo. */
struct BuildingGarrisonMemberRecord {
    std::string id;
    std::string type;
    std::vector<std::string> tags;
};

/** @brief Complete runtime-neutral placed-building snapshot. */
struct BuildingInstanceSnapshot {
    int instanceId = 0;
    std::string buildingId;
    /** @brief Placement coordinate domain: `cell`, `edge`, `corner`, or `free`. */
    std::string placementKind = "cell";
    /** @brief Canonical edge origin when placementKind is `edge`. */
    int edgeX = 0;
    int edgeY = 0;
    /** @brief Canonical edge axis: `horizontal` or `vertical`. */
    std::string edgeAxis = "horizontal";
    /** @brief Grid vertex address when placementKind is `corner`. */
    int cornerX = 0;
    int cornerY = 0;
    /** @brief Committed free-domain collision radius in world-plane units. */
    double freeRadius = 0.0;
    /** @brief Committed free-domain OBB half width; zero denotes circular collision. */
    double freeHalfWidth = 0.0;
    /** @brief Committed free-domain OBB half height; zero denotes circular collision. */
    double freeHalfHeight = 0.0;
    /** @brief Committed convex local polygon as world-plane x/y pairs. */
    std::vector<double> freeFootprintVertices;
    int cellX = 0;
    int cellY = 0;
    /** @brief Authoritative discrete floor coordinate. */
    int level = 0;
    double worldX = 0.0;
    double worldY = 0.0;
    double elevation = 0.0;
    /** @brief Stable surface provider identity captured at placement time. */
    std::string surfaceId;
    /** @brief Surface provider revision captured at placement time. */
    std::uint64_t surfaceRevision = 0;
    /** @brief Captured unit surface normal. */
    double surfaceNormalX = 0.0;
    double surfaceNormalY = 1.0;
    double surfaceNormalZ = 0.0;
    /** @brief Captured unit surface tangent. */
    double surfaceTangentX = 1.0;
    double surfaceTangentY = 0.0;
    double surfaceTangentZ = 0.0;
    /** @brief Number of footprint surface samples represented by this snapshot. */
    int surfaceSampleCount = 0;
    /** @brief Maximum sampled footprint slope in degrees. */
    double surfaceMaxSlopeDegrees = 0.0;
    /** @brief Sampled footprint height range. */
    double surfaceHeightDelta = 0.0;
    double rotationDegrees = 0.0;
    std::string channel;
    std::map<std::string, std::string> properties;
    std::vector<std::string> tags;
    std::vector<BuildingGarrisonMemberRecord> garrison;
    std::uint64_t garrisonRevision = 0;
};

/** @brief One occupied or rejected footprint cell in a placement preview. */
struct BuildingFootprintCell {
    int x = 0;
    int y = 0;
    bool inBounds = false;
    int occupant = 0;
    int terrain = 0;
};

/** @brief Runtime-neutral logical vertex used to author an orthogonal edge path. */
struct BuildingEdgePathVertex {
    int x = 0;
    int y = 0;
};

/** @brief Runtime-neutral cubic Bezier control point in logical grid-vertex space. */
struct BuildingEdgeCurvePoint {
    double x = 0.0;
    double y = 0.0;
};

/** @brief One canonical edge emitted by a curve draft preview. */
struct BuildingEdgeCurvePreviewEdge {
    int x = 0;
    int y = 0;
    std::string axis;
};

/** @brief Owning world-space center and normal sampled for a surface curve draft. */
struct BuildingEdgeCurveSurfaceSample {
    double worldX = 0.0;
    double worldY = 0.0;
    double worldZ = 0.0;
    double normalX = 0.0;
    double normalY = 1.0;
    double normalZ = 0.0;
};

/** @brief Immutable revision-tagged result for one interactive cubic curve draft. */
struct BuildingEdgeCurvePreview {
    EditorStatus status = EditorStatus::Failed;
    Revision worldRevision = 0;
    std::vector<BuildingEdgeCurvePoint> controlPoints;
    std::vector<BuildingEdgePathVertex> sampledVertices;
    std::vector<BuildingEdgeCurvePreviewEdge> edges;
    std::string surfaceProviderName;
    std::string surfaceId;
    std::uint64_t surfaceRevision = 0;
    std::vector<BuildingEdgeCurveSurfaceSample> surfaceSamples;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Structured footprint/snap preview tied to a world revision. */
struct BuildingPlacementPreview {
    EditorStatus status = EditorStatus::Failed;
    Revision worldRevision = 0;
    int snappedCellX = 0;
    int snappedCellY = 0;
    double snappedWorldX = 0.0;
    double snappedWorldY = 0.0;
    double elevation = 0.0;
    double normalizedRotation = 0.0;
    std::vector<BuildingFootprintCell> cells;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Live PlacementWorld target with reversible place/move/remove operations. */
class BuildingPlacementTarget final : public virtual IEditableTarget,
                                      public IDomainOperationTarget,
                                      public editing::IDomainOperationTargetStaging {
public:
    /** @brief Stable capability id for placement preview and mutation. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.building-placement"); }
    /** @brief Bind a borrowed world that must outlive the target. */
    BuildingPlacementTarget(std::string id, building::PlacementWorld* world);
    TargetId targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion dirtyRegion() const override { return dirty_; }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    /** @brief Query an optional target capability. @return Borrowed pointer owned by this target, or null. @lifetime Valid until this target is destroyed or mutated. */
    void* queryCapability(const CapabilityId& capability) override;
    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override;
    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override;
    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override;

    /** @brief Return a complete immutable instance snapshot. */
    EditorResult<BuildingInstanceSnapshot> instance(int instanceId) const;
    /** @brief Return the smallest currently unused positive instance id. */
    int nextAvailableInstanceId() const;
    /** @brief Preview snap, normalized rotation, footprint cells and validation reasons. */
    BuildingPlacementPreview preview(const std::string& buildingId, double worldX, double worldY,
                                     double elevation, double rotationDegrees,
                                     int excludeInstanceId = 0) const;
    /** @brief Plan reversible exact-id placement after full footprint validation. */
    EditorResult<DomainOperation> makePlace(const BuildingInstanceSnapshot& placed) const;
    /** @brief Plan reversible movement of an existing building. */
    EditorResult<DomainOperation> makeMove(int instanceId, int cellX, int cellY,
                                            double rotationDegrees) const;
    /** @brief Build an exact free-domain move operation without grid snapping. */
    EditorResult<DomainOperation> makeMoveFree(int instanceId, double worldX, double worldY,
                                               double elevation,
                                               double rotationDegrees) const;
    /** @brief Plan a reversible cell-definition replacement preserving instance state. */
    EditorResult<DomainOperation> makeReplace(int instanceId,
                                              const std::string& replacementBuildingId) const;
    /** @brief Plan one atomic reversible rectangular placement gesture. */
    EditorResult<DomainOperation> makeRectangle(const std::string& buildingId, int minCellX,
                                                int minCellY, int maxCellX, int maxCellY,
                                                double rotationDegrees) const;
    /** @brief Plan one atomic reversible circular-brush placement gesture. */
    EditorResult<DomainOperation> makeBrush(const std::string& buildingId, int centerCellX,
                                            int centerCellY, int radius,
                                            double rotationDegrees) const;
    /** @brief Plan one atomic reversible multi-segment edge path gesture. */
    EditorResult<DomainOperation>
    makeEdgePath(const std::string& buildingId,
                 const std::vector<BuildingEdgePathVertex>& vertices) const;
    /** @brief Plan one atomic reversible cubic Bezier edge-curve gesture. */
    EditorResult<DomainOperation>
    makeEdgeCubicBezier(const std::string& buildingId,
                        const std::vector<BuildingEdgeCurvePoint>& controlPoints,
                        int subdivisions) const;
    /** @brief Plan one atomic reversible cubic curve sampled on a named surface. */
    EditorResult<DomainOperation> makeEdgeCubicBezierOnSurface(
        const std::string& buildingId,
        const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
        const std::string& surfaceName) const;
    /** @brief Recompute an immutable interactive curve draft without mutating the world. */
    BuildingEdgeCurvePreview previewEdgeCubicBezier(
        const std::string& buildingId,
        const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
        int replacingMemberInstanceId = 0, const std::string& surfaceName = {}) const;
    /** @brief Build renderer-neutral handles, tangent lines and sampled path for a curve draft. */
    editing::GizmoSnapshot edgeCubicBezierGizmo(
        const std::string& buildingId,
        const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
        int level = 0, int replacingMemberInstanceId = 0,
        const std::string& surfaceName = {}) const;
    /** @brief Plan atomic replacement of an existing curve group after a handle drag. */
    EditorResult<DomainOperation> makeUpdateEdgeCubicBezier(
        int memberInstanceId, const std::vector<BuildingEdgeCurvePoint>& controlPoints,
        int subdivisions) const;
    /** @brief Convert a world-space point to continuous logical grid coordinates. */
    EditorResult<BuildingEdgeCurvePoint> curveLogicalPointFromWorld(
        double worldX, double worldY, double worldZ) const;
    /** @brief Plan reversible removal preserving properties, tags and garrison state. */
    EditorResult<DomainOperation> makeRemove(int instanceId) const;
    /** @brief Capture all instances in deterministic insertion order. */
    EditorValue snapshotValue() const;

private:
    BuildingPlacementTarget(std::string id, std::unique_ptr<building::PlacementWorld> world,
                            unsigned long long revision);
    std::string id_;
    building::PlacementWorld* world_ = nullptr;
    std::unique_ptr<building::PlacementWorld> ownedWorld_;
    unsigned long long revision_ = 1;
    EditRegion dirty_;
};

/** @brief Immutable result of one curve-handle pointer update. */
struct BuildingEdgeCurveDragPreview {
    EditorStatus status = EditorStatus::Failed;
    int controlPointIndex = -1;
    BuildingEdgeCurvePreview curve;
    editing::GizmoSnapshot gizmo;
    std::vector<EditorDiagnostic> diagnostics;
};

/**
 * @brief UI-neutral ray picking and drag state for one existing cubic building curve.
 * @ownership Borrows the target; the caller must end/cancel before destroying it.
 * @thread Viewport/editor thread only; callbacks are not retained or invoked.
 */
class BuildingEdgeCurveDragSession {
public:
    /** @brief Configure an idle drag session from authoritative editor selection state. */
    BuildingEdgeCurveDragSession(BuildingPlacementTarget* target, std::string buildingId,
                                 int memberInstanceId,
                                 std::vector<BuildingEdgeCurvePoint> controlPoints,
                                 int subdivisions, std::string surfaceName = {});
    /** @brief Pick and begin dragging the closest control sphere intersected by a world ray. */
    EditorResult<BuildingEdgeCurveDragPreview> beginDrag(
        double rayOriginX, double rayOriginY, double rayOriginZ,
        double rayDirectionX, double rayDirectionY, double rayDirectionZ);
    /** @brief Reproject a pointer ray onto the camera-facing drag plane and refresh the draft. */
    EditorResult<BuildingEdgeCurveDragPreview> updateDrag(
        double rayOriginX, double rayOriginY, double rayOriginZ,
        double rayDirectionX, double rayDirectionY, double rayDirectionZ);
    /** @brief Build the atomic replacement operation for the last valid draft and become idle. */
    EditorResult<DomainOperation> finishDrag();
    /** @brief Discard transient drag state without producing an operation. */
    void cancelDrag();
    bool isDragging() const { return dragging_; }
    int activeControlPointIndex() const { return activeControlPointIndex_; }

private:
    EditorResult<BuildingEdgeCurveDragPreview> previewCurrent() const;
    BuildingPlacementTarget* target_ = nullptr;
    std::string buildingId_;
    int memberInstanceId_ = 0;
    std::vector<BuildingEdgeCurvePoint> controls_;
    int subdivisions_ = 0;
    std::string surfaceName_;
    Revision baseRevision_ = 0;
    int activeControlPointIndex_ = -1;
    std::array<double, 3> dragPlanePoint_{0.0, 0.0, 0.0};
    std::array<double, 3> dragPlaneNormal_{0.0, 0.0, 1.0};
    bool dragging_ = false;
    bool lastPreviewValid_ = false;
};

}  // namespace eve::building_editing
