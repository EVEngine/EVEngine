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

eve::Value toCanonicalValue(const eve::StateValue& value) {
    switch (value.kind()) {
        case eve::StateValue::Kind::Null: return {};
        case eve::StateValue::Kind::Int: return eve::Value::integer(value.asInt());
        case eve::StateValue::Kind::Float: return eve::Value::number(value.asDouble());
        case eve::StateValue::Kind::Bool: return eve::Value::boolean(value.asBool());
        case eve::StateValue::Kind::String: return eve::Value::string(value.asString());
        case eve::StateValue::Kind::Array: {
            eve::Value::Array out;
            out.reserve(value.arraySize());
            for (std::size_t index = 0; index < value.arraySize(); ++index)
                out.push_back(toCanonicalValue(value.at(index)));
            return eve::Value(std::move(out));
        }
        case eve::StateValue::Kind::Object: {
            eve::Value::Object out;
            for (const auto& key : value.keys()) out.emplace(key, toCanonicalValue(*value.find(key)));
            return eve::Value(std::move(out));
        }
    }
    return {};
}

bool parameterTypeMatches(const eve::Value& value, SequenceParameterType type) {
    switch (type) {
        case SequenceParameterType::Any: return true;
        case SequenceParameterType::String: return value.isString();
        case SequenceParameterType::Integer: return value.isInt64();
        case SequenceParameterType::Number: return value.isNumeric();
        case SequenceParameterType::Boolean: return value.isBool();
    }
    return false;
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

SequenceRuntime::ExecutionState SequenceRuntime::captureExecutionState() const {
    return {asset_,
            nodeId_,
            bindings_,
            locals_,
            blocked_,
            waitingStep_,
            waitingCommand_,
            callStack_,
            loopStack_,
            parallelStack_,
            lastStepResult_,
            lastCommandRequest_,
            pendingCommandRequestId_,
            commandSequence_,
            lastConditionResult_};
}

void SequenceRuntime::restoreExecutionState(ExecutionState state) {
    asset_                   = state.asset;
    nodeId_                  = std::move(state.nodeId);
    bindings_                = std::move(state.bindings);
    locals_                  = std::move(state.locals);
    blocked_                 = state.blocked;
    waitingStep_             = state.waitingStep;
    waitingCommand_          = state.waitingCommand;
    callStack_               = std::move(state.callStack);
    loopStack_               = std::move(state.loopStack);
    parallelStack_           = std::move(state.parallelStack);
    lastStepResult_          = std::move(state.lastStepResult);
    lastCommandRequest_      = std::move(state.lastCommandRequest);
    pendingCommandRequestId_ = std::move(state.pendingCommandRequestId);
    commandSequence_         = state.commandSequence;
    lastConditionResult_     = std::move(state.lastConditionResult);
}

eve::Result<void> SequenceRuntime::fail(eve::DiagnosticCode code, std::string message, std::string path) {
    failureText_ = message;
    return eve::Result<void>::failure(eve::Diagnostic::error(code, message, std::move(path), {}, "dnut.runtime"));
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
    waitingCommand_ = false;
    callStack_.clear();
    loopStack_.clear();
    parallelStack_.clear();
    lastStepResult_ = eve::Value{};
    lastCommandRequest_.reset();
    pendingCommandRequestId_.clear();
    lastConditionResult_.reset();
}

void SequenceRuntime::registerCommand(std::string name, SequenceCommandHandler handler) {
    if (!name.empty() && handler) commandHandlers_[std::move(name)] = std::move(handler);
}

void SequenceRuntime::unregisterCommand(std::string_view name) { commandHandlers_.erase(std::string(name)); }

eve::Result<void> SequenceRuntime::start(const SequenceAsset* asset, eve::Value bindings) {
    if (!asset) return fail(eve::DiagnosticCode::InvalidArgument, "sequence: null asset", "asset");
    if (!bindings.isObject())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: bindings must be an object", "bindings");
    if (auto validation = asset->validate(); !validation.ok()) {
        const auto* diagnostic = validation.error();
        return fail(eve::DiagnosticCode::InvalidArgument,
                    diagnostic ? diagnostic->message() : "sequence: asset validation failed", "asset");
    }

    eve::Value resolvedBindings = bindings;
    for (const auto& parameter : asset->parameters) {
        const eve::Value* value = resolvedBindings.find(parameter.name);
        if (!value && !parameter.defaultValue.isNull()) {
            resolvedBindings.set(parameter.name, parameter.defaultValue);
            value = resolvedBindings.find(parameter.name);
        }
        if (!value && parameter.required)
            return fail(eve::DiagnosticCode::InvalidArgument,
                        "sequence '" + asset->id + "': missing required binding '" + parameter.name + "'",
                        "bindings." + parameter.name);
        if (value && !parameterTypeMatches(*value, parameter.type))
            return fail(eve::DiagnosticCode::InvalidArgument,
                        "sequence '" + asset->id + "': binding '" + parameter.name + "' must be " +
                            sequenceParameterTypeName(parameter.type),
                        "bindings." + parameter.name);
    }
    for (const auto& key : bindings.keys()) {
        bool declared = false;
        for (const auto& parameter : asset->parameters)
            if (parameter.name == key) {
                declared = true;
                break;
            }
        if (!declared)
            return fail(eve::DiagnosticCode::InvalidArgument,
                        "sequence '" + asset->id + "': undeclared binding '" + key + "'", "bindings." + key);
    }

    ExecutionState before = captureExecutionState();
    failureText_.clear();
    lastConditionResult_.reset();
    lastStepResult_ = eve::Value{};
    asset_       = asset;
    bindings_       = std::move(resolvedBindings);
    locals_      = eve::Value::Object{};
    callStack_.clear();
    loopStack_.clear();
    parallelStack_.clear();
    blocked_     = false;
    waitingStep_ = false;
    waitingCommand_ = false;
    pendingCommandRequestId_.clear();
    lastCommandRequest_.reset();
    emit(EventKind::Started);
    if (auto entered = enter(asset_->entry); !entered.ok()) {
        restoreExecutionState(std::move(before));
        return entered;
    }
    auto ran = runUntilBlockedImpl();
    if (!ran.ok()) restoreExecutionState(std::move(before));
    return ran;
}

eve::Result<void> SequenceRuntime::enter(const std::string& nodeId) {
    if (!asset_) return fail(eve::DiagnosticCode::PreconditionViolation, "sequence: no active asset", "node");
    blocked_     = false;
    waitingStep_ = false;
    waitingCommand_ = false;
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
        if (!outcome.error.empty())
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, outcome.error, "condition", {}, "dnut.runtime"));
        if (outcome.passed) return eve::Result<std::string>::success(route.target);
    }
    return eve::Result<std::string>::success(node.next);
}

