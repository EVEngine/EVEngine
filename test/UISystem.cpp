#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>

using namespace eve::ui;

TEST_CASE("WidgetDesc.composeNestedTree") {
    int clicks = 0;
    UIHost *h = UIHost::createHost("nest");
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

    UINode *b = h->findById("b");
    REQUIRE(b != nullptr);
    REQUIRE_NE(b->handlerClick, 0u);

    UIEvent ev;
    ev.host = h;
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
    UIHost *h = UIHost::createHost("simple");
    h->setSimplePanel("Inventory", "Hello", "Use");
    auto t = h->tree();
    REQUIRE_EQ(t->root, 0);
    REQUIRE_EQ(t->nodes.size(), 3u);
    CHECK(t->nodes[1].id == "label");
    CHECK(t->nodes[2].id == "btn");
}

TEST_CASE("UIHost.setPropsAndClickHandler") {
    UIHost *h = UIHost::createHost("props");
    h->setTree(window("W", {text("before", "label"), button("Go", "btn")}));
    h->setTextById("label", "after");
    CHECK(h->findById("label")->text == "after");
    h->setVisibleById("btn", false);
    CHECK(!h->findById("btn")->visible);

    int clicks = 0;
    CHECK(h->setClickHandler("btn", [&]() { ++clicks; }));

    UIEvent ev;
    ev.host = h;
    ev.hostName = "props";
    ev.nodeId = "btn";
    ev.kind = "click";
    ev.handlerIndex = h->findById("btn")->handlerClick;
    UISystem::pendingEvents().push_back(ev);
    UISystem::dispatchEvents();
    CHECK_EQ(clicks, 1);
}

TEST_CASE("UISystem.render.headlessImGuiWalk") {
    UIHost *h = UIHost::createHost("walk");
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
