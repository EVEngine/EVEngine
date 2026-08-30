#include "procgen_editing/PcgGraph.h"

#include "procgen/PointGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>

namespace eve::procgen_editing {
namespace {

template <class T>
EditorResult<T> pcgGraphError(const char* rule, std::string message) {
    return EditorResult<T>::error(EditorStatus::Rejected, RuleId(rule), std::move(message));
}

const GraphNodeRecord* findNode(const GraphDocumentData& graph, const GraphNodeId& id) {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                    [&](const GraphNodeRecord& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

const GraphPinRecord* findPin(const GraphDocumentData& graph, const GraphPinId& id) {
    for (const auto& node : graph.nodes) {
        const auto found = std::find_if(node.pins.begin(), node.pins.end(),
                                        [&](const GraphPinRecord& pin) { return pin.id == id; });
        if (found != node.pins.end()) return &*found;
    }
    return nullptr;
}

int inputIndex(const GraphNodeRecord& node, const GraphPinId& pin) {
    int index = 0;
    for (const auto& candidate : node.pins) {
        if (candidate.direction != GraphPinDirection::Input) continue;
        if (candidate.id == pin) return index;
        ++index;
    }
    return -1;
}

EditorValue defaultValue(const std::string& kind, const std::string& encoded) {
    if (kind == "bool") return EditorValue(encoded == "true");
    if (kind == "int") return EditorValue(static_cast<int64_t>(std::stoll(encoded)));
    if (kind == "float") return EditorValue(std::stod(encoded));
    return EditorValue(encoded);
}

}  // namespace

GraphConnectionDecision PcgPointGraphDomain::canConnect(const GraphPinRecord& from,
                                                         const GraphPinRecord& to) const {
    GraphConnectionDecision decision;
    decision.allowed = from.direction == GraphPinDirection::Output &&
                       to.direction == GraphPinDirection::Input && from.type == "point" &&
                       to.type == "point" && from.node != to.node;
    if (!decision.allowed)
        decision.diagnostics.push_back(
            {RuleId("editor.pcg.invalid-connection"), DiagnosticSeverity::Error,
             "PCG graph connections require point output-to-input pins on distinct nodes"});
    return decision;
}

EditorResult<GraphNodeRecord> PcgPointGraphDomain::makeNode(const GraphNodeId& id,
                                                            const std::string& operation) const {
    if (id.empty() || procgen::PointGraph::getOperationInputCount(operation) < 0)
        return pcgGraphError<GraphNodeRecord>("editor.pcg.unknown-operation",
                                              "Unknown PointGraph operation");
    GraphNodeRecord node;
    node.id   = id;
    node.type = operation;
    EditorValue::Object properties;
    const int parameterCount = procgen::PointGraph::getOperationParamCount(operation);
    for (int i = 0; i < parameterCount; ++i) {
        const std::string key = procgen::PointGraph::getOperationParamKey(operation, i);
        properties[key] = defaultValue(procgen::PointGraph::getOperationParamKind(operation, i),
                                       procgen::PointGraph::getOperationParamDefault(operation, i));
    }
    if (operation == "subgraph") {
        properties["definition"] = EditorValue("");
        properties["inputNode"]  = EditorValue("input");
        properties["outputNode"] = EditorValue("output");
    }
    node.properties = EditorValue(std::move(properties));
    const int inputs = procgen::PointGraph::getOperationInputCount(operation);
    for (int i = 0; i < inputs; ++i)
        node.pins.push_back({GraphPinId(id.value() + ".in" + std::to_string(i)), id, "point",
                             GraphPinDirection::Input});
    node.pins.push_back(
        {GraphPinId(id.value() + ".out"), id, "point", GraphPinDirection::Output});
    return EditorResult<GraphNodeRecord>::applied(std::move(node));
}

PcgGraphMigrationResult PcgPointGraphDomain::migrate(const GraphDocumentData& graph) const {
    PcgGraphMigrationResult result;
    result.fromVersion = graph.schemaVersion;
    result.graph       = graph;
    if (graph.domain != domain()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.pcg.wrong-domain"),
                                      DiagnosticSeverity::Error,
                                      "Graph is not a procgen.point document"});
        return result;
    }
    if (graph.schemaVersion == 1) {
        result.status = EditorStatus::NoOp;
        return result;
    }
    if (graph.schemaVersion != 0) {
        result.status = EditorStatus::Unsupported;
        result.diagnostics.push_back(
            {RuleId("editor.pcg.unsupported-schema"), DiagnosticSeverity::Error,
             "Unsupported procgen.point graph schemaVersion: " +
                 std::to_string(graph.schemaVersion)});
        return result;
    }

