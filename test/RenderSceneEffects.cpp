#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

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
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
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
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }


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

TEST_CASE("RenderScenes.studio.threePointBrightensVsAmbient") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(20, 12);
    auto *subj = Renderable3D::create();
    subj->setMesh(sphere);
    subj->setTexture(makeSolid(gfx, 210, 210, 215));
    subj->setMetallic(0.15f);
    subj->setRoughness(0.45f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.6f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.04f, 0.04f, 0.05f);

    addHud();
    warmPresent(gfx, 2);
    const float ambOnly = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    auto *key = Light3D::createLight("dir");
    key->setDirection(0.55f, 0.85f, 0.35f);
    key->setColor(1.f, 0.97f, 0.92f, 2.4f);
    key->setEnabled(true);

    auto *fill = Light3D::createLight("point");
    fill->setPosition(-1.6f, 0.4f, 1.2f);
    fill->setColor(0.55f, 0.65f, 1.f, 5.5f);
    fill->setRadius(5.f);
    fill->setEnabled(true);

    auto *rim = Light3D::createLight("point");
    rim->setPosition(0.2f, 1.2f, -1.8f);
    rim->setColor(1.f, 0.75f, 0.55f, 4.5f);
    rim->setRadius(5.f);
    rim->setEnabled(true);

    warmPresent(gfx, 3);
    const float lit = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));
    std::printf("RenderScenes.threePoint amb=%.3f lit=%.3f\n", ambOnly, lit);
    REQUIRE(lit > ambOnly + 0.05f);

    // Orientation still upright: sample above/below center after lighting.
    auto *topMark = Renderable3D::create();
    topMark->setMesh(sphere);
    topMark->setTexture(makeSolid(gfx, 40, 220, 40));
    topMark->setPosition(0.f, 1.35f, 0.f);
    topMark->setScale(0.35f, 0.35f, 0.35f);
    warmPresent(gfx, 2);
    const Color topPx = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 5);
    REQUIRE(topPx.g + 0.02f > topPx.r);

    win->close();
}

TEST_CASE("RenderScenes.ibl.metalBrighterThanDielectric") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.02f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(24, 14);
    auto *metal = Renderable3D::create();
    metal->setMesh(sphere);
    metal->setTexture(makeSolid(gfx, 220, 220, 225));
    metal->setPosition(-0.85f, 0.f, 0.f);
    metal->setScale(0.55f, 0.55f, 0.55f);
    metal->setMetallic(1.f);
    metal->setRoughness(0.12f);

    auto *diel = Renderable3D::create();
    diel->setMesh(sphere);
    diel->setTexture(makeSolid(gfx, 220, 220, 225));
    diel->setPosition(0.85f, 0.f, 0.f);
    diel->setScale(0.55f, 0.55f, 0.55f);
    diel->setMetallic(0.f);
    diel->setRoughness(0.55f);

    // Face-colored env so metals pick up tinted specular.
    const uint8_t faceRgb[6][3] = {
        {240, 180, 120}, {100, 140, 220}, {250, 250, 255}, {30, 30, 35}, {200, 210, 230},
        {180, 160, 140},
    };
    const int face = 12;
    std::vector<uint8_t> faces(size_t(face) * size_t(face) * 4u * 6u);
    for (int f = 0; f < 6; ++f) {
        uint8_t *dst = faces.data() + size_t(f) * size_t(face) * size_t(face) * 4u;
        for (int i = 0; i < face * face; ++i) {
            dst[i * 4 + 0] = faceRgb[f][0];
            dst[i * 4 + 1] = faceRgb[f][1];
            dst[i * 4 + 2] = faceRgb[f][2];
            dst[i * 4 + 3] = 255;
        }
    }
    Texture *env = gfx->newCubemap(face, faces.data());

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.35f, 3.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.03f, 0.03f, 0.03f);
    cam->setEnvMap(env);
    cam->setEnvIntensity(1.5f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.25f, 1.f, 0.2f);
    sun->setColor(0.4f, 0.4f, 0.45f, 0.6f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float metalL = meanLumaRegion(gfx, w / 8, h / 3, w * 3 / 8, h * 2 / 3);
    const float dielL = meanLumaRegion(gfx, w * 5 / 8, h / 3, w * 7 / 8, h * 2 / 3);
    std::printf("RenderScenes.ibl metalL=%.3f dielL=%.3f\n", metalL, dielL);
    REQUIRE(metalL > dielL + 0.02f);

    win->close();
}

