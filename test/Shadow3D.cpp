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

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 400, int h = 300) {
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

Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

/** Unit box centered at origin (from newMeshSphere-style: use a scaled sphere as occluder). */
Mesh *makeGroundPlane(Graphics *gfx) {
    // Thin flat box via scaled sphere is awkward; use a large shallow scale on sphere.
    return gfx->newMeshSphere(16, 8);
}

void addHud() {
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
}

float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

}  // namespace

TEST_CASE("Shadow3D.dirLightDarkensOccludedGround") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.05f, 0.05f, 0.05f);

    // Ground: large flat-ish sphere at y=-0.85
    auto *ground = Renderable3D::create();
    ground->setMesh(makeGroundPlane(gfx));
    ground->setTexture(makeSolid(gfx, 220, 220, 220));
    ground->setPosition(0.f, -1.2f, 0.f);
    ground->setScale(4.f, 0.15f, 4.f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.85f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    // Occluder above ground
    auto *box = Renderable3D::create();
    box->setMesh(gfx->newMeshSphere(20, 12));
    box->setTexture(makeSolid(gfx, 200, 200, 200));
    box->setPosition(0.f, 0.35f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.f);
    box->setRoughness(0.6f);
    box->setCastShadow(true);
    box->setReceiveShadow(false);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setShadowBias(0.003f);

    addHud();
    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);

    // Sample near center (likely under occluder) vs far right (lit ground).
    const int cx = gfx->getWidth() / 2;
    const int cy = gfx->getHeight() / 2 + gfx->getHeight() / 8;
    const int dx = gfx->getWidth() / 3;
    const float L_center = luma(gfx->getPixel(cx, cy));
    const float L_side = luma(gfx->getPixel(cx + dx, cy));

    // With shadows, side should be brighter than the occluded center region.
    // Also compare against shadows disabled.
    sun->setCastShadow(false);
    warmPresent(gfx);
    const float L_center_off = luma(gfx->getPixel(cx, cy));

    sun->setCastShadow(true);
    warmPresent(gfx);
    const float L_center_on = luma(gfx->getPixel(cx, cy));

    REQUIRE(L_center_off > L_center_on + 0.03f);
    REQUIRE(L_side > L_center + 0.02f);

    win->close();
}

TEST_CASE("Shadow3D.receiveShadowFalseIgnoresMap") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.05f, 0.05f, 0.05f);

    auto *ground = Renderable3D::create();
    ground->setMesh(gfx->newMeshSphere(16, 8));
    ground->setTexture(makeSolid(gfx, 220, 220, 220));
    ground->setPosition(0.f, -1.2f, 0.f);
    ground->setScale(4.f, 0.15f, 4.f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(false);

    auto *box = Renderable3D::create();
    box->setMesh(gfx->newMeshSphere(20, 12));
    box->setTexture(makeSolid(gfx, 200, 200, 200));
    box->setPosition(0.f, 0.35f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setCastShadow(true);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(true);

    addHud();
    gfx->setScreenReadbackEnabled(true);
    warmPresent(gfx);
    const int cx = gfx->getWidth() / 2;
    const int cy = gfx->getHeight() / 2 + gfx->getHeight() / 8;
    const float L_offRecv = luma(gfx->getPixel(cx, cy));

    ground->setReceiveShadow(true);
    warmPresent(gfx);
    const float L_onRecv = luma(gfx->getPixel(cx, cy));

    REQUIRE(L_offRecv > L_onRecv + 0.02f);

    win->close();
}
