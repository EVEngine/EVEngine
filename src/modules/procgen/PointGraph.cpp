#include "procgen/PointGraph.h"

#include "procgen/Biome.h"
#include "procgen/PointCompute.h"
#include "procgen/ShapeGrammar.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>

namespace eve::procgen {
namespace {

struct OperationParam {
    const char* key;
    const char* kind;
    const char* defaultValue;
};
struct OperationSpec {
    const char*                 id;
    int                         inputs;
    std::vector<OperationParam> params;
};

const std::vector<OperationSpec>& operationSpecs() {
    static const std::vector<OperationSpec> value = {
        {"input", 0, {}},
        {"spatial.sample", 0, {{"spacing", "float", "1"}, {"seed", "int", "1"}, {"jitter", "float", "0"}}},
        {"spatial.filter", 1, {{"invert", "bool", "false"}}},
        {"spatial.project", 1, {}},
        {"biome.generate", 0, {{"spacing", "float", "1"}, {"seed", "int", "1"},
                                {"jitter", "float", "0"}}},
        {"grammar.generate", 1, {{"grammar", "string", ""}, {"seed", "int", "1"},
                                  {"acceptIncomplete", "bool", "true"}}},
        {"merge", 2, {}},
        {"copy.points", 2, {{"maxPoints", "int", "100000"},
                             {"inheritTargetAttributes", "bool", "true"}}},
        {"density.remap", 1, {{"inputMin", "float", "0"}, {"inputMax", "float", "1"},
                               {"outputMin", "float", "0"}, {"outputMax", "float", "1"},
                               {"clamp", "bool", "true"}}},
        {"attribute.math.float", 1, {{"attribute", "string", ""},
                                      {"outputAttribute", "string", ""},
                                      {"operation", "string", "multiply"},
                                      {"operand", "float", "1"},
                                      {"defaultValue", "float", "0"}}},
        {"transform", 1, {{"x", "float", "0"}, {"y", "float", "0"}, {"z", "float", "0"},
                          {"yaw", "float", "0"}, {"scaleX", "float", "1"},
                          {"scaleY", "float", "1"}, {"scaleZ", "float", "1"}}},
        {"filter.float", 1, {{"attribute", "string", ""}, {"min", "float", "0"},
                             {"max", "float", "1"}, {"invert", "bool", "false"}}},
        {"filter.slope", 1, {{"minDegrees", "float", "0"},
                              {"maxDegrees", "float", "90"}}},
        {"filter.string", 1, {{"attribute", "string", ""}, {"value", "string", ""},
                              {"invert", "bool", "false"}}},
        {"attribute.set.float", 1, {{"attribute", "string", ""}, {"value", "float", "0"}}},
        {"attribute.set.string", 1, {{"attribute", "string", ""}, {"value", "string", ""}}},
        {"density.cull", 1, {{"seed", "int", "1"}, {"multiplier", "float", "1"}}},
        {"self.prune", 1, {{"radius", "float", "1"}}},
        {"jitter", 1, {{"seed", "int", "1"}, {"x", "float", "0"}, {"z", "float", "0"}}},
        {"branch", 2, {{"condition", "bool", "false"}}},
        {"subgraph", 1, {}},
    };
    return value;
}

bool operationKnown(const std::string& operation) {
    const auto& values = operationSpecs();
    return std::any_of(values.begin(), values.end(),
                       [&](const OperationSpec& value) { return operation == value.id; });
}

const OperationSpec* operationSpec(const std::string& operation) {
    const auto& values = operationSpecs();
    const auto found = std::find_if(values.begin(), values.end(),
                                    [&](const OperationSpec& value) { return operation == value.id; });
    return found == values.end() ? nullptr : &*found;
}

const OperationParam* operationParam(const OperationSpec* spec, const std::string& key) {
    if (!spec) return nullptr;
    const auto found = std::find_if(spec->params.begin(), spec->params.end(),
                                    [&](const OperationParam& value) { return key == value.key; });
    return found == spec->params.end() ? nullptr : &*found;
}

}  // namespace

PointGraph::PointGraph() : pointCompute_(std::make_shared<PointCompute>()) {}

bool PointGraph::addNode(const std::string& id, const std::string& operation) {
    if (id.empty() || !operationKnown(operation) || nodes_.find(id) != nodes_.end()) return false;
    nodes_[id].id        = id;
    nodes_[id].operation = operation;
    nodeOrder_.push_back(id);
    invalidateTopology(id);
    invalidateFrom(id);
    return true;
}

bool PointGraph::removeNode(const std::string& id) {
    if (nodes_.find(id) == nodes_.end()) return false;
    invalidateFrom(id);
    nodes_.erase(id);
    nodeOrder_.erase(std::remove(nodeOrder_.begin(), nodeOrder_.end(), id), nodeOrder_.end());
    for (auto it = parameterOrder_.begin(); it != parameterOrder_.end();) {
        const auto binding = parameters_.find(*it);
        if (binding != parameters_.end() && binding->second.nodeId == id) {
            floatOverrides_.erase(*it);
            intOverrides_.erase(*it);
            stringOverrides_.erase(*it);
            parameters_.erase(binding);
            it = parameterOrder_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [otherId, node] : nodes_)
        for (auto& input : node.inputs)
            if (input == id) input.clear();
    invalidateTopology(id);
    return true;
}

bool PointGraph::hasNode(const std::string& id) const { return nodes_.find(id) != nodes_.end(); }
int  PointGraph::getNodeCount() const { return int(nodeOrder_.size()); }
std::string PointGraph::getNodeId(int index) const {
    return index >= 0 && index < int(nodeOrder_.size()) ? nodeOrder_[size_t(index)] : std::string();
}
std::string PointGraph::getNodeOperation(const std::string& id) const {
    const auto found = nodes_.find(id);
    return found == nodes_.end() ? std::string() : found->second.operation;
}

bool PointGraph::connect(const std::string& fromId, const std::string& toId, int inputIndex) {
    const auto from = nodes_.find(fromId);
    const auto to   = nodes_.find(toId);
    const auto* spec = to == nodes_.end() ? nullptr : operationSpec(to->second.operation);
    if (from == nodes_.end() || to == nodes_.end() || !spec || inputIndex < 0 ||
        inputIndex >= spec->inputs || fromId == toId)
        return false;
    to->second.inputs[inputIndex] = fromId;
    invalidateTopology(toId);
    invalidateFrom(toId);
    return true;
}

bool PointGraph::disconnect(const std::string& toId, int inputIndex) {
    const auto to = nodes_.find(toId);
    if (to == nodes_.end() || inputIndex < 0 || inputIndex > 1 ||
        to->second.inputs[inputIndex].empty())
        return false;
    to->second.inputs[inputIndex].clear();
    invalidateTopology(toId);
    invalidateFrom(toId);
    return true;
}

std::string PointGraph::getInputNode(const std::string& nodeId, int inputIndex) const {
    const auto found = nodes_.find(nodeId);
    return found != nodes_.end() && inputIndex >= 0 && inputIndex <= 1
               ? found->second.inputs[inputIndex]
               : std::string();
}

bool PointGraph::setNodePoints(const std::string& id, PointSet* points) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || !points) return false;
    if (maxNodeOutputPoints_ > 0 && points->getCount() > maxNodeOutputPoints_) {
        error_ = "input exceeds node point budget at node: " + id;
        return false;
    }
    found->second.points    = *points;
    found->second.hasPoints = true;
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeSpatial(const std::string& id, SpatialData* spatial) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || !spatial) return false;
    found->second.spatial = std::make_shared<SpatialData>(*spatial);
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeBiomeRules(const std::string& id, BiomeRules* rules) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || found->second.operation != "biome.generate" || !rules)
        return false;
    found->second.biomeRules = std::make_shared<BiomeRules>(*rules);
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeShapeGrammar(const std::string& id, ShapeGrammar* grammar) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || found->second.operation != "grammar.generate" || !grammar)
        return false;
    found->second.shapeGrammar = std::make_shared<ShapeGrammar>(*grammar);
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeFloat(const std::string& id, const std::string& key, float value) {
    const auto found = nodes_.find(id);
    const auto* spec = found == nodes_.end() ? nullptr : operationSpec(found->second.operation);
    const auto* parameter = operationParam(spec, key);
    if (!parameter || std::string(parameter->kind) != "float") return false;
    found->second.floats[key] = value;
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeInt(const std::string& id, const std::string& key, int value) {
    const auto found = nodes_.find(id);
    const auto* spec = found == nodes_.end() ? nullptr : operationSpec(found->second.operation);
    const auto* parameter = operationParam(spec, key);
    if (!parameter ||
        (std::string(parameter->kind) != "int" && std::string(parameter->kind) != "bool"))
        return false;
    found->second.ints[key] = value;
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeString(const std::string& id, const std::string& key,
                               const std::string& value) {
    const auto found = nodes_.find(id);
    const auto* spec = found == nodes_.end() ? nullptr : operationSpec(found->second.operation);
    const auto* parameter = operationParam(spec, key);
    if (!parameter || std::string(parameter->kind) != "string") return false;
    found->second.strings[key] = value;
    invalidateFrom(id);
    return true;
}

bool PointGraph::exposeParameter(const std::string& name, const std::string& nodeId,
                                 const std::string& key) {
    const auto node = nodes_.find(nodeId);
    if (name.empty() || key.empty() || node == nodes_.end() || parameters_.count(name) != 0)
        return false;
    const OperationSpec* spec = operationSpec(node->second.operation);
    if (!spec) return false;
    const auto* parameter = operationParam(spec, key);
    if (!parameter) return false;
    for (const auto& [existingName, binding] : parameters_)
        if (binding.nodeId == nodeId && binding.key == key) return false;
    parameters_.emplace(name, ParameterBinding{nodeId, key, parameter->kind});
    parameterOrder_.push_back(name);
    ++revision_;
    return true;
}

int PointGraph::getParameterCount() const { return int(parameterOrder_.size()); }
std::string PointGraph::getParameterName(int index) const {
    return index >= 0 && index < int(parameterOrder_.size()) ? parameterOrder_[size_t(index)]
                                                              : std::string();
}
std::string PointGraph::getParameterKind(const std::string& name) const {
    const auto found = parameters_.find(name);
    return found == parameters_.end() ? std::string() : found->second.kind;
}
bool PointGraph::hasParameterOverride(const std::string& name) const {
    return floatOverrides_.count(name) != 0 || intOverrides_.count(name) != 0 ||
           stringOverrides_.count(name) != 0;
}
bool PointGraph::setParameterFloat(const std::string& name, float value) {
    const auto found = parameters_.find(name);
    if (found == parameters_.end() || found->second.kind != "float") return false;
    floatOverrides_[name] = value;
    invalidateFrom(found->second.nodeId);
    return true;
}
bool PointGraph::setParameterInt(const std::string& name, int value) {
    const auto found = parameters_.find(name);
    if (found == parameters_.end() || (found->second.kind != "int" &&
                                       found->second.kind != "bool"))
        return false;
    intOverrides_[name] = value;
    invalidateFrom(found->second.nodeId);
    return true;
}
bool PointGraph::setParameterString(const std::string& name, const std::string& value) {
    const auto found = parameters_.find(name);
    if (found == parameters_.end() || found->second.kind != "string") return false;
    stringOverrides_[name] = value;
    invalidateFrom(found->second.nodeId);
    return true;
}
float PointGraph::getParameterFloat(const std::string& name, float fallback) const {
    const auto found = parameters_.find(name);
    if (found == parameters_.end() || found->second.kind != "float") return fallback;
    return floatValue(nodes_.at(found->second.nodeId), found->second.key, fallback);
}
int PointGraph::getParameterInt(const std::string& name, int fallback) const {
    const auto found = parameters_.find(name);
    if (found == parameters_.end() || (found->second.kind != "int" &&
                                       found->second.kind != "bool"))
        return fallback;
    return intValue(nodes_.at(found->second.nodeId), found->second.key, fallback);
}
std::string PointGraph::getParameterString(const std::string& name,
                                            const std::string& fallback) const {
    const auto found = parameters_.find(name);
    if (found == parameters_.end() || found->second.kind != "string") return fallback;
    return stringValue(nodes_.at(found->second.nodeId), found->second.key, fallback);
}
bool PointGraph::clearParameterOverride(const std::string& name) {
    const auto found = parameters_.find(name);
    if (found == parameters_.end() || !hasParameterOverride(name)) return false;
    floatOverrides_.erase(name);
    intOverrides_.erase(name);
    stringOverrides_.erase(name);
    invalidateFrom(found->second.nodeId);
    return true;
}

bool PointGraph::setNodeSubgraph(const std::string& id, PointGraph* graph,
                                 const std::string& inputNode, const std::string& outputNode) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || found->second.operation != "subgraph" || !graph ||
        !graph->hasNode(inputNode) || !graph->hasNode(outputNode))
        return false;
    found->second.subgraph       = std::make_shared<PointGraph>(*graph);
    found->second.subgraphInput  = inputNode;
    found->second.subgraphOutput = outputNode;
    invalidateFrom(id);
    return true;
}

