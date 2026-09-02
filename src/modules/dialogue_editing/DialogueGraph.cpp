#include "dialogue_editing/DialogueGraph.h"

#include <map>
#include <set>
#include <utility>

namespace eve::dialogue_editing {
namespace {

template <class T>
EditorResult<T> dialogueError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

void diagnostic(DialogueGraphCompileResult& result, const char* rule, std::string message) {
    result.diagnostics.push_back(eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId(rule),
                                                              DiagnosticSeverity::Error, std::move(message)));
}

bool isDialogueKind(const std::string& kind) {
    static const std::set<std::string> kinds{"line", "branch", "choice", "call", "command", "wait", "end"};
    return kinds.contains(kind);
}

const std::set<std::string>& propertyKeys(const std::string& kind) {
    static const std::set<std::string> line{"speaker", "text", "pool", "i18nKey", "voice", "expression"};
    static const std::set<std::string> call{"target", "returnNode", "arguments"};
    static const std::set<std::string> command{"target", "expression", "arguments"};
    static const std::set<std::string> route{"label"};
    static const std::set<std::string> empty;
    if (kind == "line") return line;
    if (kind == "call") return call;
    if (kind == "command") return command;
    if (kind == "route") return route;
    return empty;
}

EditorValue::Object defaultProperties(const std::string& kind) {
    EditorValue::Object properties;
    if (kind == "line") {
        properties["speaker"] = "";
        properties["text"] = "";
        properties["pool"] = "";
        properties["i18nKey"] = "";
        properties["voice"] = "";
        properties["expression"] = "";
    } else if (kind == "call") {
        properties["target"] = "";
        properties["returnNode"] = "";
        properties["arguments"] = "{}";
    } else if (kind == "command") {
        properties["target"] = "";
        properties["expression"] = "";
        properties["arguments"] = "{}";
    }
    return properties;
}

}  // namespace

GraphConnectionDecision DialogueGraphDomain::canConnect(const GraphPinRecord& from,
                                                         const GraphPinRecord& to) const {
    GraphConnectionDecision decision;
    decision.allowed = from.direction == GraphPinDirection::Output &&
                       to.direction == GraphPinDirection::Input && from.type == "dialogue.flow" &&
                       to.type == "dialogue.flow" && from.node != to.node;
    if (!decision.allowed)
        decision.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.dialogue.invalid-connection"),
            DiagnosticSeverity::Error, "Dialogue flow requires output-to-input pins on distinct nodes"));
    return decision;
}

EditorResult<GraphNodeRecord> DialogueGraphDomain::makeNode(const GraphNodeId& id,
                                                            const std::string& kind) const {
    if (id.empty() || !isDialogueKind(kind))
        return dialogueError<GraphNodeRecord>(EditorStatus::Rejected, "editor.dialogue.invalid-node",
                                              "Dialogue node requires an id and supported kind");
    GraphNodeRecord node;
    node.id = id;
    node.type = "dialogue." + kind;
    node.properties = EditorValue(defaultProperties(kind));
    node.pins.push_back({GraphPinId(id.value() + ".in"), id, "dialogue.flow", GraphPinDirection::Input});
    if (kind != "end")
        node.pins.push_back({GraphPinId(id.value() + ".out"), id, "dialogue.flow",
                             GraphPinDirection::Output});
    return eve::editing::applied<GraphNodeRecord>(std::move(node));
}

EditorResult<GraphNodeRecord> DialogueGraphDomain::makeRouteNode(const GraphNodeId& id,
                                                                 const std::string& label) const {
    if (id.empty())
        return dialogueError<GraphNodeRecord>(EditorStatus::Rejected, "editor.dialogue.invalid-route",
                                              "Dialogue route requires an id");
    GraphNodeRecord node;
    node.id = id;
    node.type = "dialogue.route";
    node.properties = EditorValue::Object{{"label", label}};
    node.pins = {{GraphPinId(id.value() + ".in"), id, "dialogue.flow", GraphPinDirection::Input},
                 {GraphPinId(id.value() + ".out"), id, "dialogue.flow", GraphPinDirection::Output}};
    return eve::editing::applied<GraphNodeRecord>(std::move(node));
}

