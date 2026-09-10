#include "VoxelRenderFixtures.h"

TEST_CASE("voxel.render.vertexAoDarkensPixels") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    Texture *atlas = makeSolid(gfx, 220, 220, 220);

    const Color bright = renderVoxelAoSample(gfx, atlas, 0xFFu);
    const Color dark   = renderVoxelAoSample(gfx, atlas, 0x00u);
    REQUIRE(luma(bright) > 0.25f);
    REQUIRE(luma(dark) < luma(bright) * 0.6f);
}

// NOTE: Graphics is a process-wide singleton — reuse one window for these cases.

TEST_CASE("voxel.render.atlasTexIndexTintVisible") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Texture id 1 (air is 0); solid red 1×1 atlas.
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();

    Texture        *red = makeSolid(gfx, 240, 40, 40);
    const glm::vec3 eye(0.5f, 0.5f, 6.f);
    const glm::vec3 target(0.5f, 0.5f, 0.5f);
    for (int i = 0; i < 3; ++i) renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 100.f, true);

    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(mid.r > mid.g);
    CHECK(mid.r > mid.b);
    CHECK(mid.r > 0.12f);
}

TEST_CASE("voxel.render.nullAtlasUsesWhite") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
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

TEST_CASE("voxel.render.tilesPerRowAtlasSample") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 2×2 atlas: index 1 (col1,row0) = pure green.
    uint8_t px[16] = {
        200, 40,  40,  255,  // 0 red
        40,  200, 40,  255,  // 1 green
        40,  40,  200, 255,  // 2 blue
        200, 200, 40,  255,  // 3 yellow
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

TEST_CASE("voxel.render.tilesPerRowBlueTile") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
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

TEST_CASE("voxel.render.alternatingAtlasFrames") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();

    Texture        *red  = makeSolid(gfx, 230, 40, 40);
    Texture        *blue = makeSolid(gfx, 40, 40, 230);
    const glm::vec3 eye(2.f, 2.f, 12.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), red, 1, eye, target, 100.f, true);
    Color cRed = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cRed.r > cRed.b);

    renderVoxelFrame(gfx, world.get(), blue, 1, eye, target, 100.f, true);
    Color cBlue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(cBlue.b > cBlue.r);
}

TEST_CASE("voxel.render.tilesPerRowZeroClamped") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 240, 180);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    uint32_t        packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    Texture        *atlas  = makeSolid(gfx, 210, 90, 50);
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 8.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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

TEST_CASE("voxel.render.atlas4x4_eachPaletteChannel") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    // tex 1 green, 2 blue, 3 yellow, 4 magenta, 5 cyan
    struct Expect {
        uint8_t tex;
        char    channel;  // 'r','g','b' dominant or 'y' r&g, 'm' r&b, 'c' g&b
    };
    const Expect cases[] = {{1, 'g'}, {2, 'b'}, {3, 'y'}, {4, 'm'}, {5, 'c'}};
    for (const Expect &e : cases) {
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        fillCube(world.get(), 0, 0, 0, 4, e.tex);
        world->remeshDirty();
        renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > 0.04f);
        if (e.channel == 'g') {
            REQUIRE(mid.g > 0.08f);
            REQUIRE(mid.g > mid.r);
            REQUIRE(mid.g > mid.b);
        } else if (e.channel == 'b') {
            REQUIRE(mid.b > mid.r);
            REQUIRE(mid.b > mid.g);
        } else if (e.channel == 'y') {
            REQUIRE(mid.r > mid.b);
            REQUIRE(mid.g > mid.b);
        } else if (e.channel == 'm') {
            REQUIRE(mid.r > mid.g);
            REQUIRE(mid.b > mid.g);
        } else if (e.channel == 'c') {
            REQUIRE(mid.g > mid.r);
            REQUIRE(mid.b > mid.r);
        }
    }
}

TEST_CASE("voxel.render.atlas8x8_highTexIndex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 8×8 = 64 tiles; tex 15 uses palette slot 15 → cyan-ish (40,200,200)
    Texture                    *atlas = makeTileAtlas(gfx, 8, 8);
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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // tilesPerRow=16, 8 rows → indices 0..127
    Texture                    *atlas = makeTileAtlas(gfx, 16, 8);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 63);
    world->remeshDirty();
    REQUIRE(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY) >= 1);
    CHECK_EQ(world->getChunk(0, 0, 0)->faceRects(FaceDir::PosY)[0].tex(), 63);

    const glm::vec3 eye(2.f, 10.f, 2.f);
    const glm::mat4 view   = glm::lookAtRH(eye, glm::vec3(2.f, 2.f, 2.f), glm::vec3(0, 0, -1));
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp     = proj * view;
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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);  // green
    world->remeshDirty();
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color green = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(green.g > green.r);

    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 2);  // blue
    world->remeshDirty();
    renderVoxelFrame(gfx, world.get(), atlas, 4, eye, target, 100.f, true);
    Color blue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(blue.b > blue.g);
    REQUIRE(blue.b > green.b);
}

