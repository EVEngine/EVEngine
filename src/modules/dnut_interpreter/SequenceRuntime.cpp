#include "dnut_interpreter/SequenceRuntime.h"

#include <string>
#include <utility>

namespace eve::dnut {

namespace {

/** @brief Insert every default entry that the target object does not already define. */
void mergeMissing(eve::Value& target, const eve::Value& defaults) {
    const auto* targetObject   = target.getIf<eve::Value::Object>();
    const auto* defaultsObject = defaults.getIf<eve::Value::Object>();
    if (!targetObject || !defaultsObject) return;
    for (const auto& entry : *defaultsObject) {
        if (target.find(entry.first) == nullptr) target.set(entry.first, entry.second);
    }
}

bool readString(const eve::Value& object, const std::string& key, std::string& out) {
    const eve::Value* value = object.find(key);
    if (!value || !value->isString()) return false;
    out = value->asString();
    return true;
}

eve::Value captureFrame(const SequenceAsset* asset, const std::string& node, const eve::Value& bindings,
                        const eve::Value& locals) {
    eve::Value out = eve::Value::Object{};
    out.set("asset", eve::Value::string(asset ? asset->id : std::string{}));
    out.set("version", eve::Value::integer(asset ? asset->version : 0));
    out.set("node", eve::Value::string(node));
    out.set("bindings", bindings);
    out.set("locals", locals);
    return out;
}

}  // namespace

const SequenceNode* SequenceRuntime::currentNode() const {
    return asset_ ? asset_->findNode(nodeId_) : nullptr;
}

eve::Result<void> SequenceRuntime::fail(eve::DiagnosticCode code, std::string message, std::string path) {
    failureText_ = message;
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "dnut.runtime"));
}

void SequenceRuntime::emit(EventKind kind, const SequenceNode* node, const std::string& detail) const {
    if (!eventSink_) return;
    eventSink_({kind, asset_ ? asset_->id : std::string{}, node ? node->id : nodeId_, detail});
}

void SequenceRuntime::stop() {
    asset_ = nullptr;
    nodeId_.clear();
    bindings_ = eve::Value::Object{};
    locals_   = eve::Value::Object{};
    blocked_     = false;
    waitingStep_ = false;
    callStack_.clear();
    lastStepResult_ = eve::Value{};
    lastConditionResult_.reset();
}

eve::Result<void> SequenceRuntime::start(const SequenceAsset* asset, eve::Value bindings) {
    if (!asset) return fail(eve::DiagnosticCode::InvalidArgument, "sequence: null asset", "asset");
    if (!bindings.isObject())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: bindings must be an object", "bindings");
    failureText_.clear();
    lastConditionResult_.reset();
    lastStepResult_ = eve::Value{};
    if (auto validation = asset->validate(); !validation.ok()) {
        const auto* diagnostic = validation.error();
        return fail(eve::DiagnosticCode::InvalidArgument,
                    diagnostic ? diagnostic->message() : "sequence: asset validation failed", "asset");
    }

    asset_       = asset;
    bindings_    = std::move(bindings);
    locals_      = eve::Value::Object{};
    callStack_.clear();
    blocked_     = false;
    waitingStep_ = false;
    emit(EventKind::Started);
    if (auto entered = enter(asset_->entry); !entered.ok()) return std::move(entered);
    return runUntilBlocked();
}

eve::Result<void> SequenceRuntime::enter(const std::string& nodeId) {
    if (!asset_) return fail(eve::DiagnosticCode::PreconditionViolation, "sequence: no active asset", "node");
    blocked_     = false;
    waitingStep_ = false;
    if (nodeId.empty()) {
        stop();
        return eve::Result<void>::success();
    }
    const SequenceNode* node = asset_->findNode(nodeId);
    if (!node)
        return fail(eve::DiagnosticCode::NotFound,
                    "sequence: asset '" + asset_->id + "' has no node '" + nodeId + "'", "node");
    nodeId_ = nodeId;
    emit(EventKind::NodeEntered, node);
    return eve::Result<void>::success();
}

