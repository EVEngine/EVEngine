#include "dnut_interpreter/SequenceAsset.h"

#include <string>
#include <unordered_set>
#include <utility>

namespace eve::dnut {

namespace {

}  // namespace

bool isCoreSequenceNodeType(const std::string& type) noexcept {
    return type == "branch" || type == "choice" || type == "call" || type == "command" || type == "wait" ||
           type == "end" || type == "label" || type == "jump" || type == "loop" || type == "break" ||
           type == "continue" || type == "parallel" || type == "join";
}

const char* sequenceParameterTypeName(SequenceParameterType type) noexcept {
    switch (type) {
        case SequenceParameterType::Any: return "any";
        case SequenceParameterType::String: return "string";
        case SequenceParameterType::Integer: return "integer";
        case SequenceParameterType::Number: return "number";
        case SequenceParameterType::Boolean: return "boolean";
    }
    return "unknown";
}

const SequenceNode* SequenceAsset::findNode(const std::string& nodeId) const noexcept {
    for (const auto& node : nodes) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

eve::Result<void> SequenceAsset::validate() const {
    if (id.empty())
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence asset has no id", "id", {}, "dnut.sequence"));
    if (entry.empty())
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence asset has no entry node", "entry", {}, "dnut.sequence"));
    if (version < 1)
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence asset version must be positive", "version", {}, "dnut.sequence"));

    std::unordered_set<std::string> parameterNames;
    for (const auto& parameter : parameters) {
        if (parameter.name.empty())
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence parameter has an empty name", "parameters", {}, "dnut.sequence"));
        if (!parameterNames.insert(parameter.name).second)
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::AlreadyExists, "duplicate sequence parameter '" + parameter.name + "'", "parameters." + parameter.name, {}, "dnut.sequence"));
        if (parameter.required && !parameter.defaultValue.isNull())
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "required sequence parameter '" + parameter.name + "' cannot have a default", "parameters." + parameter.name, {}, "dnut.sequence"));
    }

    std::unordered_set<std::string> ids;
    for (const auto& node : nodes) {
        if (node.id.empty())
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence node has an empty id", "nodes", {}, "dnut.sequence"));
        if (!ids.insert(node.id).second)
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::AlreadyExists, "duplicate sequence node id '" + node.id + "'", "nodes." + node.id, {}, "dnut.sequence"));
        if (node.type.empty())
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence node '" + node.id + "' has no type", "nodes." + node.id + ".type", {}, "dnut.sequence"));
        if (!node.payload.isObject())
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence node '" + node.id + "' payload must be an object", "nodes." + node.id + ".payload", {}, "dnut.sequence"));
        for (const auto& route : node.routes) {
            if (!route.payload.isObject())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "sequence route payload must be an object", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
        }
    }

    if (!findNode(entry))
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "entry node '" + entry + "' does not exist", "entry", {}, "dnut.sequence"));

    const auto checkReference = [&](const std::string& owner, const std::string& field,
                                    const std::string& reference) -> eve::Result<void> {
        if (reference.empty() || findNode(reference)) return eve::Result<void>::success();
        return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "sequence node '" + owner + "' references missing node '" + reference + "'", "nodes." + owner + "." + field, {}, "dnut.sequence"));
    };

    for (const auto& node : nodes) {
        auto nextResult = checkReference(node.id, "next", node.next);
        if (!nextResult.ok()) return nextResult;

        if (node.type == "call") {
            const eve::Value* target = node.payload.find("target");
            if (!target || !target->isString() || target->asString().empty())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "call node '" + node.id + "' requires a target asset id", "nodes." + node.id + ".payload.target", {}, "dnut.sequence"));
            const eve::Value* returnNode = node.payload.find("return");
            if (returnNode) {
                if (!returnNode->isString())
                    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "call node '" + node.id + "' return must be a node id", "nodes." + node.id + ".payload.return", {}, "dnut.sequence"));
                auto returnResult = checkReference(node.id, "payload.return", returnNode->asString());
                if (!returnResult.ok()) return returnResult;
            }
        }
        if (node.type == "choice") {
            if (node.routes.empty())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "choice node '" + node.id + "' requires at least one route", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
            std::unordered_set<std::string> labels;
            for (const auto& route : node.routes) {
                if (route.label.empty())
                    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "choice node '" + node.id + "' has a route without a label", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
                if (!labels.insert(route.label).second)
                    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::AlreadyExists, "choice node '" + node.id + "' has duplicate route label '" + route.label + "'", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
            }
        }
        if (node.type == "branch") {
            if (node.routes.empty())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "branch node '" + node.id + "' requires at least one route", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
        }
        if (node.type == "label") {
            const eve::Value* name = node.payload.find("name");
            if (!name || !name->isString() || name->asString().empty())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "label node '" + node.id + "' requires a name", "nodes." + node.id + ".payload.name", {}, "dnut.sequence"));
        }
        if (node.type == "jump") {
            const eve::Value* target = node.payload.find("target");
            if (!target || !target->isString() || target->asString().empty())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "jump node '" + node.id + "' requires a resolved target", "nodes." + node.id + ".payload.target", {}, "dnut.sequence"));
            auto jumpTarget = checkReference(node.id, "payload.target", target->asString());
            if (!jumpTarget.ok()) return jumpTarget;
        }
        if (node.type == "loop") {
            const eve::Value* mode = node.payload.find("mode");
            if (!mode || !mode->isString())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "loop node '" + node.id + "' requires mode", "nodes." + node.id + ".payload.mode", {}, "dnut.sequence"));
            const std::string& modeText = mode->asString();
            if (modeText != "forever" && modeText != "while" && modeText != "times")
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "loop node '" + node.id + "' has unknown mode '" + modeText + "'", "nodes." + node.id + ".payload.mode", {}, "dnut.sequence"));
            if (node.routes.empty())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "loop node '" + node.id + "' requires a body route", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
            if (modeText == "times") {
                const eve::Value* count = node.payload.find("count");
                if (!count || !count->isNumeric())
                    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "loop times node '" + node.id + "' requires count", "nodes." + node.id + ".payload.count", {}, "dnut.sequence"));
            }
            if (modeText == "while" && node.routes.front().condition.isNull())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "loop while node '" + node.id + "' requires a condition", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
        }
        if (node.type == "parallel") {
            if (node.routes.size() < 2)
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "parallel node '" + node.id + "' requires at least two lanes", "nodes." + node.id + ".routes", {}, "dnut.sequence"));
            const eve::Value* join = node.payload.find("join");
            if (!join || !join->isString() || join->asString().empty())
                return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "parallel node '" + node.id + "' requires join", "nodes." + node.id + ".payload.join", {}, "dnut.sequence"));
            auto joinResult = checkReference(node.id, "payload.join", join->asString());
            if (!joinResult.ok()) return joinResult;
        }
        for (const auto& route : node.routes) {
            auto routeResult = checkReference(node.id, "routes", route.target);
            if (!routeResult.ok()) return routeResult;
        }
    }

    std::unordered_set<std::string> labelNames;
    for (const auto& node : nodes) {
        if (node.type != "label") continue;
        const eve::Value* name = node.payload.find("name");
        if (!name || !name->isString()) continue;
        if (!labelNames.insert(name->asString()).second)
            return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::AlreadyExists, "duplicate label '" + name->asString() + "'", "nodes." + node.id + ".payload.name", {}, "dnut.sequence"));
    }
    return eve::Result<void>::success();
}

}  // namespace eve::dnut
