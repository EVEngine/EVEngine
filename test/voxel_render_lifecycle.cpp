#include "VoxelRenderFixtures.h"

TEST_CASE("voxel.render.emptyInstancesNoCrash") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 view   = glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp     = proj * view;

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

TEST_CASE("voxel.render.multiBatchSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture *atlas = makeSolid(gfx, 90, 140, 220);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 1.5f, 10.f);
    const glm::vec3 target(1.5f, 1.5f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

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
            gfx->drawVoxelFaceInstances(b.packed, b.count, b.chunk->originX(), b.chunk->originY(), b.chunk->originZ(),
                                        faceDirName(b.dir), atlas, 1);
        }
    }
    RenderSystem::render(*gfx);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
}

TEST_CASE("voxel.render.requiresBegin3DFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 160, 120);

    uint32_t dummy = PackedRect::pack(0, 0, 0, 1, 1, 1).bits;
    bool     threw = false;
    try {
        gfx->drawVoxelFaceInstances(&dummy, 1, 0.f, 0.f, 0.f, "posZ", nullptr, 1);
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("voxel.render.largeInstanceCount") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
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

    Texture        *atlas = makeSolid(gfx, 200, 180, 60);
    const glm::vec3 eye(8.f, 10.f, 28.f);
    const glm::vec3 target(8.f, 0.f, 8.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    CHECK(world->getVisibleRectCount() > 10);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.multiFrameSlotReuse") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();
    Texture        *atlas = makeSolid(gfx, 160, 160, 200);
    const glm::vec3 eye(0.5f, 0.5f, 5.f);
    const glm::vec3 target(0.5f, 0.5f, 0.5f);

    for (int frame = 0; frame < 6; ++frame) {
        renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
    }
}

TEST_CASE("voxel.render.editRemeshChangesPixels") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Texture                    *atlas = makeSolid(gfx, 230, 230, 240);
    const glm::vec3             eye(4.f, 4.f, 18.f);
    const glm::vec3             target(4.f, 4.f, 4.f);

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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Texture                    *atlas = makeSolid(gfx, 255, 255, 255);
    const glm::vec3             eye(0.f, 0.f, 5.f);
    const glm::vec3             target(0.f, 0.f, 0.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 100.f, true);
    CHECK_EQ(world->getVisibleBatchCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) < 0.25f);
}

TEST_CASE("voxel.render.manyChunksBatched") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int cx = 0; cx < 3; ++cx)
        for (int cz = 0; cz < 3; ++cz) {
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    for (int z = 0; z < 2; ++z) world->setVoxel(cx * 32 + x + 8, y, cz * 32 + z + 8, 1);
        }
    world->remeshDirty();
    CHECK_EQ(world->getChunkCount(), 9);

    Texture        *atlas = makeSolid(gfx, 120, 160, 200);
    const glm::vec3 eye(48.f, 20.f, 120.f);
    const glm::vec3 target(48.f, 1.f, 48.f);
    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 300.f, true);
    CHECK(world->getVisibleChunkCount() >= 3);
    CHECK(world->getVisibleBatchCount() >= 3);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}

TEST_CASE("voxel.render.instanceCountGrowth") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture        *atlas  = makeSolid(gfx, 200, 200, 210);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(8.f, 8.f, 40.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(8.f, 0.f, 8.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

    // Grow instance buffer across draws in one frame: 1, then many.
    std::vector<uint32_t> many;
    many.reserve(64);
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) many.push_back(PackedRect::pack(x, 0, z, 1, 1, 1).bits);

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

TEST_CASE("voxel.render.twoOriginsSameFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t a     = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    uint32_t b     = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    Texture *atlas = makeSolid(gfx, 90, 200, 120);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 1.f, 4.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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

TEST_CASE("voxel.render.removeVoxelsDarkens") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 6; ++z)
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    Texture        *atlas = makeSolid(gfx, 240, 240, 250);
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

TEST_CASE("voxel.render.stressHundredsOfInstances") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::vector<uint32_t> many;
    many.reserve(256);
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) many.push_back(PackedRect::pack(x, 0, z, 1, 1, 1).bits);
    CHECK_EQ(int(many.size()), 256);

    Texture        *atlas  = makeSolid(gfx, 180, 190, 200);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(8.f, 12.f, 30.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(8.f, 0.f, 8.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

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

TEST_CASE("voxel.render.manualAllSixDirsOneFrame") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t    packed = PackedRect::pack(0, 0, 0, 2, 2, 1).bits;
    Texture    *atlas  = makeSolid(gfx, 220, 220, 40);
    const float aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    // Orbit-ish view so several faces of a unit cube-ish placement are on screen.
    const glm::vec3 eye(8.f, 8.f, 8.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.f, 1.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(55.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    const char *dirs[6] = {"posX", "negX", "posY", "negY", "posZ", "negZ"};
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        for (const char *d : dirs) gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, d, atlas, 1);
    }
    RenderSystem::render(*gfx);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.03f);
}

TEST_CASE("voxel.render.nullPackedPointerNoCrash") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 200, 150);
    tinyHud(gfx);

    Texture        *atlas  = makeSolid(gfx, 255, 255, 255);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 vp     = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) *
                         glm::lookAtRH(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(nullptr, 10, 0.f, 0.f, 0.f, "posY", atlas, 1);
        gfx->drawVoxelFaceInstances(nullptr, 0, 0.f, 0.f, 0.f, "posY", atlas, 1);
    }
    RenderSystem::render(*gfx);
}

TEST_CASE("voxel.render.remeshIncreasesThenDecreasesRects") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
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
    renderVoxelFrame(gfx, world.get(), atlas, 1, glm::vec3(2.5f, 2.5f, 14.f), glm::vec3(2.5f, 2.5f, 2.5f), 100.f, true);
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