TEST_CASE("voxel.render.multiTexStripesDoNotMerge") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    // Horizontal stripes of alternating tex 1 and 2 on a slab.
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x) world->setVoxel(x, 0, z, (z % 2) ? 1 : 2);
    world->remeshDirty();
    Chunk *c = world->getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    CHECK(c->faceRectCount(FaceDir::PosY) >= 4);

    const glm::vec3 eye(4.f, 12.f, 4.f);
    const glm::mat4 view   = glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 0, -1));
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp     = proj * view;
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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    // Two quads side by side: tex1 green @ x=0, tex2 blue @ origin shifted.
    uint32_t green = PackedRect::pack(0, 0, 0, 3, 3, 1).bits;
    uint32_t blue  = PackedRect::pack(0, 0, 0, 3, 3, 2).bits;

    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture        *atlas  = makeTileAtlas(gfx, 16, 8);
    uint32_t        packed = PackedRect::pack(0, 0, 0, 4, 4, 127).bits;
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 10.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 360, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 3, 1);
    fillCube(world.get(), 5, 0, 0, 3, 2);
    fillCube(world.get(), 0, 0, 5, 3, 3);
    fillCube(world.get(), 5, 0, 5, 3, 4);
    world->remeshDirty();
    // Four different top-face textures → at least 4 PosY rects.
    CHECK(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosY) >= 4);

    const glm::vec3 eye(4.f, 16.f, 4.f);
    const glm::mat4 view   = glm::lookAtRH(eye, glm::vec3(4.f, 0.f, 4.f), glm::vec3(0, 0, -1));
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(55.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp     = proj * view;
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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // Packed tex 0 is a valid atlas index even though voxel air is also 0.
    Texture        *atlas  = makeTileAtlas(gfx, 4, 4);
    uint32_t        packed = PackedRect::pack(0, 0, 0, 4, 4, 0).bits;  // tile 0 = red
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 10.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
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

TEST_CASE("voxel.render.twoChunksDifferentAtlasTiles") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture *atlas = makeTileAtlas(gfx, 4, 4);
    gfx->setTextureSampler(atlas, TextureSampler::nearest());
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);   // green
    fillCube(world.get(), 32, 0, 0, 4, 2);  // blue, next chunk
    world->remeshDirty();
    REQUIRE_EQ(world->getChunkCount(), 2);

    // Look at green chunk.
    renderVoxelFrame(gfx, world.get(), atlas, 4, glm::vec3(2.f, 2.f, 14.f), glm::vec3(2.f, 2.f, 2.f), 100.f, true);
    Color green = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(green.g > green.b);

    // Look at blue chunk.
    renderVoxelFrame(gfx, world.get(), atlas, 4, glm::vec3(34.f, 2.f, 14.f), glm::vec3(34.f, 2.f, 2.f), 100.f, true);
    Color blue = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    REQUIRE(blue.b > blue.g);
}

TEST_CASE("voxel.render.atlas32wide_tex31") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture        *atlas  = makeTileAtlas(gfx, 32, 2);  // 64 tiles
    uint32_t        packed = PackedRect::pack(0, 0, 0, 3, 3, 31).bits;
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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

TEST_CASE("voxel.render.drawVisibleNullAtlasWhite") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 300, 220);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlasA = makeTileAtlas(gfx, 4, 4);
    Texture                    *atlasB = makeSolid(gfx, 40, 40, 230);
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    fillCube(world.get(), 0, 0, 0, 4, 1);
    world->remeshDirty();
    const glm::vec3 eye(2.f, 2.f, 14.f);
    const glm::vec3 target(2.f, 2.f, 2.f);

    for (int i = 0; i < 4; ++i) {
        renderVoxelFrame(gfx, world.get(), (i % 2) ? atlasB : atlasA, (i % 2) ? 1 : 4, eye, target, 100.f, true);
        Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        CHECK(luma(mid) > 0.04f);
    }
}

TEST_CASE("voxel.render.sparsePillarsMultiTex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    Texture                    *atlas = makeTileAtlas(gfx, 4, 4);
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

TEST_CASE("voxel.render.tilesPerRowMismatchStillDraws") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 280, 200);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // 4×4 atlas but claim tilesPerRow=8 — UVs still sample something opaque.
    Texture        *atlas  = makeTileAtlas(gfx, 4, 4);
    uint32_t        packed = PackedRect::pack(0, 0, 0, 3, 3, 1).bits;
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(1.5f, 8.f, 1.5f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(1.5f, 0.f, 1.5f), glm::vec3(0, 0, -1));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    const glm::mat4 vp   = proj * view;

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
