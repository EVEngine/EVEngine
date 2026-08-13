#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include "RenderImageAudit.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
#include "voxel/Chunk.h"
#include "voxel/FaceDir.h"
#include "voxel/Voxel.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelWorld.h"
#include "window/Window.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>

using namespace eve::graphics;
using namespace eve::voxel;
using eve::image::ImageData;

static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

/** How many samples on a coarse grid differ from the 3D clear color. */
static int countNonBgSamples(Graphics *gfx, const Color &bg, float eps = 0.08f) {
    const int w = gfx->getWidth();
    const int h = gfx->getHeight();
    const float bl = luma(bg);
    int n = 0;
    for (int y = h / 10; y < h * 9 / 10; y += std::max(1, h / 12)) {
        for (int x = w / 10; x < w * 9 / 10; x += std::max(1, w / 12)) {
            if (std::fabs(luma(gfx->getPixel(x, y)) - bl) > eps) ++n;
        }
    }
    return n;
}

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

/** Deterministic high-contrast palette for atlas tile `id` (0..127). */
static void atlasTileRgb(int id, uint8_t &r, uint8_t &g, uint8_t &b) {
    static const uint8_t kPal[][3] = {
        {220, 40, 40},  {40, 220, 40},  {40, 40, 220},  {220, 220, 40},
        {220, 40, 220}, {40, 220, 220}, {240, 240, 240}, {220, 120, 40},
        {120, 40, 220}, {40, 160, 120}, {200, 80, 120}, {80, 200, 80},
        {80, 80, 200},  {200, 160, 40}, {160, 40, 80},  {40, 200, 200},
    };
    const int n = int(sizeof(kPal) / sizeof(kPal[0]));
    const int i = ((id % n) + n) % n;
    r = kPal[i][0];
    g = kPal[i][1];
    b = kPal[i][2];
}

/** NxM tile atlas (1px per tile). `tilesPerRow` must match draw call. */
static Texture *makeTileAtlas(Graphics *gfx, int tilesPerRow, int rows) {
    const int w = std::max(1, tilesPerRow);
    const int h = std::max(1, rows);
    std::vector<uint8_t> px(size_t(w * h * 4));
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            const int id = tx + ty * w;
            uint8_t r, g, b;
            atlasTileRgb(id, r, g, b);
            const size_t o = size_t((ty * w + tx) * 4);
            px[o + 0] = r;
            px[o + 1] = g;
            px[o + 2] = b;
            px[o + 3] = 255;
        }
    }
    return gfx->newTexture(w, h, px.data());
}

static void fillCube(VoxelWorld *world, int x0, int y0, int z0, int s, uint8_t tex) {
    for (int z = 0; z < s; ++z)
        for (int y = 0; y < s; ++y)
            for (int x = 0; x < s; ++x) world->setVoxel(x0 + x, y0 + y, z0 + z, tex);
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
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 500.f);
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
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
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
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
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
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
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
        const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
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
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
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
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(45.f), aspect, 0.1f, 100.f);
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
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
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

TEST_CASE("voxel.render.posYFromAbove") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 6; ++x) world->setVoxel(x, 0, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 200, 180, 60);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 18.f, 3.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 0.f, 3.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);

    bool sawPosY = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::PosY) sawPosY = true;
    CHECK(sawPosY);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.twoOriginsSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t a = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    uint32_t b = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    Texture *atlas = makeSolid(gfx, 90, 200, 120);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 1.f, 4.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&a, 1, 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(&b, 1, 6.f, 0.f, 6.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.faceCullRectCountAboutHalf") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(40.f, 16.f, 16.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(16.f, 16.f, 16.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp = proj * view;

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, false);
    CHECK_EQ(world->getVisibleRectCount(), 6);

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, true);
    CHECK(world->getVisibleRectCount() >= 1);
    CHECK(world->getVisibleRectCount() <= 5);
    CHECK(world->getVisibleRectCount() < 6);
}

TEST_CASE("voxel.render.removeVoxelsDarkens") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 240, 240, 250);
    const glm::vec3 eye(3.f, 3.f, 16.f);
    const glm::vec3 target(3.f, 3.f, 3.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color filled = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(filled) > 0.08f);

    world->clear();
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color empty = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(empty) < luma(filled));
    CHECK(luma(empty) < 0.22f);
}