eve::Result<void> SequenceRuntime::runUntilBlocked() {
    ExecutionState before = captureExecutionState();
    auto           ran    = runUntilBlockedImpl();
    if (!ran.ok()) restoreExecutionState(std::move(before));
    return ran;
}

eve::Result<void> SequenceRuntime::runUntilBlockedImpl() {
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
            const std::string& next = target.value();
            if (auto entered = enter(next); !entered.ok()) return entered;
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
        if (node->type == "label") {
            if (auto entered = enter(node->next); !entered.ok()) return entered;
            continue;
        }
        if (node->type == "jump") {
            const eve::Value* target = node->payload.find("target");
            if (!target || !target->isString() || target->asString().empty())
                return fail(eve::DiagnosticCode::InvariantViolation,
                            "sequence: jump node '" + node->id + "' has no target", "jump.target");
            if (auto entered = enter(target->asString()); !entered.ok()) return entered;
            continue;
        }
        if (node->type == "loop") {
            if (auto handled = handleLoop(*node); !handled.ok()) return handled;
            continue;
        }
        if (node->type == "break") {
            if (auto handled = handleBreakOrContinue(*node, true); !handled.ok()) return handled;
            continue;
        }
        if (node->type == "continue") {
            if (auto handled = handleBreakOrContinue(*node, false); !handled.ok()) return handled;
            continue;
        }
        if (node->type == "parallel") {
            if (auto handled = handleParallel(*node); !handled.ok()) return handled;
            if (blocked_) return eve::Result<void>::success();
            continue;
        }
        if (node->type == "join") {
            if (auto handled = handleJoin(*node); !handled.ok()) return handled;
            if (blocked_) return eve::Result<void>::success();
            continue;
        }
        if (node->type == "end") {
            if (callStack_.empty()) {
                emit(EventKind::Ended, node);
                stop();
                return eve::Result<void>::success();
            }
            Frame frame = std::move(callStack_.back());
            callStack_.pop_back();
            asset_         = frame.asset;
            bindings_      = std::move(frame.bindings);
            locals_        = std::move(frame.locals);
            loopStack_     = std::move(frame.loopStack);
            parallelStack_ = std::move(frame.parallelStack);
            if (auto entered = enter(frame.returnNode); !entered.ok()) return entered;
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
            if (const eve::Value* returnNode = node->payload.find("return");
                returnNode && returnNode->isString() && !returnNode->asString().empty())
                frame.returnNode = returnNode->asString();
            frame.bindings      = bindings_;
            frame.locals        = locals_;
            frame.loopStack     = loopStack_;
            frame.parallelStack = parallelStack_;
            callStack_.push_back(std::move(frame));
            eve::Value targetBindings = eve::Value::Object{};
            if (const eve::Value* arguments = node->payload.find("arguments"); arguments && arguments->isObject())
                targetBindings = *arguments;
            mergeMissing(targetBindings, bindings_);
            asset_    = target;
            bindings_ = std::move(targetBindings);
            locals_   = eve::Value::Object{};
            loopStack_.clear();
            parallelStack_.clear();
            if (auto entered = enter(target->entry); !entered.ok()) return entered;
            continue;
        }

        if (node->type == "command") {
            const eve::Value* name = node->payload.find("name");
            if (!name) name = node->payload.find("target");
            if (!name || !name->isString() || name->asString().empty())
                return fail(eve::DiagnosticCode::InvariantViolation,
                            "sequence: command node '" + node->id + "' has no name", "command.name");
            const auto named = commandHandlers_.find(name->asString());
            if (named == commandHandlers_.end() && !commandDispatcher_)
                return fail(eve::DiagnosticCode::NotFound,
                            "sequence: command '" + name->asString() + "' is not registered", "command.name");

            SequenceCommandRequest request;
            request.requestId = asset_->id + ":" + node->id + ":" + std::to_string(commandSequence_++);
            request.name      = name->asString();
            if (const eve::Value* arguments = node->payload.find("arguments"); arguments && arguments->isObject())
                request.arguments = *arguments;
            request.bindings    = bindings_;
            request.locals      = locals_;
            request.payload     = node->payload;
            lastCommandRequest_ = request;
            SequenceCommandResponse response =
                named != commandHandlers_.end() ? named->second(request) : commandDispatcher_(request);
            if (response.status == SequenceCommandResponse::Status::Failed)
                return fail(eve::DiagnosticCode::Failed,
                            response.error.empty() ? "sequence: command failed" : response.error,
                            "command." + node->id);
            if (response.status == SequenceCommandResponse::Status::Blocked) {
                blocked_                 = true;
                waitingCommand_          = true;
                waitingStep_             = false;
                pendingCommandRequestId_ = request.requestId;
                emit(EventKind::Blocked, node, "command");
                return eve::Result<void>::success();
            }
            const eve::Value* resultLocal = node->payload.find("resultLocal");
            if (!resultLocal) resultLocal = node->payload.find("result");
            if (resultLocal && resultLocal->isString() && !resultLocal->asString().empty())
                locals_.set(resultLocal->asString(), response.value);
            lastStepResult_ = std::move(response.value);
            emit(EventKind::Stepped, node, "command");
            if (auto entered = enter(node->next); !entered.ok()) return entered;
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
        if (auto entered = enter(node->next); !entered.ok()) return entered;
    }
    return eve::Result<void>::success();
}

