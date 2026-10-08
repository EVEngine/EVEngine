#pragma once
#include "common/Export.h"


#include "editor/EditorTransactionConsumer.h"

namespace eve::editor {

/**
 * @brief Transaction boundary consumed by editor property and other UI models.
 *
 * Implementations are borrowed by the model and must outlive every synchronous
 * call. The boundary owns the pending transaction and history; callers must
 * inspect every structured result. Implementations are owner-thread-only and
 * must not invoke an unknown callback while coordinating a transaction.
 */
class IEditorTransactionBackend {
public:
    virtual ~IEditorTransactionBackend() = default;

    /** @brief Begin an explicit transaction without mutating its target. */
    [[nodiscard]] virtual Result<TransactionId> begin(TransactionSpec specification) = 0;
    /** @brief Append a serializable operation to the pending transaction. */
    [[nodiscard]] virtual Result<void> append(DomainOperation operation) = 0;
    /** @brief Validate the pending transaction without publishing target state. */
    [[nodiscard]] virtual Result<EditorDryRunReport> preview() = 0;
    /** @brief Publish the pending transaction and retain it for undo. */
    [[nodiscard]] virtual Result<TransactionReceipt> commit() = 0;
    /** @brief Discard pending work after reverting any provisional preview. */
    [[nodiscard]] virtual Result<void> discard() = 0;
    /** @brief Retry a failed commit while retaining its original operation list. */
    [[nodiscard]] virtual Result<TransactionReceipt> retry() = 0;
    /** @brief Compensate the latest committed transaction. */
    [[nodiscard]] virtual Result<TransactionReceipt> undo() = 0;
    /** @brief Reapply the latest compensated transaction. */
    [[nodiscard]] virtual Result<TransactionReceipt> redo() = 0;

    /** @brief Whether a transaction is currently pending. */
    [[nodiscard]] virtual bool active() const noexcept = 0;
    /** @brief Whether committed history can be compensated. */
    [[nodiscard]] virtual bool canUndo() const noexcept = 0;
    /** @brief Whether compensated history can be reapplied. */
    [[nodiscard]] virtual bool canRedo() const noexcept = 0;
};

/** @brief Local transaction coordinator backed by an injected edit authority. */
class EVENGINE_API_ORCHESTRATION LocalTransactionBackend final : public IEditorTransactionBackend {
public:
    /** @brief Bind a non-owning authority that outlives this backend. */
    explicit LocalTransactionBackend(IEditAuthority* authority = nullptr) : consumer_(authority) {}

    /** @brief Change the non-owning authority when no transaction is active. */
    [[nodiscard]] Result<void> setAuthority(IEditAuthority* authority);
    /** @brief Begin a transaction with a stable id and base revision. */
    [[nodiscard]] Result<TransactionId> begin(TransactionSpec specification) override;
    /** @brief Append an operation to the active transaction without applying it. */
    [[nodiscard]] Result<void> append(DomainOperation operation) override;
    /** @brief Preflight the active operation list without mutating target state. */
    [[nodiscard]] Result<EditorDryRunReport> preview() override;
    /** @brief Preflight and commit all active operations through the authority. */
    [[nodiscard]] Result<TransactionReceipt> commit() override;
    /** @brief Discard the active, not-yet-committed operation list. */
    [[nodiscard]] Result<void> discard() override;
    /** @brief Compatibility spelling for discard. */
    [[nodiscard]] Result<void> rollback();
    /** @brief Retry the retained failed commit. */
    [[nodiscard]] Result<TransactionReceipt> retry() override;
    /** @brief Compensate the most recent committed transaction. */
    [[nodiscard]] Result<TransactionReceipt> undo() override;
    /** @brief Reapply the most recently compensated transaction. */
    [[nodiscard]] Result<TransactionReceipt> redo() override;

    /** @brief True while begin/append has an uncommitted transaction. */
    [[nodiscard]] bool active() const noexcept override { return consumer_.active(); }
    /** @brief True when one or more committed transactions can be compensated. */
    [[nodiscard]] bool canUndo() const noexcept override { return consumer_.canUndo(); }
    /** @brief True when one or more compensated transactions can be reapplied. */
    [[nodiscard]] bool canRedo() const noexcept override { return consumer_.canRedo(); }
    /** @brief Clear pending work and local undo/redo history without mutating the authority target. */
    void clear() { consumer_.clear(); }

private:
    static Result<TransactionReceipt> project(eve::Result<EditorTransactionRecord>&& result);
    static Result<EditorDryRunReport> project(eve::Result<EditorDryRunReport>&& result);
    static Result<TransactionId>      project(eve::Result<TransactionId>&& result);
    static Result<void>               project(eve::Result<void>&& result);

    EditorTransactionConsumer consumer_;
};

}  // namespace eve::editor
