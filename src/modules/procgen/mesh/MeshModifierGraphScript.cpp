#include "procgen/mesh/MeshModifierGraphScript.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "image/ImageData.h"
#include "procgen/mesh/DynamicMeshUvPaintSession.h"
#include "procgen/mesh/GeometryStroke.h"
#include "procgen/mesh/MeshDeformationSession.h"
#include "procgen/mesh/MeshModifierGraph.h"
#include "procgen/spline/SplinePath.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <memory>

namespace eve::procgen {

void exposeMeshModifierGraph(ssq::Table& table) {
    auto dynamicPaint = table.addClass<DynamicMeshUvPaintSession>(
        "ProcgenDynamicMeshUvPaintSession",
        std::function<DynamicMeshUvPaintSession*()>([]() -> DynamicMeshUvPaintSession* { return nullptr; }), true);
    dynamicPaint.addFunc("initialize", [vm = dynamicPaint.getHandle()](DynamicMeshUvPaintSession* self,
                                                                        MeshBuild* mesh, image::ImageData* pixels) {
        if (!mesh || !pixels)
            return eve::script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "initialize requires mesh and ImageData", "input", {},
                "procgen.dynamicMeshUvPaint.script")));
        return eve::script::projectResult(vm, self->initializeResult(*mesh, *pixels));
    });
    dynamicPaint.addFunc("updateMesh", [vm = dynamicPaint.getHandle()](DynamicMeshUvPaintSession* self,
                                                                         MeshBuild* mesh) {
        if (!mesh)
            return eve::script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "updateMesh requires a mesh", "mesh", {},
                "procgen.dynamicMeshUvPaint.script")));
        return eve::script::projectResult(vm, self->updateMeshResult(*mesh));
    });
    dynamicPaint.addFunc("paintSurfacePoint",
                         [vm = dynamicPaint.getHandle()](DynamicMeshUvPaintSession* self, int triangle, float x,
                                                         float y, float z, float radius, float r, float g, float b,
                                                         float a, bool wrapU, bool wrapV) {
        return eve::script::projectResult(vm, self->paintSurfacePointResult(triangle, x, y, z, radius, r, g, b, a,
                                                                            wrapU, wrapV));
    });
    dynamicPaint.addFunc("undo", [vm = dynamicPaint.getHandle()](DynamicMeshUvPaintSession* self) {
        return eve::script::projectResult(vm, self->undoResult());
    });
    dynamicPaint.addFunc("currentImageResult", [vm = dynamicPaint.getHandle()](DynamicMeshUvPaintSession* self) {
        auto result = self->currentImageResult();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<image::ImageData>(vm, std::move(result).takeValue());
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    dynamicPaint.addFunc("getMeshRevision", [](DynamicMeshUvPaintSession* self) { return self->meshRevision(); });
    dynamicPaint.addFunc("getPaintRevision", [](DynamicMeshUvPaintSession* self) { return self->paintRevision(); });
    auto sample = table.addClass<SplineSample>("ProcgenSplineSample", ssq::Class::Ctor<SplineSample()>());
    sample.addFunc("getX", [](SplineSample* self) { return self->x; });
    sample.addFunc("getY", [](SplineSample* self) { return self->y; });
    sample.addFunc("getZ", [](SplineSample* self) { return self->z; });
    sample.addFunc("getTangentX", [](SplineSample* self) { return self->tangentX; });
    sample.addFunc("getTangentY", [](SplineSample* self) { return self->tangentY; });
    sample.addFunc("getTangentZ", [](SplineSample* self) { return self->tangentZ; });
    sample.addFunc("getNormalizedDistance", [](SplineSample* self) { return self->normalizedDistance; });
    sample.addFunc("getRollDegrees", [](SplineSample* self) { return self->rollDegrees; });
    sample.addFunc("getScaleX", [](SplineSample* self) { return self->scaleX; });
    sample.addFunc("getScaleY", [](SplineSample* self) { return self->scaleY; });
    sample.addFunc("getChunkIndex", [](SplineSample* self) { return self->chunkIndex; });
    sample.addFunc("getPitchDegrees", [](SplineSample* self) { return self->pitchDegrees; });
    sample.addFunc("getYawDegrees", [](SplineSample* self) { return self->yawDegrees; });

    auto frame = table.addClass<SplineFrameSample>("ProcgenSplineFrameSample", ssq::Class::Ctor<SplineFrameSample()>());
    frame.addFunc("getX", [](SplineFrameSample* self) { return self->sample.x; });
    frame.addFunc("getY", [](SplineFrameSample* self) { return self->sample.y; });
    frame.addFunc("getZ", [](SplineFrameSample* self) { return self->sample.z; });
    frame.addFunc("getTangentX", [](SplineFrameSample* self) { return self->sample.tangentX; });
    frame.addFunc("getTangentY", [](SplineFrameSample* self) { return self->sample.tangentY; });
    frame.addFunc("getTangentZ", [](SplineFrameSample* self) { return self->sample.tangentZ; });
    frame.addFunc("getSideX", [](SplineFrameSample* self) { return self->sideX; });
    frame.addFunc("getSideY", [](SplineFrameSample* self) { return self->sideY; });
    frame.addFunc("getSideZ", [](SplineFrameSample* self) { return self->sideZ; });
    frame.addFunc("getUpX", [](SplineFrameSample* self) { return self->upX; });
    frame.addFunc("getUpY", [](SplineFrameSample* self) { return self->upY; });
    frame.addFunc("getUpZ", [](SplineFrameSample* self) { return self->upZ; });
    frame.addFunc("getForwardX", [](SplineFrameSample* self) { return self->forwardX; });
    frame.addFunc("getForwardY", [](SplineFrameSample* self) { return self->forwardY; });
    frame.addFunc("getForwardZ", [](SplineFrameSample* self) { return self->forwardZ; });
    frame.addFunc("getNormalizedDistance", [](SplineFrameSample* self) { return self->sample.normalizedDistance; });
    frame.addFunc("getRollDegrees", [](SplineFrameSample* self) { return self->sample.rollDegrees; });
    frame.addFunc("getScaleX", [](SplineFrameSample* self) { return self->sample.scaleX; });
    frame.addFunc("getScaleY", [](SplineFrameSample* self) { return self->sample.scaleY; });

    auto distribution = table.addClass<SplineDistribution>(
        "ProcgenSplineDistribution",
        std::function<SplineDistribution*()>([]() -> SplineDistribution* { return nullptr; }), true);
    distribution.addFunc("getCount", [](SplineDistribution* self) { return self->count(); });
    distribution.addFunc("getSampleResult", [vm = distribution.getHandle()](SplineDistribution* self, int index) {
        auto result = self->sampleResult(index);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<SplineSample>(
            vm, std::make_unique<SplineSample>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    distribution.addFunc("getFrameResult", [vm = distribution.getHandle()](SplineDistribution* self, int index) {
        auto result = self->frameResult(index);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<SplineFrameSample>(
            vm, std::make_unique<SplineFrameSample>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });

    auto polyline = table.addClass<SplinePolyline>(
        "ProcgenSplinePolyline", std::function<SplinePolyline*()>([]() -> SplinePolyline* { return nullptr; }), true);
    polyline.addFunc("getCount", [](SplinePolyline* self) { return self->count(); });
    polyline.addFunc("isClosed", [](SplinePolyline* self) { return self->isClosed(); });
    polyline.addFunc("getChunkCount", [](SplinePolyline* self) { return self->chunkCount(); });
    polyline.addFunc("getChunkPointCount",
                     [](SplinePolyline* self, int chunk) { return self->chunkPointCount(chunk); });
    polyline.addFunc("getPointResult", [vm = polyline.getHandle()](SplinePolyline* self, int index) {
        auto result = self->pointResult(index);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<SplineSample>(
            vm, std::make_unique<SplineSample>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    polyline.addFunc("getChunkPointResult", [vm = polyline.getHandle()](SplinePolyline* self, int chunk, int index) {
        auto result = self->chunkPointResult(chunk, index);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<SplineSample>(
            vm, std::make_unique<SplineSample>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });

    auto spline = table.addClass<SplinePath>(
        "ProcgenSplinePath", std::function<SplinePath*()>([]() -> SplinePath* { return nullptr; }), true);
    const auto projectSplineVoid = [vm = spline.getHandle()](Result<void> result) {
        return eve::script::projectResult(vm, std::move(result));
    };
    const auto projectSample = [vm = spline.getHandle()](Result<SplineSample> result) {
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<SplineSample>(
            vm, std::make_unique<SplineSample>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    };
    spline.addFunc("setKind", [projectSplineVoid](SplinePath* self, const std::string& kind) mutable {
        return projectSplineVoid(self->setKindResult(kind));
    });
    spline.addFunc("setClosed", [](SplinePath* self, bool closed) { self->setClosed(closed); });
    spline.addFunc("addPoint", [projectSplineVoid](SplinePath* self, float x, float y, float z, float inX, float inY,
                                                   float inZ, float outX, float outY, float outZ) mutable {
        return projectSplineVoid(self->addPointResult({x, y, z, inX, inY, inZ, outX, outY, outZ}));
    });
    spline.addFunc("setPoint", [projectSplineVoid](SplinePath* self, int index, float x, float y, float z, float inX,
                                                   float inY, float inZ, float outX, float outY, float outZ) mutable {
        return projectSplineVoid(self->setPointResult(index, {x, y, z, inX, inY, inZ, outX, outY, outZ}));
    });
    spline.addFunc("setPointProfile", [projectSplineVoid](SplinePath* self, int index, float rollDegrees, float scaleX,
                                                          float scaleY) mutable {
        return projectSplineVoid(self->setPointProfileResult(index, rollDegrees, scaleX, scaleY));
    });
    spline.addFunc("setPointChunkBreak", [projectSplineVoid](SplinePath* self, int index, bool disconnected) mutable {
        return projectSplineVoid(self->setPointChunkBreakResult(index, disconnected));
    });
    spline.addFunc("setPointRotation", [projectSplineVoid](SplinePath* self, int index, float pitchDegrees,
                                                           float yawDegrees, float rollDegrees) mutable {
        return projectSplineVoid(self->setPointRotationResult(index, pitchDegrees, yawDegrees, rollDegrees));
    });
    spline.addFunc("removePoint", [projectSplineVoid](SplinePath* self, int index) mutable {
        return projectSplineVoid(self->removePointResult(index));
    });
    spline.addFunc("clear", [](SplinePath* self) { self->clear(); });
    spline.addFunc("evaluateResult", [projectSample](SplinePath* self, float t) mutable {
        return projectSample(self->evaluateResult(t));
    });
    spline.addFunc("evaluateDistanceResult",
                   [projectSample](SplinePath* self, float distance, int samplesPerSegment) mutable {
                       return projectSample(self->evaluateDistanceResult(distance, samplesPerSegment));
                   });
    spline.addFunc("closestPointResult",
                   [projectSample](SplinePath* self, float x, float y, float z, int samplesPerSegment) mutable {
                       return projectSample(self->closestPointResult(x, y, z, samplesPerSegment));
                   });
    spline.addFunc("travelResult", [projectSample](SplinePath* self, float distance, const std::string& wrapMode,
                                                   int samplesPerSegment) mutable {
        return projectSample(self->travelResult(distance, wrapMode, samplesPerSegment));
    });
    spline.addFunc("travelFrameResult", [vm = spline.getHandle()](SplinePath* self, float distance,
                                                                  const std::string& wrapMode, int samplesPerSegment) {
        auto result = self->travelFrameResult(distance, wrapMode, samplesPerSegment);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<SplineFrameSample>(
            vm, std::make_unique<SplineFrameSample>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    spline.addFunc("distributeResult",
                   [vm = spline.getHandle()](SplinePath* self, int count, bool includeEnd, int samplesPerSegment) {
                       auto result = self->distributeResult(count, includeEnd, samplesPerSegment);
                       if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
                       auto instance = eve::script::makeOwnedSquirrelInstance<SplineDistribution>(
                           vm, std::make_unique<SplineDistribution>(std::move(result).takeValue()));
                       if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
                       auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
                       projected.set("value", std::move(instance).takeValue());
                       return projected;
                   });
    spline.addFunc("polylineResult", [vm = spline.getHandle()](SplinePath* self, int sampleCount,
                                                               bool uniformByDistance, int samplesPerSegment) {
        auto result = self->polylineResult(sampleCount, uniformByDistance, samplesPerSegment);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<SplinePolyline>(
            vm, std::make_unique<SplinePolyline>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    spline.addFunc("applyShapePreset", [projectSplineVoid](SplinePath* self, const std::string& preset, int pointCount,
                                                           float radius, float height, float turns) mutable {
        return projectSplineVoid(self->applyShapePresetResult(preset, pointCount, radius, height, turns));
    });
    spline.addFunc("lengthResult", [vm = spline.getHandle()](SplinePath* self, int samplesPerSegment) {
        auto result = self->lengthResult(samplesPerSegment);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", result.value());
        return projected;
    });
    spline.addFunc("getPointCount", [](SplinePath* self) { return self->pointCount(); });
    spline.addFunc("getSegmentCount", [](SplinePath* self) { return self->segmentCount(); });
    spline.addFunc("getChunkCount", [](SplinePath* self) { return self->chunkCount(); });
    spline.addFunc("getKind", [](SplinePath* self) { return std::string(self->kind()); });
    spline.addFunc("isClosed", [](SplinePath* self) { return self->isClosed(); });
    spline.addFunc("getRevision", [](SplinePath* self) { return self->revision(); });

    auto stroke = table.addClass<GeometryStroke>("ProcgenGeometryStroke", ssq::Class::Ctor<GeometryStroke()>());
    const auto projectStrokeVoid = [vm = stroke.getHandle()](Result<void> result) {
        return eve::script::projectResult(vm, std::move(result));
    };
    stroke.addFunc("setShape", [projectStrokeVoid](GeometryStroke* self, const std::string& shape) mutable {
        return projectStrokeVoid(self->setShapeResult(shape));
    });
    stroke.addFunc("setInputSpace", [projectStrokeVoid](GeometryStroke* self, const std::string& inputSpace,
                                                         float planeY) mutable {
        return projectStrokeVoid(self->setInputSpaceResult(inputSpace, planeY));
    });
    stroke.addFunc("setSize", [projectStrokeVoid](GeometryStroke* self, float width, float depth) mutable {
        return projectStrokeVoid(self->setSizeResult(width, depth));
    });
    stroke.addFunc("setMinimumSpacing", [projectStrokeVoid](GeometryStroke* self, float spacing) mutable {
        return projectStrokeVoid(self->setMinimumSpacingResult(spacing));
    });
    stroke.addFunc("addPoint", [vm = stroke.getHandle()](GeometryStroke* self, float x, float y, float z) {
        auto result = self->addPointResult(x, y, z);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", result.value());
        return projected;
    });
    stroke.addFunc("undo", [projectStrokeVoid](GeometryStroke* self) mutable {
        return projectStrokeVoid(self->undoResult());
    });
    stroke.addFunc("clear", [](GeometryStroke* self) { self->clear(); });
    stroke.addFunc("buildMeshResult", [vm = stroke.getHandle()](GeometryStroke* self) {
        auto result = self->buildMeshResult();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    stroke.addFunc("getPointCount", [](GeometryStroke* self) { return self->pointCount(); });
    stroke.addFunc("getShape", [](GeometryStroke* self) { return std::string(self->shape()); });
    stroke.addFunc("getInputSpace", [](GeometryStroke* self) { return std::string(self->inputSpace()); });
    stroke.addFunc("getRevision", [](GeometryStroke* self) { return self->revision(); });

    auto session = table.addClass<MeshDeformationSession>(
        "ProcgenMeshDeformationSession",
        std::function<MeshDeformationSession*()>([]() -> MeshDeformationSession* { return nullptr; }), true);
    const auto projectSessionVoid = [vm = session.getHandle()](Result<void> result) {
        return eve::script::projectResult(vm, std::move(result));
    };
    session.addFunc("initialize", [projectSessionVoid](MeshDeformationSession* self, MeshBuild* mesh) mutable {
        if (!mesh)
            return projectSessionVoid(
                Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "initialize requires a mesh",
                                                        "mesh", {}, "procgen.meshDeformationSession.script")));
        return projectSessionVoid(self->initializeResult(*mesh));
    });
    session.addFunc("applyBrush",
                    [projectSessionVoid](MeshDeformationSession* self, const std::string& mode, float x, float y,
                                         float z, float radius, float strength, float falloff) mutable {
                        return projectSessionVoid(self->applyBrushResult(mode, x, y, z, radius, strength, falloff));
                    });
    session.addFunc("applyBrushGpu",
                    [projectSessionVoid](MeshDeformationSession* self, const std::string& mode, float x, float y,
                                         float z, float radius, float strength, float falloff) mutable {
                        return projectSessionVoid(self->applyBrushGpuResult(mode, x, y, z, radius, strength, falloff));
                    });
    session.addFunc(
        "applyDirectionalBrush",
        [projectSessionVoid](MeshDeformationSession* self, float x, float y, float z, float radius, float strength,
                             float falloff, float directionX, float directionY, float directionZ) mutable {
            return projectSessionVoid(self->applyBrushResult("directional", x, y, z, radius, strength, falloff,
                                                             directionX, directionY, directionZ));
        });
    session.addFunc("applyImpact",
                    [projectSessionVoid](MeshDeformationSession* self, float x, float y, float z, float impulseX,
                                         float impulseY, float impulseZ, float radius, float plasticity, float hardness,
                                         float maxDisplacement) mutable {
                        return projectSessionVoid(self->applyImpactResult(x, y, z, impulseX, impulseY, impulseZ, radius,
                                                                          plasticity, hardness, maxDisplacement));
                    });
    session.addFunc("applyImpactGpu",
                    [projectSessionVoid](MeshDeformationSession* self, float x, float y, float z, float impulseX,
                                         float impulseY, float impulseZ, float radius, float plasticity, float hardness,
                                         float maxDisplacement) mutable {
                        return projectSessionVoid(self->applyImpactGpuResult(x, y, z, impulseX, impulseY, impulseZ,
                                                                             radius, plasticity, hardness,
                                                                             maxDisplacement));
                    });
    session.addFunc("prepareImpactVertexBlocks",
                    [projectSessionVoid](MeshDeformationSession* self, int divisionsPerAxis) mutable {
                        return projectSessionVoid(self->prepareImpactVertexBlocksResult(divisionsPerAxis));
                    });
    session.addFunc("getImpactVertexBlockCount",
                    [](MeshDeformationSession* self) { return self->impactVertexBlockCount(); });
    session.addFunc("hasImpactVertexBlocks",
                    [](MeshDeformationSession* self) { return self->hasImpactVertexBlocks(); });
    session.addFunc(
        "applySurfaceContact",
        [projectSessionVoid](MeshDeformationSession* self, float x, float y, float z, float normalX, float normalY,
                             float normalZ, float velocityX, float velocityY, float velocityZ, float radius,
                             float depth, float drag, float falloff, float plasticity) mutable {
            return projectSessionVoid(self->applySurfaceContactResult(x, y, z, normalX, normalY, normalZ, velocityX,
                                                                      velocityY, velocityZ, radius, depth, drag,
                                                                      falloff, plasticity));
        });
    session.addFunc("recoverSurface",
                    [projectSessionVoid](MeshDeformationSession* self, float dt, float recoveryRate) mutable {
                        return projectSessionVoid(self->recoverSurfaceResult(dt, recoveryRate));
                    });
    session.addFunc("applySlimeImpulse",
                    [projectSessionVoid](MeshDeformationSession* self, float x, float y, float z, float impulseX,
                                         float impulseY, float impulseZ, float radius, float falloff) mutable {
                        return projectSessionVoid(
                            self->applySlimeImpulseResult(x, y, z, impulseX, impulseY, impulseZ, radius, falloff));
                    });
    session.addFunc("stepSlime", [projectSessionVoid](MeshDeformationSession* self, float dt, float stiffness,
                                                      float damping, float maxSpeed) mutable {
        return projectSessionVoid(self->stepSlimeResult(dt, stiffness, damping, maxSpeed));
    });
    session.addFunc(
        "configureColliderRefresh",
        [projectSessionVoid](MeshDeformationSession* self, const std::string& mode, float interval, float offsetX,
                             float offsetY, float offsetZ) mutable {
            return projectSessionVoid(self->configureColliderRefreshResult(mode, interval, offsetX, offsetY, offsetZ));
        });
    session.addFunc("requestColliderRefresh", [projectSessionVoid](MeshDeformationSession* self) mutable {
        return projectSessionVoid(self->requestColliderRefreshResult());
    });
    session.addFunc("updateColliderRefresh", [vm = session.getHandle()](MeshDeformationSession* self, float dt) {
        return eve::script::projectResult(vm, self->updateColliderRefreshResult(dt),
                                          [](bool value) { return eve::Value(value); });
    });
    session.addFunc("colliderMeshResult", [vm = session.getHandle()](MeshDeformationSession* self) {
        auto result = self->colliderMeshResult();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    session.addFunc("selectVerticesSphere", [vm = session.getHandle()](MeshDeformationSession* self, float x, float y,
                                                                       float z, float radius, bool replace) {
        return eve::script::projectResult(vm, self->selectVerticesSphereResult(x, y, z, radius, replace),
                                          [](int value) { return eve::Value(value); });
    });
    session.addFunc("selectVerticesBox", [vm = session.getHandle()](MeshDeformationSession* self, float minX,
                                                                    float minY, float minZ, float maxX, float maxY,
                                                                    float maxZ, bool replace) {
        return eve::script::projectResult(vm,
                                          self->selectVerticesBoxResult(minX, minY, minZ, maxX, maxY, maxZ, replace),
                                          [](int value) { return eve::Value(value); });
    });
    session.addFunc("clearVertexSelection", [](MeshDeformationSession* self) { self->clearVertexSelection(); });
    session.addFunc("getSelectedVertexCount", [](MeshDeformationSession* self) { return self->selectedVertexCount(); });
    session.addFunc("moveSelectedVertices",
                    [projectSessionVoid](MeshDeformationSession* self, float x, float y, float z) mutable {
                        return projectSessionVoid(self->moveSelectedVerticesResult(x, y, z));
                    });
    session.addFunc("manipulateSelectedVertices",
                    [projectSessionVoid](MeshDeformationSession* self, const std::string& mode, float originX,
                                         float originY, float originZ, float directionX, float directionY,
                                         float directionZ, float distance) mutable {
                        return projectSessionVoid(self->manipulateSelectedVerticesResult(
                            mode, originX, originY, originZ, directionX, directionY, directionZ, distance));
                    });
    session.addFunc("restore", [projectSessionVoid](MeshDeformationSession* self) mutable {
        return projectSessionVoid(self->restoreResult());
    });
    session.addFunc("bake", [projectSessionVoid](MeshDeformationSession* self) mutable {
        return projectSessionVoid(self->bakeResult());
    });
    session.addFunc("undo", [projectSessionVoid](MeshDeformationSession* self) mutable {
        return projectSessionVoid(self->undoResult());
    });
    session.addFunc("currentMeshResult", [vm = session.getHandle()](MeshDeformationSession* self) {
        auto result = self->currentMeshResult();
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    session.addFunc("getRevision", [](MeshDeformationSession* self) { return self->revision(); });
    session.addFunc("getUndoCount", [](MeshDeformationSession* self) { return self->undoCount(); });
    session.addFunc("isInitialized", [](MeshDeformationSession* self) { return self->isInitialized(); });

    auto graph = table.addClass<MeshModifierGraph>(
        "ProcgenMeshModifierGraph", std::function<MeshModifierGraph*()>([]() -> MeshModifierGraph* { return nullptr; }),
        true);
    const auto projectVoid = [vm = graph.getHandle()](Result<void> result) {
        return eve::script::projectResult(vm, std::move(result));
    };
    graph.addFunc("addNode",
                  [projectVoid](MeshModifierGraph* self, const std::string& id, const std::string& operation) mutable {
                      return projectVoid(self->addNode(id, operation));
                  });
    graph.addFunc("removeNode", [projectVoid](MeshModifierGraph* self, const std::string& id) mutable {
        return projectVoid(self->removeNode(id));
    });
    graph.addFunc("connect", [projectVoid](MeshModifierGraph* self, const std::string& from, const std::string& to,
                                           int input) mutable { return projectVoid(self->connect(from, to, input)); });
    graph.addFunc("disconnect", [projectVoid](MeshModifierGraph* self, const std::string& to, int input) mutable {
        return projectVoid(self->disconnect(to, input));
    });
    graph.addFunc(
        "setNodeMesh", [projectVoid](MeshModifierGraph* self, const std::string& id, MeshBuild* mesh) mutable {
            if (!mesh)
                return projectVoid(Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "setNodeMesh requires a mesh", "mesh", {},
                                                                           "procgen.meshModifierGraph.script")));
            return projectVoid(self->setNodeMesh(id, *mesh));
        });
    graph.addFunc(
        "setNodeSplinePath", [projectVoid](MeshModifierGraph* self, const std::string& id, SplinePath* path) mutable {
            if (!path)
                return projectVoid(Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                           "setNodeSplinePath requires a path", "path",
                                                                           {}, "procgen.meshModifierGraph.script")));
            return projectVoid(self->setNodeSplinePath(id, *path));
        });
    graph.addFunc("setNodeSplineProfile", [projectVoid](MeshModifierGraph* self, const std::string& id,
                                                        ssq::Array coordinates, bool closed) mutable {
        if (coordinates.size() % 2u != 0u)
            return projectVoid(Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "profile coordinates require x/y pairs",
                                  "coordinates", {}, "procgen.meshModifierGraph.script")));
        SplineProfile profile;
        profile.closed = closed;
        profile.points.reserve(coordinates.size() / 2u);
        for (std::size_t i = 0; i < coordinates.size(); i += 2u)
            profile.points.push_back({coordinates.get<float>(i), coordinates.get<float>(i + 1u)});
        return projectVoid(self->setNodeSplineProfile(id, profile));
    });
    graph.addFunc("setNodeFloat",
                  [projectVoid](MeshModifierGraph* self, const std::string& id, const std::string& key,
                                float value) mutable { return projectVoid(self->setNodeFloat(id, key, value)); });
    graph.addFunc("setNodeInt",
                  [projectVoid](MeshModifierGraph* self, const std::string& id, const std::string& key,
                                int value) mutable { return projectVoid(self->setNodeInt(id, key, value)); });
    graph.addFunc("setNodeString", [projectVoid](MeshModifierGraph* self, const std::string& id, const std::string& key,
                                                 const std::string& value) mutable {
        return projectVoid(self->setNodeString(id, key, value));
    });
    graph.addFunc("validateResult", [vm = graph.getHandle()](MeshModifierGraph* self) {
        return eve::script::projectResult(vm, self->validateResult());
    });
    graph.addFunc("executeResult", [vm = graph.getHandle()](MeshModifierGraph* self, const std::string& output) {
        auto result = self->executeResult(output);
        if (!result.ok()) return eve::script::projectStatusResult(vm, result.status(), false, false);
        auto instance = eve::script::makeOwnedSquirrelInstance<MeshBuild>(
            vm, std::make_unique<MeshBuild>(std::move(result).takeValue()));
        if (!instance.ok()) return eve::script::projectStatusResult(vm, instance.status(), false, false);
        auto projected = eve::script::projectStatusResult(vm, Status::success(), true, true);
        projected.set("value", std::move(instance).takeValue());
        return projected;
    });
    graph.addFunc("clearCache", [](MeshModifierGraph* self) { self->clearCache(); });
    graph.addFunc("getRevision", [](MeshModifierGraph* self) { return self->revision(); });
    graph.addFunc("getExecutionPlanBuildCount",
                  [](MeshModifierGraph* self) { return self->executionPlanBuildCount(); });
    graph.addFunc("getCompiledSegmentCount", [](MeshModifierGraph* self) { return self->compiledSegmentCount(); });
    graph.addFunc("getFusedOperationCount", [](MeshModifierGraph* self) { return self->fusedOperationCount(); });
    graph.addFunc("hasNode", [](MeshModifierGraph* self, const std::string& id) { return self->hasNode(id); });
    graph.addFunc("getNodeCount", [](MeshModifierGraph* self) { return self->nodeCount(); });
    graph.addFunc("getNodeId", [](MeshModifierGraph* self, int index) { return self->nodeId(index); });
    graph.addFunc("getNodeOperation",
                  [](MeshModifierGraph* self, const std::string& id) { return self->nodeOperation(id); });
    graph.addFunc("getOperationCount", [](MeshModifierGraph*) { return MeshModifierGraph::operationCount(); });
    graph.addFunc("getOperationId",
                  [](MeshModifierGraph*, int index) { return MeshModifierGraph::operationId(index); });
    graph.addFunc("getOperationInputCount", [](MeshModifierGraph*, const std::string& operation) {
        return MeshModifierGraph::operationInputCount(operation);
    });
    graph.addFunc("getOperationParamCount", [](MeshModifierGraph*, const std::string& operation) {
        return MeshModifierGraph::operationParamCount(operation);
    });
    graph.addFunc("getOperationParamKey", [](MeshModifierGraph*, const std::string& operation, int index) {
        return MeshModifierGraph::operationParamKey(operation, index);
    });
    graph.addFunc("getOperationParamKind", [](MeshModifierGraph*, const std::string& operation, int index) {
        return MeshModifierGraph::operationParamKind(operation, index);
    });
    graph.addFunc("getOperationParamDefault", [](MeshModifierGraph*, const std::string& operation, int index) {
        return MeshModifierGraph::operationParamDefault(operation, index);
    });
}

}  // namespace eve::procgen
