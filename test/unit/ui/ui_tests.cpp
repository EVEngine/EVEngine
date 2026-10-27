// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "UIBuilder.cpp",
    "UIEditorKit.cpp",
    "UIPhaseAdv.cpp",
    "UIPhaseB.cpp",
    "UISystem.cpp",
    "pcg_color_preview_sync.cpp",
    "pcg_photo_mode_color_picker.cpp",
    "pcg_photo_mode_session.cpp",
    "ui_color_palette.cpp",
    "ui_control_semantics.cpp",
    "ui_drag_drop.cpp",
    "UIMoveRepro.cpp",
    "UITween.cpp",
    "ui_nine_patch.cpp",
    "ui_parent_scaler.cpp",
    "ui_world_anchor.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

// Each guarded section is one source origin selected by TestLevels profile builds.
// SUB_CASE groups inexpensive input variants into one process per public contract.
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIBuilder)
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <string>

namespace test_ui_builder {
using namespace eve::ui;

UIHost* host(UIHostHandle handle) {
    auto resolved = UIHost::resolve(handle);
    return resolved ? &resolved->get() : nullptr;
}

UINode* node(UIHostHandle handle, const std::string& id) {
    auto* owner = host(handle);
    if (!owner) return nullptr;
    auto found = owner->findById(id);
    return found ? &found->get() : nullptr;
}

class HudPanel : public UIHost {
public:
    ENTITY(HudPanel, UIHost)
    void release() override {}
    struct Health {
        int value = 100;
    };
    COMPONENT(Health, health)
};

TEST_CASE("ui.builder.namedMountSelectionAndTreeMutationContract") {
    UI* ui = UI::create();
    REQUIRE(ui->mountAs("hud", window("HUD", {text("HP", "hp"), button("Pause", "pause")})));
    REQUIRE(ui->mountAs("menu", window("Menu", {button("Quit", "quit")})));
    CHECK(host(UISystem::findHost("hud")) != nullptr);
    CHECK(host(UISystem::findHost("menu")) != nullptr);
    CHECK(ui->select("hud"));
    CHECK(host(ui->current()) == host(UISystem::findHost("hud")));
    ui->setText("hp", "HP 80");
    REQUIRE(node(ui->current(), "hp") != nullptr);
    CHECK_EQ(node(ui->current(), "hp")->text, "HP 80");
    CHECK(ui->select("menu"));
    CHECK(node(ui->current(), "quit") != nullptr);
    CHECK(!ui->select("missing"));
}

TEST_CASE("ui.builder.ecsSubclassOwnerBindingAndQueuedClickContract") {
    SUB_CASE("subclass remains visible through UIHost ECS view") {
        HudPanel* hud       = HudPanel::create();
        hud->meta()->entity = ecs::handle_of(hud);
        hud->setName("playerhud");
        hud->health()->value = 42;
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
        CHECK_EQ(hud->health()->value, 42);
        CHECK(host(UISystem::findHost("playerhud")) == static_cast<UIHost*>(hud));
    };
    SUB_CASE("owner lookup and queued click deliver one consumable event") {
        UI* ui = UI::create();
        REQUIRE(ui->mountAs("inv", window("Inv", {button("Use", "use")})));
        ui->bindOwner(7);
        CHECK(host(UISystem::findHostByOwner(7)) == host(ui->current()));
        UIEvent event;
        event.host         = ui->current();
        event.hostName     = "inv";
        event.nodeId       = "use";
        event.kind         = "click";
        event.handlerIndex = 0;
        UISystem::pendingEvents().push_back(event);
        UISystem::dispatchEvents();
        ui->select("inv");
        CHECK_EQ(ui->consumeClick(), "inv/use");
        CHECK(ui->consumeClick().empty());
    };
}

TEST_CASE("ui.builder.immediateBuilderMountsNamedTreeAndSupportsPropertyUpdate") {
    UI* ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Shop", "root");
    ui->addText("Gold: 10", "gold");
    ui->beginGroup("row");
    ui->addButton("Buy", "buy");
    ui->addSameLine();
    ui->addButton("Sell", "sell");
    ui->end();
    ui->end();
    REQUIRE(ui->mountBuildAs("shop"));
    REQUIRE(ui->select("shop"));
    CHECK(node(ui->current(), "buy") != nullptr);
    CHECK(node(ui->current(), "sell") != nullptr);
    ui->setText("gold", "Gold: 9");
    REQUIRE(node(ui->current(), "gold") != nullptr);
    CHECK_EQ(node(ui->current(), "gold")->text, "Gold: 9");
}
}  // namespace test_ui_builder
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIEditorKit)
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <string>

namespace test_ui_editor_kit {
using namespace eve::ui;

UIHost* host(UIHostHandle handle) {
    auto resolved = UIHost::resolve(handle);
    return resolved ? &resolved->get() : nullptr;
}

UINode* node(UIHost* owner, const std::string& id) {
    if (!owner) return nullptr;
    auto found = owner->findById(id);
    return found ? &found->get() : nullptr;
}

TEST_CASE("ui.editorKit.declarativeEditorWidgets_haveExpectedTypesAndProperties") {
    UIHost* owner = host(UIHost::createHost("editor_kit"));
    REQUIRE(owner != nullptr);
    applyTree(owner, window("Editor",
                            {menuBar({menu("File", {menuItem("Save", "Ctrl+S", "save")}, "file")}, "menu"),
                             searchField("Search assets", "", "search").withTooltip("Filter assets"),
                             sectionHeader("Properties", "heading"),
                             card({toggleSwitch("Visible", true, "visible"), badge("Modified", "state")}, "details")},
                            "root"));
    const struct {
        const char* id;
        NodeType    type;
    } expected[] = {
        {"menu", NodeType::MenuBar},          {"file", NodeType::Menu},
        {"save", NodeType::MenuItem},         {"search", NodeType::SearchField},
        {"heading", NodeType::SectionHeader}, {"details", NodeType::Card},
        {"visible", NodeType::Switch},        {"state", NodeType::Badge},
    };
    for (const auto& item : expected) {
        REQUIRE(node(owner, item.id) != nullptr);
        CHECK(node(owner, item.id)->type == item.type);
    }
    CHECK_EQ(node(owner, "save")->valueText, "Ctrl+S");
    CHECK_EQ(node(owner, "search")->tooltip, "Filter assets");
    CHECK(node(owner, "visible")->checked);
}

TEST_CASE("ui.editorKit.builderJsonRoundTrip_preservesFocusAccessibilityAndHostSettings") {
    UI* ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Editor", "root");
    ui->setThemeScope("light");
    ui->addSearchField("Search", "cube", "search");
    ui->setItemTooltip("Filter scene nodes");
    ui->setItemMouseFilter("pass");
    ui->setItemTabIndex(4);
    ui->setItemFocusNeighbors("search", "save", "search", "save");
    ui->setItemAccessibility("text-input", "Search scene", "Filters the scene tree");
    ui->beginToolbar("toolbar");
    ui->addIconButton("save", "", "save");
    ui->end();
    ui->end();
    REQUIRE(ui->mountBuildAs("editor_builder"));
    const std::string json = ui->saveTreeJson();
    CHECK(json.find("searchField") != std::string::npos);
    REQUIRE(ui->loadTreeJson(json));
    UIHost* owner = host(ui->current());
    REQUIRE(owner != nullptr);
    REQUIRE(node(owner, "search") != nullptr);
    CHECK_EQ(node(owner, "search")->tabIndex, 4);
    CHECK(node(owner, "search")->mouseFilter == MouseFilter::Pass);
    CHECK_EQ(node(owner, "search")->accessibilityName, "Search scene");
    CHECK_EQ(node(owner, "search")->focusRight, "save");
    REQUIRE(ui->requestFocus("search"));
    REQUIRE(ui->moveFocus("right"));
    CHECK(node(owner, "save")->focusRequested);
    ui->setHostMovable(true);
    ui->setHostResizable(true);
    ui->setHostOverlayAlpha(.85f);
    CHECK(!owner->meta()->lockPos);
    CHECK(!owner->meta()->lockSize);
    CHECK_EQ(owner->meta()->overlayBgAlpha, .85f);
}

TEST_CASE("ui.editorKit.textToolSerializationAndReconcile_updatesOnlyNewProperties") {
    UI* ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Text tools", "root");
    ui->addLink("Documentation", "https://example.com/docs", "docs");
    ui->addInputText("Notes", "First line\nSecond line", "notes");
    ui->setItemTextInput(true, true, "Enter notes");
    ui->setItemHelp("Plain text notes", "Document is locked");
    ui->end();
    REQUIRE(ui->mountBuildAs("text_tools"));
    REQUIRE(ui->loadTreeJson(ui->saveTreeJson()));
    UIHost* owner = host(ui->current());
    REQUIRE(owner != nullptr);
    REQUIRE(node(owner, "docs") != nullptr);
    REQUIRE(node(owner, "notes") != nullptr);
    CHECK(node(owner, "docs")->type == NodeType::Link);
    CHECK_EQ(node(owner, "docs")->valueText, "https://example.com/docs");
    CHECK(node(owner, "notes")->multiline);
    CHECK(node(owner, "notes")->readOnly);
    CHECK_EQ(node(owner, "notes")->placeholder, "Enter notes");
    applyTree(owner, window("Text tools",
                            {link("Documentation", "resource://manual", "docs"),
                             inputText("Notes", "Editable", "notes").withTextInput(false).withHelp("Updated help")},
                            "root"));
    CHECK_EQ(node(owner, "docs")->valueText, "resource://manual");
    CHECK(!node(owner, "notes")->readOnly);
    CHECK_EQ(node(owner, "notes")->helpText, "Updated help");
    CHECK(node(owner, "notes")->disabledReason.empty());
}
}  // namespace test_ui_editor_kit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIPhaseAdv)
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>
#include <vector>

