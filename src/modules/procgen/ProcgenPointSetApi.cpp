#include "procgen/Procgen.h"
#include "procgen/ProcgenLive.h"
#include "procgen/ProcgenScriptSupport.h"
#include "procgen/ProcgenOwnership.h"

#include "common/Capability.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "common/SquirrelBinding.h"
#include "procgen/ProcgenScriptObjects.h"

#include "image/ImageData.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"

namespace eve::procgen {

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleGridHandle(int width, int depth, float spacing, uint32_t seed,
                                                                float jitter) {
    if (width <= 0 || depth <= 0 || spacing <= 0.f)
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "sampleGrid requires positive dimensions and spacing", "pointSet", {},
            "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(sampleGridPoints(width, depth, spacing, seed, jitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterHeightHandle(ProcgenPointSetHandleRef input, float minHeight,
                                                                  float maxHeight) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "filterHeight input point-set handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointHeight(*view, minHeight, maxHeight)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterDensityHandle(ProcgenPointSetHandleRef input, float minDensity,
                                                                   float maxDensity) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "filterDensity input point-set handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointDensity(*view, minDensity, maxDensity)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterBoxHandle(ProcgenPointSetHandleRef input, float minX, float minY,
                                                               float minZ, float maxX, float maxY, float maxZ,
                                                               bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "filterBox input point-set handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(
                                                    filterPointBox(*view, minX, minY, minZ, maxX, maxY, maxZ, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSlopeHandle(ProcgenPointSetHandleRef input, float minDegrees,
                                                                 float maxDegrees) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "filterSlope input point-set handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointSlope(*view, minDegrees, maxDegrees)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterPolygonHandle(ProcgenPointSetHandleRef input,
                                                                   ProcgenPointSetHandleRef polygon, bool invert) {
    auto source = resolvePointSet(input);
    auto shape  = resolvePointSet(polygon);
    if (!source.isBound() || !shape.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "filterPolygon input handle is stale", "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointsByPolygon(*source, *shape, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterSplineDistanceHandle(ProcgenPointSetHandleRef input,
                                                                          ProcgenPointSetHandleRef controlPoints,
                                                                          float minDistance, float maxDistance) {
    auto source  = resolvePointSet(input);
    auto control = resolvePointSet(controlPoints);
    if (!source.isBound() || !control.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "filterSplineDistance input handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(filterPointsBySplineDistance(
                                                    *source, *control, minDistance, maxDistance)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::excludeRadiusHandle(ProcgenPointSetHandleRef input, float x, float z,
                                                                   float radius) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "excludeRadius input point-set handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(excludePointRadius(*view, x, z, radius)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::jitterPointsHandle(ProcgenPointSetHandleRef input, uint32_t seed,
                                                                  float amountX, float amountZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "jitterPoints input point-set handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(jitterPointPositions(*view, seed, amountX, amountZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::selfPruneHandle(ProcgenPointSetHandleRef input, float radius) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "selfPrune input point-set handle is stale",
                                   "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(selfPrunePoints(*view, radius)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToHeightmapHandle(ProcgenPointSetHandleRef  input,
                                                                        ProcgenHeightmapHandleRef heightmap,
                                                                        float originX, float originZ, float cellSize,
                                                                        float heightScale) {
    auto points = resolvePointSet(input);
    auto map    = resolveHeightmap(heightmap);
    if (!points.isBound() || !map.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "projectToHeightmap input handle is stale",
                                   "input", {}, "procgen.squirrel"));
    if (cellSize <= 0.f)
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "projectToHeightmap cellSize must be positive",
                                   "cellSize", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(projectPointsToHeightmap(
                                                    *points, *map, originX, originZ, cellSize, heightScale)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::sampleSplineHandle(ProcgenPointSetHandleRef controlPoints, float spacing,
                                                                  uint32_t seed, float lateralJitter) {
    auto control = resolvePointSet(controlPoints);
    if (!control.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "sampleSpline control point-set handle is stale",
                                   "controlPoints", {}, "procgen.squirrel"));
    if (spacing <= 0.f)
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "sampleSpline spacing must be positive",
                                   "spacing", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(samplePolylinePoints(*control, spacing, seed, lateralJitter)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::mergePointsHandle(ProcgenPointSetHandleRef first,
                                                                 ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "mergePoints input handle is stale", "points", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(mergePointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::unionPointsHandle(ProcgenPointSetHandleRef first,
                                                                 ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "unionPoints requires live point sets", "points",
                                   {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(unionPointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::intersectPointsHandle(ProcgenPointSetHandleRef first,
                                                                     ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "intersectPoints requires live point sets",
                                   "points", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(intersectPointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::differencePointsHandle(ProcgenPointSetHandleRef first,
                                                                      ProcgenPointSetHandleRef second) {
    auto a = resolvePointSet(first);
    auto b = resolvePointSet(second);
    if (!a.isBound() || !b.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "differencePoints requires live point sets",
                                   "points", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(differencePointSets(*a, *b)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::transformPointsHandle(ProcgenPointSetHandleRef input, float translateX,
                                                                     float translateY, float translateZ,
                                                                     float yawDegrees, float scaleX, float scaleY,
                                                                     float scaleZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "transformPoints input handle is stale", "input",
                                   {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(transformPointSet(*view, translateX, translateY, translateZ,
                                                                         yawDegrees, scaleX, scaleY, scaleZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::transformPoints3DHandle(ProcgenPointSetHandleRef input, float translateX,
                                                                       float translateY, float translateZ,
                                                                       float pitchDegrees, float yawDegrees,
                                                                       float rollDegrees, float scaleX, float scaleY,
                                                                       float scaleZ) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "transformPoints3D input handle is stale", "input",
                                   {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(transformPointSet3D(
                                                    *view, translateX, translateY, translateZ, pitchDegrees, yawDegrees,
                                                    rollDegrees, scaleX, scaleY, scaleZ)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::copyPointsHandle(ProcgenPointSetHandleRef source,
                                                                ProcgenPointSetHandleRef targets,
                                                                bool                     inheritTargetAttributes) {
    auto sourceView = resolvePointSet(source);
    auto targetView = resolvePointSet(targets);
    if (!sourceView.isBound() || !targetView.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "copyPoints requires live point sets", "points", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(copyPointsToTargets(
                                                    *sourceView, *targetView, inheritTargetAttributes)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::remapDensityHandle(ProcgenPointSetHandleRef input, float inputMin,
                                                                  float inputMax, float outputMin, float outputMax,
                                                                  bool clampOutput) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "remapDensity point-set handle is stale", "points",
                                   {}, "procgen.squirrel"));
    if (!std::isfinite(inputMin) || !std::isfinite(inputMax) || inputMin == inputMax || !std::isfinite(outputMin) ||
        !std::isfinite(outputMax))
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "remapDensity requires finite non-zero input range", "inputRange", {},
            "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(remapPointDensity(
                                                    *view, inputMin, inputMax, outputMin, outputMax, clampOutput)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::mathFloatAttributeHandle(ProcgenPointSetHandleRef input,
                                                                        const std::string&       attribute,
                                                                        const std::string&       outputAttribute,
                                                                        const std::string& operation, float operand,
                                                                        float defaultValue) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "mathFloatAttribute point-set handle is stale",
                                   "points", {}, "procgen.squirrel"));
    if (attribute.empty() || outputAttribute.empty() ||
        (operation != "add" && operation != "subtract" && operation != "multiply" && operation != "divide" &&
         operation != "min" && operation != "max") ||
        (operation == "divide" && operand == 0.f))
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "mathFloatAttribute requires a supported operation and valid attributes",
                                   "operation", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(mathPointFloatAttribute(*view, attribute, outputAttribute,
                                                                               operation, operand, defaultValue)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterFloatAttributeHandle(ProcgenPointSetHandleRef input,
                                                                          const std::string& name, float minValue,
                                                                          float maxValue, bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound() || name.empty())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            name.empty() ? eve::DiagnosticCode::InvalidArgument : eve::DiagnosticCode::StaleHandle,
            "filterFloatAttribute requires a live input and attribute name", "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(filterPointFloatAttribute(
                                                    *view, name, minValue, maxValue, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::filterStringAttributeHandle(ProcgenPointSetHandleRef input,
                                                                           const std::string&       name,
                                                                           const std::string& value, bool invert) {
    auto view = resolvePointSet(input);
    if (!view.isBound() || name.empty())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            name.empty() ? eve::DiagnosticCode::InvalidArgument : eve::DiagnosticCode::StaleHandle,
            "filterStringAttribute requires a live input and attribute name", "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(filterPointStringAttribute(*view, name, value, invert)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::excludeGridMaskHandle(
    ProcgenPointSetHandleRef input, ProcgenGridHandleRef mask, float originX, float originZ, float cellSize,
    int semantic, float clearance, int maximumChecks) {
    auto points = resolvePointSet(input);
    auto grid   = resolve(mask);
    if (!points.isBound() || !grid.isBound())
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::StaleHandle,
                                                               "excludeGridMask handle is stale", "input");
    if (maximumChecks <= 0)
        return procgenBindingFailure<ProcgenPointSetHandleRef>(eve::DiagnosticCode::InvalidArgument,
                                                               "excludeGridMask requires a positive work budget",
                                                               "maximumChecks");
    auto filtered = excludePointsByGridMask(*points, *grid, originX, originZ, cellSize, semantic, clearance,
                                            static_cast<std::size_t>(maximumChecks));
    if (!filtered.ok()) return eve::Result<ProcgenPointSetHandleRef>::failure(filtered.status());
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(std::move(filtered).takeValue()));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::densityCullHandle(ProcgenPointSetHandleRef input, uint32_t seed,
                                                                 float multiplier) {
    auto view = resolvePointSet(input);
    if (!view.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "densityCull input handle is stale", "input", {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(densityCullPoints(*view, seed, multiplier)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::projectToWorldHandle(ProcgenPointSetHandleRef input, float maxY,
                                                                    float minY, std::uint64_t maskBits,
                                                                    bool keepUnmatched) {
    auto points = resolvePointSet(input);
    if (!points.isBound())
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "projectToWorld point-set handle is stale",
                                   "points", {}, "procgen.squirrel"));
    if (!std::isfinite(maxY) || !std::isfinite(minY) || maxY < minY)
        return eve::Result<ProcgenPointSetHandleRef>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "projectToWorld requires finite maxY >= minY",
                                   "maxY", {}, "procgen.squirrel"));
    auto* query = eve::cap::query<eve::IProcgenWorldQuery>();
    if (!query)
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "projectToWorld requires an IProcgenWorldQuery provider", "worldQuery",
            {}, "procgen.squirrel"));

    PointSet output;
    output.reserve(points->points().size());
    for (size_t sourceIndex = 0; sourceIndex < points->points().size(); ++sourceIndex) {
        const auto& source      = points->points()[sourceIndex];
        auto        queryResult = query->projectDown(source.x, source.z, maxY, minY, maskBits);
        if (!queryResult.ok())
            return eve::Result<ProcgenPointSetHandleRef>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "world-query provider failed to execute projection",
                                       "worldQuery", {}, "procgen.squirrel"));
        const auto& hit = queryResult.value();
        if (!hit.hit) {
            if (keepUnmatched)
                std::move(output.appendPointFrom(*points, sourceIndex)).expect("projectToWorld attribute schema");
            continue;
        }
        ProcgenPoint projected = source;
        projected.x            = hit.x;
        projected.y            = hit.y;
        projected.z            = hit.z;
        projected.normalX      = hit.normalX;
        projected.normalY      = hit.normalY;
        projected.normalZ      = hit.normalZ;
        const int outputIndex =
            std::move(output.appendPointFrom(*points, sourceIndex)).expect("projectToWorld attribute schema");
        output.mutablePoint(size_t(outputIndex)) = std::move(projected);
        output.trySetIntAttribute(outputIndex, "worldObjectId", hit.objectId).expect("projectToWorld metadata schema");
    }
    return ownProcgenObject(ownership_->points, std::make_unique<PointSet>(std::move(output)));
}

eve::Result<ProcgenPointSetHandleRef> Procgen::poissonDiskHandle(int width, int depth, float radius, uint32_t seed,
                                                                 int maxPoints) {
    if (width < 0 || depth < 0 || radius <= 0.f || maxPoints < 0)
        return eve::Result<ProcgenPointSetHandleRef>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "poissonDisk requires non-negative dimensions/count and a positive radius", {}, {}, "procgen.squirrel"));
    return ownProcgenObject(ownership_->points,
                            std::make_unique<PointSet>(poissonDiskPoints(width, depth, radius, seed, maxPoints)));
}


}  // namespace eve::procgen
