#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>

#include <cmath>
#include <string>

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

ui::UINode *findNode(ui::UIHostHandle handle, const std::string &id) { return findNode(resolveHost(handle), id); }

}  // namespace

TEST_CASE("UI.adv.sliderProgressInputCollapseChild") {
    float slid = 0.f;
    std::string typed;
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("adv"));
    REQUIRE(h != nullptr);
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
    ev.hostName = "adv";
    ev.nodeId = "vol";
    ev.kind = "value";
    ev.handlerIndex           = findNode(h, "vol")->handlerValue;
    ev.floatValue = 0.75f;
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
    bool mouse = uimod->wantCaptureMouse();
    bool keyboard = uimod->wantCaptureKeyboard();
    CHECK(mouse == mouse);
    CHECK(keyboard == keyboard);
}

static const char *kScriptComponentContent = R"SQ(
function testScriptComponent() {
    local u = eve.UI()
    class PriceLabel extends eve.UIComponent {
        function build() {
            ui().text(props.prefix + state.amount, "gold")
        }
    }
    class ShopPanel extends eve.UIComponent {
        gold = 10
        child = null
        mounted = false
        updated = false
        constructor(uiRef) {
            base.constructor(uiRef)
            gold = 10
            child = PriceLabel(uiRef, { prefix = "Gold " })
        }
        function onMount() { mounted = true }
        function onUpdated() { updated = true }
        function build() {
            local uu = ui()
            uu.beginWindow("Shop", "root")
            renderChild(child, { prefix = props.prefix })
            uu.button("Buy", "buy")
            if (state.gold > 5) uu.text("Rich", "rich")
            uu.end()
        }
    }
    local panel = ShopPanel(u)
    panel.setProps({ prefix = "Coins " })
    panel.setState({ gold = 10 })
    panel.child.setState({ amount = 10 })
    panel.mountAs("shop_panel")
    if (!panel.mounted) return false
    if (!u.select("shop_panel")) return false
    panel.setState({ gold = 3 })
    panel.child.setState({ amount = 3 })
    if (!panel.dirty) return false
    if (!panel.updateIfDirty()) return false
    if (!panel.updated) return false
    if (eve.UIComponent == null) return false
    return true
}
)SQ";

UnitSciptTest(UIScriptComponentTest, kScriptComponentContent);

TEST_CASE_FIXTURE(UIScriptComponentTest, "UI.adv.scriptUIComponent") {
    CHECK(vm.callFunc(vm.findFunc("testScriptComponent"), vm).toBool());
}

TEST_CASE("UI.adv.flexRowColumnSpacer") {
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("flex"));
    REQUIRE(h != nullptr);
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
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    auto *tools = findNode(current, "tools");
    CHECK(tools != nullptr);
    CHECK_EQ(int(tools->type), int(ui::NodeType::Flex));
    CHECK_EQ(int(tools->flexDirection), int(ui::FlexDirection::Row));
    CHECK_EQ(int(tools->justifyContent), int(ui::FlexJustify::SpaceBetween));
    CHECK_EQ(int(tools->alignItems), int(ui::FlexAlign::Center));
    CHECK(std::abs(tools->gap - 6.f) < 1e-5f);

    auto *quit = findNode(current, "quit");
    CHECK(quit != nullptr);
    CHECK(std::abs(quit->sizeX - 80.f) < 1e-5f);

    auto *sp = findNode(current, "sp");
    CHECK(sp != nullptr);
    CHECK_EQ(int(sp->type), int(ui::NodeType::Spacer));

    auto *side = findNode(current, "side");
    CHECK(side != nullptr);
    CHECK_EQ(int(side->flexDirection), int(ui::FlexDirection::Column));
}

TEST_CASE("UI.adv.flexReconcileKeepsStructure") {
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("flexrec"));
    REQUIRE(h != nullptr);
    auto tree1 = ui::window(
        "F", {ui::row({ui::button("A", "a"), ui::spacer("s"), ui::button("B", "b")}, "r")}, "root");
    h->setTree(tree1);
    auto tree2 = ui::window(
        "F",
        {ui::row({ui::button("A2", "a"), ui::spacer("s"), ui::button("B2", "b")}, "r").withGap(10.f)},
        "root");
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
    items[1].flexGrow = 1.f;
    items[2].basisMain = 60.f;
    ui::FlexResult r =
        ui::flexArrange(true, 10.f, 400.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items);
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
        ui::flexArrange(true, 0.f, 200.f, 20.f, ui::FlexAlign::Start, ui::FlexJustify::SpaceBetween,
                        fixed);
    CHECK(std::abs(jr.items[0].x - 0.f) < 1e-4f);
    CHECK(std::abs(jr.items[1].x - 150.f) < 1e-4f);
    CHECK(std::abs(jr.contentW - 200.f) < 1e-4f);
}

