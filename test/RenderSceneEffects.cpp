#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "graphics/AmbientOcclusion.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shadow.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace eve::graphics;

namespace {

float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

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
            data->envIntensity = 0.f;
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

Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

void addHud() {
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
}

void warmPresent(Graphics *gfx, int frames = 2) {
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Delay(8);
    }
}

float meanLumaRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4) {
    double sum = 0.0;
    int n = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += luma(gfx->getPixel(x, y));
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

float meanGreenRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4) {
    double sum = 0.0;
    int n = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += gfx->getPixel(x, y).g;
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

float meanRedRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4) {
    double sum = 0.0;
    int n = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += gfx->getPixel(x, y).r;
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

/** Minimal unit cube OBJ (outward CCW for RH Y-up). */
static const char kCubeObj[] =
    "v -0.5 -0.5  0.5\nv  0.5 -0.5  0.5\nv  0.5  0.5  0.5\nv -0.5  0.5  0.5\n"
    "vn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n"
    "v  0.5 -0.5 -0.5\nv -0.5 -0.5 -0.5\nv -0.5  0.5 -0.5\nv  0.5  0.5 -0.5\n"
    "vn 0 0 -1\nf 5//2 6//2 7//2\nf 5//2 7//2 8//2\n"
    "v  0.5 -0.5  0.5\nv  0.5 -0.5 -0.5\nv  0.5  0.5 -0.5\nv  0.5  0.5  0.5\n"
    "vn 1 0 0\nf 9//3 10//3 11//3\nf 9//3 11//3 12//3\n"
    "v -0.5 -0.5 -0.5\nv -0.5 -0.5  0.5\nv -0.5  0.5  0.5\nv -0.5  0.5 -0.5\n"
    "vn -1 0 0\nf 13//4 14//4 15//4\nf 13//4 15//4 16//4\n"
    "v -0.5  0.5  0.5\nv  0.5  0.5  0.5\nv  0.5  0.5 -0.5\nv -0.5  0.5 -0.5\n"
    "vn 0 1 0\nf 17//5 18//5 19//5\nf 17//5 19//5 20//5\n"
    "v -0.5 -0.5 -0.5\nv  0.5 -0.5 -0.5\nv  0.5 -0.5  0.5\nv -0.5 -0.5  0.5\n"
    "vn 0 -1 0\nf 21//6 22//6 23//6\nf 21//6 23//6 24//6\n";

Mesh *loadCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    return m;
}

}  // namespace

TEST_CASE("RenderScenes.orientation.worldYMapsToScreenUp") {
    // Green sphere above origin, red below — top of framebuffer must read greener.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(20, 12);
    auto *top = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.05f, 0.f);
    top->setScale(0.55f, 0.55f, 0.55f);
    top->setMetallic(0.f);
    top->setRoughness(0.85f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.05f, 0.f);
    bot->setScale(0.55f, 0.55f, 0.55f);
    bot->setMetallic(0.f);
    bot->setRoughness(0.85f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.35f, 0.35f, 0.35f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 40.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float topG = meanGreenRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3);
    const float botR = meanRedRegion(gfx, w / 4, (h * 2) / 3, (w * 3) / 4, h);
    const float topR = meanRedRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3);
    const float botG = meanGreenRegion(gfx, w / 4, (h * 2) / 3, (w * 3) / 4, h);

    std::printf("RenderScenes.orientation topG=%.3f topR=%.3f botR=%.3f botG=%.3f\n", topG, topR,
                botR, botG);
    // Upside-down regression: top must prefer green, bottom prefer red.
    REQUIRE(topG > topR + 0.04f);
    REQUIRE(botR > botG + 0.04f);
    REQUIRE(topG > botG + 0.04f);
    REQUIRE(botR > topR + 0.04f);

    win->close();
}

