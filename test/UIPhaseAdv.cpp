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
