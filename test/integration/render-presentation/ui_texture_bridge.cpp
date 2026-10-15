#include <SDL2/SDL.h>
#include <imgui.h>
#include <cmath>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "ui/UI.h"
#include "ui/UIBackend.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "window/Window.h"
#include "window/sdl/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// ui_texture_bridge.cpp
using eve::graphics::Canvas;
using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::RenderSystem;
using eve::ui::image;
using eve::ui::imageButton;
using eve::ui::UI;
using eve::ui::UIHost;
using eve::ui::UIHostHandle;
using eve::ui::viewport;
using eve::ui::window;

TEST_CASE("UI.textureBridge.viewportPixelsReachSwapchain") {
    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    auto* ui  = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings settings;
    settings.width    = 640;
    settings.height   = 480;
    settings.centered = true;
    REQUIRE(win->setWindowSettings(settings));
    REQUIRE(ui->initBackend());

    gfx->setBackgroundColor(Color(0.02f, 0.03f, 0.04f, 1.f));
    gfx->setScreenReadbackEnabled(true);
    const UIHostHandle textureHost = ui->mountAs(
        "texture-pixels", window("Texture bridge",
                                 {viewport("engine-texture", 320.f, 200.f), image("engine-image", 80.f, 48.f),
                                  imageButton("engine-image-button", 80.f, 48.f)},
                                 "root"));
    REQUIRE(UIHost::resolve(textureHost).has_value());

    Canvas*  canvas    = nullptr;
    uint64_t textureId = 0;
    for (int frame = 0; frame < 4; ++frame) {
        ui->beginFrameAndRender();
        canvas = ui->viewportCanvas("engine-texture");
        REQUIRE(canvas != nullptr);
        if (textureId == 0) {
            textureId = ui->registerTexture(canvas->getTexture());
            REQUIRE(textureId != 0);
            ui->setImageTextureId("engine-image", textureId);
            ui->setImageTextureId("engine-image-button", textureId);
        }

        gfx->setCanvas(canvas);
        gfx->clearScreen();
        gfx->drawSolidRectRGBA(0.f, 0.f, float(canvas->getWidth()), float(canvas->getHeight()), 0.92f, 0.06f, 0.74f,
                               1.f);
        gfx->setCanvas(nullptr);

        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event event;
        while (SDL_PollEvent(&event)) ui->processEvent(&event);
    }

    int magentaPixels = 0;
    for (int y = 0; y < gfx->getHeight(); y += 3) {
        for (int x = 0; x < gfx->getWidth(); x += 3) {
            const Color pixel = gfx->getPixel(x, y);
            REQUIRE(std::isfinite(pixel.r));
            if (pixel.r > 0.65f && pixel.g < 0.25f && pixel.b > 0.45f) ++magentaPixels;
        }
    }
    CHECK_GT(magentaPixels, 1000);

    ui->setImageTextureId("engine-image", 0);
    ui->setImageTextureId("engine-image-button", 0);
    ui->unregisterTexture(textureId);
    gfx->setScreenReadbackEnabled(false);
    win->close();
}

// ui_texture_order.cpp
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
