#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "data/ByteData.h"
#include "graphics/Graphics.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"

using namespace eve::graphics;

namespace {

#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> readBinaryFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

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

void resetScene3D() {
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
            data->envMap = nullptr;
            data->envIntensity = 1.f;
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

Texture *makeSolidGray(Graphics *gfx, uint8_t v) {
    const uint8_t px[4] = {v, v, v, 255};
    return gfx->newTexture(1, 1, px);
}

float luma(const Color &c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

/** All faces the same solid RGBA — strong specular tint for metals. */
Texture *makeSolidCubemap(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, int face = 4) {
    std::vector<uint8_t> faces(size_t(face) * size_t(face) * 4u * 6u);
    for (size_t i = 0; i < faces.size(); i += 4) {
        faces[i + 0] = r;
        faces[i + 1] = g;
        faces[i + 2] = b;
        faces[i + 3] = 255;
    }
    return gfx->newCubemap(face, faces.data());
}

/**
 * Per-face solid colors in +X,-X,+Y,-Y,+Z,-Z order.
 * With the default camera at +Z looking at the origin, the sphere center
 * reflects roughly toward +Z — so that face dominates a smooth metal.
 */
Texture *makeFaceColoredCubemap(Graphics *gfx, const uint8_t faceRgb[6][3], int face = 8) {
    const size_t faceBytes = size_t(face) * size_t(face) * 4u;
    std::vector<uint8_t> faces(faceBytes * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * faceBytes;
        for (size_t i = 0; i < faceBytes; i += 4) {
            dst[i + 0] = faceRgb[f][0];
            dst[i + 1] = faceRgb[f][1];
            dst[i + 2] = faceRgb[f][2];
            dst[i + 3] = 255;
        }
    }
    return gfx->newCubemap(face, faces.data());
}

/** Load 128^2 PNG faces (px,nx,py,ny,pz,nz) under test/Textures/env/<name>/. */
Texture *loadEnvCubemap(Graphics *gfx, const char *name) {
    eve::image::Image::create();
    static const char *kFaces[6] = {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};

    int faceSize = 0;
    std::vector<uint8_t> packed;
    for (int f = 0; f < 6; ++f) {
        const std::string path =
            pathBesideThisSource((std::string("Textures/env/") + name + "/" + kFaces[f]).c_str());
        auto raw = readBinaryFile(path);
        REQUIRE(!raw.empty());
        eve::data::ByteData bytes(raw.data(), raw.size());
        std::unique_ptr<eve::image::ImageData> img(eve::image::Image::create()->newImageData(&bytes));
        REQUIRE(img.get() != nullptr);
        REQUIRE(img->getFormat() == std::string("RGBA8"));
        if (faceSize == 0) {
            faceSize = img->getWidth();
            REQUIRE(faceSize > 0);
            REQUIRE(img->getHeight() == faceSize);
            packed.resize(size_t(faceSize) * size_t(faceSize) * 4u * 6u);
        } else {
            REQUIRE(img->getWidth() == faceSize);
            REQUIRE(img->getHeight() == faceSize);
        }
        const size_t faceBytes = size_t(faceSize) * size_t(faceSize) * 4u;
        std::memcpy(packed.data() + size_t(f) * faceBytes, img->getData(), faceBytes);
    }
    return gfx->newCubemap(faceSize, packed.data());
}

void addHud(Graphics *) {
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;
}

void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

/** Spin the given entity so IBL / env reflections are visibly readable (~1s). */
void previewRotating(Graphics *gfx, Renderable3D *ent, int ms = 1000) {
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        if (ent != nullptr) ent->transform()->yaw = float(i) * 0.04f;
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

}  // namespace

TEST_CASE("IBL.metallicPicksUpEnvColor") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);

    addHud(gfx);

    // Kill direct light so specular IBL dominates metals.
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const Color c0 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    Texture *env = makeSolidCubemap(gfx, 240, 40, 40);
    cam->setEnvMap(env);
    cam->setEnvIntensity(1.5f);
    warmPresent(gfx);
    const Color c1 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Red env should lift R more than G/B on a metal sphere.
    REQUIRE(c1.r > c0.r + 0.04f);
    REQUIRE(c1.r > c1.g + 0.03f);
    REQUIRE(c1.r > c1.b + 0.03f);

    // Hold the red-env metal sphere so the tint is visible while debugging.
    previewRotating(gfx, ent);

    cam->setEnvIntensity(0.f);
    warmPresent(gfx);
    const Color c2 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(std::abs(c2.r - c0.r) < 0.05f);

    win->close();
}

TEST_CASE("IBL.newCubemapCreatesSixLayers") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    Texture *cube = makeSolidCubemap(gfx, 10, 20, 30);
    REQUIRE(cube != nullptr);
    REQUIRE(cube->layers == 6);
    REQUIRE(cube->getWidth() == 4);
    REQUIRE(cube->getHeight() == 4);
    REQUIRE(cube->getPixelWidth() == 4);
    REQUIRE(cube->getPixelHeight() == 4);

    Texture *larger = makeSolidCubemap(gfx, 1, 2, 3, 16);
    REQUIRE(larger != nullptr);
    REQUIRE(larger->layers == 6);
    REQUIRE(larger->getWidth() == 16);
    win->close();
}

