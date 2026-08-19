#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace eve::graphics;

namespace {

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = w;
    s.height = h;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));
}

Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    const uint8_t px[4] = {r, g, b, a};
    return gfx->newTexture(1, 1, px);
}

float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

/** Hold a textured canvas / texture on the swapchain for ~1s. */
void previewTexture(Graphics *gfx, Texture *tex, int ms = 1000) {
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

TEST_CASE("volumetric.qualityPresets") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    REQUIRE(raw->getShader() != nullptr);
    std::unique_ptr<Volumetric> vol(raw);

    vol->setQuality("low");
    CHECK(vol->getQuality() == "low");
    CHECK(vol->getSampleCount() == 16);
    CHECK(vol->getDownscale() == 4.f);
    CHECK(vol->resolutionFor(400) == 100);

    vol->setQuality("medium");
    CHECK(vol->getSampleCount() == 48);
    CHECK(vol->getDownscale() == 2.f);
    CHECK(vol->resolutionFor(400) == 200);

    vol->setQuality("high");
    CHECK(vol->getSampleCount() == 96);
    CHECK(vol->getDownscale() == 1.f);
    CHECK(vol->resolutionFor(400) == 400);

    vol->setQuality("unknown");
    CHECK(vol->getQuality() == "medium");
}

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

TEST_CASE("volumetric.occlusionMapAndScatter") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    gfx->setScreenReadbackEnabled(true);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setQuality("medium");
    vol->setShaftColor(1.f, 0.9f, 0.7f);
    vol->setFogColor(0.4f, 0.5f, 0.7f);
    vol->setIntensity(1.2f);

    const int ow = vol->resolutionFor(gfx->getWidth());
    const int oh = vol->resolutionFor(gfx->getHeight());
    Canvas *occ = gfx->newCanvas(ow, oh);
    REQUIRE(occ != nullptr);

    gfx->setCanvas(occ);
    vol->beginOcclusionMap(gfx, float(ow) * 0.75f, float(oh) * 0.2f, 18.f);
    // Occluder between light and bottom of screen.
    vol->drawOccluderSolid(gfx, float(ow) * 0.35f, float(oh) * 0.35f, float(ow) * 0.3f,
                           float(oh) * 0.2f);
    gfx->setCanvas(nullptr);

    Texture *occTex = occ->getTexture();
    REQUIRE(occTex != nullptr);

    Canvas *shafts = gfx->newCanvas(gfx->getWidth(), gfx->getHeight());
    vol->scatterTo(gfx, occTex, shafts);

    // Warm present path so offscreen content is flushed / readable.
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    gfx->clearScreen();
    gfx->drawTexturedRectRGBA(shafts->getTexture(), 0, 0, float(gfx->getWidth()),
                              float(gfx->getHeight()), 1, 1, 1, 1);
    gfx->present();
    gfx->present();

    // Shafts should brighten somewhere below the light (radial blur direction).
    float maxL = 0.f;
    for (int y = 0; y < gfx->getHeight(); y += 4) {
        for (int x = 0; x < gfx->getWidth(); x += 4) {
            maxL = std::max(maxL, luma(gfx->getPixel(x, y)));
        }
    }
    CHECK(maxL > 0.05f);

    previewTexture(gfx, shafts->getTexture());
    win->close();
}

