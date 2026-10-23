#pragma once

#include "common/Export.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"
#include "graphics/material/editing/MaterialTarget.h"

#include <string_view>

namespace eve::material_editor {

/**
 * @brief Owned, UI-neutral material document with atomic patches and undo/redo.
 * @ownership Owns the document, authority and transaction history; snapshots are detached values.
 * @threadaffinity Owner thread only. No callbacks are invoked by this facade.
 * @reentrancy Calls must not overlap. No runtime object pointers are retained.
 */
class EVENGINE_API_EDITORS MaterialEditor final {
public:
    /** @brief Create a document with the canonical material defaults. @param targetId Stable nonempty document id. */
    explicit MaterialEditor(std::string targetId);
    /** @brief Self-referencing authority/history make editor instances immovable. */
    MaterialEditor(const MaterialEditor&) = delete;
    /** @brief Copy assignment is unavailable. */
    MaterialEditor& operator=(const MaterialEditor&) = delete;
    /** @brief Move construction is unavailable. */
    MaterialEditor(MaterialEditor&&) = delete;
    /** @brief Move assignment is unavailable. */
    MaterialEditor& operator=(MaterialEditor&&) = delete;
    /** @brief Install default panels atomically; callers may customize the workspace afterwards. */
    [[nodiscard]] editing::Result<void> configureWorkspace(editor::EditorWorkspace& workspace) const;
    /** @brief Apply a property patch as one undo record; invalid patches leave document and history unchanged.
     * @param properties Detached path-to-value object, borrowed for this call.
     * @cost More expensive than a property read; batch related changes to amortize schema validation.
     */
    [[nodiscard]] editing::Result<void> setProperties(const editing::Value::Object& properties);
    /** @brief Set one schema property through the same atomic patch path. */
    [[nodiscard]] editing::Result<void> setProperty(std::string_view path, const editing::Value& value);
    /** @brief Read a property; unknown paths return NotFound rather than a default value. */
    [[nodiscard]] editing::Result<editing::Value> read(std::string_view path) const;
    /** @brief Return detached schema metadata for custom inspectors. */
    [[nodiscard]] editing::Value schema() const;
    /** @brief Return the canonical versioned material snapshot. */
    [[nodiscard]] editing::Value snapshot() const;
    /** @brief Import a canonical versioned snapshot as one undoable edit; failure preserves state.
     * @cost Full-document validation and history construction; amortize at document open/import boundaries.
     */
    [[nodiscard]] editing::Result<void> restore(const editing::Value& snapshot);
    /** @brief Undo the latest patch. */
    [[nodiscard]] editing::Result<editor::TransactionReceipt> undo();
    /** @brief Redo the latest undone patch. */
    [[nodiscard]] editing::Result<editor::TransactionReceipt> redo();
    /** @brief Query whether undo is available. */
    [[nodiscard]] bool canUndo() const { return transactions_.canUndo(); }
    /** @brief Query whether redo is available. */
    [[nodiscard]] bool canRedo() const { return transactions_.canRedo(); }
    /** @brief Current authoritative document revision. */
    [[nodiscard]] std::uint64_t revision() const { return target_.revision(); }

private:
    editing::SelectionSnapshot               selection() const;
    material_editing::MaterialDocumentTarget target_;
    editor::LocalWorldAuthority              authority_;
    editor::LocalTransactionBackend          transactions_;
    std::uint64_t                            sequence_ = 0;
};

}  // namespace eve::material_editor