PointSet* PointGraph::execute(const std::string& outputId) {
    auto result = executeResult(outputId);
    error_      = result.ok() ? std::string{} : result.status().describe();
    if (!result.ok()) return nullptr;
    return new PointSet(std::move(result).takeValue());
}

Result<PointSet> PointGraph::executeResult(std::string_view outputId) {
    const std::string output(outputId);
    metrics_.clear();
    cacheHitCount_ = 0;
    evaluatedNodes_ = 0;
    lastCancelled_ = false;
    computeFallbackReason_.clear();
    ++executionCount_;
    if (cancelRequested_) {
        lastCancelled_ = true;
        return Result<PointSet>::failure(
            Diagnostic::error(DiagnosticCode::Cancelled, "execution cancelled", output, {}, "procgen.pointGraph"));
    }
    auto compiled = compileExecutionPlan(output);
    if (!compiled.ok()) return Result<PointSet>::failure(compiled.status());
    std::unordered_map<std::string, int> states;
    auto                                 result = evaluate(output, states);
    if (!result.ok()) {
        lastCancelled_ = result.status().code() == StatusCode::Cancelled;
        return Result<PointSet>::failure(result.status());
    }
    return Result<PointSet>::success(result.value().get());
}

void PointGraph::setExecutionNodeBudget(int nodes) { executionNodeBudget_ = std::max(0, nodes); }
int  PointGraph::getExecutionNodeBudget() const { return executionNodeBudget_; }
void PointGraph::setMaxNodeOutputPoints(int points) {
    const int clamped = std::max(0, points);
    if (maxNodeOutputPoints_ == clamped) return;
    maxNodeOutputPoints_ = clamped;
    clearCache();
}
int  PointGraph::getMaxNodeOutputPoints() const { return maxNodeOutputPoints_; }
bool PointGraph::setComputePolicy(const std::string& policy) {
    if (policy != "auto" && policy != "gpu" && policy != "cpu") return false;
    if (computePolicy_ == policy) return true;
    computePolicy_ = policy;
    clearCache();
    return true;
}
std::string PointGraph::getComputePolicy() const { return computePolicy_; }
void        PointGraph::setComputeMinimumPoints(int points) {
    const int clamped = std::max(0, points);
    if (computeMinimumPoints_ == clamped) return;
    computeMinimumPoints_ = clamped;
    clearCache();
}
int  PointGraph::getComputeMinimumPoints() const { return computeMinimumPoints_; }
void PointGraph::requestCancel() { cancelRequested_ = true; }
void PointGraph::resetCancellation() {
    cancelRequested_ = false;
    lastCancelled_   = false;
}
bool PointGraph::wasCancelled() const { return lastCancelled_; }

