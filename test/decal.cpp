#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "RenderImageAudit.h"
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

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
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

namespace {

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Procedural decal art so the gallery needs no external assets.
eve::graphics::Texture *makeChecker(eve::graphics::Graphics *gfx, int size, int cells) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool dark = ((x * cells / size) + (y * cells / size)) % 2 == 0;
            const uint8_t v = dark ? 150 : 205;
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = v;
            px[i + 1] = v;
            px[i + 2] = v;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeBloodSplat(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            const float dx = u - 0.5f;
            const float dy = v - 0.5f;
            const float ang = std::atan2(dy, dx);
            const float r = std::sqrt(dx * dx + dy * dy) * 2.f;
            // Irregular splat edge: three-lobe wobble + inner core.
            const float wob = 0.72f + 0.22f * std::sin(3.f * ang + 1.3f) +
                              0.10f * std::sin(5.f * ang + 4.2f);
            const float a = 1.f - smoothstep(0.30f, wob, r);
            const float core = 1.f - smoothstep(0.0f, 0.34f, r);
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = 130;
            px[i + 1] = 14;
            px[i + 2] = 24;
            px[i + 3] = uint8_t(std::clamp((a * 0.55f + core * 0.45f) * 255.f, 0.f, 255.f));
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeDirt(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    uint32_t seed = 12345u;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const float n = float(seed % 10000u) / 10000.f;
            const float a = n < 0.62f ? 0.25f + 0.35f * n : 0.f;
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = 118;
            px[i + 1] = 102;
            px[i + 2] = 82;
            px[i + 3] = uint8_t(a * 255.f);
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeDentNormal(eve::graphics::Graphics *gfx, int size) {
    // Concave dent: tangent-space normal tilts inward toward the center.
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            const float dx = (u - 0.5f) * 2.f;
            const float dy = (v - 0.5f) * 2.f;
            const float r = std::sqrt(dx * dx + dy * dy);
            float tilt = 1.6f * std::exp(-r * r * 2.6f);
            // Raised rim just outside the crater.
            tilt -= 0.8f * std::exp(-(r - 0.55f) * (r - 0.55f) * 60.f);
            glm::vec3 n = glm::normalize(glm::vec3(-dx * tilt, -dy * tilt, 1.f));
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = uint8_t((n.x * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((n.y * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((n.z * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeDentAlbedo(eve::graphics::Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (float(x) + 0.5f) / float(size);
            const float v = (float(y) + 0.5f) / float(size);
            const float dx = (u - 0.5f) * 2.f;
            const float dy = (v - 0.5f) * 2.f;
            const float r = std::sqrt(dx * dx + dy * dy);
            const float crater = 1.f - smoothstep(0.f, 0.55f, r);
            const float ring =
                smoothstep(0.42f, 0.55f, r) * (1.f - smoothstep(0.55f, 0.68f, r));
            const uint8_t val =
                uint8_t(std::clamp((68.f + crater * -28.f + ring * 42.f), 0.f, 255.f));
            size_t i = (size_t(y) * size_t(size) + size_t(x)) * 4u;
            px[i] = val;
            px[i + 1] = uint8_t(val * 0.93f);
            px[i + 2] = uint8_t(val * 0.88f);
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data());
}

eve::graphics::Texture *makeRoughParams(eve::graphics::Graphics *gfx, int size, float rough) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4u);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = uint8_t(rough * 255.f);  // R = target roughness
        px[i + 1] = 0;                    // G = metallic
        px[i + 2] = 0;                    // B = emissive
        px[i + 3] = 255;
    }
    return gfx->newTexture(size, size, px.data());
}

}  // namespace

// Gallery render: blood / dirt / dent decals projected onto a checker floor,
// saved as PNG for visual review (build/<plat>/test/out/decal/).
TEST_CASE("decal.renderGalleryPng") {
    eve::window::Window *win = nullptr;
    eve::graphics::Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 480, 360);

    auto *mesh = makePlane(gfx, 3.f);
    REQUIRE(mesh != nullptr);
    auto *cam = Camera3D::createCamera();
    cam->data()->eyeY = 1.9f;
    cam->data()->eyeZ = 2.35f;
    auto *ent = Renderable3D::create();
    ent->meshRenderer()->mesh = mesh;
    ent->transform()->pitch = -3.14159265f * 0.5f;  // lay the plane flat (normal +Y)
    ent->meshRenderer()->texture = makeChecker(gfx, 64, 8);
    RenderSystem3D::setDirectionalLight(0.4f, 1.f, 0.3f, 1.f, 1.f, 1.f);

    static bool sGalleryDrawer = false;
    if (!sGalleryDrawer) {
        sGalleryDrawer = true;
        RenderSystem3D::addDecalExtraDrawer(
            [](eve::graphics::Graphics &g, const Camera3D::Data &camData,
               const glm::mat4 &viewProj, float aspect) {
                DecalManager::inst().drawAll(g, camData, viewProj, aspect);
            });
    }
    gfx->getRenderControl()->enable("decal");
    gfx->getRenderControl()->compile();

    DecalManager::inst().clearAll();
    auto &mgr = DecalManager::inst();
    // Blood: dark red splat, wet gloss (roughness down via params R).
    const int blood = mgr.project(-0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f,
                                  makeBloodSplat(gfx, 128), "blood", 1.05f, 0.12f, true, 7, 0.f,
                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(blood, nullptr, makeRoughParams(gfx, 16, 0.28f)));
    CHECK(mgr.setStrength(blood, 0.f, 1.f, 0.f, 0.f));
    // Dirt: gray-brown speckle, rough + slight normal wobble.
    const int dirt = mgr.project(0.f, 0.01f, 0.1f, 0.f, 1.f, 0.f, makeDirt(gfx, 128), "dirt",
                                 1.3f, 0.12f, true, 23, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    CHECK(mgr.setTextures(dirt, nullptr, makeRoughParams(gfx, 16, 0.85f)));
    CHECK(mgr.setStrength(dirt, 0.35f, 1.f, 0.f, 0.f));
    // Dent: concave normal map + crater albedo (real indentation shading).
    const int dent = mgr.project(0.85f, 0.01f, 0.15f, 0.f, 1.f, 0.f, makeDentAlbedo(gfx, 128),
                                 "dent", 1.05f, 0.12f, false, 0, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                 0.f);
    CHECK(mgr.setTextures(dent, makeDentNormal(gfx, 128), makeRoughParams(gfx, 16, 0.6f)));
    CHECK(mgr.setStrength(dent, 0.9f, 0.6f, 0.f, 0.f));

    gfx->setScreenReadbackEnabled(true);
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
    }
    eve::image::ImageData *snap = gfx->newImageData();
    REQUIRE(snap != nullptr);
    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out/decal";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    REQUIRE(saveImagePng(*snap, outDir + "/decal_gallery.png"));
    std::printf("decal gallery saved: %s/decal_gallery.png\n", outDir.c_str());
    delete snap;

    DecalManager::inst().clearAll();
    win->close();
}
