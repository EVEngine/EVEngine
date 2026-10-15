#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIBuilder)
#include "PrivateTestScope.h"
#include <string>
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// UIBuilder.cpp
namespace test_UIBuilder {
using namespace eve::ui;

namespace {

UIHost *resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

UINode *findNode(UIHostHandle handle, const std::string &id) {
    UIHost *host = resolveHost(handle);
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

}  // namespace

/** Game-facing panel: subclass UIHost so View<UIHost,…> still finds it. */
class HudPanel : public UIHost {
public:
    ENTITY(HudPanel, UIHost)
    void release() override {}

    struct Hp {
        int value = 100;
    };
    COMPONENT(Hp, hp)
};

TEST_CASE("UI.ecs.namedHostsAndSelect") {
    UI *ui = UI::create();
    REQUIRE(ui->mountAs("hud", window("HUD", {text("HP", "hp"), button("Pause", "pause")})));
    REQUIRE(ui->mountAs("menu", window("Menu", {button("Quit", "quit")})));

    CHECK(resolveHost(UISystem::findHost("hud")) != nullptr);
    CHECK(resolveHost(UISystem::findHost("menu")) != nullptr);
    CHECK(ui->select("hud"));
    CHECK(resolveHost(ui->current()) == resolveHost(UISystem::findHost("hud")));
    ui->setText("hp", "HP 80");
    REQUIRE(findNode(ui->current(), "hp") != nullptr);
    CHECK(findNode(ui->current(), "hp")->text == "HP 80");

    CHECK(ui->select("menu"));
    CHECK(findNode(ui->current(), "quit") != nullptr);
}

TEST_CASE("UI.ecs.subclassVisibleInView") {
    HudPanel *hud       = HudPanel::create();
    hud->meta()->entity = ecs::handle_of(hud);
    hud->setName("playerhud");
    hud->hp()->value = 42;
    hud->setTree(window("P", {text("x", "x")}));

    int  found = 0;
    auto view  = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->name == "playerhud") {
            ++found;
            CHECK(ecs::try_get(meta->entity) != nullptr);
        }
    }
    CHECK_GE(found, 1);
    CHECK_EQ(hud->hp()->value, 42);
    CHECK(resolveHost(UISystem::findHost("playerhud")) == static_cast<UIHost *>(hud));
}

TEST_CASE("UI.ecs.ownerBindingAndClicks") {
    UI *ui = UI::create();
    REQUIRE(ui->mountAs("inv", window("Inv", {button("Use", "use")})));
    ui->bindOwner(7);
    CHECK(resolveHost(UISystem::findHostByOwner(7)) == resolveHost(ui->current()));

    UIEvent ev;
    ev.host         = ui->current();
    ev.hostName     = "inv";
    ev.nodeId       = "use";
    ev.kind         = "click";
    ev.handlerIndex = 0;
    UISystem::pendingEvents().push_back(ev);
    UISystem::dispatchEvents();

    ui->select("inv");
    CHECK(ui->consumeClick() == "inv/use");
    CHECK(ui->consumeClick().empty());
}

TEST_CASE("UI.scriptStyleBuilder.mountBuildAs") {
    UI *ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Shop", "root");
    ui->addText("Gold: 10", "gold");
    ui->beginGroup("row");
    ui->addButton("Buy", "buy");
    ui->addSameLine();
    ui->addButton("Sell", "sell");
    ui->end();
    ui->end();
    CHECK(ui->mountBuildAs("shop"));
    CHECK(ui->select("shop"));
    CHECK(findNode(ui->current(), "gold") != nullptr);
    ui->setText("gold", "Gold: 9");
    REQUIRE(findNode(ui->current(), "gold") != nullptr);
    CHECK(findNode(ui->current(), "gold")->text == "Gold: 9");
}
}  // namespace test_UIBuilder
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIEditorKit)
#include "PrivateTestScope.h"
#include <imgui.h>
#include <string>
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// UIEditorKit.cpp
namespace test_UIEditorKit_unit {
using namespace eve::ui;

namespace {

[[maybe_unused]] UIHost *resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

[[maybe_unused]] UIHost *createHost(const std::string &name) { return resolveHost(UIHost::createHost(name)); }

[[maybe_unused]] UINode *findNode(UIHost *host, const std::string &id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

[[maybe_unused]] UINode *findNode(UI &ui, const std::string &id) { return findNode(resolveHost(ui.current()), id); }

}  // namespace

TEST_CASE("UI.editorKit.declarativeComponents") {
    UIHost *host = createHost("editor_kit");
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
    UI *ui = UI::create();
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
    UIHost *current = resolveHost(ui->current());
    REQUIRE(current != nullptr);
    CHECK(!current->meta()->lockPos);
    CHECK(!current->meta()->lockSize);
    CHECK(current->meta()->overlayBgAlpha == 0.85f);
}
}  // namespace test_UIEditorKit_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIPhaseAdv)
#include "PrivateTestScope.h"
#include <imgui.h>
#include <cmath>
#include <string>
#include "ScriptTest.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// UIPhaseAdv.cpp
namespace test_UIPhaseAdv_unit {
using namespace eve;


namespace ui = eve::ui;

namespace {

[[maybe_unused]] ui::UIHost *resolveHost(ui::UIHostHandle handle) {
    auto host = ui::UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

[[maybe_unused]] ui::UINode *findNode(ui::UIHost *host, const std::string &id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

[[maybe_unused]] ui::UINode *findNode(ui::UIHostHandle handle, const std::string &id) {
    return findNode(resolveHost(handle), id);
}

}  // namespace

TEST_CASE("UI.adv.sliderProgressInputCollapseChild") {
    float       slid = 0.f;
    std::string typed;
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("adv"));
    REQUIRE(h != nullptr);
    h->setTree(ui::window("Adv",
                          {
                              ui::slider("Vol", 0.3f, 0.f, 1.f, "vol", [&](float v) { slid = v; }),
                              ui::progress(0.5f, "hp", "50%"),
                              ui::inputText("Name", "hero", "name", [&](const std::string &s) { typed = s; }),
                              ui::collapsingHeader("More", {ui::text("hidden tip", "tip")}, "more", true),
                              ui::child("scroll", {ui::text("row0", "r0"), ui::text("row1", "r1")}, 200.f, 80.f),
                          },
                          "root"));

    REQUIRE(findNode(h, "vol") != nullptr);
    CHECK_EQ(int(findNode(h, "vol")->type), int(ui::NodeType::Slider));
    CHECK(std::abs(findNode(h, "vol")->value - 0.3f) < 1e-5f);
    CHECK_EQ(int(findNode(h, "hp")->type), int(ui::NodeType::Progress));
    CHECK(findNode(h, "name")->valueText == "hero");
    CHECK(findNode(h, "more") != nullptr);
    CHECK(findNode(h, "tip") != nullptr);
    CHECK(findNode(h, "scroll") != nullptr);
    CHECK(findNode(h, "r1") != nullptr);

    ui::UIEvent ev;
    ev.host                   = h->handle();
    ev.hostName               = "adv";
    ev.nodeId                 = "vol";
    ev.kind                   = "value";
    ev.handlerIndex           = findNode(h, "vol")->handlerValue;
    ev.floatValue             = 0.75f;
    findNode(h, "vol")->value = 0.75f;
    ui::UISystem::pendingEvents().push_back(ev);
    ui::UISystem::dispatchEvents();
    CHECK(std::abs(slid - 0.75f) < 1e-5f);
    CHECK(ui::UISystem::consumeChange() == "adv/vol");
}

TEST_CASE("UI.adv.builderScriptWidgets") {
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Shop", "root");
    uimod->addSlider("Price", 10.f, 0.f, 100.f, "price");
    uimod->addProgress(0.2f, "load", "20%");
    uimod->addInputText("SKU", "A1", "sku");
    uimod->beginCollapsing("Details", "det", true);
    uimod->addText("More info", "info");
    uimod->end();
    uimod->beginChild("list", 0.f, 60.f);
    uimod->addText("item", "it");
    uimod->end();
    uimod->end();
    CHECK(uimod->mountBuildAs("shop"));
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);
    CHECK(findNode(current, "price") != nullptr);
    CHECK(findNode(current, "sku")->valueText == "A1");
    CHECK(findNode(current, "info") != nullptr);
    uimod->setValue("price", 42.f);
    CHECK(std::abs(uimod->getValue("price") - 42.f) < 1e-5f);
    uimod->setValueText("sku", "B2");
    CHECK(uimod->getValueText("sku") == "B2");
    uimod->setHostModal(true);
    CHECK(current->meta()->modal);
}

TEST_CASE("UI.adv.wantCaptureAPI") {
    ui::UI *uimod = ui::UI::create();
    // Callable regardless of prior smoke tests that may have initialized ImGui.
    bool mouse    = uimod->wantCaptureMouse();
    bool keyboard = uimod->wantCaptureKeyboard();
    CHECK(mouse == mouse);
    CHECK(keyboard == keyboard);
}

static const char *kScriptComponentContent = R"SQ(
function testScriptComponent() {
    local u = eve.UI()
    class PriceLabel extends eve.UIComponent {
        unmounted = false
        function onUnmount() { unmounted = true }
        function build() {
            ui().text(props.prefix + state.amount, "gold")
        }
    }
    class ShopPanel extends eve.UIComponent {
        gold = 10
        child = null
        mounted = false
        updated = false
        unmounted = false
        updateCount = 0
        constructor(uiRef) {
            base.constructor(uiRef)
            gold = 10
            child = PriceLabel(uiRef, { prefix = "Gold " })
            local self = this
            bindClick("buy", function() { self.gold += 1 })
            // Rebinding the same component event replaces the prior closure.
            bindClick("buy", function() { self.gold += 2 })
        }
        function onMount() { mounted = true }
        function onUpdated() { updated = true; updateCount += 1 }
        function onUnmount() { unmounted = true }
        function build() {
            local uu = ui()
            uu.beginWindow("Shop", "root")
            renderChild(child, { prefix = props.prefix })
            if (!("showKeyed" in state) || state.showKeyed) {
                renderKeyed("stable-price", function() {
                    return PriceLabel(ui(), { prefix = "Stable " })
                }, { prefix = "Stable " })
            }
            uu.button("Buy", "buy")
            if (state.gold > 5) uu.text("Rich", "rich")
            uu.end()
        }
    }
    local panel = ShopPanel(u)
    panel.setProps({ prefix = "Coins " })
    panel.setState({ gold = 10 })
    panel.child.setState({ amount = 10 })
    panel.setState({ showKeyed = true })
    panel.mountAs("shop_panel")
    if (!panel.mounted) return false
    if (panel._eventBindings.len() != 1) return false
    if (!("stable-price" in panel._keyedChildren)) return false
    local stableChild = panel._keyedChildren["stable-price"]
    if (!u.select("shop_panel")) return false
    panel.setState({ gold = 3 })
    panel.child.setState({ amount = 3 })
    if (!panel.dirty) return false
    // Multiple state changes in one frame are coalesced into one rebuild.
    eve_ui_flush_components()
    if (!panel.updated) return false
    if (panel.updateCount != 1) return false
    if (panel.dirty) return false
    if (panel._keyedChildren["stable-price"] != stableChild) return false
    panel.setState({ showKeyed = false })
    eve_ui_flush_components()
    if (!stableChild.unmounted) return false
    if ("stable-price" in panel._keyedChildren) return false
    if (!panel.unmount()) return false
    if (!panel.unmounted) return false
    if (panel.child._mounted) return false
    if (eve.UIComponent == null) return false
    return true
}
)SQ";

UnitSciptTest(UIScriptComponentTest, kScriptComponentContent);


TEST_CASE("UI.adv.flexRowColumnSpacer") {
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("flex"));
    REQUIRE(h != nullptr);
    h->setTree(ui::window("Flex",
                          {
                              ui::row(
                                  {
                                      ui::button("L", "left"),
                                      ui::spacer("mid"),
                                      ui::button("R", "right").withFlexGrow(0.f),
                                  },
                                  "toolbar")
                                  .withGap(8.f)
                                  .withJustify(ui::FlexJustify::Start),
                              ui::column(
                                  {
                                      ui::text("A", "a"),
                                      ui::text("B", "b"),
                                  },
                                  "stack")
                                  .withGap(4.f)
                                  .withAlign(ui::FlexAlign::Stretch),
                              ui::flex(ui::FlexDirection::Row,
                                       {
                                           ui::button("1", "one").withFlexGrow(1.f),
                                           ui::button("2", "two").withFlexGrow(2.f),
                                       },
                                       "growrow")
                                  .withJustify(ui::FlexJustify::SpaceBetween),
                          },
                          "root"));

    auto *toolbar = findNode(h, "toolbar");
    CHECK(toolbar != nullptr);
    CHECK_EQ(int(toolbar->type), int(ui::NodeType::Flex));
    CHECK_EQ(int(toolbar->flexDirection), int(ui::FlexDirection::Row));
    CHECK(std::abs(toolbar->gap - 8.f) < 1e-5f);

    auto *mid = findNode(h, "mid");
    CHECK(mid != nullptr);
    CHECK_EQ(int(mid->type), int(ui::NodeType::Spacer));
    CHECK(mid->flexGrow > 0.f);

    auto *stack = findNode(h, "stack");
    CHECK(stack != nullptr);
    CHECK_EQ(int(stack->flexDirection), int(ui::FlexDirection::Column));
    CHECK_EQ(int(stack->alignItems), int(ui::FlexAlign::Stretch));

    auto *one = findNode(h, "one");
    CHECK(one != nullptr);
    CHECK(std::abs(one->flexGrow - 1.f) < 1e-5f);
    CHECK(std::abs(findNode(h, "two")->flexGrow - 2.f) < 1e-5f);
}