TEST_CASE("RenderScenes.unlit.ignoresDirectionalLight") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.f, 0.f, 0.f, 1.f));

    Material *unlit = gfx->newMaterial();
    unlit->setShadingModel("unlit");
    unlit->setAlbedoTexture(makeSolid(gfx, 180, 40, 40));
    unlit->setReceiveLight(false);

    auto *ent = Renderable3D::create();
    ent->setMesh(loadCube(gfx));
    ent->setMaterial(unlit);
    ent->setReceiveLight(false);

    auto *cam = Camera3D::createCamera();
    cam->setEye(1.8f, 1.4f, 2.6f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.2f, 0.2f, 0.2f);

    addHud();
    warmPresent(gfx, 2);
    const float before = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.3f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 4.f);
    sun->setEnabled(true);
    warmPresent(gfx, 2);
    const float after = luma(gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2));

    std::printf("RenderScenes.unlit before=%.3f after=%.3f\n", before, after);
    REQUIRE(before > 0.05f);
    // Unlit must not jump with a strong directional light.
    REQUIRE(std::fabs(after - before) < 0.08f);

    delete unlit;
    win->close();
}

TEST_CASE("RenderScenes.shadow.pointAndDirCoexist") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.04f, 0.04f, 0.05f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 170, 170, 175));
    ground->setPosition(0.f, -0.75f, 0.f);
    ground->setScale(3.2f, 0.08f, 3.2f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.95f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    auto *occ = Renderable3D::create();
    occ->setMesh(sphere);
    occ->setTexture(makeSolid(gfx, 40, 200, 40));
    occ->setPosition(0.f, 0.45f, 0.f);
    occ->setScale(0.5f, 0.5f, 0.5f);
    occ->setCastShadow(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(2.6f, 2.0f, 3.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.06f, 0.06f, 0.07f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.4f, 1.f, 0.2f);
    sun->setColor(1.f, 0.98f, 0.94f, 2.4f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setEnabled(true);

    auto *pt = Light3D::createLight("point");
    pt->setPosition(-1.2f, 1.0f, 1.0f);
    pt->setColor(0.4f, 0.55f, 1.f, 6.f);
    pt->setRadius(5.f);
    pt->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    // Green occluder still reads in the upper half (orientation) while ground is lit.
    REQUIRE(meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2) > 0.04f);
    REQUIRE(meanLumaRegion(gfx, w / 4, h / 2, (w * 3) / 4, h) > 0.05f);

    win->close();
}

TEST_CASE("RenderScenes.winding.insideClosedRoomVisible") {
    // Colored left/right walls viewed from inside — verifies upright orientation + visibility.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *cube = loadCube(gfx);
    auto *back = Renderable3D::create();
    back->setMesh(cube);
    back->setTexture(makeSolid(gfx, 180, 180, 185));
    back->setPosition(0.f, 1.f, -1.4f);
    back->setScale(2.2f, 2.0f, 0.08f);
    back->setRoughness(0.9f);

    auto *left = Renderable3D::create();
    left->setMesh(cube);
    left->setTexture(makeSolid(gfx, 210, 35, 35));
    left->setPosition(-1.1f, 1.f, 0.f);
    left->setScale(0.08f, 2.0f, 2.2f);
    left->setRoughness(0.9f);

    auto *right = Renderable3D::create();
    right->setMesh(cube);
    right->setTexture(makeSolid(gfx, 35, 190, 35));
    right->setPosition(1.1f, 1.f, 0.f);
    right->setScale(0.08f, 2.0f, 2.2f);
    right->setRoughness(0.9f);

    auto *floor = Renderable3D::create();
    floor->setMesh(cube);
    floor->setTexture(makeSolid(gfx, 140, 140, 145));
    floor->setPosition(0.f, 0.f, 0.f);
    floor->setScale(2.2f, 0.08f, 2.2f);
    floor->setRoughness(0.95f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.0f, 1.5f);
    cam->setTarget(0.f, 1.0f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.15f, 1.f, 0.1f);
    sun->setColor(1.f, 1.f, 1.f, 1.4f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const Color L = gfx->getPixel(w / 6, h / 2);
    const Color R = gfx->getPixel((w * 5) / 6, h / 2);
    std::printf("RenderScenes.room L=(%.2f,%.2f) R=(%.2f,%.2f)\n", L.r, L.g, R.r, R.g);
    REQUIRE(L.r > L.g + 0.05f);
    REQUIRE(R.g > R.r + 0.05f);
    // Floor toward bottom of frame (upright).
    const Color bot = gfx->getPixel(w / 2, (h * 5) / 6);
    REQUIRE(luma(bot) > 0.05f);

    win->close();
}

TEST_CASE("RenderScenes.gallery.cylinderAndSphereMaterials") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 400, 280);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.05f, 0.055f, 0.06f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    Mesh *cyl = gfx->newMeshCylinder(18, 1, true);

    const float metals[3] = {0.f, 0.5f, 1.f};
    const float roughs[3] = {0.2f, 0.5f, 0.85f};
    for (int i = 0; i < 3; ++i) {
        auto *s = Renderable3D::create();
        s->setMesh(sphere);
        s->setTexture(makeSolid(gfx, 200, 200, 205));
        s->setPosition(-1.2f + float(i) * 1.2f, 0.55f, 0.f);
        s->setScale(0.4f, 0.4f, 0.4f);
        s->setMetallic(metals[i]);
        s->setRoughness(roughs[i]);

        auto *c = Renderable3D::create();
        c->setMesh(cyl);
        c->setTexture(makeSolid(gfx, uint8_t(80 + i * 50), 160, uint8_t(200 - i * 40)));
        c->setPosition(-1.2f + float(i) * 1.2f, -0.35f, 0.f);
        c->setScale(0.28f, 0.35f, 0.28f);
        c->setMetallic(0.05f);
        c->setRoughness(0.7f);
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.2f, 4.2f);
    cam->setTarget(0.f, 0.1f, 0.f);
    cam->setAmbient(0.1f, 0.1f, 0.11f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.35f, 1.f, 0.4f);
    sun->setColor(1.f, 0.98f, 0.95f, 2.2f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const float mid = meanLumaRegion(gfx, 40, 40, 360, 240);
    REQUIRE(mid > 0.04f);
    // Top row (spheres) and bottom row (cylinders) both contribute.
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    REQUIRE(meanLumaRegion(gfx, w / 6, h / 8, (w * 5) / 6, h * 2 / 5) > 0.03f);
    REQUIRE(meanLumaRegion(gfx, w / 6, h * 3 / 5, (w * 5) / 6, (h * 7) / 8) > 0.03f);

    win->close();
}

TEST_CASE("RenderScenes.isolation.effectsDoNotFlipOrientation") {
    // Cycle shadow + gbuffer features; upright green/red markers must survive.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(18, 12);
    auto *top = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.05f, 0.f);
    top->setScale(0.5f, 0.5f, 0.5f);
    top->setCastShadow(true);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.05f, 0.f);
    bot->setScale(0.5f, 0.5f, 0.5f);
    bot->setCastShadow(true);

    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 90, 90, 95));
    ground->setPosition(0.f, -1.7f, 0.f);
    ground->setScale(2.5f, 0.08f, 2.5f);
    ground->setReceiveShadow(true);
    ground->setCastShadow(false);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.2f, 4.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.15f, 0.15f, 0.16f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.3f, 1.f, 0.25f);
    sun->setColor(1.f, 1.f, 1.f, 2.0f);
    sun->setEnabled(true);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);

    auto assertUpright = [&](const char *phase) {
        warmPresent(gfx, 2);
        const int w = gfx->getWidth();
        const int h = gfx->getHeight();
        const Color t = gfx->getPixel(w / 2, h / 5);
        const Color b = gfx->getPixel(w / 2, (h * 4) / 5);
        std::printf("RenderScenes.isolation[%s] topG=%.2f botR=%.2f\n", phase, t.g, b.r);
        REQUIRE(t.g > t.r + 0.04f);
        REQUIRE(b.r > b.g + 0.04f);
    };

    sun->setCastShadow(false);
    rc->disable("gbuffer");
    rc->compile();
    assertUpright("lit");

    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    assertUpright("shadow");

    rc->enable("gbuffer");
    rc->compile();
    assertUpright("gbuffer");

    rc->disable("gbuffer");
    sun->setCastShadow(false);
    rc->compile();
    assertUpright("reset");

    win->close();
}

