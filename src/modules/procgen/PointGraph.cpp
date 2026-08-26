#include "procgen/PointGraph.h"

#include <algorithm>
#include <chrono>
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

uint64_t nowNanoseconds() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

}  // namespace

bool PointGraph::addNode(const std::string& id, const std::string& operation) {
    if (id.empty() || !operationKnown(operation) || nodes_.find(id) != nodes_.end()) return false;
    nodes_[id].id        = id;
    nodes_[id].operation = operation;
    nodeOrder_.push_back(id);
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
    invalidateFrom(toId);
    return true;
}

bool PointGraph::disconnect(const std::string& toId, int inputIndex) {
    const auto to = nodes_.find(toId);
    if (to == nodes_.end() || inputIndex < 0 || inputIndex > 1 ||
        to->second.inputs[inputIndex].empty())
        return false;
    to->second.inputs[inputIndex].clear();
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

bool PointGraph::setNodeFloat(const std::string& id, const std::string& key, float value) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || key.empty()) return false;
    found->second.floats[key] = value;
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeInt(const std::string& id, const std::string& key, int value) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || key.empty()) return false;
    found->second.ints[key] = value;
    invalidateFrom(id);
    return true;
}

bool PointGraph::setNodeString(const std::string& id, const std::string& key,
                               const std::string& value) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || key.empty()) return false;
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
    const auto parameter = std::find_if(spec->params.begin(), spec->params.end(),
                                        [&](const OperationParam& value) {
                                            return key == value.key;
                                        });
    if (parameter == spec->params.end()) return false;
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
    error_.clear();
    metrics_.clear();
    cacheHitCount_ = 0;
    evaluatedNodes_ = 0;
    lastCancelled_ = false;
    ++executionCount_;
    if (cancelRequested_) {
        error_ = "execution cancelled";
        lastCancelled_ = true;
        return nullptr;
    }
    std::unordered_map<std::string, int> states;
    const PointSet* result = evaluate(outputId, states);
    return result ? new PointSet(*result) : nullptr;
}

void PointGraph::setExecutionNodeBudget(int nodes) { executionNodeBudget_ = std::max(0, nodes); }
int  PointGraph::getExecutionNodeBudget() const { return executionNodeBudget_; }
void PointGraph::requestCancel() { cancelRequested_ = true; }
void PointGraph::resetCancellation() {
    cancelRequested_ = false;
    lastCancelled_   = false;
}
bool PointGraph::wasCancelled() const { return lastCancelled_; }

bool PointGraph::validate() {
    error_.clear();
    std::unordered_map<std::string, int> states;
    for (const auto& id : nodeOrder_)
        if (!validateNode(id, states)) return false;
    return true;
}

std::string PointGraph::getError() const { return error_; }
void PointGraph::clearCache() {
    for (auto& [id, node] : nodes_) node.cacheValid = false;
    metrics_.clear();
}
int PointGraph::getExecutionCount() const { return executionCount_; }
int PointGraph::getCacheHitCount() const { return cacheHitCount_; }
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
PointSet* PointGraph::getNodeOutput(const std::string& id) const {
    const auto found = nodes_.find(id);
    return found != nodes_.end() && found->second.cacheValid ? new PointSet(found->second.cache)
                                                             : nullptr;
}

