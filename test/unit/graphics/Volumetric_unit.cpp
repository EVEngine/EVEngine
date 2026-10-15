#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
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
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    const uint8_t px[4] = {r, g, b, a};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

/** Hold a textured canvas / texture on the swapchain for ~1s. */
[[maybe_unused]] void previewTexture(Graphics *gfx, Texture *tex, int ms = 1000) {
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(tex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1,
                                  1);
        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

}  // namespace


TEST_CASE("volumetric.drawableOcclusionFlags") {
    Texture tex;
    CHECK(tex.getCastOcclusion() == true);
    tex.setCastOcclusion(false);
    CHECK(tex.getCastOcclusion() == false);

    Mesh mesh;
    CHECK(mesh.getCastOcclusion() == true);
    mesh.setCastOcclusion(false);
    CHECK(mesh.getCastOcclusion() == false);

    auto *r = Renderable2D::create();
    CHECK(r->getCastOcclusion() == true);
    r->setCastOcclusion(false);
    CHECK(r->getCastOcclusion() == false);

    auto *r3 = Renderable3D::create();
    CHECK(r3->getCastOcclusion() == true);
    r3->setCastOcclusion(false);
    CHECK(r3->getCastOcclusion() == false);
}


TEST_CASE("volumetric.lightFlags") {
    auto *l2 = Light2D::createLight("point");
    CHECK(l2->getVolumetric() == false);
    l2->setVolumetric(true);
    l2->setVolumetricIntensity(0.75f);
    CHECK(l2->getVolumetric() == true);
    CHECK(std::fabs(l2->getVolumetricIntensity() - 0.75f) < 1e-5f);
    CHECK(l2->getVolumetricOnly() == false);
    l2->setVolumetricOnly(true);
    CHECK(l2->getVolumetricOnly() == true);

    auto *l3 = Light3D::createLight("dir");
    CHECK(l3->getVolumetric() == false);
    l3->setVolumetric(true);
    CHECK(l3->getVolumetric() == true);
    CHECK(l3->getVolumetricOnly() == false);
}

TEST_CASE("volumetric.emissiveProxySkipsSurfaceLighting") {
    auto *glow2 = Light2D::createEmissiveProxy(40.f, 30.f, 1.f, 0.4f, 0.1f, 2.f, 80.f);
    CHECK(glow2->getVolumetric() == true);
    CHECK(glow2->getVolumetricOnly() == true);
    CHECK(std::fabs(glow2->getX() - 40.f) < 1e-5f);

    auto *glow3 = Light3D::createEmissiveProxy(1.f, 2.f, 3.f, 0.2f, 0.8f, 1.f, 4.f, 6.f);
    CHECK(glow3->getVolumetric() == true);
    CHECK(glow3->getVolumetricOnly() == true);
    CHECK(glow3->getCastShadow() == false);
    CHECK(std::fabs(glow3->getRadius() - 6.f) < 1e-5f);
}


namespace {

struct BoxDepth {
    int w = 0, h = 0;
    float boxU0 = 0.35f, boxU1 = 0.65f, boxV0 = 0.35f, boxV1 = 0.65f;
    float boxDepth = 0.35f;
    float floorDepth = 0.85f;
};

[[maybe_unused]] float boxDepth01(int x, int y, void *userdata) {
    auto *b = static_cast<BoxDepth *>(userdata);
    const float u = (float(x) + 0.5f) / float(b->w);
    const float v = (float(y) + 0.5f) / float(b->h);
    if (u >= b->boxU0 && u <= b->boxU1 && v >= b->boxV0 && v <= b->boxV1) return b->boxDepth;
    return b->floorDepth;
}

}  // namespace
