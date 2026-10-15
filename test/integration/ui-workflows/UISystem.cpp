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