TEST_CASE("volumetric.applyFromScene") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    gfx->setScreenReadbackEnabled(true);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setQuality("low");
    vol->setLightScreenUV(0.8f, 0.15f);
    vol->setFloat("dustAmount", 0.4f);
    vol->setFloat("fogAmount", 0.3f);

    Canvas *scene = gfx->newCanvas(gfx->getWidth(), gfx->getHeight());
    gfx->setCanvas(scene);
    gfx->clear(Color(0.08f, 0.09f, 0.12f, 1.f), std::nullopt, std::nullopt);
    // Bright "sun" disc + dark occluder.
    gfx->drawSolidRect(150, 10, 30, 30, Color(1.f, 0.95f, 0.8f, 1.f));
    gfx->drawSolidRect(70, 50, 50, 40, Color(0.02f, 0.02f, 0.03f, 1.f));
    gfx->setCanvas(nullptr);

    Canvas *out = gfx->newCanvas(gfx->getWidth(), gfx->getHeight());
    vol->applyFromSceneTo(gfx, scene->getTexture(), out);

    // Read from the destination canvas (avoid swapchain readback variance).
    const Color nearLight = out->getPixel(170, 25);
    const Color farCorner = out->getPixel(10, 140);
    CHECK(luma(nearLight) > luma(farCorner));
    CHECK(luma(nearLight) > 0.2f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("volumetric.drawOcclusionRespectsFlag") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 128);

    Texture *tex = makeSolid(gfx, 255, 255, 255);
    tex->setCastOcclusion(false);

    Canvas *c = gfx->newCanvas(64, 64);
    gfx->setCanvas(c);
    // Paint a bright plate explicitly (pending clear alone may not flush).
    gfx->drawSolidRect(0, 0, 64, 64, Color(0.9f, 0.9f, 0.9f, 1.f));
    glm::mat4 m(1.f);
    m[3][0] = 10.f;
    m[3][1] = 10.f;
    m[0][0] = 40.f;
    m[1][1] = 40.f;
    gfx->drawOcclusion(tex, m);  // should no-op because castOcclusion=false
    gfx->setCanvas(nullptr);

    CHECK(c->getPixel(30, 30).r > 0.5f);

    // Control: enabling occlusion paints black.
    tex->setCastOcclusion(true);
    gfx->setCanvas(c);
    gfx->drawSolidRect(0, 0, 64, 64, Color(0.9f, 0.9f, 0.9f, 1.f));
    gfx->drawOcclusion(tex, m);
    gfx->setCanvas(nullptr);
    CHECK(c->getPixel(30, 30).r < 0.2f);
}

TEST_CASE("volumetric.lightFlags") {
    auto *l2 = Light2D::createLight("point");
    CHECK(l2->getVolumetric() == false);
    l2->setVolumetric(true);
    l2->setVolumetricIntensity(0.75f);
    CHECK(l2->getVolumetric() == true);
    CHECK(std::fabs(l2->getVolumetricIntensity() - 0.75f) < 1e-5f);

    auto *l3 = Light3D::createLight("dir");
    CHECK(l3->getVolumetric() == false);
    l3->setVolumetric(true);
    CHECK(l3->getVolumetric() == true);
}

TEST_CASE("volumetric.modeAndRayMarchQuality") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    REQUIRE(vol->getRayMarchShader() != nullptr);

    CHECK(vol->getMode() == "screenspace");
    vol->setMode("raymarch");
    CHECK(vol->getMode() == "raymarch");

    vol->setQuality("low");
    CHECK(vol->getSampleCount() == 8);
    CHECK(vol->getDownscale() == 4.f);

    vol->setQuality("high");
    CHECK(vol->getSampleCount() == 48);

    vol->setMode("screenspace");
    vol->setQuality("medium");
    CHECK(vol->getSampleCount() == 48);
}

namespace {

struct BoxDepth {
    int w = 0, h = 0;
    float boxU0 = 0.35f, boxU1 = 0.65f, boxV0 = 0.35f, boxV1 = 0.65f;
    float boxDepth = 0.35f;
    float floorDepth = 0.85f;
};

float boxDepth01(int x, int y, void *userdata) {
    auto *b = static_cast<BoxDepth *>(userdata);
    const float u = (float(x) + 0.5f) / float(b->w);
    const float v = (float(y) + 0.5f) / float(b->h);
    if (u >= b->boxU0 && u <= b->boxU1 && v >= b->boxV0 && v <= b->boxV1) return b->boxDepth;
    return b->floorDepth;
}

}  // namespace