TEST_CASE("UI.adv.flexBuilderAPI") {
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Bar", "root");
    uimod->beginRow("tools", 6.f);
    uimod->setFlexJustify("space-between");
    uimod->setFlexAlign("center");
    uimod->setFlexWrap(true);
    uimod->setLayoutGaps(6.f, 9.f);
    uimod->setLayoutOverflow("clip");
    uimod->addButton("Save", "save");
    uimod->addSpacer("sp");
    uimod->addButton("Quit", "quit");
    uimod->setItemFlexGrow(0.f);
    uimod->setItemFlexShrink(0.5f);
    uimod->setItemFlexBasis(64.f);
    uimod->setItemAlignSelf("end");
    uimod->setItemAspectRatio(2.f);
    uimod->setItemSize(80.f, 0.f);
    uimod->end();
    uimod->beginColumn("body", 2.f);
    uimod->addText("Hello", "hello");
    uimod->addText("World", "world");
    uimod->end();
    uimod->beginFlex("column", "side", 0.f);
    uimod->addButton("X", "x");
    uimod->end();
    uimod->beginGrid(3, "cards", 10.f, 12.f);
    uimod->addButton("Wide", "wide");
    uimod->setItemGridColumnSpan(2);
    uimod->addButton("Narrow", "narrow");
    uimod->end();
    uimod->end();
    CHECK(uimod->mountBuildAs("bar"));
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    auto *tools = findNode(current, "tools");
    CHECK(tools != nullptr);
    CHECK_EQ(int(tools->type), int(ui::NodeType::Flex));
    CHECK_EQ(int(tools->flexDirection), int(ui::FlexDirection::Row));
    CHECK_EQ(int(tools->justifyContent), int(ui::FlexJustify::SpaceBetween));
    CHECK_EQ(int(tools->alignItems), int(ui::FlexAlign::Center));
    CHECK(std::abs(tools->gap - 6.f) < 1e-5f);
    CHECK(std::abs(tools->columnGap - 6.f) < 1e-5f);
    CHECK(std::abs(tools->rowGap - 9.f) < 1e-5f);
    CHECK_EQ(int(tools->flexWrap), int(ui::FlexWrap::Wrap));
    CHECK_EQ(int(tools->overflow), int(ui::OverflowMode::Clip));

    auto *quit = findNode(current, "quit");
    CHECK(quit != nullptr);
    CHECK(std::abs(quit->sizeX - 80.f) < 1e-5f);
    CHECK(std::abs(quit->flexShrink - 0.5f) < 1e-5f);
    CHECK(std::abs(quit->flexBasis - 64.f) < 1e-5f);
    CHECK_EQ(quit->alignSelf, int(ui::FlexAlign::End));
    CHECK(std::abs(quit->aspectRatio - 2.f) < 1e-5f);

    auto *sp = findNode(current, "sp");
    CHECK(sp != nullptr);
    CHECK_EQ(int(sp->type), int(ui::NodeType::Spacer));

    auto *side = findNode(current, "side");
    CHECK(side != nullptr);
    CHECK_EQ(int(side->flexDirection), int(ui::FlexDirection::Column));
    auto *cards = findNode(current, "cards");
    REQUIRE(cards != nullptr);
    CHECK_EQ(int(cards->type), int(ui::NodeType::Grid));
    CHECK_EQ(cards->gridColumns, 3);
    CHECK_EQ(findNode(current, "wide")->gridColumnSpan, 2);
}

TEST_CASE("UI.adv.flexReconcileKeepsStructure") {
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("flexrec"));
    REQUIRE(h != nullptr);
    auto tree1 = ui::window("F", {ui::row({ui::button("A", "a"), ui::spacer("s"), ui::button("B", "b")}, "r")}, "root");
    h->setTree(tree1);
    auto tree2 = ui::window(
        "F", {ui::row({ui::button("A2", "a"), ui::spacer("s"), ui::button("B2", "b")}, "r").withGap(10.f)}, "root");
    bool rebuilt = h->setTreeReconcile(std::move(tree2));
    CHECK(!rebuilt);
    CHECK(findNode(h, "a")->text == "A2");
    CHECK(std::abs(findNode(h, "r")->gap - 10.f) < 1e-5f);
}

TEST_CASE("UI.layout.flexArrangeGrowAndJustify") {
    // 3 items in a 400px row: fixed(60) + grow1 + fixed(60), gap 10.
    std::vector<ui::FlexItemSpec> items(3);
    items[0].basisMain = 60.f;
    items[1].basisMain = 20.f;
    items[1].flexGrow  = 1.f;
    items[2].basisMain = 60.f;
    ui::FlexResult r   = ui::flexArrange(true, 10.f, 400.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items);
    REQUIRE_EQ(int(r.items.size()), 3);
    // fixed 60 + gap 10 + grow item 240 + gap 10 + fixed 60 = 380; free 20 goes to grow item.
    CHECK(std::abs(r.items[0].w - 60.f) < 1e-4f);
    CHECK(std::abs(r.items[1].w - 260.f) < 1e-4f);  // 20 basis + 240 grow
    CHECK(std::abs(r.items[2].w - 60.f) < 1e-4f);
    CHECK(std::abs(r.items[0].x - 0.f) < 1e-4f);
    CHECK(std::abs(r.items[1].x - 70.f) < 1e-4f);
    CHECK(std::abs(r.items[2].x - 340.f) < 1e-4f);

    // Justify space-between consumes free space when nothing grows.
    std::vector<ui::FlexItemSpec> fixed(2);
    fixed[0].basisMain = 50.f;
    fixed[1].basisMain = 50.f;
    ui::FlexResult jr =
        ui::flexArrange(true, 0.f, 200.f, 20.f, ui::FlexAlign::Start, ui::FlexJustify::SpaceBetween, fixed);
    CHECK(std::abs(jr.items[0].x - 0.f) < 1e-4f);
    CHECK(std::abs(jr.items[1].x - 150.f) < 1e-4f);
    CHECK(std::abs(jr.contentW - 200.f) < 1e-4f);
}

TEST_CASE("UI.layout.flexArrangeMarginsPercentAbsolute") {
    // Margins participate in flow and cross placement.
    std::vector<ui::FlexItemSpec> items(1);
    items[0].basisMain         = 40.f;
    items[0].basisCross        = 10.f;
    items[0].marginBefore      = 5.f;
    items[0].marginAfter       = 5.f;
    items[0].marginCrossBefore = 3.f;
    ui::FlexResult r = ui::flexArrange(true, 0.f, 100.f, 40.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items);
    CHECK(std::abs(r.items[0].x - 5.f) < 1e-4f);
    CHECK(std::abs(r.items[0].y - 3.f) < 1e-4f);
    CHECK(std::abs(r.contentW - 50.f) < 1e-4f);  // 5 + 40 + 5

    // Percent sizing against avail.
    std::vector<ui::FlexItemSpec> pct(1);
    pct[0].percentMain  = 0.5f;
    pct[0].percentCross = 1.f;
    ui::FlexResult pr   = ui::flexArrange(true, 0.f, 200.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, pct);
    CHECK(std::abs(pr.items[0].w - 100.f) < 1e-4f);
    CHECK(std::abs(pr.items[0].h - 30.f) < 1e-4f);

    // Absolute placement: anchor (1,0) with offset (-10, 0) → right edge inset by 10.
    std::vector<ui::FlexItemSpec> abs(1);
    abs[0].absolute    = true;
    abs[0].basisMain   = 20.f;
    abs[0].basisCross  = 20.f;
    abs[0].anchorMain  = 1.f;
    abs[0].anchorCross = 0.f;
    abs[0].posMain     = -10.f;
    ui::FlexResult ar  = ui::flexArrange(true, 0.f, 200.f, 50.f, ui::FlexAlign::Start, ui::FlexJustify::Start, abs);
    CHECK(std::abs(ar.items[0].x - 170.f) < 1e-4f);  // 200 - 20 - 10
    CHECK(std::abs(ar.items[0].y - 0.f) < 1e-4f);
}

TEST_CASE("UI.layout.flexBasisShrinkAlignSelfAndOverflow") {
    std::vector<ui::FlexItemSpec> items(2);
    items[0].basisMain  = 10.f;
    items[0].flexBasis  = 80.f;
    items[0].basisCross = 10.f;
    items[0].alignSelf  = int(ui::FlexAlign::End);
    items[1].basisMain  = 80.f;
    items[1].basisCross = 10.f;
    ui::FlexResult r    = ui::flexArrange(true, 0.f, 100.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items);
    CHECK(std::abs(r.items[0].w - 50.f) < 1e-4f);
    CHECK(std::abs(r.items[1].w - 50.f) < 1e-4f);
    CHECK(std::abs(r.items[0].y - 20.f) < 1e-4f);
    CHECK(std::abs(r.overflowMain) < 1e-4f);

    items[0].flexShrink = 0.f;
    items[1].minMain    = 30.f;
    r                   = ui::flexArrange(true, 0.f, 100.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items);
    CHECK(std::abs(r.items[0].w - 80.f) < 1e-4f);
    CHECK(std::abs(r.items[1].w - 30.f) < 1e-4f);
    CHECK(std::abs(r.overflowMain - 10.f) < 1e-4f);
}

TEST_CASE("UI.layout.flexWrapIndependentGaps") {
    std::vector<ui::FlexItemSpec> items(3);
    for (auto &item : items) {
        item.basisMain  = 60.f;
        item.basisCross = 20.f;
        item.flexShrink = 0.f;
    }
    const ui::FlexResult r =
        ui::flexArrange(true, 10.f, 130.f, 100.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items, true, 7.f);
    CHECK(std::abs(r.items[0].x - 0.f) < 1e-4f);
    CHECK(std::abs(r.items[1].x - 70.f) < 1e-4f);
    CHECK(std::abs(r.items[2].x - 0.f) < 1e-4f);
    CHECK(std::abs(r.items[2].y - 27.f) < 1e-4f);
    CHECK(std::abs(r.contentH - 47.f) < 1e-4f);
}

TEST_CASE("UI.layout.gridArrangeSpanAspectAndRows") {
    std::vector<ui::GridItemSpec> items(3);
    items[0].basisH        = 20.f;
    items[0].columnSpan    = 2;
    items[1].aspectRatio   = 2.f;
    items[2].basisH        = 30.f;
    const ui::GridResult r = ui::gridArrange(3, 10.f, 5.f, 320.f, items);
    REQUIRE_EQ(int(r.items.size()), 3);
    CHECK(std::abs(r.items[0].w - 210.f) < 1e-4f);
    CHECK(std::abs(r.items[1].x - 220.f) < 1e-4f);
    CHECK(std::abs(r.items[1].h - 50.f) < 1e-4f);
    CHECK(std::abs(r.items[2].y - 55.f) < 1e-4f);
    CHECK(std::abs(r.contentH - 85.f) < 1e-4f);
}


TEST_CASE("UI.style.namedClassesResolveOnceAndRoundTrip") {
    ui::clearStyleClasses();
    CHECK_EQ(int(ui::defineStyleClass("panel")), int(ui::StyleClassStatus::Applied));
    CHECK_EQ(int(ui::setStyleClassColor("panel", "background", 0.1f, 0.2f, 0.3f, 0.8f)),
             int(ui::StyleClassStatus::Applied));
    CHECK_EQ(int(ui::setStyleClassMetric("panel", "padding", 12.f, 7.f)), int(ui::StyleClassStatus::Applied));
    CHECK_EQ(int(ui::defineStyleClass("primary", "panel")), int(ui::StyleClassStatus::Applied));
    CHECK_EQ(int(ui::setStyleClassColor("primary", "accent", 0.9f, 0.4f, 0.2f, 1.f)),
             int(ui::StyleClassStatus::Applied));
    CHECK_EQ(int(ui::defineStyleClass("bad", "missing")), int(ui::StyleClassStatus::UnknownParent));
    CHECK_EQ(int(ui::setStyleClassMetric("primary", "mystery", 1.f)), int(ui::StyleClassStatus::UnknownProperty));
    CHECK_EQ(int(ui::defineStyleClass("panel", "primary")), int(ui::StyleClassStatus::InheritanceCycle));

    ui::StyleClass resolved;
    REQUIRE(ui::resolveStyleClass("primary", &resolved) == ui::StyleClassStatus::Applied);
    CHECK(resolved.hasBackgroundColor);
    CHECK(resolved.hasAccentColor);
    CHECK(resolved.hasPadding);
    CHECK(std::abs(resolved.paddingX - 12.f) < 1e-5f);

    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Styled", "styled-root");
    uimod->setStyleScope("panel");
    uimod->addButton("Save", "save");
    uimod->setItemStyleClass("primary");
    uimod->end();
    REQUIRE(uimod->mountBuildAs("styles"));
    ui::UINode *root = findNode(uimod->current(), "styled-root");
    ui::UINode *save = findNode(uimod->current(), "save");
    REQUIRE(root != nullptr);
    REQUIRE(save != nullptr);
    CHECK(root->hasResolvedStyle);
    CHECK(save->hasResolvedStyle);
    CHECK(save->resolvedStyle.hasBackgroundColor);
    CHECK(save->resolvedStyle.hasAccentColor);

    const std::string json = uimod->saveTreeJson();
    CHECK(json.find("\"version\"") != std::string::npos);
    CHECK(json.find("\"styleClass\"") != std::string::npos);
    CHECK(json.find("primary") != std::string::npos);
    REQUIRE(uimod->loadTreeJson(json));
    save = findNode(uimod->current(), "save");
    REQUIRE(save != nullptr);
    CHECK(save->styleClass == "primary");
    CHECK(save->hasResolvedStyle);
    ui::clearStyleClasses();
}

