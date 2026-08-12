#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "voxel/Chunk.h"
#include "voxel/FaceDir.h"
#include "voxel/Frustum.h"
#include "voxel/GreedyMesher.h"
#include "voxel/Voxel.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelRectDecode.h"
#include "voxel/VoxelWorld.h"

#include <algorithm>
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

TEST_CASE("voxel.decode.winding_matches_outward_normal") {
    for (int d = 0; d < faceDirCount(); ++d) {
        const FaceDir dir = FaceDir(d);
        const PackedRect r = PackedRect::pack(2, 3, 4, 5, 7, 9);
        const DecodedRect q = decodePackedRect(r, dir, 0.f, 0.f, 0.f, 16);
        CHECK(decodedWindingMatchesNormal(q));
        float nx, ny, nz;
        faceNormal(dir, nx, ny, nz);
        CHECK_EQ(q.normal[0], nx);
        CHECK_EQ(q.normal[1], ny);
        CHECK_EQ(q.normal[2], nz);
        CHECK_EQ(q.tex, 9);
        CHECK_EQ(q.width, 5.f);
        CHECK_EQ(q.height, 7.f);
    }
}

TEST_CASE("voxel.decode.posX_plane_and_extent") {
    const PackedRect r = PackedRect::pack(4, 5, 6, 1, 1, 1);
    const DecodedRect q = decodePackedRect(r, FaceDir::PosX, 10.f, 20.f, 30.f, 8);
    for (int i = 0; i < 4; ++i) CHECK_EQ(q.corners[i][0], 10.f + 5.f);
    float minY = q.corners[0][1], maxY = q.corners[0][1];
    float minZ = q.corners[0][2], maxZ = q.corners[0][2];
    for (int i = 1; i < 4; ++i) {
        minY = std::min(minY, q.corners[i][1]);
        maxY = std::max(maxY, q.corners[i][1]);
        minZ = std::min(minZ, q.corners[i][2]);
        maxZ = std::max(maxZ, q.corners[i][2]);
    }
    CHECK_EQ(minY, 20.f + 5.f);
    CHECK_EQ(maxY, 20.f + 6.f);
    CHECK_EQ(minZ, 30.f + 6.f);
    CHECK_EQ(maxZ, 30.f + 7.f);
}

TEST_CASE("voxel.decode.atlas_uv_for_tex_index") {
    const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, 5);
    const DecodedRect q = decodePackedRect(r, FaceDir::PosZ, 0, 0, 0, 4);
    const float tile = 0.25f;
    CHECK(std::fabs(q.uv[0][0] - (1.f * tile)) < 1e-5f);
    CHECK(std::fabs(q.uv[0][1] - (1.f * tile)) < 1e-5f);
    CHECK(std::fabs(q.uv[2][0] - (2.f * tile)) < 1e-5f);
    CHECK(std::fabs(q.uv[2][1] - (2.f * tile)) < 1e-5f);
}

TEST_CASE("voxel.greedy.internal_face_hidden") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->set(1, 1, 1, 1);
    chunk->set(2, 1, 1, 1);
    chunk->remesh();
    int posXAt1 = 0, negXAt2 = 0;
    for (const auto &r : chunk->faceRects(FaceDir::PosX)) {
        if (r.x() == 1 && r.y() == 1 && r.z() == 1) ++posXAt1;
    }
    for (const auto &r : chunk->faceRects(FaceDir::NegX)) {
        if (r.x() == 2 && r.y() == 1 && r.z() == 1) ++negXAt2;
    }
    CHECK_EQ(posXAt1, 0);
    CHECK_EQ(negXAt2, 0);
    CHECK(chunk->totalRectCount() <= 10);
    CHECK(chunk->totalRectCount() >= 6);
}

