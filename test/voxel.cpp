#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "voxel/Chunk.h"
#include "voxel/FaceDir.h"
#include "voxel/Frustum.h"
#include "voxel/GreedyMesher.h"
#include "voxel/Voxel.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelWorld.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace eve::voxel;

static void lookAtRH(float eyeX, float eyeY, float eyeZ, float cx, float cy, float cz, float ux,
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

static void perspectiveRH_ZO(float fovyRad, float aspect, float zNear, float zFar, float out[16]) {
    const float f = 1.f / std::tan(fovyRad * 0.5f);
    std::memset(out, 0, sizeof(float) * 16);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = zFar / (zNear - zFar);
    out[11] = -1.f;
    out[14] = (zFar * zNear) / (zNear - zFar);
}

static void mul4(const float a[16], const float b[16], float out[16]) {
    float t[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    std::memcpy(out, t, sizeof(t));
}

TEST_CASE("voxel.module.name") {
    auto *mod = Voxel::create();
    CHECK_EQ(mod->getName(), std::string("Voxel"));
    CHECK_EQ(Voxel::create(), mod);
    CHECK_EQ(mod->getChunkSize(), 32);
}

TEST_CASE("voxel.pack.roundtrip") {
    Voxel *mod = Voxel::create();
    const uint32_t bits = mod->packRect(3, 7, 11, 5, 9, 42);
    CHECK_EQ(mod->unpackRectX(bits), 3);
    CHECK_EQ(mod->unpackRectY(bits), 7);
    CHECK_EQ(mod->unpackRectZ(bits), 11);
    CHECK_EQ(mod->unpackRectWidth(bits), 5);
    CHECK_EQ(mod->unpackRectHeight(bits), 9);
    CHECK_EQ(mod->unpackRectTex(bits), 42);

    const uint32_t full = PackedRect::pack(31, 31, 31, 32, 32, 127).bits;
    CHECK_EQ(PackedRect{full}.x(), 31);
    CHECK_EQ(PackedRect{full}.width(), 32);
    CHECK_EQ(PackedRect{full}.height(), 32);
    CHECK_EQ(PackedRect{full}.tex(), 127);
    CHECK_EQ(sizeof(PackedRect), size_t(4));
}

TEST_CASE("voxel.greedy.solid_cube") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->fill(1);
    chunk->remesh();

    CHECK_EQ(chunk->faceRectCount(FaceDir::PosX), 1);
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegX), 1);
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegY), 1);
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosZ), 1);
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegZ), 1);
    CHECK_EQ(chunk->totalRectCount(), 6);

    const PackedRect &top = chunk->faceRects(FaceDir::PosY)[0];
    CHECK_EQ(top.width(), 32);
    CHECK_EQ(top.height(), 32);
    CHECK_EQ(top.tex(), 1);
    CHECK_EQ(top.y(), 31);
}

TEST_CASE("voxel.greedy.single_voxel") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->set(4, 5, 6, 3);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 6);
    for (int i = 0; i < faceDirCount(); ++i) {
        CHECK_EQ(chunk->faceRectCount(FaceDir(i)), 1);
        const PackedRect &r = chunk->faceRects(FaceDir(i))[0];
        CHECK_EQ(r.tex(), 3);
        CHECK_EQ(r.width(), 1);
        CHECK_EQ(r.height(), 1);
    }
}

TEST_CASE("voxel.greedy.flat_slab_merges") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 8; ++x) chunk->set(x, 0, z, 2);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
    const PackedRect &top = chunk->faceRects(FaceDir::PosY)[0];
    CHECK_EQ(top.x(), 0);
    CHECK_EQ(top.y(), 0);
    CHECK_EQ(top.z(), 0);
    CHECK_EQ(top.width(), 8);
    CHECK_EQ(top.height(), 4);
    CHECK_EQ(top.tex(), 2);
}

TEST_CASE("voxel.world.selectVisible.face_and_range") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Chunk *c = world->getOrCreateChunk(0, 0, 0);
    c->fill(1);
    world->remeshDirty();

    const float eyeX = 40.f, eyeY = 16.f, eyeZ = 16.f;
    float view[16], proj[16], vp[16];
    lookAtRH(eyeX, eyeY, eyeZ, 16.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 200.f, proj);
    mul4(proj, view, vp);

    world->selectVisible(vp, eyeX, eyeY, eyeZ, 100.f, true);
    CHECK(world->getVisibleChunkCount() >= 1);
    CHECK(world->getVisibleBatchCount() >= 1);

    bool sawNegX = false;
    bool sawPosX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i) {
        const DrawBatch &b = world->getVisibleBatch(i);
        if (b.dir == FaceDir::NegX) sawNegX = true;
        if (b.dir == FaceDir::PosX) sawPosX = true;
    }
    CHECK(sawPosX);
    CHECK(!sawNegX);

    world->selectVisible(vp, eyeX, eyeY, eyeZ, 1.f, true);
    CHECK_EQ(world->getVisibleChunkCount(), 0);
    CHECK_EQ(world->getVisibleBatchCount(), 0);
}

TEST_CASE("voxel.world.frustum_rejects_behind") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(10, 0, 0)->fill(1);
    world->remeshDirty();

    float view[16], proj[16], vp[16];
    lookAtRH(0.f, 16.f, 16.f, -10.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(45.f * 3.14159265f / 180.f, 1.f, 0.1f, 50.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 0.f, 16.f, 16.f, 500.f, false);
    CHECK_EQ(world->getVisibleChunkCount(), 0);
}

TEST_CASE("voxel.world.setVoxel_cross_chunk") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(33, 1, 2, 7);
    CHECK_EQ(int(world->getVoxel(33, 1, 2)), 7);
    CHECK(world->hasChunk(1, 0, 0));
    Chunk *c = world->getChunk(1, 0, 0);
    CHECK(c != nullptr);
    CHECK_EQ(int(c->get(1, 1, 2)), 7);
}