namespace test_ui_advanced {
namespace ui = eve::ui;

ui::UIHost* host(ui::UIHostHandle handle) {
    auto resolved = ui::UIHost::resolve(handle);
    return resolved ? &resolved->get() : nullptr;
}

ui::UINode* node(ui::UIHost* owner, const std::string& id) {
    if (!owner) return nullptr;
    auto found = owner->findById(id);
    return found ? &found->get() : nullptr;
}

void close(float actual, float expected) { CHECK(std::abs(actual - expected) < 1e-4f); }

TEST_CASE("ui.advanced.controls_haveDeclaredNodeTypesAndDispatchValueChanges") {
    float       slid = 0.f;
    std::string typed;
    ui::UIHost* owner = host(ui::UIHost::createHost("advanced_controls"));
    REQUIRE(owner != nullptr);
    owner->setTree(ui::window("Advanced",
                              {ui::slider("Volume", .3f, 0.f, 1.f, "volume", [&](float value) { slid = value; }),
                               ui::progress(.5f, "progress", "50%"),
                               ui::inputText("Name", "hero", "name", [&](const std::string& value) { typed = value; }),
                               ui::collapsingHeader("More", {ui::text("hidden", "tip")}, "more", true),
                               ui::child("scroll", {ui::text("row", "row")}, 200.f, 80.f)},
                              "root"));
    const struct {
        const char*  id;
        ui::NodeType type;
    } expected[] = {{"volume", ui::NodeType::Slider},
                    {"progress", ui::NodeType::Progress},
                    {"name", ui::NodeType::InputText},
                    {"more", ui::NodeType::CollapsingHeader},
                    {"scroll", ui::NodeType::Child}};
    for (const auto& item : expected) {
        REQUIRE(node(owner, item.id) != nullptr);
        CHECK(node(owner, item.id)->type == item.type);
    }
    CHECK(node(owner, "tip") != nullptr);
    CHECK(node(owner, "row") != nullptr);
    ui::UIEvent event;
    event.host         = owner->handle();
    event.hostName     = "advanced_controls";
    event.nodeId       = "volume";
    event.kind         = "value";
    event.handlerIndex = node(owner, "volume")->handlerValue;
    event.floatValue   = .75f;
    ui::UISystem::pendingEvents().push_back(event);
    ui::UISystem::dispatchEvents();
    close(slid, .75f);
    CHECK_EQ(ui::UISystem::consumeChange(), "advanced_controls/volume");
}

TEST_CASE("ui.advanced.builderFlexGridAndReconcile_contract") {
    ui::UI* module = ui::UI::create();
    module->beginBuild();
    module->beginWindow("Layout", "root");
    module->beginRow("toolbar", 6.f);
    module->setFlexJustify("space-between");
    module->setFlexAlign("center");
    module->setFlexWrap(true);
    module->addButton("Save", "save");
    module->addSpacer("space");
    module->addButton("Quit", "quit");
    module->setItemFlexBasis(64.f);
    module->setItemFlexShrink(.5f);
    module->end();
    module->beginGrid(3, "cards", 10.f, 12.f);
    module->addButton("Wide", "wide");
    module->setItemGridColumnSpan(2);
    module->end();
    module->end();
    REQUIRE(module->mountBuildAs("layout"));
    ui::UIHost* owner = host(module->current());
    REQUIRE(owner != nullptr);
    REQUIRE(node(owner, "toolbar") != nullptr);
    CHECK(node(owner, "toolbar")->type == ui::NodeType::Flex);
    CHECK(node(owner, "toolbar")->flexDirection == ui::FlexDirection::Row);
    CHECK(node(owner, "toolbar")->justifyContent == ui::FlexJustify::SpaceBetween);
    CHECK(node(owner, "toolbar")->flexWrap == ui::FlexWrap::Wrap);
    CHECK(node(owner, "cards")->type == ui::NodeType::Grid);
    CHECK_EQ(node(owner, "cards")->gridColumns, 3);
    CHECK_EQ(node(owner, "wide")->gridColumnSpan, 2);
    close(node(owner, "quit")->flexBasis, 64.f);

    auto next =
        ui::window("Layout",
                   {ui::row({ui::button("Save2", "save"), ui::spacer("space"), ui::button("Quit2", "quit")}, "toolbar")
                        .withGap(10.f)},
                   "root");
    CHECK(owner->setTreeReconcile(std::move(next)));
    CHECK_EQ(node(owner, "save")->text, "Save2");
}

TEST_CASE("ui.layout.flexAndGrid_arithmeticMatchesInputConstraints") {
    SUB_CASE("grow item consumes remaining row width") {
        std::vector<ui::FlexItemSpec> items(3);
        items[0].basisMain = 60.f;
        items[1].basisMain = 20.f;
        items[1].flexGrow  = 1.f;
        items[2].basisMain = 60.f;
        const auto result =
            ui::flexArrange(true, 10.f, 400.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items);
        REQUIRE_EQ(result.items.size(), std::size_t{3});
        close(result.items[0].w, 60.f);
        close(result.items[1].w, 260.f);
        close(result.items[2].x, 340.f);
    };
    SUB_CASE("percent size and absolute anchor use available width") {
        std::vector<ui::FlexItemSpec> percent(1);
        percent[0].percentMain  = .5f;
        percent[0].percentCross = 1.f;
        auto result = ui::flexArrange(true, 0.f, 200.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, percent);
        close(result.items[0].w, 100.f);
        close(result.items[0].h, 30.f);
        std::vector<ui::FlexItemSpec> absolute(1);
        absolute[0].absolute   = true;
        absolute[0].basisMain  = 20.f;
        absolute[0].basisCross = 20.f;
        absolute[0].anchorMain = 1.f;
        absolute[0].posMain    = -10.f;
        result = ui::flexArrange(true, 0.f, 200.f, 50.f, ui::FlexAlign::Start, ui::FlexJustify::Start, absolute);
        close(result.items[0].x, 170.f);
    };
    SUB_CASE("wrapping uses independent row and column gaps") {
        std::vector<ui::FlexItemSpec> items(3);
        for (auto& item : items) {
            item.basisMain  = 60.f;
            item.basisCross = 20.f;
            item.flexShrink = 0.f;
        }
        const auto result =
            ui::flexArrange(true, 10.f, 130.f, 100.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items, true, 7.f);
        close(result.items[1].x, 70.f);
        close(result.items[2].x, 0.f);
        close(result.items[2].y, 27.f);
        close(result.contentH, 47.f);
    };
    SUB_CASE("grid column span and aspect ratio determine cell positions") {
        std::vector<ui::GridItemSpec> items(3);
        items[0].basisH      = 20.f;
        items[0].columnSpan  = 2;
        items[1].aspectRatio = 2.f;
        items[2].basisH      = 30.f;
        const auto result    = ui::gridArrange(3, 10.f, 5.f, 320.f, items);
        REQUIRE_EQ(result.items.size(), std::size_t{3});
        close(result.items[0].w, 210.f);
        close(result.items[1].x, 220.f);
        close(result.items[1].h, 50.f);
        close(result.items[2].y, 55.f);
    };
}

TEST_CASE("ui.advanced.styleClassInheritanceAndTreeJsonContract") {
    ui::clearStyleClasses();
    REQUIRE(ui::defineStyleClass("panel") == ui::StyleClassStatus::Applied);
    REQUIRE(ui::setStyleClassColor("panel", "background", .1f, .2f, .3f, .8f) == ui::StyleClassStatus::Applied);
    REQUIRE(ui::setStyleClassMetric("panel", "padding", 12.f, 7.f) == ui::StyleClassStatus::Applied);
    REQUIRE(ui::defineStyleClass("primary", "panel") == ui::StyleClassStatus::Applied);
    REQUIRE(ui::setStyleClassColor("primary", "accent", .9f, .4f, .2f, 1.f) == ui::StyleClassStatus::Applied);
    CHECK(ui::defineStyleClass("bad", "missing") == ui::StyleClassStatus::UnknownParent);
    CHECK(ui::defineStyleClass("panel", "primary") == ui::StyleClassStatus::InheritanceCycle);
    ui::StyleClass resolved;
    REQUIRE(ui::resolveStyleClass("primary", &resolved) == ui::StyleClassStatus::Applied);
    CHECK(resolved.hasBackgroundColor);
    CHECK(resolved.hasAccentColor);
    close(resolved.paddingX, 12.f);
    ui::UI* module = ui::UI::create();
    module->beginBuild();
    module->beginWindow("Styled", "root");
    module->setStyleScope("panel");
    module->addButton("Save", "save");
    module->setItemStyleClass("primary");
    module->end();
    REQUIRE(module->mountBuildAs("styles"));
    const std::string json = module->saveTreeJson();
    REQUIRE(module->loadTreeJson(json));
    auto* save = node(host(module->current()), "save");
    REQUIRE(save != nullptr);
    CHECK_EQ(save->styleClass, "primary");
    CHECK(save->hasResolvedStyle);
    CHECK(save->resolvedStyle.hasBackgroundColor);
    CHECK(save->resolvedStyle.hasAccentColor);
    ui::clearStyleClasses();
}

TEST_CASE("ui.advanced.imageComboAndVirtualList_widgetPropertiesAndEventContract") {
    int         clicks    = 0;
    int         selection = -1;
    ui::UIHost* owner     = host(ui::UIHost::createHost("media"));
    REQUIRE(owner != nullptr);
    owner->setTree(ui::window("Media",
                              {ui::image("avatar", 64.f, 64.f).withTint(.2f, .4f, .8f).withCornerRadius(8.f),
                               ui::imageButton("go", 48.f, 48.f, [&] { ++clicks; }),
                               ui::combo("Fruit", {"Apple", "Banana", "Cherry"}, 1, "fruit",
                                         [&](float index) { selection = static_cast<int>(index); }),
                               ui::text("long text", "paragraph").withWrap(120.f),
                               ui::virtualList("goods", {"one", "two", "three"}, 180.f, 24.f)},
                              "root"));
    CHECK(node(owner, "avatar")->type == ui::NodeType::Image);
    close(node(owner, "avatar")->sizeX, 64.f);
    close(node(owner, "avatar")->tintB, .8f);
    CHECK(node(owner, "go")->type == ui::NodeType::ImageButton);
    CHECK(node(owner, "fruit")->type == ui::NodeType::Combo);
    CHECK_EQ(node(owner, "fruit")->valueText, "Apple\nBanana\nCherry");
    close(node(owner, "paragraph")->wrapWidth, 120.f);
    CHECK(node(owner, "goods/2") != nullptr);
    CHECK_EQ(node(owner, "goods/2")->text, "three");

    ui::UIEvent click;
    click.host         = owner->handle();
    click.hostName     = "media";
    click.nodeId       = "go";
    click.kind         = "click";
    click.handlerIndex = node(owner, "go")->handlerClick;
    ui::UISystem::pendingEvents().push_back(click);
    ui::UIEvent value  = click;
    value.nodeId       = "fruit";
    value.kind         = "value";
    value.handlerIndex = node(owner, "fruit")->handlerValue;
    value.floatValue   = 2.f;
    ui::UISystem::pendingEvents().push_back(value);
    ui::UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    CHECK_EQ(selection, 2);
    CHECK_EQ(ui::UISystem::consumeClickFor("media"), "go");
    CHECK_EQ(ui::UISystem::consumeChange(), "media/fruit");
}

TEST_CASE("ui.advanced.serializationRejectsInvalidTreeAndExposesAccessibilitySnapshot") {
    ui::UI* module = ui::UI::create();
    REQUIRE(module->mountAs(
        "diagnostics",
        ui::window("Diagnostics",
                   {ui::button("Save", "save")
                        .withAccessibility(ui::AccessibilityRole::Button, "Save project", "Writes current changes")},
                   "root")));
    const std::string json = module->saveTreeJson();
    CHECK(json.find("eve.ui.tree") != std::string::npos);
    CHECK(!module->loadTreeJson("{not json"));
    CHECK(!module->loadTreeJson("{\"schema\":\"eve.ui.tree\",\"version\":999}"));
    REQUIRE(module->loadTreeJson(json));
    module->animateItemOpacity("save", .25f, 0.f);
    close(node(host(module->current()), "save")->opacity, .25f);
    CHECK(module->getLayoutDiagnostics().find("overflowX") != std::string::npos);
    CHECK(module->getAccessibilitySnapshot().find("Save project") != std::string::npos);
    module->setNavGamepad(true);
    CHECK(ui::globalTheme().navEnableGamepad);
    module->setNavGamepad(false);
    CHECK(!ui::globalTheme().navEnableGamepad);
}
}  // namespace test_ui_advanced
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIPhaseB)
#include "ui/Component.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <string>
#include <vector>