eve::Result<std::string> SequenceRuntime::evaluateRoute(const SequenceNode& node) {
    for (const auto& route : node.routes) {
        if (route.condition.isNull()) return eve::Result<std::string>::success(route.target);
        if (!conditionEvaluator_)
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::PreconditionViolation,
                "sequence: node '" + node.id + "' requires a condition evaluator", "condition", {},
                "dnut.runtime"));
        SequenceConditionOutcome outcome = conditionEvaluator_(route.condition);
        lastConditionResult_             = outcome;
        if (outcome.passed) return eve::Result<std::string>::success(route.target);
    }
    return eve::Result<std::string>::success(node.next);
}

eve::Result<void> SequenceRuntime::runUntilBlocked() {
    int budget = kExecutionBudget;
    while (asset_ != nullptr) {
        if (budget-- <= 0)
            return fail(eve::DiagnosticCode::InvariantViolation, "sequence: execution budget exceeded", "budget");
        const SequenceNode* node = currentNode();
        if (!node) return fail(eve::DiagnosticCode::InvariantViolation, "sequence: invalid execution cursor", "cursor");

        if (node->type == "branch") {
            auto target = evaluateRoute(*node);
            if (!target.ok()) {
                const auto* diagnostic = target.error();
                return fail(eve::DiagnosticCode::PreconditionViolation,
                            diagnostic ? diagnostic->message() : "sequence: branch evaluation failed", "branch");
            }
            const std::string next = target.value();
            if (auto entered = enter(next); !entered.ok()) return std::move(entered);
            continue;
        }
        if (node->type == "choice") {
            blocked_     = true;
            waitingStep_ = false;
            emit(EventKind::Blocked, node, "choice");
            return eve::Result<void>::success();
        }
        if (node->type == "wait") {
            blocked_     = true;
            waitingStep_ = false;
            emit(EventKind::Blocked, node, "wait");
            return eve::Result<void>::success();
        }
        if (node->type == "end") {
            if (callStack_.empty()) {
                emit(EventKind::Ended, node);
                stop();
                return eve::Result<void>::success();
            }
            Frame frame = std::move(callStack_.back());
            callStack_.pop_back();
            asset_    = frame.asset;
            bindings_ = std::move(frame.bindings);
            locals_   = std::move(frame.locals);
            if (auto entered = enter(frame.returnNode); !entered.ok()) return std::move(entered);
            continue;
        }
        if (node->type == "call") {
            if (!assetResolver_)
                return fail(eve::DiagnosticCode::PreconditionViolation,
                            "sequence: call node requires an asset resolver", "call");
            const eve::Value* targetId = node->payload.find("target");
            if (!targetId || !targetId->isString() || targetId->asString().empty())
                return fail(eve::DiagnosticCode::InvariantViolation,
                            "sequence: call node '" + node->id + "' has no target asset id", "call.target");
            const SequenceAsset* target = assetResolver_(targetId->asString());
            if (!target)
                return fail(eve::DiagnosticCode::NotFound,
                            "sequence: missing called asset '" + targetId->asString() + "'", "call.target");
            if (auto validation = target->validate(); !validation.ok()) {
                const auto* diagnostic = validation.error();
                return fail(eve::DiagnosticCode::InvalidArgument,
                            diagnostic ? diagnostic->message() : "sequence: called asset is invalid", "call.target");
            }
            Frame frame;
            frame.asset      = asset_;
            frame.returnNode = node->next;
            frame.bindings   = bindings_;
            frame.locals     = locals_;
            callStack_.push_back(std::move(frame));
            eve::Value targetBindings = eve::Value::Object{};
            if (const eve::Value* arguments = node->payload.find("arguments"); arguments && arguments->isObject())
                targetBindings = *arguments;
            mergeMissing(targetBindings, bindings_);
            asset_    = target;
            bindings_ = std::move(targetBindings);
            locals_   = eve::Value::Object{};
            if (auto entered = enter(target->entry); !entered.ok()) return std::move(entered);
            continue;
        }

        if (!registry_)
            return fail(eve::DiagnosticCode::PreconditionViolation,
                        "sequence: step '" + node->type + "' requires a step registry", "steps." + node->id);
        const StepContext context{asset_->id, node->id, &bindings_, &locals_, hostContext_};
        StepOutcome       outcome = registry_->dispatch(*node, context);
        if (outcome.status == StepStatus::Failed)
            return fail(eve::DiagnosticCode::Failed,
                        outcome.error.empty() ? "sequence: step '" + node->type + "' failed" : outcome.error,
                        "steps." + node->id);
        if (outcome.status == StepStatus::Blocked) {
            blocked_     = true;
            waitingStep_ = true;
            lastStepResult_ = outcome.value;
            emit(EventKind::Blocked, node, node->type);
            return eve::Result<void>::success();
        }
        lastStepResult_ = std::move(outcome.value);
        emit(EventKind::Stepped, node, node->type);
        if (auto entered = enter(node->next); !entered.ok()) return std::move(entered);
    }
    return eve::Result<void>::success();
}

