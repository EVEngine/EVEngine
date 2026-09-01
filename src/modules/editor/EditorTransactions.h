#pragma once

#include "editor/EditorTransactionConsumer.h"

#include <memory>
#include <string>

namespace eve::editor {

/** @brief Executes, groups, rolls back and replays arbitrary edit commands. */
class EditorTransactions {
public:
    /** @brief Compatibility facade over EditorTransactionConsumer::beginLegacy. */
    bool begin(const std::string &name);
    /** @brief Compatibility preview facade over the structured append API. */
    bool execute(std::unique_ptr<IEditCommand> command);
    /** @brief Compatibility projection of a checked commit Result. */
    bool commit();
    /** @brief Compatibility projection of a checked rollback Result. */
    bool rollback();
    /** @brief Compatibility projection of a checked compensation Result. */
    bool undo();
    /** @brief Compatibility projection of a checked redo Result. */
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
