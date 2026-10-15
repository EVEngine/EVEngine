#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorWorkspace.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "ui/editor/WorkspaceLayoutShell.h"

using eve::editor::EditorWorkspace;
using eve::editor::WorkspacePanelDescriptor;
using eve::ui::NodeType;
using eve::ui::UIHost;
using eve::ui_editor::WorkspaceLayoutShell;
using eve::ui_editor::WorkspaceShellOptions;

namespace {

const eve::ui::UINode* findById(const eve::ui::UIHost::Tree& tree, const std::string& id) {
    for (const auto& node : tree.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

bool hasType(const eve::ui::UIHost::Tree& tree, NodeType type) {
    for (const auto& node : tree.nodes) {
        if (node.type == type) return true;
    }
    return false;
}

int countType(const eve::ui::UIHost::Tree& tree, NodeType type) {
    int count = 0;
    for (const auto& node : tree.nodes) {
        if (node.type == type) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("editor.ui.layout.widgets_compose_fixed_chrome") {
    auto root = eve::ui::window(
        "Kit",
        {eve::ui::appFrame(
            eve::ui::splitPane(
                eve::ui::FlexDirection::Row,
                eve::ui::dockArea("left",
                                  {eve::ui::panelFrame("Assets", {eve::ui::text("list", "list")}, "assets")}),
                eve::ui::dockArea(
                    "center",
                    {eve::ui::tabContainer(
                        {eve::ui::panelFrame("Viewport", {eve::ui::text("view", "view")}, "viewport")
                             .withText("Viewport"),
                         eve::ui::panelFrame("Graph", {eve::ui::text("graph", "graph")}, "graph")
                             .withText("Graph")},
                        "viewport", "center-tabs")}),
                0.25f, "main-split"),
            eve::ui::toolbar({eve::ui::button("Save", "save")}, "toolbar"),
            eve::ui::statusBar({eve::ui::text("Ready", "ready")}, "status"), "frame")},
        "root");

    CHECK_EQ(root.type, NodeType::Window);
    REQUIRE_EQ(root.children.size(), 1u);
    CHECK_EQ(root.children[0].type, NodeType::AppFrame);

    auto host = UIHost::resolve(UIHost::createHost("layout-widgets"));
    REQUIRE(host);
    host->get().setTree(std::move(root));
    const auto& tree = *host->get().tree();
    CHECK(hasType(tree, NodeType::DockArea));
    CHECK(hasType(tree, NodeType::TabContainer));
    CHECK(hasType(tree, NodeType::PanelFrame));
    CHECK(hasType(tree, NodeType::AppFrame));
}

TEST_CASE("editor.ui.layout.workspace_shell_maps_regions_to_dock_areas") {
    EditorWorkspace workspace("demo", "Demo Editor");
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id = "assets",
                                                        .title = "Assets",
                                                        .region = "left",
                                                        .capability = "demo",
                                                        .context = "list",
                                                        .order = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id = "viewport",
                                                        .title = "Viewport",
                                                        .region = "center",
                                                        .capability = "demo",
                                                        .context = "preview",
                                                        .order = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id = "inspector",
                                                        .title = "Inspector",
                                                        .region = "right",
                                                        .capability = "demo",
                                                        .context = "inspector",
                                                        .order = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id = "timeline",
                                                        .title = "Timeline",
                                                        .region = "bottom",
                                                        .capability = "demo",
                                                        .context = "timeline",
                                                        .order = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id = "history",
                                                        .title = "History",
                                                        .region = "bottom",
                                                        .capability = "demo",
                                                        .context = "history",
                                                        .order = 1})
                .ok());
    REQUIRE(workspace.activatePanel("viewport"));

    CHECK_EQ(WorkspaceLayoutShell::panelsInRegion(workspace, "bottom").size(), 2u);
    CHECK_EQ(WorkspaceLayoutShell::panelsInRegion(workspace, "left").size(), 1u);
    CHECK_EQ(WorkspaceLayoutShell::panelsInRegion(workspace, "floating").size(), 0u);

    WorkspaceShellOptions options;
    options.toolbar = eve::ui::toolbar({eve::ui::text("Demo", "title")}, "toolbar");
    options.statusBar = eve::ui::statusBar({eve::ui::text("ok", "ok")}, "status");
    auto treeDesc = WorkspaceLayoutShell::buildWindow(
        workspace,
        [](const WorkspacePanelDescriptor& panel) {
            return std::vector<eve::ui::WidgetDesc>{eve::ui::text(panel.context, panel.id + "/body")};
        },
        options);

    auto host = UIHost::resolve(UIHost::createHost("layout-shell"));
    REQUIRE(host);
    host->get().setTree(std::move(treeDesc));
    const auto& tree = *host->get().tree();

    CHECK(hasType(tree, NodeType::AppFrame));
    CHECK(hasType(tree, NodeType::Toolbar));
    CHECK(hasType(tree, NodeType::StatusBar));
    CHECK_EQ(countType(tree, NodeType::DockArea), 4);
    CHECK(hasType(tree, NodeType::TabContainer));
    CHECK(findById(tree, "assets") != nullptr);
    CHECK(findById(tree, "viewport") != nullptr);
    CHECK(findById(tree, "inspector") != nullptr);
    const auto* tabs = findById(tree, "bottom/tabs");
    REQUIRE(tabs != nullptr);
    CHECK_EQ(tabs->type, NodeType::TabContainer);
    // Active workspace panel is center; bottom tab stack defaults to first bottom panel.
    CHECK_EQ(tabs->valueText, std::string("timeline"));
}

TEST_CASE("editor.ui.layout.workspace_shell_single_panel_skips_tabs") {
    EditorWorkspace workspace("single", "Single");
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id = "only",
                                                        .title = "Only",
                                                        .region = "center",
                                                        .order = 0})
                .ok());
    auto desc = WorkspaceLayoutShell::buildDockBody(workspace);
    auto host = UIHost::resolve(UIHost::createHost("layout-single"));
    REQUIRE(host);
    host->get().setTree(eve::ui::window("Single", {std::move(desc)}, "root"));
    const auto& tree = *host->get().tree();
    CHECK_EQ(countType(tree, NodeType::TabContainer), 0);
    CHECK(findById(tree, "only") != nullptr);
    CHECK_EQ(findById(tree, "only")->type, NodeType::PanelFrame);
}
