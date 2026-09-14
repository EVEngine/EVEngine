#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include "procgen/Biome.h"
#include "procgen/PointCompute.h"
#include "procgen/PointGraph.h"
#include "procgen/ShapeGrammar.h"

namespace eve::procgen {
namespace {
Status nestedFailure(const std::string& id, const Status& status) {
    std::vector<Diagnostic> diagnostics;
    for (const auto& diagnostic : status.diagnostics())
        diagnostics.emplace_back(diagnostic.code(), diagnostic.severity(), diagnostic.message(),
                                 id + "/" + diagnostic.path(), diagnostic.details(), diagnostic.source());
    return Status(status.code(), std::move(diagnostics));
}

template <class T>
Result<T> nodeFailure(DiagnosticCode code, const std::string& id, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), id, {}, "procgen.pointGraph"));
}

uint64_t nowNanoseconds() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

PointGraphNodeMetric makeMetric(const std::string& id, const PointSet& points, float milliseconds, bool cacheHit,
                                const std::string& backend = "cpu") {
    PointGraphNodeMetric metric;
    metric.id           = id;
    metric.outputCount  = points.getCount();
    metric.milliseconds = milliseconds;
    metric.cacheHit     = cacheHit;
    metric.backend      = backend;
    if (points.empty()) return metric;
    const auto& first = points.points().front();
    metric.minX = metric.maxX = first.x;
    metric.minY = metric.maxY = first.y;
    metric.minZ = metric.maxZ = first.z;
    double densitySum         = 0.0;
    for (const auto& point : points.points()) {
        metric.minX = std::min(metric.minX, point.x);
        metric.minY = std::min(metric.minY, point.y);
        metric.minZ = std::min(metric.minZ, point.z);
        metric.maxX = std::max(metric.maxX, point.x);
        metric.maxY = std::max(metric.maxY, point.y);
        metric.maxZ = std::max(metric.maxZ, point.z);
        densitySum += point.density;
    }
    metric.averageDensity = float(densitySum / double(points.getCount()));
    return metric;
}

uint64_t spatialSampleUpperBound(const SpatialData& spatial, float spacing) {
    if (!spatial.hasBounds() || spacing <= 0.f) return 0;
    const auto axisCount = [spacing](float minimum, float maximum) {
        if (maximum <= minimum) return uint64_t(1);
        return uint64_t(std::floor(double(maximum - minimum) / double(spacing) + 0.001)) + 1;
    };
    const uint64_t x = axisCount(spatial.getMinX(), spatial.getMaxX());
    const uint64_t y =
        spatial.getKind() == "surface.heightfield" ? uint64_t(1) : axisCount(spatial.getMinY(), spatial.getMaxY());
    const uint64_t z       = axisCount(spatial.getMinZ(), spatial.getMaxZ());
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    if (x > maximum / y) return maximum;
    const uint64_t xy = x * y;
    return xy > maximum / z ? maximum : xy * z;
}

}  // namespace