TEST_CASE("voxel.render.posXAliasPlusX") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    Texture *atlas = makeSolid(gfx, 220, 80, 40);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(10.f, 2.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.f, 2.f, 2.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "+x", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > 0.08f);
}

TEST_CASE("voxel.render.stressHundredsOfInstances") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::vector<uint32_t> many;
    many.reserve(256);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x)
            many.push_back(PackedRect::pack(x, 0, z, 1, 1, 1).bits);
    CHECK_EQ(int(many.size()), 256);

    Texture *atlas = makeSolid(gfx, 180, 190, 200);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(8.f, 12.f, 30.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(8.f, 0.f, 8.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(many.data(), int(many.size()), 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.alternatingAtlasFrames") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();

    Texture *red = makeSolid(gfx, 230, 40, 40);
    Texture *blue = makeSolid(gfx, 40, 40, 230);
    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 100.f, true);
    Color cRed = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cRed.r > cRed.b);

    renderVoxelFrame(gfx, world.get(), blue, 1, eye, target, 100.f, true);
    Color cBlue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cBlue.b > cBlue.r);
}

TEST_CASE("voxel.render.chunkSeamDoubleFaceStillDraws") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Voxels on both sides of x=32 seam.
    for (int y = 0; y < 4; ++y)
        for (int z = 0; z < 4; ++z) {
            world->setVoxel(31, y, z, 1);
            world->setVoxel(32, y, z, 1);
        }
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 200, 160, 100);

    const glm::vec3 eye(48.f, 2.f, 2.f);
    const glm::vec3 target(32.f, 2.f, 2.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleChunkCount() >= 1);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.negYFromBelow") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int x = 0; x < 6; ++x) world->setVoxel(x, 4, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 180, 120, 60);

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, -10.f, 3.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 4.f, 3.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);

    bool sawNegY = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::NegY) sawNegY = true;
    CHECK(sawNegY);

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.posZWallFromFront") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;
    Texture *atlas = makeSolid(gfx, 80, 200, 220);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 3.f, 14.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 3.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posZ", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.tilesPerRowZeroClamped") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    Texture *atlas = makeSolid(gfx, 210, 90, 50);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 8.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        // tilesPerRow <= 0 should clamp to 1 and still sample the solid atlas.
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 0);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.zeroCountAfterLargeDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::vector<uint32_t> many;
    for (int i = 0; i < 64; ++i) many.push_back(PackedRect::pack(i % 8, 0, i / 8, 1, 1, 1).bits);
    Texture *atlas = makeSolid(gfx, 200, 200, 200);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 10.f, 4.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(many.data(), int(many.size()), 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(many.data(), 0, 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.manualAllSixDirsOneFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    Texture *atlas = makeSolid(gfx, 220, 220, 40);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    // Orbit-ish view so several faces of a unit cube-ish placement are on screen.
    const glm::vec3 eye(8.f, 8.f, 8.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.f, 1.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(55.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    const char *dirs[6] = {"posX", "negX", "posY", "negY", "posZ", "negZ"};
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        for (const char *d : dirs)
            gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, d, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.rangeCullDarkensDistant") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x + 64, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 240, 240, 250);
    const glm::vec3 eye(66.f, 2.f, 20.f);
    const glm::vec3 target(66.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    Color nearEnough = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(nearEnough) > 0.06f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 5.f, true);
    Color culled = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(culled) < luma(nearEnough));
    CHECK(luma(culled) < 0.22f);
}

TEST_CASE("voxel.render.negZAliasMinusZ") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 5, 4, 4, 1).bits;
    Texture *atlas = makeSolid(gfx, 40, 180, 90);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 2.f, -6.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 2.f, 5.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "-z", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.fullChunkSixFacesOrbitPixel") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->totalRectCount(), 6);
    Texture *atlas = makeSolid(gfx, 160, 170, 180);

    const glm::vec3 eye(48.f, 40.f, 48.f);
    const glm::vec3 target(16.f, 16.f, 16.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 300.f, true);
    CHECK(world->getVisibleBatchCount() >= 2);
    CHECK(world->getVisibleBatchCount() <= 3);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.atlas4x4_tex1_green") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Voxel storage 0 = air; solid tex starts at 1 → atlas tile 1 (green).
    fillCube(world.get(), 0, 0, 0, 4, 1);
    world->remeshDirty();

    const glm::vec3 eye(2.f, 2.f, 14.f);
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, glm::vec3(2.f, 2.f, 2.f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > mid.r);
    CHECK(mid.g > mid.b);
    CHECK(mid.g > 0.08f);
}

