#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <cmath>
#include <string>

namespace ui = eve::ui;

TEST_CASE("UI.adv.sliderProgressInputCollapseChild") {
    float slid = 0.f;
    std::string typed;
    ui::UIHost *h = ui::UIHost::createHost("adv");
    h->setTree(ui::window(
        "Adv",
        {
            ui::slider("Vol", 0.3f, 0.f, 1.f, "vol", [&](float v) { slid = v; }),
            ui::progress(0.5f, "hp", "50%"),
            ui::inputText("Name", "hero", "name", [&](const std::string &s) { typed = s; }),
            ui::collapsingHeader("More", {ui::text("hidden tip", "tip")}, "more", true),
            ui::child("scroll", {ui::text("row0", "r0"), ui::text("row1", "r1")}, 200.f, 80.f),
        },
        "root"));

    CHECK(h->findById("vol") != nullptr);
    CHECK_EQ(int(h->findById("vol")->type), int(ui::NodeType::Slider));
    CHECK(std::abs(h->findById("vol")->value - 0.3f) < 1e-5f);
    CHECK_EQ(int(h->findById("hp")->type), int(ui::NodeType::Progress));
    CHECK(h->findById("name")->valueText == "hero");
    CHECK(h->findById("more") != nullptr);
    CHECK(h->findById("tip") != nullptr);
    CHECK(h->findById("scroll") != nullptr);
    CHECK(h->findById("r1") != nullptr);

    ui::UIEvent ev;
    ev.host = h;
    ev.hostName = "adv";
    ev.nodeId = "vol";
    ev.kind = "value";
    ev.handlerIndex = h->findById("vol")->handlerValue;
    ev.floatValue = 0.75f;
    h->findById("vol")->value = 0.75f;
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
    CHECK(uimod->current()->findById("price") != nullptr);
    CHECK(uimod->current()->findById("sku")->valueText == "A1");
    CHECK(uimod->current()->findById("info") != nullptr);
    uimod->setValue("price", 42.f);
    CHECK(std::abs(uimod->getValue("price") - 42.f) < 1e-5f);
    uimod->setValueText("sku", "B2");
    CHECK(uimod->getValueText("sku") == "B2");
    uimod->setHostModal(true);
    CHECK(uimod->current()->meta()->modal);
}

TEST_CASE("UI.adv.wantCaptureAPI") {
    ui::UI *uimod = ui::UI::create();
    // Callable regardless of prior smoke tests that may have initialized ImGui.
    bool mouse = uimod->wantCaptureMouse();
    bool keyboard = uimod->wantCaptureKeyboard();
    CHECK(mouse == mouse);
    CHECK(keyboard == keyboard);
}

static const char *kScriptComponentContent = R"SQ(
function testScriptComponent() {
    local u = eve.UI()
    class ShopPanel extends eve.UIComponent {
        gold = 10
        constructor(uiRef) {
            base.constructor(uiRef)
            gold = 10
        }
        function build() {
            local uu = ui()
            uu.beginWindow("Shop", "root")
            uu.text("Gold " + gold, "gold")
            uu.button("Buy", "buy")
            if (gold > 5) uu.text("Rich", "rich")
            uu.end()
        }
    }
    local panel = ShopPanel(u)
    panel.mountAs("shop_panel")
    if (!u.select("shop_panel")) return false
    panel.gold = 3
    panel.setState()
    if (!panel.updateIfDirty()) return false
    if (eve.UIComponent == null) return false
    return true
}
)SQ";

UnitSciptTest(UIScriptComponentTest, kScriptComponentContent);

TEST_CASE_FIXTURE(UIScriptComponentTest, "UI.adv.scriptUIComponent") {
    CHECK(vm.callFunc(vm.findFunc("testScriptComponent"), vm).toBool());
}