namespace test_ui_declarative {
using namespace eve::ui;

UIHost* host(UIHostHandle handle) {
    auto found = UIHost::resolve(handle);
    return found ? &found->get() : nullptr;
}

UINode* node(UIHost* owner, const std::string& id) {
    if (!owner) return nullptr;
    auto found = owner->findById(id);
    return found ? &found->get() : nullptr;
}

class Inventory : public Component {
public:
    int                      gold = 10;
    std::vector<std::string> items{"Sword", "Potion"};
    bool                     showTip = true;

    WidgetDesc build() override {
        return window("Inventory",
                      {text("Gold " + std::to_string(gold), "gold"), listButtons("items", items),
                       when(showTip, text("Extra tip", "tip")),
                       checkbox("Show tip", showTip, "tipchk",
                                [this](bool value) {
                                    showTip = value;
                                    setState();
                                })},
                      "root");
    }
};

TEST_CASE("ui.declarative.conditionalListAndKeyedReconcileContract") {
    UIHost* owner = host(UIHost::createHost("inventory"));
    REQUIRE(owner != nullptr);
    owner->setTree(window("Inventory",
                          {when(false, text("Hidden", "hidden")), when(true, text("Shown", "shown")),
                           listButtons("items", {"Sword", "Potion"})},
                          "root"));
    CHECK(node(owner, "hidden") == nullptr);
    CHECK_EQ(node(owner, "shown")->text, "Shown");
    CHECK_EQ(node(owner, "items/1")->text, "Potion");

    bool rebuilt =
        owner->setTreeReconcile(window("Inventory",
                                       {when(false, text("Hidden", "hidden")), when(true, text("Updated", "shown")),
                                        listButtons("items", {"Sword", "Potion"})},
                                       "root"));
    CHECK(!rebuilt);
    CHECK_EQ(node(owner, "shown")->text, "Updated");
    rebuilt = owner->setTreeReconcile(
        window("Inventory", {text("Updated", "shown"), listButtons("items", {"Sword", "Potion", "Shield"})}, "root"));
    CHECK(rebuilt);
    CHECK(node(owner, "items/2") != nullptr);
}

TEST_CASE("ui.declarative.componentStateAndScriptBuilderContract") {
    SUB_CASE("dirty component rebuild exposes changed model values") {
        Inventory component;
        component.mountAs("component");
        UIHost* owner = host(UISystem::findHost("component"));
        REQUIRE(owner != nullptr);
        CHECK_EQ(node(owner, "gold")->text, "Gold 10");
        REQUIRE(node(owner, "items/1") != nullptr);
        component.gold = 9;
        component.items.push_back("Shield");
        component.markDirty();
        CHECK(component.updateIfDirty());
        CHECK_EQ(node(owner, "gold")->text, "Gold 9");
        CHECK_EQ(node(owner, "items/2")->text, "Shield");
    };
    SUB_CASE("script builder nests list items and checkbox") {
        UI* module = UI::create();
        module->beginBuild();
        module->beginWindow("Shop", "root");
        module->beginList("goods");
        module->addListItem("Apple", "goods/0");
        module->addListItem("Bread", "goods/1");
        module->end();
        module->addCheckbox("Member", false, "member");
        module->end();
        REQUIRE(module->mountBuildAs("shop"));
        UIHost* owner = host(module->current());
        REQUIRE(owner != nullptr);
        CHECK_EQ(node(owner, "goods/1")->text, "Bread");
        CHECK(!node(owner, "member")->checked);
    };
}

TEST_CASE("ui.declarative.themeAndIconPublicTokenContract") {
    UI* module = UI::create();
    module->setThemeLight();
    CHECK_EQ(module->getTheme(), "light");
    CHECK(globalTheme().windowBg[0] > .5f);
    module->setThemeDark();
    CHECK_EQ(module->getTheme(), "dark");
    CHECK(module->setTheme("LIGHT"));
    CHECK_EQ(module->getTheme(), "light");
    CHECK(!module->setTheme("missing"));
    CHECK_EQ(module->getTheme(), "light");
    CHECK_EQ(Theme::dark().layout.toolbarHeight, Theme::light().layout.toolbarHeight);

    Icon parsed = Icon::None;
    CHECK(iconFromName("search", &parsed));
    CHECK(parsed == Icon::Search);
    CHECK_EQ(std::string(iconGlyph(parsed)), "\xEF\x80\x82");
    CHECK_EQ(std::string(iconName(Icon::PaintBrush)), "paint-brush");
    CHECK(!iconFromName("missing", &parsed));
    UIHost* owner = host(UIHost::createHost("icons"));
    REQUIRE(owner != nullptr);
    owner->setTree(window("Icons", {icon(Icon::Search, "search"), iconButton(Icon::Save, "Save", "save")}));
    CHECK_EQ(node(owner, "search")->text, std::string(iconGlyph(Icon::Search)));
    CHECK_EQ(node(owner, "save")->text, iconText(Icon::Save, "Save"));
}
}  // namespace test_ui_declarative
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UISystem)
#include "common/Capability.h"
#include "common/UIAutomation.h"
#include "ui/UIAutomationCapabilities.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <string>

