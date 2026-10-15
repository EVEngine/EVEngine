#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_UIComponentScheduling)
#include "PrivateTestScope.h"
#include <imgui.h>
#include <stdexcept>
#include "ScriptTest.h"
#include "ui/Component.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"
// UIComponentScheduling.cpp
namespace test_UIComponentScheduling {
using namespace eve;


namespace {
class Frame {
public:
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *context  = ImGui::CreateContext();
    Frame() {
        ImGui::SetCurrentContext(context);
        auto &io              = ImGui::GetIO();
        io.DisplaySize        = ImVec2(800.f, 600.f);
        io.IniFilename        = nullptr;
        unsigned char *pixels = nullptr;
        int            width = 0, height = 0;
        io.KeyMap[ImGuiKey_Space] = 32;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Frame() {
        ImGui::DestroyContext(context);
        if (previous) ImGui::SetCurrentContext(previous);
    }
    void render() {
        ImGui::NewFrame();
        try {
            eve::ui::UISystem::render();
        } catch (...) {
            ImGui::EndFrame();
            throw;
        }
        ImGui::EndFrame();
    }
};
class Panel final : public eve::ui::Component {
public:
    int                 builds          = 0;
    bool                editDuringBuild = false;
    bool                fail            = false;
    eve::ui::WidgetDesc build() override {
        ++builds;
        if (editDuringBuild) {
            editDuringBuild = false;
            markDirty();
            eve::ui::detail::flushPendingComponents();
        }
        if (fail) throw std::runtime_error("injected component failure");
        return eve::ui::text("published", "label");
    }
};
}  // namespace
TEST_CASE("UI.componentScheduling.coalescesAndPreservesReentrantEdits") {
    Frame frame;
    Panel panel;
    panel.mountAs("scheduled-ui");
    frame.render();
    CHECK_EQ(panel.builds, 1);
    panel.editDuringBuild = true;
    for (int i = 0; i < 20; ++i) panel.markDirty();
    frame.render();
    CHECK_EQ(panel.builds, 2);
    CHECK(panel.isDirty());
    frame.render();
    CHECK_EQ(panel.builds, 3);
    CHECK(!panel.isDirty());
    frame.render();
    CHECK_EQ(panel.builds, 3);
    {
        Panel destroyed;
        destroyed.mountAs("destroyed-ui");
        destroyed.markDirty();
    }
    frame.render();
}
TEST_CASE("UI.componentScheduling.failedBatchRetainsRemainingWork") {
    Frame frame;
    Panel first, second;
    first.mountAs("failed-ui");
    second.mountAs("remaining-ui");
    frame.render();
    first.fail = true;
    first.markDirty();
    second.markDirty();
    bool threw = false;
    try {
        frame.render();
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);
    CHECK(first.isDirty());
    CHECK(second.isDirty());
    first.fail = false;
    frame.render();
    CHECK(!first.isDirty());
    CHECK(!second.isDirty());
    CHECK_EQ(second.builds, 2);
}


UnitSciptTest(UIScheduledScriptTest, R"SQ(
function verifyScheduledComponent() {
    ::ui <- eve.UI()
    class Scheduled extends eve.UIComponent {
        builds = 0
        fail = false
        edit = false
        function build() {
            builds += 1
            if (edit) { edit = false; markDirty(); eve_ui_flush_components() }
            if (fail) throw "injected script publication failure"
            this.ui().text("published", "script-label")
        }
    }
    local first = Scheduled(eve.UI())
    local second = Scheduled(eve.UI())
    first.mountAs("script-schedule-first")
    second.mountAs("script-schedule-second")
    eve_ui_flush_components()
    if (first.builds != 1 || second.builds != 1) return false
    first.edit = true
    for (local i = 0; i < 20; ++i) first.markDirty()
    eve_ui_flush_components()
    if (first.builds != 2 || !first.dirty) return false
    eve_ui_flush_components()
    if (first.builds != 3 || first.dirty) return false
    first.fail = true
    first.markDirty()
    second.markDirty()
    local threw = false
    try { eve_ui_flush_components() } catch (error) { threw = true }
    if (!threw || !first.dirty || !second.dirty) return false
    first.fail = false
    eve_ui_flush_components()
    return !first.dirty && !second.dirty && second.builds == 2
}
)SQ");
TEST_CASE_FIXTURE(UIScheduledScriptTest, "UI.componentScheduling.scriptCoalescingAndFailureRecovery") {
    CHECK(vm.callFunc(vm.findFunc("verifyScheduledComponent"), vm).toBool());
}
}  // namespace test_UIComponentScheduling
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
namespace test_UIEditorKit {
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


TEST_CASE("UI.editorKit.desktopCompositionRenders") {
    UIHost *host = createHost("desktop_components");
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

    ImGuiContext *savedContext = ImGui::GetCurrentContext();
    IMGUI_CHECKVERSION();
    ImGuiContext *headlessContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(headlessContext);
    ImGuiIO &io           = ImGui::GetIO();
    io.DisplaySize        = ImVec2(1024.f, 720.f);
    io.IniFilename        = nullptr;
    unsigned char *pixels = nullptr;
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
}  // namespace test_UIEditorKit
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
namespace test_UIPhaseAdv {
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
    ImGuiIO &io           = ImGui::GetIO();
    io.DisplaySize        = ImVec2(800.f, 600.f);
    io.IniFilename        = nullptr;
    unsigned char *pixels = nullptr;
    int            w = 0, h = 0;
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
            ui::column({ui::text("X", "x"), ui::text("Y", "y")}, "inner").withGap(4.f).withMinSize(120.f, 0.f),
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
    click.hostName     = "cb";
    click.nodeId       = "go";
    click.kind         = "click";
    click.handlerIndex = go->handlerClick;
    ui::UISystem::pendingEvents().push_back(click);