TEST_CASE("RenderScenes.clustered.manyPointsBrightenWithoutFlip") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.015f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(14, 10);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 160, 160, 165));
    ground->setPosition(0.f, -0.6f, 0.f);
    ground->setScale(3.0f, 0.08f, 3.0f);
    ground->setRoughness(0.95f);

    auto *marker = Renderable3D::create();
    marker->setMesh(sphere);
    marker->setTexture(makeSolid(gfx, 40, 210, 40));
    marker->setPosition(0.f, 0.7f, 0.f);
    marker->setScale(0.45f, 0.45f, 0.45f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 1.5f, 4.0f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.02f, 0.02f, 0.025f);

    // >8 lights triggers clustered path in RenderSystem3D.
    for (int i = 0; i < 12; ++i) {
        const float a = float(i) / 12.f * 6.2831853f;
        auto *pt = Light3D::createLight("point");
        pt->setPosition(std::cos(a) * 1.4f, 0.55f, std::sin(a) * 1.4f);
        pt->setColor(0.7f + 0.3f * std::cos(a), 0.7f, 0.7f + 0.3f * std::sin(a), 3.5f);
        pt->setRadius(3.5f);
        pt->setEnabled(true);
    }

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float mid = meanLumaRegion(gfx, w / 4, h / 4, (w * 3) / 4, (h * 3) / 4);
    REQUIRE(mid > 0.04f);
    const Color top = gfx->getPixel(w / 2, h / 4);
    REQUIRE(top.g + 0.01f > top.r);  // green marker stays in upper half

    win->close();
}