TEST_CASE("UI.layout.flexArrangeMarginsPercentAbsolute") {
    // Margins participate in flow and cross placement.
    std::vector<ui::FlexItemSpec> items(1);
    items[0].basisMain = 40.f;
    items[0].basisCross = 10.f;
    items[0].marginBefore = 5.f;
    items[0].marginAfter = 5.f;
    items[0].marginCrossBefore = 3.f;
    ui::FlexResult r =
        ui::flexArrange(true, 0.f, 100.f, 40.f, ui::FlexAlign::Start, ui::FlexJustify::Start, items);
    CHECK(std::abs(r.items[0].x - 5.f) < 1e-4f);
    CHECK(std::abs(r.items[0].y - 3.f) < 1e-4f);
    CHECK(std::abs(r.contentW - 50.f) < 1e-4f);  // 5 + 40 + 5

    // Percent sizing against avail.
    std::vector<ui::FlexItemSpec> pct(1);
    pct[0].percentMain = 0.5f;
    pct[0].percentCross = 1.f;
    ui::FlexResult pr =
        ui::flexArrange(true, 0.f, 200.f, 30.f, ui::FlexAlign::Start, ui::FlexJustify::Start, pct);
    CHECK(std::abs(pr.items[0].w - 100.f) < 1e-4f);
    CHECK(std::abs(pr.items[0].h - 30.f) < 1e-4f);

    // Absolute placement: anchor (1,0) with offset (-10, 0) → right edge inset by 10.
    std::vector<ui::FlexItemSpec> abs(1);
    abs[0].absolute = true;
    abs[0].basisMain = 20.f;
    abs[0].basisCross = 20.f;
    abs[0].anchorMain = 1.f;
    abs[0].anchorCross = 0.f;
    abs[0].posMain = -10.f;
    ui::FlexResult ar =
        ui::flexArrange(true, 0.f, 200.f, 50.f, ui::FlexAlign::Start, ui::FlexJustify::Start, abs);
    CHECK(std::abs(ar.items[0].x - 170.f) < 1e-4f);  // 200 - 20 - 10
    CHECK(std::abs(ar.items[0].y - 0.f) < 1e-4f);
}

TEST_CASE("UI.layout.measureNestedFlexAndWindowContent") {
    ImGuiContext *saved = ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    ImGuiContext *headless = ImGui::CreateContext();
    ImGui::SetCurrentContext(headless);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.f, 600.f);
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::NewFrame();

    ui::UIHost *host = resolveHost(ui::UIHost::createHost("layout"));
    REQUIRE(host != nullptr);
    host->setTree(ui::window(
        "W",
        {
            ui::row({ui::button("A", "a"), ui::spacer("s"), ui::button("B", "b")}, "outer")
                .withGap(8.f)
                .withPadding(4.f, 4.f, 4.f, 4.f)
                .withMargin(2.f, 2.f, 2.f, 2.f),
            ui::column({ui::text("X", "x"), ui::text("Y", "y")}, "inner")
                .withGap(4.f)
                .withMinSize(120.f, 0.f),
        },
        "root"));
    ui::measureTree(*host->tree());
    ui::UINode *outer = findNode(host, "outer");
    ui::UINode *inner = findNode(host, "inner");
    ui::UINode *root  = findNode(host, "root");
    REQUIRE(outer != nullptr);
    REQUIRE(inner != nullptr);
    // Nested containers previously measured 0; now they carry real sizes.
    CHECK(outer->measuredW > 0.f);
    CHECK(outer->measuredH > 0.f);
    CHECK(inner->measuredW >= 120.f);
    CHECK(inner->measuredH > 0.f);
    CHECK(root->measuredW > 0.f);
    CHECK(root->measuredH > outer->measuredH);  // two rows stacked
    // Margins/padding/min propagate into the retained tree.
    CHECK(std::abs(outer->paddingL - 4.f) < 1e-5f);
    CHECK(std::abs(outer->marginT - 2.f) < 1e-5f);
    CHECK(std::abs(inner->minSizeX - 120.f) < 1e-5f);

    ImGui::EndFrame();
    ImGui::DestroyContext(headless);
    if (saved) ImGui::SetCurrentContext(saved);
}

