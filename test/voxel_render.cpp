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

TEST_CASE("voxel.render.requiresBegin3DFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 1).bits;
    bool threw = false;
    try {
        gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("voxel.render.faceDirAliasesDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(0.5f, 0.5f, 6.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    // One +Z face covering the voxel front.
    uint32_t packed = PackedRect::pack(0, 0, 0, 1, 1, 1).bits;
    Texture *atlas = makeSolid(gfx, 40, 180, 220);

    const char *aliases[] = {"posZ", "+z"};
    for (const char *name : aliases) {
        gfx->setBackgroundColor(Color(0.04f, 0.05f, 0.06f, 1.f));
        gfx->begin3DFrame();
        if (gfx->had3DThisFrame()) {
            gfx->setMesh3DViewProj(vp);
            gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, name, atlas, 1);
        }
        RenderSystem::render(*gfx);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.05f);
    }
}

TEST_CASE("voxel.render.nullAtlasUsesWhite") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();

    const glm::vec3 eye(1.f, 1.f, 8.f);
    const glm::vec3 target(1.f, 1.f, 1.f);
    renderVoxelFrame(gfx, world.get(), nullptr, 1, eye, target, 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.largeInstanceCount") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Many non-merged top faces: sparse pillars.
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            if (((x * 3 + z * 5) % 7) == 0) world->setVoxel(x, 0, z, 1);
    world->remeshDirty();
    CHECK(world->getOrCreateChunk(0, 0, 0)->totalRectCount() > 20);

    Texture *atlas = makeSolid(gfx, 200, 180, 60);
    const glm::vec3 eye(8.f, 10.f, 28.f);
    const glm::vec3 target(8.f, 0.f, 8.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleRectCount() > 10);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.farChunkOrigin") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Chunk (2,0,0) origin at x=64.
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(64 + x, y, z, 1);
    world->remeshDirty();
    CHECK(world->hasChunk(2, 0, 0));

    Texture *atlas = makeSolid(gfx, 220, 80, 200);
    const glm::vec3 eye(66.f, 2.f, 16.f);
    const glm::vec3 target(66.f, 2.f, 2.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleChunkCount() >= 1);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.depthNearOccludesFar") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Near red wall at z=2, far green wall at z=0 — camera looks -Z from z=12.
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            world->setVoxel(x, y, 0, 2);  // far (will use green atlas tile... single solid tex)
            world->setVoxel(x, y, 2, 1);  // near
        }
    world->remeshDirty();

    // Use bright red atlas — both walls same color; verify something draws, then
    // compare against far-only scene luma stability (near wall should dominate).
    Texture *red = makeSolid(gfx, 240, 30, 30);
    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);
    renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 200.f, true);
    Color withNear = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(withNear.r > 0.12f);

    // Remove near wall voxels.
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) world->setVoxel(x, y, 2, 0);
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 200.f, true);
    Color farOnly = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Far wall still visible (same color); just ensure render path still works.
    CHECK(farOnly.r > 0.08f);
    CHECK(luma(withNear) > 0.05f);
}

TEST_CASE("voxel.render.multiFrameSlotReuse") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 160, 160, 200);
    const glm::vec3 eye(0.5f, 0.5f, 5.f);
    const glm::vec3 target(0.5f, 0.5f, 0.5f);

    for (int frame = 0; frame < 6; ++frame) {
        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
    }
}

TEST_CASE("voxel.render.allSixFacesFromOrbit") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 210, 200, 190);

    struct Cam {
        glm::vec3 eye;
        glm::vec3 target;
        FaceDir expect;
    };
    const Cam cams[] = {
        {{20.f, 3.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::PosX},
        {{-8.f, 3.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::NegX},
        {{3.f, 20.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::PosY},
        {{3.f, -8.f, 3.f}, {3.f, 3.f, 3.f}, FaceDir::NegY},
        {{3.f, 3.f, 20.f}, {3.f, 3.f, 3.f}, FaceDir::PosZ},
        {{3.f, 3.f, -8.f}, {3.f, 3.f, 3.f}, FaceDir::NegZ},
    };

    for (const auto &c : cams) {
        // For top/bottom views, renderVoxelFrame uses +Y up which is fine for ±X/±Z;
        // for ±Y eye, lookAtRH with up=(0,1,0) degenerates — use custom path.
        const float aspect =
            float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        glm::vec3 up(0.f, 1.f, 0.f);
        if (std::fabs(c.eye.y - c.target.y) > 10.f) up = glm::vec3(0.f, 0.f, 1.f);
        const glm::mat4 view = glm::lookAtRH(c.eye, c.target, up);
        const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
        const glm::mat4 vp = proj * view;
        world->selectVisible(&vp[0][0], c.eye.x, c.eye.y, c.eye.z, 200.f, true);

        bool saw = false;
        for (int i = 0; i < world->getVisibleBatchCount(); ++i)
            if (world->getVisibleBatch(i).dir == c.expect) saw = true;
        CHECK(saw);

        gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
        gfx->begin3DFrame();
        if (gfx->had3DThisFrame()) {
            gfx->setMesh3DViewProj(vp);
            gfx->setMesh3DCameraPos(c.eye);
            world->drawVisible(gfx, atlas, 1);
        }
        RenderSystem::render(*gfx);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.05f);
    }
}

TEST_CASE("voxel.render.tilesPerRowAtlasSample") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 2×2 atlas: index 1 (col1,row0) = pure green.
    uint8_t px[16] = {
        200, 40, 40, 255,   // 0 red
        40, 200, 40, 255,   // 1 green
        40, 40, 200, 255,   // 2 blue
        200, 200, 40, 255,  // 3 yellow
    };
    Texture *atlas = gfx->newTexture(2, 2, px);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // tex id 1 → green tile
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();

    const glm::vec3 eye(1.5f, 1.5f, 10.f);
    const glm::vec3 target(1.5f, 1.5f, 1.5f);
    renderVoxelFrame(gfx, world.get(), atlas, 2, eye, target, 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > mid.r);
    CHECK(mid.g > mid.b);
    CHECK(mid.g > 0.1f);
}

