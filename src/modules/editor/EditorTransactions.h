#pragma once

#include "editor/EditCommand.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Executes, groups, rolls back and replays arbitrary edit commands. */
class EditorTransactions {
public:
    bool begin(const std::string &name);
    bool execute(std::unique_ptr<IEditCommand> command);
    bool commit();
    bool rollback();
    bool undo();
    bool redo();
    void clear();
    bool isActive() const { return active_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    int undoCount() const { return static_cast<int>(undo_.size()); }
    int redoCount() const { return static_cast<int>(redo_.size()); }
private:
    struct Transaction {
        std::string name;
        std::vector<std::unique_ptr<IEditCommand>> commands;
    };
    static void apply(Transaction &transaction);
    static void revert(Transaction &transaction);
    bool active_ = false;
    Transaction pending_;
    std::vector<Transaction> undo_;
    std::vector<Transaction> redo_;
};

}  // namespace eve::editor
