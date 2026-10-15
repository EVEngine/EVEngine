#include <cstdio>
#include <memory>
#include <string>
#include "SceneColor.h"
#include "VoxelRenderFixtures.h"
#include "voxel/Chunk.h"
#include "voxel/FaceDir.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// voxel_face_dir.cpp
using namespace eve::voxel;

TEST_CASE("voxel.faceDir.name_aliases") {
    struct Direction {
        FaceDir     direction;
        const char *canonical;
        const char *alias;
    };
    const Direction directions[] = {{FaceDir::PosX, "posX", "+x"}, {FaceDir::NegX, "negX", "-x"},
                                    {FaceDir::PosY, "posY", "+y"}, {FaceDir::NegY, "negY", "-y"},
                                    {FaceDir::PosZ, "posZ", "+z"}, {FaceDir::NegZ, "negZ", "-z"}};
    for (const auto &expected : directions) {
        for (const char *name : {expected.canonical, expected.alias}) {
            FaceDir    actual{};
            const bool parsed = faceDirFromName(name, actual);
            REQUIRE(parsed);
            REQUIRE(actual == expected.direction);
        }
        REQUIRE(std::string(faceDirName(expected.direction)) == expected.canonical);
    }
    for (const char *invalid : {"forward", "", "+", "pos", "+w", "PosX", "x+"}) {
        FaceDir    actual{};
        const bool parsed = faceDirFromName(invalid, actual);
        REQUIRE(!parsed);
    }
}

// voxel_mesher_shapes.cpp
using namespace eve::voxel;

TEST_CASE("voxel.greedy.solid_cube") {
    for (const uint8_t texture : {uint8_t(1), uint8_t(7)}) {
        std::printf("solid cube texture=%u\n", unsigned(texture));
        std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
        chunk->fill(texture);
        chunk->remesh();
        REQUIRE_EQ(chunk->totalRectCount(), 6);
        for (int i = 0; i < faceDirCount(); ++i) {
            std::printf("face=%d\n", i);
            const FaceDir direction = FaceDir(i);
            REQUIRE_EQ(chunk->faceRectCount(direction), 1);
            const PackedRect &rect = chunk->faceRects(direction)[0];
            REQUIRE_EQ(rect.width(), 32);
            REQUIRE_EQ(rect.height(), 32);
            REQUIRE_EQ(rect.tex(), texture);
        }
        REQUIRE_EQ(chunk->faceRects(FaceDir::PosY)[0].y(), 31);
    }
}

TEST_CASE("voxel.greedy.stripesMergeAlongBothAxes") {
    struct Stripe {
        int     x, z;
        int     stepX, stepZ;
        int     length;
        uint8_t texture;
        int     width, height;
    };
    for (const Stripe stripe : {Stripe{0, 3, 1, 0, 16, 4, 16, 1}, Stripe{5, 0, 0, 1, 12, 2, 1, 12}}) {
        std::printf("stripe step=(%d,%d) length=%d\n", stripe.stepX, stripe.stepZ, stripe.length);
        std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
        for (int i = 0; i < stripe.length; ++i)
            chunk->set(stripe.x + i * stripe.stepX, 0, stripe.z + i * stripe.stepZ, stripe.texture);
        chunk->remesh();
        REQUIRE_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
        const PackedRect &top = chunk->faceRects(FaceDir::PosY)[0];
        REQUIRE_EQ(top.width(), stripe.width);
        REQUIRE_EQ(top.height(), stripe.height);
        REQUIRE_EQ(top.x(), stripe.x);
        REQUIRE_EQ(top.y(), 0);
        REQUIRE_EQ(top.z(), stripe.z);
        REQUIRE_EQ(top.tex(), stripe.texture);
    }
}

// voxel_render.cpp


// voxel_render_scenes.cpp
[[maybe_unused]] static uint32_t voxelHash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

[[maybe_unused]] static float voxelH21(int x, int z) {
    return float(voxelHash(uint32_t(x) * 0x9E3779B9u ^ uint32_t(z) * 0x85EBCA6Bu) & 0xFFFFu) / 65535.f;
}

[[maybe_unused]] static float voxelValueNoise(float x, float z) {
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

[[maybe_unused]] static float voxelFbm(float x, float z) {
    float sum = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int i = 0; i < 4; ++i) {
        sum += amp * voxelValueNoise(x * freq, z * freq);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return sum / std::max(norm, 1e-4f);
}

[[maybe_unused]] static Texture *makeEarthAtlas(Graphics *gfx) {
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

[[maybe_unused]] static void fillBox(VoxelWorld *w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t tex) {
    for (int z = z0; z < z1; ++z)
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) w->setVoxel(x, y, z, tex);
}

[[maybe_unused]] static void fillHollowBox(VoxelWorld *w, int x0, int y0, int z0, int x1, int y1, int z1, uint8_t tex) {
    fillBox(w, x0, y0, z0, x1, y1, z1, tex);
    if (x1 - x0 > 2 && y1 - y0 > 1 && z1 - z0 > 2) fillBox(w, x0 + 1, y0, z0 + 1, x1 - 1, y1, z1 - 1, 0);
}

[[maybe_unused]] static void placeTree(VoxelWorld *w, int x, int groundY, int z) {
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

[[maybe_unused]] static void placeHouse(VoxelWorld *w, int x, int z, int wdt, int dpt, int hgt, uint8_t wall, uint8_t roof) {
    fillHollowBox(w, x, 1, z, x + wdt, 1 + hgt, z + dpt, wall);
    fillBox(w, x, 1 + hgt, z, x + wdt, 1 + hgt + 1, z + dpt, roof);
    const int ridgeZ = z + dpt / 2;
    for (int i = 0; i < wdt; ++i) w->setVoxel(x + i, 2 + hgt, ridgeZ, roof);
}

[[maybe_unused]] static std::string voxelSceneOutDir() { return std::string(EVENGINE_TEST_BINARY_DIR) + "/out/voxel_scenes"; }

[[maybe_unused]] static void renderVoxelScene(Graphics *gfx, VoxelWorld *world, Texture *atlas, const glm::vec3 &eye,
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

[[maybe_unused]] static void saveVoxelScenePng(Graphics *gfx, const std::string &name) {
    std::unique_ptr<ImageData> img(gfx->newImageData());
    REQUIRE(img.get() != nullptr);
    const std::string dir = voxelSceneOutDir();
    std::error_code   ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/" + name + ".png";
    REQUIRE(saveImagePng(*img, path));
    std::printf("voxel scene: %s\n", path.c_str());
}
