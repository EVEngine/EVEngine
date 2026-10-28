#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "building/editing/BuildingTargetInternal.h"
namespace eve::building_editing {
using building::codec::runtime;
using building::codec::snapshot;
using namespace target_detail;
BuildingEdgeCurveDragSession::BuildingEdgeCurveDragSession(BuildingPlacementTarget* target, std::string buildingId,
                                                           int                                 memberInstanceId,
                                                           std::vector<BuildingEdgeCurvePoint> controlPoints,
                                                           int subdivisions, std::string surfaceName)
    : target_(target),
      buildingId_(std::move(buildingId)),
      memberInstanceId_(memberInstanceId),
      controls_(std::move(controlPoints)),
      subdivisions_(subdivisions),
      surfaceName_(std::move(surfaceName)) {}

Result<BuildingEdgeCurveDragPreview> BuildingEdgeCurveDragSession::previewCurrent() const {
    if (!target_)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(EditorStatus::Rejected,
                                                                  RuleId("editor.building.curve-drag-target-required"),
                                                                  "Curve drag target is unavailable");
    BuildingEdgeCurveDragPreview result;
    result.controlPointIndex = activeControlPointIndex_;
    result.curve =
        target_->previewEdgeCubicBezier(buildingId_, controls_, subdivisions_, memberInstanceId_, surfaceName_);
    result.gizmo =
        target_->edgeCubicBezierGizmo(buildingId_, controls_, subdivisions_, 0, memberInstanceId_, surfaceName_);
    result.status      = result.curve.status;
    result.diagnostics = result.curve.diagnostics;
    return eve::editing::applied<BuildingEdgeCurveDragPreview>(std::move(result));
}

Result<BuildingEdgeCurveDragPreview> BuildingEdgeCurveDragSession::beginDrag(double rayOriginX, double rayOriginY,
                                                                             double rayOriginZ, double rayDirectionX,
                                                                             double rayDirectionY,
                                                                             double rayDirectionZ) {
    cancelDrag();
    if (!target_ || memberInstanceId_ <= 0 || controls_.size() != 4)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, RuleId("editor.building.curve-drag-state-invalid"),
            "Curve drag requires an existing group member and four controls");
    const double directionLength =
        std::sqrt(rayDirectionX * rayDirectionX + rayDirectionY * rayDirectionY + rayDirectionZ * rayDirectionZ);
    if (!std::isfinite(directionLength) || directionLength < 1e-9)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(EditorStatus::Rejected,
                                                                  RuleId("editor.building.curve-drag-ray-invalid"),
                                                                  "Curve drag requires a finite non-zero pointer ray");
    const std::array<double, 3>  origin{rayOriginX, rayOriginY, rayOriginZ};
    const std::array<double, 3>  direction{rayDirectionX / directionLength, rayDirectionY / directionLength,
                                          rayDirectionZ / directionLength};
    const editing::GizmoSnapshot gizmo =
        target_->edgeCubicBezierGizmo(buildingId_, controls_, subdivisions_, 0, memberInstanceId_, surfaceName_);
    double closest = std::numeric_limits<double>::infinity();
    for (const editing::GizmoPrimitive& primitive : gizmo.primitives) {
        if (primitive.kind != "sphere" || primitive.id.rfind("curve.control.", 0) != 0) continue;
        const std::array<double, 3> offset{origin[0] - primitive.position[0], origin[1] - primitive.position[1],
                                           origin[2] - primitive.position[2]};
        const double                b = offset[0] * direction[0] + offset[1] * direction[1] + offset[2] * direction[2];
        const double                c =
            offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2] - primitive.radius * primitive.radius;
        const double discriminant = b * b - c;
        if (discriminant < 0.0) continue;
        const double nearHit = -b - std::sqrt(discriminant);
        const double farHit  = -b + std::sqrt(discriminant);
        const double hit     = nearHit >= 0.0 ? nearHit : farHit;
        if (hit < 0.0 || hit >= closest) continue;
        closest                  = hit;
        activeControlPointIndex_ = std::stoi(primitive.id.substr(14));
        dragPlanePoint_          = primitive.position;
    }
    if (activeControlPointIndex_ < 0)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(EditorStatus::NotFound,
                                                                  RuleId("editor.building.curve-control-not-hit"),
                                                                  "Pointer ray did not hit a curve control handle");
    dragPlaneNormal_  = direction;
    baseRevision_     = target_->revision();
    dragging_         = true;
    lastPreviewValid_ = gizmo.status == EditorStatus::Applied;
    return previewCurrent();
}