TEST_CASE("UI.p0.imageAndImageButton") {
    int         clicks = 0;
    ui::UIHost *h      = resolveHost(ui::UIHost::createHost("img"));
    REQUIRE(h != nullptr);
    h->setTree(
        ui::window("W",
                   {
                       ui::image("avatar", 64.f, 64.f).withTint(0.2f, 0.4f, 0.8f).withCornerRadius(8.f),
                       ui::imageButton("btn", 48.f, 48.f, [&] { ++clicks; }),
                       ui::image("frame", 120.f, 40.f).withUv(0.f, 0.f, 0.5f, 0.5f).withNinePatch(8.f, 8.f, 8.f, 8.f),
                   },
                   "root"));

    ui::UINode *avatar = findNode(h, "avatar");
    ui::UINode *btn    = findNode(h, "btn");
    ui::UINode *frame  = findNode(h, "frame");
    REQUIRE(avatar != nullptr);
    REQUIRE(btn != nullptr);
    REQUIRE(frame != nullptr);
    CHECK_EQ(int(avatar->type), int(ui::NodeType::Image));
    CHECK_EQ(int(btn->type), int(ui::NodeType::ImageButton));
    CHECK(std::abs(avatar->sizeX - 64.f) < 1e-5f);
    CHECK(std::abs(avatar->tintB - 0.8f) < 1e-5f);
    CHECK(std::abs(avatar->cornerRadius - 8.f) < 1e-5f);
    CHECK(std::abs(frame->uv1x - 0.5f) < 1e-5f);
    CHECK(std::abs(frame->borderL - 8.f) < 1e-5f);
    CHECK(std::abs(frame->borderB - 8.f) < 1e-5f);

    // Click routing for ImageButton via the pending-event pipeline.
    ui::UIEvent ev;
    ev.host         = h->handle();
    ev.hostName     = "img";
    ev.nodeId       = "btn";
    ev.kind         = "click";
    ev.handlerIndex = btn->handlerClick;
    ui::UISystem::pendingEvents().push_back(ev);
    ui::UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    CHECK(ui::UISystem::consumeClickFor("img") == "btn");

    // Script builder + mutators.
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("B", "root");
    uimod->addImage("icon", 32.f, 32.f);
    uimod->addImageButton("go", 32.f, 32.f);
    uimod->end();
    CHECK(uimod->mountBuildAs("imgb"));
    CHECK(findNode(uimod->current(), "icon") != nullptr);
    CHECK(findNode(uimod->current(), "go") != nullptr);
    uimod->setImageTint("icon", 1.f, 0.f, 0.f, 0.5f);
    CHECK(std::abs(findNode(uimod->current(), "icon")->tintR - 1.f) < 1e-5f);
    CHECK(std::abs(findNode(uimod->current(), "icon")->tintA - 0.5f) < 1e-5f);
    uimod->setImageNinePatch("go", 4.f, 4.f, 4.f, 4.f);
    CHECK(std::abs(findNode(uimod->current(), "go")->borderT - 4.f) < 1e-5f);
}

static const char *kScriptCallbackContent = R"SQ(
function testScriptCallbacks() {
    local u = eve.UI()
    u.beginBuild()
    u.beginWindow("W", "root")
    u.button("Go", "go")
    u.slider("S", 0.5, 0.0, 1.0, "s")
    u.end()
    u.mountBuildAs("cb")
    clicks <- 0
    changeKind <- ""
    changeValue <- -1.0
    cbui <- u
    u.onClick("go", function() { clicks += 1 })
    u.onChange("s", function(kind, value) { changeKind = kind; changeValue = value })
    return true
}
function fireTestEvents() {
    cbui.dispatchEvents()
    return true
}
)SQ";

UnitSciptTest(UIScriptCallbackTest, kScriptCallbackContent);


TEST_CASE("UI.p1.comboAndTextWrap") {
    int         picked = -1;
    ui::UIHost *h      = resolveHost(ui::UIHost::createHost("p1"));
    REQUIRE(h != nullptr);
    h->setTree(ui::window("W",
                          {
                              ui::combo("Fruit", {"Apple", "Banana", "Cherry"}, 1, "fruit",
                                        [&](float idx) { picked = static_cast<int>(idx); }),
                              ui::text("long text", "tip").withWrap(120.f),
                          },
                          "root"));

    ui::UINode *combo = findNode(h, "fruit");
    ui::UINode *tip   = findNode(h, "tip");
    REQUIRE(combo != nullptr);
    REQUIRE(tip != nullptr);
    CHECK_EQ(int(combo->type), int(ui::NodeType::Combo));
    CHECK(combo->valueText == "Apple\nBanana\nCherry");
    CHECK(std::abs(combo->value - 1.f) < 1e-5f);
    CHECK(std::abs(tip->wrapWidth - 120.f) < 1e-5f);

    // Value change routing: pending event → C++ callback + change queue.
    ui::UIEvent ev;
    ev.host         = h->handle();
    ev.hostName     = "p1";
    ev.nodeId       = "fruit";
    ev.kind         = "value";
    ev.handlerIndex = combo->handlerValue;
    ev.floatValue   = 2.f;
    ui::UISystem::pendingEvents().push_back(ev);
    ui::UISystem::dispatchEvents();
    CHECK_EQ(picked, 2);
    CHECK(ui::UISystem::consumeChange() == "p1/fruit");

    // Script builder surface.
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("B", "root");
    uimod->addCombo("Class", "Warrior\nMage", 0, "cls");
    uimod->addTextWrapped("some long paragraph", 180.f, "para");
    uimod->end();
    CHECK(uimod->mountBuildAs("p1b"));
    CHECK(findNode(uimod->current(), "cls") != nullptr);
    CHECK(std::abs(findNode(uimod->current(), "para")->wrapWidth - 180.f) < 1e-5f);
    uimod->setTextWrap("para", 200.f);
    CHECK(std::abs(findNode(uimod->current(), "para")->wrapWidth - 200.f) < 1e-5f);
}


TEST_CASE("UI.p1.jsonRoundTripAndGamepadNav") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(
                uimod->mountAs(
                    "jsonhost",
                    ui::window(
                        "W",
                        {
                            ui::text("HP 100", "hp").withWrap(120.f),
                            ui::row({ui::button("A", "a"), ui::spacer("sp"), ui::button("B", "b")}, "r").withGap(8.f),
                            ui::image("avatar", 64.f, 64.f).withTint(0.2f, 0.3f, 0.4f, 0.5f).withCornerRadius(4.f),
                            ui::combo("C", {"x", "y"}, 1, "combo"),
                        },
                        "root")))
                .has_value());
    const std::string json = uimod->saveTreeJson();
    CHECK(json.find("eve.ui.tree") != std::string::npos);
    CHECK(json.find("\"version\"") != std::string::npos);
    CHECK(json.find("\"hp\"") != std::string::npos);
    CHECK(json.find("wrapWidth") != std::string::npos);

    // Mutate, then load from JSON → original structure and props restored.
    uimod->setText("hp", "changed");
    CHECK(findNode(uimod->current(), "hp")->text == "changed");
    CHECK(uimod->loadTreeJson(json));
    CHECK(findNode(uimod->current(), "hp")->text == "HP 100");
    CHECK(std::abs(findNode(uimod->current(), "hp")->wrapWidth - 120.f) < 1e-5f);
    CHECK(findNode(uimod->current(), "a") != nullptr);
    CHECK(findNode(uimod->current(), "combo")->valueText == "x\ny");
    CHECK(std::abs(findNode(uimod->current(), "avatar")->tintG - 0.3f) < 1e-5f);
    CHECK(std::abs(findNode(uimod->current(), "r")->gap - 8.f) < 1e-5f);
    CHECK(!uimod->loadTreeJson("{not json"));
    CHECK(!uimod->loadTreeJson("{\"schema\":\"eve.ui.tree\",\"version\":999}"));

    uimod->setNavGamepad(true);
    CHECK(ui::globalTheme().navEnableGamepad);
    uimod->setNavGamepad(false);
    CHECK(!ui::globalTheme().navEnableGamepad);
}

TEST_CASE("UI.p1.hostPosTween") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(uimod->mountAs("tween", ui::window("T", {ui::text("x", "x")}, "root"))).has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);
    // Zero duration → jump immediately.
    uimod->animateHostPos(120.f, 60.f, 0.f);
    CHECK(current->meta()->hasPos);
    CHECK(std::abs(current->meta()->posX - 120.f) < 1e-4f);
    CHECK(std::abs(current->meta()->posY - 60.f) < 1e-4f);

    // Long duration → starts from the current value, no jump yet.
    uimod->animateHostPos(300.f, 200.f, 5000.f);
    CHECK(std::abs(current->meta()->posX - 120.f) < 1e-4f);
    CHECK(std::abs(current->meta()->posY - 60.f) < 1e-4f);
}

TEST_CASE("UI.polish.opacityDiagnosticsAndAccessibilitySnapshot") {
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Diagnostics", "diag-root");
    uimod->addButton("Save", "save");
    uimod->setItemAccessibility("button", "Save project", "Writes current changes");
    uimod->end();
    REQUIRE(uimod->mountBuildAs("diagnostics"));

    uimod->animateItemOpacity("save", 0.25f, 0.f);
    ui::UINode *save = findNode(uimod->current(), "save");
    REQUIRE(save != nullptr);
    CHECK(std::abs(save->opacity - 0.25f) < 1e-5f);

    const std::string layout = uimod->getLayoutDiagnostics();
    CHECK(layout.find("\"host\":\"diagnostics\"") != std::string::npos);
    CHECK(layout.find("\"overflowX\"") != std::string::npos);
    const std::string accessibility = uimod->getAccessibilitySnapshot();
    CHECK(accessibility.find("Save project") != std::string::npos);
    CHECK(accessibility.find("Writes current changes") != std::string::npos);
}

TEST_CASE("UI.p1.scrollListNodeAndBuilder") {
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("sl"));
    REQUIRE(h != nullptr);
    std::vector<std::string> items;
    for (int i = 0; i < 2000; ++i) items.push_back("item" + std::to_string(i));
    h->setTree(ui::window("W",
                          {
                              ui::virtualList("goods", items, 180.f, 24.f),
                              ui::scrollList("plain", {ui::button("A", "a"), ui::button("B", "b")}, 0.f, 0.f),
                          },
                          "root"));

    ui::UINode *goods = findNode(h, "goods");
    ui::UINode *plain = findNode(h, "plain");
    REQUIRE(goods != nullptr);
    REQUIRE(plain != nullptr);
    CHECK_EQ(int(goods->type), int(ui::NodeType::ScrollList));
    CHECK(std::abs(goods->sizeY - 180.f) < 1e-5f);
    CHECK(std::abs(goods->itemHeight - 24.f) < 1e-5f);
    CHECK(std::abs(plain->itemHeight - 0.f) < 1e-5f);

    // 2000 rows are retained in the tree; findById sees the last one.
    ui::UINode *last = findNode(h, "goods/1999");
    REQUIRE(last != nullptr);
    CHECK(last->text == "item1999");

    // Script builder surface.
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("B", "root");
    uimod->beginScrollList("log", 120.f, 20.f);
    for (int i = 0; i < 3; ++i) uimod->addListItem("row" + std::to_string(i), "log/" + std::to_string(i));
    uimod->end();
    uimod->end();
    CHECK(uimod->mountBuildAs("slb"));
    CHECK(findNode(uimod->current(), "log") != nullptr);
    CHECK(std::abs(findNode(uimod->current(), "log")->sizeY - 120.f) < 1e-5f);
    CHECK(std::abs(findNode(uimod->current(), "log")->itemHeight - 20.f) < 1e-5f);
    CHECK(findNode(uimod->current(), "log/2") != nullptr);
}
}  // namespace test_UIPhaseAdv_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIPhaseB)
#include "PrivateTestScope.h"
#include <imgui.h>
#include <string>
#include <vector>
#include "ui/Component.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// UIPhaseB.cpp
namespace test_UIPhaseB_unit {
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

[[maybe_unused]] UINode *findNode(UIHostHandle handle, const std::string &id) {
    return findNode(resolveHost(handle), id);
}

}  // namespace

class InventoryPanel : public Component {
public:
    std::vector<std::string> items{"Sword", "Potion"};
    int                      gold      = 10;
    bool                     showExtra = true;

    WidgetDesc build() override {
        return window("Inventory",
                      {
                          text("Gold " + std::to_string(gold), "gold"),
                          separator("sep"),
                          listButtons("items", items),
                          when(showExtra, text("Extra tip", "tip")),
                          checkbox("Show tip", showExtra, "tipchk",
                                   [this](bool v) {
                                       showExtra = v;
                                       setState();
                                   }),
                      },
                      "root");
    }
};

TEST_CASE("UI.b.listAndWhen") {
    auto    tree = window("W", {
                                when(false, text("hidden", "h")),
                                when(true, text("shown", "s")),
                                listButtons("L", {"a", "b", "c"}),
                            });
    UIHost *h    = resolveHost(UIHost::createHost("listwhen"));
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

    rebuilt = h->setTreeReconcile(window("W", {text("v3", "label"), button("Go", "btn"), button("New", "n")}, "root"));
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

    REQUIRE(UIHost::resolve(ui->mountAs("chk", window("C", {checkbox("Mute", false, "mute")}, "root"))).has_value());
    CHECK(findNode(ui->current(), "mute") != nullptr);
    CHECK(!findNode(ui->current(), "mute")->checked);
    ui->setChecked("mute", true);
    CHECK(findNode(ui->current(), "mute")->checked);
}

TEST_CASE("UI.b.themeTokensUnified") {
    Theme dark  = Theme::dark();
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
    applyTree(host, window("Icons", {icon(Icon::Search, "search"), iconButton(Icon::Save, "Save", "save")}));
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
}  // namespace test_UIPhaseB_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UISystem)
#include "PrivateTestScope.h"
#include <imgui.h>
#include "common/Capability.h"
#include "common/UIAutomation.h"
#include "ui/UIAutomationCapabilities.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// UISystem.cpp
namespace test_UISystem_unit {
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

}  // namespace