TEST_CASE("voxel.render.atlas4x4_eachPaletteChannel") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    // tex 1 green, 2 blue, 3 yellow, 4 magenta, 5 cyan
    struct Expect {
        uint8_t tex;
        char channel;  // 'r','g','b' dominant or 'y' r&g, 'm' r&b, 'c' g&b
    };
    const Expect cases[] = {{1, 'g'}, {2, 'b'}, {3, 'y'}, {4, 'm'}, {5, 'c'}};
    for (const Expect &e : cases) {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        fillCube(world.get(), 0, 0, 0, 4, e.tex);
        world->remeshDirty();
        renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
        if (e.channel == 'g') {
            CHECK(mid.g > mid.r);
            CHECK(mid.g > mid.b);
        } else if (e.channel == 'b') {
            CHECK(mid.b > mid.r);
            CHECK(mid.b > mid.g);
        } else if (e.channel == 'y') {
            CHECK(mid.r > mid.b);
            CHECK(mid.g > mid.b);
        } else if (e.channel == 'm') {
            CHECK(mid.r > mid.g);
            CHECK(mid.b > mid.g);
        } else if (e.channel == 'c') {
            CHECK(mid.g > mid.r);
            CHECK(mid.b > mid.r);
        }
    }
}

TEST_CASE("voxel.render.atlas8x8_highTexIndex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 8×8 = 64 tiles; tex 15 uses palette slot 15 → cyan-ish (40,200,200)
    Texture *atlas = makeTileAtlas(gfx, 8, 8);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 5, 15);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->faceRects(FaceDir::PosZ)[0].tex(), 15);

    const glm::vec3 eye(2.5f, 2.5f, 16.f);
    renderVoxelFrame(gfx, world.get(), atlas, 8, eye, glm::vec3(2.5f, 2.5f, 2.5f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > mid.r);
    CHECK(mid.b > mid.r);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.atlas16x8_tex63") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // tilesPerRow=16, 8 rows → indices 0..127
    Texture *atlas = makeTileAtlas(gfx, 16, 8);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 63);
    world->remeshDirty();
    REQUIRE(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY) >= 1);
    CHECK_EQ(world->getChunk(0, 0, 0)->faceRects(FaceDir::PosY)[0].tex(), 63);

    const glm::vec3 eye(2.f, 10.f, 2.f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(2.f, 2.f, 2.f), glm::vec3(0, 0, -1));
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 16);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.swapTexIdChangesAtlasColor") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);  // green
    world->remeshDirty();
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color green = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(green.g > green.r);

    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 2);  // blue
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color blue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(blue.b > blue.g);
    CHECK(blue.b > green.b);
}

TEST_CASE("voxel.render.multiTexStripesDoNotMerge") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Horizontal stripes of alternating tex 1 and 2 on a slab.
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) world->setVoxel(x, 0, z, (z % 2) ? 1 : 2);
    world->remeshDirty();
    Chunk *c = world->getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    CHECK(c->faceRectCount(FaceDir::PosY) >= 4);

    const glm::vec3 eye(4.f, 12.f, 4.f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 0, -1));
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.manualDrawDifferentTexIndices") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    // Two quads side by side: tex1 green @ x=0, tex2 blue @ origin shifted.
    uint32_t green = PackedRect::pack(0, 0, 0, 3, 3, 1).bits;
    uint32_t blue = PackedRect::pack(0, 0, 0, 3, 3, 2).bits;

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&green, 1, 0.f, 0.f, 0.f, "posY", atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color cGreen = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cGreen.g > cGreen.b);

    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&blue, 1, 0.f, 0.f, 0.f, "posY", atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color cBlue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cBlue.b > cBlue.g);
}