bool PointGraph::validate() {
    auto result = validateResult();
    error_      = result.ok() ? std::string{} : result.status().describe();
    return result.ok();
}

Result<void> PointGraph::validateResult() const {
    std::unordered_map<std::string, int> states;
    for (const auto& id : nodeOrder_) {
        auto result = validateNode(id, states);
        if (!result.ok()) return result;
    }
    return Result<void>::success();
}

std::string PointGraph::getError() const { return error_; }
void PointGraph::clearCache() {
    for (auto& [id, node] : nodes_) {
        node.cacheValid             = false;
        node.deferredTransformValid = false;
    }
    metrics_.clear();
}
int PointGraph::getExecutionCount() const { return executionCount_; }
int PointGraph::getCacheHitCount() const { return cacheHitCount_; }
int PointGraph::getCompiledSegmentCount() const { return executionPlan_ ? int(executionPlan_->segments.size()) : 0; }
uint64_t    PointGraph::getExecutionPlanBuildCount() const { return executionPlanBuildCount_; }
uint64_t PointGraph::getRevision() const { return revision_; }
int PointGraph::getMetricCount() const { return int(metrics_.size()); }
std::string PointGraph::getMetricNodeId(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].id : std::string();
}
int PointGraph::getMetricOutputCount(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].outputCount : 0;
}
float PointGraph::getMetricMilliseconds(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].milliseconds : 0.f;
}
bool PointGraph::isMetricCacheHit(int index) const {
    return index >= 0 && index < int(metrics_.size()) && metrics_[size_t(index)].cacheHit;
}
std::string PointGraph::getMetricBackend(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].backend : std::string();
}
std::string PointGraph::getComputeFallbackReason() const { return computeFallbackReason_; }
uint64_t    PointGraph::getComputeUploadCount() const { return pointCompute_->getUploadCount(); }
uint64_t    PointGraph::getComputeDispatchCount() const { return pointCompute_->getDispatchCount(); }
uint64_t    PointGraph::getComputeReadbackCount() const { return pointCompute_->getReadbackCount(); }
uint64_t    PointGraph::getComputeBufferReuseCount() const { return pointCompute_->getBufferReuseCount(); }
uint64_t    PointGraph::getComputePeakBufferBytes() const { return pointCompute_->getPeakBufferBytes(); }
int         PointGraph::getLastFusedTransformCount() const { return pointCompute_->getLastFusedTransformCount(); }
float PointGraph::getMetricMinX(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].minX : 0.f;
}
float PointGraph::getMetricMinY(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].minY : 0.f;
}
float PointGraph::getMetricMinZ(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].minZ : 0.f;
}
float PointGraph::getMetricMaxX(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].maxX : 0.f;
}
float PointGraph::getMetricMaxY(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].maxY : 0.f;
}
float PointGraph::getMetricMaxZ(int index) const {
    return index >= 0 && index < int(metrics_.size()) ? metrics_[size_t(index)].maxZ : 0.f;
}
float PointGraph::getMetricAverageDensity(int index) const {
    return index >= 0 && index < int(metrics_.size())
               ? metrics_[size_t(index)].averageDensity
               : 0.f;
}
PointSet* PointGraph::getNodeOutput(const std::string& id) const { return materializeNodeOutput(id); }

