#pragma once

#include "editor/EditConstraint.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorDocumentService.h"
#include "editor/EditorTool.h"
#include "editor/EditorTransactions.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::editor {

class AutosaveService;
class EditorTargetCoordinator;

/**
 * @brief Hosts interchangeable editor tools and routes their lifecycle/input.
 *
 * Tools are non-owning: the caller must keep a registered tool alive until it
 * is removed or the session is destroyed. All methods are owner-thread only.
 * Tool callbacks may re-enter the session; lifecycle state is detached before
 * cancellation or deactivation callbacks are invoked.
 */
class EditorSession {
public:
    EditorSession();
    ~EditorSession() noexcept;

    EditorSession(const EditorSession&)            = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    /** @brief Register a tool. Returns false for null or duplicate id/pointer. */
    bool addTool(IEditorTool* tool);
    /** @brief Remove a registered tool by id, deactivating it when necessary. */
    bool removeTool(const std::string& id);
    /** @brief Remove all tools and release any pointer capture. */
    void clearTools();

    int          getToolCount() const;
    IEditorTool* getTool(int index) const;
    IEditorTool* findTool(const std::string& id) const;

    /** @brief Activate a registered tool by id. Empty id deactivates all tools. */
    bool         activateTool(const std::string& id);
    IEditorTool* activeTool() const { return activeTool_; }
    std::string  activeToolId() const;

    /** @brief Route input to the active tool and apply pointer-capture response. */
    ToolResponse dispatchPointer(const EditorPointerEvent& event);
    /** @brief Route keyboard input to the active tool. */
    ToolResponse dispatchKey(const EditorKeyEvent& event);
    /** @brief Update the active tool. */
    void update(float dt);
    /** @brief Ask the active tool to emit viewport feedback. */
    void drawOverlay(IEditorOverlay& overlay);
    /** @brief Ask the active tool to expose its configurable properties. */
    void inspect(IEditorInspector& inspector);
    /** @brief Cancel its gesture and clear pointer capture. */
    void cancelActiveTool();

    bool                 hasPointerCapture() const { return capturedPointerId_ >= 0; }
    int                  capturedPointerId() const { return capturedPointerId_; }
    EditorContext&       context() { return context_; }
    const EditorContext& context() const { return context_; }
    /**
     * @brief Bind a caller-managed editable target available to every tool callback.
     * @param target Borrowed target; the caller must clear the binding before destroying it.
     * @thread Owner-thread only. Tool callbacks are never invoked by this function.
     */
    void bindTarget(IEditableTarget& target);
    /** @brief Compatibility-only nullable pointer facade over bindTarget() and clearTarget(). */
    void bindTarget(IEditableTarget* target);
    /** @brief Clear the active target and invalidate retained plans derived from it. */
    void clearTarget();
    /** @brief Return the live borrowed target, or null after an explicit or tracked unbind. */
    IEditableTarget* target() const;
    /** @brief Return the identity of the current binding without dereferencing the target. */
    const TargetId&         boundTargetId() const noexcept { return boundTargetId_; }
    EditorTransactions&     transactions() { return transactions_; }
    EditConstraintPipeline& constraints() { return constraints_; }
    /** @brief Validate then execute a command in the active transaction. */
    bool execute(std::unique_ptr<IEditCommand> command);
    /** @brief Validate and execute a command while preserving constraint and transaction diagnostics. */
    [[nodiscard]] EditorResult<void> executeChecked(std::unique_ptr<IEditCommand> command);

    /** @brief Bind a non-owning V2 command service that must outlive this binding. */
    void bindCommandService(EditorCommandService& service) { commandService_ = &service; }
    /** @brief Clear the V2 command service binding. */
    void clearCommandService() { commandService_ = nullptr; }
    /** @brief Compatibility-only nullable pointer facade over bindCommandService() and clearCommandService(). */
    void setCommandService(EditorCommandService* service) { commandService_ = service; }
    /** @brief Return the bound V2 command service, or nullptr. */
    EditorCommandService* commandService() const { return commandService_; }
    /** @brief Set the host capability boundary used for command discovery and execution. */
    void setHostProfile(HostProfile profile) { hostProfile_ = std::move(profile); }
    /** @brief Return the current host capability boundary. */
    const HostProfile& hostProfile() const { return hostProfile_; }
    /** @brief Assign the stable identity included in asynchronous context snapshots. */
    void setSessionId(SessionId id) { sessionId_ = std::move(id); }
    /** @brief Return the stable session identity. */
    const SessionId& sessionId() const { return sessionId_; }
    /** @brief Capture target revision and host identity for delayed command work. */
    EditorContextSnapshot contextSnapshot() const;
    /**
     * @brief Execute a registered V2 command in this session.
     * @param id Stable registered command
     * identity.
     * @param payload Structured command input.
     * @param source User/script/automation source used
     * by execution policy.
     * @return Structured status, value and diagnostics.
     */
    EditorResult<EditorValue> executeCommand(const CommandId& id, const EditorValue& payload = {},
                                             CommandSource source = CommandSource::Api);
    /** @brief Plan a registered command without applying target mutations. */
    EditorResult<CommandPlan> planCommand(const CommandId& id, const EditorValue& payload = {},
                                          CommandSource           source           = CommandSource::Api,
                                          std::optional<Revision> expectedRevision = std::nullopt) const;
    /** @brief Execute a previously returned plan through its registered executor. */
    EditorResult<TransactionReceipt> executePlan(const CommandPlan& plan, const EditorValue& payload = {},
                                                 CommandSource source = CommandSource::Api);
    /** @brief Execute a legacy-compatible command and return a V2 transaction receipt. */
    EditorResult<TransactionReceipt> executeCommandReceipt(const CommandId& id, const EditorValue& payload = {},
                                                           CommandSource source = CommandSource::Api);
    /** @brief Return commands visible through this session's host profile. */
    std::vector<CommandDescriptor> availableCommands() const;
    /**
     * @brief Plan a command and retain its immutable payload under the returned plan id.
     * @return Stable
     * plan id suitable for script and UI frontends.
     */
    EditorResult<PlanId> retainPlan(const CommandId& id, const EditorValue& payload = {},
                                    CommandSource           source           = CommandSource::Api,
                                    std::optional<Revision> expectedRevision = std::nullopt);
    /** @brief Execute a retained plan. Successful plans are consumed exactly once. */
    EditorResult<TransactionReceipt> executeRetainedPlan(const PlanId& id, CommandSource source = CommandSource::Api);
    /** @brief Discard one retained plan without executing it. */
    EditorResult<void> cancelRetainedPlan(const PlanId& id);
    /** @brief Discard all retained plans, for example after replacing a document or target. */
    void clearRetainedPlans();

