#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "ui/UIBackend.h"
#include "window/Window.h"
#include "window/sdl/Window.h"

#include <imgui.h>

TEST_CASE("UI.textureBridge.preservesPainterOrderAndClipping") {
    auto* win = eve::window::Window::create();
    auto* gfx = eve::graphics::Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings settings;
    settings.width  = 320;
    settings.height = 240;
    REQUIRE(win->setWindowSettings(settings));
    auto* native = dynamic_cast<eve::window::sdl::Window*>(win);
    REQUIRE(native != nullptr);
    auto backend = eve::ui::createImGuiBackend();
    REQUIRE(backend->init(static_cast<SDL_Window*>(native->getHandle()), gfx));
    const uint8_t red[]      = {255, 0, 0, 255};
    const uint8_t blue[]     = {0, 0, 255, 255};
    auto*         background = gfx->newTexture(1, 1, red);
    auto*         foreground = gfx->newTexture(1, 1, blue);
    const auto    redId      = backend->registerTexture(background);
    const auto    blueId     = backend->registerTexture(foreground);
    REQUIRE(redId != 0);
    REQUIRE(blueId != 0);
    gfx->setScreenReadbackEnabled(true);
    for (int frame = 0; frame < 4; ++frame) {
        backend->newFrame();
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(320.f, 240.f));
        ImGui::Begin("ordered textures", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        auto*      list  = ImGui::GetWindowDrawList();
        const auto paint = [&](uint64_t id) {
            if (backend->usesQueuedTextureDraws()) {
                backend->queueTextureDraw(id, 20.f, 20.f, 180.f, 160.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, false);
            } else {
                list->AddImage(backend->textureHandle(id), ImVec2(20.f, 20.f), ImVec2(200.f, 180.f));
            }
        };
        paint(redId);
        list->AddRectFilled(ImVec2(30.f, 30.f), ImVec2(190.f, 170.f), IM_COL32(0, 255, 0, 255));
        list->PushClipRect(ImVec2(80.f, 80.f), ImVec2(140.f, 140.f), true);
        paint(blueId);
        list->PopClipRect();
        list->AddRectFilled(ImVec2(95.f, 95.f), ImVec2(125.f, 125.f), IM_COL32_WHITE);
        ImGui::End();
        eve::graphics::RenderSystem::render(*gfx);
        SDL_Event event;
        while (SDL_PollEvent(&event)) backend->processEvent(&event);
    }
    const auto margin = gfx->getPixel(25, 25);
    const auto middle = gfx->getPixel(50, 50);
    const auto front  = gfx->getPixel(85, 85);
    const auto last   = gfx->getPixel(110, 110);
    REQUIRE_GT(margin.r, 0.8f);
    REQUIRE_LT(margin.g, 0.2f);
    REQUIRE_LT(margin.b, 0.2f);
    REQUIRE_LT(middle.r, 0.2f);
    REQUIRE_GT(middle.g, 0.8f);
    REQUIRE_LT(middle.b, 0.2f);
    REQUIRE_LT(front.r, 0.2f);
    REQUIRE_LT(front.g, 0.2f);
    REQUIRE_GT(front.b, 0.8f);
    REQUIRE_GT(last.r, 0.8f);
    REQUIRE_GT(last.g, 0.8f);
    REQUIRE_GT(last.b, 0.8f);
    backend->unregisterTexture(redId);
    backend->unregisterTexture(blueId);
    backend->shutdown();
    gfx->releaseTexture(background);
    gfx->releaseTexture(foreground);
    gfx->setScreenReadbackEnabled(false);
    win->close();
}