namespace test_ui_system {
using namespace eve::ui;

UIHost* host(UIHostHandle handle) {
    auto found = UIHost::resolve(handle);
    return found ? &found->get() : nullptr;
}

UINode* node(UIHost* owner, const std::string& id) {
    if (!owner) return nullptr;
    auto found = owner->findById(id);
    return found ? &found->get() : nullptr;
}

TEST_CASE("ui.system.retainedTreeSimplePanelAndPropertyMutationContract") {
    SUB_CASE("nested widget description flattens into addressable nodes") {
        UIHost* owner = host(UIHost::createHost("nested"));
        REQUIRE(owner != nullptr);
        owner->setTree(window(
            "Inventory", {text("Hello", "label"), group({button("A", "a"), sameLine(), button("B", "b")})}, "root"));
        REQUIRE(!owner->tree()->nodes.empty());
        CHECK_EQ(owner->tree()->root, 0);
        CHECK(owner->tree()->nodes[0].type == NodeType::Window);
        CHECK(node(owner, "a") != nullptr);
        CHECK(node(owner, "b") != nullptr);
        owner->setTextById("label", "Updated");
        CHECK_EQ(node(owner, "label")->text, "Updated");
        owner->setVisibleById("b", false);
        CHECK(!node(owner, "b")->visible);
    };
    SUB_CASE("simple panel has stable label and button ids") {
        UIHost* owner = host(UIHost::createHost("simple"));
        REQUIRE(owner != nullptr);
        owner->setSimplePanel("Inventory", "Hello", "Use");
        REQUIRE(!owner->tree()->nodes.empty());
        CHECK_EQ(owner->tree()->nodes.size(), std::size_t{3});
        CHECK_EQ(node(owner, "label")->text, "Hello");
        CHECK_EQ(node(owner, "btn")->text, "Use");
    };
}

TEST_CASE("ui.system.queuedClickInvokesHandlerAndIsConsumableByHost") {
    int     clicks = 0;
    UIHost* owner  = host(UIHost::createHost("commands"));
    REQUIRE(owner != nullptr);
    owner->setTree(window("Commands", {button("Run", "run"), text("Idle", "status")}));
    CHECK(owner->setClickHandler("run", [&] { ++clicks; }));
    UIEvent event;
    event.host         = owner->handle();
    event.hostName     = "commands";
    event.nodeId       = "run";
    event.kind         = "click";
    event.handlerIndex = node(owner, "run")->handlerClick;
    UISystem::pendingEvents().push_back(event);
    CHECK_EQ(clicks, 0);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    CHECK_EQ(UISystem::consumeClickFor("commands"), "run");
    CHECK_EQ(UISystem::consumeClickFor("commands"), "");
}

TEST_CASE("ui.system.automationExposesSemanticsAndQueuesClick") {
    int     clicks = 0;
    UIHost* owner  = host(UIHost::createHost("automation"));
    REQUIRE(owner != nullptr);
    owner->setTree(window("Tools", {text("Ready", "status"), button("Create", "create", [&] { ++clicks; }),
                                    grid(2, {text("Cell", "cell")}, "grid")}));
    registerUIAutomationCapabilities();
    auto* automation = eve::cap::query<eve::IUIAutomation>();
    REQUIRE(automation != nullptr);
    const std::string tree = automation->tree("automation");
    CHECK(tree.find("\"id\":\"create\"") != std::string::npos);
    CHECK(tree.find("\"type\":\"grid\"") != std::string::npos);
    CHECK(tree.find("\"clickable\":true") != std::string::npos);
    CHECK(automation->get("automation", "create").find("\"type\":\"button\"") != std::string::npos);
    CHECK(automation->click("automation", "create").find("\"queued\":true") != std::string::npos);
    CHECK_EQ(clicks, 0);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
    CHECK_EQ(UISystem::consumeClickFor("automation"), "create");
    CHECK(automation->click("automation", "status").find("error: widget is not clickable") == 0);
}
}  // namespace test_ui_system
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_color_preview_sync)
#include "ui/PcgColorPreviewSync.h"
#include "ui/PcgControllerSelection.h"
#include "ui/PcgDraggableWindow.h"
#include "ui/PcgLoadingScreen.h"
#include "ui/PcgScreenshotSavedNotice.h"
#include "ui/PcgTooltip.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <string>

