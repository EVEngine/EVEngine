#pragma once
#include "common/Export.h"

#include "editing/EditorPackage.h"
#include "editor/EditorResult.h"

#include <string>
#include <string_view>

namespace eve::editor {

class EditorHost;
class EditorSession;

/**
 * @brief Tracks the focused package undo stack (Share vs Private).
 *
 * Does not merge stacks across packages. Share packages use the session
 * transaction consumer; Private packages only update the focus pointer.
 */
class EVENGINE_API_ORCHESTRATION EditorUndoCoordinator {
public:
    explicit EditorUndoCoordinator(EditorHost& host);

    /** @brief Focus a mounted owner for undo/redo presentation. */
    [[nodiscard]] Result<void> focusOwner(std::string_view ownerModule);

    /** @brief Currently focused owner, or empty. */
    [[nodiscard]] const std::string& focusedOwner() const { return focusedOwner_; }

    /** @brief Undo policy of the focused package (Share when none). */
    [[nodiscard]] editing::EditorUndoPolicy focusedPolicy() const;

    /**
     * @brief Whether the focused share stack can undo (session-backed).
     * @param session Optional session providing share-stack undo; ignored for Private.
     */
    [[nodiscard]] bool canUndo(EditorSession* session) const;

    /** @brief Whether the focused share stack can redo. */
    [[nodiscard]] bool canRedo(EditorSession* session) const;

private:
    EditorHost* host_ = nullptr;
    std::string focusedOwner_;
};

}  // namespace eve::editor
