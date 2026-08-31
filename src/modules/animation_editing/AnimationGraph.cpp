#include "animation_editing/AnimationGraph.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

namespace eve::animation_editing {
namespace {

template <class T>
EditorResult<T> animationError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

void diagnostic(AnimationGraphCompileResult& result, const char* rule, std::string message) {
    result.diagnostics.push_back({RuleId(rule), DiagnosticSeverity::Error, std::move(message)});
}

std::string quote(const std::string& value) {
    std::string result = "\"";
    for (const char character : value) {
        if (character == '\\' || character == '\"') result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('\"');
    return result;
}

}  // namespace

GraphConnectionDecision AnimationStateGraphDomain::canConnect(const GraphPinRecord& from,
                                                               const GraphPinRecord& to) const {
    GraphConnectionDecision decision;
    decision.allowed = from.direction == GraphPinDirection::Output &&
                       to.direction == GraphPinDirection::Input && from.type == "animation.flow" &&
                       to.type == "animation.flow" && from.node != to.node;
    if (!decision.allowed)
        decision.diagnostics.push_back({RuleId("editor.animation.invalid-connection"),
                                        DiagnosticSeverity::Error,
                                        "Animation graph requires flow output-to-input between distinct nodes"});
    return decision;
}

EditorResult<GraphNodeRecord> AnimationStateGraphDomain::makeStateNode(const GraphNodeId& id,
                                                                       const std::string& clipAsset) const {
    if (id.empty() || clipAsset.empty())
        return animationError<GraphNodeRecord>(EditorStatus::Rejected, "editor.animation.invalid-state",
                                               "Animation state id and clip asset are required");
    GraphNodeRecord node;
    node.id = id;
    node.type = "animation.state";
    EditorValue::Object properties;
    properties["clip"] = clipAsset;
    properties["speed"] = 1.0;
    properties["loop"] = true;
    node.properties = EditorValue(std::move(properties));
    node.pins = {{GraphPinId(id.value() + ".in"), id, "animation.flow", GraphPinDirection::Input},
                 {GraphPinId(id.value() + ".out"), id, "animation.flow", GraphPinDirection::Output}};
    return EditorResult<GraphNodeRecord>::applied(std::move(node));
}

EditorResult<GraphNodeRecord> AnimationStateGraphDomain::makeTransitionNode(const GraphNodeId& id) const {
    if (id.empty())
        return animationError<GraphNodeRecord>(EditorStatus::Rejected, "editor.animation.invalid-transition",
                                               "Animation transition id is required");
    GraphNodeRecord node;
    node.id = id;
    node.type = "animation.transition";
    EditorValue::Object properties;
    properties["blendSeconds"] = 0.2;
    properties["hasExitTime"] = false;
    properties["exitTime"] = 0.0;
    properties["conditionKind"] = "none";
    properties["parameter"] = "";
    properties["operator"] = ">";
    properties["threshold"] = 0.0;
    properties["boolValue"] = true;
    node.properties = EditorValue(std::move(properties));
    node.pins = {{GraphPinId(id.value() + ".in"), id, "animation.flow", GraphPinDirection::Input},
                 {GraphPinId(id.value() + ".out"), id, "animation.flow", GraphPinDirection::Output}};
    return EditorResult<GraphNodeRecord>::applied(std::move(node));
}

AnimationGraphCompileResult AnimationStateGraphDomain::compile(const GraphDocumentData& graph) const {
    AnimationGraphCompileResult result;
    result.documentRevision = graph.revision;
    if (graph.domain != domain()) {
        result.status = EditorStatus::Rejected;
        diagnostic(result, "editor.animation.wrong-domain", "Graph is not an animation state document");
        return result;
    }
    if (graph.schemaVersion != 1) {
        result.status = EditorStatus::Unsupported;
        diagnostic(result, "editor.animation.unsupported-schema", "Animation graph schema version is unsupported");
        return result;
    }
    const auto* parameters = graph.parameters.getIf<EditorValue::Object>();
    const EditorValue* entryValue = field(graph.parameters, "entry");
    const auto* entry = entryValue ? entryValue->getIf<std::string>() : nullptr;
    if (!parameters || !entry || entry->empty()) {
        result.status = EditorStatus::Failed;
        diagnostic(result, "editor.animation.entry-required", "Animation graph requires an entry state parameter");
        return result;
    }

    std::map<GraphNodeId, const GraphNodeRecord*> nodes;
    std::map<GraphPinId, const GraphNodeRecord*> pinOwners;
    for (const GraphNodeRecord& node : graph.nodes) {
        if (node.type != "animation.state" && node.type != "animation.transition") {
            diagnostic(result, "editor.animation.unknown-node", "Unknown animation node type: " + node.type);
            continue;
        }
        if (!nodes.emplace(node.id, &node).second)
            diagnostic(result, "editor.animation.duplicate-node", "Duplicate animation node id: " + node.id.value());
        for (const GraphPinRecord& pin : node.pins) pinOwners.emplace(pin.id, &node);
    }
    const auto entryNode = nodes.find(GraphNodeId(*entry));
    if (entryNode == nodes.end() || entryNode->second->type != "animation.state")
        diagnostic(result, "editor.animation.invalid-entry", "Entry does not reference an animation state node");

    std::map<GraphNodeId, GraphNodeId> transitionFrom;
    std::map<GraphNodeId, GraphNodeId> transitionTo;
    for (const GraphEdgeRecord& edge : graph.edges) {
        const auto fromOwner = pinOwners.find(edge.from);
        const auto toOwner = pinOwners.find(edge.to);
        if (fromOwner == pinOwners.end() || toOwner == pinOwners.end()) {
            diagnostic(result, "editor.animation.dangling-edge", "Animation edge references a missing pin");
            continue;
        }
        const GraphNodeRecord* from = fromOwner->second;
        const GraphNodeRecord* to = toOwner->second;
        if (from->type == "animation.state" && to->type == "animation.transition") {
            if (!transitionFrom.emplace(to->id, from->id).second)
                diagnostic(result, "editor.animation.multiple-transition-sources",
                           "Transition has more than one source state: " + to->id.value());
        } else if (from->type == "animation.transition" && to->type == "animation.state") {
            if (!transitionTo.emplace(from->id, to->id).second)
                diagnostic(result, "editor.animation.multiple-transition-destinations",
                           "Transition has more than one destination state: " + from->id.value());
        } else
            diagnostic(result, "editor.animation.invalid-flow",
                       "Flow must alternate state and transition nodes");
    }

    std::ostringstream definition;
    definition << "EVANIM_STATE_GRAPH 1\n" << "entry " << quote(*entry) << '\n';
    for (const auto& [id, node] : nodes) {
        if (node->type != "animation.state") continue;
        const auto* clipValue = field(node->properties, "clip");
        const auto* speedValue = field(node->properties, "speed");
        const auto* loopValue = field(node->properties, "loop");
        const auto* clip = clipValue ? clipValue->getIf<std::string>() : nullptr;
        const auto* speed = speedValue ? speedValue->getIf<double>() : nullptr;
        const auto* loop = loopValue ? loopValue->getIf<bool>() : nullptr;
        if (!clip || clip->empty() || !speed || *speed <= 0.0 || !loop) {
            diagnostic(result, "editor.animation.invalid-state-properties",
                       "State requires clip, positive speed and loop properties: " + id.value());
            continue;
        }
        definition << "state " << quote(id.value()) << ' ' << quote(*clip) << ' ' << *speed << ' '
                   << (*loop ? 1 : 0) << '\n';
    }
    for (const auto& [id, node] : nodes) {
        if (node->type != "animation.transition") continue;
        if (!transitionFrom.contains(id) || !transitionTo.contains(id)) {
            diagnostic(result, "editor.animation.incomplete-transition",
                       "Transition requires exactly one source and destination: " + id.value());
            continue;
        }
        const auto* blend = field(node->properties, "blendSeconds");
        const auto* hasExit = field(node->properties, "hasExitTime");
        const auto* exitTime = field(node->properties, "exitTime");
        const auto* kind = field(node->properties, "conditionKind");
        const auto* parameter = field(node->properties, "parameter");
        const auto* op = field(node->properties, "operator");
        const auto* threshold = field(node->properties, "threshold");
        const auto* boolValue = field(node->properties, "boolValue");
        const auto* blendNumber = blend ? blend->getIf<double>() : nullptr;
        const auto* exitEnabled = hasExit ? hasExit->getIf<bool>() : nullptr;
        const auto* exitNumber = exitTime ? exitTime->getIf<double>() : nullptr;
        const auto* kindName = kind ? kind->getIf<std::string>() : nullptr;
        const auto* parameterName = parameter ? parameter->getIf<std::string>() : nullptr;
        const auto* operatorName = op ? op->getIf<std::string>() : nullptr;
        const auto* thresholdNumber = threshold ? threshold->getIf<double>() : nullptr;
        const auto* boolFlag = boolValue ? boolValue->getIf<bool>() : nullptr;
        const bool kindValid = kindName && (*kindName == "none" || *kindName == "float" ||
                                            *kindName == "bool" || *kindName == "trigger");
        const bool opValid = operatorName && (*operatorName == ">" || *operatorName == ">=" ||
                                                *operatorName == "<" || *operatorName == "<=" ||
                                                *operatorName == "==" || *operatorName == "!=");
        if (!blendNumber || *blendNumber < 0.0 || !exitEnabled || !exitNumber || *exitNumber < 0.0 ||
            *exitNumber > 1.0 || !kindValid || !parameterName || !operatorName || !thresholdNumber ||
            !boolFlag || (*kindName != "none" && parameterName->empty()) ||
            (*kindName == "float" && !opValid)) {
            diagnostic(result, "editor.animation.invalid-transition-properties",
                       "Transition metadata is invalid: " + id.value());
            continue;
        }
        definition << "transition " << quote(id.value()) << ' ' << quote(transitionFrom[id].value()) << ' '
                   << quote(transitionTo[id].value()) << ' ' << *blendNumber << ' '
                   << (*exitEnabled ? 1 : 0) << ' ' << *exitNumber << ' ' << quote(*kindName) << ' '
                   << quote(*parameterName) << ' ' << quote(*operatorName) << ' ' << *thresholdNumber << ' '
                   << (*boolFlag ? 1 : 0) << '\n';
    }
    if (!result.diagnostics.empty()) {
        result.status = EditorStatus::Failed;
        return result;
    }
    result.status = EditorStatus::Applied;
    result.definition = definition.str();
    return result;
}

}  // namespace eve::animation_editing