TEST_CASE("voxel.greedy.texture_split_blocks_merge") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int x = 0; x < 4; ++x) chunk->set(x, 0, 0, x < 2 ? 1 : 2);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 2);
    const auto &faces = chunk->faceRects(FaceDir::PosY);
    CHECK_EQ(faces[0].width() + faces[1].width(), 4);
    CHECK(faces[0].tex() != faces[1].tex());
}

TEST_CASE("voxel.greedy.hollow_shell_counts") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->fill(1);
    for (int z = 1; z < 31; ++z)
        for (int y = 1; y < 31; ++y)
            for (int x = 1; x < 31; ++x) chunk->set(x, y, z, 0);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosX), 2);
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegX), 2);
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 2);
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegY), 2);
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosZ), 2);
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegZ), 2);
    CHECK_EQ(chunk->totalRectCount(), 12);
}

TEST_CASE("voxel.world.face_cull_all_six_axes") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();

    struct Case {
        float ex, ey, ez;
        FaceDir keep;
        FaceDir drop;
    };
    const Case cases[] = {
        {40.f, 16.f, 16.f, FaceDir::PosX, FaceDir::NegX},
        {-8.f, 16.f, 16.f, FaceDir::NegX, FaceDir::PosX},
        {16.f, 40.f, 16.f, FaceDir::PosY, FaceDir::NegY},
        {16.f, -8.f, 16.f, FaceDir::NegY, FaceDir::PosY},
        {16.f, 16.f, 40.f, FaceDir::PosZ, FaceDir::NegZ},
        {16.f, 16.f, -8.f, FaceDir::NegZ, FaceDir::PosZ},
    };

    for (const auto &c : cases) {
        float view[16], proj[16], vp[16];
        if (std::fabs(c.ey - 16.f) > 20.f)
            lookAtRH(c.ex, c.ey, c.ez, 16.f, 16.f, 16.f, 0, 0, 1, view);
        else
            lookAtRH(c.ex, c.ey, c.ez, 16.f, 16.f, 16.f, 0, 1, 0, view);
        perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 200.f, proj);
        mul4(proj, view, vp);
        world->selectVisible(vp, c.ex, c.ey, c.ez, 200.f, true);

        bool sawKeep = false, sawDrop = false;
        for (int i = 0; i < world->getVisibleBatchCount(); ++i) {
            const FaceDir d = world->getVisibleBatch(i).dir;
            if (d == c.keep) sawKeep = true;
            if (d == c.drop) sawDrop = true;
        }
        CHECK(sawKeep);
        CHECK(!sawDrop);
    }
}

TEST_CASE("voxel.world.visible_batches_decode_on_chunk_origin") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Chunk *c = world->getOrCreateChunk(1, 2, 3);
    c->set(0, 0, 0, 4);
    world->remeshDirty();

    float view[16], proj[16], vp[16];
    const float eyeX = 32.f + 40.f, eyeY = 64.f + 0.5f, eyeZ = 96.f + 0.5f;
    lookAtRH(eyeX, eyeY, eyeZ, 32.5f, 64.5f, 96.5f, 0, 1, 0, view);
    perspectiveRH_ZO(50.f * 3.14159265f / 180.f, 1.f, 0.1f, 500.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, eyeX, eyeY, eyeZ, 500.f, true);

    CHECK(world->getVisibleBatchCount() >= 1);
    CHECK(world->getVisibleRectCount() >= 1);

    for (int i = 0; i < world->getVisibleBatchCount(); ++i) {
        const DrawBatch &b = world->getVisibleBatch(i);
        CHECK(b.chunk != nullptr);
        CHECK(b.packed != nullptr);
        CHECK(b.count > 0);
        CHECK_EQ(b.chunk->originX(), 32.f);
        CHECK_EQ(b.chunk->originY(), 64.f);
        CHECK_EQ(b.chunk->originZ(), 96.f);
        for (int j = 0; j < b.count; ++j) {
            const PackedRect r{b.packed[j]};
            const DecodedRect q =
                decodePackedRect(r, b.dir, b.chunk->originX(), b.chunk->originY(), b.chunk->originZ());
            CHECK(decodedWindingMatchesNormal(q));
            for (int k = 0; k < 4; ++k) {
                CHECK(q.corners[k][0] >= 32.f - 0.01f);
                CHECK(q.corners[k][0] <= 32.f + 33.f);
                CHECK(q.corners[k][1] >= 64.f - 0.01f);
                CHECK(q.corners[k][1] <= 64.f + 33.f);
                CHECK(q.corners[k][2] >= 96.f - 0.01f);
                CHECK(q.corners[k][2] <= 96.f + 33.f);
            }
        }
    }
}