ResultRef<const PointSet> PointGraph::evaluate(const std::string& id, std::unordered_map<std::string, int>& states) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) {
        return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::NotFound, id, "unknown node: " + id);
    }
    Node& node = found->second;
    if (node.cacheValid) {
        ++cacheHitCount_;
        PointGraphNodeMetric metric = node.cacheMetric;
        metric.milliseconds         = 0.f;
        metric.cacheHit             = true;
        metrics_.push_back(std::move(metric));
        return ResultRef<const PointSet>::success(std::cref(node.cache));
    }
    if (states[id] == 1) {
        return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::Conflict, id,
                                                                   "cycle at node: " + id);
    }
    if (cancelRequested_) {
        lastCancelled_ = true;
        return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::Cancelled, id,
                                                                   "execution cancelled at node: " + id);
    }
    if (node.operation == "transform") {
        auto segment = evaluateTransformSegment(id, states);
        if (!segment.ok()) return ResultRef<const PointSet>::failure(segment.status());
        auto value = std::move(segment).takeValue();
        if (value) return ResultRef<const PointSet>::success(*value);
    }
    states[id]                  = 1;
    const uint64_t  started     = nowNanoseconds();
    std::string     nodeBackend = "cpu";
    const PointSet* first       = nullptr;
    const PointSet* second      = nullptr;
    if (!node.inputs[0].empty()) {
        auto input = evaluate(node.inputs[0], states);
        if (!input.ok()) return ResultRef<const PointSet>::failure(input.status());
        first = &input.value().get();
    }
    if (!node.inputs[1].empty()) {
        auto input = evaluate(node.inputs[1], states);
        if (!input.ok()) return ResultRef<const PointSet>::failure(input.status());
        second = &input.value().get();
    }
    if (executionNodeBudget_ > 0 && evaluatedNodes_ >= executionNodeBudget_) {
        lastCancelled_ = true;
        return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::Cancelled, id,
                                                                   "execution node budget exceeded at node: " + id);
    }
    ++evaluatedNodes_;

    if (node.operation == "input") {
        if (!node.hasPoints)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "input node has no points: " + id);
        else
            node.cache = node.points;

    } else if (node.operation == "get.points" || node.operation == "get.actor") {
        const std::string binding = stringValue(node, "binding", {});
        if (binding.empty())
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       node.operation + " requires binding: " + id);
        auto pointsResult = resolveBindingPoints(binding);
        if (!pointsResult.ok())
            return ResultRef<const PointSet>::failure(nestedFailure(id, pointsResult.status()));
        node.cache = *pointsResult.value();
    } else if (node.operation == "spatial.sample" || node.operation == "get.spatial" ||
               node.operation == "get.landscape" || node.operation == "get.spline") {
        const float spacing = floatValue(node, "spacing", 1.f);
        auto spatialResult = resolveNodeSpatial(node);
        if (!spatialResult.ok())
            return ResultRef<const PointSet>::failure(nestedFailure(id, spatialResult.status()));
        const auto spatial = spatialResult.value();
        if (node.operation == "get.landscape" && spatial->getKind() != "surface.heightfield")
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "get.landscape requires surface.heightfield binding: " + id);
        if (node.operation == "get.spline" && spatial->getKind() != "spline")
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "get.spline requires spline binding: " + id);
        if (maxNodeOutputPoints_ > 0 &&
            spatialSampleUpperBound(*spatial, spacing) > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, node.operation + " exceeds node point budget at node: " + id);
        node.cache =
            spatial->sample(spacing, uint32_t(intValue(node, "seed", 1)), floatValue(node, "jitter", 0.f));
    } else if (node.operation == "mesh.sample") {
        const float spacing = floatValue(node, "spacing", 1.f);
        auto spatialResult = resolveNodeSpatial(node);
        if (!spatialResult.ok())
            return ResultRef<const PointSet>::failure(nestedFailure(id, spatialResult.status()));
        const auto spatial = spatialResult.value();
        if (spatial->getKind() != "surface.mesh")
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "mesh.sample requires surface.mesh spatial data: " + id);
        else if (maxNodeOutputPoints_ > 0 &&
                 spatialSampleUpperBound(*spatial, spacing) > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "mesh.sample exceeds node point budget at node: " + id);
        else
            node.cache =
                spatial->sample(spacing, uint32_t(intValue(node, "seed", 1)), floatValue(node, "jitter", 0.f));
    } else if (node.operation == "grid.sample") {
        const int   width   = intValue(node, "width", 8);
        const int   depth   = intValue(node, "depth", 8);
        const float spacing = floatValue(node, "spacing", 1.f);
        if (width <= 0 || depth <= 0 || spacing <= 0.f)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id,
                "grid.sample requires positive width, depth, and spacing: " + id);
        const uint64_t expected = uint64_t(width) * uint64_t(depth);
        if (maxNodeOutputPoints_ > 0 && expected > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "grid.sample exceeds node point budget at node: " + id);
        node.cache = sampleGridPoints(width, depth, spacing, uint32_t(intValue(node, "seed", 1)),
                                      floatValue(node, "jitter", 0.f));
        const float originX = floatValue(node, "originX", 0.f);
        const float originY = floatValue(node, "originY", 0.f);
        const float originZ = floatValue(node, "originZ", 0.f);
        if (originX != 0.f || originY != 0.f || originZ != 0.f)
            node.cache = transformPointSet(node.cache, originX, originY, originZ, 0.f, 1.f, 1.f, 1.f);
    } else if (node.operation == "poisson.sample") {
        const int   width     = intValue(node, "width", 32);
        const int   depth     = intValue(node, "depth", 32);
        const float radius    = floatValue(node, "radius", 2.f);
        const int   maxPoints = intValue(node, "maxPoints", 1000);
        if (width < 0 || depth < 0 || radius <= 0.f || maxPoints < 0)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id,
                "poisson.sample requires non-negative width/depth/maxPoints and positive radius: " + id);
        if (maxNodeOutputPoints_ > 0 && uint64_t(maxPoints) > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "poisson.sample exceeds node point budget at node: " + id);
        node.cache = poissonDiskPoints(width, depth, radius, uint32_t(intValue(node, "seed", 1)), maxPoints);
        const float originX = floatValue(node, "originX", 0.f);
        const float originY = floatValue(node, "originY", 0.f);
        const float originZ = floatValue(node, "originZ", 0.f);
        if (originX != 0.f || originY != 0.f || originZ != 0.f)
            node.cache = transformPointSet(node.cache, originX, originY, originZ, 0.f, 1.f, 1.f, 1.f);
    } else if (node.operation == "spatial.filter") {
        auto spatialResult = resolveNodeSpatial(node);
        if (!first || !spatialResult.ok())
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "spatial.filter requires input and spatial data: " + id);
        else
            node.cache = spatialResult.value()->filter(*first, intValue(node, "invert", 0) != 0);
    } else if (node.operation == "spatial.project") {
        auto spatialResult = resolveNodeSpatial(node);
        if (!first || !spatialResult.ok())
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "spatial.project requires input and spatial data: " + id);
        else
            node.cache = spatialResult.value()->project(*first);
    } else if (node.operation == "spline.sample") {
        const float spacing = floatValue(node, "spacing", 1.f);
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "spline.sample requires control points: " + id);
        else if (spacing <= 0.f)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "spline.sample spacing must be positive: " + id);
        else if (first->getCount() < 2)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "spline.sample needs at least two control points: " + id);
        else
            node.cache = samplePolylinePoints(*first, spacing, uint32_t(intValue(node, "seed", 1)),
                                              floatValue(node, "lateralJitter", 0.f));
    } else if (node.operation == "spline.filter.distance") {
        if (!first || !second)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "spline.filter.distance requires points and control inputs: " + id);
        else if (second->getCount() < 2)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id,
                "spline.filter.distance needs at least two control points: " + id);
        else
            node.cache = filterPointsBySplineDistance(*first, *second, floatValue(node, "minDistance", 0.f),
                                                      floatValue(node, "maxDistance", 1.f));
    } else if (node.operation == "biome.generate") {
        const float spacing = floatValue(node, "spacing", 1.f);
        if (!node.spatial || !node.biomeRules)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "biome.generate requires spatial data and biome rules: " + id);
        else if (maxNodeOutputPoints_ > 0 &&
                 spatialSampleUpperBound(*node.spatial, spacing) > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "biome.generate exceeds node point budget at node: " + id);
        else {
            std::unique_ptr<PointSet> result(node.biomeRules->generate(
                node.spatial.get(), spacing, uint32_t(intValue(node, "seed", 1)), floatValue(node, "jitter", 0.f)));
            if (!result)
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::Failed, id, "biome.generate failed at " + id + ": " + node.biomeRules->getError());
            else
                node.cache = *result;
        }
    } else if (node.operation == "grammar.generate") {
        if (!first || !node.shapeGrammar)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "grammar.generate requires input and shape grammar: " + id);
        else {
            std::unique_ptr<PointSet> result(node.shapeGrammar->generate(
                stringValue(node, "grammar"), const_cast<PointSet*>(first), uint32_t(intValue(node, "seed", 1)),
                intValue(node, "acceptIncomplete", 1) != 0));
            if (!result)
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::Failed, id,
                    "grammar.generate failed at " + id + ": " + node.shapeGrammar->getError());
            else
                node.cache = *result;
        }
    } else if (node.operation == "merge") {
        const uint64_t outputCount = first && second ? uint64_t(first->getCount()) + uint64_t(second->getCount()) : 0;
        if (!first || !second)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "merge requires two inputs: " + id);
        else if (maxNodeOutputPoints_ > 0 && outputCount > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "merge exceeds node point budget at node: " + id);
        else
            node.cache = mergePointSets(*first, *second);
    } else if (node.operation == "points.union" || node.operation == "points.intersect" ||
               node.operation == "points.difference") {
        if (!first || !second)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, node.operation + " requires two inputs: " + id);
        else if (node.operation == "points.union")
            node.cache = unionPointSets(*first, *second);
        else if (node.operation == "points.intersect")
            node.cache = intersectPointSets(*first, *second);
        else
            node.cache = differencePointSets(*first, *second);
    } else if (node.operation == "copy.points") {
        const int      maxPoints   = intValue(node, "maxPoints", 100000);
        const uint64_t outputCount = first && second ? uint64_t(first->getCount()) * uint64_t(second->getCount()) : 0;
        if (!first || !second)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "copy.points requires source and target inputs: " + id);
        else if (maxPoints < 0 || outputCount > uint64_t(maxPoints))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "copy.points output exceeds maxPoints at node: " + id);
        else if (maxNodeOutputPoints_ > 0 && outputCount > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "copy.points exceeds node point budget at node: " + id);
        else
            node.cache = copyPointsToTargets(*first, *second, intValue(node, "inheritTargetAttributes", 1) != 0);
    } else if (node.operation == "density.remap") {
        const float inputMin = floatValue(node, "inputMin", 0.f);
        const float inputMax = floatValue(node, "inputMax", 1.f);
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "density.remap requires input: " + id);
        else if (inputMin == inputMax)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "density.remap requires a non-zero input range: " + id);
        else
            node.cache = remapPointDensity(*first, inputMin, inputMax, floatValue(node, "outputMin", 0.f),
                                           floatValue(node, "outputMax", 1.f), intValue(node, "clamp", 1) != 0);
    } else if (node.operation == "density.from.normal") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "density.from.normal requires input: " + id);
        else
            node.cache =
                densityFromNormal(*first, floatValue(node, "minDegrees", 0.f), floatValue(node, "maxDegrees", 90.f),
                                  floatValue(node, "outputMin", 0.f), floatValue(node, "outputMax", 1.f),
                                  intValue(node, "invert", 0) != 0);
    } else if (node.operation == "landscape.sample" || node.operation == "texture.sample") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, node.operation + " requires input: " + id);
        auto spatialResult = resolveNodeSpatial(node);
        if (!spatialResult.ok())
            return ResultRef<const PointSet>::failure(nestedFailure(id, spatialResult.status()));
        const auto spatial = spatialResult.value();
        if (node.operation == "landscape.sample" && spatial->getKind() != "surface.heightfield")
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "landscape.sample requires surface.heightfield: " + id);
        if (node.operation == "texture.sample" && spatial->getKind() != "volume.texture_mask")
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "texture.sample requires volume.texture_mask: " + id);
        std::string attribute = stringValue(node, "attribute", "$Density");
        if (attribute.empty()) attribute = "$Density";
        if (!isPointFloatChannel(attribute))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, node.operation + " has unknown float channel at node: " + id);
        PointSet source = *first;
        if (node.operation == "landscape.sample" && intValue(node, "project", 0) != 0) source = spatial->project(source);
        node.cache = sampleSpatialOntoChannel(source, *spatial, attribute, floatValue(node, "inputMin", 0.f),
                                              floatValue(node, "inputMax", node.operation == "texture.sample" ? 1.f : 0.f),
                                              floatValue(node, "outputMin", 0.f), floatValue(node, "outputMax", 1.f),
                                              intValue(node, "clamp", 1) != 0, intValue(node, "invert", 0) != 0);
    } else if (node.operation == "attribute.math.float") {
        const std::string attribute       = stringValue(node, "attribute");
        std::string       outputAttribute = stringValue(node, "outputAttribute");
        if (outputAttribute.empty()) outputAttribute = attribute;
        const std::string operation      = stringValue(node, "operation", "multiply");
        const float       operand        = floatValue(node, "operand", 1.f);
        const bool        operationKnown = operation == "add" || operation == "subtract" || operation == "multiply" ||
                                    operation == "divide" || operation == "min" || operation == "max";
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.math.float requires input: " + id);
        else if (attribute.empty())
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "attribute.math.float requires an attribute: " + id);
        else if (!isPointFloatChannel(attribute) || !isPointFloatChannel(outputAttribute))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "attribute.math.float has unknown float channel at node: " + id);
        else if (!operationKnown)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "attribute.math.float has invalid operation at node: " + id);
        else if (operation == "divide" && operand == 0.f)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "attribute.math.float cannot divide by zero at node: " + id);
        else
            node.cache = mathPointFloatAttribute(*first, attribute, outputAttribute, operation, operand,
                                                 floatValue(node, "defaultValue", 0.f));
    } else if (node.operation == "transform") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "transform requires input: " + id);
        else {
            const bool gpuEligible =
                !first->empty() &&
                (computePolicy_ == "gpu" || (computePolicy_ == "auto" && first->getCount() >= computeMinimumPoints_));
            const bool gpuComplete =
                gpuEligible &&
                pointCompute_->transform(*first, node.cache, floatValue(node, "x", 0.f), floatValue(node, "y", 0.f),
                                         floatValue(node, "z", 0.f), floatValue(node, "yaw", 0.f),
                                         floatValue(node, "scaleX", 1.f), floatValue(node, "scaleY", 1.f),
                                         floatValue(node, "scaleZ", 1.f));
            if (gpuComplete) {
#ifdef EVENGINE_WEBGPU
                nodeBackend = "webgpu";
#else
                nodeBackend = "vulkan";
#endif
            } else {
                if (gpuEligible) computeFallbackReason_ = pointCompute_->getError();
                node.cache = transformPointSet(*first, floatValue(node, "x", 0.f), floatValue(node, "y", 0.f),
                                               floatValue(node, "z", 0.f), floatValue(node, "yaw", 0.f),
                                               floatValue(node, "scaleX", 1.f), floatValue(node, "scaleY", 1.f),
                                               floatValue(node, "scaleZ", 1.f));
            }
        }
    } else if (node.operation == "bounds.modify") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "bounds.modify requires input: " + id);
        else
            node.cache =
                modifyPointBounds(*first, floatValue(node, "scaleX", 1.f), floatValue(node, "scaleY", 1.f),
                                  floatValue(node, "scaleZ", 1.f), floatValue(node, "padX", 0.f),
                                  floatValue(node, "padY", 0.f), floatValue(node, "padZ", 0.f));
    } else if (node.operation == "filter.float") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "filter.float requires input: " + id);
        else
            node.cache = filterPointFloatAttribute(*first, stringValue(node, "attribute"), floatValue(node, "min", 0.f),
                                                   floatValue(node, "max", 1.f), intValue(node, "invert", 0) != 0);
    } else if (node.operation == "filter.string") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "filter.string requires input: " + id);
        else
            node.cache = filterPointStringAttribute(*first, stringValue(node, "attribute"), stringValue(node, "value"),
                                                    intValue(node, "invert", 0) != 0);
    } else if (node.operation == "filter.slope") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "filter.slope requires input: " + id);
        else
            node.cache =
                filterPointSlope(*first, floatValue(node, "minDegrees", 0.f), floatValue(node, "maxDegrees", 90.f));
    } else if (node.operation == "attribute.set.float") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.set.float requires input: " + id);
        else {
            node.cache                  = *first;
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty() || !isPointFloatChannel(attribute))
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.set.float requires attribute: " + id);
            else {
                const float value = floatValue(node, "value", 0.f);
                for (int i = 0; i < node.cache.getCount(); ++i) {
                    auto written = writePointFloatChannel(node.cache, i, attribute, value);
                    if (!written.ok())
                        return ResultRef<const PointSet>::failure(nestedFailure(id, written.status()));
                }
            }
        }
    } else if (node.operation == "attribute.set.string") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.set.string requires input: " + id);
        else {
            node.cache                  = *first;
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty() || attribute.front() == '$')
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.set.string requires attribute: " + id);
            else
                for (int i = 0; i < node.cache.getCount(); ++i)
                    node.cache.setStringAttribute(i, attribute, stringValue(node, "value"));
        }
    } else if (node.operation == "attribute.set.int") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.set.int requires input: " + id);
        else {
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty() || attribute.front() == '$')
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.set.int requires attribute: " + id);
            node.cache = setPointIntAttribute(*first, attribute, intValue(node, "value", 0));
        }
    } else if (node.operation == "attribute.set.bool") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.set.bool requires input: " + id);
        else {
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty() || attribute.front() == '$')
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.set.bool requires attribute: " + id);
            node.cache = setPointBoolAttribute(*first, attribute, intValue(node, "value", 0) != 0);
        }
    } else if (node.operation == "attribute.set.vector") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.set.vector requires input: " + id);
        else {
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty() || attribute.front() == '$')
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.set.vector requires attribute: " + id);
            node.cache = setPointVectorAttribute(*first, attribute, floatValue(node, "x", 0.f),
                                                 floatValue(node, "y", 0.f), floatValue(node, "z", 0.f));
        }
    } else if (node.operation == "attribute.copy") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.copy requires input: " + id);
        else {
            auto copied = copyPointAttribute(*first, stringValue(node, "source"), stringValue(node, "target"));
            if (!copied.ok()) return ResultRef<const PointSet>::failure(nestedFailure(id, copied.status()));
            node.cache = std::move(copied).takeValue();
        }
    } else if (node.operation == "attribute.rename") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.rename requires input: " + id);
        else {
            auto renamed = renamePointAttribute(*first, stringValue(node, "from"), stringValue(node, "to"));
            if (!renamed.ok()) return ResultRef<const PointSet>::failure(nestedFailure(id, renamed.status()));
            node.cache = std::move(renamed).takeValue();
        }
    } else if (node.operation == "attribute.delete") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.delete requires input: " + id);
        else {
            auto deleted = deletePointAttribute(*first, stringValue(node, "attribute"));
            if (!deleted.ok()) return ResultRef<const PointSet>::failure(nestedFailure(id, deleted.status()));
            node.cache = std::move(deleted).takeValue();
        }
    } else if (node.operation == "attribute.transfer") {
        if (!first || !second)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.transfer requires two inputs: " + id);
        else {
            auto transferred =
                transferPointAttribute(*first, *second, stringValue(node, "attribute"),
                                       stringValue(node, "outputAttribute"), stringValue(node, "mode", "nearest"));
            if (!transferred.ok())
                return ResultRef<const PointSet>::failure(nestedFailure(id, transferred.status()));
            node.cache = std::move(transferred).takeValue();
        }
    } else if (node.operation == "attribute.compare.float") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "attribute.compare.float requires input: " + id);
        else {
            auto compared = comparePointFloatAttribute(
                *first, stringValue(node, "attribute"), stringValue(node, "comparison", "gt"),
                floatValue(node, "operand", 0.f), stringValue(node, "outputAttribute", "match"),
                floatValue(node, "defaultValue", 0.f));
            if (!compared.ok()) return ResultRef<const PointSet>::failure(nestedFailure(id, compared.status()));
            node.cache = std::move(compared).takeValue();
        }
    } else if (node.operation == "attribute.select.float") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "attribute.select.float requires input: " + id);
        else {
            auto selected = selectPointFloatAttribute(
                *first, stringValue(node, "conditionAttribute"), stringValue(node, "trueAttribute"),
                stringValue(node, "falseAttribute"), stringValue(node, "outputAttribute"),
                floatValue(node, "trueDefault", 0.f), floatValue(node, "falseDefault", 0.f));
            if (!selected.ok()) return ResultRef<const PointSet>::failure(nestedFailure(id, selected.status()));
            node.cache = std::move(selected).takeValue();
        }
    } else if (node.operation == "filter.int") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "filter.int requires input: " + id);
        else
            node.cache = filterPointIntAttribute(*first, stringValue(node, "attribute"), intValue(node, "min", 0),
                                                 intValue(node, "max", 0), intValue(node, "invert", 0) != 0);
    } else if (node.operation == "filter.bool") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "filter.bool requires input: " + id);
        else
            node.cache = filterPointBoolAttribute(*first, stringValue(node, "attribute"),
                                                  intValue(node, "value", 1) != 0, intValue(node, "invert", 0) != 0);

    } else if (node.operation == "attribute.partition") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.partition requires input: " + id);
        else {
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty())
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.partition requires attribute: " + id);
            node.cache = partitionPointAttribute(*first, attribute, stringValue(node, "outputAttribute", "partition"),
                                                 stringValue(node, "mode", "value"));
        }
    } else if (node.operation == "attribute.noise.float") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.noise.float requires input: " + id);
        else
            node.cache = noisePointFloatAttribute(*first, stringValue(node, "attribute", "noise"),
                                                  uint32_t(intValue(node, "seed", 1)),
                                                  floatValue(node, "frequency", 1.f),
                                                  floatValue(node, "amplitude", 1.f),
                                                  floatValue(node, "offset", 0.f));
    } else if (node.operation == "attribute.math.int") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.math.int requires input: " + id);
        else {
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty())
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.math.int requires attribute: " + id);
            node.cache = mathPointIntAttribute(*first, attribute, stringValue(node, "outputAttribute"),
                                               stringValue(node, "operation", "add"),
                                               intValue(node, "operand", 1), intValue(node, "defaultValue", 0));
        }
    } else if (node.operation == "attribute.math.vector") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.math.vector requires input: " + id);
        else {
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty())
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.math.vector requires attribute: " + id);
            node.cache = mathPointVectorAttribute(
                *first, attribute, stringValue(node, "outputAttribute"), stringValue(node, "operation", "scale"),
                floatValue(node, "operandX", 1.f), floatValue(node, "operandY", 1.f), floatValue(node, "operandZ", 1.f),
                floatValue(node, "defaultX", 0.f), floatValue(node, "defaultY", 0.f), floatValue(node, "defaultZ", 0.f));
        }
    } else if (node.operation == "attribute.set.data.float" || node.operation == "attribute.set.data.int" ||
               node.operation == "attribute.set.data.string") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, node.operation + " requires input: " + id);
        else {
            node.cache = *first;
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty())
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, node.operation + " requires attribute: " + id);
            Result<void> written = Result<void>::success();
            if (node.operation == "attribute.set.data.float")
                written = setPointDataFloatAttribute(node.cache, attribute, floatValue(node, "value", 0.f));
            else if (node.operation == "attribute.set.data.int")
                written = setPointDataIntAttribute(node.cache, attribute, intValue(node, "value", 0));
            else
                written = setPointDataStringAttribute(node.cache, attribute, stringValue(node, "value"));
            if (!written.ok())
                return ResultRef<const PointSet>::failure(nestedFailure(id, written.status()));
        }
    } else if (node.operation == "spawn.mesh") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "spawn.mesh requires input: " + id);
        else {
            const std::string attribute = stringValue(node, "attribute", "mesh");
            if (attribute.empty())
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "spawn.mesh requires attribute: " + id);
            const std::string meshes[4] = {stringValue(node, "mesh0"), stringValue(node, "mesh1"),
                                           stringValue(node, "mesh2"), stringValue(node, "mesh3")};
            const float       weights[4] = {floatValue(node, "weight0", 1.f), floatValue(node, "weight1", 0.f),
                                      floatValue(node, "weight2", 0.f), floatValue(node, "weight3", 0.f)};
            bool              hasEntry   = false;
            for (int entry = 0; entry < 4; ++entry)
                if (!meshes[entry].empty() && weights[entry] > 0.f) hasEntry = true;
            if (!hasEntry)
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "spawn.mesh requires at least one weighted mesh: " + id);
            node.cache = assignWeightedMeshAttribute(*first, uint32_t(intValue(node, "seed", 1)), attribute, meshes,
                                                     weights, 4);
        }
    } else if (node.operation == "density.cull") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "density.cull requires input: " + id);
        else
            node.cache =
                densityCullPoints(*first, uint32_t(intValue(node, "seed", 1)), floatValue(node, "multiplier", 1.f));
    } else if (node.operation == "self.prune") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "self.prune requires input: " + id);
        else
            node.cache = selfPrunePoints(*first, floatValue(node, "radius", 1.f));
    } else if (node.operation == "jitter") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "jitter requires input: " + id);
        else
            node.cache = jitterPointPositions(*first, uint32_t(intValue(node, "seed", 1)), floatValue(node, "x", 0.f),
                                              floatValue(node, "z", 0.f));
    } else if (node.operation == "debug.disable") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "debug.disable requires input: " + id);
        else if (intValue(node, "enabled", 1) == 0)
            node.cache = PointSet{};
        else
            node.cache = *first;
    } else if (node.operation == "debug.inspect") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "debug.inspect requires input: " + id);
        else
            node.cache = *first;
    } else if (node.operation == "branch") {
        const bool condition = intValue(node, "condition", 0) != 0;
        if ((condition && !first) || (!condition && !second))
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "branch requires both selected inputs: " + id);
        else
            node.cache = condition ? *first : *second;
    } else if (node.operation == "subgraph") {
        if (!first || !node.subgraph)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "subgraph requires input and graph: " + id);
        else {
            node.subgraph->setMaxNodeOutputPoints(maxNodeOutputPoints_);
            node.subgraph->setComputePolicy(computePolicy_);
            node.subgraph->setComputeMinimumPoints(computeMinimumPoints_);
            if (!node.subgraph->setNodePoints(node.subgraphInput, const_cast<PointSet*>(first)))
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "subgraph input is invalid: " + node.subgraphInput);
            else {
                auto result = node.subgraph->executeResult(node.subgraphOutput);
                if (!result.ok()) return ResultRef<const PointSet>::failure(nestedFailure(id, result.status()));
                node.cache = std::move(result).takeValue();
            }
        }
    }

    if (maxNodeOutputPoints_ > 0 && node.cache.getCount() > maxNodeOutputPoints_) {
        node.cache.clear();
        return nodeFailure<std::reference_wrapper<const PointSet>>(
            DiagnosticCode::InvalidArgument, id, node.operation + " exceeds node point budget at node: " + id);
    }
    node.cacheValid     = true;
    states[id]          = 2;
    const float elapsed = float(nowNanoseconds() - started) * 0.000001f;
    node.cacheMetric    = makeMetric(id, node.cache, elapsed, false, nodeBackend);
    metrics_.push_back(node.cacheMetric);
    return ResultRef<const PointSet>::success(std::cref(node.cache));
}

