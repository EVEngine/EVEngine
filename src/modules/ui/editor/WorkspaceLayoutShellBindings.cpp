#include "ui/editor/WorkspaceLayoutShellBindings.h"

#include "editor/EditorWorkspace.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/editor/UiEditorModule.h"
#include "ui/editor/WorkspaceLayoutShell.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::ui_editor {
namespace {

bool mountWorkspaceShell(ui::UI* uiModule, editor::EditorWorkspace* workspace,
                         const std::string& hostName) {
    if (!uiModule || !workspace) return false;
    WorkspaceShellOptions options;
    options.windowId = "root";
    auto toolbar = ui::toolbar(
        {ui::text(workspace->getTitle(), "shell-title"), ui::spacer("shell-fill"),
         ui::badge(workspace->getMode(), "shell-mode")},
        "shell-toolbar");
    auto status = ui::statusBar(
        {ui::text("panels=" + std::to_string(workspace->getPanelCount()), "shell-panels"),
         ui::spacer("shell-status-fill"),
         ui::text(workspace->getActivePanel().empty() ? "no active panel"
                                                      : ("active=" + workspace->getActivePanel()),
                  "shell-active")},
        "shell-status");
    options.toolbar = std::move(toolbar);
    options.statusBar = std::move(status);
    ui::WidgetDesc root = WorkspaceLayoutShell::buildWindow(*workspace, {}, std::move(options));
    const auto handle = uiModule->mountAs(hostName.empty() ? "workspace-shell" : hostName,
                                          std::move(root));
    if (auto host = ui::UIHost::resolve(handle)) {
        // Lock chrome: fixed IDE layout, not freely draggable panels.
        auto meta = host->get().meta();
        meta->hasPos = true;
        meta->posX = 0.f;
        meta->posY = 0.f;
        meta->lockPos = true;
        meta->hasSize = true;
        meta->lockSize = true;
        meta->percentW = 1.f;
        meta->percentH = 1.f;
        return true;
    }
    return false;
}

}  // namespace

void exposeWorkspaceLayoutShellBindings(ssq::Table& table, ssq::Class& moduleClass) {
    (void)table;
    moduleClass.addFunc("mountWorkspaceShell",
                        [](UiEditorModule*, ui::UI* uiModule, editor::EditorWorkspace* workspace,
                           const std::string& hostName) {
                            return mountWorkspaceShell(uiModule, workspace, hostName);
                        });
    moduleClass.addFunc("panelsInRegion",
                        [](UiEditorModule*, editor::EditorWorkspace* workspace, const std::string& region) {
                            if (!workspace) return 0;
                            return int(WorkspaceLayoutShell::panelsInRegion(*workspace, region).size());
                        });
}

}  // namespace eve::ui_editor
