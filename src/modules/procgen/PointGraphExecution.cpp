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
    } else if (node.operation == "spatial.sample") {
        const float spacing = floatValue(node, "spacing", 1.f);
        if (!node.spatial)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "spatial.sample has no spatial data: " + id);
        else if (maxNodeOutputPoints_ > 0 &&
                 spatialSampleUpperBound(*node.spatial, spacing) > uint64_t(maxNodeOutputPoints_))
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "spatial.sample exceeds node point budget at node: " + id);
        else
            node.cache =
                node.spatial->sample(spacing, uint32_t(intValue(node, "seed", 1)), floatValue(node, "jitter", 0.f));
    } else if (node.operation == "spatial.filter") {
        if (!first || !node.spatial)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "spatial.filter requires input and spatial data: " + id);
        else
            node.cache = node.spatial->filter(*first, intValue(node, "invert", 0) != 0);
    } else if (node.operation == "spatial.project") {
        if (!first || !node.spatial)
            return nodeFailure<std::reference_wrapper<const PointSet>>(
                DiagnosticCode::InvalidArgument, id, "spatial.project requires input and spatial data: " + id);
        else
            node.cache = node.spatial->project(*first);
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
            if (attribute.empty())
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.set.float requires attribute: " + id);
            else
                for (int i = 0; i < node.cache.getCount(); ++i)
                    node.cache.setFloatAttribute(i, attribute, floatValue(node, "value", 0.f));
        }
    } else if (node.operation == "attribute.set.string") {
        if (!first)
            return nodeFailure<std::reference_wrapper<const PointSet>>(DiagnosticCode::InvalidArgument, id,
                                                                       "attribute.set.string requires input: " + id);
        else {
            node.cache                  = *first;
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty())
                return nodeFailure<std::reference_wrapper<const PointSet>>(
                    DiagnosticCode::InvalidArgument, id, "attribute.set.string requires attribute: " + id);
            else
                for (int i = 0; i < node.cache.getCount(); ++i)
                    node.cache.setStringAttribute(i, attribute, stringValue(node, "value"));
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
    else if ((node.operation == "spatial.sample" || node.operation == "spatial.filter" ||
              node.operation == "spatial.project" || node.operation == "biome.generate") &&
             !node.spatial)
        return nodeFailure<void>(DiagnosticCode::InvalidArgument, id, "node has no spatial data: " + id);
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
