#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UI.h"
#include "ui/Theme.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>

#include <string>

using namespace eve::ui;

namespace {

[[maybe_unused]] UIHost* resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

[[maybe_unused]] UIHost* createHost(const std::string& name) { return resolveHost(UIHost::createHost(name)); }

[[maybe_unused]] UINode* findNode(UIHost* host, const std::string& id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

[[maybe_unused]] UINode* findNode(UI& ui, const std::string& id) { return findNode(resolveHost(ui.current()), id); }

}  // namespace

TEST_CASE("UI.editorKit.declarativeComponents") {
    UIHost* host = createHost("editor_kit");
    REQUIRE(host != nullptr);
    applyTree(host, window("Editor",
                           {menuBar({menu("File", {menuItem("Save", "Ctrl+S", "save")}, "file")}, "main_menu"),
                            searchField("Search assets", "", "search").withTooltip("Filter assets"),
                            sectionHeader("Properties", "properties"),
                            card({toggleSwitch("Visible", true, "visible"), badge("Modified", "state")}, "details")},
                           "root"));

    CHECK(int(findNode(host, "main_menu")->type) == int(NodeType::MenuBar));
    CHECK(int(findNode(host, "file")->type) == int(NodeType::Menu));
    CHECK(int(findNode(host, "save")->type) == int(NodeType::MenuItem));
    CHECK(findNode(host, "save")->valueText == "Ctrl+S");
    CHECK(int(findNode(host, "search")->type) == int(NodeType::SearchField));
    CHECK(findNode(host, "search")->tooltip == "Filter assets");
    CHECK(int(findNode(host, "properties")->type) == int(NodeType::SectionHeader));
    CHECK(int(findNode(host, "details")->type) == int(NodeType::Card));
    CHECK(int(findNode(host, "visible")->type) == int(NodeType::Switch));
    CHECK(findNode(host, "visible")->checked);
    CHECK(int(findNode(host, "state")->type) == int(NodeType::Badge));
}

TEST_CASE("UI.editorKit.scriptBuilderAndSerialization") {
    UI* ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Editor", "root");
    ui->setThemeScope("light");
    ui->beginMenuBar("menu_bar");
    ui->beginMenu("Edit", "edit");
    ui->addMenuItem("Undo", "Ctrl+Z", "undo");
    ui->end();
    ui->end();
    ui->addSearchField("Search", "cube", "search");
    ui->setItemTooltip("Filter scene nodes");
    ui->setItemFocusMode("all");
    ui->setItemMouseFilter("pass");
    ui->setItemTheme("dark");
    ui->setItemTabIndex(4);
    ui->setItemFocusOrder("undo", "toolbar_save");
    ui->setItemFocusNeighbors("undo", "toolbar_save", "undo", "toolbar_save");
    ui->setItemAccessibility("text-input", "Search scene", "Filters the scene tree");
    ui->addSectionHeader("Inspector", "inspector");
    ui->beginCard("card");
    ui->addSwitch("Enabled", true, "enabled");
    ui->addBadge("Active", "active");
    ui->setItemEnabled(false);
    ui->end();
    ui->beginToolbar("toolbar");
    ui->addIconButton("pointer", "", "select");
    ui->setItemSelected(true);
    ui->addIconButton("move", "", "move");
    ui->addIconButton("save", "", "toolbar_save");
    ui->end();
    ui->beginSplitPane("row", 0.3f, "workspace");
    ui->beginSidebar("left", 220.f);
    ui->beginToolbox("tools", 40.f, 2);
    ui->addIconButton("pointer", "", "select");
    ui->end();
    ui->end();
    ui->beginGroup("main");
    ui->addText("Viewport", "viewport_label");
    ui->end();
    ui->end();
    ui->beginStatusBar("status");
    ui->addText("Ready", "status_text");
    ui->end();
    ui->end();
    CHECK(ui->mountBuildAs("editor_builder"));

    const std::string json = ui->saveTreeJson();
    CHECK(json.find("searchField") != std::string::npos);
    CHECK(json.find("Filter scene nodes") != std::string::npos);
    CHECK(json.find("menuItem") != std::string::npos);
    CHECK(json.find("splitPane") != std::string::npos);
    CHECK(ui->loadTreeJson(json));
    CHECK(int(findNode(*ui, "enabled")->type) == int(NodeType::Switch));
    CHECK(!findNode(*ui, "active")->enabled);
    CHECK(findNode(*ui, "select")->checked);
    CHECK(!findNode(*ui, "move")->checked);
    ui->setChecked("move", true);
    CHECK(findNode(*ui, "move")->checked);
    CHECK(findNode(*ui, "search")->tooltip == "Filter scene nodes");
    CHECK(findNode(*ui, "search")->tabIndex == 4);
    CHECK(int(findNode(*ui, "search")->mouseFilter) == int(MouseFilter::Pass));
    CHECK(int(findNode(*ui, "root")->themePreset) == int(ThemePreset::Light));
    CHECK(int(findNode(*ui, "search")->themePreset) == int(ThemePreset::Dark));
    CHECK(findNode(*ui, "search")->focusRight == "toolbar_save");
    CHECK(findNode(*ui, "search")->accessibilityName == "Search scene");
    CHECK(findNode(*ui, "search")->accessibilityDescription == "Filters the scene tree");
    CHECK(int(findNode(*ui, "workspace")->type) == int(NodeType::SplitPane));
    CHECK(ui->requestFocus("search"));
    CHECK(ui->moveFocus("right"));
    CHECK(findNode(*ui, "toolbar_save")->focusRequested);
    findNode(*ui, "toolbar_save")->focused = true;
    CHECK(ui->getFocusedId() == "toolbar_save");
    ui->setEnabled("toolbar_save", false);
    CHECK(ui->getFocusedId().empty());
    ui->setHostMovable(true);
    ui->setHostResizable(true);
    ui->setHostOverlayAlpha(0.85f);
    UIHost* current = resolveHost(ui->current());
    REQUIRE(current != nullptr);
    CHECK(!current->meta()->lockPos);
    CHECK(!current->meta()->lockSize);
    CHECK(current->meta()->overlayBgAlpha == 0.85f);
}
