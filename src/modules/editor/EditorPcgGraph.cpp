#include "editor/EditorPcgGraph.h"

#include "procgen/PointGraph.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace eve::editor {
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

PcgGraphCompileResult PcgPointGraphDomain::compile(const GraphDocumentData& graph) const {
    PcgGraphCompileResult result;
    result.documentRevision = graph.revision;
    if (graph.domain != domain()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.pcg.wrong-domain"), DiagnosticSeverity::Error,
                                      "Graph is not a procgen.point document"});
        return result;
    }
    procgen::PointGraph compiled;
    for (const auto& node : graph.nodes) {
        if (!compiled.addNode(node.id.value(), node.type)) {
            result.status = EditorStatus::Failed;
            result.diagnostics.push_back({RuleId("editor.pcg.invalid-node"), DiagnosticSeverity::Error,
                                          "Unknown or duplicated PCG node: " + node.id.value()});
            return result;
        }
        const auto* properties = node.properties.getIf<EditorValue::Object>();
        if (!properties) continue;
        for (const auto& [key, value] : *properties) {
            if (const auto* boolean = value.getIf<bool>())
                compiled.setNodeInt(node.id.value(), key, *boolean ? 1 : 0);
            else if (const auto* integer = value.getIf<int64_t>())
                compiled.setNodeInt(node.id.value(), key, int(*integer));
            else if (const auto* number = value.getIf<double>())
                compiled.setNodeFloat(node.id.value(), key, float(*number));
            else if (const auto* text = value.getIf<std::string>())
                compiled.setNodeString(node.id.value(), key, *text);
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
    std::unordered_map<std::string, StableId> connectedInputs;
    for (const auto& edge : graph.edges) {
        const GraphPinRecord* from = findPin(graph, edge.from);
        const GraphPinRecord* to   = findPin(graph, edge.to);
        const GraphNodeRecord* toNode = to ? findNode(graph, to->node) : nullptr;
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

}  // namespace eve::editor