TEST_CASE("RenderScenes.lighting.ambientHonorsCamera") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    auto *cube = Renderable3D::create();
    cube->setMesh(loadCube(gfx));
    cube->setTexture(makeSolid(gfx, 240, 240, 240));
    cube->setMetallic(0.f);
    cube->setRoughness(0.9f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(1.6f, 1.2f, 2.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 30.f;

    // No lights — only ambient contributes for dielectrics.
    RenderSystem3D::setDirectionalLight(0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
    addHud();

    cam->setAmbient(0.02f, 0.02f, 0.02f);
    warmPresent(gfx, 2);
    const Color darkPx = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    const float dark = luma(darkPx);

    cam->setAmbient(0.55f, 0.55f, 0.55f);
    warmPresent(gfx, 2);
    const Color brightPx = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    const float bright = luma(brightPx);

    std::printf("RenderScenes.ambient dark=%.4f bright=%.4f\n", dark, bright);
    // Without the old shader ambient floor, low ambient stays dark and tracks the camera.
    REQUIRE(dark < 0.18f);
    REQUIRE(bright > dark + 0.12f);
    REQUIRE(bright > 0.25f);

    win->close();
}

TEST_CASE("RenderScenes.lighting.pointDoesNotBreakOrientation") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.02f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(20, 12);
    auto *top = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 30, 220, 30));
    top->setPosition(0.f, 1.15f, 0.f);
    top->setScale(0.6f, 0.6f, 0.6f);
    top->setMetallic(0.f);
    top->setRoughness(0.9f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 30, 30));
    bot->setPosition(0.f, -1.15f, 0.f);
    bot->setScale(0.6f, 0.6f, 0.6f);
    bot->setMetallic(0.f);
    bot->setRoughness(0.9f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.2f, 0.2f, 0.2f);

    auto *pt = Light3D::createLight("point");
    pt->setPosition(0.f, 0.f, 2.0f);
    pt->setColor(1.f, 1.f, 1.f, 6.f);
    pt->setRadius(8.f);
    pt->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    // Sample near expected sphere centers rather than whole thirds.
    const Color topPx = gfx->getPixel(w / 2, h / 5);
    const Color botPx = gfx->getPixel(w / 2, (h * 4) / 5);
    std::printf("RenderScenes.pointOrient top=(%.2f,%.2f) bot=(%.2f,%.2f)\n", topPx.r, topPx.g,
                botPx.r, botPx.g);
    REQUIRE(topPx.g > topPx.r + 0.05f);
    REQUIRE(botPx.r > botPx.g + 0.05f);

    win->close();
}

TEST_CASE("RenderScenes.shadow.dirShadowDarkensWithoutFlip") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.07f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 180, 180, 185));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.5f, 0.08f, 3.5f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.95f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *occluder = Renderable3D::create();
    occluder->setMesh(sphere);
    occluder->setTexture(makeSolid(gfx, 40, 200, 40));
    occluder->setPosition(0.f, 0.55f, 0.f);
    occluder->setScale(0.55f, 0.55f, 0.55f);
    occluder->setCastShadow(true);
    occluder->setReceiveShadow(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(2.8f, 2.2f, 3.6f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.09f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.35f, 1.f, 0.25f);
    sun->setColor(1.f, 0.98f, 0.94f, 2.8f);
    sun->setCastShadow(false);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 2);
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    // Occluder sits above ground — with correct Y, green should dominate the upper half.
    const float topG0 = meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2);
    const float mid0 = meanLumaRegion(gfx, w / 3, h / 2, (w * 2) / 3, (h * 5) / 6);

    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    warmPresent(gfx, 3);
    const float topG1 = meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2);
    const float mid1 = meanLumaRegion(gfx, w / 3, h / 2, (w * 2) / 3, (h * 5) / 6);

    std::printf("RenderScenes.shadow topG0=%.3f topG1=%.3f mid0=%.3f mid1=%.3f\n", topG0, topG1,
                mid0, mid1);
    REQUIRE(topG1 > 0.05f);           // orientation still upright
    REQUIRE(mid1 + 0.01f < mid0);     // ground umbra darkens
    REQUIRE(std::fabs(topG1 - topG0) < 0.25f);  // occluder itself not nuked

    win->close();
}