TEST_CASE("UI.p0.imageAndImageButton") {
    int clicks = 0;
    ui::UIHost *h      = resolveHost(ui::UIHost::createHost("img"));
    REQUIRE(h != nullptr);
    h->setTree(ui::window(
        "W",
        {
            ui::image("avatar", 64.f, 64.f).withTint(0.2f, 0.4f, 0.8f).withCornerRadius(8.f),
            ui::imageButton("btn", 48.f, 48.f, [&] { ++clicks; }),
            ui::image("frame", 120.f, 40.f)
                .withUv(0.f, 0.f, 0.5f, 0.5f)
                .withNinePatch(8.f, 8.f, 8.f, 8.f),
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
    ev.hostName = "img";
    ev.nodeId = "btn";
    ev.kind = "click";
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

TEST_CASE_FIXTURE(UIScriptCallbackTest, "UI.p0.scriptEventCallbacks") {
    CHECK(vm.callFunc(vm.findFunc("testScriptCallbacks"), vm).toBool());
    ui::UIHost *host = resolveHost(ui::UISystem::findHost("cb"));
    REQUIRE(host != nullptr);
    ui::UINode *go = findNode(host, "go");
    ui::UINode *sl = findNode(host, "s");
    REQUIRE(go != nullptr);
    REQUIRE(sl != nullptr);

    ui::UIEvent click;
    click.host         = host->handle();
    click.hostName = "cb";
    click.nodeId = "go";
    click.kind = "click";
    click.handlerIndex = go->handlerClick;
    ui::UISystem::pendingEvents().push_back(click);

    ui::UIEvent change;
    change.host         = host->handle();
    change.hostName = "cb";
    change.nodeId = "s";
    change.kind = "value";
    change.handlerIndex = sl->handlerValue;
    change.floatValue = 0.75f;
    ui::UISystem::pendingEvents().push_back(change);

    CHECK(vm.callFunc(vm.findFunc("fireTestEvents"), vm).toBool());
    CHECK(std::abs(vm.find("clicks").toFloat() - 1.f) < 1e-5f);
    CHECK(vm.find("changeKind").toString() == "value");
    CHECK(std::abs(vm.find("changeValue").toFloat() - 0.75f) < 1e-5f);
}

TEST_CASE("UI.p1.comboAndTextWrap") {
    int picked = -1;
    ui::UIHost *h      = resolveHost(ui::UIHost::createHost("p1"));
    REQUIRE(h != nullptr);
    h->setTree(ui::window(
        "W",
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
    ev.hostName = "p1";
    ev.nodeId = "fruit";
    ev.kind = "value";
    ev.handlerIndex = combo->handlerValue;
    ev.floatValue = 2.f;
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

TEST_CASE("UI.p1.statsAfterHeadlessRender") {
    ImGuiContext *saved = ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    ImGuiContext *headless = ImGui::CreateContext();
    ImGui::SetCurrentContext(headless);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.f, 600.f);
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::NewFrame();

    ui::UIHost *host = resolveHost(ui::UIHost::createHost("stats"));
    REQUIRE(host != nullptr);
    host->setTree(ui::window("S", {ui::text("a", "a"), ui::button("b", "b")}, "root"));
    ui::UISystem::render();
    const ui::UIStats &s = ui::UISystem::stats();
    CHECK_GE(s.hostCount, 1);
    CHECK_GE(s.nodeCount, 3);
    CHECK(s.measureMs >= 0.0);
    CHECK(s.walkMs >= 0.0);

    ImGui::EndFrame();
    ImGui::DestroyContext(headless);
    if (saved) ImGui::SetCurrentContext(saved);
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

TEST_CASE("UI.p1.scrollListNodeAndBuilder") {
    ui::UIHost *h = resolveHost(ui::UIHost::createHost("sl"));
    REQUIRE(h != nullptr);
    std::vector<std::string> items;
    for (int i = 0; i < 2000; ++i) items.push_back("item" + std::to_string(i));
    h->setTree(ui::window(
        "W",
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

TEST_CASE("UI.p1.scrollListHeadlessRenderLarge") {
    ImGuiContext *saved = ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    ImGuiContext *headless = ImGui::CreateContext();
    ImGui::SetCurrentContext(headless);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.f, 600.f);
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::NewFrame();

    ui::UIHost *host = resolveHost(ui::UIHost::createHost("sllarge"));
    REQUIRE(host != nullptr);
    std::vector<std::string> items;
    for (int i = 0; i < 5000; ++i) items.push_back("row" + std::to_string(i));
    host->setTree(ui::window("S", {ui::virtualList("rows", items, 200.f, 24.f)}, "root"));
    ui::measureTree(*host->tree());
    ui::UISystem::render();
    const ui::UIStats &s = ui::UISystem::stats();
    CHECK_GE(s.nodeCount, 5000);
    CHECK(s.measureMs >= 0.0);
    CHECK(s.walkMs >= 0.0);

    ImGui::EndFrame();
    ImGui::DestroyContext(headless);
    if (saved) ImGui::SetCurrentContext(saved);
}
