#pragma once

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
    [[nodiscard]] virtual EditorResult<TransactionId> begin(TransactionSpec specification) = 0;
    /** @brief Append a serializable operation to the pending transaction. */
    [[nodiscard]] virtual EditorResult<void> append(DomainOperation operation) = 0;
    /** @brief Validate the pending transaction without publishing target state. */
    [[nodiscard]] virtual EditorResult<EditorDryRunReport> preview() = 0;
    /** @brief Publish the pending transaction and retain it for undo. */
    [[nodiscard]] virtual EditorResult<TransactionReceipt> commit() = 0;
    /** @brief Discard pending work after reverting any provisional preview. */
    [[nodiscard]] virtual EditorResult<void> discard() = 0;
    /** @brief Retry a failed commit while retaining its original operation list. */
    [[nodiscard]] virtual EditorResult<TransactionReceipt> retry() = 0;
    /** @brief Compensate the latest committed transaction. */
    [[nodiscard]] virtual EditorResult<TransactionReceipt> undo() = 0;
    /** @brief Reapply the latest compensated transaction. */
    [[nodiscard]] virtual EditorResult<TransactionReceipt> redo() = 0;

    /** @brief Whether a transaction is currently pending. */
    [[nodiscard]] virtual bool active() const noexcept = 0;
    /** @brief Whether committed history can be compensated. */
    [[nodiscard]] virtual bool canUndo() const noexcept = 0;
    /** @brief Whether compensated history can be reapplied. */
    [[nodiscard]] virtual bool canRedo() const noexcept = 0;
};

/** @brief Local transaction coordinator backed by an injected edit authority. */
class LocalTransactionBackend final : public IEditorTransactionBackend {
public:
    /** @brief Bind a non-owning authority that outlives this backend. */
    explicit LocalTransactionBackend(IEditAuthority* authority = nullptr) : consumer_(authority) {}

    /** @brief Change the non-owning authority when no transaction is active. */
    [[nodiscard]] EditorResult<void> setAuthority(IEditAuthority* authority);
    /** @brief Begin a transaction with a stable id and base revision. */
    [[nodiscard]] EditorResult<TransactionId> begin(TransactionSpec specification) override;
    /** @brief Append an operation to the active transaction without applying it. */
    [[nodiscard]] EditorResult<void> append(DomainOperation operation) override;
    /** @brief Preflight the active operation list without mutating target state. */
    [[nodiscard]] EditorResult<EditorDryRunReport> preview() override;
    /** @brief Preflight and commit all active operations through the authority. */
    [[nodiscard]] EditorResult<TransactionReceipt> commit() override;
    /** @brief Discard the active, not-yet-committed operation list. */
    [[nodiscard]] EditorResult<void> discard() override;
    /** @brief Compatibility spelling for discard. */
    [[nodiscard]] EditorResult<void> rollback();
    /** @brief Retry the retained failed commit. */
    [[nodiscard]] EditorResult<TransactionReceipt> retry() override;
    /** @brief Compensate the most recent committed transaction. */
    [[nodiscard]] EditorResult<TransactionReceipt> undo() override;
    /** @brief Reapply the most recently compensated transaction. */
    [[nodiscard]] EditorResult<TransactionReceipt> redo() override;

    /** @brief True while begin/append has an uncommitted transaction. */
    [[nodiscard]] bool active() const noexcept override { return consumer_.active(); }
    /** @brief True when one or more committed transactions can be compensated. */
    [[nodiscard]] bool canUndo() const noexcept override { return consumer_.canUndo(); }
    /** @brief True when one or more compensated transactions can be reapplied. */
    [[nodiscard]] bool canRedo() const noexcept override { return consumer_.canRedo(); }

private:
    static EditorResult<TransactionReceipt> project(eve::Result<EditorTransactionRecord>&& result);
    static EditorResult<EditorDryRunReport> project(eve::Result<EditorDryRunReport>&& result);
    static EditorResult<TransactionId>      project(eve::Result<TransactionId>&& result);
    static EditorResult<void>               project(eve::Result<void>&& result);

    EditorTransactionConsumer consumer_;
};

}  // namespace eve::editor