TEST_CASE("IBL.envIntensityNegativeClampsToZero") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setEnvIntensity(1.25f);
    REQUIRE(std::abs(cam->data()->envIntensity - 1.25f) < 1e-5f);
    cam->setEnvIntensity(-3.f);
    REQUIRE(cam->data()->envIntensity == 0.f);

    win->close();
}

TEST_CASE("IBL.envIntensityScalesMetalBrightness") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvMap(makeSolidCubemap(gfx, 220, 220, 220));

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    cam->setEnvIntensity(0.4f);
    warmPresent(gfx);
    const float L_low = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    cam->setEnvIntensity(2.0f);
    warmPresent(gfx);
    const float L_high = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L_high > L_low + 0.04f);

    win->close();
}

TEST_CASE("IBL.clearEnvMapDisablesReflection") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const Color c0 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    cam->setEnvMap(makeSolidCubemap(gfx, 40, 200, 40));
    cam->setEnvIntensity(1.5f);
    warmPresent(gfx);
    const Color c1 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(c1.g > c0.g + 0.04f);

    cam->setEnvMap(nullptr);
    warmPresent(gfx);
    const Color c2 = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(std::abs(c2.r - c0.r) < 0.05f);
    REQUIRE(std::abs(c2.g - c0.g) < 0.05f);
    REQUIRE(std::abs(c2.b - c0.b) < 0.05f);

    win->close();
}

TEST_CASE("IBL.dielectricEnvWeakerThanMetal") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvMap(makeSolidCubemap(gfx, 230, 50, 50));
    cam->setEnvIntensity(1.5f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(0.0f);
    ent->setRoughness(0.35f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const Color cDielectric = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    warmPresent(gfx);
    const Color cMetal = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Metals mirror env via specular F≈1; dielectrics only get a weak diffuse IBL term.
    REQUIRE(cMetal.r > cDielectric.r + 0.05f);
    REQUIRE(cMetal.r > cMetal.g + 0.03f);

    previewRotating(gfx, ent);
    win->close();
}

TEST_CASE("IBL.plusZFaceTintsSmoothMetal") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    // Dark everywhere except +Z (cyan) — the face the default camera reflects into.
    const uint8_t faceRgb[6][3] = {
        {8, 8, 8},     // +X
        {8, 8, 8},     // -X
        {8, 8, 8},     // +Y
        {8, 8, 8},     // -Y
        {40, 220, 230}, // +Z
        {8, 8, 8},     // -Z
    };

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvMap(makeFaceColoredCubemap(gfx, faceRgb));
    cam->setEnvIntensity(1.5f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.06f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const Color c = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Cyan +Z reflection should lift G and B above R.
    REQUIRE(c.g > c.r + 0.03f);
    REQUIRE(c.b > c.r + 0.03f);

    previewRotating(gfx, ent);
    win->close();
}

TEST_CASE("IBL.loadsPolyHavenEnvCubemaps") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    Texture *studio = loadEnvCubemap(gfx, "studio_small_09");
    REQUIRE(studio != nullptr);
    REQUIRE(studio->layers == 6);
    REQUIRE(studio->getWidth() == 128);
    REQUIRE(studio->getHeight() == 128);

    Texture *sky = loadEnvCubemap(gfx, "kloppenheim_06_puresky");
    REQUIRE(sky != nullptr);
    REQUIRE(sky->layers == 6);
    REQUIRE(sky->getWidth() == 128);

    win->close();
}

TEST_CASE("IBL.studioEnvBrightensMetal") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const float L0 = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    cam->setEnvMap(loadEnvCubemap(gfx, "studio_small_09"));
    cam->setEnvIntensity(1.8f);
    warmPresent(gfx);
    const float L1 = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    REQUIRE(L1 > L0 + 0.03f);
    previewRotating(gfx, ent);
    win->close();
}

TEST_CASE("IBL.outdoorSkyDiffersFromStudio") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    Mesh *mesh = gfx->newMeshSphere(24, 16);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 3.f;
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvIntensity(1.6f);

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidGray(gfx, 220);
    ent->setMetallic(1.0f);
    ent->setRoughness(0.08f);
    addHud(gfx);
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);

    cam->setEnvMap(loadEnvCubemap(gfx, "studio_small_09"));
    warmPresent(gfx);
    const Color cStudio = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    cam->setEnvMap(loadEnvCubemap(gfx, "kloppenheim_06_puresky"));
    warmPresent(gfx);
    const Color cSky = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Different real environments should not land on the same metal response.
    const float dr = std::abs(cStudio.r - cSky.r);
    const float dg = std::abs(cStudio.g - cSky.g);
    const float db = std::abs(cStudio.b - cSky.b);
    REQUIRE(dr + dg + db > 0.04f);

    // Studio then outdoor sky, each held briefly so the env change is visible.
    cam->setEnvMap(loadEnvCubemap(gfx, "studio_small_09"));
    previewRotating(gfx, ent, 800);
    cam->setEnvMap(loadEnvCubemap(gfx, "kloppenheim_06_puresky"));
    previewRotating(gfx, ent, 800);
    win->close();
}
