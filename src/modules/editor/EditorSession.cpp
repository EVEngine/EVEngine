#include "editor/EditorSession.h"

#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorPresentation.h"
#include "editor/EditorResultProjection.h"
#include "editor/EditorTargetCoordinator.h"

#include <algorithm>
#include <exception>

namespace eve::editor {

EditorSession::EditorSession() : context_(this) {}

IEditableTarget* EditorContext::target() const { return session_ ? session_->target() : nullptr; }

EditorTransactions& EditorContext::transactions() const { return session_->transactions(); }

bool EditorContext::execute(std::unique_ptr<IEditCommand> command) const {
    return session_ && session_->execute(std::move(command));
}

EditorResult<void> EditorContext::executeChecked(std::unique_ptr<IEditCommand> command) const {
    if (!session_)
        return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.context.missing-session"),
                                         "Editor context has no dispatching session");
    return session_->executeChecked(std::move(command));
}

bool EditorSession::execute(std::unique_ptr<IEditCommand> command) {
    return executeChecked(std::move(command)).ok();
}

EditorResult<void> EditorSession::executeChecked(std::unique_ptr<IEditCommand> command) {
    if (!command)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.command.null"),
                                         "Editor command must not be null");
    EditorResult<void> constrained = constraints_.evaluateChecked(context_, *command);
    if (!constrained.ok()) return constrained;
    auto appended = transactions_.append(std::move(command));
    if (!appended.ok()) return projectCommonResult(std::move(appended));
    return EditorResult<void>::success(eve::Status(EditorStatus::Applied, constrained.diagnostics()));
}

void EditorSession::bindTarget(IEditableTarget& target) {
    releaseTargetBinding();
    target_                = &target;
    boundTargetId_         = TargetId(target.targetId());
    targetGeneration_      = nextDirectTargetGeneration_++;
    targetLifetime_        = {};
    targetLifetimeTracked_ = false;
}

void EditorSession::bindTarget(IEditableTarget* target) {
    if (target)
        bindTarget(*target);
    else
        clearTarget();
}

void EditorSession::bindTrackedTarget(IEditableTarget& target, std::weak_ptr<void> lifetime,
                                      EditorTargetCoordinator& coordinator, std::uint64_t generation) {
    releaseTargetBinding();
    target_                = &target;
    boundTargetId_         = TargetId(target.targetId());
    targetLifetime_        = std::move(lifetime);
    targetLifetimeTracked_ = true;
    targetGeneration_      = generation;
    targetCoordinator_     = &coordinator;
}

void EditorSession::releaseTargetBinding() {
    transactions_.clear();
    clearRetainedPlans();
    if (targetCoordinator_) targetCoordinator_->detach(*this);
    target_                = nullptr;
    boundTargetId_         = {};
    targetLifetime_        = {};
    targetLifetimeTracked_ = false;
    targetGeneration_      = 0;
    targetCoordinator_     = nullptr;
}

void EditorSession::clearTarget() { releaseTargetBinding(); }

void EditorSession::invalidateTrackedTarget(EditorTargetCoordinator& coordinator, const TargetId& target,
                                            std::uint64_t generation) {
    if (targetCoordinator_ != &coordinator || boundTargetId_ != target || targetGeneration_ != generation) return;
    releaseTargetBinding();
}

void EditorSession::coordinatorDestroyed(EditorTargetCoordinator& coordinator) noexcept {
    if (targetCoordinator_ != &coordinator) return;
    try {
        transactions_.clear();
    } catch (...) {
    }
    clearRetainedPlans();
    target_                = nullptr;
    boundTargetId_         = {};
    targetLifetime_        = {};
    targetLifetimeTracked_ = false;
    targetGeneration_      = 0;
    targetCoordinator_     = nullptr;
}

IEditableTarget* EditorSession::target() const {
    if (!target_) return nullptr;
    if (targetLifetimeTracked_ && targetLifetime_.expired()) return nullptr;
    return target_;
}