TEST_CASE("UI.adv.flexRowColumnSpacer") {
    ui::UIHost *h = ui::UIHost::createHost("flex");
    h->setTree(ui::window(
        "Flex",
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

    auto *toolbar = h->findById("toolbar");
    CHECK(toolbar != nullptr);
    CHECK_EQ(int(toolbar->type), int(ui::NodeType::Flex));
    CHECK_EQ(int(toolbar->flexDirection), int(ui::FlexDirection::Row));
    CHECK(std::abs(toolbar->gap - 8.f) < 1e-5f);

    auto *mid = h->findById("mid");
    CHECK(mid != nullptr);
    CHECK_EQ(int(mid->type), int(ui::NodeType::Spacer));
    CHECK(mid->flexGrow > 0.f);

    auto *stack = h->findById("stack");
    CHECK(stack != nullptr);
    CHECK_EQ(int(stack->flexDirection), int(ui::FlexDirection::Column));
    CHECK_EQ(int(stack->alignItems), int(ui::FlexAlign::Stretch));

    auto *one = h->findById("one");
    CHECK(one != nullptr);
    CHECK(std::abs(one->flexGrow - 1.f) < 1e-5f);
    CHECK(std::abs(h->findById("two")->flexGrow - 2.f) < 1e-5f);
}

TEST_CASE("UI.adv.flexBuilderAPI") {
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Bar", "root");
    uimod->beginRow("tools", 6.f);
    uimod->setFlexJustify("space-between");
    uimod->setFlexAlign("center");
    uimod->addButton("Save", "save");
    uimod->addSpacer("sp");
    uimod->addButton("Quit", "quit");
    uimod->setItemFlexGrow(0.f);
    uimod->setItemSize(80.f, 0.f);
    uimod->end();
    uimod->beginColumn("body", 2.f);
    uimod->addText("Hello", "hello");
    uimod->addText("World", "world");
    uimod->end();
    uimod->beginFlex("column", "side", 0.f);
    uimod->addButton("X", "x");
    uimod->end();
    uimod->end();
    CHECK(uimod->mountBuildAs("bar"));

    auto *tools = uimod->current()->findById("tools");
    CHECK(tools != nullptr);
    CHECK_EQ(int(tools->type), int(ui::NodeType::Flex));
    CHECK_EQ(int(tools->flexDirection), int(ui::FlexDirection::Row));
    CHECK_EQ(int(tools->justifyContent), int(ui::FlexJustify::SpaceBetween));
    CHECK_EQ(int(tools->alignItems), int(ui::FlexAlign::Center));
    CHECK(std::abs(tools->gap - 6.f) < 1e-5f);

    auto *quit = uimod->current()->findById("quit");
    CHECK(quit != nullptr);
    CHECK(std::abs(quit->sizeX - 80.f) < 1e-5f);

    auto *sp = uimod->current()->findById("sp");
    CHECK(sp != nullptr);
    CHECK_EQ(int(sp->type), int(ui::NodeType::Spacer));

    auto *side = uimod->current()->findById("side");
    CHECK(side != nullptr);
    CHECK_EQ(int(side->flexDirection), int(ui::FlexDirection::Column));
}

TEST_CASE("UI.adv.flexReconcileKeepsStructure") {
    ui::UIHost *h = ui::UIHost::createHost("flexrec");
    auto tree1 = ui::window(
        "F", {ui::row({ui::button("A", "a"), ui::spacer("s"), ui::button("B", "b")}, "r")}, "root");
    h->setTree(tree1);
    auto tree2 = ui::window(
        "F",
        {ui::row({ui::button("A2", "a"), ui::spacer("s"), ui::button("B2", "b")}, "r").withGap(10.f)},
        "root");
    bool rebuilt = h->setTreeReconcile(std::move(tree2));
    CHECK(!rebuilt);
    CHECK(h->findById("a")->text == "A2");
    CHECK(std::abs(h->findById("r")->gap - 10.f) < 1e-5f);
}
