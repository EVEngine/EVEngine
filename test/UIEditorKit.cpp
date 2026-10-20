#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/ControlPrimitives.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

using namespace eve::ui;

namespace {

UIHost* resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

UIHost* createHost(const std::string& name) { return resolveHost(UIHost::createHost(name)); }

UINode* findNode(UIHost* host, const std::string& id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

UINode* findNode(UI& ui, const std::string& id) { return findNode(resolveHost(ui.current()), id); }

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

TEST_CASE("UI.editorKit.desktopCompositionRenders") {
    UIHost* host = createHost("desktop_components");
    REQUIRE(host != nullptr);
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
                                  "tools", 0.f, 2)},
                         "sidebar", 0.f),
                 card({sectionHeader("Inspector", "inspector"), toggleSwitch("Visible", true, "visible")}, "content")
                     .withTheme(ThemePreset::Light),
                 0.3f, "workspace"),
             statusBar({text("Scene ready", "message"), spacer("status_space"), text("60 FPS", "fps")}, "status")},
            "root"));

    CHECK(int(findNode(host, "toolbar")->type) == int(NodeType::Toolbar));
    CHECK(int(findNode(host, "tools")->type) == int(NodeType::Toolbox));
    CHECK(int(findNode(host, "sidebar")->type) == int(NodeType::Sidebar));
    CHECK(int(findNode(host, "workspace")->type) == int(NodeType::SplitPane));
    CHECK(int(findNode(host, "status")->type) == int(NodeType::StatusBar));

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

    const ImVec4 buttonBefore = ImGui::GetStyleColorVec4(ImGuiCol_Button);

    ImGui::NewFrame();
    UISystem::render();
    ImGui::EndFrame();
    const ImVec4 buttonAfter = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    CHECK(buttonAfter.x == buttonBefore.x);
    CHECK(buttonAfter.y == buttonBefore.y);
    CHECK(buttonAfter.z == buttonBefore.z);
    CHECK(buttonAfter.w == buttonBefore.w);
    ImGui::DestroyContext(headlessContext);
    if (savedContext) ImGui::SetCurrentContext(savedContext);

    CHECK(!host->tree()->dirty);
    CHECK(findNode(host, "workspace")->measuredW > 0.f);
    CHECK(findNode(host, "workspace")->measuredH < host->meta()->sizeY);
    CHECK(findNode(host, "status")->measuredH == Theme::dark().layout.statusBarHeight);
    CHECK(findNode(host, "sidebar")->measuredW >= Theme::dark().layout.minPaneSize);
    CHECK(findNode(host, "content")->measuredW > findNode(host, "sidebar")->measuredW);
    CHECK(findNode(host, "tool_search")->measuredW > Theme::dark().layout.searchMinWidth);
}

TEST_CASE("UI.editorKit.textToolsRoundTripAndReconcile") {
    UI* ui = UI::create();
    REQUIRE(ui != nullptr);
    ui->beginBuild();
    ui->beginWindow("Text tools", "root");
    ui->addLink("Documentation", "https://example.com/docs", "docs");
    ui->addInputText("Notes", "First line\n第二行", "notes");
    ui->setItemTextInput(true, true, "Enter notes");
    ui->setItemHelp("Plain text notes", "Document is locked");
    ui->end();
    REQUIRE(ui->mountBuildAs("text_tools"));
    const std::string saved = ui->saveTreeJson();
    REQUIRE(ui->loadTreeJson(saved));
    UINode* docs  = findNode(*ui, "docs");
    UINode* notes = findNode(*ui, "notes");
    REQUIRE(docs != nullptr);
    REQUIRE(notes != nullptr);
    REQUIRE(docs->type == NodeType::Link);
    REQUIRE(docs->valueText == "https://example.com/docs");
    REQUIRE(docs->accessibilityRole == AccessibilityRole::Link);
    REQUIRE(notes->multiline);
    REQUIRE(notes->readOnly);
    REQUIRE(notes->valueText == "First line\n第二行");
    REQUIRE(notes->placeholder == "Enter notes");
    REQUIRE(notes->helpText == "Plain text notes");
    REQUIRE(notes->disabledReason == "Document is locked");

    UIHost* host = resolveHost(ui->current());
    REQUIRE(host != nullptr);
    applyTree(host, window("Text tools",
                           {link("Documentation", "resource://manual", "docs"),
                            inputText("Notes", "Editable", "notes").withTextInput(false).withHelp("Updated help")},
                           "root"));
    REQUIRE(findNode(host, "notes")->multiline == false);
    REQUIRE(findNode(host, "notes")->readOnly == false);
    REQUIRE(findNode(host, "notes")->disabledReason.empty());
    REQUIRE(findNode(host, "notes")->helpText == "Updated help");
    REQUIRE(findNode(host, "docs")->valueText == "resource://manual");
}

TEST_CASE("UI.editorKit.textControlsInteraction") {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context  = ImGui::CreateContext();
    auto&         io       = ImGui::GetIO();
    io.DisplaySize         = ImVec2(800.f, 600.f);
    io.IniFilename         = nullptr;
    unsigned char* pixels  = nullptr;
    int            width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    std::string value = "original\ntext";
    ImVec2      linkPos, inputPos;
    auto        frame = [&](bool disabled, bool readOnly) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10.f, 10.f));
        ImGui::SetNextWindowSize(ImVec2(500.f, 400.f));
        ImGui::Begin("Text controls");
        if (disabled) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        const bool clicked = controls::textLink("Documentation##docs") == controls::ControlEdit::Changed;
        ImVec2     pos     = ImGui::GetItemRectMin();
        linkPos            = ImVec2(pos.x + 3.f, pos.y + 3.f);
        if (disabled) ImGui::PopItemFlag();
        controls::inputText("Notes", value, true, ImVec2(300.f, 100.f), readOnly);
        pos      = ImGui::GetItemRectMin();
        inputPos = ImVec2(pos.x + 10.f, pos.y + 10.f);
        ImGui::End();
        ImGui::Render();
        return clicked;
    };
    frame(false, true);
    frame(false, true);
    io.MousePos     = linkPos;
    io.MouseDown[0] = true;
    REQUIRE(!frame(false, true));
    io.MouseDown[0] = false;
    REQUIRE(frame(false, true));
    io.MouseDown[0] = true;
    REQUIRE(!frame(true, true));
    io.MouseDown[0] = false;
    REQUIRE(!frame(true, true));
    io.MousePos     = inputPos;
    io.MouseDown[0] = true;
    frame(false, true);
    io.MouseDown[0] = false;
    frame(false, true);
    io.AddInputCharactersUTF8("unchanged");
    frame(false, true);
    REQUIRE(value == "original\ntext");
    frame(false, false);
    io.MousePos     = inputPos;
    io.MouseDown[0] = true;
    frame(false, false);
    io.MouseDown[0] = false;
    frame(false, false);
    const std::string longText(2048, 'x');
    io.AddInputCharactersUTF8(longText.c_str());
    frame(false, false);
    REQUIRE(value.size() > 2048);
    ImGui::DestroyContext(context);
    if (previous) ImGui::SetCurrentContext(previous);
}
