#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>

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
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    auto *ui = UI::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    REQUIRE(ui != nullptr);

    eve::window::WindowSettings settings;
    settings.width = 640;
    settings.height = 480;
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

    Canvas *canvas = nullptr;
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
        gfx->drawSolidRectRGBA(0.f, 0.f, float(canvas->getWidth()), float(canvas->getHeight()),
                               0.92f, 0.06f, 0.74f, 1.f);
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