std::string PointGraph::debugReport() const {
    std::ostringstream out;
    out << "nodes=" << nodes_.size() << " executions=" << executionCount_
        << " cacheHits=" << cacheHitCount_;
    for (const auto& metric : metrics_)
        out << "\n" << metric.id << " count=" << metric.outputCount << " ms="
            << metric.milliseconds << (metric.cacheHit ? " cached" : "");
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

const PointSet* PointGraph::evaluate(const std::string& id,
                                     std::unordered_map<std::string, int>& states) {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) {
        error_ = "unknown node: " + id;
        return nullptr;
    }
    Node& node = found->second;
    if (node.cacheValid) {
        ++cacheHitCount_;
        metrics_.push_back({id, node.cache.getCount(), 0.f, true});
        return &node.cache;
    }
    if (states[id] == 1) {
        error_ = "cycle at node: " + id;
        return nullptr;
    }
    if (cancelRequested_) {
        error_ = "execution cancelled at node: " + id;
        lastCancelled_ = true;
        return nullptr;
    }
    states[id] = 1;
    const uint64_t started = nowNanoseconds();
    const PointSet* first  = nullptr;
    const PointSet* second = nullptr;
    if (!node.inputs[0].empty()) {
        first = evaluate(node.inputs[0], states);
        if (!first) return nullptr;
    }
    if (!node.inputs[1].empty()) {
        second = evaluate(node.inputs[1], states);
        if (!second) return nullptr;
    }
    if (executionNodeBudget_ > 0 && evaluatedNodes_ >= executionNodeBudget_) {
        error_ = "execution node budget exceeded at node: " + id;
        lastCancelled_ = true;
        return nullptr;
    }
    ++evaluatedNodes_;

    if (node.operation == "input") {
        if (!node.hasPoints) error_ = "input node has no points: " + id;
        else node.cache = node.points;
    } else if (node.operation == "spatial.sample") {
        if (!node.spatial) error_ = "spatial.sample has no spatial data: " + id;
        else
            node.cache = node.spatial->sample(floatValue(node, "spacing", 1.f),
                                              uint32_t(intValue(node, "seed", 1)),
                                              floatValue(node, "jitter", 0.f));
    } else if (node.operation == "spatial.filter") {
        if (!first || !node.spatial) error_ = "spatial.filter requires input and spatial data: " + id;
        else node.cache = node.spatial->filter(*first, intValue(node, "invert", 0) != 0);
    } else if (node.operation == "spatial.project") {
        if (!first || !node.spatial) error_ = "spatial.project requires input and spatial data: " + id;
        else node.cache = node.spatial->project(*first);
    } else if (node.operation == "merge") {
        if (!first || !second) error_ = "merge requires two inputs: " + id;
        else node.cache = mergePointSets(*first, *second);
    } else if (node.operation == "copy.points") {
        const int maxPoints = intValue(node, "maxPoints", 100000);
        const uint64_t outputCount = first && second
                                         ? uint64_t(first->getCount()) * uint64_t(second->getCount())
                                         : 0;
        if (!first || !second) error_ = "copy.points requires source and target inputs: " + id;
        else if (maxPoints < 0 || outputCount > uint64_t(maxPoints))
            error_ = "copy.points output exceeds maxPoints at node: " + id;
        else
            node.cache = copyPointsToTargets(
                *first, *second, intValue(node, "inheritTargetAttributes", 1) != 0);
    } else if (node.operation == "density.remap") {
        const float inputMin = floatValue(node, "inputMin", 0.f);
        const float inputMax = floatValue(node, "inputMax", 1.f);
        if (!first) error_ = "density.remap requires input: " + id;
        else if (inputMin == inputMax)
            error_ = "density.remap requires a non-zero input range: " + id;
        else
            node.cache = remapPointDensity(
                *first, inputMin, inputMax, floatValue(node, "outputMin", 0.f),
                floatValue(node, "outputMax", 1.f), intValue(node, "clamp", 1) != 0);
    } else if (node.operation == "attribute.math.float") {
        const std::string attribute = stringValue(node, "attribute");
        std::string outputAttribute = stringValue(node, "outputAttribute");
        if (outputAttribute.empty()) outputAttribute = attribute;
        const std::string operation = stringValue(node, "operation", "multiply");
        const float operand = floatValue(node, "operand", 1.f);
        const bool operationKnown = operation == "add" || operation == "subtract" ||
                                    operation == "multiply" || operation == "divide" ||
                                    operation == "min" || operation == "max";
        if (!first) error_ = "attribute.math.float requires input: " + id;
        else if (attribute.empty()) error_ = "attribute.math.float requires an attribute: " + id;
        else if (!operationKnown)
            error_ = "attribute.math.float has invalid operation at node: " + id;
        else if (operation == "divide" && operand == 0.f)
            error_ = "attribute.math.float cannot divide by zero at node: " + id;
        else
            node.cache = mathPointFloatAttribute(
                *first, attribute, outputAttribute, operation, operand,
                floatValue(node, "defaultValue", 0.f));
    } else if (node.operation == "transform") {
        if (!first) error_ = "transform requires input: " + id;
        else
            node.cache = transformPointSet(
                *first, floatValue(node, "x", 0.f), floatValue(node, "y", 0.f),
                floatValue(node, "z", 0.f), floatValue(node, "yaw", 0.f),
                floatValue(node, "scaleX", 1.f), floatValue(node, "scaleY", 1.f),
                floatValue(node, "scaleZ", 1.f));
    } else if (node.operation == "filter.float") {
        if (!first) error_ = "filter.float requires input: " + id;
        else
            node.cache = filterPointFloatAttribute(
                *first, stringValue(node, "attribute"), floatValue(node, "min", 0.f),
                floatValue(node, "max", 1.f), intValue(node, "invert", 0) != 0);
    } else if (node.operation == "filter.string") {
        if (!first) error_ = "filter.string requires input: " + id;
        else
            node.cache = filterPointStringAttribute(
                *first, stringValue(node, "attribute"), stringValue(node, "value"),
                intValue(node, "invert", 0) != 0);
    } else if (node.operation == "attribute.set.float") {
        if (!first) error_ = "attribute.set.float requires input: " + id;
        else {
            node.cache = *first;
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty()) error_ = "attribute.set.float requires attribute: " + id;
            else
                for (int i = 0; i < node.cache.getCount(); ++i)
                    node.cache.setFloatAttribute(i, attribute, floatValue(node, "value", 0.f));
        }
    } else if (node.operation == "attribute.set.string") {
        if (!first) error_ = "attribute.set.string requires input: " + id;
        else {
            node.cache = *first;
            const std::string attribute = stringValue(node, "attribute");
            if (attribute.empty()) error_ = "attribute.set.string requires attribute: " + id;
            else
                for (int i = 0; i < node.cache.getCount(); ++i)
                    node.cache.setStringAttribute(i, attribute, stringValue(node, "value"));
        }
    } else if (node.operation == "density.cull") {
        if (!first) error_ = "density.cull requires input: " + id;
        else
            node.cache = densityCullPoints(*first, uint32_t(intValue(node, "seed", 1)),
                                           floatValue(node, "multiplier", 1.f));
    } else if (node.operation == "self.prune") {
        if (!first) error_ = "self.prune requires input: " + id;
        else node.cache = selfPrunePoints(*first, floatValue(node, "radius", 1.f));
    } else if (node.operation == "jitter") {
        if (!first) error_ = "jitter requires input: " + id;
        else
            node.cache = jitterPointPositions(*first, uint32_t(intValue(node, "seed", 1)),
                                              floatValue(node, "x", 0.f),
                                              floatValue(node, "z", 0.f));
    } else if (node.operation == "branch") {
        const bool condition = intValue(node, "condition", 0) != 0;
        if ((condition && !first) || (!condition && !second))
            error_ = "branch requires both selected inputs: " + id;
        else node.cache = condition ? *first : *second;
    } else if (node.operation == "subgraph") {
        if (!first || !node.subgraph) error_ = "subgraph requires input and graph: " + id;
        else if (!node.subgraph->setNodePoints(node.subgraphInput, const_cast<PointSet*>(first)))
            error_ = "subgraph input node is invalid: " + id;
        else {
            std::unique_ptr<PointSet> result(node.subgraph->execute(node.subgraphOutput));
            if (!result) error_ = "subgraph failed at " + id + ": " + node.subgraph->getError();
            else node.cache = *result;
        }
    }

    if (!error_.empty()) return nullptr;
    node.cacheValid = true;
    states[id]      = 2;
    const float elapsed = float(nowNanoseconds() - started) * 0.000001f;
    metrics_.push_back({id, node.cache.getCount(), elapsed, false});
    return &node.cache;
}

