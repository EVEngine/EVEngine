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

[[maybe_unused]] ui::UIHost *resolveHost(ui::UIHostHandle handle) {
    auto host = ui::UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

[[maybe_unused]] ui::UINode *findNode(ui::UIHost *host, const std::string &id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

[[maybe_unused]] ui::UINode *findNode(ui::UIHostHandle handle, const std::string &id) { return findNode(resolveHost(handle), id); }

}  // namespace


static const char *kScriptComponentContent = R"SQ(
[[maybe_unused]] function testScriptComponent() {
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

TEST_CASE_FIXTURE(UIScriptComponentTest, "UI.adv.scriptUIComponent") {
    CHECK(vm.callFunc(vm.findFunc("testScriptComponent"), vm).toBool());
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


static const char *kScriptCallbackContent = R"SQ(
[[maybe_unused]] function testScriptCallbacks() {
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
[[maybe_unused]] function fireTestEvents() {
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
    host->setTree(ui::window(
        "S",
        {
            ui::text("a", "a"),
            ui::button("b", "b"),
            ui::row({ui::button("one", "one").withFlexBasis(70.f).withFlexShrink(0.f),
                     ui::button("two", "two").withFlexBasis(70.f).withFlexShrink(0.f)},
                    "wrapped")
                .withSize(100.f, 60.f)
                .withFlexWrap(ui::FlexWrap::Wrap)
                .withGaps(4.f, 6.f)
                .withOverflow(ui::OverflowMode::Clip),
            ui::grid(2,
                     {ui::button("g1", "g1").withAspectRatio(2.f),
                      ui::button("g2", "g2").withAspectRatio(2.f),
                      ui::button("g3", "g3").withGridColumnSpan(2)},
                     "grid")
                .withSize(180.f, 30.f)
                .withGaps(8.f, 5.f)
                .withOverflow(ui::OverflowMode::Scroll),
        },
        "root"));
    ui::UISystem::render();
    const ui::UIStats &s = ui::UISystem::stats();
    CHECK_GE(s.hostCount, 1);
    CHECK_GE(s.nodeCount, 3);
    CHECK(s.measureMs >= 0.0);
    CHECK(s.walkMs >= 0.0);
    CHECK(findNode(host, "wrapped")->layoutOverflowX >= 0.f);
    CHECK(findNode(host, "grid")->layoutOverflowY > 0.f);

    ImGui::EndFrame();
    ImGui::DestroyContext(headless);
    if (saved) ImGui::SetCurrentContext(saved);
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
