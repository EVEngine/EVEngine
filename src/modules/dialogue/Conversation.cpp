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
    callStack_.clear();
    blocked_ = false;
    waitingCommand_ = false;
    emit(Event::Kind::Started);
    return enter(asset_->entry, error) && runUntilBlocked(error);
}

void ConversationRunner::stop() {
    asset_ = nullptr;
    nodeId_.clear();
    bindings_ = StateValue::object();
    locals_ = StateValue::object();
    blocked_ = false;
    callStack_.clear();
    waitingCommand_ = false;
}

void ConversationRunner::registerCommand(const std::string& name, CommandHandler handler) {
    if (!name.empty() && handler) commandHandlers_[name] = std::move(handler);
}

void ConversationRunner::unregisterCommand(const std::string& name) {
    commandHandlers_.erase(name);
}

void ConversationRunner::emit(Event::Kind kind, const ConversationAsset::Node* node,
                              const std::string& name) const {
    if (!eventSink_) return;
    eventSink_({kind, asset_ ? asset_->id : std::string{}, node ? node->id : nodeId_, name});
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
    waitingCommand_ = false;
    emit(Event::Kind::NodeEntered, asset_->findNode(nodeId));
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
                emit(Event::Kind::Line, node);
                blocked_ = true;
                return true;
            case ConversationAsset::Node::Kind::Choice:
                emit(Event::Kind::Choice, node);
                blocked_ = true;
                return true;
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
                if (callStack_.empty()) {
                    emit(Event::Kind::Ended, node);
                    stop();
                    return true;
                } else {
                    Frame frame = std::move(callStack_.back());
                    callStack_.pop_back();
                    asset_ = frame.asset;
                    bindings_ = std::move(frame.bindings);
                    locals_ = std::move(frame.locals);
                    if (!enter(frame.returnNode, error)) return false;
                }
                break;
            case ConversationAsset::Node::Kind::Call: {
                if (!assetResolver_)
                    return fail(error, "conversation: call node requires an asset resolver");
                const ConversationAsset* target = assetResolver_(node->target);
                if (!target)
                    return fail(error, "conversation: missing called asset '" + node->target + "'");
                std::string validationError;
                if (!target->validate(&validationError)) return fail(error, validationError);
                Frame frame;
                frame.asset = asset_;
                frame.returnNode = node->returnNode.empty() ? node->next : node->returnNode;
                frame.bindings = bindings_;
                frame.locals = locals_;
                callStack_.push_back(std::move(frame));
                StateValue targetBindings = node->arguments;
                targetBindings.mergeDefaults(bindings_);
                asset_ = target;
                bindings_ = std::move(targetBindings);
                locals_ = StateValue::object();
                if (!enter(target->entry, error)) return false;
                break;
            }
            case ConversationAsset::Node::Kind::Command: {
                const auto it = commandHandlers_.find(node->target);
                if (it == commandHandlers_.end())
                    return fail(error, "conversation: command '" + node->target +
                                           "' is not registered");
                emit(Event::Kind::Command, node, node->target);
                CommandResult result = it->second(node->arguments, bindings_, locals_);
                if (result.status == CommandResult::Status::Failed)
                    return fail(error, result.error.empty() ? "conversation: command failed"
                                                            : result.error);
                if (result.status == CommandResult::Status::Blocked) {
                    blocked_ = true;
                    waitingCommand_ = true;
                    return true;
                }
                if (!node->expression.empty())
                    locals_.set(node->expression, std::move(result.value));
                if (!enter(node->next, error)) return false;
                break;
            }
        }
    }
    return fail(error, "conversation: execution budget exceeded");
}

namespace {

bool readString(const StateValue& object, const std::string& key, std::string& out) {
    const StateValue* value = object.find(key);
    if (!value || !value->isString()) return false;
    out = value->asString();
    return true;
}

StateValue captureFrame(const ConversationAsset* asset, const std::string& node,
                        const StateValue& bindings, const StateValue& locals) {
    StateValue out = StateValue::object();
    out.set("asset", StateValue::string(asset ? asset->id : std::string{}));
    out.set("version", StateValue::integer(asset ? asset->version : 0));
    out.set("node", StateValue::string(node));
    out.set("bindings", bindings);
    out.set("locals", locals);
    return out;
}

}  // namespace

