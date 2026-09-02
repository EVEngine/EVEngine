#pragma once

#include "editor/EditorTransactionConsumer.h"

#include <memory>
#include <string>

namespace eve::editor {

/** @brief Executes, groups, rolls back and replays arbitrary edit commands. */
class EditorTransactions {
public:
    /** @brief Begin a canonical checked transaction. */
    [[nodiscard]] eve::Result<TransactionId> beginTransaction(TransactionSpec specification);
    /** @brief Begin a checked legacy-command transaction with a generated identity. */
    [[nodiscard]] eve::Result<TransactionId> beginTransaction(std::string label);
    /** @brief Stage and preview one command without discarding diagnostics. */
    [[nodiscard]] eve::Result<void> append(std::unique_ptr<IEditCommand> command);
    /** @brief Commit the active transaction and return its complete record. */
    [[nodiscard]] eve::Result<EditorTransactionRecord> commitTransaction();
    /** @brief Discard the active transaction and restore its preview state. */
    [[nodiscard]] eve::Result<void> rollbackTransaction();
    /** @brief Compensate the latest committed transaction. */
    [[nodiscard]] eve::Result<EditorTransactionRecord> undoTransaction();
    /** @brief Reapply the latest compensated transaction. */
    [[nodiscard]] eve::Result<EditorTransactionRecord> redoTransaction();

    /** @brief Compatibility-only boolean facade over beginTransaction(label). */
    bool begin(const std::string &name);
    /** @brief Compatibility-only boolean facade over append(). */
    bool execute(std::unique_ptr<IEditCommand> command);
    /** @brief Compatibility-only boolean projection of commitTransaction(). */
    bool commit();
    /** @brief Compatibility-only boolean projection of rollbackTransaction(). */
    bool rollback();
    /** @brief Compatibility-only boolean projection of undoTransaction(). */
    bool undo();
    /** @brief Compatibility-only boolean projection of redoTransaction(). */
    bool redo();
    void clear();
    bool isActive() const { return consumer_.active(); }
    bool canUndo() const { return consumer_.canUndo(); }
    bool canRedo() const { return consumer_.canRedo(); }
    int  undoCount() const { return static_cast<int>(consumer_.undoCount()); }
    int  redoCount() const { return static_cast<int>(consumer_.redoCount()); }

private:
    EditorTransactionConsumer consumer_;
};

}  // namespace eve::editor