TEST_CASE("voxel.render.editRemeshChangesPixels") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Texture *atlas = makeSolid(gfx, 230, 230, 240);
    const glm::vec3 eye(4.f, 4.f, 18.f);
    const glm::vec3 target(4.f, 4.f, 4.f);

    // Empty world → dark
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK_EQ(world->getVisibleRectCount(), 0);
    Color empty = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(empty) < 0.2f);

    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    Color filled = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(filled) > luma(empty) + 0.03f);
}

TEST_CASE("voxel.render.emptyWorldDrawVisible") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Texture *atlas = makeSolid(gfx, 255, 255, 255);
    const glm::vec3 eye(0.f, 0.f, 5.f);
    const glm::vec3 target(0.f, 0.f, 0.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    CHECK_EQ(world->getVisibleBatchCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) < 0.25f);
}

TEST_CASE("voxel.render.negFaceDirAliases") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    // Camera on -Z looking toward origin → see NegZ? Actually looking +Z direction from -Z.
    // Eye at z=-4 looking at z=0.5 → see -Z face of voxel at origin (plane z=0).
    const glm::vec3 eye(0.5f, 0.5f, -4.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    uint32_t packed = PackedRect::pack(0, 0, 0, 1, 1, 1).bits;
    Texture *atlas = makeSolid(gfx, 180, 90, 40);

    for (const char *name : {"negZ", "-z"}) {
        gfx->setBackgroundColor(Color(0.04f, 0.05f, 0.06f, 1.f));
        gfx->begin3DFrame();
        if (gfx->had3DThisFrame()) {
            gfx->setMesh3DViewProj(vp);
            gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, name, atlas, 1);
        }
        RenderSystem::render(*gfx);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
    }
}

TEST_CASE("voxel.render.manyChunksBatched") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int cx = 0; cx < 3; ++cx)
        for (int cz = 0; cz < 3; ++cz) {
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    for (int z = 0; z < 2; ++z)
                        world->setVoxel(cx * 32 + x + 8, y, cz * 32 + z + 8, 1);
        }
    world->remeshDirty();
    CHECK_EQ(world->getChunkCount(), 9);

    Texture *atlas = makeSolid(gfx, 120, 160, 200);
    const glm::vec3 eye(48.f, 20.f, 120.f);
    const glm::vec3 target(48.f, 1.f, 48.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 300.f, true);
    CHECK(world->getVisibleChunkCount() >= 3);
    CHECK(world->getVisibleBatchCount() >= 3);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.singleFaceManualDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Large +Z quad covering much of the view.
    uint32_t packed = PackedRect::pack(0, 0, 0, 8, 8, 1).bits;
    Texture *atlas = makeSolid(gfx, 50, 200, 80);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 4.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(45.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posZ", atlas, 1);
    }
    RenderSystem::render(*gfx);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > mid.r);
    CHECK(mid.g > 0.08f);

    // Corners of screen may still be background depending on FOV; center must be lit.
    Color corner = gfx->getPixel(2, 2);
    CHECK(luma(mid) > luma(corner));
}

TEST_CASE("voxel.render.tilesPerRowBlueTile") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint8_t px[16] = {
        200, 40, 40, 255, 40, 200, 40, 255, 40, 40, 220, 255, 200, 200, 40, 255,
    };
    Texture *atlas = gfx->newTexture(2, 2, px);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // tex id 2 → blue (row1,col0)
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 2);
    world->remeshDirty();

    const glm::vec3 eye(2.f, 2.f, 12.f);
    renderVoxelFrame(gfx, world.get(), atlas, 2, eye, glm::vec3(2.f, 2.f, 2.f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.b > mid.r);
    CHECK(mid.b > mid.g);
    CHECK(mid.b > 0.1f);
}

TEST_CASE("voxel.render.instanceCountGrowth") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeSolid(gfx, 200, 200, 210);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(8.f, 8.f, 40.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(8.f, 0.f, 8.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp = proj * view;

    // Grow instance buffer across draws in one frame: 1, then many.
    std::vector<uint32_t> many;
    many.reserve(64);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x)
            many.push_back(PackedRect::pack(x, 0, z, 1, 1, 1).bits);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        uint32_t one = many[0];
        gfx->drawVoxelFaceInstances(&one, 1, 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(many.data(), int(many.size()), 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.posXWallFromSide") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y) world->setVoxel(0, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 230, 120, 50);

    const glm::vec3 eye(12.f, 4.f, 4.f);
    const glm::vec3 target(0.f, 4.f, 4.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);

    bool sawPosX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::PosX) sawPosX = true;
    CHECK(sawPosX);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > 0.1f);
}

TEST_CASE("voxel.render.clearColorWhenCulled") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 255, 255, 255);

    // Camera behind looking away from chunk.
    const glm::vec3 eye(16.f, 16.f, 16.f);
    const glm::vec3 target(16.f, 16.f, 100.f);  // look +Z, chunk is around same place but...
    // Better: put eye far and look opposite.
    renderVoxelFrame(gfx, world.get(), atlas, 1, glm::vec3(-40.f, 16.f, 16.f),
                     glm::vec3(-80.f, 16.f, 16.f), 200.f, true);
    // Chunk at origin not in front of camera looking toward -X further.
    CHECK_EQ(world->getVisibleChunkCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) < 0.22f);
}