bool ConversationRunner::captureState(StateValue& out) const {
    out = StateValue::object();
    out.set("active", StateValue::boolean(asset_ != nullptr));
    if (!asset_) return true;
    out.set("current", captureFrame(asset_, nodeId_, bindings_, locals_));
    out.set("blocked", StateValue::boolean(blocked_));
    out.set("waitingCommand", StateValue::boolean(waitingCommand_));
    StateValue stack = StateValue::array();
    for (const auto& frame : callStack_)
        stack.pushBack(captureFrame(frame.asset, frame.returnNode, frame.bindings, frame.locals));
    out.set("stack", std::move(stack));
    return true;
}

bool ConversationRunner::restoreState(const StateValue& in, std::string* error) {
    if (!in.isObject()) return fail(error, "conversation: runner state must be an object");
    const StateValue* active = in.find("active");
    if (!active || !active->isBool()) return fail(error, "conversation: state is missing active");
    if (!active->asBool()) {
        stop();
        return true;
    }
    if (!assetResolver_) return fail(error, "conversation: restore requires an asset resolver");
    const StateValue* current = in.find("current");
    if (!current || !current->isObject()) return fail(error, "conversation: state is missing current");
    std::string assetId;
    std::string nodeId;
    if (!readString(*current, "asset", assetId) || !readString(*current, "node", nodeId))
        return fail(error, "conversation: current frame is malformed");
    const ConversationAsset* restoredAsset = assetResolver_(assetId);
    if (!restoredAsset) return fail(error, "conversation: saved asset '" + assetId + "' is missing");
    const StateValue* version = current->find("version");
    if (!version || !version->isInt() || version->asInt() != restoredAsset->version)
        return fail(error, "conversation: saved asset version does not match '" + assetId + "'");
    const StateValue* savedBindings = current->find("bindings");
    const StateValue* savedLocals = current->find("locals");
    if (!savedBindings || !savedBindings->isObject() || !savedLocals || !savedLocals->isObject())
        return fail(error, "conversation: current values are malformed");

    std::vector<Frame> restoredStack;
    const StateValue* stack = in.find("stack");
    if (!stack || !stack->isArray()) return fail(error, "conversation: state is missing stack");
    for (size_t i = 0; i < stack->arraySize(); ++i) {
        const StateValue& saved = stack->at(i);
        std::string savedAssetId;
        std::string returnNode;
        if (!saved.isObject() || !readString(saved, "asset", savedAssetId) ||
            !readString(saved, "node", returnNode))
            return fail(error, "conversation: saved call frame is malformed");
        const ConversationAsset* savedAsset = assetResolver_(savedAssetId);
        const StateValue* savedVersion = saved.find("version");
        const StateValue* frameBindings = saved.find("bindings");
        const StateValue* frameLocals = saved.find("locals");
        if (!savedAsset || !savedVersion || !savedVersion->isInt() ||
            savedVersion->asInt() != savedAsset->version || !frameBindings ||
            !frameBindings->isObject() || !frameLocals || !frameLocals->isObject() ||
            !savedAsset->findNode(returnNode))
            return fail(error, "conversation: saved call frame cannot be restored");
        restoredStack.push_back({savedAsset, returnNode, *frameBindings, *frameLocals});
    }
    if (!restoredAsset->findNode(nodeId))
        return fail(error, "conversation: saved node '" + nodeId + "' is missing");
    const StateValue* blocked = in.find("blocked");
    if (!blocked || !blocked->isBool()) return fail(error, "conversation: state is missing blocked");
    const StateValue* waitingCommand = in.find("waitingCommand");
    if (waitingCommand && !waitingCommand->isBool())
        return fail(error, "conversation: waitingCommand is malformed");
    asset_ = restoredAsset;
    nodeId_ = std::move(nodeId);
    bindings_ = *savedBindings;
    locals_ = *savedLocals;
    blocked_ = blocked->asBool();
    waitingCommand_ = waitingCommand && waitingCommand->asBool();
    callStack_ = std::move(restoredStack);
    return true;
}

bool ConversationRunner::resumeCommand(StateValue result, std::string* error) {
    const auto* node = currentNode();
    if (!node || !blocked_ || !waitingCommand_ ||
        node->kind != ConversationAsset::Node::Kind::Command)
        return fail(error, "conversation: runner is not waiting for a command");
    if (!node->expression.empty()) locals_.set(node->expression, std::move(result));
    return enter(node->next, error) && runUntilBlocked(error);
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
