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
Result<DomainOperation> BuildingPlacementTarget::makeEdgeCubicBezier(
    const std::string& buildingId, const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions) const {
    std::vector<building::PlacementSystem::EdgeCurvePoint> runtimeControls;
    runtimeControls.reserve(controlPoints.size());
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        runtimeControls.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
    auto vertices = building::PlacementSystem::sampleEdgeCubicBezier(runtimeControls, subdivisions);
    if (!vertices.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected,
                                                     RuleId("editor.building.edge-curve-rejected"),
                                                     "Cubic edge curve sampling was rejected");
    std::vector<BuildingEdgePathVertex> editorVertices;
    editorVertices.reserve(vertices.value().size());
    for (const building::CornerAddress& vertex : vertices.value()) editorVertices.push_back({vertex.x, vertex.y});
    auto path = makeEdgePath(buildingId, editorVertices);
    if (!path.ok()) return path;
    auto instances = parseInstances(path.value().payload);
    if (!instances.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Failed,
                                                     RuleId("editor.building.edge-curve-encoding-failed"),
                                                     "Edge curve instances could not be encoded");
    building::EdgeCurveGroup group;
    group.id           = world_->nextEdgeCurveGroupId();
    group.buildingId   = buildingId;
    group.level        = instances.value().front().level;
    group.subdivisions = subdivisions;
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        group.controlPoints.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
    for (const BuildingInstanceSnapshot& instance : instances.value()) group.instanceIds.push_back(instance.instanceId);
    DomainOperation operation = std::move(path).takeValue();
    operation.type            = "building.edge-curve.set.v1";
    operation.inverseType     = "building.edge-curve.delete.v1";
    operation.payload         = edgeCurveValue(group, instances.value());
    operation.inverse         = operation.payload;
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

Result<DomainOperation> BuildingPlacementTarget::makeEdgeCubicBezierOnSurface(
    const std::string& buildingId, const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
    const std::string& surfaceName) const {
    if (!world_)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.building.world-required"),
                                                     "Placement world is unavailable");
    auto operation = makeEdgeCubicBezier(buildingId, controlPoints, subdivisions);
    if (!operation.ok()) return operation;
    std::vector<building::PlacementSystem::EdgeCurvePoint> controls;
    controls.reserve(controlPoints.size());
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        controls.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
    auto surface = building::PlacementSystem::sampleEdgeCurveSurface(*world_, surfaceName, controls, subdivisions);
    if (!surface.ok())
        return eve::editing::failed<DomainOperation>(
            EditorStatus::Rejected, RuleId("editor.building.edge-curve-surface-rejected"),
            surface.error() ? surface.error()->message() : "Curve could not be sampled on the requested surface");
    auto parsed = parseEdgeCurve(operation.value().payload);
    if (!parsed.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Failed,
                                                     RuleId("editor.building.edge-curve-encoding-failed"),
                                                     "Edge curve instances could not be encoded");
    parsed.value().group.surfaceProviderName = surface.value().providerName;
    parsed.value().group.surfaceId           = surface.value().surfaceId;
    parsed.value().group.surfaceRevision     = surface.value().surfaceRevision;
    parsed.value().group.surfaceSamples      = surface.value().samples;
    DomainOperation enriched                 = std::move(operation).takeValue();
    enriched.payload                         = edgeCurveValue(parsed.value().group, parsed.value().instances);
    enriched.inverse                         = enriched.payload;
    return eve::editing::applied<DomainOperation>(std::move(enriched));
}

