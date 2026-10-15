#include "ui/editor/WorkspaceLayoutShell.h"

#include <algorithm>
#include <cmath>

namespace eve::ui_editor {
namespace {

using eve::editor::WorkspacePanelDescriptor;
using eve::ui::WidgetDesc;

float clampRatio(float value) {
    if (!std::isfinite(value)) return 0.25f;
    return std::max(0.1f, std::min(0.9f, value));
}

std::vector<WidgetDesc> defaultContent(const WorkspacePanelDescriptor &panel) {
    std::vector<WidgetDesc> body;
    body.push_back(ui::text(panel.title.empty() ? panel.id : panel.title,
                            panel.id.empty() ? "placeholder" : panel.id + "/title"));
    if (!panel.capability.empty() || !panel.context.empty()) {
        body.push_back(ui::text("capability=" + panel.capability + "  context=" + panel.context,
                                panel.id + "/meta"));
    }
    return body;
}

WidgetDesc fillPanel(const WorkspacePanelDescriptor &panel, WorkspaceLayoutShell::ContentBuilder content) {
    std::vector<WidgetDesc> body =
        content ? content(panel) : defaultContent(panel);
    WidgetDesc page = ui::panelFrame(panel.title.empty() ? panel.id : panel.title, std::move(body),
                                     panel.id);
    // TabContainer uses page.text as the tab label and page.id as the page key.
    page.text = panel.title.empty() ? panel.id : panel.title;
    return page;
}

WidgetDesc regionContent(const std::vector<WorkspacePanelDescriptor> &panels,
                         const std::string &activePanel, bool tabStack,
                         WorkspaceLayoutShell::ContentBuilder content) {
    if (panels.empty()) {
        return ui::panelFrame("Empty", {ui::text("No panel", "empty")}, "empty");
    }
    if (panels.size() == 1 || !tabStack) {
        return fillPanel(panels.front(), content);
    }
    std::vector<WidgetDesc> pages;
    pages.reserve(panels.size());
    std::string active = activePanel;
    bool activeFound = false;
    for (const auto &panel : panels) {
        if (panel.id == active) activeFound = true;
        pages.push_back(fillPanel(panel, content));
    }
    if (!activeFound) active = panels.front().id;
    return ui::tabContainer(std::move(pages), active, panels.front().region + "/tabs");
}

WidgetDesc wrapDock(const std::string &region, WidgetDesc child) {
    return ui::dockArea(region, {std::move(child)}, "dock/" + region);
}

}  // namespace

std::vector<WorkspacePanelDescriptor> WorkspaceLayoutShell::panelsInRegion(
    const editor::EditorWorkspace &workspace, std::string_view region) {
    std::vector<WorkspacePanelDescriptor> out;
    const int count = workspace.getPanelCount();
    out.reserve(size_t(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        auto panel = workspace.panelAt(size_t(i));
        if (!panel.ok()) continue;
        if (!panel.value().visible) continue;
        if (panel.value().region != region) continue;
        out.push_back(panel.value());
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const WorkspacePanelDescriptor &a, const WorkspacePanelDescriptor &b) {
                         if (a.order != b.order) return a.order < b.order;
                         return a.id < b.id;
                     });
    return out;
}

ui::WidgetDesc WorkspaceLayoutShell::buildDockBody(const editor::EditorWorkspace &workspace,
                                                   ContentBuilder content,
                                                   WorkspaceShellOptions options) {
    const auto left = panelsInRegion(workspace, "left");
    const auto right = panelsInRegion(workspace, "right");
    const auto center = panelsInRegion(workspace, "center");
    const auto bottom = panelsInRegion(workspace, "bottom");
    const auto top = panelsInRegion(workspace, "top");
    const std::string &active = workspace.getActivePanel();
    const bool tabStack = options.tabStackRegions;

    WidgetDesc centerPane =
        wrapDock("center", regionContent(center, active, tabStack, content));
    if (!right.empty()) {
        centerPane = ui::splitPane(ui::FlexDirection::Row, std::move(centerPane),
                                   wrapDock("right", regionContent(right, active, tabStack, content)),
                                   1.f - clampRatio(options.rightRatio), "split/center-right");
    }
    WidgetDesc mainRow = std::move(centerPane);
    if (!left.empty()) {
        mainRow = ui::splitPane(ui::FlexDirection::Row,
                                wrapDock("left", regionContent(left, active, tabStack, content)),
                                std::move(mainRow), clampRatio(options.leftRatio), "split/left-main");
    }

    WidgetDesc body = std::move(mainRow);
    if (!top.empty()) {
        body = ui::splitPane(ui::FlexDirection::Column,
                             wrapDock("top", regionContent(top, active, tabStack, content)),
                             std::move(body), 0.12f, "split/top-main");
    }
    if (!bottom.empty()) {
        body = ui::splitPane(ui::FlexDirection::Column, std::move(body),
                             wrapDock("bottom", regionContent(bottom, active, tabStack, content)),
                             1.f - clampRatio(options.bottomRatio), "split/main-bottom");
    }
    body.flexGrow = 1.f;
    return body;
}

ui::WidgetDesc WorkspaceLayoutShell::buildWindow(const editor::EditorWorkspace &workspace,
                                                 ContentBuilder content,
                                                 WorkspaceShellOptions options) {
    WidgetDesc body = buildDockBody(workspace, std::move(content), options);
    WidgetDesc frame =
        ui::appFrame(std::move(body), std::move(options.toolbar), std::move(options.statusBar),
                     "app-frame");
    return ui::window(workspace.getTitle(), {std::move(frame)}, options.windowId);
}

}  // namespace eve::ui_editor