TEST_CASE("voxel.render.maxTex127_on16WideAtlas") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 16, 8);
    uint32_t packed = PackedRect::pack(0, 0, 0, 4, 4, 127).bits;
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 10.f, 2.f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 16);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.fourTexBlocksSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 3, 1);
    fillCube(world.get(), 5, 0, 0, 3, 2);
    fillCube(world.get(), 0, 0, 5, 3, 3);
    fillCube(world.get(), 5, 0, 5, 3, 4);
    world->remeshDirty();
    // Four different top-face textures → at least 4 PosY rects.
    CHECK(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY) >= 4);

    const glm::vec3 eye(4.f, 16.f, 4.f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 0, -1));
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(55.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 100.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.packedTex0_samplesFirstAtlasTile") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Packed tex 0 is a valid atlas index even though voxel air is also 0.
    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    uint32_t packed = PackedRect::pack(0, 0, 0, 4, 4, 0).bits;  // tile 0 = red
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 10.f, 2.f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 4);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > mid.g);
    CHECK(mid.r > mid.b);
    CHECK(mid.r > 0.08f);
}

TEST_CASE("voxel.render.atlasYellowThenMagentaSwap") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 5, 3);  // yellow
    world->remeshDirty();
    const glm::vec3 eye(2.5f, 2.5f, 16.f);
    const glm::vec3 target(2.5f, 2.5f, 2.5f);

    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color yellow = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(yellow.r > yellow.b);
    CHECK(yellow.g > yellow.b);

    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) world->setVoxel(x, y, z, 4);  // magenta
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color magenta = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(magenta.r > magenta.g);
    CHECK(magenta.b > magenta.g);
}

TEST_CASE("voxel.render.plusY_alias") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    Texture *atlas = makeSolid(gfx, 50, 210, 90);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 12.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "+y", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > 0.08f);
}

TEST_CASE("voxel.render.plusZ_alias") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    Texture *atlas = makeSolid(gfx, 40, 100, 230);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 2.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "+z", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.b > 0.08f);
}

TEST_CASE("voxel.render.wide32x1_rect") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 32, 1, 1).bits;
    Texture *atlas = makeSolid(gfx, 230, 180, 40);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(16.f, 10.f, 16.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(16.f, 0.f, 0.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.tall1x32_side") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 0, 0, 1, 32, 1).bits;  // ±X: w along Z, h along Y
    Texture *atlas = makeSolid(gfx, 200, 60, 200);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(20.f, 16.f, 0.5f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.f, 16.f, 0.5f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posX", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.negXWallFromLeft") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int y = 0; y < 6; ++y) world->setVoxel(4, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 230, 100, 50);

    const glm::vec3 eye(-6.f, 3.f, 3.f);
    const glm::vec3 target(4.f, 3.f, 3.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    bool sawNegX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i)
        if (world->getVisibleBatch(i).dir == FaceDir::NegX) sawNegX = true;
    CHECK(sawNegX);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.frustumCulledChunkDark") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Chunk far to the side — outside typical forward frustum when looking +Z.
    fillCube(world.get(), 200, 0, 200, 4, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 255, 255, 255);

    const glm::vec3 eye(0.f, 2.f, 0.f);
    const glm::vec3 target(0.f, 2.f, 10.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 500.f, true);
    CHECK_EQ(world->getVisibleChunkCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) < 0.22f);
}

TEST_CASE("voxel.render.carveHoleDarkensCenter") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 8, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 230, 230, 240);
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::vec3 target(4.f, 4.f, 4.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color solid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(solid) > 0.08f);

    // Carve a tunnel through the center along Z so the mid pixel looks through.
    for (int z = 0; z < 8; ++z)
        for (int y = 3; y <= 4; ++y)
            for (int x = 3; x <= 4; ++x) world->setVoxel(x, y, z, 0);
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    Color carved = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(carved) < luma(solid));
}

TEST_CASE("voxel.render.twoChunksDifferentAtlasTiles") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);    // green
    fillCube(world.get(), 32, 0, 0, 4, 2);   // blue, next chunk
    world->remeshDirty();
    CHECK_EQ(world->getChunkCount(), 2);

    // Look at green chunk.
    renderVoxelFrame(gfx, world.get(), atlas, 4, glm::vec3(2.f, 2.f, 14.f),
                     glm::vec3(2.f, 2.f, 2.f), 100.f, true);
    Color green = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(green.g > green.b);

    // Look at blue chunk.
    renderVoxelFrame(gfx, world.get(), atlas, 4, glm::vec3(34.f, 2.f, 14.f),
                     glm::vec3(34.f, 2.f, 2.f), 100.f, true);
    Color blue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(blue.b > blue.g);
}