BuildingEdgeCurvePreview BuildingPlacementTarget::previewEdgeCubicBezier(
    const std::string& buildingId, const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
    int replacingMemberInstanceId, const std::string& surfaceName) const {
    BuildingEdgeCurvePreview result;
    result.worldRevision = revisionValue();
    result.controlPoints = controlPoints;
    if (!world_) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.building.world-required"),
            DiagnosticSeverity::Error, "Placement world is unavailable"));
        return result;
    }
    std::vector<building::PlacementSystem::EdgeCurvePoint> controls;
    for (const BuildingEdgeCurvePoint& point : controlPoints)
        controls.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
    auto sampled = building::PlacementSystem::sampleEdgeCubicBezier(controls, subdivisions);
    if (!sampled.ok()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, RuleId("editor.building.edge-curve-invalid"),
            DiagnosticSeverity::Error, "Cubic edge curve controls or subdivisions are invalid"));
        return result;
    }
    for (const building::CornerAddress& vertex : sampled.value())
        result.sampledVertices.push_back({vertex.x, vertex.y});
    std::unique_ptr<building::PlacementWorld> previewOwner;
    building::PlacementWorld*                 previewWorld = world_;
    if (replacingMemberInstanceId > 0) {
        auto replacingGroup = world_->edgeCurveGroupForInstance(replacingMemberInstanceId);
        if (!replacingGroup.ok()) {
            result.status = EditorStatus::NotFound;
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::NotFound, RuleId("editor.building.edge-curve-group-not-found"),
                DiagnosticSeverity::Error, "Replacement preview instance does not belong to a curve group"));
            return result;
        }
        previewOwner = world_->cloneState();
        for (int instanceId : replacingGroup.value().instanceIds)
            building::PlacementSystem::removeBuilding(previewOwner.get(), instanceId);
        previewWorld = previewOwner.get();
    }
    auto preview = building::PlacementSystem::previewEdgeCubicBezier(previewWorld, buildingId, controls, subdivisions);
    if (!preview.ok()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            preview.error() ? preview.error()->code() : eve::DiagnosticCode::PreconditionViolation,
            RuleId("editor.building.edge-curve-rejected"), DiagnosticSeverity::Error,
            preview.error() ? preview.error()->message() : "Cubic edge curve preview was rejected"));
        return result;
    }
    for (const building::EdgeAddress& edge : preview.value().edges)
        result.edges.push_back(
            {edge.x, edge.y, edge.axis == building::EdgeAxis::Horizontal ? "horizontal" : "vertical"});
    if (!surfaceName.empty()) {
        auto surface = building::PlacementSystem::sampleEdgeCurveSurface(*world_, surfaceName, controls, subdivisions);
        if (!surface.ok()) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                surface.error() ? surface.error()->code() : eve::DiagnosticCode::PreconditionViolation,
                RuleId("editor.building.edge-curve-surface-rejected"), DiagnosticSeverity::Error,
                surface.error() ? surface.error()->message() : "Curve surface preview was rejected"));
            return result;
        }
        result.surfaceProviderName = surface.value().providerName;
        result.surfaceId           = surface.value().surfaceId;
        result.surfaceRevision     = surface.value().surfaceRevision;
        for (const building::EdgeCurveSurfaceSample& sample : surface.value().samples)
            result.surfaceSamples.push_back(
                {sample.worldX, sample.worldY, sample.worldZ, sample.normalX, sample.normalY, sample.normalZ});
    }
    result.status = EditorStatus::Applied;
    return result;
}

