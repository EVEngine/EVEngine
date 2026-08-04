#include <gtest/gtest.h>

#include <SDL2/SDL.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "common/Exception.h"
#include "filesystem/Filesystem.h"
#include "graphics/Batcher.h"
#include "graphics/RenderSystem.h"
#include "graphics/Graphics.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"
#include "window/Window.h"

using namespace eve::graphics;

TEST(Batcher, addRectProducesSixVertices) {
    Batcher b;
    b.addRect(10, 20, 30, 40, Color(1, 0, 0, 1));
    ASSERT_EQ(b.vertices().size(), 6u);
    EXPECT_FLOAT_EQ(b.vertices()[0].pos.x, 10);
    EXPECT_FLOAT_EQ(b.vertices()[0].pos.y, 20);
}

TEST(Batcher, toNDCMapsTopLeftOrigin) {
    Batcher b;
    b.addRect(0, 0, 100, 100, Color(1, 1, 1, 1));
    b.toNDC(100, 100);
    EXPECT_NEAR(b.vertices()[0].pos.x, -1.0f, 1e-5);
    EXPECT_NEAR(b.vertices()[0].pos.y, 1.0f, 1e-5);
}

TEST(RenderSystem, drawsVisibleSpritesViaMocklessPath) {
    auto *a = Renderable2D::create();
    a->transform()->x = 5;
    a->transform()->y = 6;
    a->sprite()->width = 16;
    a->sprite()->height = 16;
    a->sprite()->r = 0.2f;
    a->sprite()->visible = true;

    EXPECT_FLOAT_EQ(a->transform()->x, 5);
    EXPECT_FLOAT_EQ(a->sprite()->width, 16);

    int visible = 0;
    auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (sp->visible) ++visible;
    }
    EXPECT_GE(visible, 1);
}

static std::vector<uint8_t> makeCheckerRGBA(int w, int h, int cell, uint8_t r0, uint8_t g0, uint8_t b0,
                                            uint8_t r1, uint8_t g1, uint8_t b1) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool on = ((x / cell) + (y / cell)) % 2 == 0;
            size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            px[i + 0] = on ? r0 : r1;
            px[i + 1] = on ? g0 : g1;
            px[i + 2] = on ? b0 : b1;
            px[i + 3] = 255;
        }
    }
    return px;
}

static std::vector<uint8_t> makeStripeRGBA(int w, int h, int band) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool on = (x / band) % 2 == 0;
            size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            px[i + 0] = on ? 40 : 80;
            px[i + 1] = on ? 200 : 40;
            px[i + 2] = on ? 255 : 200;
            px[i + 3] = 255;
        }
    }
    return px;
}

static eve::filesystem::Filesystem *bootstrapFilesystemForSaveIO() {
    auto *fs = eve::filesystem::Filesystem::create();
    fs->setIdentity("evengine_gfx_smoke", true);
    if (!fs->setupWriteDirectory()) {
        throw std::runtime_error("setupWriteDirectory failed");
    }
    return fs;
}

TEST(GraphicsSmoke, clearAndPresentWindow) {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    ASSERT_NE(win, nullptr);
    ASSERT_NE(gfx, nullptr);

    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 480;
    s.centered = true;
    ASSERT_TRUE(win->setWindowSettings(s)) << "Failed to create Vulkan window";

    ASSERT_GT(gfx->getWidth(), 0);
    ASSERT_GT(gfx->getPixelWidth(), 0);

    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.22f, 1.0f));

    auto *fs = bootstrapFilesystemForSaveIO();
    eve::image::Image::create();

    auto checkerPx = makeCheckerRGBA(64, 64, 8, 255, 220, 60, 40, 40, 180);
    eve::image::ImageData src(64, 64, "RGBA8");
    std::memcpy(src.getData(), checkerPx.data(), checkerPx.size());

    const char *tmpName = "evengine_smoke_checker.png";
    std::unique_ptr<eve::filesystem::FileData> encoded(
        src.encode(medialoader::FormatHandler::ENCODED_PNG, tmpName, true));
    ASSERT_NE(encoded, nullptr);

    Texture *fromFile = gfx->newTextureFromFile(tmpName);
    ASSERT_NE(fromFile, nullptr);

    auto stripePx = makeStripeRGBA(64, 64, 8);
    eve::image::ImageData imageData(64, 64, "RGBA8");
    std::memcpy(imageData.getData(), stripePx.data(), stripePx.size());
    Texture *stripes = gfx->newTexture(&imageData);
    ASSERT_NE(stripes, nullptr);
    ASSERT_NE(stripes, fromFile);

    auto *solid = Renderable2D::create();
    solid->transform()->x = 40;
    solid->transform()->y = 40;
    solid->sprite()->width = 120;
    solid->sprite()->height = 80;
    solid->sprite()->r = 1.0f;
    solid->sprite()->g = 0.4f;
    solid->sprite()->b = 0.2f;
    solid->sprite()->visible = true;

    auto *spriteA = Renderable2D::create();
    spriteA->transform()->x = 200;
    spriteA->transform()->y = 80;
    spriteA->sprite()->width = 128;
    spriteA->sprite()->height = 128;
    spriteA->sprite()->texture = fromFile;
    spriteA->sprite()->r = 1;
    spriteA->sprite()->g = 1;
    spriteA->sprite()->b = 1;
    spriteA->sprite()->visible = true;

    auto *spriteB = Renderable2D::create();
    spriteB->transform()->x = 360;
    spriteB->transform()->y = 160;
    spriteB->sprite()->width = 160;
    spriteB->sprite()->height = 160;
    spriteB->sprite()->texture = stripes;
    spriteB->sprite()->r = 1;
    spriteB->sprite()->g = 1;
    spriteB->sprite()->b = 1;
    spriteB->sprite()->visible = true;

    spriteA->sprite()->layer = 1;
    spriteB->sprite()->layer = 1;

    for (int frame = 0; frame < 60; ++frame) {
        solid->transform()->x = 40.0f + float(frame) * 0.8f;
        spriteA->transform()->y = 80.0f + float((frame % 40) - 20) * 0.5f;
        spriteB->transform()->x = 360.0f + float((frame % 30) - 15) * 0.4f;
        RenderSystem::render(*gfx);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    fs->remove(tmpName);
    win->close();
    SUCCEED();
}

TEST(GraphicsSmoke, newTextureFromFileThrowsOnMissing) {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    ASSERT_NE(win, nullptr);
    ASSERT_NE(gfx, nullptr);

    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    ASSERT_TRUE(win->setWindowSettings(s));

    bootstrapFilesystemForSaveIO();
    EXPECT_THROW(gfx->newTextureFromFile("definitely_missing_evengine_xyz.png"), eve::Exception);

    win->close();
}
