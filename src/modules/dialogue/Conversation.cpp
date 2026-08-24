#include "dialogue/Conversation.h"

#include <unordered_set>

namespace eve::dialogue {

const ConversationAsset::Node* ConversationAsset::findNode(const std::string& nodeId) const {
    for (const auto& node : nodes)
        if (node.id == nodeId) return &node;
    return nullptr;
}

bool ConversationAsset::validate(std::string* error) const {
    const auto fail = [&](const std::string& message) {
        if (error) *error = "conversation '" + id + "': " + message;
        return false;
    };
    if (id.empty()) return fail("missing id");
    if (entry.empty()) return fail("missing entry node");
    std::unordered_set<std::string> ids;
    for (const auto& node : nodes) {
        if (node.id.empty()) return fail("node has an empty id");
        if (!ids.insert(node.id).second) return fail("duplicate node id '" + node.id + "'");
    }
    if (!findNode(entry)) return fail("entry node '" + entry + "' does not exist");
    const auto checkRef = [&](const std::string& owner, const std::string& ref) {
        return ref.empty() || findNode(ref) ? true : fail("node '" + owner + "' references missing node '" + ref + "'");
    };
    for (const auto& node : nodes) {
        if (!checkRef(node.id, node.next)) return false;
        if (!checkRef(node.id, node.returnNode)) return false;
        for (const auto& route : node.routes)
            if (!checkRef(node.id, route.second)) return false;
    }
    return true;
}

bool ConversationRunner::fail(std::string* error, const std::string& message) const {
    if (error) *error = message;
    return false;
}

bool ConversationRunner::start(const ConversationAsset* asset, StateValue bindings,
                               std::string* error) {
    if (!asset) return fail(error, "conversation: null asset");
    if (!bindings.isObject()) return fail(error, "conversation: bindings must be an object");
    if (!asset->validate(error)) return false;
    asset_ = asset;
    bindings_ = std::move(bindings);
    locals_ = StateValue::object();
    blocked_ = false;
    return enter(asset_->entry, error) && runUntilBlocked(error);
}

void ConversationRunner::stop() {
    asset_ = nullptr;
    nodeId_.clear();
    bindings_ = StateValue::object();
    locals_ = StateValue::object();
    blocked_ = false;
}

const ConversationAsset::Node* ConversationRunner::currentNode() const {
    return asset_ ? asset_->findNode(nodeId_) : nullptr;
}

bool ConversationRunner::enter(const std::string& nodeId, std::string* error) {
    if (!asset_) return fail(error, "conversation: no active asset");
    if (nodeId.empty()) {
        stop();
        return true;
    }
    if (!asset_->findNode(nodeId))
        return fail(error, "conversation '" + asset_->id + "': missing node '" + nodeId + "'");
    nodeId_ = nodeId;
    blocked_ = false;
    return true;
}

std::string ConversationRunner::evaluateRoute(const ConversationAsset::Node& node,
                                              std::string* error) const {
    for (const auto& route : node.routes) {
        if (route.first.empty() || route.first == "else") return route.second;
        if (!expressionEvaluator_) {
            fail(error, "conversation: branch requires an expression evaluator");
            return {};
        }
        const StateValue value = expressionEvaluator_(route.first, bindings_, locals_);
        if (!value.isBool()) {
            fail(error, "conversation: expression '" + route.first + "' did not return bool");
            return {};
        }
        if (value.asBool()) return route.second;
    }
    return node.next;
}

bool ConversationRunner::runUntilBlocked(std::string* error) {
    int budget = 10000;
    while (asset_ && budget-- > 0) {
        const auto* node = currentNode();
        if (!node) return fail(error, "conversation: invalid execution cursor");
        switch (node->kind) {
            case ConversationAsset::Node::Kind::Line:
            case ConversationAsset::Node::Kind::Choice:
            case ConversationAsset::Node::Kind::Wait:
                blocked_ = true;
                return true;
            case ConversationAsset::Node::Kind::Branch: {
                const std::string next = evaluateRoute(*node, error);
                if (next.empty() && error && !error->empty()) return false;
                if (!enter(next, error)) return false;
                break;
            }
            case ConversationAsset::Node::Kind::End:
                stop();
                return true;
            case ConversationAsset::Node::Kind::Call:
                return fail(error, "conversation: call nodes require call-stack support");
            case ConversationAsset::Node::Kind::Command:
                return fail(error, "conversation: command nodes require a command handler");
        }
    }
    return fail(error, "conversation: execution budget exceeded");
}

bool ConversationRunner::advance(std::string* error) {
    const auto* node = currentNode();
    if (!node || !blocked_) return fail(error, "conversation: runner is not blocked");
    if (node->kind == ConversationAsset::Node::Kind::Choice)
        return fail(error, "conversation: select a choice route instead");
    const std::string next = node->next;
    return enter(next, error) && runUntilBlocked(error);
}

bool ConversationRunner::select(const std::string& routeId, std::string* error) {
    const auto* node = currentNode();
    if (!node || !blocked_ || node->kind != ConversationAsset::Node::Kind::Choice)
        return fail(error, "conversation: runner is not waiting for a choice");
    for (const auto& route : node->routes) {
        if (route.first == routeId) return enter(route.second, error) && runUntilBlocked(error);
    }
    return fail(error, "conversation: unknown choice '" + routeId + "'");
}

}  // namespace eve::dialogue
