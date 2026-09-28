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

static float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

static Texture *makeSolidTexture(Graphics *gfx, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
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
static Texture *makeBiasedNormal(Graphics *gfx, int w, int h) {
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
static void previewOnWindow(Graphics *gfx, Camera2D *cam,
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

TEST_CASE("Lighting2D.pointLightBrightensNearbyUnlitSprite") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x = 64.f;
    cam->data()->y = 32.f;
    cam->data()->zoom = 1.f;
    cam->setAmbient(0.05f, 0.05f, 0.05f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    REQUIRE(albedo != nullptr);

    auto *nearSp = Renderable2D::create();
    nearSp->transform()->x = 8.f;
    nearSp->transform()->y = 16.f;
    nearSp->sprite()->width = 24.f;
    nearSp->sprite()->height = 24.f;
    nearSp->sprite()->texture = albedo;
    nearSp->sprite()->receiveLight = true;
    nearSp->sprite()->canvas = rt;
    nearSp->sprite()->visible = true;

    auto *farSp = Renderable2D::create();
    farSp->transform()->x = 46.f;
    farSp->transform()->y = 16.f;
    farSp->sprite()->width = 24.f;
    farSp->sprite()->height = 24.f;
    farSp->sprite()->texture = albedo;
    farSp->sprite()->receiveLight = true;
    farSp->sprite()->canvas = rt;
    farSp->sprite()->visible = true;

    auto *light = Light2D::createLight("point");
    light->setCanvas(rt);
    light->setPosition(20.f, 28.f);
    light->setColor(1.f, 1.f, 1.f, 2.5f);
    light->setRadius(50.f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float nearL = luma(rt->getPixel(20, 28));
    float farL = luma(rt->getPixel(48, 28));
    CHECK(nearL > farL + 0.08f);

    previewOnWindow(gfx, cam, {nearSp, farSp}, {light});

    nearSp->sprite()->visible = false;
    farSp->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.normalMapLitSideBrighter") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x = 64.f;
    cam->data()->y = 32.f;
    cam->data()->zoom = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.08f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 32, 32, 220, 220, 220);
    Texture *normal = makeBiasedNormal(gfx, 32, 32);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);

    auto *sp = Renderable2D::create();
    sp->transform()->x = 32.f;
    sp->transform()->y = 8.f;
    sp->sprite()->width = 64.f;
    sp->sprite()->height = 48.f;
    sp->setTexture(albedo);
    sp->setNormalTexture(normal);
    sp->setReceiveLight(true);
    sp->sprite()->canvas = rt;
    sp->sprite()->visible = true;

    auto *light = Light2D::createLight("point");
    light->setCanvas(rt);
    // Light to the right of the sprite → right-facing normals should be brighter.
    light->setPosition(110.f, 32.f);
    light->setColor(1.f, 1.f, 1.f, 3.f);
    light->setRadius(120.f);
    light->setEnabled(true);

    // Second light (also collected) slightly above — ensures multi-light packing.
    auto *light2 = Light2D::createLight("point");
    light2->setCanvas(rt);
    light2->setPosition(100.f, 10.f);
    light2->setColor(0.4f, 0.4f, 1.f, 1.2f);
    light2->setRadius(80.f);
    light2->setEnabled(true);

    RenderSystem::render(*gfx);

    float leftL = luma(rt->getPixel(42, 32));
    float rightL = luma(rt->getPixel(86, 32));
    CHECK(rightL > leftL + 0.05f);

    previewOnWindow(gfx, cam, {sp}, {light, light2});

    sp->sprite()->visible = false;
    light->setEnabled(false);
    light2->setEnabled(false);
    cam->data()->active = false;
    win->close();
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

TEST_CASE("Lighting2D.zeroRadiusSpotDoesNotActAsDirectional") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x = 32.f;
    cam->data()->y = 32.f;
    cam->data()->zoom = 1.f;
    cam->setAmbient(0.05f, 0.05f, 0.05f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    auto *sp = Renderable2D::create();
    sp->transform()->x = 20.f;
    sp->transform()->y = 20.f;
    sp->sprite()->width = 24.f;
    sp->sprite()->height = 24.f;
    sp->sprite()->texture = albedo;
    sp->sprite()->receiveLight = true;
    sp->sprite()->canvas = rt;
    sp->sprite()->visible = true;

    auto *light = Light2D::createLight("spot");
    light->setCanvas(rt);
    light->setPosition(8.f, 8.f);
    light->setDirection(1.f, 0.f);
    light->setColor(1.f, 1.f, 1.f, 4.f);
    light->setRadius(0.f);  // must not become a directional light
    light->setSpotAngle(25.f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);
    float L = luma(rt->getPixel(32, 32));
    // Ambient-only: a mispacked directional would push luma well above ambient.
    CHECK(L < 0.15f);

    sp->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.spotLightBrightensAlongBeam") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x = 64.f;
    cam->data()->y = 32.f;
    cam->data()->zoom = 1.f;
    cam->setAmbient(0.04f, 0.04f, 0.04f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    REQUIRE(albedo != nullptr);

    auto *onBeam = Renderable2D::create();
    onBeam->transform()->x = 48.f;
    onBeam->transform()->y = 20.f;
    onBeam->sprite()->width = 24.f;
    onBeam->sprite()->height = 24.f;
    onBeam->sprite()->texture = albedo;
    onBeam->sprite()->receiveLight = true;
    onBeam->sprite()->canvas = rt;
    onBeam->sprite()->visible = true;

    auto *offBeam = Renderable2D::create();
    offBeam->transform()->x = 48.f;
    offBeam->transform()->y = 4.f;
    offBeam->sprite()->width = 24.f;
    offBeam->sprite()->height = 24.f;
    offBeam->sprite()->texture = albedo;
    offBeam->sprite()->receiveLight = true;
    offBeam->sprite()->canvas = rt;
    offBeam->sprite()->visible = true;

    auto *light = Light2D::createLight("spot");
    light->setCanvas(rt);
    light->setPosition(20.f, 32.f);
    light->setDirection(1.f, 0.f);  // beam to the right
    light->setColor(1.f, 1.f, 1.f, 3.f);
    light->setRadius(90.f);
    light->setSpotAngle(20.f);
    light->setSpotSoftness(0.2f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float onL = luma(rt->getPixel(60, 32));
    float offL = luma(rt->getPixel(60, 16));
    CHECK(onL > offL + 0.06f);

    previewOnWindow(gfx, cam, {onBeam, offBeam}, {light});

    onBeam->sprite()->visible = false;
    offBeam->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.setNormalTextureEnablesLitPathAndRotation") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x = 64.f;
    cam->data()->y = 32.f;
    cam->data()->zoom = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.08f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture *albedo = makeSolidTexture(gfx, 32, 32, 220, 220, 220);
    Texture *normal = makeBiasedNormal(gfx, 32, 32);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);

    auto *sp = Renderable2D::create();
    sp->setPosition(64.f, 32.f);
    sp->setSize(48.f, 48.f);
    sp->setAnchor(0.5f, 0.5f);
    sp->setTexture(albedo);
    CHECK(sp->getNormalTexture() == nullptr);
    sp->setNormalTexture(normal);
    CHECK(sp->getNormalTexture() == normal);
    sp->setReceiveLight(true);
    sp->setRotation(90.f);
    sp->sprite()->canvas = rt;
    sp->sprite()->visible = true;

    // Directional light has no distance falloff, so a brightness gap between
    // equal-distance samples must come from rotated normals — not attenuation.
    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    // After 90° clockwise rotation, the map's +X bias faces screen +Y (down).
    // Light from below → lower half should be brighter.
    light->setDirection(0.f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 1.5f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float topL = luma(rt->getPixel(64, 18));
    float botL = luma(rt->getPixel(64, 46));
    CHECK(botL > topL + 0.04f);

    sp->setNormalTexture(nullptr);
    CHECK(sp->getNormalTexture() == nullptr);
    sp->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

/**
 * Pack a biased normal into a corner of a large atlas so UV derivatives are
 * tiny. The lit2d det singularity must be scale-aware or atlas tiles fall back
 * to the flat-Z path and lose the left/right bias.
 */
static Texture *makeAtlasBiasedNormal(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY, int tileW,
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

static Texture *makeAtlasSolid(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY, int tileW, int tileH,
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

TEST_CASE("Lighting2D.atlasUvNormalMapKeepsSideBias") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(128, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 64.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.08f, 0.08f, 0.08f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    constexpr int kAtlas = 1024;
    constexpr int kTile  = 32;
    Texture      *albedo = makeAtlasSolid(gfx, kAtlas, kAtlas, 0, 0, kTile, kTile, 220, 220, 220);
    Texture      *normal = makeAtlasBiasedNormal(gfx, kAtlas, kAtlas, 0, 0, kTile, kTile);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    Quad *tile = gfx->newQuad(0, 0, kTile, kTile);
    REQUIRE(tile != nullptr);

    auto *sp             = Renderable2D::create();
    sp->transform()->x   = 32.f;
    sp->transform()->y   = 8.f;
    sp->sprite()->width  = 64.f;
    sp->sprite()->height = 48.f;
    sp->setTexture(albedo);
    sp->setNormalTexture(normal);
    sp->setQuad(tile);  // tiny UV span → exercises scale-aware det
    sp->setReceiveLight(true);
    sp->sprite()->canvas  = rt;
    sp->sprite()->visible = true;

    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    light->setDirection(1.f, 0.f);
    light->setColor(1.f, 1.f, 1.f, 1.5f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    float leftL  = luma(rt->getPixel(42, 32));
    float rightL = luma(rt->getPixel(86, 32));
    CHECK(rightL > leftL + 0.05f);

    sp->sprite()->visible = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.additiveBlendBrightensOverClear") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 32.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.f, 0.f, 0.f);
    // Non-black clear so additive and alpha produce distinguishable results.
    cam->data()->r = 0.25f;
    cam->data()->g = 0.1f;
    cam->data()->b = 0.1f;
    cam->data()->a = 1.f;

    Texture              *albedo    = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    const uint8_t         flatPx[4] = {128, 128, 255, 255};
    eve::image::ImageData flatImage(1, 1, "RGBA8");
    std::memcpy(flatImage.getData(), flatPx, 4);
    Texture *flat = gfx->newTexture(&flatImage);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);

    auto *alphaSp             = Renderable2D::create();
    alphaSp->transform()->x   = 4.f;
    alphaSp->transform()->y   = 20.f;
    alphaSp->sprite()->width  = 24.f;
    alphaSp->sprite()->height = 24.f;
    alphaSp->setTexture(albedo);
    alphaSp->setNormalTexture(flat);
    alphaSp->setReceiveLight(true);
    alphaSp->setBlend("alpha");
    alphaSp->setColor(0.4f, 0.4f, 0.4f, 0.5f);
    alphaSp->sprite()->canvas  = rt;
    alphaSp->sprite()->visible = true;

    auto *addSp             = Renderable2D::create();
    addSp->transform()->x   = 36.f;
    addSp->transform()->y   = 20.f;
    addSp->sprite()->width  = 24.f;
    addSp->sprite()->height = 24.f;
    addSp->setTexture(albedo);
    addSp->setNormalTexture(flat);
    addSp->setReceiveLight(true);
    addSp->setBlend("additive");
    addSp->setColor(0.4f, 0.4f, 0.4f, 0.5f);
    addSp->sprite()->canvas  = rt;
    addSp->sprite()->visible = true;

    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    light->setDirection(0.f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 1.f);
    light->setEnabled(true);

    RenderSystem::render(*gfx);

    Color alphaPx = rt->getPixel(16, 32);
    Color addPx   = rt->getPixel(48, 32);
    // Additive over a non-black clear keeps more of the destination red channel
    // than alpha blend (src*a + dst*(1-a)), which darkens the clear.
    CHECK(addPx.r > alphaPx.r + 0.05f);
    CHECK(luma(addPx) > luma(alphaPx) + 0.03f);

    alphaSp->sprite()->visible = false;
    addSp->sprite()->visible   = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}

TEST_CASE("Lighting2D.hdrCanvasLitBlendDoesNotCrash") {
    // HDR canvases reject pixel readback; this is a pipeline-compat smoke test:
    // lit Alpha + Additive must bind HDR lit pipelines (not RGBA8 offscreen).
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);

    eve::window::WindowSettings s;
    s.width    = 320;
    s.height   = 240;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    Canvas *rt = gfx->newHDRCanvas(64, 64);
    REQUIRE(rt != nullptr);

    auto *cam           = Camera2D::createCamera();
    cam->data()->canvas = rt;
    cam->data()->active = true;
    cam->data()->x      = 32.f;
    cam->data()->y      = 32.f;
    cam->data()->zoom   = 1.f;
    cam->setAmbient(0.1f, 0.1f, 0.1f);
    cam->data()->r = 0.f;
    cam->data()->g = 0.f;
    cam->data()->b = 0.f;
    cam->data()->a = 1.f;

    Texture              *albedo    = makeSolidTexture(gfx, 8, 8, 255, 255, 255);
    const uint8_t         flatPx[4] = {128, 128, 255, 255};
    eve::image::ImageData flatImage(1, 1, "RGBA8");
    std::memcpy(flatImage.getData(), flatPx, 4);
    Texture *flat = gfx->newTexture(&flatImage);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);

    auto *alphaSp             = Renderable2D::create();
    alphaSp->transform()->x   = 4.f;
    alphaSp->transform()->y   = 20.f;
    alphaSp->sprite()->width  = 24.f;
    alphaSp->sprite()->height = 24.f;
    alphaSp->setTexture(albedo);
    alphaSp->setNormalTexture(flat);
    alphaSp->setReceiveLight(true);
    alphaSp->setBlend("alpha");
    alphaSp->sprite()->canvas  = rt;
    alphaSp->sprite()->visible = true;

    auto *addSp             = Renderable2D::create();
    addSp->transform()->x   = 36.f;
    addSp->transform()->y   = 20.f;
    addSp->sprite()->width  = 24.f;
    addSp->sprite()->height = 24.f;
    addSp->setTexture(albedo);
    addSp->setNormalTexture(flat);
    addSp->setReceiveLight(true);
    addSp->setBlend("additive");
    addSp->sprite()->canvas  = rt;
    addSp->sprite()->visible = true;

    auto *light = Light2D::createLight("dir");
    light->setCanvas(rt);
    light->setDirection(0.f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 1.f);
    light->setEnabled(true);

    // Must complete without Vulkan/WebGPU pipeline-format mismatch.
    RenderSystem::render(*gfx);
    CHECK(true);

    alphaSp->sprite()->visible = false;
    addSp->sprite()->visible   = false;
    light->setEnabled(false);
    cam->data()->active = false;
    win->close();
}