EditorResult<EditorValue> EditorSession::executeCommand(const CommandId& id, const EditorValue& payload,
                                                        CommandSource source) {
    if (!commandService_)
        return eve::editing::failed<EditorValue>(EditorStatus::Failed, RuleId("editor.command.missing-service"),
                                                "Editor session has no command service");

    const CommandDescriptor* descriptor = commandService_->find(id);
    const bool ownsTransaction          = descriptor && descriptor->createsTransaction && !transactions_.isActive();
    if (ownsTransaction) {
        auto begun = transactions_.beginTransaction(descriptor->displayName.empty() ? id.value() : descriptor->displayName);
        if (!begun.ok()) return projectCommonFailure<EditorValue>(begun.status());
    }

    CommandContext commandContext;
    commandContext.session = this;
    commandContext.profile = &hostProfile_;
    commandContext.source  = source;
    if (IEditableTarget* currentTarget = target()) {
        commandContext.target         = TargetId(currentTarget->targetId());
        commandContext.targetRevision = currentTarget->revision();
    }

    EditorResult<EditorValue> result = commandService_->execute(id, commandContext, payload);
    if (ownsTransaction) {
        if (result.ok()) {
            auto committed = transactions_.commitTransaction();
            if (!committed.ok()) {
                auto discarded = transactions_.rollbackTransaction();
                if (!discarded.ok()) {
                    std::vector<EditorDiagnostic> diagnostics = committed.diagnostics();
                    appendProjectedDiagnostics(diagnostics, discarded.status());
                    return EditorResult<EditorValue>::failure(
                        eve::Status(EditorStatus::Failed, std::move(diagnostics)));
                }
                return projectCommonFailure<EditorValue>(committed.status());
            }
        } else {
            auto discarded = transactions_.rollbackTransaction();
            if (!discarded.ok()) {
                std::vector<EditorDiagnostic> diagnostics = result.diagnostics();
                appendProjectedDiagnostics(diagnostics, discarded.status());
                return EditorResult<EditorValue>::failure(
                    eve::Status(EditorStatus::Failed, std::move(diagnostics)));
            }
        }
    }
    return result;
}

EditorContextSnapshot EditorSession::contextSnapshot() const {
    EditorContextSnapshot snapshot;
    snapshot.session = sessionId_;
    snapshot.host    = hostProfile_.kind();
    if (IEditableTarget* currentTarget = target()) {
        snapshot.target         = TargetId(currentTarget->targetId());
        snapshot.targetRevision = currentTarget->revision();
        snapshot.targetGeneration = targetGeneration_;
    }
    return snapshot;
}

EditorResult<CommandPlan> EditorSession::planCommand(const CommandId& id, const EditorValue& payload,
                                                     CommandSource           source,
                                                     std::optional<Revision> expectedRevision) const {
    if (!commandService_)
        return eve::editing::failed<CommandPlan>(EditorStatus::Failed, RuleId("editor.command.missing-service"),
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
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Failed, RuleId("editor.command.missing-service"),
                                                       "Editor session has no command service");
    CommandRequest request;
    request.id               = plan.command;
    request.payload          = payload;
    request.source           = source;
    request.context          = contextSnapshot();
    if (request.context.target != plan.target || request.context.targetRevision != plan.baseRevision ||
        request.context.targetGeneration != plan.targetGeneration)
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Conflict, RuleId("editor.command.stale-plan"),
                                                        "The live target binding no longer matches this plan");
    request.expectedRevision = plan.baseRevision;
    return commandService_->executePlan(request, plan, hostProfile_);
}

EditorResult<TransactionReceipt> EditorSession::executeCommandReceipt(const CommandId& id, const EditorValue& payload,
                                                                      CommandSource source) {
    TransactionReceipt receipt;
    receipt.id             = TransactionId((sessionId_.empty() ? std::string("editor.session") : sessionId_.value()) +
                                           ".transaction." + std::to_string(++receiptSequence_));
    IEditableTarget* currentTarget = target();
    receipt.beforeRevision = currentTarget ? currentTarget->revision() : 0;
    EditorResult<EditorValue> command = executeCommand(id, payload, source);
    currentTarget                     = target();
    receipt.afterRevision             = currentTarget ? currentTarget->revision() : receipt.beforeRevision;
    receipt.diagnostics               = command.diagnostics();
    receipt.state = command.ok() ? TransactionState::Committed
                                       : (command.code() == EditorStatus::Conflict ? TransactionState::Conflicted
                                                                                   : TransactionState::Rejected);
    if (!command.ok()) return EditorResult<TransactionReceipt>::failure(command.status());
    return EditorResult<TransactionReceipt>::success(
        std::move(receipt), eve::Status(command.code(), command.diagnostics()));
}

std::vector<CommandDescriptor> EditorSession::availableCommands() const {
    return commandService_ ? commandService_->commands(hostProfile_) : std::vector<CommandDescriptor>{};
}

EditorResult<PlanId> EditorSession::retainPlan(const CommandId& id, const EditorValue& payload, CommandSource source,
                                               std::optional<Revision> expectedRevision) {
    EditorResult<CommandPlan> planned = planCommand(id, payload, source, expectedRevision);
    if (!planned.ok()) return EditorResult<PlanId>::failure(planned.status());
    const PlanId planId = planned.value().id;
    retainedPlans_.erase(std::remove_if(retainedPlans_.begin(), retainedPlans_.end(),
                                        [&](const RetainedPlan& retained) { return retained.plan.id == planId; }),
                         retainedPlans_.end());
    retainedPlans_.push_back({std::move(planned.value()), payload});
    return eve::editing::applied<PlanId>(planId);
}

