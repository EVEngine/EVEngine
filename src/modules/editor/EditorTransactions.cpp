#include "editor/EditorTransactions.h"

namespace eve::editor {

eve::Result<TransactionId> EditorTransactions::beginTransaction(TransactionSpec specification) {
    return consumer_.begin(std::move(specification));
}

eve::Result<TransactionId> EditorTransactions::beginTransaction(std::string label) {
    return consumer_.beginLegacy(std::move(label));
}

eve::Result<void> EditorTransactions::append(std::unique_ptr<IEditCommand> command) {
    return consumer_.appendPreview(std::move(command));
}

eve::Result<EditorTransactionRecord> EditorTransactions::commitTransaction() { return consumer_.commit(); }

eve::Result<void> EditorTransactions::rollbackTransaction() { return consumer_.rollback(); }

eve::Result<EditorTransactionRecord> EditorTransactions::undoTransaction() { return consumer_.undo(); }

eve::Result<EditorTransactionRecord> EditorTransactions::redoTransaction() { return consumer_.redo(); }

bool EditorTransactions::begin(const std::string &name) {
    auto result = beginTransaction(name);
    return result.ok();
}
bool EditorTransactions::execute(std::unique_ptr<IEditCommand> command) {
    auto result = append(std::move(command));
    return result.ok();
}
bool EditorTransactions::commit() {
    auto result = commitTransaction();
    return result.ok();
}
bool EditorTransactions::rollback() {
    auto result = rollbackTransaction();
    return result.ok();
}
bool EditorTransactions::undo() {
    auto result = undoTransaction();
    return result.ok();
}
bool EditorTransactions::redo() {
    auto result = redoTransaction();
    return result.ok();
}
void EditorTransactions::clear() { consumer_.clear(); }

}  // namespace eve::editor
