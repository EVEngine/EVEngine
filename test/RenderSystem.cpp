#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <cmath>

#include "common/Exception.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Batcher.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

TEST_CASE("Batcher.addRectProducesSixVertices") {
    Batcher b;
    b.addRect(10, 20, 30, 40, Color(1, 0, 0, 1));
    REQUIRE_EQ(b.vertices().size(), 6u);
    CHECK(std::abs(b.vertices()[0].pos.x - 10.f) < 1e-5f);
    CHECK(std::abs(b.vertices()[0].pos.y - 20.f) < 1e-5f);
}

TEST_CASE("Batcher.toNDCMapsTopLeftOrigin") {
    Batcher b;
    b.addRect(0, 0, 100, 100, Color(1, 1, 1, 1));
    b.toNDC(100, 100);
    CHECK(std::abs(b.vertices()[0].pos.x - (-1.0f)) < 1e-5f);
    // Vulkan NDC (-1,-1) is top-left; logical (0,0) maps there.
    CHECK(std::abs(b.vertices()[0].pos.y - (-1.0f)) < 1e-5f);
}

TEST_CASE("RenderSystem.drawsVisibleSpritesViaMocklessPath") {
    auto *a = Renderable2D::create();
    a->transform()->x = 5;
    a->transform()->y = 6;
    a->sprite()->width = 16;
    a->sprite()->height = 16;
    a->sprite()->r = 0.2f;
    a->sprite()->visible = true;

    CHECK(std::abs(a->transform()->x - 5.f) < 1e-5f);
    CHECK(std::abs(a->sprite()->width - 16.f) < 1e-5f);

    int visible = 0;
    auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (sp->visible) ++visible;
    }
    CHECK_GE(visible, 1);
}

TEST_CASE("RenderSystem.sprite2dPropertyApiReachesDrawItem") {
    auto *sprite = Renderable2D::create();
    sprite->setPosition(12.f, 34.f);
    sprite->setSize(80.f, 60.f);
    sprite->setScale(1.5f, 0.5f);
    sprite->setRotation(27.f);
    sprite->setLayer(7);
    sprite->setBlend("additive");
    sprite->setReceiveLight(false);
    sprite->setAnchor(0.25f, 0.75f);
    sprite->setFlip(true, false);

    std::vector<DrawItem2D> items;
    RenderSystem::collectSprites(items);
    bool found = false;
    for (const auto &item : items) {
        if (std::fabs(item.x - 12.f) < 1e-5f && std::fabs(item.y - 34.f) < 1e-5f) {
            found = true;
            CHECK(std::fabs(item.w - 120.f) < 1e-5f);
            CHECK(std::fabs(item.h - 30.f) < 1e-5f);
            CHECK(std::fabs(item.rotation - 27.f) < 1e-5f);
            CHECK_EQ(item.layer, 7);
            CHECK_EQ(static_cast<int>(item.blend), static_cast<int>(BlendMode::Additive));
            CHECK(!item.receiveLight);
            CHECK(std::fabs(item.anchorX - 0.25f) < 1e-5f);
            CHECK(std::fabs(item.anchorY - 0.75f) < 1e-5f);
            CHECK(item.flipX);
            CHECK(!item.flipY);
        }
    }
    CHECK(found);
    sprite->release();
}

TEST_CASE("DrawItem2D.equalVisualKeysPreserveSubmissionOrder") {
    std::vector<DrawItem2D> items(3);
    for (auto &item : items) {
        item.layer = 4;
        item.depthY = 20.f;
    }
    items[0].texture = reinterpret_cast<Texture *>(uintptr_t(0x300));
    items[1].texture = reinterpret_cast<Texture *>(uintptr_t(0x100));
    items[2].texture = reinterpret_cast<Texture *>(uintptr_t(0x200));
    Texture *first = items[0].texture;
    Texture *second = items[1].texture;
    Texture *third = items[2].texture;
    sortDrawItems2D(items);
    CHECK(items[0].texture == first);
    CHECK(items[1].texture == second);
    CHECK(items[2].texture == third);
}