TEST_CASE("RenderScenes.depth.nearOccludesFar") {
    // Red cube in front of a larger green cube — center pixel must read red.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *cube = loadCube(gfx);
    auto *farGreen = Renderable3D::create();
    farGreen->setMesh(cube);
    farGreen->setTexture(makeSolid(gfx, 40, 210, 40));
    farGreen->setPosition(0.f, 0.f, -0.6f);
    farGreen->setScale(1.4f, 1.4f, 0.2f);
    farGreen->setRoughness(0.9f);

    auto *nearRed = Renderable3D::create();
    nearRed->setMesh(cube);
    nearRed->setTexture(makeSolid(gfx, 220, 40, 40));
    nearRed->setPosition(0.f, 0.f, 0.4f);
    nearRed->setScale(0.55f, 0.55f, 0.55f);
    nearRed->setRoughness(0.9f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.15f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    std::printf("RenderScenes.depth mid=(%.2f,%.2f,%.2f)\n", mid.r, mid.g, mid.b);
    REQUIRE(mid.r > mid.g + 0.05f);
    REQUIRE(mid.r > 0.08f);

    win->close();
}

TEST_CASE("RenderScenes.scale.nonUniformKeepsUprightMarkers") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *top = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.15f, 0.f);
    top->setScale(0.85f, 0.5f, 0.7f);
    top->setRoughness(0.85f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.15f, 0.f);
    bot->setScale(0.5f, 0.85f, 0.7f);
    bot->setRoughness(0.85f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.25f, 0.25f, 0.25f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.25f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float topG = meanGreenRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
    const float topR = meanRedRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
    const float botR = meanRedRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
    const float botG = meanGreenRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
    REQUIRE(topG > topR + 0.03f);
    REQUIRE(botR > botG + 0.03f);

    win->close();
}

TEST_CASE("RenderScenes.checker.colorGridReadable") {
    // 2x2 tinted cubes — quadrant regions must keep their dominant channel.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.01f, 0.01f, 0.015f, 1.f));

    Mesh *cube = loadCube(gfx);
    struct Cell {
        float x, y;
        uint8_t r, g, b;
    };
    const Cell cells[4] = {
        {-1.05f, 1.05f, 220, 40, 40},
        {1.05f, 1.05f, 40, 220, 40},
        {-1.05f, -1.05f, 40, 40, 220},
        {1.05f, -1.05f, 220, 220, 40},
    };
    for (const auto &c : cells) {
        auto *e = Renderable3D::create();
        e->setMesh(cube);
        e->setTexture(makeSolid(gfx, c.r, c.g, c.b));
        e->setPosition(c.x, c.y, 0.f);
        e->setScale(0.7f, 0.7f, 0.7f);
        e->setRoughness(0.9f);
    }

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.35f, 0.35f, 0.35f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.25f);
    sun->setColor(1.f, 1.f, 1.f, 1.2f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float tlR = meanRedRegion(gfx, w / 8, h / 8, w * 3 / 8, h * 3 / 8);
    const float tlG = meanGreenRegion(gfx, w / 8, h / 8, w * 3 / 8, h * 3 / 8);
    const float trG = meanGreenRegion(gfx, w * 5 / 8, h / 8, w * 7 / 8, h * 3 / 8);
    const float trR = meanRedRegion(gfx, w * 5 / 8, h / 8, w * 7 / 8, h * 3 / 8);
    const float blB = meanLumaRegion(gfx, w / 8, h * 5 / 8, w * 3 / 8, h * 7 / 8);  // proxy
    (void)blB;
    double sumB = 0.0;
    int nB = 0;
    for (int y = h * 5 / 8; y < h * 7 / 8; y += 4) {
        for (int x = w / 8; x < w * 3 / 8; x += 4) {
            sumB += gfx->getPixel(x, y).b;
            ++nB;
        }
    }
    const float blBlue = nB ? float(sumB / nB) : 0.f;
    const float brR = meanRedRegion(gfx, w * 5 / 8, h * 5 / 8, w * 7 / 8, h * 7 / 8);
    const float brG = meanGreenRegion(gfx, w * 5 / 8, h * 5 / 8, w * 7 / 8, h * 7 / 8);
    std::printf("RenderScenes.checker tlR=%.2f trG=%.2f blB=%.2f brR=%.2f\n", tlR, trG, blBlue,
                brR);
    REQUIRE(tlR > tlG + 0.03f);
    REQUIRE(trG > trR + 0.03f);
    REQUIRE(blBlue > 0.08f);
    REQUIRE(brR > 0.08f);
    REQUIRE(brG > 0.08f);

    win->close();
}

