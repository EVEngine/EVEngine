#include "editor/EditorSession.h"

#include "editor/EditorPresentation.h"

#include <algorithm>

namespace eve::editor {

EditorSession::EditorSession() : context_(this) {}

EditorTransactions& EditorContext::transactions() const { return session_->transactions(); }

bool EditorContext::execute(std::unique_ptr<IEditCommand> command) const {
    return session_ && session_->execute(std::move(command));
}

bool EditorSession::execute(std::unique_ptr<IEditCommand> command) {
    if (!command || !constraints_.evaluate(context_, *command)) return false;
    return transactions_.execute(std::move(command));
}

EditorResult<EditorValue> EditorSession::executeCommand(const CommandId& id, const EditorValue& payload,
                                                        CommandSource source) {
    if (!commandService_)
        return EditorResult<EditorValue>::error(EditorStatus::Failed, RuleId("editor.command.missing-service"),
                                                "Editor session has no command service");

    const CommandDescriptor* descriptor = commandService_->find(id);
    const bool ownsTransaction          = descriptor && descriptor->createsTransaction && !transactions_.isActive();
    if (ownsTransaction) transactions_.begin(descriptor->displayName.empty() ? id.value() : descriptor->displayName);

    CommandContext commandContext;
    commandContext.session = this;
    commandContext.profile = &hostProfile_;
    commandContext.source  = source;
    if (context_.target_) {
        commandContext.target         = TargetId(context_.target_->targetId());
        commandContext.targetRevision = context_.target_->revision();
    }

    EditorResult<EditorValue> result = commandService_->execute(id, commandContext, payload);
    if (ownsTransaction) {
        if (result.accepted())
            transactions_.commit();
        else
            transactions_.rollback();
    }
    return result;
}

EditorContextSnapshot EditorSession::contextSnapshot() const {
    EditorContextSnapshot snapshot;
    snapshot.session = sessionId_;
    snapshot.host    = hostProfile_.kind();
    if (context_.target_) {
        snapshot.target         = TargetId(context_.target_->targetId());
        snapshot.targetRevision = context_.target_->revision();
    }
    return snapshot;
}

EditorResult<CommandPlan> EditorSession::planCommand(const CommandId& id, const EditorValue& payload,
                                                     CommandSource           source,
                                                     std::optional<Revision> expectedRevision) const {
    if (!commandService_)
        return EditorResult<CommandPlan>::error(EditorStatus::Failed, RuleId("editor.command.missing-service"),
                                                "Editor session has no command service");
    CommandRequest request;
    request.id               = id;
    request.payload          = payload;
    request.source           = source;
    request.context          = contextSnapshot();
    request.expectedRevision = expectedRevision;
    request.dryRun           = true;
    return commandService_->plan(request, hostProfile_);
}

EditorResult<TransactionReceipt> EditorSession::executePlan(const CommandPlan& plan, const EditorValue& payload,
                                                            CommandSource source) {
    if (!commandService_)
        return EditorResult<TransactionReceipt>::error(EditorStatus::Failed, RuleId("editor.command.missing-service"),
                                                       "Editor session has no command service");
    CommandRequest request;
    request.id                     = plan.command;
    request.payload                = payload;
    request.source                 = source;
    request.context                = contextSnapshot();
    request.context.target         = plan.target;
    request.context.targetRevision = plan.baseRevision;
    request.expectedRevision       = plan.baseRevision;
    return commandService_->executePlan(request, plan, hostProfile_);
}

EditorResult<TransactionReceipt> EditorSession::executeCommandReceipt(const CommandId& id, const EditorValue& payload,
                                                                      CommandSource source) {
    TransactionReceipt receipt;
    receipt.id             = TransactionId((sessionId_.empty() ? std::string("editor.session") : sessionId_.value()) +
                                           ".transaction." + std::to_string(++receiptSequence_));
    receipt.beforeRevision = context_.target_ ? context_.target_->revision() : 0;
    EditorResult<EditorValue> command = executeCommand(id, payload, source);
    receipt.afterRevision             = context_.target_ ? context_.target_->revision() : receipt.beforeRevision;
    receipt.diagnostics               = command.diagnostics;
    receipt.state = command.accepted() ? TransactionState::Committed
                                       : (command.status == EditorStatus::Conflict ? TransactionState::Conflicted
                                                                                   : TransactionState::Rejected);
    EditorResult<TransactionReceipt> result;
    result.status      = command.status;
    result.value       = receipt;
    result.diagnostics = std::move(command.diagnostics);
    return result;
}

std::vector<CommandDescriptor> EditorSession::availableCommands() const {
    return commandService_ ? commandService_->commands(hostProfile_) : std::vector<CommandDescriptor>{};
}

EditorResult<PlanId> EditorSession::retainPlan(const CommandId& id, const EditorValue& payload, CommandSource source,
                                               std::optional<Revision> expectedRevision) {
    EditorResult<CommandPlan> planned = planCommand(id, payload, source, expectedRevision);
    if (!planned.accepted() || !planned.value) {
        EditorResult<PlanId> result;
        result.status      = planned.status;
        result.diagnostics = std::move(planned.diagnostics);
        return result;
    }
    const PlanId planId = planned.value->id;
    retainedPlans_.erase(std::remove_if(retainedPlans_.begin(), retainedPlans_.end(),
                                        [&](const RetainedPlan& retained) { return retained.plan.id == planId; }),
                         retainedPlans_.end());
    retainedPlans_.push_back({std::move(*planned.value), payload});
    return EditorResult<PlanId>::applied(planId);
}

EditorResult<TransactionReceipt> EditorSession::executeRetainedPlan(const PlanId& id, CommandSource source) {
    const auto found = std::find_if(retainedPlans_.begin(), retainedPlans_.end(),
                                    [&](const RetainedPlan& retained) { return retained.plan.id == id; });
    if (found == retainedPlans_.end())
        return EditorResult<TransactionReceipt>::error(EditorStatus::NotFound, RuleId("editor.command.plan-not-found"),
                                                       "Retained command plan was not found");
    EditorResult<TransactionReceipt> result = executePlan(found->plan, found->payload, source);
    if (result.accepted()) retainedPlans_.erase(found);
    return result;
}

EditorResult<void> EditorSession::cancelRetainedPlan(const PlanId& id) {
    const auto found = std::find_if(retainedPlans_.begin(), retainedPlans_.end(),
                                    [&](const RetainedPlan& retained) { return retained.plan.id == id; });
    if (found == retainedPlans_.end())
        return EditorResult<void>::error(EditorStatus::NotFound, RuleId("editor.command.plan-not-found"),
                                         "Retained command plan was not found");
    retainedPlans_.erase(found);
    return EditorResult<void>::applied();
}

void EditorSession::clearRetainedPlans() { retainedPlans_.clear(); }

EditorSession::~EditorSession() { deactivateCurrent(); }

bool EditorSession::addTool(IEditorTool* tool) {
    if (!tool || tool->descriptor().id.empty()) return false;
    for (auto* registered : tools_) {
        if (registered == tool || registered->descriptor().id == tool->descriptor().id) return false;
    }
    tools_.push_back(tool);
    return true;
}

bool EditorSession::removeTool(const std::string& id) {
    auto it = std::find_if(tools_.begin(), tools_.end(),
                           [&](const IEditorTool* tool) { return tool && tool->descriptor().id == id; });
    if (it == tools_.end()) return false;
    if (*it == activeTool_) deactivateCurrent();
    tools_.erase(it);
    return true;
}

void EditorSession::clearTools() {
    deactivateCurrent();
    tools_.clear();
}

int EditorSession::getToolCount() const { return static_cast<int>(tools_.size()); }

IEditorTool* EditorSession::getTool(int index) const {
    if (index < 0 || index >= static_cast<int>(tools_.size())) return nullptr;
    return tools_[static_cast<size_t>(index)];
}

IEditorTool* EditorSession::findTool(const std::string& id) const {
    for (auto* tool : tools_) {
        if (tool && tool->descriptor().id == id) return tool;
    }
    return nullptr;
}

bool EditorSession::activateTool(const std::string& id) {
    if (id.empty()) {
        deactivateCurrent();
        return true;
    }
    IEditorTool* next = findTool(id);
    if (!next) return false;
    if (next == activeTool_) return true;
    deactivateCurrent();
    activeTool_ = next;
    activeTool_->activate(context_);
    return true;
}

std::string EditorSession::activeToolId() const { return activeTool_ ? activeTool_->descriptor().id : std::string{}; }

ToolResponse EditorSession::dispatchPointer(const EditorPointerEvent& event) {
    if (!activeTool_) return ToolResponse::ignored();
    if (capturedPointerId_ >= 0 && event.pointerId != capturedPointerId_) {
        return ToolResponse::ignored();
    }
    ToolResponse response = activeTool_->pointerEvent(context_, event);
    if (response.capturePointer && capturedPointerId_ < 0) capturedPointerId_ = event.pointerId;
    if (response.releasePointer && capturedPointerId_ == event.pointerId) capturedPointerId_ = -1;
    if (event.phase == EditorPointerEvent::Phase::Cancel && capturedPointerId_ == event.pointerId) {
        capturedPointerId_ = -1;
    }
    return response;
}

ToolResponse EditorSession::dispatchKey(const EditorKeyEvent& event) {
    return activeTool_ ? activeTool_->keyEvent(context_, event) : ToolResponse::ignored();
}

void EditorSession::update(float dt) {
    if (activeTool_) activeTool_->update(context_, dt);
}

void EditorSession::drawOverlay(IEditorOverlay& overlay) {
    if (activeTool_) activeTool_->drawOverlay(context_, overlay);
}

void EditorSession::inspect(IEditorInspector& inspector) {
    if (activeTool_) activeTool_->inspect(context_, inspector);
}

void EditorSession::cancelActiveTool() {
    if (activeTool_) activeTool_->cancel(context_);
    capturedPointerId_ = -1;
}

void EditorSession::deactivateCurrent() {
    if (activeTool_) {
        activeTool_->cancel(context_);
        activeTool_->deactivate(context_);
    }
    activeTool_        = nullptr;
    capturedPointerId_ = -1;
}

}  // namespace eve::editor