editing::GizmoSnapshot BuildingPlacementTarget::edgeCubicBezierGizmo(
    const std::string& buildingId, const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions,
    int level, int replacingMemberInstanceId, const std::string& surfaceName) const {
    editing::GizmoSnapshot gizmo;
    gizmo.target         = id_;
    gizmo.targetRevision = revisionValue();
    const BuildingEdgeCurvePreview preview =
        previewEdgeCubicBezier(buildingId, controlPoints, subdivisions, replacingMemberInstanceId, surfaceName);
    gizmo.status      = preview.status;
    gizmo.diagnostics = preview.diagnostics;
    if (!world_) return gizmo;
    const auto project = [&](double gx, double gy) {
        float ox = 0.f, oy = 0.f, xx = 0.f, xy = 0.f, yx = 0.f, yy = 0.f;
        world_->cellToWorldPlane(0, 0, ox, oy);
        world_->cellToWorldPlane(1, 0, xx, xy);
        world_->cellToWorldPlane(0, 1, yx, yy);
        const double px        = ox + gx * (xx - ox) + gy * (yx - ox);
        const double py        = oy + gx * (xy - oy) + gy * (yy - oy);
        const double elevation = static_cast<double>(level) * world_->getFloorHeight();
        if (!surfaceName.empty()) {
            auto hit = building::PlacementSystem::sampleSurface(*world_, surfaceName, static_cast<float>(px),
                                                                static_cast<float>(py));
            if (hit.ok()) return std::array<double, 3>{hit.value().worldX, hit.value().worldY, hit.value().worldZ};
        }
        return world_->getGridPlaneName() == "xz" ? std::array<double, 3>{px, elevation, py}
                                                  : std::array<double, 3>{px, py, elevation};
    };
    const auto addLine = [&](const std::string& id, const std::array<double, 3>& a, const std::array<double, 3>& b,
                             const std::array<double, 4>& color, bool dashed) {
        editing::GizmoPrimitive line;
        line.id   = id;
        line.kind = "line";
        for (size_t axis = 0; axis < 3; ++axis) {
            line.position[axis]  = (a[axis] + b[axis]) * 0.5;
            line.direction[axis] = b[axis] - a[axis];
        }
        line.length = std::sqrt(line.direction[0] * line.direction[0] + line.direction[1] * line.direction[1] +
                                line.direction[2] * line.direction[2]);
        if (line.length > 0.0)
            for (double& component : line.direction) component /= line.length;
        line.color  = color;
        line.dashed = dashed;
        gizmo.primitives.push_back(std::move(line));
    };
    const std::array<double, 4> accepted{0.2, 0.95, 0.35, 1.0};
    const std::array<double, 4> rejected{0.95, 0.2, 0.2, 1.0};
    const auto&                 pathColor = preview.status == EditorStatus::Applied ? accepted : rejected;
    for (size_t index = 0; index < controlPoints.size(); ++index) {
        editing::GizmoPrimitive handle;
        handle.id       = "curve.control." + std::to_string(index);
        handle.kind     = "sphere";
        handle.position = project(controlPoints[index].x, controlPoints[index].y);
        handle.radius   = world_->getCellSize() * 0.15;
        handle.color    = index == 0 || index + 1 == controlPoints.size() ? std::array<double, 4>{0.2, 0.65, 1.0, 1.0}
                                                                          : std::array<double, 4>{1.0, 0.7, 0.15, 1.0};
        gizmo.primitives.push_back(std::move(handle));
        if (index > 0)
            addLine("curve.tangent." + std::to_string(index - 1),
                    project(controlPoints[index - 1].x, controlPoints[index - 1].y),
                    project(controlPoints[index].x, controlPoints[index].y), {0.65, 0.65, 0.7, 0.8}, true);
    }
    if (!preview.surfaceSamples.empty()) {
        for (size_t index = 1; index < preview.surfaceSamples.size(); ++index) {
            const auto& a = preview.surfaceSamples[index - 1];
            const auto& b = preview.surfaceSamples[index];
            addLine("curve.path." + std::to_string(index - 1), {a.worldX, a.worldY, a.worldZ},
                    {b.worldX, b.worldY, b.worldZ}, pathColor, false);
        }
    } else {
        for (size_t index = 1; index < preview.sampledVertices.size(); ++index)
            addLine("curve.path." + std::to_string(index - 1),
                    project(preview.sampledVertices[index - 1].x, preview.sampledVertices[index - 1].y),
                    project(preview.sampledVertices[index].x, preview.sampledVertices[index].y), pathColor, false);
    }
    return gizmo;
}