Result<OptionalRef<const PointSet>> PointGraph::evaluateTransformSegment(const std::string&                    id,
                                                                         std::unordered_map<std::string, int>& states) {
    if (!executionPlan_) return Result<OptionalRef<const PointSet>>::success(std::nullopt);
    const auto segmentEntry = executionPlan_->segmentByOutput.find(id);
    if (segmentEntry == executionPlan_->segmentByOutput.end())
        return Result<OptionalRef<const PointSet>>::success(std::nullopt);
    const ExecutionSegment& planned = executionPlan_->segments[segmentEntry->second];
    if (!planned.gpuTransformChain || planned.nodes.size() < 2)
        return Result<OptionalRef<const PointSet>>::success(std::nullopt);
    const std::vector<std::string>& chain = planned.nodes;
    if (std::any_of(chain.begin(), chain.end(),
                    [&](const std::string& nodeId) { return nodes_.at(nodeId).cacheValid; }))
        return Result<OptionalRef<const PointSet>>::success(std::nullopt);
    const std::string& cursor = nodes_.at(chain.front()).inputs[0];

    auto evaluated = evaluate(cursor, states);
    if (!evaluated.ok()) return Result<OptionalRef<const PointSet>>::failure(evaluated.status());
    const PointSet* input = &evaluated.value().get();
    const bool      gpuEligible =
        !input->empty() &&
        (computePolicy_ == "gpu" || (computePolicy_ == "auto" && input->getCount() >= computeMinimumPoints_));
    if (!gpuEligible) return Result<OptionalRef<const PointSet>>::success(std::nullopt);
    if (executionNodeBudget_ > 0 && evaluatedNodes_ + int(chain.size()) > executionNodeBudget_) {
        lastCancelled_ = true;
        return nodeFailure<OptionalRef<const PointSet>>(DiagnosticCode::Cancelled, id,
                                                        "execution node budget exceeded at node: " + id);
    }

    std::vector<PointCompute::Transform> transforms;
    transforms.reserve(chain.size());
    for (const std::string& nodeId : chain) {
        const Node& transformNode = nodes_.at(nodeId);
        transforms.push_back({floatValue(transformNode, "x", 0.f), floatValue(transformNode, "y", 0.f),
                              floatValue(transformNode, "z", 0.f), floatValue(transformNode, "yaw", 0.f),
                              floatValue(transformNode, "scaleX", 1.f), floatValue(transformNode, "scaleY", 1.f),
                              floatValue(transformNode, "scaleZ", 1.f)});
    }

    PointSet       output;
    const uint64_t started = nowNanoseconds();
    if (!pointCompute_->transformChain(*input, output, transforms)) {
        computeFallbackReason_ = pointCompute_->getError();
        return Result<OptionalRef<const PointSet>>::success(std::nullopt);
    }

    evaluatedNodes_ += int(chain.size());
    for (const std::string& nodeId : chain) {
        Node& transformNode                  = nodes_.at(nodeId);
        transformNode.deferredTransformValid = true;
        states[nodeId]                       = 2;
    }
    Node& finalNode                  = nodes_.at(id);
    finalNode.cache                  = std::move(output);
    finalNode.cacheValid             = true;
    finalNode.deferredTransformValid = false;
#ifdef EVENGINE_WEBGPU
    const std::string backend = "webgpu";
#else
    const std::string backend = "vulkan";
#endif
    const float elapsed   = float(nowNanoseconds() - started) * 0.000001f;
    finalNode.cacheMetric = makeMetric(id, finalNode.cache, elapsed, false, backend);
    metrics_.push_back(finalNode.cacheMetric);
    return Result<OptionalRef<const PointSet>>::success(std::cref(finalNode.cache));
}

