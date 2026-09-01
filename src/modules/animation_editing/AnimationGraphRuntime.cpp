#include "animation_editing/AnimationGraph.h"

#include "animation/AnimClip.h"
#include "animation/AnimStateMachine.h"

#include <map>
#include <memory>
#include <utility>

namespace eve::animation_editing {
namespace {

template <class T>
EditorResult<T> runtimeError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* runtimeField(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

EditorResult<animation::AnimStateMachine*> AnimationStateGraphRuntimeBuilder::build(
    const GraphDocumentData& graph, animation::AnimSkeleton* skeleton, const ClipResolver& clips) const {
    if (!skeleton || !clips)
        return runtimeError<animation::AnimStateMachine*>(EditorStatus::Rejected,
                                                         "editor.animation.runtime-input",
                                                         "Animation skeleton and clip resolver are required");
    AnimationStateGraphDomain domain;
    const AnimationGraphCompileResult compiled = domain.compile(graph);
    if (compiled.status != EditorStatus::Applied) {
        EditorResult<animation::AnimStateMachine*> failed;
        failed.status = compiled.status;
        failed.diagnostics = compiled.diagnostics;
        return failed;
    }

    std::map<GraphNodeId, const GraphNodeRecord*> nodes;
    std::map<GraphPinId, GraphNodeId> pinOwners;
    for (const GraphNodeRecord& node : graph.nodes) {
        nodes.emplace(node.id, &node);
        for (const GraphPinRecord& pin : node.pins) pinOwners.emplace(pin.id, node.id);
    }
    std::map<GraphNodeId, GraphNodeId> from;
    std::map<GraphNodeId, GraphNodeId> to;
    for (const GraphEdgeRecord& edge : graph.edges) {
        const GraphNodeId fromNode = pinOwners.at(edge.from);
        const GraphNodeId toNode = pinOwners.at(edge.to);
        if (nodes.at(fromNode)->type == "animation.state") from[toNode] = fromNode;
        else to[fromNode] = toNode;
    }

    auto machine = std::make_unique<animation::AnimStateMachine>(skeleton);
    for (const auto& [id, node] : nodes) {
        if (node->type != "animation.state") continue;
        const auto* clipAssetValue = runtimeField(node->properties, "clip");
        const auto* clipAsset = clipAssetValue->getIf<std::string>();
        animation::AnimClip* clip = clips(*clipAsset);
        if (!clip)
            return runtimeError<animation::AnimStateMachine*>(EditorStatus::NotFound,
                                                             "editor.animation.clip-not-found",
                                                             "Animation clip asset could not be resolved: " +
                                                                 *clipAsset);
        machine->addState(id.value(), clip);
    }
    const auto* entryValue = runtimeField(graph.parameters, "entry");
    machine->setEntry(*entryValue->getIf<std::string>());

    for (const auto& [id, node] : nodes) {
        if (node->type != "animation.transition") continue;
        const double blend = *runtimeField(node->properties, "blendSeconds")->getIf<double>();
        const bool hasExit = *runtimeField(node->properties, "hasExitTime")->getIf<bool>();
        const double exitTime = *runtimeField(node->properties, "exitTime")->getIf<double>();
        const std::string kind = *runtimeField(node->properties, "conditionKind")->getIf<std::string>();
        const std::string parameter = *runtimeField(node->properties, "parameter")->getIf<std::string>();
        const std::string op = *runtimeField(node->properties, "operator")->getIf<std::string>();
        const double threshold = *runtimeField(node->properties, "threshold")->getIf<double>();
        const bool boolValue = *runtimeField(node->properties, "boolValue")->getIf<bool>();
        const int transition = machine->addTransition(from.at(id).value(), to.at(id).value(),
                                                      static_cast<float>(blend));
        machine->setHasExitTime(transition, hasExit);
        if (hasExit) machine->setExitTime(transition, static_cast<float>(exitTime));
        if (kind == "float")
            machine->addFloatCondition(transition, parameter, op, static_cast<float>(threshold));
        else if (kind == "bool")
            machine->addBoolCondition(transition, parameter, boolValue);
        else if (kind == "trigger")
            machine->addTriggerCondition(transition, parameter);
    }
    return EditorResult<animation::AnimStateMachine*>::applied(machine.release());
}

}  // namespace eve::animation_editing
