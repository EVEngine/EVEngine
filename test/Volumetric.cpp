#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    win->setGraphics(gfx);
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