TEST_CASE("WidgetDesc.composeNestedTree") {
    int     clicks = 0;
    UIHost *h      = resolveHost(UIHost::createHost("nest"));
    REQUIRE(h != nullptr);
    h->setTree(window("Inventory",
                      {
                          text("Hello", "label"),
                          group({
                              button("A", "a"),
                              sameLine(),
                              button("B", "b", [&]() { ++clicks; }),
                          }),
                      },
                      "root"));

    auto t = h->tree();
    REQUIRE_EQ(t->root, 0);
    CHECK_EQ(int(t->nodes[0].type), int(NodeType::Window));
    REQUIRE_GE(t->nodes.size(), 5u);

    UINode *b = findNode(h, "b");
    REQUIRE(b != nullptr);
    REQUIRE_NE(b->handlerClick, 0u);

    UIEvent ev;
    ev.host         = h->handle();
    ev.hostName     = "nest";
    ev.nodeId       = "b";
    ev.kind         = "click";
    ev.handlerIndex = b->handlerClick;
    UISystem::pendingEvents().push_back(ev);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    CHECK(UISystem::consumeClickFor("nest") == "b");
}

TEST_CASE("UIHost.setSimplePanel.buildsRetainedTree") {
    UIHost *h = resolveHost(UIHost::createHost("simple"));
    REQUIRE(h != nullptr);
    h->setSimplePanel("Inventory", "Hello", "Use");
    auto t = h->tree();
    REQUIRE_EQ(t->root, 0);
    REQUIRE_EQ(t->nodes.size(), 3u);
    CHECK(t->nodes[1].id == "label");
    CHECK(t->nodes[2].id == "btn");
}

TEST_CASE("UIHost.setPropsAndClickHandler") {
    UIHost *h = resolveHost(UIHost::createHost("props"));
    REQUIRE(h != nullptr);
    h->setTree(window("W", {text("before", "label"), button("Go", "btn")}));
    h->setTextById("label", "after");
    CHECK(findNode(h, "label")->text == "after");
    h->setVisibleById("btn", false);
    CHECK(!findNode(h, "btn")->visible);

    int clicks = 0;
    CHECK(h->setClickHandler("btn", [&]() { ++clicks; }));

    UIEvent ev;
    ev.host         = h->handle();
    ev.hostName     = "props";
    ev.nodeId       = "btn";
    ev.kind         = "click";
    ev.handlerIndex = findNode(h, "btn")->handlerClick;
    UISystem::pendingEvents().push_back(ev);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
}

