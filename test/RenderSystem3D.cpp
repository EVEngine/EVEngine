#include "zeroerr/assert.h"
#include <cstdio>
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "graphics/Graphics.h"
#include "graphics/Light.h"
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

/** Non-periodic motif so UV remapping (not just phase shift) is measurable. */
static Texture *makeMotif(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    const float cx = float(size) * 0.35f;
    const float cy = float(size) * 0.4f;
    const float r = float(size) * 0.22f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float dx = float(x) - cx;
            float dy = float(y) - cy;
            float d = std::sqrt(dx * dx + dy * dy);
            uint8_t v = d < r ? 255 : uint8_t((x * 13 + y * 7) % 180);
            if (x > size * 3 / 4 && y < size / 4) v = 40;
            size_t i = size_t(y * size + x) * 4;
            px[i] = v;
            px[i + 1] = uint8_t((v * 3) / 4);
            px[i + 2] = uint8_t(255 - v / 2);
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), true, true);
}

// Graphics/ECS are process-wide singletons — hide leftovers from earlier cases.
static void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp] = *it;
            sp->visible = false;
        }
    }
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

TEST_CASE("Mesh.newMeshSphere") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Mesh *m = gfx->newMeshSphere(24, 12);
    REQUIRE(m != nullptr);
    CHECK_GT(m->indexCount, 0);
    // stacks bands * slices quads * 2 tris * 3 indices
    CHECK_EQ(m->indexCount, 24 * 12 * 6);

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
    resetScene3D();

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

    // ~1s of visible rotation so the smoke case is a real on-screen scene.
    for (int i = 0; i < 60; ++i) {
        ent->transform()->yaw = float(i) * 0.05f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    win->close();
}

TEST_CASE("RenderSystem3D.textureCheckerPixels") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

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
    // Warm up a couple of frames after possible surface/swapchain recreate.
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    const int cx = gfx->getWidth() / 2;
    const int cy = gfx->getHeight() / 2;
    const int dx = std::max(16, gfx->getWidth() / 10);
    // Sample left vs right of the projected front face (UV u≈0.25 vs u≈0.75).
    Color a = gfx->getPixel(cx - dx, cy);
    Color b = gfx->getPixel(cx + dx, cy);
    // Lit PBR softens albedo contrast vs unlit; still require a clear left/right split.
    REQUIRE(std::abs(luma(a) - luma(b)) > 0.05f);

    win->close();
}

TEST_CASE("RenderSystem3D.lightingAffectsPixels") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

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
    // Empirically for this mesh/view under the PBR path, +Z lights the visible face brighter.
    RenderSystem3D::setDirectionalLight(0.f, 0.f, 1.f, 1.f, 1.f, 1.f);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_bright = luma(gfx->getPixel(cx, cy));

    RenderSystem3D::setDirectionalLight(0.f, 0.f, -1.f, 1.f, 1.f, 1.f);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_dark = luma(gfx->getPixel(cx, cy));

    REQUIRE(L_bright - L_dark > 0.08f);

    win->close();
}

TEST_CASE("Lighting3D.pointLightBrightensNearbyFace") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = loadUvCube(gfx);
    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.04f, 0.04f, 0.04f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(0.05f);
    ent->setRoughness(0.55f);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;

    // Point light in front of +Z face.
    auto *light = Light3D::createLight("point");
    light->setPosition(0.f, 0.f, 2.f);
    light->setColor(1.f, 1.f, 1.f, 4.f);
    light->setRadius(6.f);

    // Disable legacy directional fallback by also having Light3D enabled (already).
    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_near = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    light->setPosition(0.f, 0.f, -4.f);  // behind cube → front face darker
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_far = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L_near > L_far + 0.05f);

    light->setEnabled(false);
    win->close();
}

TEST_CASE("Lighting3D.metallicIncreasesSpecularHighlight") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = loadUvCube(gfx);
    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.05f, 0.05f, 0.05f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 200);
    ent->setRoughness(0.2f);
    ent->setMetallic(0.0f);

    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;

    auto *light = Light3D::createLight("dir");
    // Light from the camera side so N·L > 0 and a specular lobe can form.
    light->setDirection(0.f, 0.35f, 1.f);
    light->setColor(1.f, 1.f, 1.f, 2.0f);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_dielectric = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    ent->setMetallic(1.0f);
    ent->setRoughness(0.12f);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
    const float L_metal = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L_dielectric > 0.08f);
    // With this view, the specular lobe may miss the center pixel; metals still
    // drop Lambert diffuse, so the metal sample should be darker than dielectric.
    REQUIRE(L_metal < L_dielectric - 0.02f);

    light->setEnabled(false);
    win->close();
}

