#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "ui/UI.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <string>

using namespace eve::graphics;
using namespace eve::ui;

/**
 * Real window + Vulkan: world sprites (RenderSystem) and ImGui UI overlay in one present.
 * Frame order: UI NewFrame/widgets → RenderSystem (draws + present with overlay) → dispatch.
 */
TEST_CASE("UI.smoke.RenderSystemSameFrameVulkanOverlay") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE_GT(gfx->getWidth(), 0);
    REQUIRE_GT(gfx->getPixelWidth(), 0);

    REQUIRE(ui->initBackend());
    CHECK(ui->isBackendReady());
    const bool overlayHooked = gfx->getPresentOverlay() != nullptr && gfx->getPresentOverlayUser() != nullptr;
    CHECK(overlayHooked);

    gfx->setBackgroundColor(Color(0.10f, 0.12f, 0.18f, 1.0f));
    gfx->setScreenReadbackEnabled(true);

    auto *cam = Camera2D::createCamera();
    cam->data()->r = 0.10f;
    cam->data()->g = 0.12f;
    cam->data()->b = 0.18f;

    auto *solid = Renderable2D::create();
    solid->transform()->x = 80;
    solid->transform()->y = 100;
    solid->sprite()->width = 160;
    solid->sprite()->height = 100;
    solid->sprite()->r = 0.2f;
    solid->sprite()->g = 0.85f;
    solid->sprite()->b = 0.35f;
    solid->sprite()->visible = true;

    ui->mountAs("smoke",
                window("Overlay",
                       {
                           text("UI+RenderSystem", "label"),
                           button("Ping", "ping"),
                       },
                       "root"));

    int framesOk = 0;
    for (int frame = 0; frame < 30; ++frame) {
        solid->transform()->x = 80.0f + float(frame) * 2.0f;

        // ImGui frame must be built before present (RenderSystem ends with present).
        ui->beginFrameAndRender();
        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }

        // After present+readback, swapchain content should be readable.
        Color p = gfx->getPixel(gfx->getPixelWidth() / 2, gfx->getPixelHeight() / 2);
        CHECK(std::isfinite(p.r));
        CHECK(std::isfinite(p.g));
        CHECK(std::isfinite(p.b));
        ++framesOk;

        SDL_Delay(8);
    }

    CHECK_EQ(framesOk, 30);
    CHECK(UISystem::findHost("smoke") != nullptr);
    CHECK(ui->current()->findById("label") != nullptr);

    gfx->setScreenReadbackEnabled(false);
    win->close();
}

TEST_CASE("UI.smoke.panelGalleryPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 720;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
    REQUIRE(ui->initBackend());

    gfx->setBackgroundColor(Color(0.09f, 0.10f, 0.14f, 1.0f));

    auto *cam = Camera2D::createCamera();
    cam->data()->r = 0.09f;
    cam->data()->g = 0.10f;
    cam->data()->b = 0.14f;

    // Soft backdrop sprites so the world + overlay composition is obvious.
    auto *bgA = Renderable2D::create();
    bgA->transform()->x = 40;
    bgA->transform()->y = 60;
    bgA->sprite()->width = 220;
    bgA->sprite()->height = 140;
    bgA->sprite()->r = 0.25f;
    bgA->sprite()->g = 0.45f;
    bgA->sprite()->b = 0.75f;
    bgA->sprite()->visible = true;

    auto *bgB = Renderable2D::create();
    bgB->transform()->x = 420;
    bgB->transform()->y = 220;
    bgB->sprite()->width = 180;
    bgB->sprite()->height = 160;
    bgB->sprite()->r = 0.75f;
    bgB->sprite()->g = 0.35f;
    bgB->sprite()->b = 0.45f;
    bgB->sprite()->visible = true;

    float volume = 0.55f;
    bool muted = false;
    int clicks = 0;

    ui->mountAs("gallery",
                window("EVEngine Preview",
                       {
                           text("UI overlay + world sprites", "title"),
                           separator("sep0"),
                           checkbox("Mute", muted, "mute", [&](bool v) { muted = v; }),
                           slider("Volume", volume, 0.f, 1.f, "vol", [&](float v) { volume = v; }),
                           progress(volume, "prog", "level"),
                           button("Ping", "ping", [&]() { ++clicks; }),
                           text("clicks: 0", "clicks"),
                       },
                       "root"));

    for (int frame = 0; frame < 90; ++frame) {
        bgA->transform()->x = 40.f + float(frame) * 1.2f;
        bgB->transform()->y = 220.f + std::sin(float(frame) * 0.08f) * 24.f;
        volume = 0.35f + 0.3f * (0.5f + 0.5f * std::sin(float(frame) * 0.06f));

        ui->remountAs("gallery",
                      window("EVEngine Preview",
                             {
                                 text("UI overlay + world sprites", "title"),
                                 separator("sep0"),
                                 checkbox("Mute", muted, "mute", [&](bool v) { muted = v; }),
                                 slider("Volume", volume, 0.f, 1.f, "vol",
                                        [&](float v) { volume = v; }),
                                 progress(volume, "prog", muted ? "muted" : "level"),
                                 button("Ping", "ping", [&]() { ++clicks; }),
                                 text("clicks: " + std::to_string(clicks) +
                                          "  vol: " + std::to_string(int(volume * 100.f)),
                                      "clicks"),
                             },
                             "root"));

        ui->beginFrameAndRender();
        RenderSystem::render(*gfx);
        ui->dispatchEvents();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ui->processEvent(&e);
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK(UISystem::findHost("gallery") != nullptr);
    bgA->sprite()->visible = false;
    bgB->sprite()->visible = false;
    win->close();
}