eve::Result<void> SequenceRuntime::advance() {
    const SequenceNode* node = currentNode();
    if (!node || !blocked_)
        return fail(eve::DiagnosticCode::PreconditionViolation, "sequence: runtime is not blocked", "advance");
    if (waitingCommand_)
        return fail(eve::DiagnosticCode::PreconditionViolation,
                    "sequence: resume the pending command instead of advancing", "advance");
    if (node->type == "choice")
        return fail(eve::DiagnosticCode::PreconditionViolation,
                    "sequence: select a choice route instead of advancing", "advance");
    ExecutionState    before = captureExecutionState();
    const std::string next = node->next;
    waitingStep_           = false;
    if (!parallelStack_.empty() && parallelStack_.back().hostBlocked) {
        ParallelFrame& frame = parallelStack_.back();
        ParallelLane*  lane  = nullptr;
        for (auto& candidate : frame.lanes) {
            if (!candidate.done && candidate.blocked && candidate.nodeId == nodeId_) {
                lane = &candidate;
                break;
            }
        }
        if (!lane) {
            restoreExecutionState(std::move(before));
            return fail(eve::DiagnosticCode::InvariantViolation,
                        "sequence: parallel host block has no matching lane", "parallel");
        }
        lane->blocked     = false;
        lane->waitingStep = false;
        lane->nodeId      = next;
        blocked_          = false;
        frame.hostBlocked = false;
        if (auto driven = driveParallelUntilBlocked(); !driven.ok()) {
            restoreExecutionState(std::move(before));
            return driven;
        }
        if (!blocked_) {
            auto ran = runUntilBlockedImpl();
            if (!ran.ok()) restoreExecutionState(std::move(before));
            return ran;
        }
        return eve::Result<void>::success();
    }
    if (auto entered = enter(next); !entered.ok()) {
        restoreExecutionState(std::move(before));
        return entered;
    }
    auto ran = runUntilBlockedImpl();
    if (!ran.ok()) restoreExecutionState(std::move(before));
    return ran;
}

eve::Result<void> SequenceRuntime::select(std::string_view routeLabel) { return selectImpl(routeLabel); }

eve::Result<void> SequenceRuntime::selectRouteForTransaction(std::string_view routeLabel) {
    return selectImpl(routeLabel);
}