namespace test_ui_pcg_controls {
using namespace eve::ui;

TEST_CASE("ui.pcg.colorPreviewScalesRgbAndPreservesLastGoodValue") {
    PcgColorPreviewSync preview;
    REQUIRE(preview.sync(.2f, .3f, .4f, .5f).ok());
    CHECK_EQ(preview.getRed(), .5f);
    CHECK_EQ(preview.getGreen(), .75f);
    CHECK_EQ(preview.getBlue(), 1.f);
    CHECK_EQ(preview.getAlpha(), 1.25f);
    REQUIRE(preview.sync(0.f, 0.f, 0.f, .2f).ok());
    CHECK_EQ(preview.getRed(), .5f);
    CHECK_EQ(preview.getGreen(), .5f);
    CHECK_EQ(preview.getBlue(), .5f);
    CHECK_EQ(preview.getAlpha(), 1.f);
    CHECK(!preview.sync(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 1.f).ok());
    CHECK_EQ(preview.getRed(), .5f);
}

TEST_CASE("ui.pcg.controllerSelectionFirstMatchAndMissingRefreshContract") {
    PcgControllerSelection selection;
    REQUIRE(selection.add("Flying", 1, "flying").ok());
    REQUIRE(selection.add("Duplicate", 1, "duplicate").ok());
    REQUIRE(selection.add("Walking", 2, "walking").ok());
    auto first = selection.refresh(1);
    REQUIRE(first.ok());
    CHECK(first.value());
    CHECK_EQ(selection.getSelectedIndex(), 0);
    REQUIRE(selection.getVisible(0).ok());
    CHECK(selection.getVisible(0).value());
    REQUIRE(selection.getVisible(1).ok());
    CHECK(!selection.getVisible(1).value());
    REQUIRE(selection.getWidgetId(0).ok());
    CHECK_EQ(selection.getWidgetId(0).value(), "flying");
    auto absent = selection.refresh(99);
    REQUIRE(absent.ok());
    CHECK(!absent.value());
    CHECK_EQ(selection.getSelectedIndex(), 0);
    CHECK(!selection.getVisible(3).ok());
    CHECK(!selection.add("", 3, "invalid").ok());
    CHECK_EQ(selection.getCount(), 3);
}

TEST_CASE("ui.pcg.draggableWindowClampPointerAndRejectedGeometryContract") {
    PcgDraggableWindow window;
    REQUIRE(window.configure(-100.f, 100.f, 200.f, 100.f, 1920.f, 1080.f, 2.f).ok());
    REQUIRE(window.drag(100.f, -300.f).ok());
    CHECK_EQ(window.getX(), -100.f);
    CHECK_EQ(window.getY(), 50.f);
    REQUIRE(window.drag(-10000.f, 10000.f).ok());
    CHECK_EQ(window.getX(), -1820.f);
    CHECK_EQ(window.getY(), 1030.f);
    window.pointerDown(false);
    CHECK(window.consumeBringToFront() == PcgUiRequestStatus::Requested);
    CHECK(window.consumeBringToFront() == PcgUiRequestStatus::None);
    window.pointerDown(true);
    CHECK_EQ(window.getX(), -100.f);
    CHECK_EQ(window.getY(), 100.f);
    CHECK(!window.configure(0.f, 0.f, 3000.f, 80.f, 800.f, 600.f, 1.f).ok());
    CHECK_EQ(window.getX(), -100.f);
    CHECK(!window.drag(std::numeric_limits<float>::quiet_NaN(), 0.f).ok());
    CHECK_EQ(window.getY(), 100.f);
}

TEST_CASE("ui.pcg.loadingProgressFadeAndTimeoutDiagnosticContract") {
    SUB_CASE("progress completion starts fade and hides after duration") {
        PcgLoadingScreen screen;
        REQUIRE(screen.configure(2.f).ok());
        screen.onLoadProgressStarted();
        CHECK(screen.getCanvasVisible());
        CHECK_EQ(screen.getProgress(), 0.f);
        REQUIRE(screen.onLoadProgressUpdated(.75f).ok());
        CHECK_EQ(screen.getProgress(), .75f);
        REQUIRE(screen.onLoadProgressUpdated(1.f).ok());
        CHECK(screen.getFading());
        CHECK(!screen.getProgressVisible());
        REQUIRE(screen.tick(.25f).ok());
        CHECK_EQ(screen.getBackgroundAlpha(), .5f);
        REQUIRE(screen.tick(.25f).ok());
        CHECK(!screen.getCanvasVisible());
    };
    SUB_CASE("timeout collects references only between begin and end") {
        PcgLoadingScreen screen;
        CHECK(!screen.addMissingScene("tile", "impostor").ok());
        screen.beginTimeout();
        auto scene = screen.addMissingScene("Terrain", "Impostor");
        REQUIRE(scene.ok());
        REQUIRE(screen.addRegularReference(scene.value(), "Biome Loader").ok());
        REQUIRE(screen.addImpostorReference(scene.value(), "Camera").ok());
        REQUIRE(screen.endTimeout().ok());
        CHECK(screen.getFading());
        CHECK(screen.getTimeoutMessage().find("Terrain") != std::string::npos);
        CHECK(screen.getTimeoutMessage().find("Biome Loader") != std::string::npos);
        CHECK(screen.getTimeoutMessage().find("Camera") != std::string::npos);
        CHECK(!screen.endTimeout().ok());
    };
}

TEST_CASE("ui.pcg.screenshotNoticeEndFrameLifetimeAndCancellationContract") {
    PcgScreenshotSavedNotice notice;
    REQUIRE(notice.configure(true, 1.f).ok());
    REQUIRE(notice.request("shots/a.png").ok());
    CHECK(notice.getPending());
    CHECK(!notice.getVisible());
    REQUIRE(notice.tick(10.f).ok());
    CHECK(!notice.getVisible());
    REQUIRE(notice.endFrame(10.f).ok());
    CHECK(notice.getVisible());
    CHECK_EQ(notice.getPath(), "shots/a.png");
    REQUIRE(notice.tick(10.999f).ok());
    CHECK(notice.getVisible());
    REQUIRE(notice.tick(11.f).ok());
    CHECK(!notice.getVisible());
    REQUIRE(notice.request("b.png").ok());
    CHECK(!notice.configure(true, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(notice.getPending());
    REQUIRE(notice.configure(false, 1.f).ok());
    CHECK(!notice.getPending());
    CHECK(!notice.getVisible());
}

TEST_CASE("ui.pcg.tooltipTriggerDelayPlacementAndDismissalContract") {
    PcgTooltipManager tooltip;
    REQUIRE(tooltip.addTooltip("camera.fov", "FOV", "Field of view").ok());
    REQUIRE(tooltip.addTooltip("camera.fov.extra", "Later", "Other").ok());
    auto found = tooltip.enter(0, "fov", 10.f);
    REQUIRE(found.ok());
    CHECK(found.value());
    CHECK(tooltip.getPending());
    REQUIRE(tooltip.tick(11.9f, 100.f, 200.f, 1000.f, 800.f, false).ok());
    CHECK(!tooltip.getVisible());
    REQUIRE(tooltip.tick(12.f, 100.f, 200.f, 1000.f, 800.f, false).ok());
    CHECK(tooltip.getVisible());
    CHECK_EQ(tooltip.getHeader(), "FOV");
    auto removed = tooltip.removeTooltip("fov");
    REQUIRE(removed.ok());
    CHECK(removed.value());
    CHECK_EQ(tooltip.getCount(), 1);
    REQUIRE(tooltip.exit(0).ok());
    CHECK(!tooltip.getVisible());
    CHECK(!tooltip.configure(3, 0, 1, 1, 1, 10).ok());
}
}  // namespace test_ui_pcg_controls
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_photo_mode_color_picker)
#include "ui/PcgPhotoModeColorPicker.h"
#include "ui/PcgPhotoModePanels.h"
#include "ui/PcgPhotoModeRanges.h"
#include "ui/PcgPhotoModeRuntimeUI.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <string>

namespace test_ui_photo_mode_controls {
using namespace eve::ui;

TEST_CASE("ui.photoMode.colorPickerOpenEditResetAndInvalidInputContract") {
    PcgPhotoModeColorPicker picker;
    REQUIRE(picker.setLast(.1f, .2f, .3f, -2.f).ok());
    REQUIRE(picker.open(.2f, .3f, .4f, 2.f, true).ok());
    CHECK(picker.getOpen());
    CHECK_EQ(picker.getRed(), .4f);
    CHECK_EQ(picker.getPreviewRed(), .8f);
    CHECK_EQ(picker.getPreviewGreen(), 1.2f);
    CHECK_EQ(picker.getHdrSwatch(), .7f);
    CHECK(picker.consumeBringToFront() == PcgUiRequestStatus::Requested);
    CHECK(picker.consumeBringToFront() == PcgUiRequestStatus::None);
    REQUIRE(picker.setRed(.7f).ok());
    REQUIRE(picker.setGreen(.8f).ok());
    REQUIRE(picker.setBlue(.9f).ok());
    REQUIRE(picker.setHdr(3.f).ok());
    CHECK_EQ(picker.getChangeRevision(), std::uint64_t{4});
    CHECK(!picker.setHdr(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_EQ(picker.getAlpha(), 3.f);
    picker.reset();
    CHECK_EQ(picker.getRed(), .1f);
    CHECK_EQ(picker.getAlpha(), -2.f);
    CHECK_EQ(picker.getPreviewRed(), -.2f);
    picker.setFocusedName("Fog");
    CHECK_EQ(picker.getFocusedName(), "(Fog)");
    picker.close();
    CHECK(!picker.getOpen());
}

TEST_CASE("ui.photoMode.panelSelectionAndButtonVisualStateContract") {
    PcgPhotoModePanels panels;
    REQUIRE(panels.select(4).ok());
    CHECK_EQ(panels.getSelected(), 4);
    for (int index = 0; index < 7; ++index) CHECK_EQ(panels.isActive(index), index == 4);
    CHECK(panels.consumeScrollReset() == PcgUiRequestStatus::Requested);
    CHECK(panels.consumeScrollReset() == PcgUiRequestStatus::None);
    const auto revision = panels.getRevision();
    CHECK(!panels.select(7).ok());
    CHECK_EQ(panels.getSelected(), 4);
    CHECK_EQ(panels.getRevision(), revision);

    PcgPhotoModePanelButton button;
    REQUIRE(button.configure(.1f, .2f, .3f, .4f, .8f, .7f, .6f, .5f).ok());
    CHECK_EQ(button.getNormalR(), .1f);
    CHECK(!button.getLayoutEnabled());
    button.setSelected(true);
    CHECK_EQ(button.getNormalR(), .8f);
    CHECK_EQ(button.getNormalA(), .5f);
    CHECK(button.getLayoutEnabled());
    CHECK(!button.configure(std::numeric_limits<float>::quiet_NaN(), 0, 0, 1, 1, 1, 0, 1).ok());
    CHECK_EQ(button.getNormalR(), .8f);
}

TEST_CASE("ui.photoMode.rangesDefaultBoundsClampAndAtomicUpdateContract") {
    PcgPhotoModeRanges ranges;
    CHECK_EQ(ranges.getCount(), std::uint64_t{57});
    REQUIRE(ranges.getName(0).ok());
    CHECK_EQ(ranges.getName(0).value(), "m_0To1");
    CHECK_EQ(ranges.getName(56).value(), "m_cameraCellSubdivision");
    REQUIRE(ranges.select("m_fieldOfView").ok());
    CHECK_EQ(ranges.getMinimum(), 1.0);
    CHECK_EQ(ranges.getMaximum(), 115.0);
    CHECK(!ranges.getIntegral());
    REQUIRE(ranges.select("m_cameraCellSubdivision").ok());
    CHECK_EQ(ranges.getMinimum(), -8.0);
    CHECK(ranges.getIntegral());
    REQUIRE(ranges.select("m_densityVolumeFogDistance").ok());
    CHECK(std::isinf(ranges.getMaximum()));
    auto clamped = ranges.clamp("m_fieldOfView", 200.0);
    REQUIRE(clamped.ok());
    CHECK_EQ(clamped.value(), 115.0);
    REQUIRE(ranges.setRange("m_fieldOfView", 10.0, 80.0).ok());
    clamped = ranges.clamp("m_fieldOfView", 5.0);
    REQUIRE(clamped.ok());
    CHECK_EQ(clamped.value(), 10.0);
    CHECK(!ranges.setRange("m_fieldOfView", 90.0, 20.0).ok());
    REQUIRE(ranges.select("m_fieldOfView").ok());
    CHECK_EQ(ranges.getMaximum(), 80.0);
    CHECK(!ranges.clamp("missing", 1.0).ok());
}

TEST_CASE("ui.photoMode.runtimeWidgetConfigurationAndInputSynchronizationContract") {
    PcgPhotoModeRuntimeUI control;
    REQUIRE(control.configure(1, "Exposure", "1.5", 1.5f, 0.f, 4.f).ok());
    CHECK(control.getLabelVisible());
    CHECK(control.getSliderVisible());
    CHECK(control.getInputVisible());
    CHECK(!control.getButtonVisible());
    REQUIRE(control.setSliderValue(3.5f).ok());
    CHECK(control.getInputRefresh());
    CHECK_EQ(control.getValueText(), "3.5");
    REQUIRE(control.setSliderValue(4.f).ok());
    CHECK(!control.getInputRefresh());
    control.markSliderUsed();
    REQUIRE(control.setSliderValue(5.f).ok());
    CHECK(control.getInputRefresh());
    REQUIRE(control.configure(1, "Value", "", 0.f, -1.f, 1.f).ok());
    CHECK(control.applyFloatInput("4.25") == PcgPhotoModeInputStatus::Applied);
    CHECK_EQ(control.getValue(), 1.f);
    CHECK(control.applyFloatInput("4x") == PcgPhotoModeInputStatus::Ignored);
    CHECK_EQ(control.getValue(), 1.f);
    REQUIRE(control.configure(13, "Position", "", 0.f, -10.f, 10.f).ok());
    CHECK(control.getVector3Visible());
    CHECK(!control.getSliderVisible());
    CHECK_EQ(PcgPhotoModeRuntimeUI::updateWrap(std::string(69, 'x')), std::string(68, 'x') + "\n");
}
}  // namespace test_ui_photo_mode_controls
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_photo_mode_session)
#include "ui/PcgPhotoModeSession.h"
#include "ui/PcgPhotoModeValues.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace test_ui_photo_mode_session {
using namespace eve::ui;

std::string profile(const char* scene, std::int64_t lighting, float fieldOfView) {
    PcgPhotoModeValues values;
    values.setString("m_lastSceneName", scene).value();
    values.setBool("m_isUsingPcgLighting", lighting >= 0).value();
    values.setInt("m_selectedPcgLightingProfile", lighting).value();
    values.setFloat("m_fieldOfView", fieldOfView).value();
    auto snapshot = values.snapshotJson();
    snapshot.value();
    return std::move(snapshot).takeValue();
}

float fieldOfView(const std::string& json) {
    PcgPhotoModeValues values;
    values.restoreJson(json).value();
    auto value = values.getFloat("m_fieldOfView");
    value.value();
    return std::move(value).takeValue();
}

TEST_CASE("ui.photoMode.sessionCompatibilityControlsLoadAndRestoreContract") {
    const std::string   current = profile("World", 2, 60.f);
    const std::string   saved   = profile("World", 2, 90.f);
    PcgPhotoModeSession session;
    REQUIRE(session.begin(current, saved, true, true, 1, 1, "World", 2).ok());
    CHECK_EQ(session.getLoadDecision(), 1);
    auto working = session.workingJson();
    REQUIRE(working.ok());
    CHECK_EQ(fieldOfView(working.value()), 90.f);
    REQUIRE(session.end(false, true).ok());
    CHECK(!session.getRestoreRequested());
    auto output = session.outputJson();
    REQUIRE(output.ok());
    CHECK_EQ(fieldOfView(output.value()), 90.f);
    CHECK(session.getRemovePhotoCameraRequested());
    CHECK(session.getUnfreezePlayerRequested());
}

TEST_CASE("ui.photoMode.sessionRejectsMismatchedOrMalformedProfilesAtomically") {
    SUB_CASE("scene mismatch selects current profile for restoration") {
        const std::string   current = profile("World", 2, 60.f);
        PcgPhotoModeSession session;
        REQUIRE(session.begin(current, profile("Other", 2, 90.f), true, true, 1, 1, "World", 2).ok());
        CHECK_EQ(session.getLoadDecision(), 2);
        REQUIRE(session.end(true, true).ok());
        CHECK(session.getRestoreRequested());
        auto output = session.outputJson();
        REQUIRE(output.ok());
        CHECK_EQ(fieldOfView(output.value()), 60.f);
    };
    SUB_CASE("active session rejects second begin and bad replacement") {
        const std::string   current = profile("World", 2, 60.f);
        PcgPhotoModeSession session;
        CHECK(!session.begin("bad", "", false, false, 0, 0, "", 0).ok());
        CHECK(!session.getActive());
        REQUIRE(session.begin(current, "", false, false, 0, 0, "World", 2).ok());
        const auto revision = session.getRevision();
        CHECK(!session.begin(current, "", false, false, 0, 0, "World", 2).ok());
        CHECK_EQ(session.getRevision(), revision);
        CHECK(!session.replaceWorkingJson("bad").ok());
        auto working = session.workingJson();
        REQUIRE(working.ok());
        CHECK_EQ(fieldOfView(working.value()), 60.f);
        REQUIRE(session.replaceWorkingJson(profile("World", 2, 75.f)).ok());
        REQUIRE(session.end(true, true).ok());
        CHECK(!session.end(true, true).ok());
    };
    SUB_CASE("lighting flag inconsistent with profile identity rejects saved profile") {
        PcgPhotoModeValues saved;
        saved.restoreJson(profile("World", 2, 90.f)).value();
        saved.setBool("m_isUsingPcgLighting", false).value();
        auto json = saved.snapshotJson();
        REQUIRE(json.ok());
        PcgPhotoModeSession session;
        REQUIRE(session.begin(profile("World", 2, 60.f), json.value(), true, true, 1, 1, "World", 2).ok());
        CHECK_EQ(session.getLoadDecision(), 2);
    };
}

TEST_CASE("ui.pcgPhotoModeValues.roundTripsAllFieldsAndRejectsUnknownSchema") {
    PcgPhotoModeValues source;
    REQUIRE(source.setBool("m_showFPS", true).ok());
    REQUIRE(source.setInt("m_targetFPS", 144).ok());
    REQUIRE(source.setFloat("m_pcgWindSpeed", 0.875f).ok());
    REQUIRE(source.setString("m_lastSceneName", "Alpine World").ok());
    REQUIRE(source.setColor("m_sunColor", 1.25f, .5f, .25f, 2.0f).ok());
    auto encoded = source.snapshotJson();
    REQUIRE(encoded.ok());
    PcgPhotoModeValues restored;
    REQUIRE(restored.restoreJson(encoded.value()).ok());
    auto again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), encoded.value());
    auto fps  = restored.getInt("m_targetFPS");
    auto wind = restored.getFloat("m_pcgWindSpeed");
    REQUIRE(fps.ok());
    REQUIRE(wind.ok());
    CHECK_EQ(fps.value(), int64_t(144));
    CHECK_EQ(wind.value(), .875f);
    REQUIRE(restored.selectColor("m_sunColor").ok());
    CHECK_EQ(restored.getColorR(), 1.25f);
    CHECK_EQ(restored.getColorA(), 2.0f);
    const auto  before = again.value();
    std::string wrong  = before;
    auto        pos    = wrong.find("eve.ui.pcg-photo-mode-values");
    REQUIRE(pos != std::string::npos);
    wrong.replace(pos, 29, "eve.ui.wrong-photo-values");
    CHECK(!restored.restoreJson(wrong).ok());
    auto after = restored.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before);
}

