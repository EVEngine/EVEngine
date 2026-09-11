#include "SceneColor.h"
#include "VoxelRenderFixtures.h"

static Color renderVoxelAoSample(Graphics *gfx, Texture *atlas, uint32_t ao) {
    const uint32_t  packed = PackedRect::pack(0, 0, 0, 4, 4, 1).bits;
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::vec3 eye(2.f, 8.f, 2.f);
    const glm::mat4 view = glm::lookAtRH(eye, glm::vec3(2.f, 0.f, 2.f), glm::vec3(0.f, 0.f, -1.f));
    const glm::mat4 proj = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f);
    gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
    gfx->begin3DFrame();
    if (gfx->had3DThisFrame()) {
        gfx->setMesh3DViewProj(proj * view);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, 1, &ao);
    }
    RenderSystem::render(*gfx);
    return gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
}

/** @brief Vertex AO must shade identically on Vulkan and WebGPU voxel pipelines. */
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
    // AO multiplies linear radiance; ACES and sRGB compress the displayed ratio.
    REQUIRE(luma(testSceneLinearColor(dark)) < luma(testSceneLinearColor(bright)) * 0.6f);
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
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    for (int size : {2, 4}) {
        std::printf("null atlas cube size=%d\n", size);
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        fillCube(world.get(), 0, 0, 0, size, 1);
        world->remeshDirty();
        const float center = float(size) * 0.5f;
        renderVoxelFrame(gfx, world.get(), nullptr, 1, glm::vec3(center, center, float(size * 2 + 4)),
                         glm::vec3(center), 100.f, true);
        const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > 0.05f);
        REQUIRE(mid.r > 0.2f);
        REQUIRE(mid.g > 0.2f);
        REQUIRE(mid.b > 0.2f);
    }
    win->close();
}

TEST_CASE("voxel.render.tilesPerRowAtlasSample") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    struct Sample {
        int     size;
        int     tile;
        uint8_t blue;
    };
    for (const auto sample : {Sample{3, 1, 200}, Sample{4, 2, 220}}) {
        std::printf("2x2 atlas tile=%d\n", sample.tile);
        const uint8_t px[16] = {200, 40, 40, 255, 40, 200, 40, 255, 40, 40, sample.blue, 255, 200, 200, 40, 255};
        Texture      *atlas  = gfx->newTexture(2, 2, px);
        REQUIRE(atlas != nullptr);
        std::unique_ptr<VoxelWorld> world(new VoxelWorld());
        fillCube(world.get(), 0, 0, 0, sample.size, sample.tile);
        world->remeshDirty();
        const float center = float(sample.size) * 0.5f;
        renderVoxelFrame(gfx, world.get(), atlas, 2, glm::vec3(center, center, float(sample.size * 2 + 4)),
                         glm::vec3(center), 100.f, true);
        const Color mid      = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        const float dominant = sample.tile == 1 ? mid.g : mid.b;
        const float other    = sample.tile == 1 ? mid.b : mid.g;
        REQUIRE(dominant > mid.r);
        REQUIRE(dominant > other);
        REQUIRE(dominant > 0.1f);
    }
    win->close();
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

TEST_CASE("voxel.render.atlasBoundaryIndices") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);
    struct Sample {
        int   columns;
        int   rows;
        int   tile;
        int   size;
        float eyeY;
    };
    // Index zero is valid in packed faces even though voxel zero means air.
    // Keep the first tile, a wide-atlas row edge, and the maximum packed index.
    for (const auto sample : {Sample{4, 4, 0, 4, 10.f}, Sample{32, 2, 31, 3, 8.f}, Sample{16, 8, 127, 4, 10.f}}) {
        std::printf("atlas columns=%d rows=%d tile=%d\n", sample.columns, sample.rows, sample.tile);
        std::vector<uint8_t> pixels(size_t(sample.columns * sample.rows * 4), 0);
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        // tilesPerRow divides both UV axes; rows is the texture's pixel height,
        // not an independently configurable number of logical tile rows.
        const int    pixelRow = (sample.tile / sample.columns) * sample.rows / sample.columns;
        const size_t target   = size_t((pixelRow * sample.columns + sample.tile % sample.columns) * 4);
        pixels[target]        = 220;
        pixels[target + 1]    = 40;
        pixels[target + 2]    = 40;
        Texture *atlas        = gfx->newTexture(sample.columns, sample.rows, pixels.data());
        REQUIRE(atlas != nullptr);
        gfx->setTextureSampler(atlas, TextureSampler::nearest());
        const uint32_t  packed = PackedRect::pack(0, 0, 0, sample.size, sample.size, sample.tile).bits;
        const float     center = float(sample.size) * 0.5f;
        const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
        const glm::mat4 view =
            glm::lookAtRH(glm::vec3(center, sample.eyeY, center), glm::vec3(center, 0.f, center), glm::vec3(0, 0, -1));
        const glm::mat4 vp = perspectiveVulkanRH_ZO(glm::radians(50.f), aspect, 0.1f, 100.f) * view;
        gfx->setBackgroundColor(Color(0.05f, 0.06f, 0.08f, 1.f));
        gfx->begin3DFrame();
        REQUIRE(gfx->had3DThisFrame());
        gfx->setMesh3DViewProj(vp);
        gfx->drawVoxelFaceInstances(&packed, 1, 0.f, 0.f, 0.f, "posY", atlas, sample.columns);
        RenderSystem::render(*gfx);
        const Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
        REQUIRE(luma(mid) > 0.04f);
        REQUIRE(mid.r > mid.g);
        REQUIRE(mid.r > mid.b);
        REQUIRE(mid.r > 0.08f);
    }
    win->close();
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
