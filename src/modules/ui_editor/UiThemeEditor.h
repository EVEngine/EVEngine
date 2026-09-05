#pragma once

/**
 * @file UiThemeEditor.h
 * @brief UI-neutral named Theme catalog editor: workspace, gallery preview, undo.
 */

#include "editor/EditorAuthority.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"
#include "ui_editing/UiTheme.h"

#include <cstdint>
#include <string>

namespace eve::ui_editor {

/**
 * @brief Workspace controller for one authored UI theme catalog.
 *
 * Owns the catalog, local undo and a revision-bound Theme preview copy.
 * Gallery hosts consume the preview through a host-local Theme override;
 * process globalTheme() changes only after a successful setActive publish.
 *
 * @ownership Editor owns the catalog. Preview getters copy values.
 * @threadaffinity Owner thread only.
 * @reentrancy No unknown callbacks.
 */
class UiThemeEditor {
public:
    /** @brief Construct a catalog seeded with built-in Dark and Light assets. */
    explicit UiThemeEditor(std::string targetId);

    UiThemeEditor(const UiThemeEditor&)            = delete;
    UiThemeEditor& operator=(const UiThemeEditor&) = delete;

    /** @brief Borrow the authoritative theme catalog. */
    const ui_editing::UiThemeCatalogTarget& target() const noexcept { return target_; }

    /**
     * @brief Install Themes / Preview / Inspector panels.
     * @note Does not retain @p workspace.
     */
    [[nodiscard]] ui_editing::EditorResult<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] ui_editing::EditorResult<void> selectTheme(std::string id);
    [[nodiscard]] ui_editing::EditorResult<void> createFromPreset(std::string id, std::string name, std::string preset);
    [[nodiscard]] ui_editing::EditorResult<void> duplicateSelected(std::string id, std::string name);
    [[nodiscard]] ui_editing::EditorResult<void> deleteSelected();
    [[nodiscard]] ui_editing::EditorResult<void> setActiveSelected();
    [[nodiscard]] ui_editing::EditorResult<void> resetSelectedToBase();
    [[nodiscard]] ui_editing::EditorResult<void> setToken(const std::string& path, const ui_editing::EditorValue& value);
    [[nodiscard]] ui_editing::EditorResult<editor::TransactionReceipt> undo();
    [[nodiscard]] ui_editing::EditorResult<editor::TransactionReceipt> redo();

    /**
     * @brief Copy the selected theme onto a live UIHost without changing globalTheme.
     * @param hostName Retained UI host name (typically the preview panel id).
     */
    [[nodiscard]] ui_editing::EditorResult<void> applyPreviewHost(const std::string& hostName);

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    std::uint64_t revision() const noexcept { return target_.revision(); }
    std::uint64_t previewRevision() const noexcept { return preview_.documentRevision; }
    std::string   selectedId() const { return selectedId_; }
    std::string   activeId() const { return target_.activeId().value(); }
    std::string   previewRuntimeName() const { return preview_.runtimeName; }

    int         themeCount() const { return static_cast<int>(target_.themes().size()); }
    std::string themeId(int index) const;
    std::string themeName(int index) const;
    bool        isThemeSelected(int index) const;
    bool        isThemeActive(int index) const;

    float getColorChannel(const std::string& path, int channel) const;
    float getFloat(const std::string& path) const;

private:
    [[nodiscard]] ui_editing::EditorResult<void> commit(
        ui_editing::EditorResult<ui_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] ui_editing::EditorResult<void> refreshPreview();
    editor::SelectionSnapshot selection() const;
    /**
     * @brief Indexed catalog lookup for script getters.
     * @return Borrowed pointer owned by the editor catalog, or null.
     * @ownership this editor
     * @lifetime Valid until the next catalog mutation or editor destruction.
     */
    const ui_editing::UiThemeAsset* themeAt(int index) const;

    ui_editing::UiThemeCatalogTarget     target_;
    editor::LocalWorldAuthority          authority_;
    editor::LocalTransactionBackend      transactions_;
    ui_editing::UiThemePreviewService    previews_;
    ui_editing::UiThemeRuntimePublisher  publisher_;
    ui_editing::UiThemePreviewSnapshot   preview_;
    std::string                          selectedId_ = "dark";
    std::uint64_t                        txSequence_ = 0;
};

}  // namespace eve::ui_editor
