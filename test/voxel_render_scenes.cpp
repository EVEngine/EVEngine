#include "VoxelRenderFixtures.h"

static uint32_t voxelHash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float voxelH21(int x, int z) {
    return float(voxelHash(uint32_t(x) * 0x9E3779B9u ^ uint32_t(z) * 0x85EBCA6Bu) & 0xFFFFu) / 65535.f;
}

static float voxelValueNoise(float x, float z) {
    const int   x0 = int(std::floor(x));
    const int   z0 = int(std::floor(z));
    const float tx = x - float(x0);
    const float tz = z - float(z0);
    const float sx = tx * tx * (3.f - 2.f * tx);
    const float sz = tz * tz * (3.f - 2.f * tz);
    const float a  = voxelH21(x0, z0);
    const float b  = voxelH21(x0 + 1, z0);
    const float c  = voxelH21(x0, z0 + 1);
    const float d  = voxelH21(x0 + 1, z0 + 1);
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
        {20, 22, 28},  {86, 158, 62}, {145, 96, 55},   {128, 128, 132}, {210, 186, 120}, {48, 96, 168},
        {118, 78, 42}, {46, 118, 48}, {236, 240, 245}, {96, 96, 100},   {168, 52, 42},   {210, 196, 168},
        {72, 48, 32},  {148, 72, 56}, {212, 168, 64},  {64, 68, 76},
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
    if (x1 - x0 > 2 && y1 - y0 > 1 && z1 - z0 > 2) fillBox(w, x0 + 1, y0, z0 + 1, x1 - 1, y1, z1 - 1, 0);
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

static void placeHouse(VoxelWorld *w, int x, int z, int wdt, int dpt, int hgt, uint8_t wall, uint8_t roof) {
    fillHollowBox(w, x, 1, z, x + wdt, 1 + hgt, z + dpt, wall);
    fillBox(w, x, 1 + hgt, z, x + wdt, 1 + hgt + 1, z + dpt, roof);
    const int ridgeZ = z + dpt / 2;
    for (int i = 0; i < wdt; ++i) w->setVoxel(x + i, 2 + hgt, ridgeZ, roof);
}

static std::string voxelSceneOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/voxel_scenes"; }

static void renderVoxelScene(Graphics *gfx, VoxelWorld *world, Texture *atlas, const glm::vec3 &eye,
                             const glm::vec3 &target, float farZ, float viewRange) {
    const float     aspect = float(std::max(1, gfx->getPixelWidth())) / float(std::max(1, gfx->getPixelHeight()));
    const glm::mat4 view   = glm::lookAtRH(eye, target, glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj   = perspectiveVulkanRH_ZO(glm::radians(48.f), aspect, 0.2f, farZ);
    const glm::mat4 vp     = proj * view;
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
    std::error_code   ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/" + name + ".png";
    REQUIRE(saveImagePng(*img, path));
    std::printf("voxel scene: %s\n", path.c_str());
}

TEST_CASE("voxel.render.sceneHillsIsland") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    constexpr int               kN   = 160;
    constexpr int               kSea = 6;
    for (int z = 0; z < kN; ++z) {
        for (int x = 0; x < kN; ++x) {
            const float n      = voxelFbm(float(x) * 0.035f, float(z) * 0.035f);
            const float ridge  = std::fabs(voxelFbm(float(x) * 0.018f + 40.f, float(z) * 0.018f) * 2.f - 1.f);
            const float h01    = n * 0.62f + (1.f - ridge) * 0.38f;
            const float cx     = (float(x) - 80.f) / 88.f;
            const float cz     = (float(z) - 80.f) / 88.f;
            float       island = std::clamp(1.12f - std::sqrt(cx * cx + cz * cz), 0.f, 1.f);
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
            int     gy  = 24;
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
    for (int i = 0; i < 2; ++i) renderVoxelScene(gfx, world.get(), atlas, eye, target, 420.f, 360.f);

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
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 960, 540);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    constexpr int               kN = 96;
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
    for (int i = 0; i < 2; ++i) renderVoxelScene(gfx, world.get(), atlas, eye, target, 320.f, 280.f);

    CHECK(world->getVisibleRectCount() > 40);
    const Color sky(0.42f, 0.58f, 0.78f, 1.f);
    CHECK(countNonBgSamples(gfx, sky) >= 20);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.06f);
    saveVoxelScenePng(gfx, "walled_town");
}

TEST_CASE("voxel.render.registryMultiFaceTex") {
    hideLeftover3D();
    eve::window::Window *win = nullptr;
    Graphics            *gfx = nullptr;
    openGfxWindow(win, gfx, 320, 240);
    tinyHud(gfx);
    gfx->setScreenReadbackEnabled(true);

    // grass：顶面绿色 tile(1)，其余面红色 tile(0)；方向性炉子：前脸蓝色 tile(2)
    CubeTypeRegistry reg;
    CubeType         grass;
    grass.name = "grass";
    for (int i = 0; i < 6; ++i) grass.faceTex[i] = 0;
    grass.faceTex[2] = 1;  // 顶 = green
    reg.add(grass);

    CubeType furnace;
    furnace.name = "furnace";
    for (int i = 0; i < 6; ++i) furnace.faceTex[i] = 0;
    furnace.faceTex[0]  = 2;  // +X = blue
    furnace.directional = true;
    reg.add(furnace);

    VoxelWorld world(reg);
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) world.setVoxelByName(x, y, z, "grass");
    world.remeshDirty();
    Chunk *c = world.getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    CHECK_EQ(c->faceRects(FaceDir::PosY)[0].tex(), 1);  // 顶面解析为 green
    CHECK_EQ(c->faceRects(FaceDir::PosX)[0].tex(), 0);  // 侧面 red

    // 4×4 图集：tile0=red, tile1=green, tile2=blue
    Texture        *atlas = makeTileAtlas(gfx, 4, 4);
    const glm::vec3 eye(1.5f, 1.5f, 10.f);
    const glm::vec3 target(1.5f, 1.5f, 1.5f);
    renderVoxelFrame(gfx, &world, atlas, 4, eye, target, 100.f, true);
    Color mid = gfx->getPixel(gfx->getWidth() / 2, gfx->getHeight() / 2);
    CHECK(luma(mid) > 0.04f);
}
