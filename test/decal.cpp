#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "decal/DecalManager.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
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