eve::Result<void> SequenceRuntime::advance() {
    const SequenceNode* node = currentNode();
    if (!node || !blocked_)
        return fail(eve::DiagnosticCode::PreconditionViolation, "sequence: runtime is not blocked", "advance");
    if (node->type == "choice")
        return fail(eve::DiagnosticCode::PreconditionViolation,
                    "sequence: select a choice route instead of advancing", "advance");
    const std::string next = node->next;
    waitingStep_           = false;
    if (auto entered = enter(next); !entered.ok()) return std::move(entered);
    return runUntilBlocked();
}

eve::Result<void> SequenceRuntime::select(std::string_view routeLabel) {
    const SequenceNode* node = currentNode();
    if (!node || !blocked_ || node->type != "choice")
        return fail(eve::DiagnosticCode::PreconditionViolation,
                    "sequence: runtime is not waiting for a choice", "route");

    for (const auto& route : node->routes) {
        if (route.label != routeLabel) continue;
        if (!route.condition.isNull()) {
            if (!conditionEvaluator_)
                return fail(eve::DiagnosticCode::PreconditionViolation,
                            "sequence: choice condition requires an evaluator", "route.condition");
            SequenceConditionOutcome outcome = conditionEvaluator_(route.condition);
            lastConditionResult_             = outcome;
            if (!outcome.passed)
                return fail(eve::DiagnosticCode::Conflict,
                            "sequence: choice condition rejected (" +
                                (outcome.reason.empty() ? std::string("rejected") : outcome.reason) + ")",
                            "route.condition");
        }

        eve::Value before;
        if (auto captured = captureState(before); !captured.ok())
            return fail(eve::DiagnosticCode::Failed, "sequence: could not capture choice state", "route");
        blocked_     = false;
        waitingStep_ = false;
        std::string error;
        if (auto entered = enter(route.target); !entered.ok()) {
            error = failureText_;
        } else if (auto ran = runUntilBlocked(); !ran.ok()) {
            error = failureText_;
        } else {
            return eve::Result<void>::success();
        }
        if (auto restored = restoreState(before); !restored.ok() && error.empty())
            error = "sequence: could not restore choice state";
        return fail(eve::DiagnosticCode::Failed,
                    error.empty() ? "sequence: choice selection failed" : std::move(error), "route");
    }
    return fail(eve::DiagnosticCode::NotFound,
                "sequence: unknown choice route '" + std::string(routeLabel) + "'", "route");
}

eve::Result<void> SequenceRuntime::resumeStep(eve::Value result) {
    const SequenceNode* node = currentNode();
    if (!node || !blocked_ || !waitingStep_)
        return fail(eve::DiagnosticCode::PreconditionViolation,
                    "sequence: runtime is not waiting for a step result", "step");
    eve::Value before;
    if (auto captured = captureState(before); !captured.ok())
        return fail(eve::DiagnosticCode::Failed, "sequence: could not capture step state", "step");

    if (const eve::Value* key = node->payload.find("result"); key && key->isString() && !key->asString().empty())
        locals_.set(key->asString(), result);
    lastStepResult_ = std::move(result);
    blocked_        = false;
    waitingStep_    = false;

    std::string error;
    if (auto entered = enter(node->next); !entered.ok()) {
        error = failureText_;
    } else if (auto ran = runUntilBlocked(); !ran.ok()) {
        error = failureText_;
    } else {
        emit(EventKind::Resumed, node, node->type);
        return eve::Result<void>::success();
    }
    if (auto restored = restoreState(before); !restored.ok() && error.empty())
        error = "sequence: could not restore step state";
    return fail(eve::DiagnosticCode::Failed,
                error.empty() ? "sequence: step resume failed" : std::move(error), "step");
}

