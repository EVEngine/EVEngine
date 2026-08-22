#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "decal/DecalManager.h"
#include "decal/Decal.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <vector>

using namespace eve::decal;
using namespace eve::graphics;

TEST_CASE("decal.managerProjectRemove") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    CHECK(mgr.count() == 0);

    const int id = mgr.project(1.f, 2.f, 3.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f,
                               false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(mgr.count() == 1);
    CHECK(mgr.instances()[0].size == 0.5f);
    CHECK(mgr.instances()[0].ny == 1.f);

    CHECK(mgr.remove(id));
    CHECK(mgr.count() == 0);
    CHECK(!mgr.remove(id));
}

TEST_CASE("decal.managerLifetimeFadeExpiry") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0, 0.1f, 1.f,
                0.5f, 0.f, 0.f, 0.f, 0.f);
    mgr.update(0.5f);
    CHECK(mgr.count() == 1);
    CHECK(mgr.instances()[0].age >= 0.5f);
    // age = 0.5 + 1.2 = 1.7 >= lifetime(1) + fadeOut(0.5) -> expired.
    mgr.update(1.2f);
    CHECK(mgr.count() == 0);
}

TEST_CASE("decal.managerPersistentNeverExpires") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0, 0.f, 0.f, 0.f,
                0.f, 0.f, 0.f, 0.f);
    mgr.update(1000.f);
    CHECK(mgr.count() == 1);
}

TEST_CASE("decal.managerKindQuotaEvictsOldest") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.setLimit("blood", 2);
    const int first =
        mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    const int second =
        mgr.project(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    const int third =
        mgr.project(2.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.count() == 2);
    // The oldest ("first") was evicted to make room for "third".
    CHECK(!mgr.remove(first));
    CHECK(mgr.remove(second));
    CHECK(mgr.remove(third));
}

TEST_CASE("decal.managerOtherKindNotEvicted") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    mgr.setLimit("blood", 1);
    mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false, 0, 0.f, 0.f,
                0.f, 0.f, 0.f, 0.f, 0.f);
    const int dirt =
        mgr.project(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "dirt", 0.5f, 0.15f, false, 0, 0.f,
                    0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.count() == 2);
    CHECK(mgr.remove(dirt));
}

TEST_CASE("decal.managerAtlasBlendSetters") {
    auto &mgr = DecalManager::inst();
    mgr.clearAll();
    const int id = mgr.project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "", 0.5f, 0.15f, false, 0,
                               0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(mgr.setUvRect(id, 0.25f, 0.25f, 0.5f, 0.5f));
    CHECK(mgr.instances()[0].uvRect[0] == 0.25f);
    CHECK(mgr.instances()[0].uvRect[3] == 0.5f);

    CHECK(mgr.setBlend(id, "add"));
    CHECK(mgr.instances()[0].blendMode == 1);
    CHECK(mgr.setBlend(id, "over"));
    CHECK(mgr.instances()[0].blendMode == 0);
    CHECK(!mgr.setBlend(id, "bogus"));  // unknown mode rejected

    CHECK(mgr.setTextures(id, nullptr, nullptr));
    CHECK(!mgr.setUvRect(99999, 0.f, 0.f, 1.f, 1.f));  // unknown id
}

TEST_CASE("decal.capabilityProvidedToConsumers") {
    // Instantiate the decal module (registers the drawer + capability).
    eve::ModuleManager::requireInstance<eve::decal::Decal>("Decal");
    auto *q = eve::cap::query<eve::IDecalQuery>();
    REQUIRE(q != nullptr);
    q->clearAll();
    CHECK(q->count() == 0);
    q->setLimit("blood", 1);
    const int id = q->project(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, nullptr, "blood", 0.5f, 0.15f, false,
                              0, 0.f, 0.f, 0.f);
    CHECK(id > 0);
    CHECK(q->count() == 1);
    CHECK(q->remove(id));
    CHECK(q->count() == 0);
}

TEST_CASE("decal.renderControlFeatureGatesPass") {
    eve::graphics::RenderControl rc;
    CHECK(rc.supports("decal"));
    CHECK(!rc.isEnabled("decal"));

    rc.enable("decal");
    CHECK(rc.isEnabled("decal"));
    CHECK(rc.isEnabled("gbuffer"));  // decal implies the G-buffer (depth/normal inputs)

    rc.compile();
    CHECK(rc.hasPass("decal"));
    // Order: shadow -> gbuffer -> decal -> forward -> hair.
    CHECK(rc.getPassName(1) == "gbuffer");
    CHECK(rc.getPassName(2) == "decal");
    CHECK(rc.getPassName(3) == "forward");

    rc.disable("gbuffer");
    CHECK(!rc.isEnabled("decal"));  // dropping the G-buffer drops decals too
}