TEST_CASE("RenderSystem3D.texCellBombChangesPixels") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    resetScene3D();

    // Same cube + camera as textureCheckerPixels (known-good textured face).
    Mesh *mesh = loadUvCube(gfx);
    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeMotif(gfx, 64);
    ent->setMetallic(0.f);
    ent->setRoughness(0.85f);

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

    auto capture = [&](std::vector<float> &out) {
        out.clear();
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        const int w = gfx->getWidth();
        const int h = gfx->getHeight();
        const int cx = w / 2;
        const int cy = h / 2;
        const int dx = std::max(8, w / 12);
        const int dy = std::max(8, h / 12);
        for (int y = cy - 2 * dy; y <= cy + 2 * dy; y += dy) {
            for (int x = cx - 2 * dx; x <= cx + 2 * dx; x += dx) {
                out.push_back(luma(gfx->getPixel(x, y)));
            }
        }
    };

    std::vector<float> off, on;
    ent->setTexCellBomb(3.f, 0.f, 1.f);
    CHECK_EQ(ent->getTexCellBombStrength(), 0.f);
    capture(off);

    ent->setTexCellBomb(3.f, 1.f, 1.f);
    CHECK_EQ(ent->getTexCellBombScale(), 3.f);
    CHECK_EQ(ent->getTexCellBombStrength(), 1.f);
    capture(on);

    REQUIRE_EQ(off.size(), on.size());
    REQUIRE_GT(off.size(), 4u);

    float offMin = off[0], offMax = off[0];
    for (float v : off) {
        offMin = std::min(offMin, v);
        offMax = std::max(offMax, v);
    }
    REQUIRE(offMax - offMin > 0.02f);

    float mad = 0.f;
    for (size_t i = 0; i < off.size(); ++i)
        mad = std::max(mad, std::fabs(off[i] - on[i]));
    if (!(mad > 0.015f)) {
        std::fprintf(stderr, "texCellBomb mad=%g offRange=%g n=%zu\n", mad, offMax - offMin,
                     off.size());
    }
    REQUIRE(mad > 0.015f);

    win->close();
}

/** Left-half high / right-half low height so glancing POM shifts albedo UVs. */
static Texture *makeSplitHeight(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t h = (x < size / 2) ? 255 : 0;
            size_t i = size_t(y * size + x) * 4;
            px[i] = h;
            px[i + 1] = h;
            px[i + 2] = h;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), true, true);
}

TEST_CASE("RenderSystem3D.parallaxChangesPixels") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    resetScene3D();

    Mesh *mesh = loadUvCube(gfx);
    auto *cam = Camera3D::createCamera();
    // Off-axis eye so tangent-space view has a strong XY component.
    cam->setEye(1.6f, 0.7f, 2.4f);
    cam->setTarget(0.f, 0.f, 0.f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeMotif(gfx, 64);
    ent->setHeightTexture(makeSplitHeight(gfx, 64));
    ent->setMetallic(0.f);
    ent->setRoughness(0.85f);

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

    auto capture = [&](std::vector<float> &out) {
        out.clear();
        for (int i = 0; i < 3; ++i) {
            RenderSystem3D::render(*gfx);
            RenderSystem::render(*gfx);
        }
        const int w = gfx->getWidth();
        const int h = gfx->getHeight();
        const int cx = w / 2;
        const int cy = h / 2;
        const int dx = std::max(8, w / 12);
        const int dy = std::max(8, h / 12);
        for (int y = cy - 2 * dy; y <= cy + 2 * dy; y += dy) {
            for (int x = cx - 2 * dx; x <= cx + 2 * dx; x += dx) {
                out.push_back(luma(gfx->getPixel(x, y)));
            }
        }
    };

    std::vector<float> off, on;
    ent->setParallax(0.f);
    CHECK_EQ(ent->getParallaxScale(), 0.f);
    capture(off);

    ent->setParallax(0.12f, 8.f, 24.f);
    CHECK_EQ(ent->getParallaxScale(), 0.12f);
    CHECK_EQ(ent->getParallaxMinLayers(), 8.f);
    CHECK_EQ(ent->getParallaxMaxLayers(), 24.f);
    capture(on);

    REQUIRE_EQ(off.size(), on.size());
    REQUIRE_GT(off.size(), 4u);

    float offMin = off[0], offMax = off[0];
    for (float v : off) {
        offMin = std::min(offMin, v);
        offMax = std::max(offMax, v);
    }
    REQUIRE(offMax - offMin > 0.02f);

    float mad = 0.f;
    for (size_t i = 0; i < off.size(); ++i)
        mad = std::max(mad, std::fabs(off[i] - on[i]));
    if (!(mad > 0.01f)) {
        std::fprintf(stderr, "parallax mad=%g offRange=%g n=%zu\n", mad, offMax - offMin,
                     off.size());
    }
    REQUIRE(mad > 0.01f);

    win->close();
}

TEST_CASE("Camera3D.screenToRayPick") {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 5.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setUp(0.f, 1.f, 0.f);
    cam->setFov(60.f);
    cam->screenToRay(160.f, 120.f, 320.f, 240.f);  // center pixel

    CHECK(std::fabs(cam->getScreenRayOriginX()) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayOriginY()) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayOriginZ() - 5.f) < 1e-4f);

    // Looking toward -Z; center ray should be mostly -Z.
    CHECK(std::fabs(cam->getScreenRayDirX()) < 0.05f);
    CHECK(std::fabs(cam->getScreenRayDirY()) < 0.05f);
    CHECK(cam->getScreenRayDirZ() < -0.9f);

    float len = std::sqrt(cam->getScreenRayDirX() * cam->getScreenRayDirX() +
                          cam->getScreenRayDirY() * cam->getScreenRayDirY() +
                          cam->getScreenRayDirZ() * cam->getScreenRayDirZ());
    CHECK(std::fabs(len - 1.f) < 1e-4f);
}