TEST_CASE("RenderScenes.effects.gbufferAoDoesNotInvertOrientation") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.03f, 0.03f, 0.04f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *top = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 210, 40));
    top->setPosition(0.f, 1.0f, 0.f);
    top->setScale(0.5f, 0.5f, 0.5f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 210, 40, 40));
    bot->setPosition(0.f, -1.0f, 0.f);
    bot->setScale(0.5f, 0.5f, 0.5f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.0f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.2f, 0.2f, 0.22f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.3f, 1.f, 0.2f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setEnabled(true);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->enable("gbuffer");
    rc->compile();
    REQUIRE(rc->isCompiled());
    REQUIRE(rc->hasPass("gbuffer"));

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    REQUIRE(meanGreenRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3) >
            meanRedRegion(gfx, w / 4, 0, (w * 3) / 4, h / 3) + 0.03f);

    // Disable gbuffer so later cases are not polluted.
    rc->disable("gbuffer");
    rc->compile();
    win->close();
}

TEST_CASE("RenderScenes.mesh.assimpNodeTransformBaked") {
    // Parent node translates +Y by 2 — baked upload must move verts with it.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    medialoader::ModelLoader loader;
    auto scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);

    aiMatrix4x4 world;
    world.a4 = 0.f;
    world.b4 = 2.f;  // +Y translation
    world.c4 = 0.f;
    Mesh *mesh = gfx->newMeshFromAssimp(*scene->mMeshes[0], world);
    REQUIRE(mesh != nullptr);

    auto *ent = Renderable3D::create();
    ent->setMesh(mesh);
    ent->setTexture(makeSolid(gfx, 40, 220, 40));
    ent->setMetallic(0.f);
    ent->setRoughness(0.8f);

    auto *cam = Camera3D::createCamera();
    // Aim at the baked +Y location; identity-local cube at origin would miss the frustum center.
    cam->setEye(0.f, 2.f, 4.0f);
    cam->setTarget(0.f, 2.f, 0.f);
    cam->setAmbient(0.25f, 0.25f, 0.25f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.4f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float mid = meanLumaRegion(gfx, w / 3, h / 3, (w * 2) / 3, (h * 2) / 3);
    REQUIRE(mid > 0.08f);

    // Control: upload without bake, keep camera on +Y — center should be mostly background.
    resetScene3D();
    Mesh *local = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    auto *miss = Renderable3D::create();
    miss->setMesh(local);
    miss->setTexture(makeSolid(gfx, 40, 220, 40));
    auto *cam2 = Camera3D::createCamera();
    cam2->setEye(0.f, 2.f, 4.0f);
    cam2->setTarget(0.f, 2.f, 0.f);
    cam2->setAmbient(0.25f, 0.25f, 0.25f);
    auto *sun2 = Light3D::createLight("dir");
    sun2->setDirection(0.2f, 1.f, 0.3f);
    sun2->setColor(1.f, 1.f, 1.f, 1.4f);
    sun2->setEnabled(true);
    addHud();
    warmPresent(gfx, 2);
    const float midMiss = meanLumaRegion(gfx, w / 3, h / 3, (w * 2) / 3, (h * 2) / 3);
    std::printf("RenderScenes.nodeBake mid=%.3f midMiss=%.3f\n", mid, midMiss);
    REQUIRE(mid > midMiss + 0.04f);

    win->close();
}

TEST_CASE("RenderScenes.clipSpace.perspectiveVulkanFlipsY") {
    const glm::mat4 gl = glm::perspectiveRH_ZO(1.0f, 1.0f, 0.1f, 100.f);
    const glm::mat4 vk = perspectiveVulkanRH_ZO(1.0f, 1.0f, 0.1f, 100.f);
    CHECK(std::fabs(gl[1][1] + vk[1][1]) < 1e-5f);
    CHECK(vk[1][1] < 0.f);
    CHECK(gl[1][1] > 0.f);
}

TEST_CASE("RenderScenes.pick.screenToRayMatchesUprightProjection") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    resetScene3D();

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 5.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setUp(0.f, 1.f, 0.f);
    cam->setFov(60.f);

    // Top-center pixel should ray toward +Y (world up), not -Y.
    cam->screenToRay(100.f, 10.f, 200.f, 150.f);
    REQUIRE(cam->getScreenRayDirY() > 0.05f);

    // Bottom-center pixel should ray toward -Y.
    cam->screenToRay(100.f, 140.f, 200.f, 150.f);
    REQUIRE(cam->getScreenRayDirY() < -0.05f);

    win->close();
}
