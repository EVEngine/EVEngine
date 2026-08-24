#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>

#include <string>

using namespace eve::ui;

TEST_CASE("UI.editorKit.declarativeComponents") {
    UIHost* host = UIHost::createHost("editor_kit");
    applyTree(host, window("Editor",
                           {menuBar({menu("File", {menuItem("Save", "Ctrl+S", "save")}, "file")}, "main_menu"),
                            searchField("Search assets", "", "search").withTooltip("Filter assets"),
                            sectionHeader("Properties", "properties"),
                            card({toggleSwitch("Visible", true, "visible"), badge("Modified", "state")}, "details")},
                           "root"));

    CHECK(int(host->findById("main_menu")->type) == int(NodeType::MenuBar));
    CHECK(int(host->findById("file")->type) == int(NodeType::Menu));
    CHECK(int(host->findById("save")->type) == int(NodeType::MenuItem));
    CHECK(host->findById("save")->valueText == "Ctrl+S");
    CHECK(int(host->findById("search")->type) == int(NodeType::SearchField));
    CHECK(host->findById("search")->tooltip == "Filter assets");
    CHECK(int(host->findById("properties")->type) == int(NodeType::SectionHeader));
    CHECK(int(host->findById("details")->type) == int(NodeType::Card));
    CHECK(int(host->findById("visible")->type) == int(NodeType::Switch));
    CHECK(host->findById("visible")->checked);
    CHECK(int(host->findById("state")->type) == int(NodeType::Badge));
}

TEST_CASE("UI.editorKit.scriptBuilderAndSerialization") {
    UI* ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Editor", "root");
    ui->beginMenuBar("menu_bar");
    ui->beginMenu("Edit", "edit");
    ui->addMenuItem("Undo", "Ctrl+Z", "undo");
    ui->end();
    ui->end();
    ui->addSearchField("Search", "cube", "search");
    ui->setItemTooltip("Filter scene nodes");
    ui->addSectionHeader("Inspector", "inspector");
    ui->beginCard("card");
    ui->addSwitch("Enabled", true, "enabled");
    ui->addBadge("Active", "active");
    ui->end();
    ui->beginToolbar("toolbar");
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
    CHECK(int(ui->current()->findById("enabled")->type) == int(NodeType::Switch));
    CHECK(ui->current()->findById("search")->tooltip == "Filter scene nodes");
    CHECK(int(ui->current()->findById("workspace")->type) == int(NodeType::SplitPane));
}

TEST_CASE("UI.editorKit.desktopCompositionRenders") {
    UIHost* host          = UIHost::createHost("desktop_components");
    host->meta()->hasSize = true;
    host->meta()->sizeX   = 800.f;
    host->meta()->sizeY   = 560.f;
    applyTree(
        host,
        window(
            "Editor",
            {toolbar({iconButton(Icon::Save, "", "save"), spacer("toolbar_space"), badge("Ready", "ready")}, "toolbar"),
             splitPane(
                 FlexDirection::Row,
                 sidebar({searchField("Search tools", "", "tool_search"),
                          toolbox({iconButton(Icon::Pointer, "", "select"), iconButton(Icon::Move, "", "move"),
                                   iconButton(Icon::PaintBrush, "", "paint")},
                                  "tools", 40.f, 2)},
                         "sidebar", 220.f),
                 card({sectionHeader("Inspector", "inspector"), toggleSwitch("Visible", true, "visible")}, "content"),
                 0.3f, "workspace"),
             statusBar({text("Scene ready", "message"), spacer("status_space"), text("60 FPS", "fps")}, "status")},
            "root"));

    CHECK(int(host->findById("toolbar")->type) == int(NodeType::Toolbar));
    CHECK(int(host->findById("tools")->type) == int(NodeType::Toolbox));
    CHECK(int(host->findById("sidebar")->type) == int(NodeType::Sidebar));
    CHECK(int(host->findById("workspace")->type) == int(NodeType::SplitPane));
    CHECK(int(host->findById("status")->type) == int(NodeType::StatusBar));

    ImGuiContext* savedContext = ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    ImGuiContext* headlessContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(headlessContext);
    ImGuiIO& io           = ImGui::GetIO();
    io.DisplaySize        = ImVec2(1024.f, 720.f);
    io.IniFilename        = nullptr;
    unsigned char* pixels = nullptr;
    int            width  = 0;
    int            height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    (void)pixels;

    ImGui::NewFrame();
    UISystem::render();
    ImGui::EndFrame();
    ImGui::DestroyContext(headlessContext);
    if (savedContext) ImGui::SetCurrentContext(savedContext);

    CHECK(!host->tree()->dirty);
    CHECK(host->findById("workspace")->measuredW > 0.f);
}
