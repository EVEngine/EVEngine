#include "editor/EditorSelectionRouter.h"

#include "editor/EditorHost.h"

namespace eve::editor {

EditorSelectionRouter::EditorSelectionRouter(EditorHost& host) : host_(&host) {}

const std::string& EditorSelectionRouter::channel() const { return host_->selectionChannel(); }

Result<std::string> EditorSelectionRouter::select(const SelectionItem& item, bool additive) {
    if (!host_)
        return eve::editing::failed<std::string>(EditorStatus::Failed, RuleId("editor.selection.no-host"),
                                                 "SelectionRouter has no host");
    EditorWorkspace& workspace = host_->workspace();
    auto             selected  = workspace.selectItem(channel(), item, additive);
    if (!selected.ok())
        return eve::editing::failed<std::string>(selected.code(), RuleId("editor.selection.apply-failed"),
                                                 "Could not apply host selection");

    std::string activated;
    const int   count = workspace.getPanelCount();
    for (int index = 0; index < count; ++index) {
        if (workspace.getPanelContext(index) != "inspector") continue;
        if (!workspace.getPanelVisible(index)) continue;
        const std::string capability = workspace.getPanelCapability(index);
        if (!capability.empty() && !item.type.empty() && capability.find(item.type) == std::string::npos) {
            // Prefer panels whose capability mentions the selection type; fall back to first inspector.
            if (activated.empty()) activated = workspace.getPanelId(index);
            continue;
        }
        activated = workspace.getPanelId(index);
        break;
    }
    if (activated.empty()) {
        for (int index = 0; index < count; ++index) {
            if (workspace.getPanelContext(index) != "inspector" || !workspace.getPanelVisible(index)) continue;
            activated = workspace.getPanelId(index);
            break;
        }
    }
    if (!activated.empty()) (void)workspace.activatePanel(activated);
    return eve::editing::applied<std::string>(std::move(activated));
}

Result<void> EditorSelectionRouter::clear() {
    if (!host_)
        return eve::editing::failed<void>(EditorStatus::Failed, RuleId("editor.selection.no-host"),
                                          "SelectionRouter has no host");
    host_->workspace().clearSelection(channel());
    return eve::editing::applied<void>();
}

}  // namespace eve::editor
