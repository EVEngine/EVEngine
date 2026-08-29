#include "editor/EditorTransactions.h"

namespace eve::editor {

bool EditorTransactions::begin(const std::string &name) {
    auto result = consumer_.beginLegacy(name);
    return result.ok();
}
bool EditorTransactions::execute(std::unique_ptr<IEditCommand> command) {
    auto result = consumer_.appendPreview(std::move(command));
    return result.ok();
}
bool EditorTransactions::commit() {
    auto result = consumer_.commit();
    return result.ok();
}
bool EditorTransactions::rollback() {
    auto result = consumer_.rollback();
    return result.ok();
}
bool EditorTransactions::undo() {
    auto result = consumer_.undo();
    return result.ok();
}
bool EditorTransactions::redo() {
    auto result = consumer_.redo();
    return result.ok();
}
void EditorTransactions::clear() { consumer_.clear(); }

}  // namespace eve::editor
