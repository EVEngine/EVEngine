#pragma once

#include "editor/EditorAuthority.h"

#include <optional>
#include <vector>

namespace eve::editor {

/** @brief Transaction coordinator backed by an injected edit authority. */
class LocalTransactionBackend {
public:
    /** @brief Bind a non-owning authority that outlives this backend. */
    explicit LocalTransactionBackend(IEditAuthority* authority = nullptr) : authority_(authority) {}

    /** @brief Change the non-owning authority when no transaction is active. */
    EditorResult<void> setAuthority(IEditAuthority* authority);
    /** @brief Begin a transaction with a stable id and base revision. */
    EditorResult<TransactionId> begin(TransactionSpec specification);
    /** @brief Append an operation to the active transaction without applying it. */
    EditorResult<void> append(DomainOperation operation);
    /** @brief Preflight and commit all active operations through the authority. */
    EditorResult<TransactionReceipt> commit();
    /** @brief Discard the active, not-yet-committed operation list. */
    EditorResult<void> rollback();
    /** @brief Compensate the most recent committed transaction. */
    EditorResult<TransactionReceipt> undo();
    /** @brief Reapply the most recently compensated transaction. */
    EditorResult<TransactionReceipt> redo();

    /** @brief True while begin/append has an uncommitted transaction. */
    bool active() const { return pending_.has_value(); }
    /** @brief True when one or more committed transactions can be compensated. */
    bool canUndo() const { return !undo_.empty(); }
    /** @brief True when one or more compensated transactions can be reapplied. */
    bool canRedo() const { return !redo_.empty(); }

private:
    struct Pending {
        TransactionSpec              specification;
        std::vector<DomainOperation> operations;
    };
    struct HistoryEntry {
        TransactionSpec              specification;
        std::vector<DomainOperation> operations;
        TransactionReceipt           receipt;
    };

    static EditorResult<TransactionReceipt> error(EditorStatus status, const char* rule, std::string message);

    IEditAuthority*           authority_ = nullptr;
    std::optional<Pending>    pending_;
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    std::uint64_t             redoSequence_ = 0;
};

}  // namespace eve::editor
