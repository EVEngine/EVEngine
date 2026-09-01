#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/UIAutomation.h"
#include "ui/UIAutomationCapabilities.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>

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

}  // namespace

TEST_CASE("WidgetDesc.composeNestedTree") {
    int clicks = 0;
    UIHost *h      = resolveHost(UIHost::createHost("nest"));
    REQUIRE(h != nullptr);
    h->setTree(window(
        "Inventory",
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
    ev.hostName = "nest";
    ev.nodeId = "b";
    ev.kind = "click";
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
    ev.hostName = "props";
    ev.nodeId = "btn";
    ev.kind = "click";
    ev.handlerIndex = findNode(h, "btn")->handlerClick;
    UISystem::pendingEvents().push_back(ev);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
}

TEST_CASE("UIAutomation.semanticTreeGetAndClick") {
    int clicks = 0;
    UIHost *host   = resolveHost(UIHost::createHost("mcp-ui-test"));
    REQUIRE(host != nullptr);
    host->setTree(window("Automation", {text("Ready", "status"),
                                        button("Add Tree", "asset-tree", [&]() { ++clicks; })}));

    registerUIAutomationCapabilities();
    auto *automation = eve::cap::query<eve::IUIAutomation>();
    REQUIRE(automation != nullptr);

    const std::string tree = automation->tree("mcp-ui-test");
    CHECK(tree.find("\"name\":\"mcp-ui-test\"") != std::string::npos);
    CHECK(tree.find("\"id\":\"asset-tree\"") != std::string::npos);
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

    CHECK(automation->click("mcp-ui-test", "status").find("error: widget is not clickable") ==
          0);
}

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
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.f, 600.f);
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int w = 0, htex = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &htex);
    (void)pixels;

    ImGui::NewFrame();
    UISystem::render();
    ImGui::EndFrame();
    ImGui::DestroyContext(headlessContext);
    if (savedContext) ImGui::SetCurrentContext(savedContext);

    CHECK(!h->tree()->dirty);
}