eve::Result<void> SequenceRuntime::captureState(eve::Value& out) const {
    out = eve::Value::Object{};
    out.set("active", eve::Value::boolean(asset_ != nullptr));
    if (!asset_) return eve::Result<void>::success();
    out.set("current", captureFrame(asset_, nodeId_, bindings_, locals_));
    out.set("blocked", eve::Value::boolean(blocked_));
    out.set("waitingStep", eve::Value::boolean(waitingStep_));
    eve::Value::Array stack;
    stack.reserve(callStack_.size());
    for (const auto& frame : callStack_)
        stack.push_back(captureFrame(frame.asset, frame.returnNode, frame.bindings, frame.locals));
    out.set("stack", eve::Value(std::move(stack)));
    return eve::Result<void>::success();
}

eve::Result<void> SequenceRuntime::restoreState(const eve::Value& in) {
    if (!in.isObject())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: runtime state must be an object", "state");
    const eve::Value* active = in.find("active");
    if (!active || !active->isBool())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: runtime state is missing 'active'", "state");
    if (!active->asBool()) {
        stop();
        return eve::Result<void>::success();
    }
    if (!assetResolver_)
        return fail(eve::DiagnosticCode::PreconditionViolation,
                    "sequence: restore requires an asset resolver", "state");

    const eve::Value* current = in.find("current");
    if (!current || !current->isObject())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: runtime state is missing 'current'", "state");
    std::string assetId;
    std::string nodeId;
    if (!readString(*current, "asset", assetId) || !readString(*current, "node", nodeId))
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: saved frame is malformed", "state.current");

    const SequenceAsset* restoredAsset = assetResolver_(assetId);
    if (!restoredAsset)
        return fail(eve::DiagnosticCode::NotFound, "sequence: saved asset '" + assetId + "' is missing", "state.asset");
    const eve::Value* version = current->find("version");
    if (!version || !version->isInt64() || version->asInt() != restoredAsset->version)
        return fail(eve::DiagnosticCode::Conflict,
                    "sequence: saved asset version does not match '" + assetId + "'", "state.version");
    const eve::Value* savedBindings = current->find("bindings");
    const eve::Value* savedLocals   = current->find("locals");
    if (!savedBindings || !savedBindings->isObject() || !savedLocals || !savedLocals->isObject())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: saved values are malformed", "state.current");

    const eve::Value* stack = in.find("stack");
    if (!stack || !stack->isArray())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: runtime state is missing 'stack'", "state");
    std::vector<Frame> restoredStack;
    restoredStack.reserve(stack->arraySize());
    for (std::size_t index = 0; index < stack->arraySize(); ++index) {
        const eve::Value& saved        = stack->at(index);
        std::string       savedAssetId;
        std::string       returnNode;
        if (!saved.isObject() || !readString(saved, "asset", savedAssetId) ||
            !readString(saved, "node", returnNode))
            return fail(eve::DiagnosticCode::InvalidArgument, "sequence: saved call frame is malformed",
                        "state.stack");
        const SequenceAsset* savedAsset     = assetResolver_(savedAssetId);
        const eve::Value*    savedVersion   = saved.find("version");
        const eve::Value*    frameBindings  = saved.find("bindings");
        const eve::Value*    frameLocals    = saved.find("locals");
        if (!savedAsset || !savedVersion || !savedVersion->isInt64() ||
            savedVersion->asInt() != savedAsset->version || !frameBindings || !frameBindings->isObject() ||
            !frameLocals || !frameLocals->isObject() || !savedAsset->findNode(returnNode))
            return fail(eve::DiagnosticCode::Conflict, "sequence: saved call frame cannot be restored", "state.stack");
        restoredStack.push_back({savedAsset, returnNode, *frameBindings, *frameLocals});
    }
    const eve::Value* blocked = in.find("blocked");
    if (!blocked || !blocked->isBool())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: runtime state is missing 'blocked'", "state");
    const eve::Value* waitingStep = in.find("waitingStep");
    if (waitingStep && !waitingStep->isBool())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: waitingStep is malformed", "state");
    if (!restoredAsset->findNode(nodeId))
        return fail(eve::DiagnosticCode::NotFound, "sequence: saved node '" + nodeId + "' is missing", "state.node");

    asset_       = restoredAsset;
    nodeId_      = std::move(nodeId);
    bindings_    = *savedBindings;
    locals_      = *savedLocals;
    blocked_     = blocked->asBool();
    waitingStep_ = waitingStep && waitingStep->asBool();
    callStack_   = std::move(restoredStack);
    failureText_.clear();
    return eve::Result<void>::success();
}

}  // namespace eve::dnut