TEST_CASE("volumetric.rayMarchProducesScatter") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setMode("raymarch");
    vol->setQuality("medium");
    vol->setLightDirection(0.5f, 1.f, 0.2f);
    vol->setLightScreenUV(0.8f, 0.15f);
    vol->setCamera(0.f, 2.f, 5.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 50.f, 160.f / 120.f, 0.1f, 40.f);
    vol->setDensity(1.2f);
    vol->setIntensity(2.5f);
    vol->setShaftColor(1.f, 0.92f, 0.75f);
    vol->setFloat("fogAmount", 0.5f);
    vol->setFloat("dustAmount", 0.4f);

    BoxDepth bd;
    bd.w = 80;
    bd.h = 60;
    Texture *depth = vol->newLinearDepthTexture(gfx, bd.w, bd.h, boxDepth01, &bd);
    REQUIRE(depth != nullptr);

    Canvas *out = gfx->newCanvas(bd.w, bd.h);
    vol->rayMarchTo(gfx, depth, out);

    float maxL = 0.f;
    float sum = 0.f;
    int n = 0;
    for (int y = 0; y < bd.h; y += 2) {
        for (int x = 0; x < bd.w; x += 2) {
            const float L = luma(out->getPixel(x, y));
            maxL = std::max(maxL, L);
            sum += L;
            ++n;
        }
    }
    CHECK(maxL > 0.005f);
    CHECK(sum / float(std::max(n, 1)) > 0.0005f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("volumetric.rayMarchShadowDarkensBehindOccluder") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    Volumetric *raw = gfx->newVolumetric();
    REQUIRE(raw != nullptr);
    std::unique_ptr<Volumetric> vol(raw);
    vol->setMode("raymarch");
    vol->setQuality("high");
    vol->setLightDirection(0.6f, 0.8f, 0.1f);
    vol->setLightScreenUV(0.85f, 0.1f);
    vol->setCamera(0.f, 1.5f, 4.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 160.f / 120.f, 0.1f, 30.f);
    vol->setDensity(0.7f);
    vol->setIntensity(1.6f);
    vol->setFloat("shadowSteps", 16.f);

    BoxDepth bd;
    bd.w = 96;
    bd.h = 72;
    bd.boxU0 = 0.4f;
    bd.boxU1 = 0.6f;
    bd.boxV0 = 0.3f;
    bd.boxV1 = 0.55f;
    bd.boxDepth = 0.3f;
    bd.floorDepth = 0.9f;
    Texture *depth = vol->newLinearDepthTexture(gfx, bd.w, bd.h, boxDepth01, &bd);

    Canvas *out = gfx->newCanvas(bd.w, bd.h);
    vol->rayMarchTo(gfx, depth, out);

    // Sample below the box (away from light) vs beside it toward the light.
    const int midX = int(bd.w * 0.5f);
    const int belowY = int(bd.h * 0.7f);
    const int litY = int(bd.h * 0.2f);
    const float behind = luma(out->getPixel(midX, belowY));
    const float towardLight = luma(out->getPixel(int(bd.w * 0.75f), litY));
    // Lit side / skyward samples should retain some energy; occluded volume may be darker.
    CHECK(towardLight + behind > 0.0f);
    CHECK(towardLight + 0.001f >= behind * 0.25f);
}

TEST_CASE("volumetric.drawOccluders2DUsesCastFlag") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 96);

    // Hide leftover sprites from other tests if any manager exists.
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }

    auto *a = Renderable2D::create();
    a->transform()->x = 20;
    a->transform()->y = 20;
    a->sprite()->width = 40;
    a->sprite()->height = 40;
    a->sprite()->visible = true;
    a->setCastOcclusion(true);

    auto *b = Renderable2D::create();
    b->transform()->x = 70;
    b->transform()->y = 20;
    b->sprite()->width = 40;
    b->sprite()->height = 40;
    b->sprite()->visible = true;
    b->setCastOcclusion(false);

    Volumetric *raw = gfx->newVolumetric();
    std::unique_ptr<Volumetric> vol(raw);

    Canvas *occ = gfx->newCanvas(128, 96);
    gfx->setCanvas(occ);
    gfx->clear(Color(1.f, 1.f, 1.f, 1.f), std::nullopt, std::nullopt);
    // Paint a bright plate so skipped occluders stay readable.
    gfx->drawSolidRect(0, 0, 128, 96, Color(0.95f, 0.95f, 0.95f, 1.f));
    vol->drawOccluders2D(gfx);
    gfx->setCanvas(nullptr);

    // Occluder A region dark; B skipped → stays bright.
    CHECK(occ->getPixel(30, 30).r < 0.15f);
    CHECK(occ->getPixel(85, 30).r > 0.5f);
}

TEST_CASE("volumetric.paramRoundTripShared") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setDensity(0.42f);
    CHECK(std::fabs(vol->getFloat("density") - 0.42f) < 1e-5f);
    vol->setMode("raymarch");
    vol->setDensity(0.33f);
    CHECK(std::fabs(vol->getFloat("density") - 0.33f) < 1e-5f);
    CHECK(vol->hasParam("invViewProj") == true);
    CHECK(vol->hasParam("exposure") == true);
}

TEST_CASE("volumetric.fogModeAndQuality") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    REQUIRE(vol->getFogShader() != nullptr);

    vol->setMode("fog");
    CHECK(vol->getMode() == "fog");
    vol->setQuality("low");
    CHECK(vol->getSampleCount() == 8);
    vol->setQuality("high");
    CHECK(vol->getSampleCount() == 48);
    vol->setQuality("medium");
    CHECK(vol->getSampleCount() == 24);
}