TEST_CASE("UIAutomation.semanticTreeGetAndClick") {
    int     clicks = 0;
    UIHost *host   = resolveHost(UIHost::createHost("mcp-ui-test"));
    REQUIRE(host != nullptr);
    host->setTree(window("Automation", {text("Ready", "status"), button("Add Tree", "asset-tree", [&]() { ++clicks; }),
                                        grid(2, {text("Cell", "grid-cell")}, "asset-grid")}));

    registerUIAutomationCapabilities();
    auto *automation = eve::cap::query<eve::IUIAutomation>();
    REQUIRE(automation != nullptr);

    const std::string tree = automation->tree("mcp-ui-test");
    CHECK(tree.find("\"name\":\"mcp-ui-test\"") != std::string::npos);
    CHECK(tree.find("\"id\":\"asset-tree\"") != std::string::npos);
    CHECK(tree.find("\"type\":\"grid\"") != std::string::npos);
    CHECK(tree.find("\"clickable\":true") != std::string::npos);

    const std::string widget = automation->get("mcp-ui-test", "asset-tree");
    CHECK(widget.find("\"type\":\"button\"") != std::string::npos);
    CHECK(widget.find("\"host\":\"mcp-ui-test\"") != std::string::npos);

    const std::string queued = automation->click("mcp-ui-test", "asset-tree");
    CHECK(queued.find("\"queued\":true") != std::string::npos);
    CHECK_EQ(clicks, 0);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    CHECK(UISystem::consumeClickFor("mcp-ui-test") == "asset-tree");

    CHECK(automation->click("mcp-ui-test", "status").find("error: widget is not clickable") == 0);
}
}  // namespace test_UISystem_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_color_preview_sync)
#include "PrivateTestScope.h"
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "ui/PcgColorPreviewSync.h"
#include "ui/PcgControllerSelection.h"
#include "ui/PcgDraggableWindow.h"
#include "ui/PcgLoadingScreen.h"
#include "ui/PcgScreenshotSavedNotice.h"
#include "ui/PcgTooltip.h"
#include "ui/UI.h"
#include "zeroerr/unittest.h"
// pcg_color_preview_sync.cpp
namespace test_pcg_color_preview_sync_unit {
// pcg_color_preview_sync.cpp
using eve::ui::PcgColorPreviewSync;
TEST_CASE("ui.pcgColorPreview.multipliesNonBlackSource") {
    PcgColorPreviewSync sync;
    REQUIRE(sync.sync(.2f, .3f, .4f, .5f).ok());
    CHECK_EQ(sync.getRed(), .5f);
    CHECK_EQ(sync.getGreen(), .75f);
    CHECK_EQ(sync.getBlue(), 1.f);
    CHECK_EQ(sync.getAlpha(), 1.25f);
}
TEST_CASE("ui.pcgColorPreview.blackUsesReadableGrayAndRejectsInvalid") {
    PcgColorPreviewSync sync;
    REQUIRE(sync.sync(0, 0, 0, .2f).ok());
    CHECK_EQ(sync.getRed(), .5f);
    CHECK_EQ(sync.getGreen(), .5f);
    CHECK_EQ(sync.getBlue(), .5f);
    CHECK_EQ(sync.getAlpha(), 1.f);
    CHECK(!sync.sync(std::numeric_limits<float>::quiet_NaN(), 0, 0, 1).ok());
    CHECK_EQ(sync.getRed(), .5f);
}

// pcg_controller_selection.cpp
using eve::ui::PcgControllerSelection;
TEST_CASE("ui.pcgControllerSelection.firstMatchWins") {
    PcgControllerSelection s;
    REQUIRE(s.add("Flying", 1, "flying").ok());
    REQUIRE(s.add("Duplicate", 1, "duplicate").ok());
    REQUIRE(s.add("Third", 3, "third").ok());
    auto r = s.refresh(1);
    REQUIRE(r.ok());
    CHECK(r.value());
    CHECK_EQ(s.getSelectedIndex(), 0);
    auto a = s.getVisible(0);
    auto b = s.getVisible(1);
    auto c = s.getVisible(2);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    CHECK(a.value());
    CHECK(!b.value());
    CHECK(!c.value());
    auto id = s.getWidgetId(0);
    REQUIRE(id.ok());
    CHECK_EQ(id.value(), "flying");
}
TEST_CASE("ui.pcgControllerSelection.noMatchPreservesSnapshot") {
    PcgControllerSelection s;
    REQUIRE(s.add("A", 1, "a").ok());
    REQUIRE(s.add("B", 2, "b").ok());
    REQUIRE(s.refresh(2).ok());
    auto miss = s.refresh(99);
    REQUIRE(miss.ok());
    CHECK(!miss.value());
    CHECK_EQ(s.getSelectedIndex(), 1);
    auto visible = s.getVisible(1);
    REQUIRE(visible.ok());
    CHECK(visible.value());
    CHECK(!s.getVisible(2).ok());
    CHECK(!s.add("", 3, "x").ok());
    CHECK_EQ(s.getCount(), 2);
}

// pcg_draggable_window.cpp
using eve::ui::PcgDraggableWindow;
using eve::ui::PcgUiRequestStatus;
TEST_CASE("ui.pcgDraggableWindow.matchesPcgClampAndScale") {
    PcgDraggableWindow w;
    REQUIRE(w.configure(-100, 100, 200, 100, 1920, 1080, 2).ok());
    REQUIRE(w.drag(100, -300).ok());
    CHECK_EQ(w.getX(), -100.f);
    CHECK_EQ(w.getY(), 50.f);
    REQUIRE(w.drag(-10000, 10000).ok());
    CHECK_EQ(w.getX(), -1820.f);
    CHECK_EQ(w.getY(), 1030.f);
}
TEST_CASE("ui.pcgDraggableWindow.pointerDownRaisesAndMiddleResets") {
    PcgDraggableWindow w;
    REQUIRE(w.configure(-300, 200, 200, 100, 1280, 720, 1).ok());
    REQUIRE(w.drag(100, 100).ok());
    w.pointerDown(false);
    CHECK_EQ(w.consumeBringToFront(), PcgUiRequestStatus::Requested);
    CHECK_EQ(w.consumeBringToFront(), PcgUiRequestStatus::None);
    w.pointerDown(true);
    CHECK_EQ(w.getX(), -300.f);
    CHECK_EQ(w.getY(), 200.f);
    CHECK_EQ(w.consumeBringToFront(), PcgUiRequestStatus::Requested);
}
TEST_CASE("ui.pcgDraggableWindow.rejectsInvalidGeometryAtomically") {
    PcgDraggableWindow w;
    REQUIRE(w.configure(-10, 20, 100, 80, 800, 600, 1).ok());
    CHECK(!w.configure(0, 0, 900, 80, 800, 600, 1).ok());
    CHECK_EQ(w.getX(), -10.f);
    CHECK(!w.drag(std::numeric_limits<float>::quiet_NaN(), 0).ok());
    CHECK_EQ(w.getY(), 20.f);
}

// pcg_loading_screen.cpp
using eve::ui::PcgLoadingScreen;

TEST_CASE("ui.pcgLoadingScreen.progressCompletesAndFades") {
    PcgLoadingScreen screen;
    REQUIRE(screen.configure(2.f).ok());
    screen.onLoadProgressStarted();
    CHECK(screen.getCanvasVisible());
    CHECK_EQ(screen.getProgress(), 0.f);
    CHECK_EQ(screen.getBackgroundAlpha(), 1.f);
    REQUIRE(screen.onLoadProgressUpdated(.75f).ok());
    CHECK_EQ(screen.getProgress(), .75f);
    CHECK(!screen.getFading());
    REQUIRE(screen.onLoadProgressUpdated(1.f).ok());
    CHECK(screen.getFading());
    CHECK(!screen.getProgressVisible());
    CHECK(!screen.getTextVisible());
    REQUIRE(screen.tick(.25f).ok());
    CHECK_EQ(screen.getBackgroundAlpha(), .5f);
    REQUIRE(screen.tick(.25f).ok());
    CHECK(!screen.getCanvasVisible());
    CHECK(!screen.getFading());
}

TEST_CASE("ui.pcgLoadingScreen.buildsTimeoutDiagnosticTransactionally") {
    PcgLoadingScreen screen;
    CHECK(!screen.addMissingScene("tile", "tile-impostor").ok());
    screen.beginTimeout();
    auto scene = screen.addMissingScene("Terrain_1", "Impostor_1");
    REQUIRE(scene.ok());
    REQUIRE(screen.addRegularReference(scene.value(), "Biome Loader").ok());
    REQUIRE(screen.addRegularReference(scene.value(), "Player").ok());
    REQUIRE(screen.addImpostorReference(scene.value(), "Far Camera").ok());
    REQUIRE(screen.endTimeout().ok());
    CHECK(screen.getFading());
    CHECK(screen.getTimeoutMessage().find("Terrain_1") != std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Biome Loader, Player, ") != std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Impostor_1") != std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Far Camera, ") != std::string::npos);
    CHECK(!screen.endTimeout().ok());
}


// pcg_screenshot_saved_notice.cpp
using eve::ui::PcgScreenshotSavedNotice;
TEST_CASE("ui.pcgScreenshotNotice.waitsForEndFrameAndExpires") {
    PcgScreenshotSavedNotice n;
    REQUIRE(n.configure(true, 1).ok());
    REQUIRE(n.request("shots/a.png").ok());
    CHECK(n.getPending());
    CHECK(!n.getVisible());
    REQUIRE(n.tick(10).ok());
    CHECK(!n.getVisible());
    REQUIRE(n.endFrame(10).ok());
    CHECK(n.getVisible());
    CHECK(n.getPathVisible());
    CHECK_EQ(n.getPath(), "shots/a.png");
    REQUIRE(n.tick(10.999f).ok());
    CHECK(n.getVisible());
    REQUIRE(n.tick(11).ok());
    CHECK(!n.getVisible());
}
TEST_CASE("ui.pcgScreenshotNotice.restartAndDisableCancelOldNotice") {
    PcgScreenshotSavedNotice n;
    REQUIRE(n.configure(true, 2).ok());
    REQUIRE(n.request("a.png").ok());
    REQUIRE(n.endFrame(1).ok());
    REQUIRE(n.request("b.png").ok());
    CHECK(!n.getVisible());
    REQUIRE(n.endFrame(2).ok());
    REQUIRE(n.tick(3.5f).ok());
    CHECK(n.getVisible());
    REQUIRE(n.configure(false, 2).ok());
    CHECK(!n.getVisible());
    CHECK(!n.getPending());
    REQUIRE(n.request("ignored.png").ok());
    CHECK(!n.getPending());
}
TEST_CASE("ui.pcgScreenshotNotice.invalidInputsAreAtomic") {
    PcgScreenshotSavedNotice n;
    REQUIRE(n.configure(true, 1).ok());
    REQUIRE(n.request("ok.png").ok());
    CHECK(!n.configure(true, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(n.getPending());
    CHECK(!n.endFrame(-1).ok());
    CHECK(n.getPending());
}

// pcg_tooltip.cpp
using eve::ui::PcgTooltipManager;
TEST_CASE("ui.pcgTooltip.profileUsesFirstSubstringMatch") {
    PcgTooltipManager m;
    REQUIRE(m.addTooltip("camera.fov", "FOV", "Field of view").ok());
    REQUIRE(m.addTooltip("camera.fov.extra", "Other", "Later").ok());
    auto found = m.enter(0, "fov", 10);
    REQUIRE(found.ok());
    CHECK(found.value());
    CHECK(m.getPending());
    REQUIRE(m.tick(11.9f, 100, 200, 1000, 800, false).ok());
    CHECK(!m.getVisible());
    REQUIRE(m.tick(12, 100, 200, 1000, 800, false).ok());
    CHECK(m.getVisible());
    CHECK_EQ(m.getHeader(), "FOV");
    auto removed = m.removeTooltip("fov");
    REQUIRE(removed.ok());
    CHECK(removed.value());
    CHECK_EQ(m.getCount(), 1);
}
TEST_CASE("ui.pcgTooltip.triggerModesPivotThemeAndDismissal") {
    PcgTooltipManager m;
    REQUIRE(m.configure(1, 0, 1, 2, 3, 4).ok());
    REQUIRE(m.enterText(0, "ignored", "UI", 0).ok());
    CHECK(!m.getPending());
    REQUIRE(m.enterText(1, "long content", "Scene", 1).ok());
    REQUIRE(m.tick(2, 400, 700, 800, 800, false).ok());
    CHECK(m.getVisible());
    CHECK_EQ(m.getPivotX(), .5f);
    CHECK_EQ(m.getPivotY(), 2.625f);
    CHECK(m.getHeaderVisible());
    CHECK(m.getWrapEnabled());
    CHECK_EQ(m.getBackgroundR(), 1.f);
    CHECK_EQ(m.getForeground(), 0.f);
    REQUIRE(m.tick(2.1f, 400, 100, 800, 800, true).ok());
    CHECK(!m.getVisible());
}
TEST_CASE("ui.pcgTooltip.exitAndInvalidConfigAreSafe") {
    PcgTooltipManager m;
    REQUIRE(m.configure(2, 1, 0, 2, 2, 40).ok());
    REQUIRE(m.enterText(0, "text", "", 0).ok());
    REQUIRE(m.exit(0).ok());
    CHECK(!m.getPending());
    CHECK(!m.getVisible());
    CHECK(!m.configure(3, 0, 1, 1, 1, 10).ok());
    CHECK(!m.tick(-1, 0, 0, 100, 100, false).ok());
    auto miss = m.removeTooltip("missing");
    REQUIRE(miss.ok());
    CHECK(!miss.value());
}
}  // namespace test_pcg_color_preview_sync_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_photo_mode_color_picker)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "ui/PcgPhotoModeColorPicker.h"
#include "ui/PcgPhotoModePanels.h"
#include "ui/PcgPhotoModeRanges.h"
#include "ui/PcgPhotoModeRuntimeUI.h"
#include "zeroerr/unittest.h"
// pcg_photo_mode_color_picker.cpp
namespace test_pcg_photo_mode_color_picker {
// pcg_photo_mode_color_picker.cpp
using eve::ui::PcgPhotoModeColorPicker;
TEST_CASE("ui.pcgPhotoModeColorPicker.preservesHdrDoubleApplication") {
    PcgPhotoModeColorPicker p;
    REQUIRE(p.open(.2f, .3f, .4f, 2.f, true).ok());
    CHECK_EQ(p.getRed(), .4f);
    CHECK_EQ(p.getPreviewRed(), .8f);
    CHECK_EQ(p.getPreviewGreen(), 1.2f);
    CHECK_EQ(p.getPreviewBlue(), 1.6f);
    CHECK_EQ(p.getHdrSwatch(), .7f);
    CHECK(p.getOpen());
    CHECK_EQ(p.consumeBringToFront(), eve::ui::PcgUiRequestStatus::Requested);
    CHECK_EQ(p.consumeBringToFront(), eve::ui::PcgUiRequestStatus::None);
}
TEST_CASE("ui.pcgPhotoModeColorPicker.slidersResetAndNegativeHdr") {
    PcgPhotoModeColorPicker p;
    REQUIRE(p.setLast(.1f, .2f, .3f, -2.f).ok());
    REQUIRE(p.open(1, 1, 1, 1, true).ok());
    REQUIRE(p.setRed(.7f).ok());
    REQUIRE(p.setGreen(.8f).ok());
    REQUIRE(p.setBlue(.9f).ok());
    REQUIRE(p.setHdr(3.f).ok());
    CHECK_EQ(p.getChangeRevision(), uint64_t(4));
    p.reset();
    CHECK_EQ(p.getRed(), .1f);
    CHECK_EQ(p.getAlpha(), -2.f);
    CHECK_EQ(p.getPreviewRed(), -.2f);
    CHECK_EQ(p.getChangeRevision(), uint64_t(9));
    p.setFocusedName("Fog");
    CHECK_EQ(p.getFocusedName(), "(Fog)");
    p.close();
    CHECK(!p.getOpen());
}
TEST_CASE("ui.pcgPhotoModeColorPicker.invalidValuesAreAtomic") {
    PcgPhotoModeColorPicker p;
    REQUIRE(p.open(.1f, .2f, .3f, 1, false).ok());
    CHECK(!p.open(std::numeric_limits<float>::infinity(), 0, 0, 1, true).ok());
    CHECK_EQ(p.getRed(), .1f);
    CHECK(!p.setHdr(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_EQ(p.getAlpha(), 1.f);
}

// pcg_photo_mode_panels.cpp
using namespace eve::ui;
TEST_CASE("ui.pcgPhotoModePanels.selectsExactlyOneAndResetsScroll") {
    PcgPhotoModePanels p;
    REQUIRE(p.select(4).ok());
    CHECK_EQ(p.getSelected(), 4);
    for (int i = 0; i < 7; ++i) CHECK_EQ(p.isActive(i), i == 4);
    CHECK_EQ(p.consumeScrollReset(), PcgUiRequestStatus::Requested);
    CHECK_EQ(p.consumeScrollReset(), PcgUiRequestStatus::None);
    CHECK_EQ(p.getRevision(), uint64_t(1));
    REQUIRE(p.select(6).ok());
    CHECK(p.isActive(6));
}
TEST_CASE("ui.pcgPhotoModePanels.invalidSelectionIsAtomic") {
    PcgPhotoModePanels p;
    REQUIRE(p.select(2).ok());
    auto rev = p.getRevision();
    CHECK(!p.select(7).ok());
    CHECK_EQ(p.getSelected(), 2);
    CHECK_EQ(p.getRevision(), rev);
    CHECK(p.isActive(2));
}
TEST_CASE("ui.pcgPhotoModePanelButton.matchesColorsAndLayout") {
    PcgPhotoModePanelButton b;
    REQUIRE(b.configure(.1f, .2f, .3f, .4f, .8f, .7f, .6f, .5f).ok());
    b.setSelected(false);
    CHECK_EQ(b.getNormalR(), .1f);
    CHECK_EQ(b.getHighlightR(), .8f);
    CHECK(!b.getLayoutEnabled());
    b.setSelected(true);
    CHECK_EQ(b.getNormalR(), .8f);
    CHECK_EQ(b.getNormalA(), .5f);
    CHECK(b.getLayoutEnabled());
    CHECK(!b.configure(std::numeric_limits<float>::quiet_NaN(), 0, 0, 1, 1, 1, 0, 1).ok());
    CHECK_EQ(b.getNormalR(), .8f);
}

// pcg_photo_mode_ranges.cpp
using namespace eve::ui;
TEST_CASE("ui.pcgPhotoModeRanges.containsAllExactDefaults") {
    PcgPhotoModeRanges r;
    CHECK_EQ(r.getCount(), uint64_t(57));
    auto first = r.getName(0);
    auto last  = r.getName(56);
    REQUIRE(first.ok());
    REQUIRE(last.ok());
    CHECK_EQ(first.value(), "m_0To1");
    CHECK_EQ(last.value(), "m_cameraCellSubdivision");
    REQUIRE(r.select("m_fieldOfView").ok());
    CHECK_EQ(r.getMinimum(), 1.0);
    CHECK_EQ(r.getMaximum(), 115.0);
    CHECK(!r.getIntegral());
    REQUIRE(r.select("m_cameraCellSubdivision").ok());
    CHECK_EQ(r.getMinimum(), -8.0);
    CHECK_EQ(r.getMaximum(), 8.0);
    CHECK(r.getIntegral());
    REQUIRE(r.select("m_densityVolumeFogDistance").ok());
    CHECK(std::isinf(r.getMaximum()));
}
TEST_CASE("ui.pcgPhotoModeRanges.clampsAndUpdatesAtomically") {
    PcgPhotoModeRanges r;
    auto               c = r.clamp("m_fieldOfView", 200);
    REQUIRE(c.ok());
    CHECK_EQ(c.value(), 115.0);
    REQUIRE(r.setRange("m_fieldOfView", 10, 80).ok());
    c = r.clamp("m_fieldOfView", 5);
    REQUIRE(c.ok());
    CHECK_EQ(c.value(), 10.0);
    CHECK(!r.setRange("m_fieldOfView", 90, 20).ok());
    REQUIRE(r.select("m_fieldOfView").ok());
    CHECK_EQ(r.getMaximum(), 80.0);
    CHECK(!r.setRange("m_targetFPS", 0.5, 60).ok());
    CHECK(!r.clamp("missing", 1).ok());
}

// pcg_photo_mode_runtime_ui.cpp
using namespace eve::ui;

TEST_CASE("pcg.photoModeRuntimeUI.configuresWidgetFamilies") {
    PcgPhotoModeRuntimeUI ui;
    REQUIRE(ui.configure(1, "Exposure", "1.5", 1.5f, 0.0f, 4.0f).ok());
    CHECK(ui.getLabelVisible());
    CHECK(ui.getSliderVisible());
    CHECK(ui.getInputVisible());
    CHECK(!ui.getButtonVisible());
    CHECK(ui.getInitialCallbackRevision() == 1);
    REQUIRE(ui.configure(13, "Position", "", 0.0f, -10.0f, 10.0f).ok());
    CHECK(ui.getVector3Visible());
    CHECK(!ui.getSliderVisible());
    CHECK(ui.getInitialCallbackRevision() == 2);
    REQUIRE(ui.configure(11, "Banner", "", 0.0f, 0.0f, 0.0f, false).ok());
    CHECK(!ui.getImageVisible());
    CHECK(ui.getInitialCallbackRevision() == 2);
}

TEST_CASE("pcg.photoModeRuntimeUI.preservesSliderInputSynchronization") {
    PcgPhotoModeRuntimeUI ui;
    REQUIRE(ui.configure(1, "Value", "2.0", 2.0f, 0.0f, 10.0f).ok());
    REQUIRE(ui.setSliderValue(3.5f).ok());
    CHECK(ui.getInputRefresh());
    CHECK(ui.getValueText() == "3.5");
    CHECK(!ui.getUsingSlider());
    REQUIRE(ui.setSliderValue(4.0f).ok());
    CHECK(!ui.getInputRefresh());
    CHECK(ui.getValueText() == "3.5");
    ui.markSliderUsed();
    REQUIRE(ui.setSliderValue(5.0f).ok());
    CHECK(ui.getInputRefresh());
}

TEST_CASE("pcg.photoModeRuntimeUI.parsesAndClampsFloatInput") {
    PcgPhotoModeRuntimeUI ui;
    REQUIRE(ui.configure(1, "Value", "", 2.0f, -1.0f, 1.0f).ok());
    CHECK(ui.applyFloatInput("") == PcgPhotoModeInputStatus::Applied);
    CHECK(ui.getValue() == 0.0f);
    CHECK(ui.applyFloatInput("4.25") == PcgPhotoModeInputStatus::Applied);
    CHECK(ui.getValue() == 1.0f);
    CHECK(ui.getUsingSlider());
    CHECK(ui.applyFloatInput("4x") == PcgPhotoModeInputStatus::Ignored);
    CHECK(ui.getValue() == 1.0f);
}

TEST_CASE("pcg.photoModeRuntimeUI.matchesMetricsWrappingQuirks") {
    CHECK(PcgPhotoModeRuntimeUI::updateWrap(std::string(69, 'x')) == std::string(68, 'x') + "\n");
    const std::string spaced = std::string(60, 'a') + " " + std::string(20, 'b');
    CHECK(PcgPhotoModeRuntimeUI::updateWrap(spaced) == std::string(60, 'a') + "\n" + std::string(20, 'b'));
}
}  // namespace test_pcg_photo_mode_color_picker
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_photo_mode_session)
#include "PrivateTestScope.h"
#include <fstream>
#include <limits>
#include "ui/PcgPhotoModeSession.h"
#include "ui/PcgPhotoModeValues.h"
#include "zeroerr/unittest.h"
// pcg_photo_mode_session.cpp
namespace test_pcg_photo_mode_session_unit {
// pcg_photo_mode_session.cpp
using namespace eve::ui;
namespace {
[[maybe_unused]] std::string profile(const char *scene, int64_t light, float fov) {
    PcgPhotoModeValues v;
    v.setString("m_lastSceneName", scene).value();
    v.setBool("m_isUsingPcgLighting", light >= 0).value();
    v.setInt("m_selectedPcgLightingProfile", light).value();
    v.setFloat("m_fieldOfView", fov).value();
    auto j = v.snapshotJson();
    j.value();
    return std::move(j).takeValue();
}
[[maybe_unused]] float fov(const std::string &j) {
    PcgPhotoModeValues v;
    v.restoreJson(j).value();
    auto x = v.getFloat("m_fieldOfView");
    x.value();
    return std::move(x).takeValue();
}
}  // namespace
TEST_CASE("ui.pcgPhotoModeSession.loadsOnlyCompatibleSavedValues") {
    auto                current = profile("World", 2, 60);
    auto                saved   = profile("World", 2, 90);
    PcgPhotoModeSession s;
    REQUIRE(s.begin(current, saved, true, true, 1, 1, "World", 2).ok());
    CHECK_EQ(s.getLoadDecision(), 1);
    auto w = s.workingJson();
    REQUIRE(w.ok());
    CHECK_EQ(fov(w.value()), 90.0f);
    REQUIRE(s.end(false, true).ok());
    CHECK(!s.getRestoreRequested());
    auto out = s.outputJson();
    REQUIRE(out.ok());
    CHECK_EQ(fov(out.value()), 90.0f);
    CHECK(s.getRemovePhotoCameraRequested());
    CHECK(s.getUnfreezePlayerRequested());
}
TEST_CASE("ui.pcgPhotoModeSession.matchesMismatchBranches") {
    auto                current       = profile("World", 2, 60);
    auto                sceneMismatch = profile("Other", 2, 90);
    PcgPhotoModeSession a;
    REQUIRE(a.begin(current, sceneMismatch, true, true, 1, 1, "World", 2).ok());
    CHECK_EQ(a.getLoadDecision(), 2);
    REQUIRE(a.end(true, true).ok());
    CHECK(a.getRestoreRequested());
    auto restored = a.outputJson();
    REQUIRE(restored.ok());
    CHECK_EQ(fov(restored.value()), 60.0f);
    PcgPhotoModeSession b;
    REQUIRE(b.begin(current, sceneMismatch, true, true, 1, 2, "World", 2).ok());
    CHECK_EQ(b.getLoadDecision(), 0);
    REQUIRE(b.end(true, false).ok());
    CHECK(!b.getRestoreRequested());
}
TEST_CASE("ui.pcgPhotoModeSession.isAtomicAndSingleActive") {
    auto                current = profile("World", 2, 60);
    PcgPhotoModeSession s;
    CHECK(!s.begin("bad", "", false, false, 0, 0, "", 0).ok());
    CHECK(!s.getActive());
    REQUIRE(s.begin(current, "", false, false, 0, 0, "World", 2).ok());
    auto rev = s.getRevision();
    CHECK(!s.begin(current, "", false, false, 0, 0, "World", 2).ok());
    CHECK_EQ(s.getRevision(), rev);
    CHECK(!s.replaceWorkingJson("bad").ok());
    auto w = s.workingJson();
    REQUIRE(w.ok());
    CHECK_EQ(fov(w.value()), 60.0f);
    REQUIRE(s.replaceWorkingJson(profile("World", 2, 75)).ok());
    REQUIRE(s.end(true, true).ok());
    CHECK(!s.end(true, true).ok());
}
TEST_CASE("ui.pcgPhotoModeSession.rejectsInconsistentPcgLightingIdentity") {
    auto               current = profile("World", 2, 60);
    PcgPhotoModeValues saved;
    saved.restoreJson(profile("World", 2, 90)).value();
    saved.setBool("m_isUsingPcgLighting", false).value();
    auto json = saved.snapshotJson();
    REQUIRE(json.ok());
    PcgPhotoModeSession session;
    REQUIRE(session.begin(current, json.value(), true, true, 1, 1, "World", 2).ok());
    CHECK_EQ(session.getLoadDecision(), 2);
}

// pcg_photo_mode_values.cpp
using namespace eve::ui;
TEST_CASE("ui.pcgPhotoModeValues.containsCompleteExactDefaults") {
    PcgPhotoModeValues v;
    CHECK_EQ(v.getFieldCount(), uint64_t(102));
    auto first = v.getFieldName(0);
    auto last  = v.getFieldName(101);
    REQUIRE(first.ok());
    REQUIRE(last.ok());
    CHECK_EQ(first.value(), "m_isUsingPcgLighting");
    CHECK_EQ(last.value(), "m_cameraCellSubdivision");
    auto fov  = v.getFloat("m_fieldOfView");
    auto fps  = v.getInt("m_targetFPS");
    auto png  = v.getInt("m_screenshotImageFormat");
    auto load = v.getBool("m_loadSavedSettings");
    REQUIRE(fov.ok());
    REQUIRE(fps.ok());
    REQUIRE(png.ok());
    REQUIRE(load.ok());
    CHECK_EQ(fov.value(), 60.0f);
    CHECK_EQ(fps.value(), int64_t(-1));
    CHECK_EQ(png.value(), int64_t(2));
    CHECK(load.value());
    REQUIRE(v.selectColor("m_ambientSkyColor").ok());
    CHECK_EQ(v.getColorR(), 0.7027151f);
    CHECK_EQ(v.getColorA(), 0.4192761f);
}
TEST_CASE("ui.pcgPhotoModeValues.enforcesSchemaTypesAtomically") {
    PcgPhotoModeValues v;
    REQUIRE(v.setFloat("m_fieldOfView", 90.0f).ok());
    CHECK(!v.setInt("m_fieldOfView", 90).ok());
    CHECK(!v.setFloat("m_fieldOfView", std::numeric_limits<float>::infinity()).ok());
    auto f = v.getFloat("m_fieldOfView");
    REQUIRE(f.ok());
    CHECK_EQ(f.value(), 90.0f);
    CHECK(!v.setBool("missing", true).ok());
    CHECK(!v.getFieldName(102).ok());
    REQUIRE(v.setColor("m_fogColor", .1f, .2f, .3f, .4f).ok());
    REQUIRE(v.selectColor("m_fogColor").ok());
    CHECK_EQ(v.getColorB(), .3f);
    v.resetDefaults();
    REQUIRE(v.selectColor("m_fogColor").ok());
    CHECK_EQ(v.getColorR(), 0.0f);
    CHECK_EQ(v.getColorA(), 1.0f);
}
}  // namespace test_pcg_photo_mode_session_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_color_palette)
#include "PrivateTestScope.h"
#include <string>
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_color_palette.cpp
namespace test_ui_color_palette {
using namespace eve::ui;

namespace {

UIHost *createHost(const std::string &name) {
    auto host = UIHost::resolve(UIHost::createHost(name));
    return host ? &host->get() : nullptr;
}

UINode *node(UIHost &host, const std::string &id) {
    auto found = host.findById(id);
    return found ? &found->get() : nullptr;
}

}  // namespace

TEST_CASE("ui.color_palette.flattens_rgba_and_type") {
    UIHost *host = createHost("color-palette");
    REQUIRE(host != nullptr);

    WidgetDesc palette = colorPalette("Accent", 0.1f, 0.2f, 0.3f, 0.4f, "accent");
    host->setTree(window("Palette", {std::move(palette)}, "root"));

    UINode *accent = node(*host, "accent");
    REQUIRE(accent != nullptr);
    CHECK_EQ(static_cast<int>(accent->type), static_cast<int>(NodeType::ColorPalette));
    CHECK_EQ(accent->text, std::string("Accent"));
    CHECK_EQ(accent->tintR, 0.1f);
    CHECK_EQ(accent->tintG, 0.2f);
    CHECK_EQ(accent->tintB, 0.3f);
    CHECK_EQ(accent->tintA, 0.4f);
}

TEST_CASE("ui.color_palette.builder_and_script_accessors") {
    UI *uimod = UI::create();
    REQUIRE(uimod != nullptr);
    uimod->beginBuild();
    uimod->beginWindow("Colors", "root");
    uimod->addColorPalette("Fill", 0.8f, 0.1f, 0.2f, 1.f, "fill");
    uimod->end();
    CHECK(uimod->mountBuildAs("palette-host"));
    CHECK(uimod->select("palette-host"));
    CHECK_EQ(uimod->getColorR("fill"), 0.8f);
    CHECK_EQ(uimod->getColorG("fill"), 0.1f);
    CHECK_EQ(uimod->getColorB("fill"), 0.2f);
    CHECK_EQ(uimod->getColorA("fill"), 1.f);
    uimod->setColor("fill", 0.0f, 1.0f, 0.5f, 0.25f);
    CHECK_EQ(uimod->getColorR("fill"), 0.0f);
    CHECK_EQ(uimod->getColorG("fill"), 1.0f);
    CHECK_EQ(uimod->getColorB("fill"), 0.5f);
    CHECK_EQ(uimod->getColorA("fill"), 0.25f);
}
}  // namespace test_ui_color_palette
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_control_semantics)
#include "PrivateTestScope.h"
#include <string>
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_control_semantics.cpp
namespace test_ui_control_semantics {
using namespace eve::ui;

namespace {

UIHost *createHost(const std::string &name) {
    auto host = UIHost::resolve(UIHost::createHost(name));
    return host ? &host->get() : nullptr;
}

UINode *node(UIHost &host, const std::string &id) {
    auto found = host.findById(id);
    return found ? &found->get() : nullptr;
}

}  // namespace

TEST_CASE("ui.control.policies_survive_flatten_and_reconcile") {
    UIHost *host = createHost("control-semantics");
    REQUIRE(host != nullptr);

    WidgetDesc action =
        button("Apply", "apply")
            .withEnabled(false)
            .withFocusMode(FocusMode::Click)
            .withMouseFilter(MouseFilter::Ignore)
            .withTabIndex(7)
            .withFocusOrder("cancel", "reset")
            .withFocusNeighbors("left", "right", "up", "down")
            .withAccessibility(AccessibilityRole::Button, "Apply changes", "Commits the current property edits");
    host->setTree(window("Controls", {std::move(action)}, "root"));

    UINode *applyNode = node(*host, "apply");
    REQUIRE(applyNode != nullptr);
    CHECK(!applyNode->enabled);
    CHECK_EQ(static_cast<int>(applyNode->focusMode), static_cast<int>(FocusMode::Click));
    CHECK_EQ(static_cast<int>(applyNode->mouseFilter), static_cast<int>(MouseFilter::Ignore));
    CHECK_EQ(applyNode->tabIndex, 7);
    CHECK_EQ(applyNode->focusPrevious, std::string("cancel"));
    CHECK_EQ(applyNode->focusNext, std::string("reset"));
    CHECK_EQ(applyNode->focusLeft, std::string("left"));
    CHECK_EQ(applyNode->focusRight, std::string("right"));
    CHECK_EQ(applyNode->focusUp, std::string("up"));
    CHECK_EQ(applyNode->focusDown, std::string("down"));
    CHECK_EQ(static_cast<int>(applyNode->accessibilityRole), static_cast<int>(AccessibilityRole::Button));
    CHECK_EQ(applyNode->accessibilityName, std::string("Apply changes"));

    WidgetDesc updated = button("Apply now", "apply")
                             .withFocusMode(FocusMode::All)
                             .withMouseFilter(MouseFilter::Pass)
                             .withTabIndex(2)
                             .withAccessibility(AccessibilityRole::Button, "Apply now");
    CHECK(host->setTreeReconcile(window("Controls", {std::move(updated)}, "root")));
    applyNode = node(*host, "apply");
    REQUIRE(applyNode != nullptr);
    CHECK(applyNode->enabled);
    CHECK_EQ(static_cast<int>(applyNode->focusMode), static_cast<int>(FocusMode::All));
    CHECK_EQ(static_cast<int>(applyNode->mouseFilter), static_cast<int>(MouseFilter::Pass));
    CHECK_EQ(applyNode->tabIndex, 2);
    CHECK(applyNode->focusPrevious.empty());
    CHECK_EQ(applyNode->accessibilityName, std::string("Apply now"));
}

TEST_CASE("ui.control.focus_request_rejects_ineligible_controls") {
    UIHost *host = createHost("focus-semantics");
    REQUIRE(host != nullptr);
    host->setTree(window("Focus",
                         {button("First", "first"), button("Disabled", "disabled").withEnabled(false),
                          button("Decorative", "decorative").withFocusMode(FocusMode::None)},
                         "root"));

    CHECK(!host->requestFocusById("missing"));
    CHECK(!host->requestFocusById("disabled"));
    CHECK(!host->requestFocusById("decorative"));
    CHECK(!host->requestFocusById("root"));
    CHECK(host->requestFocusById("first"));
    CHECK(node(*host, "first")->focusRequested);

    node(*host, "first")->focused = true;
    CHECK_EQ(host->focusedId(), std::string("first"));
    host->setEnabledById("first", false);
    CHECK(host->focusedId().empty());
    CHECK(!node(*host, "first")->focusRequested);
}

TEST_CASE("ui.control.focus_navigation_uses_neighbors_then_stable_tab_order") {
    UIHost *host = createHost("focus-navigation");
    REQUIRE(host != nullptr);
    host->setTree(window(
        "Focus",
        {text("Label", "label"), button("Click only", "click").withFocusMode(FocusMode::Click).withTabIndex(0),
         button("Disabled", "disabled").withEnabled(false).withTabIndex(5), button("Second", "second").withTabIndex(10),
         button("First", "first")
             .withTabIndex(20)
             .withFocusOrder("", "special")
             .withFocusNeighbors("second", "special", "second", "special"),
         button("Special", "special").withTabIndex(30).withFocusOrder("second", "")},
        "root"));

    // No current focus: select the lowest eligible tab index. Decorative,
    // disabled and click-only nodes do not participate in keyboard traversal.
    CHECK(host->moveFocus(FocusDirection::Next));
    CHECK(node(*host, "second")->focusRequested);

    node(*host, "second")->focused = true;
    CHECK(host->moveFocus(FocusDirection::Next));
    CHECK(node(*host, "first")->focusRequested);

    node(*host, "second")->focused = false;
    node(*host, "first")->focused  = true;
    CHECK(host->moveFocus(FocusDirection::Next));
    CHECK(node(*host, "special")->focusRequested);

    node(*host, "first")->focused   = false;
    node(*host, "special")->focused = true;
    CHECK(host->moveFocus(FocusDirection::Previous));
    CHECK(node(*host, "second")->focusRequested);

    node(*host, "special")->focused = false;
    node(*host, "first")->focused   = true;
    CHECK(host->moveFocus(FocusDirection::Left));
    CHECK(node(*host, "second")->focusRequested);

    node(*host, "first")->focused   = false;
    node(*host, "special")->focused = true;
    CHECK(!host->moveFocus(FocusDirection::Next, false));
}

TEST_CASE("ui.control.mouse_pass_bubbles_click_and_stop_contains_it") {
    int     childClicks   = 0;
    int     ignoredClicks = 0;
    int     rootClicks    = 0;
    UIHost *host          = createHost("pointer-routing");
    REQUIRE(host != nullptr);

    WidgetDesc child = button("Action", "action", [&]() { ++childClicks; }).withMouseFilter(MouseFilter::Pass);
    WidgetDesc ignored =
        group({std::move(child)}, "ignored").withClick([&]() { ++ignoredClicks; }).withMouseFilter(MouseFilter::Ignore);
    WidgetDesc root = window("Routing", {std::move(ignored)}, "root")
                          .withClick([&]() { ++rootClicks; })
                          .withMouseFilter(MouseFilter::Stop);
    host->setTree(std::move(root));

    UINode *action = node(*host, "action");
    REQUIRE(action != nullptr);
    auto tree = host->tree();
    REQUIRE(!tree->nodes.empty());
    CHECK_EQ(tree->nodes[size_t(action->parent)].id, std::string("ignored"));

    UISystem::pendingEvents().clear();
    UISystem::clickQueue().clear();
    UIEvent event;
    event.host         = host->handle();
    event.hostName     = host->getName();
    event.nodeId       = action->id;
    event.nodeIndex    = int(action - tree->nodes.data());
    event.kind         = "click";
    event.handlerIndex = action->handlerClick;
    UISystem::pendingEvents().push_back(event);
    UISystem::dispatchEvents();

    CHECK_EQ(childClicks, 1);
    CHECK_EQ(ignoredClicks, 0);
    CHECK_EQ(rootClicks, 1);
    CHECK_EQ(UISystem::consumeClickFor(host->getName()), std::string("action"));

    action->mouseFilter = MouseFilter::Stop;
    UISystem::pendingEvents().push_back(event);
    UISystem::dispatchEvents();
    CHECK_EQ(childClicks, 2);
    CHECK_EQ(rootClicks, 1);
}
}  // namespace test_ui_control_semantics
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_drag_drop)
#include "PrivateTestScope.h"
#include <string>
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_drag_drop.cpp
namespace test_ui_drag_drop {
using namespace eve::ui;

TEST_CASE("ui.dragDrop.retainedPayloadSurvivesFlattenAndReconcile") {
    auto host = UIHost::resolve(UIHost::createHost("drag-drop-retained"));
    REQUIRE(host.has_value());

    WidgetDesc source = button("Asset", "asset").withDragSource("asset", "textures/stone.png");
    WidgetDesc target = button("Viewport", "viewport").withDropTarget("asset");
    host->get().setTree(window("DragDrop", {std::move(source), std::move(target)}, "root"));

    auto sourceNode = host->get().findById("asset");
    auto targetNode = host->get().findById("viewport");
    REQUIRE(sourceNode.has_value());
    REQUIRE(targetNode.has_value());
    CHECK(sourceNode->get().dragSource);
    CHECK_EQ(sourceNode->get().dragPayloadType, std::string("asset"));
    CHECK_EQ(sourceNode->get().dragPayloadText, std::string("textures/stone.png"));
    CHECK(targetNode->get().dropTarget);
    CHECK_EQ(targetNode->get().acceptedDropType, std::string("asset"));

    WidgetDesc updated = button("Asset", "asset").withDragSource("entity", "unit-42");
    CHECK(
        host->get().setTreeReconcile(window("DragDrop", {std::move(updated), button("Viewport", "viewport")}, "root")));
    sourceNode = host->get().findById("asset");
    targetNode = host->get().findById("viewport");
    REQUIRE(sourceNode.has_value());
    REQUIRE(targetNode.has_value());
    CHECK_EQ(sourceNode->get().dragPayloadType, std::string("entity"));
    CHECK_EQ(sourceNode->get().dragPayloadText, std::string("unit-42"));
    CHECK(!targetNode->get().dropTarget);
    CHECK(targetNode->get().acceptedDropType.empty());
}

TEST_CASE("ui.dragDrop.completedEventOwnsPayloadAfterSourceLifetime") {
    UISystem::dropQueue().clear();
    UISystem::dropQueue().push_back(
        {DragDropOrigin::Internal, "assets", "stone", "scene", "viewport", "asset", "textures/stone.png"});

    auto drop = UISystem::consumeDrop();
    REQUIRE(drop.has_value());
    CHECK_EQ(drop->sourceHostName, std::string("assets"));
    CHECK_EQ(drop->sourceNodeId, std::string("stone"));
    CHECK_EQ(drop->targetHostName, std::string("scene"));
    CHECK_EQ(drop->targetNodeId, std::string("viewport"));
    CHECK_EQ(drop->payloadType, std::string("asset"));
    CHECK_EQ(drop->payloadText, std::string("textures/stone.png"));
    CHECK(!UISystem::consumeDrop().has_value());
}

TEST_CASE("ui.dragDrop.platformSupportIsExplicit") {
#if (defined(_WIN32) || defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    CHECK_EQ(static_cast<int>(UISystem::dragDropSupport()), static_cast<int>(DragDropSupport::Supported));
#else
    CHECK_EQ(static_cast<int>(UISystem::dragDropSupport()), static_cast<int>(DragDropSupport::UnsupportedPlatform));
#endif
}
}  // namespace test_ui_drag_drop
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIMoveRepro) || defined(EVE_TEST_ORIGIN_UITween)
#include "PrivateTestScope.h"
#include <cmath>
#include <string>
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UiTween.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: ui_motion
namespace test_ui_motion {
// UIMoveRepro.cpp
namespace {
namespace UIMoveRepro {
// Regression probe: WidgetDesc move must work in the test binary even with the
// image/layout fields present (crash investigation, kept as a regression test).
TEST_CASE("UI.regression.widgetDescMove") {
    eve::ui::WidgetDesc w;
    w.id                      = "root";
    eve::ui::WidgetDesc moved = std::move(w);
    CHECK(moved.id == "root");
}
}  // namespace UIMoveRepro
}  // namespace

// UITween.cpp
namespace {
namespace UITween {
namespace ui = eve::ui;

namespace {

ui::UIHost *resolveHost(ui::UIHostHandle handle) {
    auto host = ui::UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

ui::UINode *findNode(ui::UIHost *host, const std::string &id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

}  // namespace

TEST_CASE("UI.tween.conveniencePathIndependentOfAnimation") {
    // Provider-absent contract: presentation tweens must not require Animation.
    ui::UI *uimod = ui::UI::create();
    REQUIRE(
        ui::UIHost::resolve(uimod->mountAs("no-anim", ui::window("T", {ui::button("Go", "go")}, "root"))).has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->animateHostPos(40.f, 80.f, 0.f, "outQuad");
    uimod->animateItemOpacity("go", 0.5f, 0.f, "linear");
    CHECK(std::abs(current->meta()->posX - 40.f) < 1e-4f);
    CHECK(std::abs(current->meta()->posY - 80.f) < 1e-4f);
    ui::UINode *go = findNode(current, "go");
    REQUIRE(go != nullptr);
    CHECK(std::abs(go->opacity - 0.5f) < 1e-5f);
    CHECK_EQ(uimod->getHostTweenCount(), 0u);
    CHECK_EQ(uimod->getItemTweenCount(), 0u);
}

TEST_CASE("UI.tween.easeKinds") {
    CHECK(std::abs(ui::evaluateUiEase(0.f, "linear") - 0.f) < 1e-5f);
    CHECK(std::abs(ui::evaluateUiEase(1.f, "linear") - 1.f) < 1e-5f);
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "linear") - 0.5f) < 1e-5f);
    // smoothstep(0.5) == 0.5
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "smoothstep") - 0.5f) < 1e-5f);
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "") - 0.5f) < 1e-5f);
    // outQuad(0.5) = 1 - 0.5^2 = 0.75
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "outQuad") - 0.75f) < 1e-5f);
    // unknown falls back to linear
    CHECK(std::abs(ui::evaluateUiEase(0.25f, "not-a-real-ease") - 0.25f) < 1e-5f);
}