    /**
     * @brief Bind non-owning persistence services used by the active document.
     * @param documents Formal document coordinator; must outlive the session.
     * @param autosave Optional draft service; must outlive the session.
     */
    void setDocumentServices(DocumentService* documents, AutosaveService* autosave = nullptr);
    /** @brief Bind document services; documents must outlive this binding. */
    void bindDocumentServices(DocumentService& documents, AutosaveService* autosave = nullptr);
    /** @brief Clear document service bindings and unbind the active document. */
    void clearDocumentServices();
    /**
     * @brief Select an already-open document for save, autosave and conflict polling.
     * @param document Stable identity already opened by DocumentService.
     * @return Bound document snapshot or a structured lookup failure.
     */
    EditorResult<DocumentSnapshot> bindDocument(const DocumentId& document);
    /** @brief Stop coordinating the active document without closing it. */
    void unbindDocument();
    /** @brief Return the active document identity, or an empty id. */
    const DocumentId& activeDocument() const { return activeDocument_; }
    /**
     * @brief Replace active working content using optional optimistic edit revision.
     * @param content New serializable working content.
     * @param expectedRevision Optional edit revision used to reject stale writers.
     * @return Updated document snapshot or a structured conflict.
     */
    EditorResult<DocumentSnapshot> editDocument(EditorValue             content,
                                                std::optional<Revision> expectedRevision = std::nullopt);
    /** @brief Persist the current active revision through the document CAS store. */
    EditorResult<DocumentSnapshot> saveDocument();
    /** @brief Write the active dirty revision to its separate autosave draft. */
    EditorResult<StoredDocument> autosaveDocument();
    /** @brief Detect external disk changes, adopting only when the document is clean. */
    EditorResult<DocumentSnapshot> pollDocumentChanges();
    /** @brief Configure draft cadence. @param seconds Interval in seconds; zero disables automatic drafts. */
    void setAutosaveInterval(float seconds);
    /** @brief Configure external-store polling cadence. @param seconds Interval in seconds; zero disables polling. */
    void setExternalPollInterval(float seconds);
    /** @brief Last edit revision successfully written to the draft store. */
    Revision lastAutosavedRevision() const { return lastAutosavedRevision_; }

private:
    friend class EditorTargetCoordinator;
    void bindTrackedTarget(IEditableTarget& target, std::weak_ptr<void> lifetime, EditorTargetCoordinator& coordinator,
                           std::uint64_t generation);
    void invalidateTrackedTarget(EditorTargetCoordinator& coordinator, const TargetId& target,
                                 std::uint64_t generation);
    void coordinatorDestroyed(EditorTargetCoordinator& coordinator) noexcept;
    void releaseTargetBinding();
    void deactivateCurrent();

    std::vector<IEditorTool*> tools_;
    IEditorTool*              activeTool_        = nullptr;
    int                       capturedPointerId_ = -1;
    EditorContext             context_;
    IEditableTarget*          target_ = nullptr;
    std::weak_ptr<void>       targetLifetime_;
    bool                      targetLifetimeTracked_ = false;
    TargetId                  boundTargetId_;
    std::uint64_t             targetGeneration_           = 0;
    std::uint64_t             nextDirectTargetGeneration_ = 1;
    EditorTargetCoordinator*  targetCoordinator_          = nullptr;
    EditorTransactions        transactions_;
    EditConstraintPipeline    constraints_;
    EditorCommandService*     commandService_ = nullptr;
    HostProfile               hostProfile_    = HostProfile::developer();
    SessionId                 sessionId_;
    std::uint64_t             receiptSequence_ = 0;
    std::uint64_t             toolGeneration_  = 0;
    struct RetainedPlan {
        CommandPlan plan;
        EditorValue payload;
    };
    std::vector<RetainedPlan> retainedPlans_;
    DocumentService*          documentService_ = nullptr;
    AutosaveService*          autosaveService_ = nullptr;
    DocumentId                activeDocument_;
    float                     autosaveInterval_      = 30.f;
    float                     autosaveElapsed_       = 0.f;
    float                     externalPollInterval_  = 1.f;
    float                     externalPollElapsed_   = 0.f;
    Revision                  lastAutosavedRevision_ = 0;
};

}  // namespace eve::editor
