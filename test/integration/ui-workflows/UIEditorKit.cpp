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