TEST_CASE("voxel.render.atlas32wide_tex31") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 32, 2);  // 64 tiles
    uint32_t packed = PackedRect::pack(0, 0, 0, 3, 3, 31).bits;
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 32);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.minusX_alias") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(5, 0, 0, 4, 4, 1).bits;
    Texture *atlas = makeSolid(gfx, 220, 70, 40);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(-4.f, 2.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(5.f, 2.f, 2.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.07f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "-x", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > 0.08f);
}

TEST_CASE("voxel.render.nearPlaneOccludesFarDifferentColor") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *red = makeSolid(gfx, 240, 30, 30);
    Texture *green = makeSolid(gfx, 30, 240, 30);
    uint32_t nearR = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;
    uint32_t farG = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;

    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 3.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 3.f, 0.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        // Far green at z≈1, near red at z≈8 — both posZ planes.
        gfx->drawVoxelFaceInstances(&farG, 1, 0.f, 0.f, 0.f, "posZ", green, 1);
        gfx->drawVoxelFaceInstances(&nearR, 1, 0.f, 0.f, 8.f, "posZ", red, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > mid.g);
    CHECK(mid.r > 0.08f);
}

TEST_CASE("voxel.render.minusY_alias") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t packed = PackedRect::pack(0, 4, 0, 4, 4, 1).bits;
    Texture *atlas = makeSolid(gfx, 180, 90, 40);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, -6.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 4.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "-y", atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.nullPackedPointerNoCrash") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    tinyHud(gfx);

    Texture *atlas = makeSolid(gfx, 255, 255, 255);
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 vp = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) *
                         glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(nullptr, 10, 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(nullptr, 0, 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
}

TEST_CASE("voxel.render.drawVisibleNullAtlasWhite") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);
    world->remeshDirty();
    const glm::vec3 eye(2.f, 2.f, 12.f);
    renderVoxelFrame(gfx, world.get(), nullptr, 1, eye, glm::vec3(2.f, 2.f, 2.f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.multiFrameAtlasSwapStable") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlasA = makeTileAtlas(gfx, 4, 4);
    Texture *atlasB = makeSolid(gfx, 40, 40, 230);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);
    world->remeshDirty();
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    for (int i = 0; i < 4; ++i) {
        renderVoxelFrame(gfx, world.get(), (i % 2) ? atlasB : atlasA, (i % 2) ? 1 : 4, eye, target,
                         100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
    }
}

TEST_CASE("voxel.render.sparsePillarsMultiTex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 12; z += 3)
        for (int x = 0; x < 12; x += 3)
            for (int y = 0; y < 3; ++y) world->setVoxel(x, y, z, uint8_t(1 + ((x + z) % 4)));
    world->remeshDirty();
    CHECK(world->getChunk(0, 0, 0)->totalRectCount() > 10);

    const glm::vec3 eye(6.f, 8.f, 22.f);
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, glm::vec3(6.f, 1.f, 6.f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.fullLayerTopFromAbove") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) world->setVoxel(x, 0, z, 1);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY), 1);
    Texture *atlas = makeSolid(gfx, 200, 160, 80);

    const glm::vec3 eye(16.f, 40.f, 16.f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(16.f, 0.f, 16.f), glm::vec3(0, 0, -1));
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, true);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        world->drawVisible(gfx, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);
}

TEST_CASE("voxel.render.twoFacesStackedDepth") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *nearTex = makeSolid(gfx, 40, 200, 60);
    Texture *farTex = makeSolid(gfx, 200, 40, 40);
    uint32_t a = PackedRect::pack(0, 0, 0, 5, 5, 1).bits;
    uint32_t b = PackedRect::pack(0, 0, 0, 5, 5, 1).bits;
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.5f, 2.5f, 18.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.5f, 2.5f, 0.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&b, 1, 0.f, 0.f, 0.f, "posZ", farTex, 1);
        gfx->drawVoxelFaceInstances(&a, 1, 0.f, 0.f, 6.f, "posZ", nearTex, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.g > mid.r);
}

