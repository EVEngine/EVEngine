#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "voxel/Chunk.h"
#include "voxel/FaceDir.h"
#include "voxel/Voxel.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelWorld.h"
#include "window/Window.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>

using namespace eve::graphics;
using namespace eve::voxel;

static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

static void openGfxWindow(eve::window::Window *&win, Graphics *&gfx, int w = 320, int h = 240) {
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

static Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

static void tinyHud(Graphics *gfx) {
    (void)gfx;
    // Keep 2D present path consistent with other 3D tests.
    auto *hud = Renderable2D::create();
    hud->transform()->x = 0;
    hud->transform()->y = 0;
    hud->sprite()->width = 2;
    hud->sprite()->height = 2;
    hud->sprite()->r = 0.f;
    hud->sprite()->g = 0.f;
    hud->sprite()->b = 0.f;
    hud->sprite()->a = 0.f;
    hud->sprite()->visible = true;
}

static void hideLeftover3D() {
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
}

static void renderVoxelFrame(Graphics *gfx, VoxelWorld *world, Texture *atlas, int tilesPerRow,
                             const glm::vec3 &eye, const glm::vec3 &target, float viewRange,
                             bool faceCull) {
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.f), aspect, 0.1f, 500.f);
    const glm::mat4 vp = proj * view;

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, viewRange, faceCull);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (!gfx->had3DThisFrame()) return;
    gfx->setMesh3DViewProj(vp);
    gfx->setMesh3DCameraPos(eye);
    world->drawVisible(gfx, atlas, tilesPerRow);
    RenderSystem::render(*gfx);  // closes / presents via 2D path
}

// NOTE: Graphics is a process-wide singleton — reuse one window for these cases.

TEST_CASE("voxel.render.smokeDrawVisibleProducesPixels") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Small solid block near origin so a +Z camera sees it.
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    CHECK(world->getOrCreateChunk(0, 0, 0)->totalRectCount() > 0);

    Texture *atlas = makeSolid(gfx, 230, 60, 50);
    REQUIRE(atlas != nullptr);

    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);
    for (int i = 0; i < 3; ++i)
        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Background is dark slate; a lit red face should raise luma / red channel.
    CHECK(mid.r > 0.15f);
    CHECK(luma(mid) > 0.08f);
    CHECK(luma(mid) < 0.98f);
}

TEST_CASE("voxel.render.emptyInstancesNoCrash") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 view = glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.1f, 0.1f, 0.12f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        // count == 0 must be a no-op.
        gfx->drawVoxelFaceInstances(nullptr, 0, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
        uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 0).bits;
        // Also tolerate a single instance draw.
        gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
    }
    RenderSystem::render(*gfx);
    Color c = gfx->getPixel(1, 1);
    CHECK(c.a >= 0.f);
}

TEST_CASE("voxel.render.invalidFaceDirThrows") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    const glm::mat4 vp(1.f);
    gfx->begin3DFrame();
    if (!gfx->had3DThisFrame()) {
        RenderSystem::render(*gfx);
        return;
    }
    gfx->setMesh3DViewProj(vp);
    uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 0).bits;
    bool threw = false;
    try {
        gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "sideways", nullptr, 1);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
    // Recover present path.
    RenderSystem::render(*gfx);
}

TEST_CASE("voxel.render.atlasTexIndexTintVisible") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Texture id 1 (air is 0); solid red 1×1 atlas.
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();

    Texture *red = makeSolid(gfx, 240, 40, 40);
    const glm::vec3 eye(0.5f, 0.5f, 6.f);
    const glm::vec3 target(0.5f, 0.5f, 0.5f);
    for (int i = 0; i < 3; ++i)
        renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 100.f, true);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > mid.g);
    CHECK(mid.r > mid.b);
    CHECK(mid.r > 0.12f);
}

TEST_CASE("voxel.render.faceCullChangesCoverage") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 200, 200, 210);

    // Camera on +X; with face cull, back faces dropped — still should see the +X wall.
    const glm::vec3 eye(20.f, 4.f, 4.f);
    const glm::vec3 target(4.f, 4.f, 4.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleBatchCount() >= 1);
    bool sawPosX = false, sawNegX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i) {
        if (world->getVisibleBatch(i).dir == FaceDir::PosX) sawPosX = true;
        if (world->getVisibleBatch(i).dir == FaceDir::NegX) sawNegX = true;
    }
    CHECK(sawPosX);
    CHECK(!sawNegX);

    Color withCull = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, false);
    CHECK(world->getVisibleRectCount() >= 6);
    Color noCull = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Both frames should show geometry (not pure background).
    CHECK(luma(withCull) > 0.06f);
    CHECK(luma(noCull) > 0.06f);
}

TEST_CASE("voxel.render.outOfRangeDrawsNothingBright") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 255, 255, 255);

    const glm::vec3 eye(16.f, 16.f, 80.f);
    const glm::vec3 target(16.f, 16.f, 16.f);
    // Tiny range → selectVisible yields no batches.
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 1.f, true);
    CHECK_EQ(world->getVisibleBatchCount(), 0);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Should be near the dark clear color.
    CHECK(luma(mid) < 0.2f);
}

TEST_CASE("voxel.render.multiBatchSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Two separated pillars → multiple face batches.
    for (int y = 0; y < 6; ++y) {
        world->setVoxel(2, y, 2, 1);
        world->setVoxel(10, y, 10, 1);
    }
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 180, 170, 90);

    const glm::vec3 eye(6.f, 8.f, 22.f);
    const glm::vec3 target(6.f, 3.f, 6.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleBatchCount() >= 2);
    CHECK(world->getVisibleRectCount() >= 2);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.drawVisibleMatchesManualBatches") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 90, 140, 220);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 1.5f, 10.f);
    const glm::vec3 target(1.5f, 1.5f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp = proj * view;

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, true);
    const int batches = world->getVisibleBatchCount();
    REQUIRE(batches > 0);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->setMesh3DCameraPos(eye);
        for (int i = 0; i < batches; ++i) {
            const DrawBatch &b = world->getVisibleBatch(i);
            gfx->drawVoxelFaceInstances(b.packed, b.count, b.chunk->originX(), b.chunk->originY(),
                                        b.chunk->originZ(), faceDirName(b.dir), atlas, 1);
        }
    }
    RenderSystem::render(*gfx);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
}