EditorResult<TransactionReceipt> EditorSession::executeRetainedPlan(const PlanId& id, CommandSource source) {
    const auto found = std::find_if(retainedPlans_.begin(), retainedPlans_.end(),
                                    [&](const RetainedPlan& retained) { return retained.plan.id == id; });
    if (found == retainedPlans_.end())
        return eve::editing::failed<TransactionReceipt>(EditorStatus::NotFound, RuleId("editor.command.plan-not-found"),
                                                       "Retained command plan was not found");
    EditorResult<TransactionReceipt> result = executePlan(found->plan, found->payload, source);
    if (result.ok()) retainedPlans_.erase(found);
    return result;
}

EditorResult<void> EditorSession::cancelRetainedPlan(const PlanId& id) {
    const auto found = std::find_if(retainedPlans_.begin(), retainedPlans_.end(),
                                    [&](const RetainedPlan& retained) { return retained.plan.id == id; });
    if (found == retainedPlans_.end())
        return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("editor.command.plan-not-found"),
                                         "Retained command plan was not found");
    retainedPlans_.erase(found);
    return eve::editing::applied<void>();
}

void EditorSession::clearRetainedPlans() { retainedPlans_.clear(); }

void EditorSession::setDocumentServices(DocumentService* documents, AutosaveService* autosave) {
    if (documents)
        bindDocumentServices(*documents, autosave);
    else
        clearDocumentServices();
}

void EditorSession::bindDocumentServices(DocumentService& documents, AutosaveService* autosave) {
    documentService_ = &documents;
    autosaveService_ = autosave;
    unbindDocument();
}

void EditorSession::clearDocumentServices() {
    unbindDocument();
    documentService_ = nullptr;
    autosaveService_ = nullptr;
}

EditorResult<DocumentSnapshot> EditorSession::bindDocument(const DocumentId& document) {
    if (!documentService_)
        return eve::editing::failed<DocumentSnapshot>(EditorStatus::Failed,
                                                      RuleId("editor.session.missing-document-service"),
                                                      "Editor session has no document service");
    EditorResult<DocumentSnapshot> snapshot = documentService_->snapshot(document);
    if (!snapshot.ok() || !snapshot.ok()) return snapshot;
    activeDocument_          = document;
    autosaveElapsed_         = 0.f;
    externalPollElapsed_     = 0.f;
    lastAutosavedRevision_   = snapshot.value().revision.saved;
    return snapshot;
}

void EditorSession::unbindDocument() {
    activeDocument_        = DocumentId();
    autosaveElapsed_       = 0.f;
    externalPollElapsed_   = 0.f;
    lastAutosavedRevision_ = 0;
}

EditorResult<DocumentSnapshot> EditorSession::editDocument(EditorValue content,
                                                           std::optional<Revision> expectedRevision) {
    if (!documentService_ || activeDocument_.empty())
        return eve::editing::failed<DocumentSnapshot>(EditorStatus::Failed,
                                                      RuleId("editor.session.no-active-document"),
                                                      "Editor session has no active document");
    return documentService_->edit(activeDocument_, std::move(content), expectedRevision);
}

EditorResult<DocumentSnapshot> EditorSession::saveDocument() {
    if (!documentService_ || activeDocument_.empty())
        return eve::editing::failed<DocumentSnapshot>(EditorStatus::Failed,
                                                      RuleId("editor.session.no-active-document"),
                                                      "Editor session has no active document");
    EditorResult<SaveTicket> ticket = documentService_->requestSave(activeDocument_);
    if (!ticket.ok()) return EditorResult<DocumentSnapshot>::failure(ticket.status());
    EditorResult<DocumentSnapshot> result = documentService_->executeSave(ticket.value());
    if (result.ok()) {
        autosaveElapsed_ = 0.f;
        if (!result.value().dirty()) lastAutosavedRevision_ = result.value().revision.edit;
    }
    return result;
}

