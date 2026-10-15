#include <imgui.h>
#include <imgui_internal.h>
#include "ui/Theme.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("UI.progress.explicitSizeMatchesRenderedContent") {
    auto* previous = ImGui::GetCurrentContext();
    auto* context  = ImGui::CreateContext();
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
    auto host = eve::ui::UIHost::resolve(eve::ui::UIHost::createHost("progress-size"));
    REQUIRE(host.has_value());
    host->get().meta()->overlay = true;
    // The original renderer always used (-1,0), ignoring both requested axes.
    for (const ImVec2 size : {ImVec2(268.f, 12.f), ImVec2(180.f, 28.f)}) {
        host->get().setTree(
            eve::ui::window("Progress", {eve::ui::progress(0.5f, "bar", " ").withSize(size.x, size.y)}, "root"));
        ImGui::NewFrame();
        eve::ui::UISystem::render();
        auto* window = ImGui::FindWindowByName("Progress###progress-size/Progress");
        REQUIRE(window != nullptr);
        CHECK_EQ(window->DC.CursorMaxPos.x - window->DC.CursorStartPos.x, size.x);
        CHECK_EQ(window->DC.CursorMaxPos.y - window->DC.CursorStartPos.y, size.y);
        ImGui::EndFrame();
    }
    host->get().setVisible(false);
}