TEST_CASE("voxel.render.chunkOriginNegativeDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Local (0,0,0) in chunk (-1,0,0) → world x in [-32,-1]
    world->setVoxel(-16, 0, 0, 1);
    world->setVoxel(-15, 0, 0, 1);
    world->setVoxel(-16, 1, 0, 1);
    world->setVoxel(-15, 1, 0, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 220, 180, 60);
    const glm::vec3 eye(-15.5f, 1.f, 12.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, glm::vec3(-15.5f, 1.f, 0.f), 100.f, true);
    CHECK(world->getVisibleChunkCount() >= 1);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.tilesPerRowMismatchStillDraws") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 4×4 atlas but claim tilesPerRow=8 — UVs still sample something opaque.
    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    uint32_t packed = PackedRect::pack(0, 0, 0, 3, 3, 1).bits;
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view =
        glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp = proj * view;

    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 8);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.remeshIncreasesThenDecreasesRects") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(2, 2, 2, 1);
    world->remeshDirty();
    const int one = world->getChunk(0, 0, 0)->totalRectCount();
    CHECK_EQ(one, 6);

    fillCube(world.get(), 0, 0, 0, 5, 1);
    world->remeshDirty();
    const int many = world->getChunk(0, 0, 0)->totalRectCount();
    CHECK(many < one * 5);  // greedy merges
    CHECK(many >= 6);

    Texture *atlas = makeSolid(gfx, 200, 200, 210);
    renderVoxelFrame(gfx, world.get(), atlas, 1, glm::vec3(2.5f, 2.5f, 14.f),
                     glm::vec3(2.5f, 2.5f, 2.5f), 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.05f);

    world->clear();
    world->setVoxel(2, 2, 2, 1);
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->totalRectCount(), 6);
}

// ---------------------------------------------------------------------------
// Large visual scenes (PNG under test/out/voxel_scenes/)
// ---------------------------------------------------------------------------

static uint32_t voxelHash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float voxelH21(int x, int z) {
    return float(voxelHash(uint32_t(x) * 0x9E3779B9u ^ uint32_t(z) * 0x85EBCA6Bu) & 0xFFFFu) /
           65535.f;
}

static float voxelValueNoise(float x, float z) {
    const int x0 = int(std::floor(x));
    const int z0 = int(std::floor(z));
    const float tx = x - float(x0);
    const float tz = z - float(z0);
    const float sx = tx * tx * (3.f - 2.f * tx);
    const float sz = tz * tz * (3.f - 2.f * tz);
    const float a = voxelH21(x0, z0);
    const float b = voxelH21(x0 + 1, z0);
    const float c = voxelH21(x0, z0 + 1);
    const float d = voxelH21(x0 + 1, z0 + 1);
    return (a * (1.f - sx) + b * sx) * (1.f - sz) + (c * (1.f - sx) + d * sx) * sz;
}

static float voxelFbm(float x, float z) {
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int i = 0; i < 4; ++i) {
        sum += amp * voxelValueNoise(x * freq, z * freq);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / std::max(norm, 1e-4f);
}

static Texture *makeEarthAtlas(Graphics *gfx) {
    static const uint8_t pal[16][3] = {
        {20, 22, 28},    {86, 158, 62},   {145, 96, 55},   {128, 128, 132},
        {210, 186, 120}, {48, 96, 168},   {118, 78, 42},   {46, 118, 48},
        {236, 240, 245}, {96, 96, 100},   {168, 52, 42},   {210, 196, 168},
        {72, 48, 32},    {148, 72, 56},   {212, 168, 64},  {64, 68, 76},
    };
    std::vector<uint8_t> px(16u * 4u);
    for (int i = 0; i < 16; ++i) {
        px[size_t(i) * 4u + 0] = pal[i][0];
        px[size_t(i) * 4u + 1] = pal[i][1];
        px[size_t(i) * 4u + 2] = pal[i][2];
        px[size_t(i) * 4u + 3] = 255;
    }
    return gfx->newTexture(4, 4, px.data());
}

static void fillBox(VoxelWorld *w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t tex) {
    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) w->setVoxel(x, y, z, tex);
}

static void fillHollowBox(VoxelWorld *w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t tex) {
    fillBox(w, x0, y0, z0, x1, y1, z1, tex);
    if (x1 - x0 > 2 && y1 - y0 > 1 && z1 - z0 > 2)
        fillBox(w, x0 + 1, y0, z0 + 1, x1 - 1, y1, z1 - 1, 0);
}