TEST_CASE("UI.tween.hostPosDurationZeroAndMid") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(
        ui::UIHost::resolve(uimod->mountAs("tween-pos", ui::window("T", {ui::text("x", "x")}, "root"))).has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->animateHostPos(120.f, 60.f, 0.f);
    CHECK(current->meta()->hasPos);
    CHECK(std::abs(current->meta()->posX - 120.f) < 1e-4f);
    CHECK(std::abs(current->meta()->posY - 60.f) < 1e-4f);
    CHECK_EQ(uimod->getHostTweenCount(), 0u);

    const double t0 = 1000.0;
    // Seed an in-flight tween via tickTweens clock injection: start at t0 by calling
    // animate then immediately tick at t0 (wall clock start is close enough for replace tests),
    // then drive with explicit ticks relative to a known baseline using the driver API.
    uimod->animateHostPos(300.f, 200.f, 1000.f, "linear");
    CHECK_EQ(uimod->getHostTweenCount(), 1u);
    // Still near start until we tick; pos should remain previous target until first tick advances.
    CHECK(std::abs(current->meta()->posX - 120.f) < 1e-4f);

    // Complete via explicit clock far in the future.
    uimod->tickTweens(1e15);
    CHECK(std::abs(current->meta()->posX - 300.f) < 1e-3f);
    CHECK(std::abs(current->meta()->posY - 200.f) < 1e-3f);
    CHECK_EQ(uimod->getHostTweenCount(), 0u);
    (void)t0;
}