TEST_CASE("ui.photoMode.valueSchemaDefaultsTypesAndResetContract") {
    PcgPhotoModeValues values;
    CHECK_EQ(values.getFieldCount(), std::uint64_t{102});
    REQUIRE(values.getFieldName(0).ok());
    CHECK_EQ(values.getFieldName(0).value(), "m_isUsingPcgLighting");
    CHECK_EQ(values.getFieldName(101).value(), "m_cameraCellSubdivision");
    REQUIRE(values.getFloat("m_fieldOfView").ok());
    CHECK_EQ(values.getFloat("m_fieldOfView").value(), 60.f);
    CHECK_EQ(values.getInt("m_targetFPS").value(), std::int64_t{-1});
    CHECK_EQ(values.getInt("m_screenshotImageFormat").value(), std::int64_t{2});
    CHECK(values.getBool("m_loadSavedSettings").value());
    REQUIRE(values.setFloat("m_fieldOfView", 90.f).ok());
    CHECK(!values.setInt("m_fieldOfView", 90).ok());
    CHECK(!values.setFloat("m_fieldOfView", std::numeric_limits<float>::infinity()).ok());
    CHECK_EQ(values.getFloat("m_fieldOfView").value(), 90.f);
    CHECK(!values.setBool("missing", true).ok());
    CHECK(!values.getFieldName(102).ok());
    REQUIRE(values.setColor("m_fogColor", .1f, .2f, .3f, .4f).ok());
    REQUIRE(values.selectColor("m_fogColor").ok());
    CHECK_EQ(values.getColorB(), .3f);
    values.resetDefaults();
    REQUIRE(values.selectColor("m_fogColor").ok());
    CHECK_EQ(values.getColorR(), 0.f);
    CHECK_EQ(values.getColorA(), 1.f);
}
}  // namespace test_ui_photo_mode_session
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_color_palette)
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

namespace test_ui_palette {
using namespace eve::ui;

TEST_CASE("ui.palette.declarationAndBuilderPreserveRgbaThroughFlattening") {
    auto resolved = UIHost::resolve(UIHost::createHost("palette"));
    REQUIRE(resolved.has_value());
    resolved->get().setTree(window("Colors", {colorPalette("Accent", .1f, .2f, .3f, .4f, "accent")}, "root"));
    auto accent = resolved->get().findById("accent");
    REQUIRE(accent.has_value());
    CHECK(accent->get().type == NodeType::ColorPalette);
    CHECK_EQ(accent->get().text, "Accent");
    CHECK_EQ(accent->get().tintR, .1f);
    CHECK_EQ(accent->get().tintG, .2f);
    CHECK_EQ(accent->get().tintB, .3f);
    CHECK_EQ(accent->get().tintA, .4f);

    UI* module = UI::create();
    module->beginBuild();
    module->beginWindow("Editor", "root");
    module->addColorPalette("Fill", .8f, .1f, .2f, 1.f, "fill");
    module->end();
    REQUIRE(module->mountBuildAs("palette-builder"));
    CHECK_EQ(module->getColorR("fill"), .8f);
    CHECK_EQ(module->getColorG("fill"), .1f);
    module->setColor("fill", 0.f, 1.f, .5f, .25f);
    CHECK_EQ(module->getColorR("fill"), 0.f);
    CHECK_EQ(module->getColorG("fill"), 1.f);
    CHECK_EQ(module->getColorB("fill"), .5f);
    CHECK_EQ(module->getColorA("fill"), .25f);
}
}  // namespace test_ui_palette
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_control_semantics)
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <string>