TEST_CASE("RenderSystem.spriteExtendedBlendModes") {
    auto *sprite = Renderable2D::create();
    sprite->setBlend("premultiplied");
    CHECK(sprite->getBlend() == "premultiplied");
    sprite->setBlend("multiply");
    CHECK(sprite->getBlend() == "multiply");
    sprite->release();
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

static eve::filesystem::Filesystem *bootstrapFilesystemForSaveIO(const char *identity) {
    auto *fs = eve::filesystem::Filesystem::create();
    // Unique identity per caller: CTest runs GraphicsSmoke.* in parallel and the
    // Filesystem module is a process-wide singleton — shared write dirs race on macOS.
    REQUIRE(fs->setIdentity(identity, true));
    REQUIRE(fs->setupWriteDirectory());
    return fs;
}

TEST_CASE("GraphicsSmoke.clearAndPresentWindow") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    REQUIRE_GT(gfx->getWidth(), 0);
    REQUIRE_GT(gfx->getPixelWidth(), 0);

    gfx->setBackgroundColor(Color(0.12f, 0.14f, 0.22f, 1.0f));

    auto *fs = bootstrapFilesystemForSaveIO("evengine_gfx_smoke_clear");
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();

    auto checkerPx = makeCheckerRGBA(64, 64, 8, 255, 220, 60, 40, 40, 180);
    eve::image::ImageData src(64, 64, "RGBA8");
    std::memcpy(src.getData(), checkerPx.data(), checkerPx.size());

    const char *tmpName = "evengine_smoke_checker.png";
    std::unique_ptr<eve::filesystem::FileData> encoded(
        src.encode(medialoader::FormatHandler::ENCODED_PNG, tmpName, true));
    REQUIRE(encoded.get() != nullptr);

    Texture *fromFile = gfx->newTextureFromFile(tmpName);
    REQUIRE(fromFile != nullptr);

    auto stripePx = makeStripeRGBA(64, 64, 8);
    eve::image::ImageData imageData(64, 64, "RGBA8");
    std::memcpy(imageData.getData(), stripePx.data(), stripePx.size());
    Texture *stripes = gfx->newTexture(&imageData);
    REQUIRE(stripes != nullptr);
    REQUIRE(stripes != fromFile);

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
}

TEST_CASE("GraphicsSmoke.newTextureFromFileThrowsOnMissing") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    // No write-dir bootstrap: missing-file should throw from PhysFS open alone.
    // (Parallel GraphicsSmoke.clearAndPresentWindow used to race the same identity.)
    [[maybe_unused]] auto *const filesystemModule = eve::filesystem::Filesystem::create();
    bool threw = false;
    try {
        gfx->newTextureFromFile("definitely_missing_evengine_xyz.png");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    win->close();
}

TEST_CASE("Canvas.offscreenGetPixelAndSampleOnScreen") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *c = gfx->newCanvas(64, 64);
    REQUIRE(c != nullptr);
    gfx->setCanvas(c);
    gfx->clear(Color(0.2f, 0.4f, 0.6f, 1.0f), std::nullopt, std::nullopt);
    gfx->drawSolidRect(0, 0, 64, 64, Color(1.0f, 0.0f, 0.0f, 1.0f));
    gfx->setCanvas();

    Color p = c->getPixel(32, 32);
    CHECK(std::abs(p.r - 1.0f) < 0.05f);
    CHECK(std::abs(p.g - 0.0f) < 0.05f);
    CHECK(std::abs(p.b - 0.0f) < 0.05f);

    std::unique_ptr<eve::image::ImageData> img(c->newImageData());
    REQUIRE(img->getWidth() == 64);
    REQUIRE(img->getHeight() == 64);
    auto *px = static_cast<uint8_t *>(img->getData());
    CHECK(px[(32 * 64 + 32) * 4 + 0] >= 200);

    gfx->setCanvas(c);
    bool presentThrew = false;
    try {
        gfx->present();
    } catch (const eve::Exception &) {
        presentThrew = true;
    }
    CHECK(presentThrew);
    gfx->setCanvas();

    Canvas *c2 = gfx->newCanvas(128, 128);
    REQUIRE(c2 != nullptr);
    REQUIRE(c2->getTexture() != nullptr);

    auto *off = Renderable2D::create();
    off->transform()->x = 0;
    off->transform()->y = 0;
    off->sprite()->width = 128;
    off->sprite()->height = 128;
    off->sprite()->r = 0.1f;
    off->sprite()->g = 0.8f;
    off->sprite()->b = 0.3f;
    off->sprite()->canvas = c2;
    off->sprite()->visible = true;

    auto *onScreen = Renderable2D::create();
    onScreen->transform()->x = 80;
    onScreen->transform()->y = 80;
    onScreen->sprite()->width = 192;
    onScreen->sprite()->height = 192;
    onScreen->sprite()->texture = c2->getTexture();
    onScreen->sprite()->r = 1;
    onScreen->sprite()->g = 1;
    onScreen->sprite()->b = 1;
    onScreen->sprite()->visible = true;

    for (int frame = 0; frame < 30; ++frame) {
        RenderSystem::render(*gfx);
        SDL_Delay(8);
    }

    Color p2 = c2->getPixel(64, 64);
    CHECK(std::abs(p2.g - 0.8f) < 0.15f);

    win->close();
}

