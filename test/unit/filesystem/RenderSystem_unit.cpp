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

[[maybe_unused]] static std::vector<uint8_t> makeCheckerRGBA(int w, int h, int cell, uint8_t r0, uint8_t g0, uint8_t b0,
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

[[maybe_unused]] static std::vector<uint8_t> makeStripeRGBA(int w, int h, int band) {
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

[[maybe_unused]] static eve::filesystem::Filesystem *bootstrapFilesystemForSaveIO(const char *identity) {
    auto *fs = eve::filesystem::Filesystem::create();
    // Unique identity per caller: CTest runs GraphicsSmoke.* in parallel and the
    // Filesystem module is a process-wide singleton — shared write dirs race on macOS.
    REQUIRE(fs->setIdentity(identity, true));
    REQUIRE(fs->setupWriteDirectory());
    return fs;
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
