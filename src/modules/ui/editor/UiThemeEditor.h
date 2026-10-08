#pragma once

/**
 * @file UiThemeEditor.h
 * @brief UI-neutral named Theme catalog editor: workspace, gallery preview, undo.
 */

#include "editor/EditorAuthority.h"
#include "editor/EditorSelection.h"
#include "editor/EditorTransactionService.h"
#include "editor/EditorWorkspace.h"
#include "ui/editing/UiTheme.h"

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
    [[nodiscard]] ui_editing::Result<void> configureWorkspace(editor::EditorWorkspace& workspace) const;

    [[nodiscard]] ui_editing::Result<void> selectTheme(std::string id);
    [[nodiscard]] ui_editing::Result<void> createFromPreset(std::string id, std::string name, std::string preset);
    [[nodiscard]] ui_editing::Result<void> duplicateSelected(std::string id, std::string name);
    [[nodiscard]] ui_editing::Result<void> deleteSelected();
    [[nodiscard]] ui_editing::Result<void> setActiveSelected();
    [[nodiscard]] ui_editing::Result<void> resetSelectedToBase();
    [[nodiscard]] ui_editing::Result<void> setToken(const std::string& path, const ui_editing::EditorValue& value);
    [[nodiscard]] ui_editing::Result<editor::TransactionReceipt> undo();
    [[nodiscard]] ui_editing::Result<editor::TransactionReceipt> redo();

    /**
     * @brief Copy the selected theme onto a live UIHost without changing globalTheme.
     * @param hostName Retained UI host name (typically the preview panel id).
     */
    [[nodiscard]] ui_editing::Result<void> applyPreviewHost(const std::string& hostName);

    bool          canUndo() const noexcept { return transactions_.canUndo(); }
    /** @brief Can redo. */
    bool          canRedo() const noexcept { return transactions_.canRedo(); }
    /** @brief Revision. */
    std::uint64_t revision() const noexcept { return target_.revision(); }
    /** @brief Preview revision. */
    std::uint64_t previewRevision() const noexcept { return preview_.documentRevision; }
    /** @brief Selected id. */
    std::string   selectedId() const { return selectedId_; }
    /** @brief Active id. */
    std::string   activeId() const { return target_.activeId().value(); }
    /** @brief Preview runtime name. */
    std::string   previewRuntimeName() const { return preview_.runtimeName; }

    /** @brief Theme count. */
    int         themeCount() const { return static_cast<int>(target_.themes().size()); }
    /** @brief Theme id. */
    std::string themeId(int index) const;
    /** @brief Theme name. */
    std::string themeName(int index) const;
    /** @brief True when theme selected. */
    bool        isThemeSelected(int index) const;
    /** @brief True when theme active. */
    bool        isThemeActive(int index) const;

    /** @brief Returns the color channel. */
    float getColorChannel(const std::string& path, int channel) const;
    /** @brief Returns the float. */
    float getFloat(const std::string& path) const;

private:
    [[nodiscard]] ui_editing::Result<void> commit(
        ui_editing::Result<ui_editing::DomainOperation> operation, std::string label);
    [[nodiscard]] ui_editing::Result<void> refreshPreview();
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