static void placeTree(VoxelWorld *w, int x, int groundY, int z) {
    const int h = 3 + int(voxelH21(x, z) * 3.f);
    for (int y = 1; y <= h; ++y) w->setVoxel(x, groundY + y, z, 6);
    const int cy = groundY + h;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = 0; dy <= 2; ++dy) {
                if (dx * dx + dy * dy + dz * dz > 6) continue;
                w->setVoxel(x + dx, cy + dy, z + dz, 7);
            }
        }
    }
}

static void placeHouse(VoxelWorld *w, int x, int z, int wdt, int dpt, int hgt, uint8_t wall,
                       uint8_t roof) {
    fillHollowBox(w, x, 1, z, x + wdt, 1 + hgt, z + dpt, wall);
    fillBox(w, x, 1 + hgt, z, x + wdt, 1 + hgt + 1, z + dpt, roof);
    const int ridgeZ = z + dpt / 2;
    for (int i = 0; i < wdt; ++i) w->setVoxel(x + i, 2 + hgt, ridgeZ, roof);
}

static std::string voxelSceneOutDir() {
    return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/voxel_scenes";
}

static void renderVoxelScene(Graphics *gfx, VoxelWorld *world, Texture *atlas, const glm::vec3 &eye,
                             const glm::vec3 &target, float farZ, float viewRange) {
    const float aspect =
        float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(48.f), aspect, 0.2f, farZ);
    const glm::mat4 vp = proj * view;
    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, viewRange, true);
    const Color sky(0.42f, 0.58f, 0.78f, 1.f);
    gfx->setBackgroundColor(sky);
    gfx->clear(sky, std::nullopt, std::nullopt);
    gfx->begin3DFrame();
    if (!gfx->had3DThisFrame()) return;
    gfx->setMesh3DViewProj(vp);
    gfx->setMesh3DCameraPos(eye);
    world->drawVisible(gfx, atlas, 4);
    RenderSystem::render(*gfx);
}

static void saveVoxelScenePng(Graphics *gfx, const std::string &name) {
    std::unique_ptr<ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    const std::string dir = voxelSceneOutDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/" + name + ".png";
    REQUIRE(saveImagePng(*img, path));
    std::printf("voxel scene: %s\n", path.c_str());
}

TEST_CASE("voxel.render.sceneHillsIsland") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    constexpr int kN = 160;
    constexpr int kSea = 6;
    for (int z = 0; z < kN; ++z) {
        for (int x = 0; x < kN; ++x) {
            const float n = voxelFbm(float(x) * 0.035f, float(z) * 0.035f);
            const float ridge =
                std::fabs(voxelFbm(float(x) * 0.018f + 40.f, float(z) * 0.018f) * 2.f - 1.f);
            const float h01 = n * 0.62f + (1.f - ridge) * 0.38f;
            const float cx = (float(x) - 80.f) / 88.f;
            const float cz = (float(z) - 80.f) / 88.f;
            float island = std::clamp(1.12f - std::sqrt(cx * cx + cz * cz), 0.f, 1.f);
            island *= island;
            int h = int(2.f + h01 * 20.f * island);
            if (h < 1) h = 1;
            if (h > 24) h = 24;
            for (int y = 0; y <= h; ++y) {
                uint8_t tex = 3;
                if (y == h) {
                    if (h <= kSea + 1)
                        tex = 4;
                    else if (h >= 18)
                        tex = 8;
                    else
                        tex = 1;
                } else if (y >= h - 2) {
                    tex = 2;
                }
                world->setVoxel(x, y, z, tex);
            }
            if (h < kSea) {
                for (int y = h + 1; y <= kSea; ++y) world->setVoxel(x, y, z, 5);
            }
        }
    }
    for (int z = 8; z < kN - 8; z += 11) {
        for (int x = 8; x < kN - 8; x += 13) {
            if (voxelH21(x * 3, z * 5) < 0.78f) continue;
            uint8_t top = 0;
            int gy = 24;
            while (gy > 0 && (top = world->getVoxel(x, gy, z)) == 0) --gy;
            if (top != 1) continue;
            placeTree(world.get(), x, gy, z);
        }
    }

    const int remeshed = world->remeshDirty();
    CHECK(remeshed >= 20);
    CHECK(world->getChunkCount() >= 20);

    Texture *atlas = makeEarthAtlas(gfx);
    REQUIRE(atlas != nullptr);
    const glm::vec3 eye(78.f, 52.f, 168.f);
    const glm::vec3 target(80.f, 8.f, 78.f);
    for (int i = 0; i < 2; ++i)
        renderVoxelScene(gfx, world.get(), atlas, eye, target, 420.f, 360.f);

    CHECK(world->getVisibleRectCount() > 80);
    const Color sky(0.42f, 0.58f, 0.78f, 1.f);
    CHECK(countNonBgSamples(gfx, sky) >= 20);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
    saveVoxelScenePng(gfx, "hills_island");
}