TEST_CASE("RenderScenes.camera.moveKeepsUprightMarkers") {
    // Moving the active camera must not invert world-Y markers.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.02f, 0.02f, 0.03f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *top = Renderable3D::create();
    top->setMesh(sphere);
    top->setTexture(makeSolid(gfx, 40, 220, 40));
    top->setPosition(0.f, 1.0f, 0.f);
    top->setScale(0.5f, 0.5f, 0.5f);

    auto *bot = Renderable3D::create();
    bot->setMesh(sphere);
    bot->setTexture(makeSolid(gfx, 220, 40, 40));
    bot->setPosition(0.f, -1.0f, 0.f);
    bot->setScale(0.5f, 0.5f, 0.5f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 4.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.25f, 0.25f, 0.25f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.25f, 1.f, 0.3f);
    sun->setColor(1.f, 1.f, 1.f, 1.5f);
    sun->setEnabled(true);

    addHud();
    auto assertUpright = [&](const char *tag) {
        warmPresent(gfx, 2);
        const int w = gfx->getWidth();
        const int h = gfx->getHeight();
        const float topG = meanGreenRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
        const float topR = meanRedRegion(gfx, w / 3, 0, (w * 2) / 3, h / 3);
        const float botR = meanRedRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
        const float botG = meanGreenRegion(gfx, w / 3, (h * 2) / 3, (w * 2) / 3, h);
        std::printf("RenderScenes.cameraMove[%s] topG=%.2f botR=%.2f\n", tag, topG, botR);
        REQUIRE(topG > topR + 0.03f);
        REQUIRE(botR > botG + 0.03f);
    };
    assertUpright("near");

    cam->setEye(0.35f, 0.25f, 4.8f);
    cam->setTarget(0.f, 0.f, 0.f);
    assertUpright("orbit");

    win->close();
}

TEST_CASE("RenderScenes.shadow.receiveOffIgnoresUmbra") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 270);
    resetScene3D();
    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.06f, 1.f));

    Mesh *sphere = gfx->newMeshSphere(16, 10);
    auto *ground = Renderable3D::create();
    ground->setMesh(sphere);
    ground->setTexture(makeSolid(gfx, 180, 180, 185));
    ground->setPosition(0.f, -0.7f, 0.f);
    ground->setScale(3.2f, 0.08f, 3.2f);
    ground->setRoughness(0.95f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(false);  // should ignore caster umbra

    auto *occ = Renderable3D::create();
    occ->setMesh(sphere);
    occ->setTexture(makeSolid(gfx, 40, 200, 40));
    occ->setPosition(0.f, 0.55f, 0.f);
    occ->setScale(0.5f, 0.5f, 0.5f);
    occ->setCastShadow(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(2.8f, 2.2f, 3.6f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.08f, 0.08f, 0.09f);

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.35f, 1.f, 0.25f);
    sun->setColor(1.f, 0.98f, 0.94f, 2.8f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setEnabled(true);

    addHud();
    warmPresent(gfx, 3);

    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float mid = meanLumaRegion(gfx, w / 3, h / 2, (w * 2) / 3, (h * 5) / 6);
    // Ground stays reasonably bright despite an active shadow caster above it.
    REQUIRE(mid > 0.08f);
    REQUIRE(meanGreenRegion(gfx, w / 5, 0, (w * 4) / 5, h / 2) > 0.04f);

    win->close();
}