Result<void> PointGraph::validateNode(const std::string& id, std::unordered_map<std::string, int>& states) const {
    if (states[id] == 2) return Result<void>::success();
    if (states[id] == 1) {
        return nodeFailure<void>(DiagnosticCode::Conflict, id, "cycle at node: " + id);
    }
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || getOperationInputCount(found->second.operation) < 0) {
        return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, "invalid node: " + id);
    }
    states[id] = 1;
    for (const auto& input : found->second.inputs) {
        if (!input.empty()) {
            auto result = validateNode(input, states);
            if (!result.ok()) return result;
        }
    }
    const auto& node = found->second;
    if (node.operation == "input" && !node.hasPoints)
        return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, "input node has no points: " + id);
    else if (node.operation == "get.points" || node.operation == "get.actor") {
        if (stringValue(node, "binding").empty())
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, node.operation + " requires binding: " + id);
        auto points = resolveBindingPoints(stringValue(node, "binding"));
        if (!points.ok()) return Result<void>::failure(nestedFailure(id, points.status()));
    } else if (node.operation == "get.spatial" || node.operation == "get.landscape" ||
               node.operation == "get.spline" || node.operation == "spatial.sample" ||
               node.operation == "mesh.sample" || node.operation == "spatial.filter" ||
               node.operation == "spatial.project" || node.operation == "biome.generate" ||
               node.operation == "landscape.sample" || node.operation == "texture.sample") {
        auto spatial = resolveNodeSpatial(node);
        if (!spatial.ok()) return Result<void>::failure(nestedFailure(id, spatial.status()));
        if (node.operation == "get.landscape" && spatial.value()->getKind() != "surface.heightfield")
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id,
                                     "get.landscape requires surface.heightfield binding: " + id);
        if (node.operation == "get.spline" && spatial.value()->getKind() != "spline")
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, "get.spline requires spline binding: " + id);
        if (node.operation == "mesh.sample" && spatial.value()->getKind() != "surface.mesh")
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id,
                                     "mesh.sample requires surface.mesh spatial data: " + id);
        if (node.operation == "landscape.sample" && spatial.value()->getKind() != "surface.heightfield")
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id,
                                     "landscape.sample requires surface.heightfield: " + id);
        if (node.operation == "texture.sample" && spatial.value()->getKind() != "volume.texture_mask")
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id,
                                     "texture.sample requires volume.texture_mask: " + id);
    } else if (node.operation == "grid.sample") {
        if (intValue(node, "width", 8) <= 0 || intValue(node, "depth", 8) <= 0 ||
            floatValue(node, "spacing", 1.f) <= 0.f)
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id,
                                     "grid.sample requires positive width, depth, and spacing: " + id);
    } else if (node.operation == "poisson.sample") {
        if (intValue(node, "width", 32) < 0 || intValue(node, "depth", 32) < 0 ||
            floatValue(node, "radius", 2.f) <= 0.f || intValue(node, "maxPoints", 1000) < 0)
            return nodeFailure<void>(
                DiagnosticCode::InvalidArgument, id,
                "poisson.sample requires non-negative width/depth/maxPoints and positive radius: " + id);
    }
    else if (node.operation == "biome.generate" && !node.biomeRules)
        return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, "node has no biome rules: " + id);
    else if (node.operation == "grammar.generate" && !node.shapeGrammar)
        return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, "node has no shape grammar: " + id);
    else if (node.operation == "subgraph" && (!node.subgraph || node.inputs[0].empty()))
        return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, "subgraph is not configured: " + id);
    else if (node.operation == "subgraph") {
        PointGraph nested = *node.subgraph;
        PointSet   externalInput;
        if (!nested.setNodePoints(node.subgraphInput, &externalInput))
            return nodeFailure<void>(DiagnosticCode::InvalidArgument, id,
                                     "invalid subgraph input: " + node.subgraphInput);
        auto result = nested.validateResult();
        if (!result.ok()) return Result<void>::failure(nestedFailure(id, result.status()));
    } else {
        const int inputCount = getOperationInputCount(node.operation);
        for (int index = 0; index < inputCount; ++index)
            if (node.inputs[index].empty()) {
                return nodeFailure<void>(DiagnosticCode::InvalidArgument, id,
                                         node.operation + " requires input " + std::to_string(index) + ": " + id);
            }
    }
    states[id] = 2;
    return Result<void>::success();
}

}  // namespace eve::procgen
