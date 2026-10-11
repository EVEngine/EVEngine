#include "dnut_interpreter/SequenceAsset.h"

#include <string>
#include <unordered_set>
#include <utility>

namespace eve::dnut {

namespace {

eve::Result<void> assetFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "dnut.sequence"));
}

}  // namespace

bool isCoreSequenceNodeType(const std::string& type) noexcept {
    return type == "branch" || type == "choice" || type == "call" || type == "wait" || type == "end";
}

const SequenceNode* SequenceAsset::findNode(const std::string& nodeId) const noexcept {
    for (const auto& node : nodes) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

eve::Result<void> SequenceAsset::validate() const {
    if (id.empty())
        return assetFailure(eve::DiagnosticCode::InvariantViolation, "sequence asset has no id", "id");
    if (entry.empty())
        return assetFailure(eve::DiagnosticCode::InvariantViolation, "sequence asset has no entry node", "entry");
    if (version < 1)
        return assetFailure(eve::DiagnosticCode::InvariantViolation, "sequence asset version must be positive",
                            "version");

    std::unordered_set<std::string> ids;
    for (const auto& node : nodes) {
        if (node.id.empty())
            return assetFailure(eve::DiagnosticCode::InvariantViolation, "sequence node has an empty id", "nodes");
        if (!ids.insert(node.id).second)
            return assetFailure(eve::DiagnosticCode::AlreadyExists, "duplicate sequence node id '" + node.id + "'",
                                "nodes." + node.id);
        if (node.type.empty())
            return assetFailure(eve::DiagnosticCode::InvariantViolation,
                                "sequence node '" + node.id + "' has no type", "nodes." + node.id + ".type");
        if (!node.payload.isObject())
            return assetFailure(eve::DiagnosticCode::InvariantViolation,
                                "sequence node '" + node.id + "' payload must be an object",
                                "nodes." + node.id + ".payload");
    }

    if (!findNode(entry))
        return assetFailure(eve::DiagnosticCode::NotFound, "entry node '" + entry + "' does not exist", "entry");

    const auto checkReference = [&](const std::string& owner, const std::string& field,
                                    const std::string& reference) -> eve::Result<void> {
        if (reference.empty() || findNode(reference)) return eve::Result<void>::success();
        return assetFailure(eve::DiagnosticCode::NotFound,
                            "sequence node '" + owner + "' references missing node '" + reference + "'",
                            "nodes." + owner + "." + field);
    };

    for (const auto& node : nodes) {
        auto nextResult = checkReference(node.id, "next", node.next);
        if (!nextResult.ok()) return nextResult;

        if (node.type == "call") {
            const eve::Value* target = node.payload.find("target");
            if (!target || !target->isString() || target->asString().empty())
                return assetFailure(eve::DiagnosticCode::InvariantViolation,
                                    "call node '" + node.id + "' requires a target asset id",
                                    "nodes." + node.id + ".payload.target");
        }
        if (node.type == "choice") {
            if (node.routes.empty())
                return assetFailure(eve::DiagnosticCode::InvariantViolation,
                                    "choice node '" + node.id + "' requires at least one route",
                                    "nodes." + node.id + ".routes");
            std::unordered_set<std::string> labels;
            for (const auto& route : node.routes) {
                if (route.label.empty())
                    return assetFailure(eve::DiagnosticCode::InvariantViolation,
                                        "choice node '" + node.id + "' has a route without a label",
                                        "nodes." + node.id + ".routes");
                if (!labels.insert(route.label).second)
                    return assetFailure(eve::DiagnosticCode::AlreadyExists,
                                        "choice node '" + node.id + "' has duplicate route label '" + route.label + "'",
                                        "nodes." + node.id + ".routes");
            }
        }
        if (node.type == "branch") {
            if (node.routes.empty())
                return assetFailure(eve::DiagnosticCode::InvariantViolation,
                                    "branch node '" + node.id + "' requires at least one route",
                                    "nodes." + node.id + ".routes");
            for (const auto& route : node.routes) {
                if (route.condition.isNull())
                    return assetFailure(eve::DiagnosticCode::InvariantViolation,
                                        "branch node '" + node.id + "' has a route without a condition",
                                        "nodes." + node.id + ".routes");
            }
        }
        for (const auto& route : node.routes) {
            auto routeResult = checkReference(node.id, "routes", route.target);
            if (!routeResult.ok()) return routeResult;
        }
    }
    return eve::Result<void>::success();
}

}  // namespace eve::dnut
