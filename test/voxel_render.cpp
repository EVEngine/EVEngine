#include "SceneColor.h"
#include "VoxelRenderFixtures.h"

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

    // Camera outside the +X chunk boundary; with face cull, back faces dropped — still should see the +X wall.
    const glm::vec3 eye(48.f, 4.f, 4.f);
    const glm::vec3 target(4.f, 4.f, 4.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    REQUIRE(world->getVisibleBatchCount() >= 1);
    bool sawPosX = false, sawNegX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i) {
        if (world->getVisibleBatch(i).dir == FaceDir::PosX) sawPosX = true;
        if (world->getVisibleBatch(i).dir == FaceDir::NegX) sawNegX = true;
    }
    REQUIRE(sawPosX);
    REQUIRE(!sawNegX);

    Color withCull = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, false);
    REQUIRE(world->getVisibleRectCount() >= 6);
    Color noCull = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);

    // Both frames should show geometry (not pure background).
    REQUIRE(luma(withCull) > 0.06f);
    REQUIRE(luma(noCull) > 0.06f);
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
    REQUIRE_EQ(world->getVisibleBatchCount(), 0);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Should be near the dark clear color.
    REQUIRE(luma(testSceneLinearColor(mid)) < 0.2f);
}

TEST_CASE("voxel.render.faceDirAliasesDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    Texture       *atlas      = makeSolid(gfx, 220, 180, 140);
    const uint32_t packed     = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    const float    aspect     = float(gfx->getPixelWidth()) / float(gfx->getPixelHeight());
    const auto     projection = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    struct Direction {
        const char *canonical;
        const char *alias;
        glm::vec3   eye, target, up;
    };
    const Direction directions[] = {
        {"posX", "+x", {10, 2, 2}, {1, 2, 2}, {0, 1, 0}},  {"negX", "-x", {-10, 2, 2}, {0, 2, 2}, {0, 1, 0}},
        {"posY", "+y", {2, 10, 2}, {2, 1, 2}, {0, 0, -1}}, {"negY", "-y", {2, -10, 2}, {2, 0, 2}, {0, 0, 1}},
        {"posZ", "+z", {2, 2, 10}, {2, 2, 1}, {0, 1, 0}},  {"negZ", "-z", {2, 2, -10}, {2, 2, 0}, {0, 1, 0}},
    };
    for (const auto &direction : directions) {
        Color canonical;
        for (const char *name : {direction.canonical, direction.alias}) {
            gfx->setBackgroundColor(Color(0.04f, 0.05f, 0.06f, 1.f));
            gfx->begin3DFrame();
            REQUIRE(gfx->had3DThisFrame());
            gfx->setMesh3DViewProj(projection * glm::lookAtRH(direction.eye, direction.target, direction.up));
            gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, name, atlas, 1);
            RenderSystem::render(*gfx);
            const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
            REQUIRE(luma(mid) > 0.08f);
            if (name == direction.canonical)
                canonical = mid;
            else {
                REQUIRE(std::fabs(mid.r - canonical.r) < 0.01f);
                REQUIRE(std::fabs(mid.g - canonical.g) < 0.01f);
                REQUIRE(std::fabs(mid.b - canonical.b) < 0.01f);
            }
        }
    }
    win->close();
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

    // Near green wall at z=2, far blue wall at z=0; camera looks down -Z.
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            world->setVoxel(x, y, 0, 2);  // far blue
            world->setVoxel(x, y, 2, 1);  // near
        }
    world->remeshDirty();

    // Different atlas tiles distinguish occlusion from merely drawing any wall.
    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 200.f, true);
    Color withNear = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(withNear.g > withNear.r);
    REQUIRE(withNear.g > withNear.b);
    REQUIRE(withNear.g > 0.12f);

    // Remove near wall voxels.
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) world->setVoxel(x, y, 2, 0);
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 200.f, true);
    Color farOnly = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    // Removing the near wall must reveal the blue far wall.
    REQUIRE(farOnly.b > farOnly.r);
    REQUIRE(farOnly.b > farOnly.g);
    REQUIRE(farOnly.b > 0.08f);
    REQUIRE(luma(withNear) > 0.05f);
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