eve::Result<void> SequenceRuntime::selectImpl(std::string_view routeLabel) {
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
            if (!outcome.error.empty()) return fail(eve::DiagnosticCode::Failed, outcome.error, "route.condition");
            if (!outcome.passed)
                return fail(eve::DiagnosticCode::Conflict,
                            "sequence: choice condition rejected (" +
                                (outcome.reason.empty() ? std::string("rejected") : outcome.reason) + ")",
                            "route.condition");
        }

        ExecutionState before = captureExecutionState();
        blocked_     = false;
        waitingStep_ = false;
        std::string error;
        if (auto entered = enter(route.target); !entered.ok()) {
            error = failureText_;
        } else if (auto ran = runUntilBlockedImpl(); !ran.ok()) {
            error = failureText_;
        } else {
            return eve::Result<void>::success();
        }
        restoreExecutionState(std::move(before));
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
    ExecutionState before = captureExecutionState();

    if (const eve::Value* key = node->payload.find("result"); key && key->isString() && !key->asString().empty())
        locals_.set(key->asString(), result);
    lastStepResult_ = std::move(result);
    blocked_        = false;
    waitingStep_    = false;

    if (!parallelStack_.empty() && parallelStack_.back().hostBlocked) {
        ParallelFrame& frame = parallelStack_.back();
        ParallelLane*  lane  = nullptr;
        for (auto& candidate : frame.lanes) {
            if (!candidate.done && candidate.blocked && candidate.waitingStep && candidate.nodeId == nodeId_) {
                lane = &candidate;
                break;
            }
        }
        if (!lane) {
            restoreExecutionState(std::move(before));
            return fail(eve::DiagnosticCode::InvariantViolation,
                        "sequence: parallel step resume has no matching lane", "parallel");
        }
        lane->blocked         = false;
        lane->waitingStep     = false;
        lane->lastStepResult  = lastStepResult_;
        lane->nodeId          = node->next;
        frame.hostBlocked     = false;
        if (auto driven = driveParallelUntilBlocked(); !driven.ok()) {
            restoreExecutionState(std::move(before));
            return driven;
        }
        emit(EventKind::Resumed, node, node->type);
        if (!blocked_) {
            auto ran = runUntilBlockedImpl();
            if (!ran.ok()) {
                restoreExecutionState(std::move(before));
                return ran;
            }
        }
        return eve::Result<void>::success();
    }

    std::string error;
    if (auto entered = enter(node->next); !entered.ok()) {
        error = failureText_;
    } else if (auto ran = runUntilBlockedImpl(); !ran.ok()) {
        error = failureText_;
    } else {
        emit(EventKind::Resumed, node, node->type);
        return eve::Result<void>::success();
    }
    restoreExecutionState(std::move(before));
    return fail(eve::DiagnosticCode::Failed,
                error.empty() ? "sequence: step resume failed" : std::move(error), "step");
}

eve::Result<void> SequenceRuntime::handleLoop(const SequenceNode& node) {
    const eve::Value* modeValue = node.payload.find("mode");
    if (!modeValue || !modeValue->isString())
        return fail(eve::DiagnosticCode::InvariantViolation, "sequence: loop missing mode", "loop.mode");
    const std::string& mode = modeValue->asString();
    if (node.routes.empty())
        return fail(eve::DiagnosticCode::InvariantViolation, "sequence: loop missing body", "loop.routes");

    LoopFrame* frame = nullptr;
    if (!loopStack_.empty() && loopStack_.back().loopNodeId == node.id) frame = &loopStack_.back();
    if (!frame) {
        LoopFrame created;
        created.loopNodeId  = node.id;
        created.afterNodeId = node.next;
        created.mode        = mode;
        if (mode == "times") {
            const eve::Value* count = node.payload.find("count");
            if (!count || !count->isNumeric())
                return fail(eve::DiagnosticCode::InvariantViolation, "sequence: loop times missing count",
                            "loop.count");
            created.remaining = count->isInt64() ? static_cast<int>(count->asInt())
                                                 : static_cast<int>(count->asDouble());
            if (created.remaining < 0) created.remaining = 0;
        }
        loopStack_.push_back(std::move(created));
        frame = &loopStack_.back();
    }

    const std::string& body = node.routes.front().target;
    if (mode == "forever") {
        frame->entered = true;
        return enter(body);
    }
    if (mode == "while") {
        auto target = evaluateRoute(node);
        if (!target.ok()) {
            const auto* diagnostic = target.error();
            return fail(eve::DiagnosticCode::PreconditionViolation,
                        diagnostic ? diagnostic->message() : "sequence: loop while evaluation failed", "loop");
        }
        if (target.value() == body) {
            frame->entered = true;
            return enter(body);
        }
        loopStack_.pop_back();
        return enter(node.next);
    }
    if (mode == "times") {
        if (frame->remaining <= 0) {
            loopStack_.pop_back();
            return enter(node.next);
        }
        frame->remaining -= 1;
        frame->entered = true;
        return enter(body);
    }
    return fail(eve::DiagnosticCode::InvariantViolation, "sequence: unknown loop mode '" + mode + "'", "loop.mode");
}

eve::Result<void> SequenceRuntime::handleBreakOrContinue(const SequenceNode& node, bool isBreak) {
    if (loopStack_.empty())
        return fail(eve::DiagnosticCode::PreconditionViolation,
                    std::string("sequence: ") + (isBreak ? "break" : "continue") + " without an active loop",
                    isBreak ? "break" : "continue");
    if (isBreak) {
        const std::string after = loopStack_.back().afterNodeId;
        loopStack_.pop_back();
        return enter(after.empty() ? node.next : after);
    }
    return enter(loopStack_.back().loopNodeId);
}