namespace {

eve::graphics::Texture *makeSolidTex(eve::graphics::Graphics *gfx, uint8_t r, uint8_t g,
                                     uint8_t b) {
    const int size = 16;
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Mesh *makePlane(eve::graphics::Graphics *gfx, float size) {
    const float h = size * 0.5f;
    const std::vector<float> pos = {-h, -h, 0.f, h, -h, 0.f, h, h, 0.f, -h, h, 0.f};
    const std::vector<float> nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
                                    0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const std::vector<float> uv = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), 4, idx.data(), 6);
}

}  // namespace

// End-to-end smoke: a red decal projected onto a white plane must change the
// screen-center pixel toward red (the decal layer is written between the
// G-buffer and forward passes, and mesh3d.frag blends it before lighting).
TEST_CASE("decal.gpuProjectBlendsIntoForward") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 255, 255, 255);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    // Enable the decal pass and feed it from DecalManager through a drawer
    // (same path the Decal script module registers at startup).
    static bool sDrawerRegistered = false;
    if (!sDrawerRegistered) {
        sDrawerRegistered = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    DecalManager::inst().clearAll();
    DecalManager::inst().project(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, decalTex, "smoke", 1.2f, 0.1f,
                                 false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }

    const int W = gfx->getWidth();
    const int H = gfx->getHeight();
    const Color center = gfx->getPixel(W / 2, H / 2);
    const Color corner = gfx->getPixel(4, 4);
    // Center (inside the 1.2m decal on a 2m plane) must be red-tinted; the
    // corner stays near-white.
    CHECK_GT(center.r - center.g, 0.1f);
    CHECK_GT(center.r - center.b, 0.1f);
    CHECK_LT(corner.r - corner.g, 0.1f);

    DecalManager::inst().clearAll();
    win->close();
}

// Emissive decals use the additive layer pipeline: a red glow on a dark plane
// must visibly brighten the screen center vs. the unlit corner.
TEST_CASE("decal.gpuEmissiveAdditiveGlow") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);

    auto *cam = Camera3D::createCamera();
    cam->data()->eyeZ = 2.6f;

    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->meshRenderer()->texture = makeSolidTex(gfx, 60, 60, 60);  // dark base
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    static bool sGlowDrawer = false;
    if (!sGlowDrawer) {
        sGlowDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    auto *glowColor = makeSolidTex(gfx, 200, 20, 20);  // red glow color
    auto *glowParams = makeSolidTex(gfx, 0, 0, 255);   // B = emissive intensity
    DecalManager::inst().clearAll();
    const int id = DecalManager::inst().project(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, glowColor, "glow",
                                                1.2f, 0.1f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                0.f, 0.f);
    CHECK(id > 0);
    CHECK(DecalManager::inst().setTextures(id, nullptr, glowParams));
    CHECK(DecalManager::inst().setBlend(id, "add"));
    CHECK(DecalManager::inst().setStrength(id, 0.f, 0.f, 0.f, 1.f));

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }

    const int W = gfx->getWidth();
    const int H = gfx->getHeight();
    const Color center = gfx->getPixel(W / 2, H / 2);
    const Color corner = gfx->getPixel(4, 4);
    CHECK_GT(center.r, corner.r + 0.2f);  // emissive red lifts the center
    CHECK_GT(center.r - center.g, 0.1f);  // ... toward red, not white

    DecalManager::inst().clearAll();
    win->close();
}

// Headless verification of the decal pass itself: queue a G-buffer plane + one
// decal, then read the DecalLayer back to CPU. Works without a swapchain, so it
// also runs when the interactive GPU/surface is busy.
TEST_CASE("decal.gpuHeadlessProjectionReadback") {
    eve::graphics::Graphics *gfx = nullptr;
    openHeadlessGfx(gfx, 160, 120);

    auto *mesh = makePlane(gfx, 2.f);
    REQUIRE(mesh != nullptr);
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    gfx->beginGBufferPass(160, 120);
    gfx->drawMeshGBuffer(mesh, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f,
                         makeSolidTex(gfx, 255, 255, 255));
    gfx->endGBufferPass();

    auto *decalTex = makeSolidTex(gfx, 200, 20, 20);
    gfx->beginDecalPass(160, 120);
    gfx->setDecalCamera(glm::mat4(1.f), 0.1f, 100.f);
    // Identity box at the origin covers the screen center.
    gfx->drawDecal(glm::mat4(1.f), decalTex, nullptr, nullptr, nullptr, 1.f, 0.f, 0.f, 0.f, 0.f);
    gfx->endDecalPass();

    auto *img = gfx->readDecalLayerToImageData("albedo");
    REQUIRE(img != nullptr);
    CHECK_EQ(img->getWidth(), 160);
    CHECK_EQ(img->getHeight(), 120);

    const uint8_t *px = static_cast<const uint8_t *>(img->getData());
    auto at = [&](int x, int y) { return px + (size_t(y) * 160u + size_t(x)) * 4u; };
    const uint8_t *center = at(80, 60);
    CHECK_GT(center[0], 150);  // red decal written at the center
    CHECK_LT(center[1], 80);
    const uint8_t *corner = at(4, 4);
    CHECK_LT(corner[0], 20);  // outside the unit box -> layer stays cleared
    delete img;
}