TEST_CASE("voxel.render.singleFaceManualDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 256, 192);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Large +Z quad covering much of the view.
    uint32_t packed = PackedRect::pack(0, 0, 0, 8, 8, 1).bits;
    Texture *atlas  = makeSolid(gfx, 50, 200, 80);

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(4.f, 4.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(4.f, 4.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(45.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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
    renderVoxelFrame(gfx, world.get(), atlas, 1, glm::vec3(-40.f, 16.f, 16.f), glm::vec3(-80.f, 16.f, 16.f), 200.f,
                     true);
    // Chunk at origin not in front of camera looking toward -X further.
    REQUIRE_EQ(world->getVisibleChunkCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(testSceneLinearColor(mid)) < 0.22f);
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

TEST_CASE("voxel.render.faceCullRectCountAboutHalf") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(40.f, 16.f, 16.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(16.f, 16.f, 16.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 200.f);
    const glm::mat4 vp   = proj * view;

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, false);
    CHECK_EQ(world->getVisibleRectCount(), 6);

    world->selectVisible(&vp[0][0], eye.x, eye.y, eye.z, 200.f, true);
    CHECK(world->getVisibleRectCount() >= 1);
    CHECK(world->getVisibleRectCount() <= 5);
    CHECK(world->getVisibleRectCount() < 6);
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

    uint32_t        packed = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;
    Texture        *atlas  = makeSolid(gfx, 80, 200, 220);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 3.f, 14.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 3.f, 1.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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

TEST_CASE("voxel.render.zeroCountAfterLargeDraw") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::vector<uint32_t> many;
    for (int i = 0; i < 64; ++i) many.push_back(PackedRect::pack(i % 8, 0, i / 8, 1, 1, 1).bits);
    Texture    *atlas = makeSolid(gfx, 200, 200, 200);
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
    Texture        *atlas = makeSolid(gfx, 240, 240, 250);
    const glm::vec3 eye(66.f, 2.f, 20.f);
    const glm::vec3 target(66.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 200.f, true);
    Color nearEnough = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(nearEnough) > 0.06f);

    renderVoxelFrame(gfx, world.get(), atlas, 1, eye, target, 5.f, true);
    Color culled = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(culled) < luma(nearEnough));
    REQUIRE(luma(testSceneLinearColor(culled)) < 0.22f);
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

TEST_CASE("voxel.render.thinRectanglesAcrossAxes") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    struct Rectangle {
        int         width, height;
        const char *direction;
        glm::vec3   eye, target, up;
        uint8_t     red, green, blue;
        Color       background;
        float       minLuma;
    };
    // The +Y face lies at y=1; aim at its center rather than the plane below it.
    const Rectangle cases[] = {
        {32,
         1,
         "posY",
         {16.f, 10.f, 16.f},
         {16.f, 1.f, 0.5f},
         {0, 0, -1},
         230,
         180,
         40,
         Color(0.05f, 0.06f, 0.08f, 1.f),
         0.05f},
        {1,
         32,
         "posX",
         {20.f, 16.f, 0.5f},
         {1.f, 16.f, 0.5f},
         {0, 1, 0},
         200,
         60,
         200,
         Color(0.05f, 0.05f, 0.07f, 1.f),
         0.04f},
    };
    for (const auto &input : cases) {
        std::printf("thin face=%s size=%dx%d\n", input.direction, input.width, input.height);
        const uint32_t packed = PackedRect::pack(0, 0, 0, input.width, input.height, 1).bits;
        Texture       *atlas  = makeSolid(gfx, input.red, input.green, input.blue);
        REQUIRE(atlas != nullptr);
        const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        const glm::mat4 vp     = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) *
                             glm::lookAtRH(input.eye, input.target, input.up);
        gfx->setBackgroundColor(input.background);
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, input.direction, atlas, 1);
        RenderSystem::render(*gfx);
        const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > input.minLuma);
        REQUIRE(mid.r > 0.1f);
        if (input.green > input.blue) {
            REQUIRE(mid.g > mid.b);
            REQUIRE(mid.r > mid.b);
        } else {
            REQUIRE(mid.b > mid.g);
            REQUIRE(mid.r > mid.g);
        }
    }
    win->close();
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
    REQUIRE_EQ(world->getVisibleChunkCount(), 0);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(luma(testSceneLinearColor(mid)) < 0.22f);
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
    Texture        *atlas = makeSolid(gfx, 230, 230, 240);
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

TEST_CASE("voxel.render.nearPlaneOccludesFarDifferentColor") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *red   = makeSolid(gfx, 240, 30, 30);
    Texture *green = makeSolid(gfx, 30, 240, 30);
    uint32_t nearR = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;
    uint32_t farG  = PackedRect::pack(0, 0, 0, 6, 6, 1).bits;

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(3.f, 3.f, 20.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(3.f, 3.f, 0.f), glm::vec3(0, 1, 0));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

    for (bool nearFirst : {false, true}) {
        gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        gfx->setMesh3DViewProj(vp);
        if (nearFirst) gfx->drawVoxelFaceInstances(&nearR, 1, 0.f, 0.f, 8.f, "posZ", red, 1);
        gfx->drawVoxelFaceInstances(&farG, 1, 0.f, 0.f, 0.f, "posZ", green, 1);
        if (!nearFirst) gfx->drawVoxelFaceInstances(&nearR, 1, 0.f, 0.f, 8.f, "posZ", red, 1);
        RenderSystem::render(*gfx);
        const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(nearFirst ? mid.g > mid.r : mid.r > mid.g);
        REQUIRE(nearFirst ? mid.g > 0.08f : mid.r > 0.08f);
        std::swap(red, green);  // Exercise both dominant colors, as the old stacked-depth case did.
    }
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
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(16.f, 0.f, 16.f), glm::vec3(0, 0, -1));
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