eve::Result<void> SequenceRuntime::handleParallel(const SequenceNode& node) {
    if (node.routes.size() < 2)
        return fail(eve::DiagnosticCode::InvariantViolation, "sequence: parallel needs two lanes", "parallel");
    const eve::Value* join = node.payload.find("join");
    if (!join || !join->isString() || join->asString().empty())
        return fail(eve::DiagnosticCode::InvariantViolation, "sequence: parallel missing join", "parallel.join");
    if (parallelStack_.empty() || parallelStack_.back().parallelNodeId != node.id) {
        ParallelFrame frame;
        frame.parallelNodeId = node.id;
        frame.joinNext       = node.next;
        frame.joinNodeId     = join->asString();
        for (const auto& route : node.routes) {
            ParallelLane lane;
            lane.nodeId = route.target;
            frame.lanes.push_back(std::move(lane));
        }
        parallelStack_.push_back(std::move(frame));
    }
    return driveParallelUntilBlocked();
}

eve::Result<void> SequenceRuntime::handleJoin(const SequenceNode& node) {
    if (parallelStack_.empty())
        return fail(eve::DiagnosticCode::PreconditionViolation, "sequence: join without parallel", "join");
    ParallelFrame& frame = parallelStack_.back();
    for (auto& lane : frame.lanes) {
        if (!lane.done && lane.nodeId == node.id) lane.done = true;
    }
    (void)node;
    return driveParallelUntilBlocked();
}

eve::Result<void> SequenceRuntime::stepLaneOnce(ParallelLane& lane) {
    if (lane.done || lane.blocked) return eve::Result<void>::success();
    if (lane.nodeId.empty()) {
        lane.done = true;
        return eve::Result<void>::success();
    }
    const SequenceNode* node = asset_->findNode(lane.nodeId);
    if (!node)
        return fail(eve::DiagnosticCode::NotFound, "sequence: parallel lane missing node '" + lane.nodeId + "'",
                    "parallel.lane");

    if (node->type == "join") {
        lane.done = true;
        return eve::Result<void>::success();
    }
    if (node->type == "label") {
        lane.nodeId = node->next;
        return eve::Result<void>::success();
    }
    if (node->type == "jump") {
        const eve::Value* target = node->payload.find("target");
        if (!target || !target->isString() || target->asString().empty())
            return fail(eve::DiagnosticCode::InvariantViolation, "sequence: jump has no target", "jump.target");
        lane.nodeId = target->asString();
        return eve::Result<void>::success();
    }
    if (node->type == "wait") {
        lane.blocked     = true;
        lane.waitingStep = false;
        return eve::Result<void>::success();
    }
    if (node->type == "choice" || node->type == "command" || node->type == "call" || node->type == "parallel" ||
        node->type == "loop" || node->type == "break" || node->type == "continue" || node->type == "end" ||
        node->type == "branch") {
        return fail(eve::DiagnosticCode::Failed,
                    "sequence: '" + node->type + "' is not supported inside a parallel lane", "parallel.lane");
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
        lane.blocked        = true;
        lane.waitingStep    = true;
        lane.lastStepResult = outcome.value;
        return eve::Result<void>::success();
    }
    lastStepResult_ = std::move(outcome.value);
    emit(EventKind::Stepped, node, node->type);
    lane.nodeId = node->next;
    if (lane.nodeId.empty()) lane.done = true;
    else if (const SequenceNode* landed = asset_->findNode(lane.nodeId); landed && landed->type == "join")
        lane.done = true;
    return eve::Result<void>::success();
}

eve::Result<void> SequenceRuntime::driveParallelUntilBlocked() {
    if (parallelStack_.empty())
        return fail(eve::DiagnosticCode::PreconditionViolation, "sequence: no parallel frame", "parallel");
    int budget = kExecutionBudget;
    while (true) {
        if (budget-- <= 0)
            return fail(eve::DiagnosticCode::InvariantViolation, "sequence: parallel budget exceeded", "budget");
        ParallelFrame& frame   = parallelStack_.back();
        bool           allDone = true;
        bool           progressed = false;
        for (std::size_t index = 0; index < frame.lanes.size(); ++index) {
            ParallelLane& lane = frame.lanes[index];
            if (lane.done) continue;
            allDone = false;
            while (!lane.done && !lane.blocked) {
                const std::string before = lane.nodeId;
                if (auto stepped = stepLaneOnce(lane); !stepped.ok()) return stepped;
                if (lane.nodeId != before || lane.done || lane.blocked) progressed = true;
                if (lane.blocked) break;
                if (budget-- <= 0)
                    return fail(eve::DiagnosticCode::InvariantViolation, "sequence: parallel budget exceeded",
                                "budget");
            }
        }
        if (allDone) {
            const std::string joinNext = frame.joinNext;
            parallelStack_.pop_back();
            blocked_     = false;
            waitingStep_ = false;
            return enter(joinNext);
        }
        ParallelLane* blockedLane = nullptr;
        for (auto& lane : frame.lanes) {
            if (!lane.done && lane.blocked) {
                blockedLane = &lane;
                break;
            }
        }
        if (blockedLane) {
            nodeId_           = blockedLane->nodeId;
            blocked_          = true;
            waitingStep_      = blockedLane->waitingStep;
            lastStepResult_   = blockedLane->lastStepResult;
            frame.hostBlocked = true;
            frame.activeLane  = static_cast<std::size_t>(blockedLane - frame.lanes.data());
            const SequenceNode* node = currentNode();
            emit(EventKind::Blocked, node, node ? node->type : "parallel");
            return eve::Result<void>::success();
        }
        if (!progressed)
            return fail(eve::DiagnosticCode::InvariantViolation, "sequence: parallel made no progress", "parallel");
    }
}