    std::unordered_map<std::string, GraphPinId> pinRemap;
    std::vector<GraphNodeRecord> migratedNodes;
    migratedNodes.reserve(graph.nodes.size());
    for (const auto& legacy : graph.nodes) {
        auto canonical = makeNode(legacy.id, legacy.type);
        if (!canonical.value) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(
                {RuleId("editor.pcg.migration-unknown-operation"), DiagnosticSeverity::Error,
                 "Cannot migrate unknown PCG operation: " + legacy.type});
            return result;
        }
        auto* defaults = canonical.value->properties.getIf<EditorValue::Object>();
        const auto* properties = legacy.properties.getIf<EditorValue::Object>();
        if (legacy.properties.type() != EditorValue::Type::Null && !properties) {
            result.status = EditorStatus::Rejected;
            result.diagnostics.push_back(
                {RuleId("editor.pcg.migration-invalid-properties"), DiagnosticSeverity::Error,
                 "Legacy PCG node properties must be an object: " + legacy.id.value()});
            return result;
        }
        if (properties)
            for (const auto& [key, value] : *properties) (*defaults)[key] = value;

        int input = 0;
        int output = 0;
        for (const auto& pin : legacy.pins) {
            const int ordinal = pin.direction == GraphPinDirection::Input ? input++ : output++;
            int canonicalOrdinal = 0;
            const auto replacement = std::find_if(
                canonical.value->pins.begin(), canonical.value->pins.end(),
                [&](const GraphPinRecord& candidate) {
                    if (candidate.direction != pin.direction) return false;
                    return canonicalOrdinal++ == ordinal;
                });
            if (replacement == canonical.value->pins.end()) continue;
            if (!pinRemap.emplace(pin.id.value(), replacement->id).second) {
                result.status = EditorStatus::Rejected;
                result.diagnostics.push_back(
                    {RuleId("editor.pcg.migration-duplicate-pin"), DiagnosticSeverity::Error,
                     "Legacy PCG graph contains duplicate pin id: " + pin.id.value()});
                return result;
            }
        }
        migratedNodes.push_back(std::move(*canonical.value));
    }
    result.graph.nodes = std::move(migratedNodes);
    for (auto& edge : result.graph.edges) {
        const auto from = pinRemap.find(edge.from.value());
        const auto to   = pinRemap.find(edge.to.value());
        if (from != pinRemap.end()) edge.from = from->second;
        if (to != pinRemap.end()) edge.to = to->second;
    }
    result.graph.schemaVersion = 1;
    result.status              = EditorStatus::Applied;
    result.diagnostics.push_back(
        {RuleId("editor.pcg.migrated-v0-v1"), DiagnosticSeverity::Info,
         "Migrated procgen.point graph schemaVersion 0 to 1"});
    return result;
}