EditorResult<StoredDocument> EditorSession::autosaveDocument() {
    if (!documentService_ || !autosaveService_ || activeDocument_.empty())
        return eve::editing::failed<StoredDocument>(EditorStatus::Failed,
                                                   RuleId("editor.session.missing-autosave-service"),
                                                   "Editor session has no active autosave service");
    EditorResult<DocumentSnapshot> snapshot = documentService_->snapshot(activeDocument_);
    EditorResult<EditorValue>      content  = documentService_->content(activeDocument_);
    if (!snapshot.ok() || !snapshot.ok() || !content.ok() || !content.ok())
        return eve::editing::failed<StoredDocument>(EditorStatus::Failed,
                                                   RuleId("editor.session.autosave-snapshot-failed"),
                                                   "Active document could not be captured for autosave");
    EditorResult<StoredDocument> result = autosaveService_->writeDraft(snapshot.value(), content.value());
    if (result.ok()) {
        lastAutosavedRevision_ = snapshot.value().revision.edit;
        autosaveElapsed_       = 0.f;
    }
    return result;
}

EditorResult<DocumentSnapshot> EditorSession::pollDocumentChanges() {
    if (!documentService_ || activeDocument_.empty())
        return eve::editing::failed<DocumentSnapshot>(EditorStatus::Failed,
                                                      RuleId("editor.session.no-active-document"),
                                                      "Editor session has no active document");
    externalPollElapsed_ = 0.f;
    return documentService_->reconcileExternal(activeDocument_);
}

void EditorSession::setAutosaveInterval(float seconds) { autosaveInterval_ = std::max(0.f, seconds); }

void EditorSession::setExternalPollInterval(float seconds) {
    externalPollInterval_ = std::max(0.f, seconds);
}

EditorSession::~EditorSession() noexcept {
    try {
        deactivateCurrent();
    } catch (...) {
    }
    try {
        releaseTargetBinding();
    } catch (...) {
    }
}

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
    const bool wasActive = *it == activeTool_;
    tools_.erase(it);
    if (wasActive) deactivateCurrent();
    return true;
}

void EditorSession::clearTools() {
    tools_.clear();
    deactivateCurrent();
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
    next = findTool(id);
    if (!next) return false;
    activeTool_ = next;
    ++toolGeneration_;
    try {
        activeTool_->activate(context_);
    } catch (...) {
        if (activeTool_ == next) {
            activeTool_ = nullptr;
            capturedPointerId_ = -1;
            ++toolGeneration_;
        }
        throw;
    }
    return true;
}

std::string EditorSession::activeToolId() const { return activeTool_ ? activeTool_->descriptor().id : std::string{}; }

ToolResponse EditorSession::dispatchPointer(const EditorPointerEvent& event) {
    if (!activeTool_) return ToolResponse::ignored();
    if (capturedPointerId_ >= 0 && event.pointerId != capturedPointerId_) {
        return ToolResponse::ignored();
    }
    IEditorTool* const dispatchedTool = activeTool_;
    const std::uint64_t generation = toolGeneration_;
    ToolResponse response = dispatchedTool->pointerEvent(context_, event);
    if (activeTool_ != dispatchedTool || toolGeneration_ != generation)
        return {response.handled, false, false};
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
    if (!documentService_ || activeDocument_.empty() || dt <= 0.f) return;

    if (externalPollInterval_ > 0.f) {
        externalPollElapsed_ += dt;
        if (externalPollElapsed_ >= externalPollInterval_) (void)pollDocumentChanges();
    }

    EditorResult<DocumentSnapshot> snapshot = documentService_->snapshot(activeDocument_);
    if (!snapshot.ok() || !snapshot.value().dirty()) {
        autosaveElapsed_ = 0.f;
        return;
    }
    if (!autosaveService_ || autosaveInterval_ <= 0.f ||
        snapshot.value().revision.edit == lastAutosavedRevision_)
        return;
    autosaveElapsed_ += dt;
    if (autosaveElapsed_ >= autosaveInterval_) (void)autosaveDocument();
}

void EditorSession::drawOverlay(IEditorOverlay& overlay) {
    if (activeTool_) activeTool_->drawOverlay(context_, overlay);
}

void EditorSession::inspect(IEditorInspector& inspector) {
    if (activeTool_) activeTool_->inspect(context_, inspector);
}

void EditorSession::cancelActiveTool() {
    capturedPointerId_ = -1;
    if (activeTool_) activeTool_->cancel(context_);
}

void EditorSession::deactivateCurrent() {
    IEditorTool* current = activeTool_;
    activeTool_        = nullptr;
    capturedPointerId_ = -1;
    if (!current) return;
    ++toolGeneration_;

    std::exception_ptr failure;
    try {
        current->cancel(context_);
    } catch (...) {
        failure = std::current_exception();
    }
    try {
        current->deactivate(context_);
    } catch (...) {
        if (!failure) failure = std::current_exception();
    }
    if (failure) std::rethrow_exception(failure);
}

}  // namespace eve::editor
