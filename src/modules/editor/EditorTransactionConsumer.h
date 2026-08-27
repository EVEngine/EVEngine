#pragma once

#include "editor/EditorAuthority.h"
#include "editor/EditCommand.h"
#include "transaction/Transaction.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eve::editor {

/**
 * @brief Structured result of one editor transaction or compensation.
 *
 * The generic coordinator receipt is the lifecycle source of truth. The
 * optional authority receipt is only the editor-domain projection needed to
 * replay an authority-backed transaction; it is not a second commit state.
 */
struct EditorTransactionRecord {
    transaction::TransactionReceipt coordinator;
    TransactionSpec                  specification;
    std::optional<eve::editor::TransactionReceipt> authorityReceipt;
    std::size_t                     commandCount   = 0;
    std::size_t                     operationCount = 0;
};

/**
 * @brief Side-effect-free information returned by an editor preflight.
 *
 * Command-only transactions are structurally checked. Transactions containing
 * DomainOperation values additionally run the injected IEditAuthority's
 * preflight. Neither path publishes target state.
 */
struct EditorDryRunReport {
    TransactionSpec                 specification;
    std::size_t                     commandCount   = 0;
    std::size_t                     operationCount = 0;
    std::optional<AuthorityPlan>    authorityPlan;
};

/** @brief Lifecycle state of the editor consumer's active commit attempt. */
enum class EditorCommitState {
    /** @brief A transaction is open and has not yet had a commit attempt fail. */
    Pending,
    /** @brief The last attempt failed and retained work may be retried safely. */
    FailedRetryable,
    /** @brief The active transaction was committed and moved to editor history. */
    Committed,
    /** @brief The active transaction was explicitly rolled back or discarded. */
    Discarded,
};

/**
 * @brief Editor consumer of the common transaction participant/coordinator protocol.
 *
 * This class owns the editor transaction history. It adapts legacy reversible
 * IEditCommand objects to the generic prepare/commit/rollback/compensate
 * lifecycle, and adapts IEditAuthority for serializable DomainOperation work.
 * The consumer is synchronous and owner-thread-only. Authorities, command
 * targets, and extra participants are borrowed and must outlive the call that
 * uses them. No callback is invoked while an internal lock is held.
 */
class EditorTransactionConsumer {
public:
    /** @brief Creates a consumer with an optional non-owning edit authority. */
    explicit EditorTransactionConsumer(IEditAuthority* authority = nullptr);
    ~EditorTransactionConsumer();

    EditorTransactionConsumer(const EditorTransactionConsumer&)            = delete;
    EditorTransactionConsumer& operator=(const EditorTransactionConsumer&) = delete;

    /** @brief Changes the borrowed authority when no transaction is active. */
    [[nodiscard]] eve::Result<void> setAuthority(IEditAuthority* authority);
    /** @brief Begins a transaction with an explicit stable editor identifier. */
    [[nodiscard]] eve::Result<TransactionId> begin(TransactionSpec specification);
    /** @brief Begins a compatibility command transaction with an allocated identifier. */
    [[nodiscard]] eve::Result<TransactionId> beginLegacy(std::string label);

    /** @brief Appends a command without publishing its target mutation. */
    [[nodiscard]] eve::Result<void> append(std::unique_ptr<IEditCommand> command);
    /**
     * @brief Compatibility-only append that applies a command as an editor preview.
     * @remarks The preview is still committed, rolled back, and compensated by
     *          the generic participant; new code should use append().
     */
    [[nodiscard]] eve::Result<void> appendPreview(std::unique_ptr<IEditCommand> command);
    /** @brief Appends a serializable domain operation for authority preflight. */
    [[nodiscard]] eve::Result<void> append(DomainOperation operation);

    /** @brief Runs structural and authority preflight without publishing state. */
    [[nodiscard]] eve::Result<EditorDryRunReport> dryRun() const;
    /** @brief Commits the active editor participant set through Coordinator. */
    [[nodiscard]] eve::Result<EditorTransactionRecord> commit();
    /**
     * @brief Commits with additional borrowed participants for one composition boundary.
     * @param additional Participants owned by the caller and used only for this call.
     * @remarks Additional participants are coordinated atomically for this call;
     *          editor history only owns the editor participants and therefore a
     *          caller must provide their own replay policy for external effects.
     */
    [[nodiscard]] eve::Result<EditorTransactionRecord> commit(
        std::span<transaction::ITransactionParticipant*> additional);
    /**
     * @brief Retries a failed commit while retaining its pending work.
     * @param additional The same borrowed participant set used by the failed
     *        attempt; participants are deliberately not retained by the consumer.
     * @return A committed record, or a failure leaving the pending work and
     *         diagnostic available for another retry or discard.
     * @remarks A retry uses a fresh attempt transaction id. The caller must
     *          provide the same participant instances in the same order and
     *          ensure their compensation/rollback contract made them reusable.
     */
    [[nodiscard]] eve::Result<EditorTransactionRecord> retry(
        std::span<transaction::ITransactionParticipant*> additional = {});
    /**
     * @brief Explicitly discards the pending transaction and any preview.
     * @return Success when all preview state was reverted, otherwise a failure
     *         retaining the pending transaction for diagnosis.
     */
    [[nodiscard]] eve::Result<void> discard();
    /** @brief Discards the active transaction and any unpublished preview. */
    [[nodiscard]] eve::Result<void> rollback();
    /** @brief Compensates the latest editor-owned commit and moves it to redo history. */
    [[nodiscard]] eve::Result<EditorTransactionRecord> undo();
    /** @brief Re-executes the latest compensated editor-owned transaction. */
    [[nodiscard]] eve::Result<EditorTransactionRecord> redo();

    /** @brief Returns whether an editor transaction is active. */
    [[nodiscard]] bool active() const noexcept;
    /** @brief Returns whether editor-owned history can be compensated. */
    [[nodiscard]] bool canUndo() const noexcept;
    /** @brief Returns whether compensated editor history can be replayed. */
    [[nodiscard]] bool canRedo() const noexcept;
    /** @brief Returns the number of undo entries. */
    [[nodiscard]] std::size_t undoCount() const noexcept;
    /** @brief Returns the number of redo entries. */
    [[nodiscard]] std::size_t redoCount() const noexcept;
    /** @brief Returns the explicit state of the active/latest commit lifecycle. */
    [[nodiscard]] EditorCommitState state() const noexcept;
    /** @brief Returns diagnostics retained from the latest failed commit attempt. */
    [[nodiscard]] const std::vector<eve::Diagnostic>& diagnostics() const noexcept;
    /** @brief Clears active work and editor-owned undo/redo history. */
    void clear();

private:
    [[nodiscard]] eve::Result<EditorTransactionRecord> commitAttempt(
        std::span<transaction::ITransactionParticipant*> additional, bool retryAttempt);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::editor
