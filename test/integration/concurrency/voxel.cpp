#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/ByteData.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainSampler.h"
#include "voxel/Chunk.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/Frustum.h"
#include "voxel/GreedyMesher.h"
#include "voxel/Voxel.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelRectDecode.h"
#include "voxel/VoxelWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace eve::voxel;

[[maybe_unused]] static void lookAtRH(float eyeX, float eyeY, float eyeZ, float cx, float cy, float cz, float ux,
                     float uy, float uz, float out[16]) {
    float fx = cx - eyeX, fy = cy - eyeY, fz = cz - eyeZ;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl;
    fy /= fl;
    fz /= fl;
    float sx = fy * uz - fz * uy;
    float sy = fz * ux - fx * uz;
    float sz = fx * uy - fy * ux;
    float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl;
    sy /= sl;
    sz /= sl;
    float rx = sy * fz - sz * fy;
    float ry = sz * fx - sx * fz;
    float rz = sx * fy - sy * fx;
    out[0] = sx;
    out[1] = rx;
    out[2] = -fx;
    out[3] = 0;
    out[4] = sy;
    out[5] = ry;
    out[6] = -fy;
    out[7] = 0;
    out[8] = sz;
    out[9] = rz;
    out[10] = -fz;
    out[11] = 0;
    out[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
    out[13] = -(rx * eyeX + ry * eyeY + rz * eyeZ);
    out[14] = -(-fx * eyeX - fy * eyeY - fz * eyeZ);
    out[15] = 1;
}

[[maybe_unused]] static void perspectiveRH_ZO(float fovyRad, float aspect, float zNear, float zFar, float out[16]) {
    const float f = 1.f / std::tan(fovyRad * 0.5f);
    std::memset(out, 0, sizeof(float) * 16);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = zFar / (zNear - zFar);
    out[11] = -1.f;
    out[14] = (zFar * zNear) / (zNear - zFar);
}

[[maybe_unused]] static void mul4(const float a[16], const float b[16], float out[16]) {
    float t[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    std::memcpy(out, t, sizeof(t));
}


TEST_CASE("voxel.world.remeshDirty_parallel_equals_serial") {
    auto build = []() {
        auto w = std::make_unique<VoxelWorld>();
        for (int cz = -1; cz <= 1; ++cz)
            for (int cy = -1; cy <= 1; ++cy)
                for (int cx = -1; cx <= 1; ++cx) {
                    Chunk *c = w->getOrCreateChunk(cx, cy, cz);
                    c->fill(uint8_t(1 + ((cx * 7 + cy * 5 + cz * 3) & 3)));
                    // Carve holes (incl. on seams) to make neighbor culling exercise paths.
                    w->setVoxel(cx * 32 + 15, cy * 32 + 15, cz * 32 + 15, 0);
                    w->setVoxel(cx * 32 + 16, cy * 32 + 16, cz * 32 + 16, 0);
                }
        return w;
    };

    auto serial = build();
    auto parallel = build();
    CHECK_EQ(serial->remeshDirty(1), 27);
    CHECK_EQ(parallel->remeshDirty(4), 27);
    CHECK_EQ(parallel->remeshDirty(4), 0);  // nothing left dirty

    for (int cz = -1; cz <= 1; ++cz)
        for (int cy = -1; cy <= 1; ++cy)
            for (int cx = -1; cx <= 1; ++cx) {
                const Chunk *a = serial->getChunk(cx, cy, cz);
                const Chunk *b = parallel->getChunk(cx, cy, cz);
                REQUIRE(a != nullptr);
                REQUIRE(b != nullptr);
                CHECK_EQ(a->totalRectCount(), b->totalRectCount());
                for (int d = 0; d < faceDirCount(); ++d) {
                    const auto &fa = a->faceRects(FaceDir(d));
                    const auto &fb = b->faceRects(FaceDir(d));
                    const uint32_t *aoa = a->faceAOPackedData(FaceDir(d));
                    const uint32_t *aob = b->faceAOPackedData(FaceDir(d));
                    CHECK_EQ(int(fa.size()), int(fb.size()));
                    for (size_t i = 0; i < fa.size() && i < fb.size(); ++i)
                        CHECK_EQ(fa[i].bits, fb[i].bits);
                    if (aoa && aob)
                        for (size_t i = 0; i < fa.size() && i < fb.size(); ++i)
                            CHECK_EQ(aoa[i], aob[i]);
                }
            }
}


TEST_CASE("voxel.world.streamAround_create_budget_spreads_across_calls") {
    VoxelWorld world;
    world.setTerrainParams(42, 1, 2, 3, 8.f, 14.f, 1.f / 32.f);

    StreamStats first = world.streamAround(0, 0, 0, 2, {}, 4);
    CHECK(first.created + world.getInflightStreamCount() >= 4);
    CHECK(world.getChunkCount() <= 4);
    CHECK_EQ(first.pending + world.getChunkCount(), 33);

    for (int i = 0; i < 400 && world.getChunkCount() < 33; ++i) {
        world.streamAround(0, 0, 0, 2, {}, 8);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_EQ(world.getChunkCount(), 33);
    CHECK_EQ(world.getInflightStreamCount(), 0);
}
