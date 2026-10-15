#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/Theme.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <imgui.h>
#include <imgui_internal.h>

TEST_CASE("UI.overlay.transparentHostHasNoChromeAndRestoresStyle") {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context  = ImGui::CreateContext();
    struct Cleanup {
        ImGuiContext*  previous;
        ImGuiContext*  context;
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
    auto& io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800.f, 600.f);
    for (int key = 0; key < ImGuiKey_COUNT; ++key) io.KeyMap[key] = key;
    unsigned char* pixels = nullptr;
    int            width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto& style                             = ImGui::GetStyle();
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
    auto* window = ImGui::FindWindowByName("Chrome###chrome-contract/Chrome");
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