Result<BuildingEdgeCurveDragPreview> BuildingEdgeCurveDragSession::updateDrag(double rayOriginX, double rayOriginY,
                                                                              double rayOriginZ, double rayDirectionX,
                                                                              double rayDirectionY,
                                                                              double rayDirectionZ) {
    if (!dragging_ || !target_)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(
            EditorStatus::Rejected, RuleId("editor.building.curve-drag-not-active"), "Curve drag has not begun");
    if (target_->revision() != baseRevision_) {
        cancelDrag();
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(
            EditorStatus::Conflict, RuleId("editor.building.curve-drag-stale"),
            "Curve world changed while the control handle was being dragged");
    }
    const double length =
        std::sqrt(rayDirectionX * rayDirectionX + rayDirectionY * rayDirectionY + rayDirectionZ * rayDirectionZ);
    if (!std::isfinite(length) || length < 1e-9)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(EditorStatus::Rejected,
                                                                  RuleId("editor.building.curve-drag-ray-invalid"),
                                                                  "Curve drag requires a finite non-zero pointer ray");
    const std::array<double, 3> origin{rayOriginX, rayOriginY, rayOriginZ};
    const std::array<double, 3> direction{rayDirectionX / length, rayDirectionY / length, rayDirectionZ / length};
    const double                denominator =
        direction[0] * dragPlaneNormal_[0] + direction[1] * dragPlaneNormal_[1] + direction[2] * dragPlaneNormal_[2];
    if (std::abs(denominator) < 1e-9)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(EditorStatus::Rejected,
                                                                  RuleId("editor.building.curve-drag-ray-parallel"),
                                                                  "Pointer ray is parallel to the curve drag plane");
    const double distance = ((dragPlanePoint_[0] - origin[0]) * dragPlaneNormal_[0] +
                             (dragPlanePoint_[1] - origin[1]) * dragPlaneNormal_[1] +
                             (dragPlanePoint_[2] - origin[2]) * dragPlaneNormal_[2]) /
                            denominator;
    if (distance < 0.0)
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(EditorStatus::Rejected,
                                                                  RuleId("editor.building.curve-drag-behind-camera"),
                                                                  "Curve drag plane is behind the pointer ray origin");
    auto logical = target_->curveLogicalPointFromWorld(
        origin[0] + direction[0] * distance, origin[1] + direction[1] * distance, origin[2] + direction[2] * distance);
    if (!logical.ok())
        return eve::editing::failed<BuildingEdgeCurveDragPreview>(
            logical.code(), RuleId("editor.building.curve-drag-projection-failed"),
            "Pointer ray could not be projected to logical curve coordinates");
    controls_[static_cast<size_t>(activeControlPointIndex_)] = logical.value();
    auto preview                                             = previewCurrent();
    if (preview.ok()) lastPreviewValid_ = preview.value().status == EditorStatus::Applied;
    return preview;
}

Result<DomainOperation> BuildingEdgeCurveDragSession::finishDrag() {
    if (!dragging_ || !target_)
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building.curve-drag-not-active"), "Curve drag has not begun");
    if (target_->revision() != baseRevision_) {
        cancelDrag();
        return eve::editing::failed<DomainOperation>(EditorStatus::Conflict, RuleId("editor.building.curve-drag-stale"),
                                                     "Curve world changed before drag commit");
    }
    if (!lastPreviewValid_)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                                                     RuleId("editor.building.curve-drag-preview-invalid"),
                                                     "The last curve drag preview is not valid for commit");
    auto operation = target_->makeUpdateEdgeCubicBezier(memberInstanceId_, controls_, subdivisions_);
    if (!operation.ok()) return operation;
    cancelDrag();
    return operation;
}

void BuildingEdgeCurveDragSession::cancelDrag() {
    dragging_                = false;
    activeControlPointIndex_ = -1;
    lastPreviewValid_        = false;
}


}  // namespace eve::building_editing
