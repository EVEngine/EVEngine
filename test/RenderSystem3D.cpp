#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"

using namespace eve::graphics;

// Cube with per-face UVs (0..1). Front face (+Z) is first so Assimp preserves it.
static const char kCubeObj[] =
    // +Z front
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\n"
    "f 1/1/1 3/3/1 4/4/1\n"
    // -Z back
    "v  0.5 -0.5 -0.5\n"
    "v -0.5 -0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "vn 0 0 -1\n"
    "f 5/1/2 6/2/2 7/3/2\n"
    "f 5/1/2 7/3/2 8/4/2\n"
    // +X right
    "v  0.5 -0.5  0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v  0.5  0.5  0.5\n"
    "vn 1 0 0\n"
    "f 9/1/3 10/2/3 11/3/3\n"
    "f 9/1/3 11/3/3 12/4/3\n"
    // -X left
    "v -0.5 -0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn -1 0 0\n"
    "f 13/1/4 14/2/4 15/3/4\n"
    "f 13/1/4 15/3/4 16/4/4\n"
    // +Y top
    "v -0.5  0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn 0 1 0\n"
    "f 17/1/5 18/2/5 19/3/5\n"
    "f 17/1/5 19/3/5 20/4/5\n"
    // -Y bottom
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v -0.5 -0.5  0.5\n"
    "vn 0 -1 0\n"
    "f 21/1/6 22/2/6 23/3/6\n"
    "f 21/1/6 23/3/6 24/4/6\n";

static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

static void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
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

static Mesh *loadUvCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    REQUIRE(scene->mMeshes[0]->HasTextureCoords(0));
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    REQUIRE_GT(m->indexCount, 0);
    return m;
}

static Texture *makeChecker(Graphics *gfx, int size, int cell) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t c = ((x / cell) ^ (y / cell)) & 1 ? 255 : 0;
            size_t i = size_t(y * size + x) * 4;
            px[i] = c;
            px[i + 1] = c;
            px[i + 2] = c;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

static Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

// NOTE: Graphics is a process-wide singleton — one window lifetime per process.
TEST_CASE("Mesh.newMeshFromAssimpCube") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Mesh *m = loadUvCube(gfx);
    REQUIRE(m != nullptr);

    win->close();
}

TEST_CASE("Graphics.screenGetPixelAfterPresent") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 64, 64);

    const Color expected(0.20f, 0.45f, 0.70f, 1.f);
    gfx->setScreenReadbackEnabled(true);
    gfx->clear(expected, std::nullopt, std::nullopt);
    // Full-screen solid ensures a known color even if clear-only present is optimized later.
    gfx->drawSolidRect(0, 0, float(gfx->getWidth()), float(gfx->getHeight()), expected);
    gfx->present();

    Color c = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(std::abs(c.r - expected.r) < 0.08f);
    REQUIRE(std::abs(c.g - expected.g) < 0.08f);
    REQUIRE(std::abs(c.b - expected.b) < 0.08f);

    win->close();
}

TEST_CASE("RenderSystem3D.smokeRotatingCube") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Mesh *mesh = loadUvCube(gfx);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeChecker(gfx, 16, 4);

    // Same-frame 2D overlay (also ensures present closes the open 3D pass).
    auto *hud = Renderable2D::create();
    hud->transform()->x = 4;
    hud->transform()->y = 4;
    hud->sprite()->width = 8;
    hud->sprite()->height = 8;
    hud->sprite()->r = 1.f;
    hud->sprite()->g = 0.2f;
    hud->sprite()->b = 0.2f;

    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    for (int i = 0; i < 30; ++i) {
        ent->transform()->yaw = float(i) * 0.05f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    win->close();
}

TEST_CASE("RenderSystem3D.textureCheckerPixels") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Mesh *mesh = loadUvCube(gfx);
    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    // 2x2 checker: left/right halves differ clearly under face-on projection.
    ent->meshRenderer()->texture = makeChecker(gfx, 16, 8);

    // Off-sample HUD so present closes the 3D pass via the same path as smoke.
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;

    gfx->setScreenReadbackEnabled(true);
    RenderSystem3D::setDirectionalLight(0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);

    const int cx = gfx->getWidth() / 2;
    const int cy = gfx->getHeight() / 2;
    const int dx = std::max(16, gfx->getWidth() / 10);
    // Sample left vs right of the projected front face (UV u≈0.25 vs u≈0.75).
    Color a = gfx->getPixel(cx - dx, cy);
    Color b = gfx->getPixel(cx + dx, cy);
    REQUIRE(std::abs(luma(a) - luma(b)) > 0.08f);

    win->close();
}

TEST_CASE("RenderSystem3D.lightingAffectsPixels") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Mesh *mesh = loadUvCube(gfx);
    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;

    const int cx = gfx->getWidth() / 2;
    const int cy = gfx->getHeight() / 2;

    gfx->setScreenReadbackEnabled(true);
    // LightDir is dotted with normals as-is; for +Z-facing geometry, -Z lights the front.
    RenderSystem3D::setDirectionalLight(0.f, 0.f, -1.f, 1.f, 1.f, 1.f);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);
    const float L_bright = luma(gfx->getPixel(cx, cy));

    RenderSystem3D::setDirectionalLight(0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
    RenderSystem3D::render(*gfx);
    RenderSystem::render(*gfx);
    const float L_dark = luma(gfx->getPixel(cx, cy));

    REQUIRE(L_bright - L_dark > 0.1f);

    win->close();
}