DialogueGraphCompileResult DialogueGraphDomain::compile(const GraphDocumentData& graph) const {
    DialogueGraphCompileResult result;
    result.documentRevision = graph.revision;
    if (graph.domain != domain()) {
        result.status = EditorStatus::Rejected;
        diagnostic(result, "editor.dialogue.wrong-domain", "Graph is not a dialogue conversation document");
        return result;
    }
    if (graph.schemaVersion != 1) {
        result.status = EditorStatus::Unsupported;
        diagnostic(result, "editor.dialogue.unsupported-schema", "Dialogue graph schema version is unsupported");
        return result;
    }
    const auto* idValue = field(graph.parameters, "id");
    const auto* versionValue = field(graph.parameters, "version");
    const auto* entryValue = field(graph.parameters, "entry");
    const auto* parametersValue = field(graph.parameters, "parameters");
    const auto* id = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto* version = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* entry = entryValue ? entryValue->getIf<std::string>() : nullptr;
    const auto* parameters = parametersValue ? parametersValue->getIf<EditorValue::Array>() : nullptr;
    if (!id || id->empty() || !version || *version <= 0 || !entry || entry->empty() || !parameters) {
        result.status = EditorStatus::Failed;
        diagnostic(result, "editor.dialogue.invalid-document",
                   "Dialogue parameters require id, positive version, entry and a parameter array");
        return result;
    }

    std::map<GraphNodeId, const GraphNodeRecord*> nodes;
    std::map<GraphPinId, const GraphNodeRecord*> pinOwners;
    for (const GraphNodeRecord& node : graph.nodes) {
        const std::string kind = node.type.starts_with("dialogue.") ? node.type.substr(9) : "";
        if (!isDialogueKind(kind) && kind != "route")
            diagnostic(result, "editor.dialogue.unknown-node", "Unknown dialogue node type: " + node.type);
        if (!nodes.emplace(node.id, &node).second)
            diagnostic(result, "editor.dialogue.duplicate-node", "Duplicate dialogue node id: " + node.id.value());
        for (const GraphPinRecord& pin : node.pins)
            if (!pinOwners.emplace(pin.id, &node).second)
                diagnostic(result, "editor.dialogue.duplicate-pin", "Duplicate dialogue pin id: " + pin.id.value());
    }
    const auto entryNode = nodes.find(GraphNodeId(*entry));
    if (entryNode == nodes.end() || entryNode->second->type == "dialogue.route")
        diagnostic(result, "editor.dialogue.invalid-entry", "Entry must reference a dialogue business node");

    std::map<GraphNodeId, std::vector<GraphNodeId>> outgoing;
    std::map<GraphNodeId, std::vector<GraphNodeId>> incoming;
    for (const GraphEdgeRecord& edge : graph.edges) {
        const auto from = pinOwners.find(edge.from);
        const auto to = pinOwners.find(edge.to);
        if (from == pinOwners.end() || to == pinOwners.end()) {
            diagnostic(result, "editor.dialogue.dangling-edge", "Dialogue edge references a missing pin");
            continue;
        }
        const GraphPinRecord* fromPin = nullptr;
        const GraphPinRecord* toPin = nullptr;
        for (const GraphPinRecord& pin : from->second->pins)
            if (pin.id == edge.from) fromPin = &pin;
        for (const GraphPinRecord& pin : to->second->pins)
            if (pin.id == edge.to) toPin = &pin;
        if (!fromPin || !toPin || !canConnect(*fromPin, *toPin).allowed) {
            diagnostic(result, "editor.dialogue.invalid-edge", "Dialogue edge has invalid direction or type");
            continue;
        }
        outgoing[from->second->id].push_back(to->second->id);
        incoming[to->second->id].push_back(from->second->id);
    }

    EditorValue::Array compiledNodes;
    for (const auto& [nodeId, node] : nodes) {
        const std::string kind = node->type.substr(9);
        const auto* properties = node->properties.getIf<EditorValue::Object>();
        if (!properties) {
            diagnostic(result, "editor.dialogue.invalid-properties",
                       "Dialogue node properties must be an object: " + nodeId.value());
        } else {
            const auto& allowed = propertyKeys(kind);
            for (const auto& [key, value] : *properties)
                if (!allowed.contains(key) || !value.getIf<std::string>())
                    diagnostic(result, "editor.dialogue.invalid-property",
                               "Dialogue property is unknown or not a string: " + nodeId.value() + "." + key);
            for (const std::string& key : allowed)
                if (!properties->contains(key))
                    diagnostic(result, "editor.dialogue.missing-property",
                               "Dialogue node is missing property: " + nodeId.value() + "." + key);
        }
        if (kind == "route") {
            if (incoming[nodeId].size() != 1 || outgoing[nodeId].size() != 1)
                diagnostic(result, "editor.dialogue.incomplete-route",
                           "Route requires exactly one source and target: " + nodeId.value());
            else {
                const auto source = nodes.at(incoming[nodeId].front());
                if (source->type != "dialogue.branch" && source->type != "dialogue.choice")
                    diagnostic(result, "editor.dialogue.invalid-route-source",
                               "Routes may only originate from branch or choice nodes: " + nodeId.value());
                if (nodes.at(outgoing[nodeId].front())->type == "dialogue.route")
                    diagnostic(result, "editor.dialogue.invalid-route-target",
                               "A route must target a business node: " + nodeId.value());
            }
            continue;
        }
        const bool routed = kind == "branch" || kind == "choice";
        for (const GraphNodeId& target : outgoing[nodeId]) {
            const bool targetIsRoute = nodes.at(target)->type == "dialogue.route";
            if (routed != targetIsRoute)
                diagnostic(result, "editor.dialogue.invalid-flow",
                           routed ? "Branch and choice outputs must connect through route nodes"
                                  : "Only branch and choice nodes may connect to route nodes");
        }
        if (kind == "end" && !outgoing[nodeId].empty())
            diagnostic(result, "editor.dialogue.end-has-output", "End node cannot have outgoing flow");
        if (!routed && kind != "end" && outgoing[nodeId].size() != 1)
            diagnostic(result, "editor.dialogue.next-required",
                       "Dialogue node requires exactly one next target: " + nodeId.value());
        if (routed && outgoing[nodeId].empty())
            diagnostic(result, "editor.dialogue.route-required",
                       "Branch or choice requires at least one route: " + nodeId.value());

        EditorValue::Object compiled{{"id", nodeId.value()}, {"kind", kind}, {"properties", node->properties}};
        if (!routed && kind != "end" && outgoing[nodeId].size() == 1)
            compiled["next"] = outgoing[nodeId].front().value();
        EditorValue::Array routes;
        if (routed) {
            for (const GraphNodeId& routeId : outgoing[nodeId]) {
                if (nodes.at(routeId)->type != "dialogue.route" || outgoing[routeId].size() != 1) continue;
                const EditorValue* labelValue = field(nodes.at(routeId)->properties, "label");
                const auto* label = labelValue ? labelValue->getIf<std::string>() : nullptr;
                if (!label)
                    diagnostic(result, "editor.dialogue.invalid-route-label",
                               "Route label must be a string: " + routeId.value());
                else
                    routes.emplace_back(EditorValue::Object{{"id", routeId.value()},
                                                             {"label", *label},
                                                             {"target", outgoing[routeId].front().value()}});
            }
        }
        compiled["routes"] = std::move(routes);
        compiledNodes.emplace_back(std::move(compiled));
    }
    std::set<std::string> parameterNames;
    for (const EditorValue& parameter : *parameters) {
        const auto* name = parameter.getIf<std::string>();
        if (!name || name->empty() || !parameterNames.insert(*name).second)
            diagnostic(result, "editor.dialogue.invalid-parameter",
                       "Conversation parameters must be unique non-empty strings");
    }
    if (!result.diagnostics.empty()) {
        result.status = EditorStatus::Failed;
        return result;
    }
    result.definition = EditorValue::Object{{"id", *id}, {"version", *version}, {"entry", *entry},
                                             {"parameters", *parameters}, {"nodes", std::move(compiledNodes)}};
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::dialogue_editing
