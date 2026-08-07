#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "window/Window.h"

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

void resetScene3D() {
    if (ecs::ComponentManager<Renderable3D>::inst().registy != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr] = *it;
            mr->visible = false;
        }
    }
    if (ecs::ComponentManager<Camera3D>::inst().registy != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            data->active = false;
            data->envMap = nullptr;
            data->envIntensity = 1.f;
        }
    }
    if (ecs::ComponentManager<Light3D>::inst().registy != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d] = *it;
            d->enabled = false;
        }
    }
    if (ecs::ComponentManager<Renderable2D>::inst().registy != nullptr) {
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

/** All faces the same solid RGBA — strong specular tint for metals. */
Texture *makeSolidCubemap(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const int face = 4;
    std::vector<uint8_t> faces(size_t(face) * size_t(face) * 4u * 6u);
    for (size_t i = 0; i < faces.size(); i += 4) {
        faces[i + 0] = r;
        faces[i + 1] = g;
        faces[i + 2] = b;
        faces[i + 3] = 255;
    }
    return gfx->newCubemap(face, faces.data());
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
    win->close();
}