namespace test_ui_semantics {
using namespace eve::ui;

UIHost* host(const char* name) {
    auto resolved = UIHost::resolve(UIHost::createHost(name));
    return resolved ? &resolved->get() : nullptr;
}

UINode* node(UIHost* owner, const char* id) {
    auto found = owner->findById(id);
    return found ? &found->get() : nullptr;
}

TEST_CASE("ui.semantics.widgetPoliciesSurviveFlatteningAndReconcile") {
    UIHost* owner = host("policies");
    REQUIRE(owner != nullptr);
    owner->setTree(window("Controls",
                          {button("Apply", "apply")
                               .withEnabled(false)
                               .withFocusMode(FocusMode::Click)
                               .withMouseFilter(MouseFilter::Ignore)
                               .withTabIndex(7)
                               .withFocusOrder("cancel", "reset")
                               .withFocusNeighbors("left", "right", "up", "down")
                               .withAccessibility(AccessibilityRole::Button, "Apply changes", "Commits edits")},
                          "root"));
    UINode* apply = node(owner, "apply");
    REQUIRE(apply != nullptr);
    CHECK(!apply->enabled);
    CHECK(apply->focusMode == FocusMode::Click);
    CHECK(apply->mouseFilter == MouseFilter::Ignore);
    CHECK_EQ(apply->tabIndex, 7);
    CHECK_EQ(apply->focusPrevious, "cancel");
    CHECK_EQ(apply->focusRight, "right");
    CHECK_EQ(apply->accessibilityName, "Apply changes");
    owner->setTreeReconcile(window("Controls",
                                   {button("Apply now", "apply")
                                        .withFocusMode(FocusMode::All)
                                        .withMouseFilter(MouseFilter::Pass)
                                        .withTabIndex(2)
                                        .withAccessibility(AccessibilityRole::Button, "Apply now")},
                                   "root"));
    apply = node(owner, "apply");
    REQUIRE(apply != nullptr);
    CHECK(apply->enabled);
    CHECK(apply->focusMode == FocusMode::All);
    CHECK(apply->mouseFilter == MouseFilter::Pass);
    CHECK_EQ(apply->tabIndex, 2);
    CHECK(apply->focusPrevious.empty());
    CHECK_EQ(apply->accessibilityName, "Apply now");
}

TEST_CASE("ui.semantics.focusEligibilityTabOrderAndExplicitNeighbors") {
    UIHost* owner = host("focus");
    REQUIRE(owner != nullptr);
    owner->setTree(window(
        "Focus",
        {text("Label", "label"), button("Click only", "click").withFocusMode(FocusMode::Click).withTabIndex(0),
         button("Disabled", "disabled").withEnabled(false).withTabIndex(5), button("Second", "second").withTabIndex(10),
         button("First", "first")
             .withTabIndex(20)
             .withFocusOrder("", "special")
             .withFocusNeighbors("second", "special", "second", "special"),
         button("Special", "special").withTabIndex(30).withFocusOrder("second", "")},
        "root"));
    CHECK(!owner->requestFocusById("missing"));
    CHECK(!owner->requestFocusById("disabled"));
    CHECK(!owner->requestFocusById("root"));
    CHECK(owner->moveFocus(FocusDirection::Next));
    CHECK(node(owner, "second")->focusRequested);
    node(owner, "second")->focused = true;
    CHECK(owner->moveFocus(FocusDirection::Next));
    CHECK(node(owner, "first")->focusRequested);
    node(owner, "second")->focused = false;
    node(owner, "first")->focused  = true;
    CHECK(owner->moveFocus(FocusDirection::Right));
    CHECK(node(owner, "special")->focusRequested);
    CHECK(owner->requestFocusById("first"));
    owner->setEnabledById("first", false);
    CHECK(!node(owner, "first")->focusRequested);
}

TEST_CASE("ui.semantics.pointerPassBubblesPastIgnoreAndStopContains") {
    int     childClicks   = 0;
    int     ignoredClicks = 0;
    int     rootClicks    = 0;
    UIHost* owner         = host("pointer");
    REQUIRE(owner != nullptr);
    owner->setTree(
        window(
            "Routing",
            {group({button("Action", "action", [&] { ++childClicks; }).withMouseFilter(MouseFilter::Pass)}, "ignored")
                 .withClick([&] { ++ignoredClicks; })
                 .withMouseFilter(MouseFilter::Ignore)},
            "root")
            .withClick([&] { ++rootClicks; })
            .withMouseFilter(MouseFilter::Stop));
    UINode* action = node(owner, "action");
    REQUIRE(action != nullptr);
    auto tree = owner->tree();
    REQUIRE(!tree->nodes.empty());
    UIEvent event;
    event.host         = owner->handle();
    event.hostName     = owner->getName();
    event.nodeId       = action->id;
    event.nodeIndex    = static_cast<int>(action - tree->nodes.data());
    event.kind         = "click";
    event.handlerIndex = action->handlerClick;
    UISystem::pendingEvents().push_back(event);
    UISystem::dispatchEvents();
    CHECK_EQ(childClicks, 1);
    CHECK_EQ(ignoredClicks, 0);
    CHECK_EQ(rootClicks, 1);
    action->mouseFilter = MouseFilter::Stop;
    UISystem::pendingEvents().push_back(event);
    UISystem::dispatchEvents();
    CHECK_EQ(childClicks, 2);
    CHECK_EQ(rootClicks, 1);
}
}  // namespace test_ui_semantics
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_drag_drop)
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <utility>

namespace test_ui_drag_drop {
using namespace eve::ui;

TEST_CASE("ui.dragDrop.payloadAndAcceptedTypeSurviveTreeReconcile") {
    auto resolved = UIHost::resolve(UIHost::createHost("drag-drop"));
    REQUIRE(resolved.has_value());
    UIHost& owner = resolved->get();
    owner.setTree(window("Canvas",
                         {button("Asset", "asset").withDragSource("asset", "textures/stone.png"),
                          button("Viewport", "viewport").withDropTarget("asset")},
                         "root"));
    auto source = owner.findById("asset");
    auto target = owner.findById("viewport");
    REQUIRE(source.has_value());
    REQUIRE(target.has_value());
    CHECK(source->get().dragSource);
    CHECK_EQ(source->get().dragPayloadType, "asset");
    CHECK_EQ(source->get().dragPayloadText, "textures/stone.png");
    CHECK(target->get().dropTarget);
    CHECK_EQ(target->get().acceptedDropType, "asset");
    owner.setTreeReconcile(
        window("Canvas", {button("Asset", "asset").withDragSource("entity", "unit-42"), button("Viewport", "viewport")},
               "root"));
    source = owner.findById("asset");
    target = owner.findById("viewport");
    REQUIRE(source.has_value());
    REQUIRE(target.has_value());
    CHECK_EQ(source->get().dragPayloadType, "entity");
    CHECK_EQ(source->get().dragPayloadText, "unit-42");
    CHECK(!target->get().dropTarget);
    CHECK(target->get().acceptedDropType.empty());
}

TEST_CASE("ui.dragDrop.completedEventOwnsPayloadAndQueueIsConsumable") {
    UISystem::dropQueue().clear();
    UISystem::dropQueue().push_back(
        {DragDropOrigin::Internal, "assets", "stone", "scene", "viewport", "asset", "textures/stone.png"});
    auto drop = UISystem::consumeDrop();
    REQUIRE(drop.has_value());
    CHECK_EQ(drop->sourceHostName, "assets");
    CHECK_EQ(drop->sourceNodeId, "stone");
    CHECK_EQ(drop->targetHostName, "scene");
    CHECK_EQ(drop->targetNodeId, "viewport");
    CHECK_EQ(drop->payloadType, "asset");
    CHECK_EQ(drop->payloadText, "textures/stone.png");
    CHECK(!UISystem::consumeDrop().has_value());
#if (defined(_WIN32) || defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    CHECK(UISystem::dragDropSupport() == DragDropSupport::Supported);
#else
    CHECK(UISystem::dragDropSupport() == DragDropSupport::UnsupportedPlatform);
#endif
}
}  // namespace test_ui_drag_drop
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIMoveRepro)
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <utility>

namespace test_ui_widget_move {
TEST_CASE("ui.widget.moveTransfersNodeIdentity") {
    eve::ui::WidgetDesc original;
    original.id               = "root";
    eve::ui::WidgetDesc moved = std::move(original);
    REQUIRE_EQ(moved.id, "root");
}
}  // namespace test_ui_widget_move
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UITween)
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UiTween.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