bool PointGraph::validateNode(const std::string& id, std::unordered_map<std::string, int>& states) {
    if (states[id] == 2) return true;
    if (states[id] == 1) {
        error_ = "cycle at node: " + id;
        return false;
    }
    const auto found = nodes_.find(id);
    if (found == nodes_.end() || !operationKnown(found->second.operation)) {
        error_ = "invalid node: " + id;
        return false;
    }
    states[id] = 1;
    for (const auto& input : found->second.inputs) {
        if (!input.empty() && !validateNode(input, states)) return false;
    }
    const auto& node = found->second;
    if (node.operation == "input" && !node.hasPoints) error_ = "input node has no points: " + id;
    else if ((node.operation == "spatial.sample" || node.operation == "spatial.filter" ||
              node.operation == "spatial.project") &&
             !node.spatial)
        error_ = "node has no spatial data: " + id;
    else if (node.operation == "merge" && (node.inputs[0].empty() || node.inputs[1].empty()))
        error_ = "merge requires two inputs: " + id;
    else if (node.operation == "branch" && (node.inputs[0].empty() || node.inputs[1].empty()))
        error_ = "branch requires two inputs: " + id;
    else if (node.operation == "subgraph" && (!node.subgraph || node.inputs[0].empty()))
        error_ = "subgraph is not configured: " + id;
    else if (node.operation == "subgraph") {
        PointGraph nested = *node.subgraph;
        PointSet   externalInput;
        if (!nested.setNodePoints(node.subgraphInput, &externalInput) || !nested.validate())
            error_ = "invalid subgraph at " + id + ": " + nested.getError();
    }
    else if (node.operation != "input" && node.operation != "spatial.sample" &&
             node.operation != "merge" && node.inputs[0].empty())
        error_ = "node requires input: " + id;
    if (!error_.empty()) return false;
    states[id] = 2;
    return true;
}

void PointGraph::invalidate() { clearCache(); }
void PointGraph::invalidateFrom(const std::string& id) {
    ++revision_;
    std::vector<std::string> dirty{id};
    std::unordered_map<std::string, bool> visited;
    for (size_t cursor = 0; cursor < dirty.size(); ++cursor) {
        const std::string& current = dirty[cursor];
        if (visited[current]) continue;
        visited[current] = true;
        const auto found = nodes_.find(current);
        if (found != nodes_.end()) found->second.cacheValid = false;
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