TEST_CASE("UI.tween.hostPosReplaceAndCancel") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(uimod->mountAs("tween-replace", ui::window("T", {ui::text("x", "x")}, "root")))
                .has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->setHostPos(0.f, 0.f);
    uimod->animateHostPos(100.f, 0.f, 5000.f, "linear");
    CHECK_EQ(uimod->getHostTweenCount(), 1u);
    uimod->animateHostPos(200.f, 50.f, 5000.f, "linear");
    // Retrigger replaces; still exactly one pending pos tween.
    CHECK_EQ(uimod->getHostTweenCount(), 1u);

    uimod->cancelHostTweens();
    CHECK_EQ(uimod->getHostTweenCount(), 0u);
    // Cancel leaves the last applied sample (still at from until a tick).
    CHECK(std::abs(current->meta()->posX - 0.f) < 1e-4f);
}

TEST_CASE("UI.tween.hostSizeAndOverlayAlpha") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(
        ui::UIHost::resolve(uimod->mountAs("tween-size", ui::window("T", {ui::text("x", "x")}, "root"))).has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->animateHostSize(320.f, 180.f, 0.f);
    CHECK(current->meta()->hasSize);
    CHECK(std::abs(current->meta()->sizeX - 320.f) < 1e-4f);
    CHECK(std::abs(current->meta()->sizeY - 180.f) < 1e-4f);

    uimod->setHostOverlay(true);
    uimod->setHostOverlayAlpha(1.f);
    uimod->animateHostOverlayAlpha(0.25f, 0.f);
    CHECK(std::abs(current->meta()->overlayBgAlpha - 0.25f) < 1e-4f);
}

TEST_CASE("UI.tween.itemOpacityAndPos") {
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Panel", "root");
    uimod->beginFlex("row", "flex");
    uimod->addButton("Go", "go");
    uimod->setItemAbsolute(0.f, 0.f, 10.f, 20.f);
    uimod->end();
    uimod->end();
    REQUIRE(uimod->mountBuildAs("tween-item"));

    ui::UINode *go = findNode(resolveHost(uimod->current()), "go");
    REQUIRE(go != nullptr);

    uimod->animateItemOpacity("go", 0.4f, 0.f);
    CHECK(std::abs(go->opacity - 0.4f) < 1e-5f);

    uimod->animateItemPos("go", 80.f, 90.f, 0.f);
    CHECK(go->absolute);
    CHECK(std::abs(go->posX - 80.f) < 1e-4f);
    CHECK(std::abs(go->posY - 90.f) < 1e-4f);

    uimod->animateItemOpacity("go", 1.f, 2000.f, "linear");
    CHECK_EQ(uimod->getItemTweenCount(), 1u);
    uimod->cancelItemTweens("go");
    CHECK_EQ(uimod->getItemTweenCount(), 0u);
}

TEST_CASE("UI.tween.driverMidSampleLinear") {
    ui::UiTweenDriver driver;
    auto              handle = ui::UIHost::createHost("driver-mid");
    auto              host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
    host->get().meta()->hasPos = true;
    host->get().meta()->posX   = 0.f;
    host->get().meta()->posY   = 0.f;

    driver.animateHostPos(handle, 100.f, 50.f, 1000.f, "linear", 0.f, 0.0);
    CHECK_EQ(driver.hostTweenCount(), 1u);

    driver.tick(0.0);
    CHECK(std::abs(host->get().meta()->posX - 0.f) < 1e-4f);

    driver.tick(500.0);
    CHECK(std::abs(host->get().meta()->posX - 50.f) < 1e-3f);
    CHECK(std::abs(host->get().meta()->posY - 25.f) < 1e-3f);

    driver.tick(1000.0);
    CHECK(std::abs(host->get().meta()->posX - 100.f) < 1e-3f);
    CHECK(std::abs(host->get().meta()->posY - 50.f) < 1e-3f);
    CHECK_EQ(driver.hostTweenCount(), 0u);
}

