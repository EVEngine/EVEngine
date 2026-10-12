#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "editor/EditorWorkspace.h"
#include "ui/Widget.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::ui_editor {

/** @brief Options that control the fixed IDE chrome produced by WorkspaceLayoutShell. */
struct WorkspaceShellOptions {
    /** @brief Optional toolbar widgets placed above the dock body. */
    ui::WidgetDesc toolbar;
    /** @brief Optional status-bar widgets placed below the dock body. */
    ui::WidgetDesc statusBar;
    /** @brief Left pane fraction of the horizontal split (clamped to [0.1, 0.9]). */
    float leftRatio = 0.22f;
    /** @brief Right pane fraction of the remaining horizontal split. */
    float rightRatio = 0.28f;
    /** @brief Bottom pane fraction of the vertical split. */
    float bottomRatio = 0.28f;
    /** @brief Host window id used by UIHost / ImGui persistence. */
    std::string windowId = "root";
    /** @brief When true, multiple panels in one region become a TabContainer. */
    bool tabStackRegions = true;
};

/**
 * @brief Builds a fixed, non-draggable editor chrome from an EditorWorkspace.
 *
 * Domain editors stay UI-neutral: they only register WorkspacePanelDescriptor
 * entries. This shell maps those descriptors into one Window with AppFrame,
 * nested SplitPane regions, DockArea slots, PanelFrame chrome, and optional
 * TabContainer stacks — replacing the ad-hoc pattern of one movable UIHost per
 * panel.
 *
 * @ownership Returned WidgetDesc trees are values owned by the caller.
 * @thread Call on the UI / main thread that owns the workspace snapshot.
 * @reentrancy Does not invoke content builders while holding locks; builders
 *             must not mutate the workspace they are reading.
 */
class EVENGINE_API_WORLD WorkspaceLayoutShell {
public:
    /**
     * @brief Build panel body content for one workspace descriptor.
     * @param panel Owning snapshot of the panel being filled.
     * @return Body widgets placed inside the panel's PanelFrame / tab page.
     */
    using ContentBuilder =
        std::function<std::vector<ui::WidgetDesc>(const editor::WorkspacePanelDescriptor &panel)>;

    /**
     * @brief Compose a full Window tree for the workspace.
     * @param workspace UI-neutral panel registry and active selection.
     * @param content Optional builder; when null, each panel gets a placeholder label.
     * @param options Chrome ratios, toolbar/status, and tab-stack policy.
     * @return Window WidgetDesc ready for UIHost::setTree / UI::mountAs.
     * @cost Linear in visible panel count; builds an owning WidgetDesc tree once per call.
     */
    [[nodiscard]] static ui::WidgetDesc buildWindow(const editor::EditorWorkspace &workspace,
                                                    ContentBuilder content = {},
                                                    WorkspaceShellOptions options = {});

    /**
     * @brief Compose only the dock body (SplitPane + DockArea tree) without a Window.
     * Useful when the caller already owns MenuBar / Window chrome.
     * @cost Linear in visible panel count; builds an owning WidgetDesc subtree once per call.
     */
    [[nodiscard]] static ui::WidgetDesc buildDockBody(const editor::EditorWorkspace &workspace,
                                                      ContentBuilder content = {},
                                                      WorkspaceShellOptions options = {});

    /**
     * @brief Collect visible panels for one region in deterministic order.
     * @return Owning descriptor snapshots; empty when the region has no visible panels.
     * @cost Linear in workspace panel count; returns an owning vector copy per call.
     */
    [[nodiscard]] static std::vector<editor::WorkspacePanelDescriptor> panelsInRegion(
        const editor::EditorWorkspace &workspace, std::string_view region);
};

}  // namespace eve::ui_editor