PointSet* PointGraph::materializeNodeOutput(const std::string& id) const {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) return nullptr;
    const Node& node = found->second;
    if (node.cacheValid) return new PointSet(node.cache);
    if (!node.deferredTransformValid || node.operation != "transform" || node.inputs[0].empty()) return nullptr;
    std::unique_ptr<PointSet> input(materializeNodeOutput(node.inputs[0]));
    if (!input) return nullptr;
    return new PointSet(transformPointSet(*input, floatValue(node, "x", 0.f), floatValue(node, "y", 0.f),
                                          floatValue(node, "z", 0.f), floatValue(node, "yaw", 0.f),
                                          floatValue(node, "scaleX", 1.f), floatValue(node, "scaleY", 1.f),
                                          floatValue(node, "scaleZ", 1.f)));
}

std::string PointGraph::debugReport() const {
    std::ostringstream out;
    out << "nodes=" << nodes_.size() << " executions=" << executionCount_ << " cacheHits=" << cacheHitCount_
        << " planBuilds=" << executionPlanBuildCount_ << " segments=" << getCompiledSegmentCount();
    for (const auto& metric : metrics_)
        out << "\n"
            << metric.id << " count=" << metric.outputCount << " ms=" << metric.milliseconds
            << " backend=" << metric.backend << (metric.cacheHit ? " cached" : "");
    if (!error_.empty()) out << "\nerror=" << error_;
    return out.str();
}

