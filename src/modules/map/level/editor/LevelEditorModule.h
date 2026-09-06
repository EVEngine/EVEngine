#pragma once

#include "common/Module.h"

#include <memory>
#include <string>

namespace eve::editor {
class EditorSession;
}
namespace eve::level_editing {
class Brush;
class EditorHistory;
class TileBuffer;
class TileBufferTarget;
}  // namespace eve::level_editing

namespace eve::level_editor {

/** @brief Script composition adapter for tile-oriented level authoring. */
class LevelEditorModule final : public Module {
public:
    Module_REG(LevelEditorModule);

    /** @brief Create an independent GID grid. @ownership Transfers unique ownership to the caller. */
    [[nodiscard]] std::unique_ptr<level_editing::TileBuffer> newTileBuffer(int width, int height) const;
    /** @brief Create a tile brush. @ownership Transfers unique ownership to the caller. */
    [[nodiscard]] std::unique_ptr<level_editing::Brush> newBrush() const;
    /** @brief Create a tile-aware undo history. @ownership Transfers unique ownership to the caller. */
    [[nodiscard]] std::unique_ptr<level_editing::EditorHistory> newHistory() const;
    /**
     * @brief Create a non-owning editable target over a tile buffer.
     * @param id Stable target identity.
     * @param buffer Borrowed buffer that must outlive the returned target.
     */
    [[nodiscard]] std::unique_ptr<level_editing::TileBufferTarget> newTarget(std::string                id,
                                                                             level_editing::TileBuffer* buffer) const;
    /** @brief Bind a borrowed tile target to a borrowed editor session. */
    void bind(editor::EditorSession* session, level_editing::TileBufferTarget* target) const;
};

}  // namespace eve::level_editor