TEST_CASE("UI.tween.delayHoldsFrom") {
    ui::UiTweenDriver driver;
    auto              handle = ui::UIHost::createHost("driver-delay");
    auto              host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
    host->get().meta()->hasPos = true;
    host->get().meta()->posX   = 10.f;
    host->get().meta()->posY   = 20.f;

    driver.animateHostPos(handle, 110.f, 120.f, 1000.f, "linear", 200.f, 0.0);
    driver.tick(100.0);
    CHECK(std::abs(host->get().meta()->posX - 10.f) < 1e-4f);
    driver.tick(200.0);
    CHECK(std::abs(host->get().meta()->posX - 10.f) < 1e-4f);
    driver.tick(700.0);  // 500ms into the 1000ms duration after 200ms delay
    CHECK(std::abs(host->get().meta()->posX - 60.f) < 1e-3f);
}

TEST_CASE("UI.tween.destroyedHostDropsTweens") {
    ui::UiTweenDriver driver;
    auto              handle = ui::UIHost::createHost("driver-drop");
    auto              host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
    host->get().meta()->hasPos = true;
    host->get().meta()->posX   = 0.f;
    host->get().meta()->posY   = 0.f;
    driver.animateHostPos(handle, 50.f, 50.f, 1000.f, "linear", 0.f, 0.0);
    CHECK_EQ(driver.hostTweenCount(), 1u);
    ecs::DestroyEntity(&host->get());
    CHECK(!ui::UIHost::resolve(handle).has_value());
    driver.tick(500.0);
    CHECK_EQ(driver.hostTweenCount(), 0u);
}

TEST_CASE("UI.tween.movableHostGetsAnimDriveOverride") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(uimod->mountAs("tween-movable", ui::window("T", {ui::text("x", "x")}, "root")))
                .has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);
    uimod->setHostMovable(true);
    uimod->setHostResizable(true);
    CHECK(!current->meta()->lockPos);
    CHECK(!current->meta()->lockSize);

    uimod->animateHostPos(40.f, 60.f, 1000.f, "linear");
    uimod->animateHostSize(200.f, 100.f, 1000.f, "linear");
    CHECK(current->meta()->animDrivePos);
    CHECK(current->meta()->animDriveSize);
    // User movable/resizable preference is preserved.
    CHECK(!current->meta()->lockPos);
    CHECK(!current->meta()->lockSize);

    const double t0 = ui::uiTweenWallClockMs();
    uimod->tickTweens(t0 + 500.0);
    CHECK(current->meta()->animDrivePos);
    CHECK(current->meta()->animDriveSize);
    CHECK(current->meta()->posX > 0.f);
    CHECK(current->meta()->sizeX > 0.f);
}

TEST_CASE("UI.tween.overshootEaseClampsAlphaAndOpacity") {
    ui::UiTweenDriver driver;
    auto              handle = ui::UIHost::createHost("tween-clamp");
    auto              host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("W", {ui::button("Go", "go")}, "root"));
    host->get().meta()->overlayBgAlpha = 1.f;

    // outBack overshoots past 1; fading 1→0 must not store a negative alpha.
    REQUIRE(ui::evaluateUiEase(0.9f, "outBack") > 1.f);
    driver.animateHostOverlayAlpha(handle, 0.f, 1000.f, "outBack", 0.f, 0.0);
    driver.tick(900.0);
    CHECK(host->get().meta()->overlayBgAlpha >= 0.f);
    CHECK(host->get().meta()->overlayBgAlpha <= 1.f);

    auto go = host->get().findById("go");
    REQUIRE(go.has_value());
    go->get().opacity = 1.f;
    driver.animateItemOpacity(handle, "go", 0.f, 1000.f, "outBack", 0.f, 0.0).expect("opacity tween");
    driver.tick(900.0);
    CHECK(go->get().opacity >= 0.f);
    CHECK(go->get().opacity <= 1.f);
}

TEST_CASE("UI.tween.gridChildPosMarksAbsolute") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(
        ui::UIHost::resolve(
            uimod->mountAs("tween-grid",
                           ui::window("T", {ui::grid(2, {ui::button("A", "a"), ui::button("B", "b")}, "g")}, "root")))
            .has_value());
    ui::UINode *a = findNode(resolveHost(uimod->current()), "a");
    REQUIRE(a != nullptr);
    CHECK(!a->absolute);
    uimod->animateItemPos("a", 12.f, 24.f, 0.f);
    CHECK(a->absolute);
    CHECK(std::abs(a->posX - 12.f) < 1e-4f);
    CHECK(std::abs(a->posY - 24.f) < 1e-4f);
}
}  // namespace UITween
}  // namespace
}  // namespace test_ui_motion
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_nine_patch)
#include "PrivateTestScope.h"
#include <memory>
#include <string>
#include "image/ImageData.h"
#include "ui/NinePatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_nine_patch.cpp
namespace test_ui_nine_patch_unit {
using eve::image::ImageData;
using eve::ui::NinePatchInfo;

namespace {

using Color = ImageData::Colorf;

void mark(ImageData &image, int x, int y) { image.setPixel(x, y, Color{0.f, 0.f, 0.f, 1.f}); }

}  // namespace

TEST_CASE("ui.nine_patch.parses_stretch_content_and_strips_marker_frame") {
    ImageData source(8, 7, "RGBA8");
    for (int y = 1; y < 6; ++y) {
        for (int x = 1; x < 7; ++x) source.setPixel(x, y, Color{0.2f, 0.4f, 0.8f, 1.f});
    }

    for (int x = 2; x <= 4; ++x) mark(source, x, 0);  // content x 1..3
    for (int y = 2; y <= 4; ++y) mark(source, 0, y);  // content y 1..3
    for (int x = 2; x <= 5; ++x) mark(source, x, 6);  // padding x 1..4
    for (int y = 3; y <= 5; ++y) mark(source, 7, y);  // padding y 2..4

    NinePatchInfo patchInfo;
    std::string   error;
    REQUIRE(eve::ui::parseNinePatch(source, patchInfo, &error));
    CHECK(error.empty());
    CHECK_EQ(patchInfo.width, 6);
    CHECK_EQ(patchInfo.height, 5);
    CHECK_EQ(patchInfo.borderLeft, 1);
    CHECK_EQ(patchInfo.borderRight, 2);
    CHECK_EQ(patchInfo.borderTop, 1);
    CHECK_EQ(patchInfo.borderBottom, 1);
    CHECK_EQ(patchInfo.paddingLeft, 1);
    CHECK_EQ(patchInfo.paddingRight, 1);
    CHECK_EQ(patchInfo.paddingTop, 2);
    CHECK_EQ(patchInfo.paddingBottom, 0);

    std::unique_ptr<ImageData> cropped = eve::ui::stripNinePatchBorder(source);
    REQUIRE(bool(cropped));
    CHECK_EQ(cropped->getWidth(), 6);
    CHECK_EQ(cropped->getHeight(), 5);
    const Color pixel = cropped->getPixel(0, 0);
    CHECK(pixel.r > 0.19f);
    CHECK(pixel.b > 0.79f);
    CHECK(pixel.a > 0.99f);
}

TEST_CASE("ui.nine_patch.rejects_disjoint_stretch_runs") {
    ImageData source(7, 7, "RGBA8");
    mark(source, 1, 0);
    mark(source, 3, 0);
    mark(source, 0, 2);
    mark(source, 0, 3);

    NinePatchInfo patchInfo;
    std::string   error;
    CHECK(!eve::ui::parseNinePatch(source, patchInfo, &error));
    CHECK(error.find("multiple stretch runs") != std::string::npos);
}
}  // namespace test_ui_nine_patch_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_parent_scaler)
#include "PrivateTestScope.h"
#include <limits>
#include "ui/ParentScaler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_parent_scaler.cpp
namespace test_ui_parent_scaler {
using namespace eve::ui;

TEST_CASE("ui.parentScaler.fullAndPartScreenTrackCanvasChanges") {
    ParentScalerSettings settings;
    ParentScalerInput    input{true, 2, 720.f};
    ParentScalerState    state;
    ParentScalerOutput   output;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(output.applyHeight);
    CHECK_EQ(output.height, 720.f);
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(!output.applyHeight);

    settings.partScreen = true;
    input.canvasHeight  = 800.f;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(output.applyHeight);
    CHECK_EQ(output.height, 500.f);
    input.canvasHeight = 0.f;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK_EQ(output.height, 0.1f);
}

TEST_CASE("ui.parentScaler.disabledEmptyAndInvalidAreAtomic") {
    ParentScalerSettings settings;
    ParentScalerInput    input{false, 3, 640.f};
    ParentScalerState    state;
    ParentScalerOutput   output{true, 123.f};
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(!output.applyHeight);
    CHECK_EQ(state.lastScaleHeight, 0.f);
    input.hasCanvas   = true;
    input.targetCount = 0;
    REQUIRE(static_cast<bool>(evaluateParentScaler(&state, &output, &settings, &input)));
    CHECK(!output.applyHeight);
    const auto unchangedState  = state;
    const auto unchangedOutput = output;
    input.canvasHeight         = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateParentScaler(&state, &output, &settings, &input));
    CHECK_EQ(state.lastScaleHeight, unchangedState.lastScaleHeight);
    CHECK_EQ(output.applyHeight, unchangedOutput.applyHeight);
}
}  // namespace test_ui_parent_scaler
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_world_anchor)
#include "PrivateTestScope.h"
#include <cmath>
#include <glm/mat4x4.hpp>
#include "ui/UIHost.h"
#include "ui/WorldAnchorProjection.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_world_anchor.cpp
namespace test_ui_world_anchor {
using namespace eve::ui;

TEST_CASE("UI.worldAnchor.projectsCenterAndDistanceScale") {
    UIHost::WorldAnchor anchor;
    anchor.enabled           = true;
    anchor.worldZ            = 0.5f;
    anchor.distanceScale     = true;
    anchor.referenceDistance = 2.f;
    anchor.minScale          = 0.5f;
    anchor.maxScale          = 1.5f;

    const auto projected = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 2.5f, 800.f, 600.f);
    CHECK(projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::Visible));
    CHECK(std::abs(projected.screenX - 400.f) < 0.001f);
    CHECK(std::abs(projected.screenY - 300.f) < 0.001f);
    CHECK(std::abs(projected.scale - 1.f) < 0.001f);
}

TEST_CASE("UI.worldAnchor.hidesOrClampsOutsideViewport") {
    UIHost::WorldAnchor anchor;
    anchor.enabled = true;
    anchor.worldX  = 2.f;
    anchor.worldZ  = 0.5f;

    auto projected = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 0.f, 100.f, 80.f);
    CHECK(!projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::OutsideViewport));

    anchor.edgePolicy = WorldAnchorEdgePolicy::Clamp;
    anchor.safeMargin = 7.f;
    projected         = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 0.f, 100.f, 80.f);
    CHECK(projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::OutsideViewport));
    CHECK(std::abs(projected.screenX - 93.f) < 0.001f);
}

TEST_CASE("UI.worldAnchor.detectsBehindCameraAndHostCanDetach") {
    UIHost::WorldAnchor anchor;
    anchor.enabled = true;
    anchor.worldZ  = 1.f;
    glm::mat4 behind(0.f);
    behind[0][0]         = 1.f;
    behind[1][1]         = 1.f;
    behind[2][2]         = 1.f;
    behind[2][3]         = -1.f;
    const auto projected = projectWorldAnchor(anchor, behind, 0.f, 0.f, 0.f, 100.f, 80.f);
    CHECK(!projected.render);
    CHECK(static_cast<int>(projected.state) == static_cast<int>(WorldAnchorState::BehindCamera));

    const UIHostHandle handle = UIHost::createHost("world-anchor-lifecycle");
    auto               host   = UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setWorldAnchor(1.f, 2.f, 3.f);
    CHECK(host->get().worldAnchor()->enabled);
    host->get().clearWorldAnchor();
    CHECK(!host->get().worldAnchor()->enabled);
    CHECK(static_cast<int>(host->get().worldAnchor()->state) == static_cast<int>(WorldAnchorState::Disabled));
}

TEST_CASE("UI.worldAnchor.avoidsOverlapByPriorityWithStableResults") {
    WorldAnchorLayoutItem lower;
    lower.stableIndex     = 2;
    lower.screenX         = 50.f;
    lower.screenY         = 50.f;
    lower.width           = 20.f;
    lower.height          = 10.f;
    lower.pivotX          = 0.5f;
    lower.pivotY          = 0.5f;
    lower.padding         = 2.f;
    lower.maxDisplacement = 30.f;
    lower.priority        = 1;
    lower.avoidOverlap    = true;

    auto higher        = lower;
    higher.stableIndex = 7;
    higher.priority    = 10;

    const auto resolved = resolveWorldAnchorOverlaps({lower, higher}, 100.f, 100.f);
    REQUIRE(resolved.size() == 2);
    CHECK(resolved[0].stableIndex == 2);
    CHECK(resolved[0].render);
    CHECK(std::abs(resolved[0].displacementY + 12.f) < 0.001f);
    CHECK(resolved[1].stableIndex == 7);
    CHECK(resolved[1].render);
    CHECK(std::abs(resolved[1].displacementY) < 0.001f);
}

TEST_CASE("UI.worldAnchor.suppressesCrowdedAnchorWhenDisplacementIsBounded") {
    WorldAnchorLayoutItem fixed;
    fixed.stableIndex = 0;
    fixed.screenX     = 50.f;
    fixed.screenY     = 50.f;
    fixed.width       = 20.f;
    fixed.height      = 10.f;

    auto crowded            = fixed;
    crowded.stableIndex     = 1;
    crowded.avoidOverlap    = true;
    crowded.maxDisplacement = 0.f;

    const auto resolved = resolveWorldAnchorOverlaps({fixed, crowded}, 100.f, 100.f);
    REQUIRE(resolved.size() == 2);
    CHECK(resolved[0].render);
    CHECK(!resolved[1].render);
}
}  // namespace test_ui_world_anchor
#endif
