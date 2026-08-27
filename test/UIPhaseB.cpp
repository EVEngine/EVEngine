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

UIHost *resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

UINode *findNode(UIHost *host, const std::string &id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

UINode *findNode(UIHostHandle handle, const std::string &id) {
    return findNode(resolveHost(handle), id);
}

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

TEST_CASE("UI.b.listAndWhen") {
    auto tree = window("W", {
                                when(false, text("hidden", "h")),
                                when(true, text("shown", "s")),
                                listButtons("L", {"a", "b", "c"}),
                            });
    UIHost *h = resolveHost(UIHost::createHost("listwhen"));
    REQUIRE(h != nullptr);
    h->setTree(std::move(tree));
    CHECK(findNode(h, "s") != nullptr);
    CHECK(findNode(h, "h") == nullptr);  // when(false) → empty group, no "h"
    CHECK(findNode(h, "L/0") != nullptr);
    CHECK(findNode(h, "L/2") != nullptr);
}

TEST_CASE("UI.b.keyReconcilePropsOnly") {
    UIHost *h = resolveHost(UIHost::createHost("rec"));
    REQUIRE(h != nullptr);
    h->setTree(window("W", {text("v1", "label"), button("Go", "btn")}, "root"));
    bool rebuilt = h->setTreeReconcile(window("W", {text("v2", "label"), button("Go", "btn")}, "root"));
    CHECK(!rebuilt);  // structure same → props patch
    CHECK(findNode(h, "label")->text == "v2");

    rebuilt = h->setTreeReconcile(
        window("W", {text("v3", "label"), button("Go", "btn"), button("New", "n")}, "root"));
    CHECK(rebuilt);  // child count changed → full rebuild
    CHECK(findNode(h, "n") != nullptr);
}

TEST_CASE("UI.b.componentRebuild") {
    InventoryPanel panel;
    panel.mountAs("inv");
    UIHost *h = resolveHost(UISystem::findHost("inv"));
    REQUIRE(h != nullptr);
    CHECK(findNode(h, "gold")->text == "Gold 10");
    CHECK(findNode(h, "items/0") != nullptr);

    panel.gold = 9;
    panel.items.push_back("Shield");
    panel.markDirty();
    CHECK(panel.updateIfDirty());
    CHECK(findNode(h, "gold")->text == "Gold 9");
    CHECK(findNode(h, "items/2") != nullptr);
}

TEST_CASE("UI.b.themeAndCheckbox") {
    UI *ui = UI::create();
    ui->setThemeLight();
    CHECK(globalTheme().windowBg[0] > 0.5f);
    CHECK(ui->getTheme() == "light");
    // Light/dark share geometry so switching palette keeps a unified look.
    CHECK(globalTheme().frameRounding == Theme::dark().frameRounding);
    CHECK(globalTheme().windowPaddingX == Theme::dark().windowPaddingX);
    CHECK(globalTheme().fontScale == Theme::dark().fontScale);

    ui->setThemeDark();
    CHECK(ui->getTheme() == "dark");
    ui->setNavKeyboard(true);
    CHECK(globalTheme().navEnableKeyboard);

    CHECK(ui->setTheme("LIGHT"));
    CHECK(ui->getTheme() == "light");
    CHECK(globalTheme().windowBg[0] > 0.5f);
    CHECK(!ui->setTheme("unknown"));
    CHECK(ui->getTheme() == "light");  // unchanged on failure

    REQUIRE(UIHost::resolve(
                ui->mountAs("chk", window("C", {checkbox("Mute", false, "mute")}, "root")))
                .has_value());
    CHECK(findNode(ui->current(), "mute") != nullptr);
    CHECK(!findNode(ui->current(), "mute")->checked);
    ui->setChecked("mute", true);
    CHECK(findNode(ui->current(), "mute")->checked);
}

TEST_CASE("UI.b.themeTokensUnified") {
    Theme dark = Theme::dark();
    Theme light = Theme::light();
    CHECK(dark.frameRounding == light.frameRounding);
    CHECK(dark.windowRounding == light.windowRounding);
    CHECK(dark.windowBorderSize == light.windowBorderSize);
    CHECK(dark.frameBorderSize == light.frameBorderSize);
    CHECK(dark.itemSpacingX == light.itemSpacingX);
    CHECK(dark.windowPaddingY == light.windowPaddingY);
    CHECK(dark.fontScale == light.fontScale);
    CHECK(dark.layout.toolbarHeight == light.layout.toolbarHeight);
    CHECK(dark.layout.sidebarWidth == light.layout.sidebarWidth);
    CHECK(dark.layout.cardPaddingX == light.layout.cardPaddingX);
    CHECK(dark.layout.splitterSize == light.layout.splitterSize);
    CHECK(dark.layout.searchIconGap == light.layout.searchIconGap);
    // Palettes differ
    CHECK(dark.windowBg[0] < 0.5f);
    CHECK(light.windowBg[0] > 0.5f);
    CHECK(dark.text[0] > light.text[0]);
}

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

TEST_CASE("UI.b.semanticIcons") {
    Icon value = Icon::None;
    CHECK(iconFromName("search", &value));
    CHECK(static_cast<int>(value) == static_cast<int>(Icon::Search));
    CHECK(std::string(iconGlyph(value)) == "\xEF\x80\x82");
    CHECK(iconName(Icon::PaintBrush) == std::string("paint-brush"));
    CHECK(iconText(Icon::Save, "Save").find("Save") != std::string::npos);
    CHECK(iconFromName("folder_open", &value));
    CHECK(static_cast<int>(value) == static_cast<int>(Icon::FolderOpen));
    CHECK(!iconFromName("missing", &value));

    UIHost *host = resolveHost(UIHost::createHost("icon_widgets"));
    REQUIRE(host != nullptr);
    applyTree(host, window("Icons", {icon(Icon::Search, "search"),
                                     iconButton(Icon::Save, "Save", "save")}));
    CHECK(findNode(host, "search")->text == std::string(iconGlyph(Icon::Search)));
    CHECK(findNode(host, "save")->text == iconText(Icon::Save, "Save"));
}

TEST_CASE("UI.b.scriptListBuilder") {
    UI *ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Shop", "root");
    ui->beginList("goods");
    ui->addListItem("Apple", "goods/0");
    ui->addListItem("Bread", "goods/1");
    ui->end();
    ui->addSeparator("s");
    ui->addCheckbox("Member", false, "mem");
    ui->end();
    CHECK(ui->mountBuildAs("shop"));
    CHECK(findNode(ui->current(), "goods/1") != nullptr);
    CHECK(findNode(ui->current(), "mem") != nullptr);
}