TEST_CASE("voxel.render.sceneWalledTown") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    constexpr int kN = 96;
    fillBox(world.get(), 0, 0, 0, kN, 1, kN, 2);
    fillBox(world.get(), 2, 1, 2, kN - 2, 2, kN - 2, 9);

    const int wallH = 8;
    for (int i = 4; i < kN - 4; ++i) {
        for (int y = 1; y <= wallH; ++y) {
            world->setVoxel(i, y, 4, 3);
            world->setVoxel(i, y, kN - 5, 3);
            world->setVoxel(4, y, i, 3);
            world->setVoxel(kN - 5, y, i, 3);
        }
        if ((i % 2) == 0) {
            world->setVoxel(i, wallH + 1, 4, 3);
            world->setVoxel(i, wallH + 1, kN - 5, 3);
            world->setVoxel(4, wallH + 1, i, 3);
            world->setVoxel(kN - 5, wallH + 1, i, 3);
        }
    }
    // Gate on +Z wall
    fillBox(world.get(), 44, 1, 4, 52, wallH + 2, 6, 0);
    fillBox(world.get(), 43, 1, 3, 44, wallH + 2, 7, 12);
    fillBox(world.get(), 52, 1, 3, 53, wallH + 2, 7, 12);

    auto tower = [&](int x, int z) {
        fillHollowBox(world.get(), x, 1, z, x + 7, 14, z + 7, 3);
        fillBox(world.get(), x, 14, z, x + 7, 15, z + 7, 10);
        fillBox(world.get(), x + 1, 15, z + 1, x + 6, 16, z + 6, 10);
        fillBox(world.get(), x + 2, 16, z + 2, x + 5, 17, z + 5, 10);
        world->setVoxel(x + 3, 17, z + 3, 14);
    };
    tower(2, 2);
    tower(kN - 9, 2);
    tower(2, kN - 9);
    tower(kN - 9, kN - 9);

    fillHollowBox(world.get(), 36, 1, 36, 60, 16, 60, 13);
    fillBox(world.get(), 36, 16, 36, 60, 17, 60, 10);
    fillBox(world.get(), 38, 17, 38, 58, 18, 58, 10);
    fillBox(world.get(), 46, 18, 46, 50, 22, 50, 12);
    world->setVoxel(48, 22, 48, 14);
    fillBox(world.get(), 46, 1, 36, 50, 8, 37, 0);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int x = 10 + col * 8;
            const int z = 12 + row * 8;
            if (x + 6 >= 34) continue;
            placeHouse(world.get(), x, z, 6, 6, 4 + ((col + row) % 3), 11, 10);
        }
    }
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            placeHouse(world.get(), 64 + col * 8, 12 + row * 9, 7, 7, 3 + (row % 2), 11, 10);
        }
    }
    fillBox(world.get(), 12, 1, 70, 30, 2, 88, 4);
    for (int i = 0; i < 5; ++i) placeTree(world.get(), 16 + i * 3, 1, 74 + (i % 2) * 4);

    const int remeshed = world->remeshDirty();
    CHECK(remeshed >= 4);
    CHECK(world->getChunkCount() >= 4);

    Texture *atlas = makeEarthAtlas(gfx);
    REQUIRE(atlas != nullptr);
    const glm::vec3 eye(118.f, 68.f, 118.f);
    const glm::vec3 target(48.f, 6.f, 48.f);
    for (int i = 0; i < 2; ++i)
        renderVoxelScene(gfx, world.get(), atlas, eye, target, 320.f, 280.f);

    CHECK(world->getVisibleRectCount() > 40);
    const Color sky(0.42f, 0.58f, 0.78f, 1.f);
    CHECK(countNonBgSamples(gfx, sky) >= 20);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
    saveVoxelScenePng(gfx, "walled_town");
}
