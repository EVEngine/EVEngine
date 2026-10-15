#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
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
#include "image/ImageData.h"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

[[maybe_unused]] static float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

[[maybe_unused]] static Texture *makeSolidTexture(Graphics *gfx, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 255);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

/**
 * Normal map biased toward +X on the right half and -X on the left half so a light
 * from the right brightens the right side more.
 */
[[maybe_unused]] static Texture *makeBiasedNormal(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            // Encode [-1,1] → [0,255]; left leans -X, right leans +X.
            float nx = (x < w / 2) ? -0.7f : 0.7f;
            float ny = 0.f;
            float nz = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

/** Live-render the current lit scene to the window for ~1s.
 *  Retargets cam / sprites / lights from the offscreen canvas onto the swapchain
 *  so we don't depend on sampling a stale canvas texture after getPixel/present. */
[[maybe_unused]] static void previewOnWindow(Graphics *gfx, Camera2D *cam,
                            std::initializer_list<Renderable2D *> sprites,
                            std::initializer_list<Light2D *> lights, int ms = 1000) {
    cam->data()->canvas = nullptr;
    // 128×64 content → roughly fill a 320×240 window.
    cam->data()->zoom = 2.5f;
    for (auto *sp : sprites) sp->sprite()->canvas = nullptr;
    for (auto *L : lights) L->setCanvas(nullptr);

    gfx->setBackgroundColorRGBA(0.06f, 0.06f, 0.08f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}


TEST_CASE("Lighting2D.createLightTypesAndAmbient") {
    auto *pt = Light2D::createLight("point");
    CHECK_EQ(pt->getType(), std::string("point"));
    auto *dir = Light2D::createLight("dir");
    CHECK_EQ(dir->getType(), std::string("dir"));
    dir->setDirection(1.f, 0.f);
    CHECK(std::abs(dir->getDirX() - 1.f) < 1e-5f);
    auto *spot = Light2D::createLight("spot");
    CHECK_EQ(spot->getType(), std::string("spot"));
    spot->setSpotAngle(25.f);
    spot->setSpotSoftness(0.5f);
    CHECK(std::abs(spot->getSpotAngle() - 25.f) < 1e-5f);
    CHECK(std::abs(spot->getSpotSoftness() - 0.5f) < 1e-5f);
    spot->setSpotAngle(120.f);  // clamp to 89
    CHECK(spot->getSpotAngle() <= 89.f + 1e-5f);

    auto *cam = Camera2D::createCamera();
    cam->setAmbient(0.2f, 0.3f, 0.4f);
    CHECK(std::abs(cam->data()->ambientR - 0.2f) < 1e-5f);
    CHECK(std::abs(cam->data()->ambientG - 0.3f) < 1e-5f);
    CHECK(std::abs(cam->data()->ambientB - 0.4f) < 1e-5f);

    pt->setEnabled(false);
    dir->setEnabled(false);
    spot->setEnabled(false);
    cam->data()->active = false;
}


/**
 * Pack a biased normal into a corner of a large atlas so UV derivatives are
 * tiny. The lit2d det singularity must be scale-aware or atlas tiles fall back
 * to the flat-Z path and lose the left/right bias.
 */
[[maybe_unused]] static Texture *makeAtlasBiasedNormal(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY, int tileW,
                                      int tileH) {
    std::vector<uint8_t> px(size_t(atlasW) * size_t(atlasH) * 4);
    for (int y = 0; y < atlasH; ++y) {
        for (int x = 0; x < atlasW; ++x) {
            size_t i  = (size_t(y) * size_t(atlasW) + size_t(x)) * 4;
            px[i + 0] = 128;
            px[i + 1] = 128;
            px[i + 2] = 255;
            px[i + 3] = 255;
        }
    }
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            size_t i  = (size_t(tileY + y) * size_t(atlasW) + size_t(tileX + x)) * 4;
            float  nx = (x < tileW / 2) ? -0.7f : 0.7f;
            float  ny = 0.f;
            float  nz = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(atlasW, atlasH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

[[maybe_unused]] static Texture *makeAtlasSolid(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY, int tileW, int tileH,
                               uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(atlasW) * size_t(atlasH) * 4, 0);
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            size_t i  = (size_t(tileY + y) * size_t(atlasW) + size_t(tileX + x)) * 4;
            px[i + 0] = r;
            px[i + 1] = g;
            px[i + 2] = b;
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(atlasW, atlasH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}