TEST_CASE("Camera.panAndZoomAffectOffscreenPixels") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(100, 100);
    auto *cam = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x = 50.f;
    cam->data()->y = 50.f;
    cam->data()->zoom = 1.f;
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    // Create override camera before any render/View (ECS create after heavy GPU
    // work has been flaky on this path); wire it up later via Sprite.camera.
    auto *camOverride = Camera2D::createCamera();
    camOverride->data()->canvas = rt;
    camOverride->data()->active = false;
    camOverride->data()->x = 40.f;
    camOverride->data()->y = 40.f;
    camOverride->data()->zoom = 1.f;
    camOverride->data()->r = 0.f;
    camOverride->data()->g = 0.f;
    camOverride->data()->b = 0.f;
    camOverride->data()->a = 1.f;

    auto *sp = Renderable2D::create();
    sp->transform()->x = 40.f;
    sp->transform()->y = 40.f;
    sp->sprite()->width = 20.f;
    sp->sprite()->height = 20.f;
    sp->sprite()->r = 1.f;
    sp->sprite()->g = 0.f;
    sp->sprite()->b = 0.f;
    sp->sprite()->canvas = rt;
    sp->sprite()->visible = true;

    RenderSystem::render(*gfx);

    Color mid = rt->getPixel(45, 45);
    CHECK(std::abs(mid.r - 1.f) < 0.08f);

    cam->data()->zoom = 2.f;
    RenderSystem::render(*gfx);
    Color z = rt->getPixel(35, 35);
    CHECK(std::abs(z.r - 1.f) < 0.08f);
    Color outside = rt->getPixel(80, 80);
    CHECK(outside.r < 0.2f);

    sp->sprite()->camera = camOverride;
    cam->data()->zoom = 1.f;
    RenderSystem::render(*gfx);
    // Override cam center (40,40): sprite lands at (50,50)-(70,70).
    // Default cam would have left it at (40,40)-(60,60).
    Color ov = rt->getPixel(65, 65);
    CHECK(std::abs(ov.r - 1.f) < 0.08f);
    Color notDefault = rt->getPixel(45, 45);
    CHECK(notDefault.r < 0.2f);

    sp->sprite()->visible = false;
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Camera2D.screenWorldPick") {
    auto *cam = Camera2D::createCamera();
    cam->setPosition(100.f, 50.f);
    cam->setZoom(2.f);
    CHECK(std::fabs(cam->getX() - 100.f) < 1e-5f);
    CHECK(std::fabs(cam->getZoom() - 2.f) < 1e-5f);

    const float viewW = 200.f, viewH = 100.f;
    // Screen center maps to camera look-at.
    CHECK(std::fabs(cam->screenToWorldX(100.f, 50.f, viewW, viewH) - 100.f) < 1e-4f);
    CHECK(std::fabs(cam->screenToWorldY(100.f, 50.f, viewW, viewH) - 50.f) < 1e-4f);

    float sx = cam->worldToScreenX(100.f, 50.f, viewW, viewH);
    float sy = cam->worldToScreenY(100.f, 50.f, viewW, viewH);
    CHECK(std::fabs(sx - 100.f) < 1e-4f);
    CHECK(std::fabs(sy - 50.f) < 1e-4f);

    float wx = cam->screenToWorldX(120.f, 50.f, viewW, viewH);
    CHECK(std::fabs(wx - 110.f) < 1e-4f);  // (120-100)/2 + 100
}
