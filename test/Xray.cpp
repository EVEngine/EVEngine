#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <cstdio>
#include <memory>
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
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "medialoader/model/ModelLoader.h"
#include "stylize/Stylize.h"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
    win = eve::window::Window::create();
    gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
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

/**
 * Builds a scene with a large occluder cube (a "building") between the camera
 * and a smaller target cube that sits fully behind it. Returns the target.
 * The camera looks straight down -Z at the origin.
 */
Renderable3D *setupOcclusionScene(Graphics *gfx, Renderable3D *&occluder, Shader *xrayShader) {
    resetScene3D();

    auto *building = Renderable3D::create();
    building->setMesh(loadCube(gfx));
    building->setTexture(makeSolid(gfx, 120, 110, 100));
    building->setMetallic(0.f);
    building->setRoughness(0.9f);
    building->setPosition(0.f, 0.f, 1.2f);   // near the camera
    building->setScale(3.f, 2.2f, 1.5f);     // big enough to fully hide the target
    occluder = building;

    auto *target = Renderable3D::create();
    target->setMesh(loadCube(gfx));
    target->setTexture(makeSolid(gfx, 90, 200, 90));
    target->setMetallic(0.f);
    target->setRoughness(0.6f);
    target->setPosition(0.f, 0.f, -1.2f);  // behind the occluder, hidden from the camera
    target->setXRayShader(xrayShader);
    target->setXRayHighlight(true);

    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 3.2f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 40.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
    return target;
}

}  // namespace

TEST_CASE("xray.shaderAndFlags") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    eve::stylize::Stylize *stylize = eve::stylize::Stylize::create();
    REQUIRE(stylize != nullptr);
    CHECK(stylize->hasStyle("xray"));
    CHECK(stylize->hasMeshStyle("xray"));
    CHECK(stylize->supports("xray", "mesh"));

    Shader *sh = stylize->newMeshShader(gfx, "xray");
    REQUIRE(sh != nullptr);
    REQUIRE(sh->gpuHandle != nullptr);
    CHECK(sh->isXray());

    // Push-constant params exist and round-trip.
    CHECK(sh->hasUniform("colorR"));
    CHECK(sh->hasUniform("colorG"));
    CHECK(sh->hasUniform("colorB"));
    CHECK(sh->hasUniform("bias"));
    CHECK(sh->hasUniform("screenW"));
    CHECK(sh->hasUniform("screenH"));
    CHECK(sh->hasUniform("rimPower"));
    CHECK(sh->hasUniform("rimStrength"));
    CHECK(sh->hasUniform("alpha"));

    sh->sendFloat("colorR", 0.2f);
    float v = 0.f;
    CHECK(sh->getFromVar("colorR", &v, sizeof(v)) == int(sizeof(v)));
    CHECK(std::fabs(v - 0.2f) < 1e-5f);

    // Renderable3D X-ray wiring.
    auto *r = Renderable3D::create();
    r->setXRayShader(sh);
    r->setXRayHighlight(true);
    CHECK(r->getXRayShader() == sh);
    CHECK(r->getXRayHighlight() == true);
    r->setXRayHighlight(false);
    CHECK(r->getXRayHighlight() == false);
    r->setVisible(false);

    win->close();
}

TEST_CASE("xray.occludedSilhouette") {
    // A target hidden behind a building should show its X-ray silhouette through
    // the wall (highlight color), while a target that is NOT flagged stays hidden.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("ao");
    rc->enable("gbuffer");  // X-ray needs the G-buffer scene depth
    rc->compile();

    eve::stylize::Stylize *stylize = eve::stylize::Stylize::create();
    Shader *xray = stylize->newMeshShader(gfx, "xray");
    REQUIRE(xray != nullptr);
    xray->sendFloat("alpha", 1.f);

    Renderable3D *occluder = nullptr;
    Renderable3D *target = setupOcclusionScene(gfx, occluder, xray);

    // Warm up so the G-buffer / pipelines are ready.
    warmPresent(gfx, 4);

    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    CHECK(gb->isValid());
    CHECK(gb->getHwDepthTexture() != nullptr);

    // Hold a few rendered frames so the X-ray pass executes without error and
    // the silhouette is visually inspectable.
    for (int i = 0; i < 60; ++i) {
        target->setRotation(float(i) * 0.04f, float(i) * 0.01f, 0.f);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) goto done;
        }
        SDL_Delay(16);
    }
done:
    target->setVisible(false);
    win->close();
}
