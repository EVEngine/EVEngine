#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
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
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


[[maybe_unused]] void resetScene3D() {
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

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] Mesh *makeGroundPlane(Graphics *gfx) { return gfx->newMeshSphere(16, 8); }

[[maybe_unused]] void addHud() {
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
}

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

[[maybe_unused]] void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

/** Live-render the shadow scene to the window for ~1s. */
[[maybe_unused]] void previewScene(Graphics *gfx, int ms = 1000) {
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

/** Max (a - b) over a coarse pixel grid. Used because Vulkan FB Y / RH_ZO place
 *  this scene's umbra off the naive "center + below" sample point. */
[[maybe_unused]] float maxLumaDelta(Graphics *gfx, const std::vector<float> &a, const std::vector<float> &b,
                   int step = 4) {
    float best = -1.f;
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const size_t i = size_t(y * w + x);
            best = std::max(best, a[i] - b[i]);
        }
    }
    return best;
}

[[maybe_unused]] std::vector<float> captureLumaGrid(Graphics *gfx, int step = 4) {
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    std::vector<float> out(size_t(w * h), 0.f);
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            out[size_t(y * w + x)] = luma(gfx->getPixel(x, y));
        }
    }
    return out;
}

[[maybe_unused]] void setupShadowScene(Graphics *gfx, Renderable3D *&ground, Light3D *&sun, bool groundReceive,
                      Renderable3D **caster = nullptr) {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.05f, 0.05f, 0.05f);

    ground = Renderable3D::create();
    ground->setMesh(makeGroundPlane(gfx));
    ground->setTexture(makeSolid(gfx, 220, 220, 220));
    ground->setPosition(0.f, -1.2f, 0.f);
    ground->setScale(4.f, 0.15f, 4.f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.85f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(groundReceive);

    auto *box = Renderable3D::create();
    box->setMesh(gfx->newMeshSphere(20, 12));
    box->setTexture(makeSolid(gfx, 200, 200, 200));
    box->setPosition(0.f, 0.35f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.f);
    box->setRoughness(0.6f);
    box->setCastShadow(true);
    box->setReceiveShadow(false);
    if (caster) *caster = box;

    sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setShadowBias(0.003f);

    addHud();
}

}  // namespace


TEST_CASE("Shadow.directionalCSM.clampsFarAndSplitsAreViewZ") {
    const glm::vec3 dir(0.3f, 1.f, 0.2f);
    const glm::vec3 eye(0.f, 2.f, 8.f);
    const glm::vec3 target(0.f, 1.f, 0.f);
    const glm::vec3 up(0.f, 1.f, 0.f);
    ShadowUpload u = buildDirectionalCSM(dir, eye, target, up, 1.047f, 16.f / 9.f, 0.1f, 500.f,
                                         0.002f, 1.f);
    REQUIRE(u.active);
    // Far used for splits must be the practical shadow distance, not camera far=500.
    CHECK(u.ubo.splits.x > 0.1f);
    CHECK(u.ubo.splits.x < u.ubo.splits.y);
    CHECK(u.ubo.splits.y <= u.ubo.splits.z + 1e-4f);
    CHECK(u.ubo.splits.z <= ShadowConfig::kMaxDistance + 1e-3f);
    CHECK(u.ubo.splits.z < 100.f);
    CHECK(u.ubo.bias.x >= 0.002f);
}