    ui::UIEvent change;
    change.host         = host->handle();
    change.hostName     = "cb";
    change.nodeId       = "s";
    change.kind         = "value";
    change.handlerIndex = sl->handlerValue;
    change.floatValue   = 0.75f;
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
    ImGuiIO &io           = ImGui::GetIO();
    io.DisplaySize        = ImVec2(800.f, 600.f);
    io.IniFilename        = nullptr;
    unsigned char *pixels = nullptr;
    int            w = 0, h = 0;
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
                     {ui::button("g1", "g1").withAspectRatio(2.f), ui::button("g2", "g2").withAspectRatio(2.f),
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
    ImGuiIO &io           = ImGui::GetIO();
    io.DisplaySize        = ImVec2(800.f, 600.f);
    io.IniFilename        = nullptr;
    unsigned char *pixels = nullptr;
    int            w = 0, h = 0;
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
}  // namespace test_UIPhaseAdv
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
namespace test_UIPhaseB {
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
}  // namespace test_UIPhaseB
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
namespace test_UISystem {
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


TEST_CASE("UISystem.render.headlessImGuiWalk") {
    UIHost *h = resolveHost(UIHost::createHost("walk"));
    REQUIRE(h != nullptr);
    h->setTree(window("Panel", {text("Label", "label"), button("Btn", "btn")}));

    // This test is headless: it drives UISystem::render() with a private ImGui
    // context. When run in the same process after a real-window UI test, the UI
    // module's ImGui backend keeps its own context (and present-overlay hook).
    // ImGui::CreateContext() only auto-switches current when GImGui is NULL, so
    // explicitly switch to the private context and back — otherwise the UI
    // backend's context would be the one NewFrame/EndFrame and DestroyContext
    // operate on, leaving a dangling ImGui context for later present() calls.
    ImGuiContext *savedContext = ImGui::GetCurrentContext();

    IMGUI_CHECKVERSION();
    ImGuiContext *headlessContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(headlessContext);
    ImGuiIO &io           = ImGui::GetIO();
    io.DisplaySize        = ImVec2(800.f, 600.f);
    io.IniFilename        = nullptr;
    unsigned char *pixels = nullptr;
    int            w = 0, htex = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &htex);
    (void)pixels;

    ImGui::NewFrame();
    UISystem::render();
    ImGui::EndFrame();
    ImGui::DestroyContext(headlessContext);
    if (savedContext) ImGui::SetCurrentContext(savedContext);