TEST_CASE("voxel.world.multi_chunk_range_filter") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->getOrCreateChunk(5, 0, 0)->fill(1);
    world->remeshDirty();

    float view[16], proj[16], vp[16];
    lookAtRH(16.f, 16.f, 80.f, 16.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 400.f, proj);
    mul4(proj, view, vp);

    world->selectVisible(vp, 16.f, 16.f, 80.f, 80.f, false);
    CHECK_EQ(world->getVisibleChunkCount(), 1);
    int cx = -1, cy = -1, cz = -1;
    world->getVisibleChunkCoord(0, cx, cy, cz);
    CHECK_EQ(cx, 0);
    CHECK_EQ(cy, 0);
    CHECK_EQ(cz, 0);
}

TEST_CASE("voxel.world.face_cull_off_keeps_both_sides") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();

    float view[16], proj[16], vp[16];
    lookAtRH(40.f, 16.f, 16.f, 16.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 200.f, proj);
    mul4(proj, view, vp);

    world->selectVisible(vp, 40.f, 16.f, 16.f, 200.f, false);
    bool sawPosX = false, sawNegX = false;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i) {
        if (world->getVisibleBatch(i).dir == FaceDir::PosX) sawPosX = true;
        if (world->getVisibleBatch(i).dir == FaceDir::NegX) sawNegX = true;
    }
    CHECK(sawPosX);
    CHECK(sawNegX);
    CHECK_EQ(world->getVisibleRectCount(), 6);
}

TEST_CASE("voxel.pack.bit_isolation") {
    const uint32_t a = PackedRect::pack(31, 0, 0, 1, 1, 0).bits;
    const uint32_t b = PackedRect::pack(0, 31, 0, 1, 1, 0).bits;
    const uint32_t c = PackedRect::pack(0, 0, 31, 1, 1, 0).bits;
    const uint32_t d = PackedRect::pack(0, 0, 0, 32, 1, 0).bits;
    const uint32_t e = PackedRect::pack(0, 0, 0, 1, 32, 0).bits;
    const uint32_t f = PackedRect::pack(0, 0, 0, 1, 1, 127).bits;
    CHECK_EQ(PackedRect{a}.x(), 31);
    CHECK_EQ(PackedRect{a}.y(), 0);
    CHECK_EQ(PackedRect{b}.y(), 31);
    CHECK_EQ(PackedRect{b}.x(), 0);
    CHECK_EQ(PackedRect{c}.z(), 31);
    CHECK_EQ(PackedRect{d}.width(), 32);
    CHECK_EQ(PackedRect{e}.height(), 32);
    CHECK_EQ(PackedRect{f}.tex(), 127);
    CHECK_EQ(a & b, 0u);
}

TEST_CASE("voxel.frustum.aabb_corners") {
    float view[16], proj[16], vp[16];
    lookAtRH(0, 0, 5, 0, 0, 0, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 100.f, proj);
    mul4(proj, view, vp);
    const Frustum f = Frustum::fromViewProjColumnMajor(vp);
    CHECK(f.intersectsAABB(-1, -1, -1, 1, 1, 1));
    CHECK(!f.intersectsAABB(50, 50, 50, 60, 60, 60));
    CHECK(!f.intersectsAABB(-1, -1, 10, 1, 1, 20));
}