Result<DomainOperation> BuildingPlacementTarget::makeUpdateEdgeCubicBezier(
    int memberInstanceId, const std::vector<BuildingEdgeCurvePoint>& controlPoints, int subdivisions) const {
    if (!world_)
        return eve::editing::failed<DomainOperation>(EditorStatus::Rejected, RuleId("editor.building.world-required"),
                                                     "Placement world is unavailable");
    auto currentGroup = world_->edgeCurveGroupForInstance(memberInstanceId);
    if (!currentGroup.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::NotFound,
                                                     RuleId("editor.building.edge-curve-group-not-found"),
                                                     "Instance does not belong to a curve group");
    std::unique_ptr<building::PlacementWorld> draftWorld = world_->cloneState();
    for (int instanceId : currentGroup.value().instanceIds)
        if (!building::PlacementSystem::removeBuilding(draftWorld.get(), instanceId))
            return eve::editing::failed<DomainOperation>(EditorStatus::Failed,
                                                         RuleId("editor.building.edge-curve-draft-delete-failed"),
                                                         "Curve update could not create an isolated draft world");
    BuildingPlacementTarget draftTarget(id_, std::move(draftWorld), revisionValue());
    auto                    desiredOperation =
        draftTarget.makeEdgeCubicBezier(currentGroup.value().buildingId, controlPoints, subdivisions);
    if (!desiredOperation.ok()) return desiredOperation;
    auto desired = parseEdgeCurve(desiredOperation.value().payload);
    if (!desired.ok())
        return eve::editing::failed<DomainOperation>(EditorStatus::Failed,
                                                     RuleId("editor.building.edge-curve-encoding-failed"),
                                                     "Updated edge curve could not be encoded");
    desired.value().group.id = currentGroup.value().id;
    if (!currentGroup.value().surfaceProviderName.empty()) {
        std::vector<building::PlacementSystem::EdgeCurvePoint> runtimeControls;
        for (const BuildingEdgeCurvePoint& point : controlPoints)
            runtimeControls.push_back({static_cast<float>(point.x), static_cast<float>(point.y)});
        auto surface = building::PlacementSystem::sampleEdgeCurveSurface(
            *world_, currentGroup.value().surfaceProviderName, runtimeControls, subdivisions);
        if (!surface.ok())
            return eve::editing::failed<DomainOperation>(
                EditorStatus::Rejected, RuleId("editor.building.edge-curve-surface-rejected"),
                "Updated curve could not be sampled on its committed surface provider");
        desired.value().group.surfaceProviderName = surface.value().providerName;
        desired.value().group.surfaceId           = surface.value().surfaceId;
        desired.value().group.surfaceRevision     = surface.value().surfaceRevision;
        desired.value().group.surfaceSamples      = surface.value().samples;
    }
    std::vector<BuildingInstanceSnapshot> beforeInstances;
    for (int instanceId : currentGroup.value().instanceIds)
        beforeInstances.push_back(snapshot(world_->buildings().at(instanceId)));
    DomainOperation operation = std::move(desiredOperation).takeValue();
    operation.type            = "building.edge-curve.replace.v1";
    operation.inverseType     = operation.type;
    operation.payload         = edgeCurveValue(desired.value().group, desired.value().instances);
    operation.inverse         = edgeCurveValue(currentGroup.value(), beforeInstances);
    operation.hasInverse      = true;
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

Result<BuildingEdgeCurvePoint> BuildingPlacementTarget::curveLogicalPointFromWorld(double worldX, double worldY,
                                                                                   double worldZ) const {
    if (!world_ || !std::isfinite(worldX) || !std::isfinite(worldY) || !std::isfinite(worldZ))
        return eve::editing::failed<BuildingEdgeCurvePoint>(
            EditorStatus::Rejected, RuleId("editor.building.curve-world-point-invalid"),
            "Curve drag requires a finite world-space point and an available world");
    float ox = 0.f, oy = 0.f, xx = 0.f, xy = 0.f, yx = 0.f, yy = 0.f;
    world_->cellToWorldPlane(0, 0, ox, oy);
    world_->cellToWorldPlane(1, 0, xx, xy);
    world_->cellToWorldPlane(0, 1, yx, yy);
    const double bxX         = xx - ox;
    const double bxY         = xy - oy;
    const double byX         = yx - ox;
    const double byY         = yy - oy;
    const double determinant = bxX * byY - bxY * byX;
    if (std::abs(determinant) < 1e-9)
        return eve::editing::failed<BuildingEdgeCurvePoint>(EditorStatus::Unsupported,
                                                            RuleId("editor.building.curve-grid-basis-singular"),
                                                            "Curve drag requires an invertible affine grid basis");
    const double           planeX = worldX;
    const double           planeY = world_->getGridPlaneName() == "xz" ? worldZ : worldY;
    const double           dx     = planeX - ox;
    const double           dy     = planeY - oy;
    BuildingEdgeCurvePoint point;
    point.x = (dx * byY - dy * byX) / determinant;
    point.y = (bxX * dy - bxY * dx) / determinant;
    return eve::editing::applied<BuildingEdgeCurvePoint>(point);
}


}  // namespace eve::building_editing