int PointGraph::getOperationCount() { return int(operationSpecs().size()); }
std::string PointGraph::getOperationId(int index) {
    return index >= 0 && index < int(operationSpecs().size())
               ? operationSpecs()[size_t(index)].id
               : std::string();
}
int PointGraph::getOperationInputCount(const std::string& operation) {
    const auto* spec = operationSpec(operation);
    return spec ? spec->inputs : -1;
}
int PointGraph::getOperationParamCount(const std::string& operation) {
    const auto* spec = operationSpec(operation);
    return spec ? int(spec->params.size()) : 0;
}
std::string PointGraph::getOperationParamKey(const std::string& operation, int index) {
    const auto* spec = operationSpec(operation);
    return spec && index >= 0 && index < int(spec->params.size())
               ? spec->params[size_t(index)].key
               : std::string();
}
std::string PointGraph::getOperationParamKind(const std::string& operation, int index) {
    const auto* spec = operationSpec(operation);
    return spec && index >= 0 && index < int(spec->params.size())
               ? spec->params[size_t(index)].kind
               : std::string();
}
std::string PointGraph::getOperationParamDefault(const std::string& operation, int index) {
    const auto* spec = operationSpec(operation);
    return spec && index >= 0 && index < int(spec->params.size())
               ? spec->params[size_t(index)].defaultValue
               : std::string();
}

