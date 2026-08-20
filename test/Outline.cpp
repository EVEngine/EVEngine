#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>

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

Mesh *loadCube(Graphics *gfx) {
    Mesh *m = gfx->newMeshCube();
    REQUIRE(m != nullptr);
    return m;
}

void setupCubeScene(Graphics *gfx, Renderable3D *&cube) {
    resetScene3D();
    cube = Renderable3D::create();
    cube->setMesh(loadCube(gfx));
    cube->setTexture(makeSolid(gfx, 200, 190, 170));
    cube->setMetallic(0.f);
    cube->setRoughness(0.85f);

    auto *cam = Camera3D::createCamera();
    cam->setEye(1.6f, 1.2f, 2.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.3f, 0.3f, 0.3f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 30.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.2f, 1.f, 0.4f);
    sun->setColor(1.f, 1.f, 1.f, 1.6f);
    sun->setEnabled(true);

    addHud();
}

}  // namespace

TEST_CASE("outline.paramRoundTrip") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);
    std::unique_ptr<Outline> outline(gfx->newOutline());
    REQUIRE(outline->getShader() != nullptr);

    outline->setColor(0.1f, 0.2f, 0.3f);
    outline->setWidth(2.f);
    outline->setDepthThreshold(0.4f);
    outline->setDepthSensitivity(0.01f);
    outline->setNormalThreshold(0.5f);
    outline->setSoftness(0.3f);
    outline->setClip(0.1f, 50.f);

    CHECK(std::fabs(outline->getColorR() - 0.1f) < 1e-5f);
    CHECK(std::fabs(outline->getColorG() - 0.2f) < 1e-5f);
    CHECK(std::fabs(outline->getColorB() - 0.3f) < 1e-5f);
    CHECK(std::fabs(outline->getWidth() - 2.f) < 1e-5f);
    CHECK(std::fabs(outline->getDepthThreshold() - 0.4f) < 1e-5f);
    CHECK(std::fabs(outline->getDepthSensitivity() - 0.01f) < 1e-5f);
    CHECK(std::fabs(outline->getNormalThreshold() - 0.5f) < 1e-5f);
    CHECK(std::fabs(outline->getSoftness() - 0.3f) < 1e-5f);

    CHECK(outline->hasParam("width") == true);
    CHECK(outline->hasParam("depthThreshold") == true);
    CHECK(outline->hasParam("normalThreshold") == true);
    CHECK(outline->hasParam("softness") == true);
    CHECK(std::fabs(outline->getFloat("width") - 2.f) < 1e-5f);

    outline->setFloat("colorR", 0.9f);
    CHECK(std::fabs(outline->getFloat("colorR") - 0.9f) < 1e-5f);

    win->close();
}

TEST_CASE("outline.renderControlFeatureToggle") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("ao");
    CHECK(rc->supports("outline") == true);
    CHECK(rc->isEnabled("outline") == false);

    rc->enable("outline");
    CHECK(rc->isEnabled("outline") == true);
    // Enabling outline implies the gbuffer depth/normal fill.
    CHECK(rc->isEnabled("gbuffer") == true);

    // Turning the gbuffer off must drop the dependent outline feature.
    rc->disable("gbuffer");
    CHECK(rc->isEnabled("outline") == false);

    rc->enable("gbuffer");
    rc->enable("outline");
    rc->compile();

    Renderable3D *cube = nullptr;
    setupCubeScene(gfx, cube);
    warmPresent(gfx, 3);

    GBuffer *gb = rc->getGBuffer();
    REQUIRE(gb != nullptr);
    CHECK(gb->isValid());
    CHECK(gb->getHwDepthTexture() != nullptr);
    CHECK(gb->getNormalTexture() != nullptr);

    win->close();
}

TEST_CASE("outline.newMeshCubeAndPipelineConfig") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);

    Mesh *cube = gfx->newMeshCube(1.f);
    REQUIRE(cube != nullptr);

    // The auto-applied (render-feature) outline must be reachable and configurable.
    Outline *pipeline = gfx->pipelineOutline();
    REQUIRE(pipeline != nullptr);
    pipeline->setColor(0.2f, 0.1f, 0.05f);
    pipeline->setWidth(2.5f);
    pipeline->setClip(0.05f, 40.f);
    CHECK(std::fabs(pipeline->getWidth() - 2.5f) < 1e-5f);
    CHECK(std::fabs(pipeline->getColorR() - 0.2f) < 1e-5f);

    RenderControl *rc = gfx->getRenderControl();
    rc->disable("ao");
    rc->enable("outline");
    rc->compile();

    Renderable3D *cubeEnt = Renderable3D::create();
    cubeEnt->setMesh(cube);
    cubeEnt->setPosition(0.f, 0.f, 0.f);
    cubeEnt->setRoughness(0.7f);
    auto *cam = Camera3D::createCamera();
    cam->setEye(2.6f, 1.9f, 3.4f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->data()->nearZ = 0.05f;
    cam->data()->farZ = 40.f;
    addHud();
    warmPresent(gfx, 2);
    CHECK(rc->getGBuffer()->isValid());

    win->close();
}

TEST_CASE("outline.previewCube") {
    // Visual check: rotating cube with a screen-space ink outline. Holds the
    // window open for a few seconds so it can be inspected interactively.
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 420, 360);
    gfx->setBackgroundColor(Color(0.12f, 0.13f, 0.16f, 1.f));

    RenderControl *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("ao");
    rc->enable("outline");
    rc->compile();

    Renderable3D *cube = nullptr;
    setupCubeScene(gfx, cube);

    // Slightly thicker, softer dark outline so the silhouette reads clearly.
    Outline *outline = gfx->pipelineOutline();
    REQUIRE(outline != nullptr);
    outline->setWidth(1.5f);
    outline->setColor(0.04f, 0.03f, 0.05f);
    outline->setNormalThreshold(0.5f);
    outline->setSoftness(0.2f);

    const int frames = 150;  // ~5s at 30 fps
    for (int i = 0; i < frames; ++i) {
        cube->setRotation(0.15f * float(i), float(i) * 0.02f, 0.f);
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) goto done;
        }
        SDL_Delay(33);
    }
done:
    win->close();
}