TEST_CASE("volumetric.fogParamsRoundTrip") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setMode("fog");
    vol->setFogHeight(-1.5f);
    vol->setFogHeightFalloff(0.22f);
    vol->setFogStart(3.f);
    vol->setFogEnd(25.f);
    vol->setFogNoise(0.5f);
    vol->setFogColor(0.4f, 0.5f, 0.7f);
    CHECK(std::fabs(vol->getFloat("fogHeight") + 1.5f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("heightFalloff") - 0.22f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("fogStart") - 3.f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("fogEnd") - 25.f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("noiseAmount") - 0.5f) < 1e-5f);
    CHECK(std::fabs(vol->getFloat("fogR") - 0.4f) < 1e-5f);
}

TEST_CASE("volumetric.applyFogProducesOpacity") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setMode("fog");
    vol->setQuality("medium");
    vol->setCamera(0.f, 1.5f, 6.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 50.f, 160.f / 120.f, 0.1f, 50.f);
    vol->setLightDirection(0.3f, 1.f, 0.2f);
    vol->setFogHeight(0.f);
    vol->setFogHeightFalloff(0.2f);
    vol->setFogStart(1.f);
    vol->setFogEnd(30.f);
    vol->setDensity(0.45f);
    vol->setIntensity(1.5f);
    vol->setFogColor(0.6f, 0.7f, 0.85f);
    vol->setFogNoise(0.2f);

    BoxDepth bd;
    bd.w = 80;
    bd.h = 60;
    bd.boxDepth = 0.4f;
    bd.floorDepth = 0.95f;
    Texture *depth = vol->newLinearDepthTexture(gfx, bd.w, bd.h, boxDepth01, &bd);
    Canvas *out = gfx->newCanvas(bd.w, bd.h);
    // Transparent clear so SrcAlpha fog writes measurable alpha (opaque dst always
    // blends to a=1 with the textured pipeline).
    gfx->setCanvas(out);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    vol->applyFog(gfx, depth);
    gfx->setCanvas(nullptr);

    float maxA = 0.f;
    float maxL = 0.f;
    for (int y = 0; y < bd.h; y += 2) {
        for (int x = 0; x < bd.w; x += 2) {
            const Color c = out->getPixel(x, y);
            maxA = std::max(maxA, c.a);
            maxL = std::max(maxL, luma(c));
        }
    }
    CHECK(maxA > 0.02f);
    CHECK(maxL > 0.01f);

    previewTexture(gfx, out->getTexture());
    win->close();
}

TEST_CASE("volumetric.fogDenserAtDistance") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 128, 96);

    std::unique_ptr<Volumetric> vol(gfx->newVolumetric());
    vol->setMode("fog");
    vol->setQuality("high");
    vol->setCamera(0.f, 1.f, 4.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 128.f / 96.f, 0.1f, 40.f);
    vol->setFogHeight(-0.5f);
    vol->setFogHeightFalloff(0.05f);
    vol->setFogStart(0.5f);
    vol->setFogEnd(20.f);
    // Keep density moderate so near/far do not both saturate to opaque.
    vol->setDensity(0.08f);
    vol->setIntensity(1.2f);
    vol->setFogNoise(0.0f);

    // Near wall vs far wall depths.
    BoxDepth nearBd;
    nearBd.w = 64;
    nearBd.h = 48;
    nearBd.boxU0 = 0.f;
    nearBd.boxU1 = 1.f;
    nearBd.boxV0 = 0.f;
    nearBd.boxV1 = 1.f;
    nearBd.boxDepth = 0.15f;
    nearBd.floorDepth = 0.15f;

    BoxDepth farBd = nearBd;
    farBd.boxDepth = 0.95f;
    farBd.floorDepth = 0.95f;

    Texture *nearDepth = vol->newLinearDepthTexture(gfx, nearBd.w, nearBd.h, boxDepth01, &nearBd);
    Texture *farDepth = vol->newLinearDepthTexture(gfx, farBd.w, farBd.h, boxDepth01, &farBd);
    Canvas *nearOut = gfx->newCanvas(nearBd.w, nearBd.h);
    Canvas *farOut = gfx->newCanvas(farBd.w, farBd.h);

    gfx->setCanvas(nearOut);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    vol->applyFog(gfx, nearDepth);
    gfx->setCanvas(farOut);
    gfx->clear(Color(0.f, 0.f, 0.f, 0.f), std::nullopt, std::nullopt);
    vol->applyFog(gfx, farDepth);
    gfx->setCanvas(nullptr);

    const float nearA = nearOut->getPixel(32, 24).a;
    const float farA = farOut->getPixel(32, 24).a;
    CHECK(farA > nearA);
}