    CHECK(!h->tree()->dirty);
}
}  // namespace test_UISystem
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_overlay_chrome)
#include "PrivateTestScope.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "ui/Theme.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_overlay_chrome.cpp
namespace test_ui_overlay_chrome {
TEST_CASE("UI.overlay.transparentHostHasNoChromeAndRestoresStyle") {
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *context  = ImGui::CreateContext();
    struct Cleanup {
        ImGuiContext  *previous;
        ImGuiContext  *context;
        eve::ui::Theme theme;
        float          scale;
        ~Cleanup() {
            if (context->WithinFrameScope) ImGui::EndFrame();
            ImGui::DestroyContext(context);
            ImGui::SetCurrentContext(previous);
            eve::ui::globalTheme() = theme;
            eve::ui::setThemeUiScale(scale);
        }
    } cleanup{previous, context, eve::ui::globalTheme(), eve::ui::themeUiScale()};
    eve::ui::setThemeUiScale(1.f);
    auto &io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.f, 600.f);
    for (int key = 0; key < ImGuiKey_COUNT; ++key) io.KeyMap[key] = key;
    unsigned char *pixels = nullptr;
    int            width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto &style                             = ImGui::GetStyle();
    eve::ui::globalTheme().windowBorderSize = 3.f;
    eve::ui::globalTheme().windowPaddingX   = 11.f;
    eve::ui::globalTheme().windowPaddingY   = 13.f;
    const auto handle                       = eve::ui::UIHost::createHost("chrome-contract");
    auto       host                         = eve::ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(eve::ui::window("Chrome", {eve::ui::text("Skin content", "text")}, "root"));
    host->get().meta()->overlay        = true;
    host->get().meta()->overlayBgAlpha = 0.f;
    ImGui::NewFrame();
    eve::ui::UISystem::render();
    auto *window = ImGui::FindWindowByName("Chrome###chrome-contract/Chrome");
    REQUIRE(window != nullptr);
    REQUIRE_EQ(window->WindowBorderSize, 0.f);
    REQUIRE_EQ(window->WindowPadding.x, 0.f);
    REQUIRE_EQ(window->WindowPadding.y, 0.f);
    REQUIRE_EQ(style.WindowBorderSize, 3.f);
    REQUIRE_EQ(style.WindowPadding.x, 11.f);
    REQUIRE_EQ(style.WindowPadding.y, 13.f);
    ImGui::EndFrame();

    // Opaque overlays and normal windows retain their theme chrome.
    for (bool overlay : {true, false}) {
        host->get().meta()->overlay        = overlay;
        host->get().meta()->overlayBgAlpha = 0.8f;
        ImGui::NewFrame();
        eve::ui::UISystem::render();
        REQUIRE_EQ(window->WindowBorderSize, 3.f);
        REQUIRE_EQ(window->WindowPadding.x, 11.f);
        REQUIRE_EQ(window->WindowPadding.y, 13.f);
        ImGui::EndFrame();
    }
    host->get().setVisible(false);
}
}  // namespace test_ui_overlay_chrome
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ui_progress_size)
#include "PrivateTestScope.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "ui/Theme.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ui_progress_size.cpp
namespace test_ui_progress_size {
TEST_CASE("UI.progress.explicitSizeMatchesRenderedContent") {
    auto *previous = ImGui::GetCurrentContext();
    auto *context  = ImGui::CreateContext();
    struct Cleanup {
        ImGuiContext  *previous;
        ImGuiContext  *context;
        eve::ui::Theme theme;
        float          scale;
        ~Cleanup() {
            if (context->WithinFrameScope) ImGui::EndFrame();
            ImGui::DestroyContext(context);
            ImGui::SetCurrentContext(previous);
            eve::ui::globalTheme() = theme;
            eve::ui::setThemeUiScale(scale);
        }
    } cleanup{previous, context, eve::ui::globalTheme(), eve::ui::themeUiScale()};
    eve::ui::setThemeUiScale(1.f);
    auto &io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.f, 600.f);
    for (int key = 0; key < ImGuiKey_COUNT; ++key) io.KeyMap[key] = key;
    unsigned char *pixels = nullptr;
    int            width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto host = eve::ui::UIHost::resolve(eve::ui::UIHost::createHost("progress-size"));
    REQUIRE(host.has_value());
    host->get().meta()->overlay = true;
    // The original renderer always used (-1,0), ignoring both requested axes.
    for (const ImVec2 size : {ImVec2(268.f, 12.f), ImVec2(180.f, 28.f)}) {
        host->get().setTree(
            eve::ui::window("Progress", {eve::ui::progress(0.5f, "bar", " ").withSize(size.x, size.y)}, "root"));
        ImGui::NewFrame();
        eve::ui::UISystem::render();
        auto *window = ImGui::FindWindowByName("Progress###progress-size/Progress");
        REQUIRE(window != nullptr);
        CHECK_EQ(window->DC.CursorMaxPos.x - window->DC.CursorStartPos.x, size.x);
        CHECK_EQ(window->DC.CursorMaxPos.y - window->DC.CursorStartPos.y, size.y);
        ImGui::EndFrame();
    }
    host->get().setVisible(false);
}
}  // namespace test_ui_progress_size
#endif
