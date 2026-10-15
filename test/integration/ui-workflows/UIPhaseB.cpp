#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/Component.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>

#include <string>
#include <vector>

using namespace eve::ui;

namespace {

[[maybe_unused]] UIHost *resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

[[maybe_unused]] UINode *findNode(UIHost *host, const std::string &id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

[[maybe_unused]] UINode *findNode(UIHostHandle handle, const std::string &id) { return findNode(resolveHost(handle), id); }

}  // namespace

class InventoryPanel : public Component {
public:
    std::vector<std::string> items{"Sword", "Potion"};
    int gold = 10;
    bool showExtra = true;

    WidgetDesc build() override {
        return window("Inventory",
                      {
                          text("Gold " + std::to_string(gold), "gold"),
                          separator("sep"),
                          listButtons("items", items),
                          when(showExtra, text("Extra tip", "tip")),
                          checkbox("Show tip", showExtra, "tipchk", [this](bool v) {
                              showExtra = v;
                              setState();
                          }),
                      },
                      "root");
    }
};


TEST_CASE("UI.b.themeAppliesCompleteModernStyle") {
    ImGui::CreateContext();
    const Theme dark = Theme::dark();
    applyThemeToImGui(dark, 1.f);

    const ImGuiStyle &style = ImGui::GetStyle();
    CHECK(style.WindowPadding.x == dark.windowPaddingX);
    CHECK(style.FramePadding.y == dark.framePaddingY);
    CHECK(style.CellPadding.y == dark.cellPaddingY);
    CHECK(style.ScrollbarSize == dark.scrollbarSize);
    CHECK(style.Colors[ImGuiCol_MenuBarBg].x == dark.menuBarBg[0]);
    CHECK(style.Colors[ImGuiCol_TabActive].y == dark.tabActive[1]);
    CHECK(style.Colors[ImGuiCol_TableHeaderBg].z == dark.tableHeaderBg[2]);
    CHECK(style.Colors[ImGuiCol_ModalWindowDimBg].w == dark.modalDimBg[3]);

    ImGui::DestroyContext();
}
