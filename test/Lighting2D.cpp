#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "image/ImageData.h"
#include "window/Window.h"

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
    sp->sprite()->texture = albedo;
    sp->sprite()->normalTexture = normal;
    sp->sprite()->receiveLight = true;
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

    auto *cam = Camera2D::createCamera();
    cam->setAmbient(0.2f, 0.3f, 0.4f);
    CHECK(std::abs(cam->data()->ambientR - 0.2f) < 1e-5f);
    CHECK(std::abs(cam->data()->ambientG - 0.3f) < 1e-5f);
    CHECK(std::abs(cam->data()->ambientB - 0.4f) < 1e-5f);

    pt->setEnabled(false);
    dir->setEnabled(false);
    cam->data()->active = false;
}