PcgGraphCompileResult PcgPointGraphDomain::compile(const GraphDocumentData& graph) const {
    PcgGraphCompileResult result;
    result.documentRevision = graph.revision;
    const auto migration = migrate(graph);
    if (migration.status != EditorStatus::Applied && migration.status != EditorStatus::NoOp) {
        result.status      = migration.status;
        result.diagnostics = migration.diagnostics;
        return result;
    }
    result.diagnostics = migration.diagnostics;
    const auto& source = migration.graph;
    procgen::PointGraph compiled;
    for (const auto& node : source.nodes) {
        if (!compiled.addNode(node.id.value(), node.type)) {
            result.status = EditorStatus::Failed;
            result.diagnostics.push_back({RuleId("editor.pcg.invalid-node"), DiagnosticSeverity::Error,
                                          "Unknown or duplicated PCG node: " + node.id.value()});
            return result;
        }
        const auto* properties = node.properties.getIf<EditorValue::Object>();
        if (!properties) {
            result.status = EditorStatus::Failed;
            result.diagnostics.push_back(
                {RuleId("editor.pcg.invalid-properties"), DiagnosticSeverity::Error,
                 "PCG node properties must be an object: " + node.id.value()});
            return result;
        }
        for (const auto& [key, value] : *properties) {
            const bool subgraphProperty =
                node.type == "subgraph" &&
                (key == "definition" || key == "inputNode" || key == "outputNode");
            if (subgraphProperty) continue;
            int parameterIndex = -1;
            const int parameterCount = procgen::PointGraph::getOperationParamCount(node.type);
            for (int index = 0; index < parameterCount; ++index)
                if (procgen::PointGraph::getOperationParamKey(node.type, index) == key) {
                    parameterIndex = index;
                    break;
                }
            const std::string kind = parameterIndex >= 0
                                         ? procgen::PointGraph::getOperationParamKind(
                                               node.type, parameterIndex)
                                         : std::string();
            bool applied = false;
            if (kind == "bool") {
                if (const auto* boolean = value.getIf<bool>())
                    applied = compiled.setNodeInt(node.id.value(), key, *boolean ? 1 : 0);
            } else if (kind == "int") {
                if (const auto* integer = value.getIf<int64_t>();
                    integer && *integer >= std::numeric_limits<int>::min() &&
                    *integer <= std::numeric_limits<int>::max())
                    applied = compiled.setNodeInt(node.id.value(), key, int(*integer));
            } else if (kind == "float") {
                if (const auto* number = value.getIf<double>();
                    number && std::isfinite(*number) &&
                    std::abs(*number) <= std::numeric_limits<float>::max())
                    applied = compiled.setNodeFloat(node.id.value(), key, float(*number));
            } else if (kind == "string") {
                if (const auto* text = value.getIf<std::string>())
                    applied = compiled.setNodeString(node.id.value(), key, *text);
            }
            if (!applied) {
                result.status = EditorStatus::Failed;
                result.diagnostics.push_back(
                    {RuleId(parameterIndex < 0 ? "editor.pcg.unknown-property"
                                               : "editor.pcg.property-type-mismatch"),
                     DiagnosticSeverity::Error,
                     "Invalid PCG property " + node.id.value() + "." + key});
                return result;
            }
        }
        if (node.type == "subgraph") {
            const auto definition = properties->find("definition");
            const auto inputNode  = properties->find("inputNode");
            const auto outputNode = properties->find("outputNode");
            const auto* definitionText =
                definition == properties->end() ? nullptr : definition->second.getIf<std::string>();
            const auto* inputText =
                inputNode == properties->end() ? nullptr : inputNode->second.getIf<std::string>();
            const auto* outputText =
                outputNode == properties->end() ? nullptr : outputNode->second.getIf<std::string>();
            procgen::PointGraph nested;
            if (!definitionText || !inputText || !outputText || definitionText->empty() ||
                !nested.deserializeDefinition(*definitionText) ||
                !compiled.setNodeSubgraph(node.id.value(), &nested, *inputText, *outputText)) {
                result.status = EditorStatus::Failed;
                result.diagnostics.push_back(
                    {RuleId("editor.pcg.invalid-subgraph"), DiagnosticSeverity::Error,
                     "PCG subgraph node requires a valid definition and input/output node ids"});
                return result;
            }
        }
    }
    if (source.parameters.type() != EditorValue::Type::Null) {
        const auto* parameters = source.parameters.getIf<EditorValue::Object>();
        if (!parameters) {
            result.status = EditorStatus::Failed;
            result.diagnostics.push_back(
                {RuleId("editor.pcg.invalid-parameters"), DiagnosticSeverity::Error,
                 "PCG graph parameters must be an object"});
            return result;
        }
        for (const auto& [name, value] : *parameters) {
            const auto* binding = value.getIf<EditorValue::Object>();
            const auto node = binding ? binding->find("node") : EditorValue::Object::const_iterator{};
            const auto key = binding ? binding->find("key") : EditorValue::Object::const_iterator{};
            const auto* nodeId = binding && node != binding->end()
                                     ? node->second.getIf<std::string>()
                                     : nullptr;
            const auto* parameterKey = binding && key != binding->end()
                                           ? key->second.getIf<std::string>()
                                           : nullptr;
            if (!nodeId || !parameterKey ||
                !compiled.exposeParameter(name, *nodeId, *parameterKey)) {
                result.status = EditorStatus::Failed;
                result.diagnostics.push_back(
                    {RuleId("editor.pcg.invalid-parameter-binding"), DiagnosticSeverity::Error,
                     "Invalid PCG graph parameter binding: " + name});
                return result;
            }
        }
    }
    std::unordered_map<std::string, StableId> connectedInputs;
    for (const auto& edge : source.edges) {
        const GraphPinRecord* from = findPin(source, edge.from);
        const GraphPinRecord* to   = findPin(source, edge.to);
        const GraphNodeRecord* toNode = to ? findNode(source, to->node) : nullptr;
        const int slot = toNode && to ? inputIndex(*toNode, to->id) : -1;
        const std::string inputKey = to ? to->id.value() : std::string();
        if (!inputKey.empty() && connectedInputs.find(inputKey) != connectedInputs.end()) {
            result.status = EditorStatus::Failed;
            result.diagnostics.push_back(
                {RuleId("editor.pcg.duplicate-input"), DiagnosticSeverity::Error,
                 "PCG input pin has more than one incoming edge: " + inputKey});
            return result;
        }
        if (!from || !to || !toNode || !canConnect(*from, *to).allowed || slot < 0 ||
            !compiled.connect(from->node.value(), to->node.value(), slot)) {
            result.status = EditorStatus::Failed;
            result.diagnostics.push_back({RuleId("editor.pcg.invalid-edge"), DiagnosticSeverity::Error,
                                          "Invalid PCG graph edge: " + edge.id.value()});
            return result;
        }
        connectedInputs.emplace(inputKey, edge.id);
    }
    result.definition = compiled.serializeDefinition();
    procgen::PointGraph structuralCheck;
    if (!structuralCheck.deserializeDefinition(result.definition)) {
        result.status = EditorStatus::Failed;
        result.diagnostics.push_back({RuleId("editor.pcg.cycle"), DiagnosticSeverity::Error,
                                      structuralCheck.getError()});
        result.definition.clear();
        return result;
    }
    result.status = EditorStatus::Applied;
    return result;
}

