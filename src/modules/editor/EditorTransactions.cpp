#include "editor/EditorTransactions.h"

namespace eve::editor {

bool EditorTransactions::begin(const std::string &name) {
    if (active_) return false;
    active_ = true;
    pending_ = {};
    pending_.name = name;
    return true;
}
bool EditorTransactions::execute(std::unique_ptr<IEditCommand> command) {
    if (!active_ || !command || !command->apply()) return false;
    if (!pending_.commands.empty() && pending_.commands.back()->mergeWith(*command)) return true;
    pending_.commands.push_back(std::move(command));
    return true;
}
bool EditorTransactions::commit() {
    if (!active_) return false;
    active_ = false;
    if (pending_.commands.empty()) { pending_ = {}; return false; }
    undo_.push_back(std::move(pending_));
    pending_ = {};
    redo_.clear();
    return true;
}
bool EditorTransactions::rollback() {
    if (!active_) return false;
    revert(pending_);
    active_ = false;
    pending_ = {};
    return true;
}
bool EditorTransactions::undo() {
    if (active_ || undo_.empty()) return false;
    Transaction transaction = std::move(undo_.back());
    undo_.pop_back();
    revert(transaction);
    redo_.push_back(std::move(transaction));
    return true;
}
bool EditorTransactions::redo() {
    if (active_ || redo_.empty()) return false;
    Transaction transaction = std::move(redo_.back());
    redo_.pop_back();
    apply(transaction);
    undo_.push_back(std::move(transaction));
    return true;
}
void EditorTransactions::clear() {
    if (active_) rollback();
    undo_.clear();
    redo_.clear();
}
void EditorTransactions::apply(Transaction &transaction) {
    for (auto &command : transaction.commands) command->apply();
}
void EditorTransactions::revert(Transaction &transaction) {
    for (auto it = transaction.commands.rbegin(); it != transaction.commands.rend(); ++it) (*it)->revert();
}

}  // namespace eve::editor