eve::Result<void> SequenceRuntime::resumeCommand(std::string_view requestId, eve::StateValue result) {
    return resumeCommandImpl(requestId, toCanonicalValue(result));
}

eve::Result<void> SequenceRuntime::resumeCommand(std::string_view requestId, eve::Value result) {
    return resumeCommandImpl(requestId, std::move(result));
}

eve::Result<void> SequenceRuntime::resumeCommandImpl(std::string_view requestId, eve::Value result) {
    const SequenceNode* node = currentNode();
    if (!node || !blocked_ || !waitingCommand_ || node->type != "command")
        return fail(eve::DiagnosticCode::DialogueNotWaitingForCommand, "sequence: runtime is not waiting for a command",
                    "command");
    if (requestId.empty() || requestId != pendingCommandRequestId_)
        return fail(eve::DiagnosticCode::Conflict, "sequence: command request id is stale or does not match",
                    "command.requestId");

    ExecutionState before = captureExecutionState();

    const eve::Value* resultLocal = node->payload.find("resultLocal");
    if (!resultLocal) resultLocal = node->payload.find("result");
    if (resultLocal && resultLocal->isString() && !resultLocal->asString().empty())
        locals_.set(resultLocal->asString(), result);
    lastStepResult_ = std::move(result);
    blocked_        = false;
    waitingCommand_ = false;
    pendingCommandRequestId_.clear();

    std::string error;
    if (auto entered = enter(node->next); !entered.ok()) {
        error = failureText_;
    } else if (auto ran = runUntilBlockedImpl(); !ran.ok()) {
        error = failureText_;
    } else {
        emit(EventKind::Resumed, node, "command");
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    restoreExecutionState(std::move(before));
    return fail(eve::DiagnosticCode::Failed, error.empty() ? "sequence: command resume failed" : std::move(error),
                "command");
}

eve::Result<void> SequenceRuntime::captureState(eve::Value& out) const {
    const auto writeLoopStack = [](const std::vector<LoopFrame>& frames) {
        eve::Value::Array stack;
        stack.reserve(frames.size());
        for (const auto& frame : frames) {
            eve::Value item = eve::Value::Object{};
            item.set("loopNodeId", eve::Value::string(frame.loopNodeId));
            item.set("afterNodeId", eve::Value::string(frame.afterNodeId));
            item.set("mode", eve::Value::string(frame.mode));
            item.set("remaining", eve::Value::integer(frame.remaining));
            item.set("entered", eve::Value::boolean(frame.entered));
            stack.push_back(std::move(item));
        }
        return eve::Value(std::move(stack));
    };
    const auto writeParallelStack = [](const std::vector<ParallelFrame>& frames) {
        eve::Value::Array stack;
        stack.reserve(frames.size());
        for (const auto& frame : frames) {
            eve::Value item = eve::Value::Object{};
            item.set("parallelNodeId", eve::Value::string(frame.parallelNodeId));
            item.set("joinNext", eve::Value::string(frame.joinNext));
            item.set("joinNodeId", eve::Value::string(frame.joinNodeId));
            item.set("activeLane", eve::Value::integer(static_cast<std::int64_t>(frame.activeLane)));
            item.set("hostBlocked", eve::Value::boolean(frame.hostBlocked));
            eve::Value::Array lanes;
            lanes.reserve(frame.lanes.size());
            for (const auto& lane : frame.lanes) {
                eve::Value laneValue = eve::Value::Object{};
                laneValue.set("nodeId", eve::Value::string(lane.nodeId));
                laneValue.set("done", eve::Value::boolean(lane.done));
                laneValue.set("blocked", eve::Value::boolean(lane.blocked));
                laneValue.set("waitingStep", eve::Value::boolean(lane.waitingStep));
                laneValue.set("lastStepResult", lane.lastStepResult);
                lanes.push_back(std::move(laneValue));
            }
            item.set("lanes", eve::Value(std::move(lanes)));
            stack.push_back(std::move(item));
        }
        return eve::Value(std::move(stack));
    };

    out = eve::Value::Object{};
    out.set("schema", eve::Value::string(std::string(SaveSchema)));
    out.set("schemaVersion", eve::Value::integer(SaveVersion));
    out.set("active", eve::Value::boolean(asset_ != nullptr));
    if (!asset_) return eve::Result<void>::success();
    out.set("current", captureFrame(asset_, nodeId_, bindings_, locals_));
    out.set("blocked", eve::Value::boolean(blocked_));
    out.set("waitingStep", eve::Value::boolean(waitingStep_));
    out.set("waitingCommand", eve::Value::boolean(waitingCommand_));
    out.set("pendingCommandRequestId", eve::Value::string(pendingCommandRequestId_));
    out.set("commandSequence", eve::Value::integer(static_cast<std::int64_t>(commandSequence_)));
    out.set("loopStack", writeLoopStack(loopStack_));
    out.set("parallelStack", writeParallelStack(parallelStack_));
    eve::Value::Array stack;
    stack.reserve(callStack_.size());
    for (const auto& frame : callStack_) {
        eve::Value item = captureFrame(frame.asset, frame.returnNode, frame.bindings, frame.locals);
        item.set("loopStack", writeLoopStack(frame.loopStack));
        item.set("parallelStack", writeParallelStack(frame.parallelStack));
        stack.push_back(std::move(item));
    }
    out.set("stack", eve::Value(std::move(stack)));
    return eve::Result<void>::success();
}

eve::Result<void> SequenceRuntime::restoreState(const eve::Value& in) {
    if (!in.isObject())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: runtime state must be an object", "state");
    const eve::Value* schema        = in.find("schema");
    const eve::Value* schemaVersion = in.find("schemaVersion");
    if (!schema || !schema->isString() || schema->asString() != SaveSchema || !schemaVersion ||
        !schemaVersion->isInt64() || schemaVersion->asInt() < MinSaveVersion ||
        schemaVersion->asInt() > SaveVersion)
        return fail(eve::DiagnosticCode::UnknownVersion, "sequence: unsupported runtime save schema or version",
                    "state.schemaVersion");
    const std::int64_t restoredVersion = schemaVersion->asInt();
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

    const auto readLoopStack = [&](const eve::Value* value, std::vector<LoopFrame>& outFrames) -> eve::Result<void> {
        outFrames.clear();
        if (!value) return eve::Result<void>::success();
        if (!value->isArray())
            return fail(eve::DiagnosticCode::InvalidArgument, "sequence: loopStack is malformed", "state.loopStack");
        outFrames.reserve(value->arraySize());
        for (std::size_t index = 0; index < value->arraySize(); ++index) {
            const eve::Value& item = value->at(index);
            LoopFrame         frame;
            if (!item.isObject() || !readString(item, "loopNodeId", frame.loopNodeId) ||
                !readString(item, "afterNodeId", frame.afterNodeId) || !readString(item, "mode", frame.mode))
                return fail(eve::DiagnosticCode::InvalidArgument, "sequence: loop frame is malformed",
                            "state.loopStack");
            const eve::Value* remaining = item.find("remaining");
            const eve::Value* entered   = item.find("entered");
            if (!remaining || !remaining->isInt64() || !entered || !entered->isBool())
                return fail(eve::DiagnosticCode::InvalidArgument, "sequence: loop frame counters are malformed",
                            "state.loopStack");
            frame.remaining = static_cast<int>(remaining->asInt());
            frame.entered   = entered->asBool();
            outFrames.push_back(std::move(frame));
        }
        return eve::Result<void>::success();
    };
    const auto readParallelStack = [&](const eve::Value* value,
                                       std::vector<ParallelFrame>& outFrames) -> eve::Result<void> {
        outFrames.clear();
        if (!value) return eve::Result<void>::success();
        if (!value->isArray())
            return fail(eve::DiagnosticCode::InvalidArgument, "sequence: parallelStack is malformed",
                        "state.parallelStack");
        outFrames.reserve(value->arraySize());
        for (std::size_t index = 0; index < value->arraySize(); ++index) {
            const eve::Value& item = value->at(index);
            ParallelFrame     frame;
            if (!item.isObject() || !readString(item, "parallelNodeId", frame.parallelNodeId) ||
                !readString(item, "joinNext", frame.joinNext) || !readString(item, "joinNodeId", frame.joinNodeId))
                return fail(eve::DiagnosticCode::InvalidArgument, "sequence: parallel frame is malformed",
                            "state.parallelStack");
            const eve::Value* activeLane  = item.find("activeLane");
            const eve::Value* hostBlocked = item.find("hostBlocked");
            const eve::Value* lanes       = item.find("lanes");
            if (!activeLane || !activeLane->isInt64() || !hostBlocked || !hostBlocked->isBool() || !lanes ||
                !lanes->isArray())
                return fail(eve::DiagnosticCode::InvalidArgument, "sequence: parallel frame lanes are malformed",
                            "state.parallelStack");
            frame.activeLane  = static_cast<std::size_t>(activeLane->asInt());
            frame.hostBlocked = hostBlocked->asBool();
            for (std::size_t laneIndex = 0; laneIndex < lanes->arraySize(); ++laneIndex) {
                const eve::Value& laneValue = lanes->at(laneIndex);
                ParallelLane      lane;
                if (!laneValue.isObject() || !readString(laneValue, "nodeId", lane.nodeId))
                    return fail(eve::DiagnosticCode::InvalidArgument, "sequence: parallel lane is malformed",
                                "state.parallelStack");
                const eve::Value* done        = laneValue.find("done");
                const eve::Value* blocked     = laneValue.find("blocked");
                const eve::Value* waitingStep = laneValue.find("waitingStep");
                const eve::Value* lastResult  = laneValue.find("lastStepResult");
                if (!done || !done->isBool() || !blocked || !blocked->isBool() || !waitingStep ||
                    !waitingStep->isBool())
                    return fail(eve::DiagnosticCode::InvalidArgument, "sequence: parallel lane flags are malformed",
                                "state.parallelStack");
                lane.done        = done->asBool();
                lane.blocked     = blocked->asBool();
                lane.waitingStep = waitingStep->asBool();
                if (lastResult) lane.lastStepResult = *lastResult;
                frame.lanes.push_back(std::move(lane));
            }
            outFrames.push_back(std::move(frame));
        }
        return eve::Result<void>::success();
    };

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
        Frame frame{savedAsset, returnNode, *frameBindings, *frameLocals, {}, {}};
        if (restoredVersion >= 3) {
            if (auto loops = readLoopStack(saved.find("loopStack"), frame.loopStack); !loops.ok()) return loops;
            if (auto parallels = readParallelStack(saved.find("parallelStack"), frame.parallelStack); !parallels.ok())
                return parallels;
        }
        restoredStack.push_back(std::move(frame));
    }
    std::vector<LoopFrame>     restoredLoops;
    std::vector<ParallelFrame> restoredParallels;
    if (restoredVersion >= 3) {
        if (auto loops = readLoopStack(in.find("loopStack"), restoredLoops); !loops.ok()) return loops;
        if (auto parallels = readParallelStack(in.find("parallelStack"), restoredParallels); !parallels.ok())
            return parallels;
    }
    const eve::Value* blocked = in.find("blocked");
    if (!blocked || !blocked->isBool())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: runtime state is missing 'blocked'", "state");
    const eve::Value* waitingStep = in.find("waitingStep");
    if (waitingStep && !waitingStep->isBool())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: waitingStep is malformed", "state");
    const eve::Value* waitingCommand = in.find("waitingCommand");
    if (waitingCommand && !waitingCommand->isBool())
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: waitingCommand is malformed", "state");
    const eve::Value* pendingRequestId = in.find("pendingCommandRequestId");
    const eve::Value* commandSequence  = in.find("commandSequence");
    if (!pendingRequestId || !pendingRequestId->isString() || !commandSequence || !commandSequence->isInt64() ||
        commandSequence->asInt() < 1)
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: pending command state is malformed", "state");
    const bool restoredWaitingCommand = waitingCommand && waitingCommand->asBool();
    if (restoredWaitingCommand != !pendingRequestId->asString().empty())
        return fail(eve::DiagnosticCode::InvalidArgument,
                    "sequence: pending command identity does not match waiting state", "state");
    if (!restoredAsset->findNode(nodeId))
        return fail(eve::DiagnosticCode::NotFound, "sequence: saved node '" + nodeId + "' is missing", "state.node");
    const SequenceNode* restoredNode = restoredAsset->findNode(nodeId);
    if (restoredWaitingCommand && (!restoredNode || restoredNode->type != "command"))
        return fail(eve::DiagnosticCode::InvalidArgument, "sequence: pending command cursor is not a command node",
                    "state.node");

    asset_       = restoredAsset;
    nodeId_      = std::move(nodeId);
    bindings_    = *savedBindings;
    locals_      = *savedLocals;
    blocked_     = blocked->asBool();
    waitingStep_ = waitingStep && waitingStep->asBool();
    waitingCommand_          = restoredWaitingCommand;
    pendingCommandRequestId_ = pendingRequestId->asString();
    commandSequence_         = static_cast<std::uint64_t>(commandSequence->asInt());
    callStack_     = std::move(restoredStack);
    loopStack_     = std::move(restoredLoops);
    parallelStack_ = std::move(restoredParallels);
    failureText_.clear();
    lastCommandRequest_.reset();
    if (waitingCommand_) {
        SequenceCommandRequest request;
        request.requestId      = pendingCommandRequestId_;
        const eve::Value* name = restoredNode->payload.find("name");
        if (!name) name = restoredNode->payload.find("target");
        request.name = name && name->isString() ? name->asString() : std::string{};
        if (const eve::Value* arguments = restoredNode->payload.find("arguments"); arguments && arguments->isObject())
            request.arguments = *arguments;
        request.bindings    = bindings_;
        request.locals      = locals_;
        request.payload     = restoredNode->payload;
        lastCommandRequest_ = std::move(request);
        emit(EventKind::Blocked, restoredNode, "command");
    }
    return eve::Result<void>::success();
}

}  // namespace eve::dnut