PcgGraphPreviewResult PcgPointGraphDomain::preview(const GraphDocumentData& graph,
                                                    const std::string& inputNode,
                                                    procgen::PointSet* previewInput,
                                                    const std::string& outputNode,
                                                    int nodeBudget, int pointBudget) const {
    PcgGraphPreviewResult result;
    result.documentRevision = graph.revision;
    const auto compiled = compile(graph);
    result.diagnostics = compiled.diagnostics;
    if (compiled.status != EditorStatus::Applied) {
        result.status = compiled.status;
        return result;
    }
    if (!previewInput || inputNode.empty() || outputNode.empty()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(
            {RuleId("editor.pcg.preview-invalid-input"), DiagnosticSeverity::Error,
             "PCG preview requires input/output node ids and point input"});
        return result;
    }
    procgen::PointGraph runtime;
    if (!runtime.deserializeDefinition(compiled.definition)) {
        result.status = EditorStatus::Failed;
        result.diagnostics.push_back(
            {RuleId("editor.pcg.preview-bind-failed"), DiagnosticSeverity::Error,
             "PCG preview definition could not be loaded"});
        return result;
    }
    runtime.setExecutionNodeBudget(nodeBudget);
    runtime.setMaxNodeOutputPoints(pointBudget);
    if (!runtime.setNodePoints(inputNode, previewInput)) {
        result.status = EditorStatus::Failed;
        result.diagnostics.push_back(
            {RuleId("editor.pcg.preview-bind-failed"), DiagnosticSeverity::Error,
             runtime.getError().empty() ? "PCG preview input node is invalid: " + inputNode
                                        : runtime.getError()});
        return result;
    }
    std::unique_ptr<procgen::PointSet> output(runtime.execute(outputNode));
    if (!output) {
        result.status = runtime.wasCancelled() ? EditorStatus::Cancelled : EditorStatus::Failed;
        result.diagnostics.push_back(
            {RuleId(runtime.wasCancelled() ? "editor.pcg.preview-cancelled"
                                           : "editor.pcg.preview-execution-failed"),
             runtime.wasCancelled() ? DiagnosticSeverity::Info : DiagnosticSeverity::Error,
             runtime.getError()});
        return result;
    }
    result.outputCount = output->getCount();
    result.metrics.reserve(size_t(runtime.getMetricCount()));
    for (int index = 0; index < runtime.getMetricCount(); ++index) {
        PcgGraphPreviewMetric metric;
        metric.nodeId         = runtime.getMetricNodeId(index);
        metric.outputCount    = runtime.getMetricOutputCount(index);
        metric.milliseconds   = runtime.getMetricMilliseconds(index);
        metric.cacheHit       = runtime.isMetricCacheHit(index);
        metric.minX           = runtime.getMetricMinX(index);
        metric.minY           = runtime.getMetricMinY(index);
        metric.minZ           = runtime.getMetricMinZ(index);
        metric.maxX           = runtime.getMetricMaxX(index);
        metric.maxY           = runtime.getMetricMaxY(index);
        metric.maxZ           = runtime.getMetricMaxZ(index);
        metric.averageDensity = runtime.getMetricAverageDensity(index);
        result.metrics.push_back(std::move(metric));
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::procgen_editing