void PointGraph::invalidate() { clearCache(); }

void PointGraph::invalidateFrom(const std::string& id) {
    ++revision_;
    std::vector<std::string> dirty{id};
    std::unordered_map<std::string, bool> visited;
    for (size_t cursor = 0; cursor < dirty.size(); ++cursor) {
        const std::string current = dirty[cursor];
        if (visited[current]) continue;
        visited[current] = true;
        const auto found = nodes_.find(current);
        if (found != nodes_.end()) {
            found->second.cacheValid             = false;
            found->second.deferredTransformValid = false;
        }
        for (const auto& [candidateId, candidate] : nodes_) {
            if (candidate.inputs[0] == current || candidate.inputs[1] == current)
                dirty.push_back(candidateId);
        }
    }
    metrics_.clear();
}
float PointGraph::floatValue(const Node& node, const std::string& key, float fallback) const {
    for (const auto& name : parameterOrder_) {
        const auto& binding = parameters_.at(name);
        if (binding.nodeId == node.id && binding.key == key) {
            const auto overridden = floatOverrides_.find(name);
            if (overridden != floatOverrides_.end()) return overridden->second;
        }
    }
    const auto found = node.floats.find(key);
    return found == node.floats.end() ? fallback : found->second;
}
int PointGraph::intValue(const Node& node, const std::string& key, int fallback) const {
    for (const auto& name : parameterOrder_) {
        const auto& binding = parameters_.at(name);
        if (binding.nodeId == node.id && binding.key == key) {
            const auto overridden = intOverrides_.find(name);
            if (overridden != intOverrides_.end()) return overridden->second;
        }
    }
    const auto found = node.ints.find(key);
    return found == node.ints.end() ? fallback : found->second;
}
std::string PointGraph::stringValue(const Node& node, const std::string& key,
                                    const std::string& fallback) const {
    for (const auto& name : parameterOrder_) {
        const auto& binding = parameters_.at(name);
        if (binding.nodeId == node.id && binding.key == key) {
            const auto overridden = stringOverrides_.find(name);
            if (overridden != stringOverrides_.end()) return overridden->second;
        }
    }
    const auto found = node.strings.find(key);
    return found == node.strings.end() ? fallback : found->second;
}

}  // namespace eve::procgen