namespace test_ui_tween {
using namespace eve::ui;

UIHost* host(UIHostHandle handle) {
    auto found = UIHost::resolve(handle);
    return found ? &found->get() : nullptr;
}

UINode* node(UIHost* owner, const char* id) {
    auto found = owner->findById(id);
    return found ? &found->get() : nullptr;
}

void close(float actual, float expected) { CHECK(std::abs(actual - expected) < .001f); }

TEST_CASE("ui.tween.easingEndpointsMidpointsAndUnknownFallback") {
    close(evaluateUiEase(0.f, "linear"), 0.f);
    close(evaluateUiEase(1.f, "linear"), 1.f);
    close(evaluateUiEase(.5f, "linear"), .5f);
    close(evaluateUiEase(.5f, "smoothstep"), .5f);
    close(evaluateUiEase(.5f, "outQuad"), .75f);
    close(evaluateUiEase(.25f, "unknown"), .25f);
    REQUIRE(evaluateUiEase(.9f, "outBack") > 1.f);
}

TEST_CASE("ui.tween.driverSamplesHostPositionAtInjectedTimes") {
    UiTweenDriver driver;
    auto          handle = UIHost::createHost("driver-position");
    UIHost*       owner  = host(handle);
    REQUIRE(owner != nullptr);
    owner->setTree(window("Window", {text("Ready", "label")}, "root"));
    owner->meta()->hasPos = true;
    owner->meta()->posX   = 10.f;
    owner->meta()->posY   = 20.f;
    driver.animateHostPos(handle, 110.f, 120.f, 1000.f, "linear", 200.f, 0.0);
    CHECK_EQ(driver.hostTweenCount(), std::size_t{1});
    driver.tick(100.0);
    close(owner->meta()->posX, 10.f);
    driver.tick(200.0);
    close(owner->meta()->posX, 10.f);
    driver.tick(700.0);
    close(owner->meta()->posX, 60.f);
    close(owner->meta()->posY, 70.f);
    driver.tick(1200.0);
    close(owner->meta()->posX, 110.f);
    close(owner->meta()->posY, 120.f);
    CHECK_EQ(driver.hostTweenCount(), std::size_t{0});
}

TEST_CASE("ui.tween.convenienceApiImmediateReplaceCancelAndItemPositionContract") {
    UI* module = UI::create();
    REQUIRE(host(module->mountAs(
                "panel", window("Panel", {grid(2, {button("A", "a"), button("B", "b")}, "grid")}, "root"))) != nullptr);
    UIHost* owner = host(module->current());
    REQUIRE(owner != nullptr);
    module->animateHostPos(40.f, 80.f, 0.f, "outQuad");
    close(owner->meta()->posX, 40.f);
    close(owner->meta()->posY, 80.f);
    CHECK_EQ(module->getHostTweenCount(), std::size_t{0});
    module->animateHostSize(320.f, 180.f, 0.f);
    close(owner->meta()->sizeX, 320.f);
    module->setHostOverlay(true);
    module->animateHostOverlayAlpha(.25f, 0.f);
    close(owner->meta()->overlayBgAlpha, .25f);
    UINode* a = node(owner, "a");
    REQUIRE(a != nullptr);
    CHECK(!a->absolute);
    module->animateItemOpacity("a", .4f, 0.f);
    close(a->opacity, .4f);
    module->animateItemPos("a", 12.f, 24.f, 0.f);
    CHECK(a->absolute);
    close(a->posX, 12.f);
    close(a->posY, 24.f);
    module->animateHostPos(100.f, 0.f, 5000.f);
    module->animateHostPos(200.f, 50.f, 5000.f);
    CHECK_EQ(module->getHostTweenCount(), std::size_t{1});
    module->cancelHostTweens();
    CHECK_EQ(module->getHostTweenCount(), std::size_t{0});
}

TEST_CASE("ui.tween.driverClampsOvershootingOpacityAndDropsDestroyedHost") {
    UiTweenDriver driver;
    auto          handle = UIHost::createHost("driver-clamp");
    UIHost*       owner  = host(handle);
    REQUIRE(owner != nullptr);
    owner->setTree(window("Window", {button("Go", "go")}, "root"));
    owner->meta()->overlayBgAlpha = 1.f;
    driver.animateHostOverlayAlpha(handle, 0.f, 1000.f, "outBack", 0.f, 0.0);
    auto started = driver.animateItemOpacity(handle, "go", 0.f, 1000.f, "outBack", 0.f, 0.0);
    REQUIRE(started.ok());
    driver.tick(900.0);
    CHECK(owner->meta()->overlayBgAlpha >= 0.f);
    CHECK(owner->meta()->overlayBgAlpha <= 1.f);
    CHECK(node(owner, "go")->opacity >= 0.f);
    CHECK(node(owner, "go")->opacity <= 1.f);
    ecs::DestroyEntity(owner);
    CHECK(!UIHost::resolve(handle).has_value());
    driver.tick(1000.0);
    CHECK_EQ(driver.hostTweenCount(), std::size_t{0});
    CHECK_EQ(driver.itemTweenCount(), std::size_t{0});
}
}  // namespace test_ui_tween
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_nine_patch)
#include "image/ImageData.h"
#include "ui/NinePatch.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>

namespace test_ui_nine_patch {
using eve::image::ImageData;
using eve::ui::NinePatchInfo;
using Color = ImageData::Colorf;

void mark(ImageData& image, int x, int y) { image.setPixel(x, y, Color{0.f, 0.f, 0.f, 1.f}); }

TEST_CASE("ui.ninePatch.markerRunsProduceContentBoundsAndCroppedImage") {
    ImageData source(8, 7, "RGBA8");
    for (int y = 1; y < 6; ++y) {
        for (int x = 1; x < 7; ++x) source.setPixel(x, y, Color{.2f, .4f, .8f, 1.f});
    }
    for (int x = 2; x <= 4; ++x) mark(source, x, 0);
    for (int y = 2; y <= 4; ++y) mark(source, 0, y);
    for (int x = 2; x <= 5; ++x) mark(source, x, 6);
    for (int y = 3; y <= 5; ++y) mark(source, 7, y);
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
    REQUIRE(static_cast<bool>(cropped));
    CHECK_EQ(cropped->getWidth(), 6);
    CHECK_EQ(cropped->getHeight(), 5);
    const Color pixel = cropped->getPixel(0, 0);
    CHECK(pixel.r > .19f);
    CHECK(pixel.b > .79f);
}

TEST_CASE("ui.ninePatch.disjointStretchMarkersReturnAnError") {
    ImageData source(7, 7, "RGBA8");
    mark(source, 1, 0);
    mark(source, 3, 0);
    mark(source, 0, 2);
    mark(source, 0, 3);
    NinePatchInfo patchInfo;
    std::string   error;
    CHECK(!eve::ui::parseNinePatch(source, patchInfo, &error));
    REQUIRE(error.find("multiple stretch runs") != std::string::npos);
}
}  // namespace test_ui_nine_patch
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_parent_scaler)
#include "ui/ParentScaler.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace test_ui_parent_scaler {
using namespace eve::ui;

TEST_CASE("ui.parentScaler.canvasHeightChangesEmitOnlyNeededUpdates") {
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
    CHECK_EQ(output.height, .1f);
}

TEST_CASE("ui.parentScaler.missingCanvasOrTargetsAndInvalidSizeAreAtomic") {
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
    const auto beforeState  = state;
    const auto beforeOutput = output;
    input.canvasHeight      = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateParentScaler(&state, &output, &settings, &input));
    CHECK_EQ(state.lastScaleHeight, beforeState.lastScaleHeight);
    CHECK_EQ(output.applyHeight, beforeOutput.applyHeight);
}
}  // namespace test_ui_parent_scaler
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_world_anchor)
#include "ui/UIHost.h"
#include "ui/WorldAnchorProjection.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <glm/mat4x4.hpp>

namespace test_ui_world_anchor {
using namespace eve::ui;

void close(float actual, float expected) { CHECK(std::abs(actual - expected) < .001f); }

TEST_CASE("ui.worldAnchor.projectionMapsCenterAndDistanceScale") {
    UIHost::WorldAnchor anchor;
    anchor.enabled           = true;
    anchor.worldZ            = .5f;
    anchor.distanceScale     = true;
    anchor.referenceDistance = 2.f;
    anchor.minScale          = .5f;
    anchor.maxScale          = 1.5f;
    const auto projected     = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 2.5f, 800.f, 600.f);
    CHECK(projected.render);
    REQUIRE(projected.state == WorldAnchorState::Visible);
    close(projected.screenX, 400.f);
    close(projected.screenY, 300.f);
    close(projected.scale, 1.f);
}

TEST_CASE("ui.worldAnchor.outsideViewportPolicyHidesOrClamps") {
    UIHost::WorldAnchor anchor;
    anchor.enabled = true;
    anchor.worldX  = 2.f;
    anchor.worldZ  = .5f;
    auto projected = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 0.f, 100.f, 80.f);
    CHECK(!projected.render);
    CHECK(projected.state == WorldAnchorState::OutsideViewport);
    anchor.edgePolicy = WorldAnchorEdgePolicy::Clamp;
    anchor.safeMargin = 7.f;
    projected         = projectWorldAnchor(anchor, glm::mat4(1.f), 0.f, 0.f, 0.f, 100.f, 80.f);
    CHECK(projected.render);
    REQUIRE(projected.state == WorldAnchorState::OutsideViewport);
    close(projected.screenX, 93.f);
}

TEST_CASE("ui.worldAnchor.behindCameraAndHostDetachContract") {
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
    CHECK(projected.state == WorldAnchorState::BehindCamera);
    auto resolved = UIHost::resolve(UIHost::createHost("anchored"));
    REQUIRE(resolved.has_value());
    resolved->get().setWorldAnchor(1.f, 2.f, 3.f);
    CHECK(resolved->get().worldAnchor()->enabled);
    resolved->get().clearWorldAnchor();
    CHECK(!resolved->get().worldAnchor()->enabled);
    CHECK(resolved->get().worldAnchor()->state == WorldAnchorState::Disabled);
}

TEST_CASE("ui.worldAnchor.overlapPriorityDisplacesLowerAndSuppressesBounded") {
    WorldAnchorLayoutItem lower;
    lower.stableIndex     = 2;
    lower.screenX         = 50.f;
    lower.screenY         = 50.f;
    lower.width           = 20.f;
    lower.height          = 10.f;
    lower.pivotX          = .5f;
    lower.pivotY          = .5f;
    lower.padding         = 2.f;
    lower.maxDisplacement = 30.f;
    lower.priority        = 1;
    lower.avoidOverlap    = true;
    auto higher           = lower;
    higher.stableIndex    = 7;
    higher.priority       = 10;
    const auto resolved   = resolveWorldAnchorOverlaps({lower, higher}, 100.f, 100.f);
    REQUIRE_EQ(resolved.size(), std::size_t{2});
    CHECK_EQ(resolved[0].stableIndex, 2);
    CHECK(resolved[0].render);
    close(resolved[0].displacementY, -12.f);
    CHECK_EQ(resolved[1].stableIndex, 7);
    CHECK(resolved[1].render);
    close(resolved[1].displacementY, 0.f);
    lower.maxDisplacement = 0.f;
    const auto bounded    = resolveWorldAnchorOverlaps({higher, lower}, 100.f, 100.f);
    REQUIRE_EQ(bounded.size(), std::size_t{2});
    CHECK(bounded[0].render);
    CHECK(!bounded[1].render);
}
}  // namespace test_ui_world_anchor
#endif
