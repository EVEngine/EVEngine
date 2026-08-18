#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
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
    CubeTypeRegistry *types = mod->newCubeTypes();
    REQUIRE(types != nullptr);
    CHECK_EQ(types->count(), 0);
    CHECK_EQ(types->variantCount(), 0);
    delete types;
    VoxelWorld *world = mod->newWorld();
    REQUIRE(world != nullptr);
    CHECK_EQ(world->getChunkCount(), 0);
    delete world;
}

TEST_CASE("voxel.pack.roundtrip") {
    const uint32_t bits = PackedRect::pack(3, 7, 11, 5, 9, 42).bits;
    CHECK_EQ(PackedRect{bits}.x(), 3);
    CHECK_EQ(PackedRect{bits}.y(), 7);
    CHECK_EQ(PackedRect{bits}.z(), 11);
    CHECK_EQ(PackedRect{bits}.width(), 5);
    CHECK_EQ(PackedRect{bits}.height(), 9);
    CHECK_EQ(PackedRect{bits}.tex(), 42);

    const uint32_t full = PackedRect::pack(31, 31, 31, 32, 32, 127).bits;
    CHECK_EQ(PackedRect{full}.x(), 31);
    CHECK_EQ(PackedRect{full}.width(), 32);
    CHECK_EQ(PackedRect{full}.height(), 32);
    CHECK_EQ(PackedRect{full}.tex(), 127);
    CHECK_EQ(sizeof(PackedRect), size_t(4));

    // Field isolation / bit disjointness: each field must survive on its own.
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

    // Minimum width/height encode zero in their bit fields.
    const PackedRect min = PackedRect::pack(0, 0, 0, 1, 1, 0);
    CHECK_EQ(min.width(), 1);
    CHECK_EQ(min.height(), 1);
    CHECK_EQ((min.bits >> 15) & 31u, 0u);
    CHECK_EQ((min.bits >> 20) & 31u, 0u);

    // tex field is independent of xyz across its full range.
    for (int tex = 0; tex <= 127; tex += 17) {
        const PackedRect t = PackedRect::pack(3, 5, 7, 2, 4, tex);
        CHECK_EQ(t.x(), 3);
        CHECK_EQ(t.y(), 5);
        CHECK_EQ(t.z(), 7);
        CHECK_EQ(t.width(), 2);
        CHECK_EQ(t.height(), 4);
        CHECK_EQ(t.tex(), tex);
    }

    // Out-of-range inputs are masked to the field width, not rejected.
    const PackedRect masked = PackedRect::pack(33, 40, 50, 40, 50, 200);
    CHECK_EQ(masked.x(), 1);    // 33 & 31
    CHECK_EQ(masked.y(), 8);    // 40 & 31
    CHECK_EQ(masked.z(), 18);   // 50 & 31
    CHECK_EQ(masked.width(), 8);   // (40-1)&31 + 1
    CHECK_EQ(masked.height(), 18); // (49&31)+1
    CHECK_EQ(masked.tex(), 72);    // 200 & 127
    const PackedRect neg = PackedRect::pack(100, -1, 64, 40, -3, 200);
    CHECK(neg.x() >= 0);
    CHECK(neg.x() <= 31);
    CHECK(neg.y() >= 0);
    CHECK(neg.y() <= 31);
    CHECK(neg.z() >= 0);
    CHECK(neg.z() <= 31);
    CHECK(neg.tex() >= 0);
    CHECK(neg.tex() <= 127);

    // Grid sweep of representative field values.
    for (int x = 0; x < 32; x += 7)
        for (int y = 0; y < 32; y += 11)
            for (int z = 0; z < 32; z += 13)
                for (int tex = 0; tex < 128; tex += 31) {
                    const PackedRect g = PackedRect::pack(x, y, z, 1 + (x % 5), 1 + (y % 7), tex);
                    CHECK_EQ(g.x(), x);
                    CHECK_EQ(g.y(), y);
                    CHECK_EQ(g.z(), z);
                    CHECK_EQ(g.tex(), tex);
                }
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
    // Chunk-based meshing: one 1×1 rect per face with packed coords/tex.
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
        CHECK_EQ(r.x(), 4);
        CHECK_EQ(r.y(), 5);
        CHECK_EQ(r.z(), 6);
    }

    // Max tex id and corner voxel (31,31,31) survive packing.
    std::unique_ptr<Chunk> corner(new Chunk(0, 0, 0));
    corner->set(31, 31, 31, 127);
    corner->remesh();
    CHECK_EQ(corner->totalRectCount(), 6);
    for (int i = 0; i < faceDirCount(); ++i) {
        const PackedRect &r = corner->faceRects(FaceDir(i))[0];
        CHECK_EQ(r.tex(), 127);
        CHECK_EQ(r.x(), 31);
        CHECK_EQ(r.y(), 31);
        CHECK_EQ(r.z(), 31);
    }

    // Direct GreedyMesher::meshChunk path.
    std::vector<uint8_t> voxels(size_t(32 * 32 * 32), 0);
    voxels[size_t(2 + 3 * 32 + 4 * 32 * 32)] = 7;
    std::vector<PackedRect> faces[6];
    GreedyMesher::meshChunk(voxels.data(), faces);
    CHECK_EQ(int(faces[int(FaceDir::PosX)].size()), 1);
    CHECK_EQ(int(faces[int(FaceDir::NegX)].size()), 1);
    CHECK_EQ(int(faces[int(FaceDir::PosY)].size()), 1);
    const uint32_t bits = faces[int(FaceDir::PosZ)][0].bits;
    CHECK_EQ(PackedRect{bits}.tex(), 7);
    CHECK_EQ(PackedRect{bits}.x(), 2);
    CHECK_EQ(PackedRect{bits}.y(), 3);
    CHECK_EQ(PackedRect{bits}.z(), 4);
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

TEST_CASE("voxel.decode.planes_extents_winding") {
    // Winding, outward normal, tex and size for every face direction.
    for (int d = 0; d < faceDirCount(); ++d) {
        const FaceDir dir = FaceDir(d);
        const PackedRect r = PackedRect::pack(2, 3, 4, 5, 7, 9);
        const DecodedRect q = decodePackedRect(r, dir, 0.f, 0.f, 0.f, 16);
        CHECK(decodedWindingMatchesNormal(q));
        CHECK(decodedSecondTriangleMatchesNormal(q));
        float nx, ny, nz;
        faceNormal(dir, nx, ny, nz);
        CHECK_EQ(q.normal[0], nx);
        CHECK_EQ(q.normal[1], ny);
        CHECK_EQ(q.normal[2], nz);
        CHECK_EQ(q.tex, 9);
        CHECK_EQ(q.width, 5.f);
        CHECK_EQ(q.height, 7.f);
    }

    // Plane position for all six faces (outward side = coord + 1).
    {
        const PackedRect r = PackedRect::pack(3, 4, 5, 2, 3, 1);
        struct PlaneCase {
            FaceDir dir;
            int axis;
            float expect;
        };
        const PlaneCase planes[] = {
            {FaceDir::PosX, 0, 4.f}, {FaceDir::NegX, 0, 3.f},
            {FaceDir::PosY, 1, 5.f}, {FaceDir::NegY, 1, 4.f},
            {FaceDir::PosZ, 2, 6.f}, {FaceDir::NegZ, 2, 5.f},
        };
        for (const auto &c : planes) {
            auto q = decodePackedRect(r, c.dir, 0, 0, 0);
            for (int i = 0; i < 4; ++i) CHECK_EQ(q.corners[i][c.axis], c.expect);
            CHECK(decodedWindingMatchesNormal(q));
        }
    }

    // Extents (width along the "secondary" axes) per direction.
    {
        const DecodedRect px = decodePackedRect(PackedRect::pack(4, 5, 6, 1, 1, 1),
                                                FaceDir::PosX, 10.f, 20.f, 30.f, 8);
        float minY = px.corners[0][1], maxY = px.corners[0][1];
        float minZ = px.corners[0][2], maxZ = px.corners[0][2];
        for (int i = 1; i < 4; ++i) {
            minY = std::min(minY, px.corners[i][1]);
            maxY = std::max(maxY, px.corners[i][1]);
            minZ = std::min(minZ, px.corners[i][2]);
            maxZ = std::max(maxZ, px.corners[i][2]);
        }
        CHECK_EQ(minY, 20.f + 5.f);
        CHECK_EQ(maxY, 20.f + 6.f);
        CHECK_EQ(minZ, 30.f + 6.f);
        CHECK_EQ(maxZ, 30.f + 7.f);
    }
    {
        const DecodedRect nx = decodePackedRect(PackedRect::pack(4, 5, 6, 2, 3, 1),
                                                FaceDir::NegX, 0, 0, 0);
        float minY = nx.corners[0][1], maxY = nx.corners[0][1];
        float minZ = nx.corners[0][2], maxZ = nx.corners[0][2];
        for (int i = 1; i < 4; ++i) {
            minY = std::min(minY, nx.corners[i][1]);
            maxY = std::max(maxY, nx.corners[i][1]);
            minZ = std::min(minZ, nx.corners[i][2]);
            maxZ = std::max(maxZ, nx.corners[i][2]);
        }
        CHECK_EQ(minY, 5.f);
        CHECK_EQ(maxY, 8.f);
        CHECK_EQ(minZ, 6.f);
        CHECK_EQ(maxZ, 8.f);
    }
    {
        const DecodedRect pz = decodePackedRect(PackedRect::pack(2, 3, 4, 5, 6, 1),
                                                FaceDir::PosZ, 0, 0, 0);
        float minX = pz.corners[0][0], maxX = pz.corners[0][0];
        float minY = pz.corners[0][1], maxY = pz.corners[0][1];
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, pz.corners[i][0]);
            maxX = std::max(maxX, pz.corners[i][0]);
            minY = std::min(minY, pz.corners[i][1]);
            maxY = std::max(maxY, pz.corners[i][1]);
        }
        CHECK_EQ(minX, 2.f);
        CHECK_EQ(maxX, 7.f);
        CHECK_EQ(minY, 3.f);
        CHECK_EQ(maxY, 9.f);

        const DecodedRect ny = decodePackedRect(PackedRect::pack(2, 3, 4, 5, 6, 1),
                                                FaceDir::NegY, 10, 20, 30);
        for (int i = 0; i < 4; ++i) CHECK_EQ(ny.corners[i][1], 23.f);
        CHECK(decodedWindingMatchesNormal(ny));
        CHECK(decodedSecondTriangleMatchesNormal(ny));
    }
    {
        const DecodedRect nz = decodePackedRect(PackedRect::pack(1, 2, 3, 4, 5, 6),
                                                FaceDir::NegZ, 0, 0, 0);
        float minX = nz.corners[0][0], maxX = nz.corners[0][0];
        float minY = nz.corners[0][1], maxY = nz.corners[0][1];
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, nz.corners[i][0]);
            maxX = std::max(maxX, nz.corners[i][0]);
            minY = std::min(minY, nz.corners[i][1]);
            maxY = std::max(maxY, nz.corners[i][1]);
        }
        CHECK_EQ(minX, 1.f);
        CHECK_EQ(maxX, 5.f);
        CHECK_EQ(minY, 2.f);
        CHECK_EQ(maxY, 7.f);
    }

    // Max 32×32 extent.
    {
        const DecodedRect mx = decodePackedRect(PackedRect::pack(0, 0, 0, 32, 32, 1),
                                                FaceDir::PosY, 0, 0, 0);
        CHECK(decodedWindingMatchesNormal(mx));
        for (int i = 0; i < 4; ++i) CHECK_EQ(mx.corners[i][1], 1.f);
        float minX = mx.corners[0][0], maxX = mx.corners[0][0];
        float minZ = mx.corners[0][2], maxZ = mx.corners[0][2];
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, mx.corners[i][0]);
            maxX = std::max(maxX, mx.corners[i][0]);
            minZ = std::min(minZ, mx.corners[i][2]);
            maxZ = std::max(maxZ, mx.corners[i][2]);
        }
        CHECK_EQ(minX, 0.f);
        CHECK_EQ(maxX, 32.f);
        CHECK_EQ(minZ, 0.f);
        CHECK_EQ(maxZ, 32.f);
    }
}

TEST_CASE("voxel.decode.uv_tiling") {
    // tex 5 with 4 tiles/row → tile col=1,row=1 (atlas lookup).
    {
        const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, 5);
        const DecodedRect q = decodePackedRect(r, FaceDir::PosZ, 0, 0, 0, 4);
        const float tile = 0.25f;
        CHECK(std::fabs(q.uv[0][0] - (1.f * tile)) < 1e-5f);
        CHECK(std::fabs(q.uv[0][1] - (1.f * tile)) < 1e-5f);
        CHECK(std::fabs(q.uv[2][0] - (2.f * tile)) < 1e-5f);
        CHECK(std::fabs(q.uv[2][1] - (2.f * tile)) < 1e-5f);
    }
    // One tile per row → v spans [tex, tex+1].
    {
        const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, 3);
        const DecodedRect q = decodePackedRect(r, FaceDir::PosZ, 0, 0, 0, 1);
        CHECK(std::fabs(q.uv[0][0] - 0.f) < 1e-5f);
        CHECK(std::fabs(q.uv[0][1] - 3.f) < 1e-5f);
        CHECK(std::fabs(q.uv[2][0] - 1.f) < 1e-5f);
        CHECK(std::fabs(q.uv[2][1] - 4.f) < 1e-5f);
    }
    // All four corners monotonic inside the tile.
    {
        const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, 5);
        const DecodedRect q = decodePackedRect(r, FaceDir::PosY, 0, 0, 0, 4);
        CHECK_EQ(q.uv[0][0], 0.25f);
        CHECK_EQ(q.uv[0][1], 0.25f);
        CHECK_EQ(q.uv[1][0], 0.5f);
        CHECK_EQ(q.uv[1][1], 0.25f);
        CHECK_EQ(q.uv[2][0], 0.5f);
        CHECK_EQ(q.uv[2][1], 0.5f);
        CHECK_EQ(q.uv[3][0], 0.25f);
        CHECK_EQ(q.uv[3][1], 0.5f);
    }
    // Sweep many tex indices across the atlas grid.
    {
        const int tiles = 8;
        for (int tex = 0; tex < 32; ++tex) {
            const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, tex);
            const DecodedRect q = decodePackedRect(r, FaceDir::PosY, 0, 0, 0, tiles);
            const float tile = 1.f / float(tiles);
            const float col = float(tex % tiles);
            const float row = float(tex / tiles);
            CHECK(std::fabs(q.uv[0][0] - col * tile) < 1e-5f);
            CHECK(std::fabs(q.uv[0][1] - row * tile) < 1e-5f);
            CHECK(std::fabs(q.uv[2][0] - (col + 1.f) * tile) < 1e-5f);
            CHECK(std::fabs(q.uv[2][1] - (row + 1.f) * tile) < 1e-5f);
            CHECK_EQ(q.tex, tex);
        }
    }
    // Max tex id with 16 tiles/row.
    {
        const PackedRect r = PackedRect::pack(0, 0, 0, 2, 2, 127);
        const DecodedRect q = decodePackedRect(r, FaceDir::PosX, 0, 0, 0, 16);
        CHECK(std::fabs(q.uv[0][0] - (15.f / 16.f)) < 1e-5f);
        CHECK(std::fabs(q.uv[0][1] - (7.f / 16.f)) < 1e-5f);
        CHECK(std::fabs(q.uv[2][0] - 1.f) < 1e-5f);
        CHECK(std::fabs(q.uv[2][1] - (8.f / 16.f)) < 1e-5f);
    }
    // tilesPerRow < 1 clamps to 1.
    {
        const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, 0);
        const DecodedRect q = decodePackedRect(r, FaceDir::PosY, 0, 0, 0, -3);
        CHECK_EQ(q.uv[0][0], 0.f);
        CHECK_EQ(q.uv[1][0], 1.f);
    }
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

    // Rect count equals the sum of batch counts, and face culling keeps
    // the visible batch set small.
    int sum = 0;
    for (int i = 0; i < world->getVisibleBatchCount(); ++i) sum += world->getVisibleBatch(i).count;
    CHECK_EQ(sum, world->getVisibleRectCount());
    CHECK(world->getVisibleBatchCount() >= 1);
    CHECK(world->getVisibleBatchCount() <= 5);
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

TEST_CASE("voxel.frustum.intersects") {
    // AABB in front of the camera intersects; out-of-frustum AABBs do not.
    {
        float view[16], proj[16], vp[16];
        lookAtRH(0, 0, 5, 0, 0, 0, 0, 1, 0, view);
        perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 100.f, proj);
        mul4(proj, view, vp);
        const Frustum f = Frustum::fromViewProjColumnMajor(vp);
        CHECK(f.intersectsAABB(-1, -1, -1, 1, 1, 1));
        CHECK(!f.intersectsAABB(50, 50, 50, 60, 60, 60));
        CHECK(!f.intersectsAABB(-1, -1, 10, 1, 1, 20));
    }
    // Huge AABB containing the camera.
    {
        float view[16], proj[16], vp[16];
        lookAtRH(0, 0, 0, 0, 0, -1, 0, 1, 0, view);
        perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 100.f, proj);
        mul4(proj, view, vp);
        const Frustum f = Frustum::fromViewProjColumnMajor(vp);
        CHECK(f.intersectsAABB(-50, -50, -50, 50, 50, 50));
    }
    // Far to the left of the frustum.
    {
        float view[16], proj[16], vp[16];
        lookAtRH(0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 0, 1, 0, view);
        perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 100.f, proj);
        mul4(proj, view, vp);
        const Frustum f = Frustum::fromViewProjColumnMajor(vp);
        CHECK(!f.intersectsAABB(-1000.f, -1.f, -1.f, -900.f, 1.f, 1.f));
        CHECK(f.intersectsAABB(-1.f, -1.f, -1.f, 1.f, 1.f, 1.f));
    }
    // Above the frustum and behind the camera.
    {
        float view[16], proj[16], vp[16];
        lookAtRH(0.f, 0.f, 5.f, 0.f, 0.f, 0.f, 0, 1, 0, view);
        perspectiveRH_ZO(50.f * 3.14159265f / 180.f, 1.f, 0.5f, 20.f, proj);
        mul4(proj, view, vp);
        const Frustum f = Frustum::fromViewProjColumnMajor(vp);
        CHECK(!f.intersectsAABB(-1.f, 500.f, -1.f, 1.f, 501.f, 1.f));
        CHECK(!f.intersectsAABB(-1.f, -1.f, -500.f, 1.f, 1.f, -400.f));
        CHECK(f.intersectsAABB(-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f));
    }
    // Tiny AABB on axis, and far off-axis rejection.
    {
        float view[16], proj[16], vp[16];
        lookAtRH(0.f, 0.f, 3.f, 0.f, 0.f, 0.f, 0, 1, 0, view);
        perspectiveRH_ZO(45.f * 3.14159265f / 180.f, 1.f, 0.1f, 50.f, proj);
        mul4(proj, view, vp);
        const Frustum f = Frustum::fromViewProjColumnMajor(vp);
        CHECK(f.intersectsAABB(-0.01f, -0.01f, -0.01f, 0.01f, 0.01f, 0.01f));
        CHECK(!f.intersectsAABB(100.f, -0.01f, -0.01f, 101.f, 0.01f, 0.01f));
    }
}

TEST_CASE("voxel.faceDir.name_aliases") {
    FaceDir d;
    CHECK(faceDirFromName("posX", d));
    CHECK_EQ(int(d), int(FaceDir::PosX));
    CHECK(faceDirFromName("+x", d));
    CHECK_EQ(int(d), int(FaceDir::PosX));
    CHECK(faceDirFromName("-y", d));
    CHECK_EQ(int(d), int(FaceDir::NegY));
    CHECK(faceDirFromName("posZ", d));
    CHECK_EQ(int(d), int(FaceDir::PosZ));
    CHECK(!faceDirFromName("forward", d));
    CHECK(!faceDirFromName("", d));
    CHECK_EQ(std::string(faceDirName(FaceDir::NegZ)), std::string("negZ"));
}

TEST_CASE("voxel.chunk.empty_and_dirty_flags") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    CHECK(chunk->isDirty());
    chunk->remesh();
    CHECK(!chunk->isDirty());
    CHECK_EQ(chunk->totalRectCount(), 0);
    chunk->set(0, 0, 0, 1);
    CHECK(chunk->isDirty());
    chunk->ensureMeshed();
    CHECK(!chunk->isDirty());
    CHECK_EQ(chunk->totalRectCount(), 6);
    chunk->clear();
    CHECK(chunk->isDirty());
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 0);

    // ensureMeshed is idempotent.
    chunk->set(0, 0, 0, 1);
    CHECK(chunk->isDirty());
    chunk->ensureMeshed();
    CHECK(!chunk->isDirty());
    const int n = chunk->totalRectCount();
    chunk->ensureMeshed();
    CHECK(!chunk->isDirty());
    CHECK_EQ(chunk->totalRectCount(), n);

    // fill() dirties; clear() dirties; both resolve on remesh.
    std::unique_ptr<Chunk> other(new Chunk(1, 2, 3));
    other->remesh();
    CHECK(!other->isDirty());
    other->fill(9);
    CHECK(other->isDirty());
    other->remesh();
    CHECK(!other->isDirty());
    other->clear();
    CHECK(other->isDirty());
    other->ensureMeshed();
    CHECK_EQ(other->totalRectCount(), 0);
}

TEST_CASE("voxel.chunk.full_layer_merges_to_one") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) chunk->set(x, 5, z, 3);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegY), 1);
    const PackedRect &top = chunk->faceRects(FaceDir::PosY)[0];
    CHECK_EQ(top.width(), 32);
    CHECK_EQ(top.height(), 32);
    CHECK_EQ(top.y(), 5);
    CHECK_EQ(top.tex(), 3);
}

TEST_CASE("voxel.greedy.column_merges_on_side_face") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    // Vertical column at x=0 → +X face should be one 1×8 rect (w along Z=1, h along Y=8).
    for (int y = 0; y < 8; ++y) chunk->set(0, y, 0, 2);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosX), 1);
    const PackedRect &r = chunk->faceRects(FaceDir::PosX)[0];
    CHECK_EQ(r.x(), 0);
    CHECK_EQ(r.z(), 0);
    CHECK_EQ(r.y(), 0);
    CHECK_EQ(r.width(), 1);
    CHECK_EQ(r.height(), 8);
}

TEST_CASE("voxel.greedy.L_shape_top_is_two_rects_or_merged_strip") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int x = 0; x < 4; ++x) chunk->set(x, 0, 0, 1);
    for (int z = 1; z < 4; ++z) chunk->set(0, 0, z, 1);
    chunk->remesh();
    // Top faces: greedy typically emits 2 rects (row + stub) or similar; never 7.
    CHECK(chunk->faceRectCount(FaceDir::PosY) >= 2);
    CHECK(chunk->faceRectCount(FaceDir::PosY) <= 4);
    int area = 0;
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) area += r.width() * r.height();
    CHECK_EQ(area, 7);
}

TEST_CASE("voxel.world.negative_coords_and_remove") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(-1, -1, -1, 5);
    CHECK_EQ(int(world->getVoxel(-1, -1, -1)), 5);
    CHECK(world->hasChunk(-1, -1, -1));
    Chunk *c = world->getChunk(-1, -1, -1);
    REQUIRE(c != nullptr);
    // Local coords within that chunk: -1 - (-1)*32 = 31
    CHECK_EQ(int(c->get(31, 31, 31)), 5);
    world->remeshDirty();
    CHECK(c->totalRectCount() > 0);

    world->removeChunk(-1, -1, -1);
    CHECK(!world->hasChunk(-1, -1, -1));
    CHECK_EQ(int(world->getVoxel(-1, -1, -1)), 0);
    CHECK_EQ(world->getChunkCount(), 0);

    // Removing a chunk makes voxel reads return air for that region.
    world->setVoxel(1, 1, 1, 2);
    CHECK(world->hasChunk(0, 0, 0));
    world->removeChunk(0, 0, 0);
    CHECK(!world->hasChunk(0, 0, 0));
    CHECK_EQ(int(world->getVoxel(1, 1, 1)), 0);
    CHECK_EQ(world->getChunkCount(), 0);
}

TEST_CASE("voxel.world.remeshDirty_counts_only_dirty") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->getOrCreateChunk(1, 0, 0)->fill(1);
    int n = world->remeshDirty();
    CHECK_EQ(n, 2);
    n = world->remeshDirty();
    CHECK_EQ(n, 0);
    n = world->remeshDirty();
    CHECK_EQ(n, 0);
    world->setVoxel(1, 1, 1, 2);
    n = world->remeshDirty();
    CHECK_EQ(n, 1);
    n = world->remeshDirty();
    CHECK_EQ(n, 0);
}

TEST_CASE("voxel.world.viewRange_nonpositive_disables_distance") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(20, 0, 0)->fill(1);  // center ~656
    world->remeshDirty();

    float view[16], proj[16], vp[16];
    // Look toward the far chunk so frustum can include it.
    lookAtRH(0.f, 16.f, 16.f, 640.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 2000.f, proj);
    mul4(proj, view, vp);

    world->selectVisible(vp, 0.f, 16.f, 16.f, 10.f, false);
    CHECK_EQ(world->getVisibleChunkCount(), 0);

    world->selectVisible(vp, 0.f, 16.f, 16.f, 0.f, false);  // disable range
    CHECK_EQ(world->getVisibleChunkCount(), 1);

    world->selectVisible(vp, 0.f, 16.f, 16.f, -1.f, false);
    CHECK_EQ(world->getVisibleChunkCount(), 1);
}

TEST_CASE("voxel.world.null_viewProj_clears_visible") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    float vp[16];
    std::memset(vp, 0, sizeof(vp));
    vp[0] = vp[5] = vp[10] = vp[15] = 1.f;
    world->selectVisible(vp, 16.f, 16.f, 80.f, 200.f, false);
    // May or may not be visible with identity-ish matrix; force clear:
    world->selectVisible(nullptr, 0, 0, 0, 100.f, true);
    CHECK_EQ(world->getVisibleBatchCount(), 0);
    CHECK_EQ(world->getVisibleChunkCount(), 0);
    CHECK_EQ(world->getVisibleRectCount(), 0);
}

TEST_CASE("voxel.world.getVisibleChunkCoord_oob") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    int cx = 9, cy = 9, cz = 9;
    world->getVisibleChunkCoord(-1, cx, cy, cz);
    CHECK_EQ(cx, 0);
    CHECK_EQ(cy, 0);
    CHECK_EQ(cz, 0);
    world->getVisibleChunkCoord(0, cx, cy, cz);
    CHECK_EQ(cx, 0);
}

TEST_CASE("voxel.world.clear_resets_everything") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    float view[16], proj[16], vp[16];
    lookAtRH(16, 16, 80, 16, 16, 16, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 200.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 16, 16, 80, 200, true);
    CHECK(world->getVisibleBatchCount() > 0);
    world->clear();
    CHECK_EQ(world->getChunkCount(), 0);
    CHECK_EQ(world->getVisibleBatchCount(), 0);
    CHECK_EQ(world->getVisibleChunkCount(), 0);
}

TEST_CASE("voxel.world.drawVisible_null_gfx_safe") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    float view[16], proj[16], vp[16];
    lookAtRH(40, 16, 16, 16, 16, 16, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 200.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 40, 16, 16, 200, true);
    world->drawVisible(nullptr, nullptr, 16);  // must not crash
    CHECK(world->getVisibleBatchCount() > 0);
}

TEST_CASE("voxel.decode.chunk_origin_offsets") {
    // Positive origin shifts every corner.
    {
        const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, 1);
        const DecodedRect q = decodePackedRect(r, FaceDir::PosZ, 100.f, 200.f, 300.f);
        for (int i = 0; i < 4; ++i) {
            CHECK(q.corners[i][0] >= 100.f);
            CHECK(q.corners[i][0] <= 101.f);
            CHECK(q.corners[i][1] >= 200.f);
            CHECK(q.corners[i][1] <= 201.f);
            CHECK_EQ(q.corners[i][2], 301.f);
        }
    }
    // Negative origin (negative chunk coords).
    {
        const PackedRect r = PackedRect::pack(0, 0, 0, 1, 1, 1);
        const DecodedRect q = decodePackedRect(r, FaceDir::PosX, -32.f, -64.f, -96.f);
        for (int i = 0; i < 4; ++i) {
            CHECK_EQ(q.corners[i][0], -31.f);  // originX + x+1
        }
        CHECK_EQ(q.corners[0][1], -64.f);
        CHECK_EQ(q.corners[0][2], -96.f);
    }
}

TEST_CASE("voxel.greedy.checkerboard_same_tex_still_many_faces") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x)
            if (((x + z) & 1) == 0) chunk->set(x, 0, z, 1);
    chunk->remesh();
    // 32 filled cells on y=0; top faces cannot fully merge into one.
    CHECK(chunk->faceRectCount(FaceDir::PosY) >= 8);
    int area = 0;
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) area += r.width() * r.height();
    CHECK_EQ(area, 32);
}

TEST_CASE("voxel.world.edit_then_remesh_updates_batches") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();
    float view[16], proj[16], vp[16];
    lookAtRH(0.5f, 0.5f, 8.f, 0.5f, 0.5f, 0.5f, 0, 1, 0, view);
    perspectiveRH_ZO(50.f * 3.14159265f / 180.f, 1.f, 0.1f, 100.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 0.5f, 0.5f, 8.f, 100.f, true);
    const int before = world->getVisibleRectCount();
    CHECK(before >= 1);

    // Fill a 4³ block — more surface area / larger merged faces still ≥ before.
    for (int z = 0; z < 4; ++z)
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) world->setVoxel(x, y, z, 1);
    world->remeshDirty();
    world->selectVisible(vp, 2.f, 2.f, 12.f, 100.f, true);
    CHECK(world->getVisibleRectCount() >= 1);
    // Solid 4³ has 6 faces; single voxel also 6 — merged counts equal, but batches exist.
    CHECK(world->getVisibleBatchCount() >= 1);
    (void)before;
}

TEST_CASE("voxel.chunk.oob_set_ignored") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->set(-1, 0, 0, 9);
    chunk->set(0, -1, 0, 9);
    chunk->set(0, 0, -1, 9);
    chunk->set(32, 0, 0, 9);
    chunk->set(0, 32, 0, 9);
    chunk->set(0, 0, 32, 9);
    CHECK_EQ(int(chunk->get(-1, 0, 0)), 0);
    CHECK_EQ(int(chunk->get(32, 0, 0)), 0);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 0);
}

TEST_CASE("voxel.chunk.facePackedData_null_when_empty") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->remesh();
    for (int i = 0; i < faceDirCount(); ++i) {
        CHECK(chunk->facePackedData(FaceDir(i)) == nullptr);
        CHECK_EQ(chunk->faceRectCount(FaceDir(i)), 0);
    }
    chunk->set(1, 1, 1, 1);
    chunk->remesh();
    for (int i = 0; i < faceDirCount(); ++i) {
        CHECK(chunk->facePackedData(FaceDir(i)) != nullptr);
        CHECK_EQ(chunk->faceRectCount(FaceDir(i)), 1);
    }
}

TEST_CASE("voxel.chunk.border_ring_merges") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    // Only the outer ring on y=0 (hollow square).
    for (int x = 0; x < 8; ++x) {
        chunk->set(x, 0, 0, 1);
        chunk->set(x, 0, 7, 1);
    }
    for (int z = 1; z < 7; ++z) {
        chunk->set(0, 0, z, 1);
        chunk->set(7, 0, z, 1);
    }
    chunk->remesh();
    int area = 0;
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) area += r.width() * r.height();
    CHECK_EQ(area, 8 * 2 + 6 * 2);  // 28
    CHECK(chunk->faceRectCount(FaceDir::PosY) >= 4);
}

TEST_CASE("voxel.greedy.stripe_horizontal_one_rect") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int x = 0; x < 16; ++x) chunk->set(x, 0, 3, 4);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].width(), 16);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].height(), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].z(), 3);
}

TEST_CASE("voxel.greedy.stripe_vertical_depth") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 12; ++z) chunk->set(5, 0, z, 2);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].width(), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].height(), 12);
}

TEST_CASE("voxel.world.many_chunks_visibility_subset") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    for (int cx = 0; cx < 4; ++cx)
        for (int cz = 0; cz < 4; ++cz) world->getOrCreateChunk(cx, 0, cz)->fill(1);
    CHECK_EQ(world->getChunkCount(), 16);
    world->remeshDirty();

    float view[16], proj[16], vp[16];
    // Eye near origin chunk, short range → only nearby chunks.
    lookAtRH(16.f, 16.f, 80.f, 16.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(50.f * 3.14159265f / 180.f, 1.f, 0.1f, 500.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 16.f, 16.f, 80.f, 50.f, false);
    CHECK(world->getVisibleChunkCount() >= 1);
    CHECK(world->getVisibleChunkCount() < 16);
}

TEST_CASE("voxel.world.remove_updates_visibility") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->getOrCreateChunk(1, 0, 0)->fill(1);
    world->remeshDirty();

    float view[16], proj[16], vp[16];
    lookAtRH(32.f, 16.f, 100.f, 32.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 400.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 32.f, 16.f, 100.f, 200.f, false);
    const int before = world->getVisibleChunkCount();
    CHECK(before >= 1);

    world->removeChunk(0, 0, 0);
    world->selectVisible(vp, 32.f, 16.f, 100.f, 200.f, false);
    CHECK(world->getVisibleChunkCount() <= before);
    CHECK(world->hasChunk(1, 0, 0));
    CHECK(!world->hasChunk(0, 0, 0));
}

TEST_CASE("voxel.world.getOrCreate_idempotent") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Chunk *a = world->getOrCreateChunk(3, 4, 5);
    Chunk *b = world->getOrCreateChunk(3, 4, 5);
    CHECK_EQ(a, b);
    CHECK_EQ(world->getChunkCount(), 1);
    CHECK_EQ(a->cx(), 3);
    CHECK_EQ(a->cy(), 4);
    CHECK_EQ(a->cz(), 5);
}

TEST_CASE("voxel.world.air_get_missing_chunk") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    CHECK_EQ(int(world->getVoxel(0, 0, 0)), 0);
    CHECK_EQ(int(world->getVoxel(1000, -50, 7)), 0);
    CHECK(!world->hasChunk(0, 0, 0));
}

TEST_CASE("voxel.registry.add_and_lookup") {
    CubeTypeRegistry reg;
    CubeType grass;
    grass.name = "grass";
    grass.faceTex[0] = 1;
    grass.faceTex[1] = 1;
    grass.faceTex[2] = 2;
    grass.faceTex[3] = 3;
    grass.faceTex[4] = 1;
    grass.faceTex[5] = 1;
    const uint8_t gid = reg.add(grass);
    CHECK(gid >= 1);
    CHECK_EQ(reg.count(), 1);
    CHECK_EQ(reg.variantCount(), 1);

    const CubeType *t = reg.find("grass");
    REQUIRE(t != nullptr);
    CHECK_EQ(t->id, gid);
    CHECK_EQ(int(t->faceTex[2]), 2);
    CHECK(!t->directional);

    CHECK(reg.find("nope") == nullptr);
    CHECK(reg.find(0) == nullptr);
    CHECK(reg.find(uint8_t(200)) == nullptr);

    reg.clear();
    CHECK_EQ(reg.count(), 0);
    CHECK_EQ(reg.variantCount(), 0);

    // faceTex 0 is a valid atlas tile (top face keeps tex 0 after meshing).
    {
        CubeTypeRegistry reg2;
        CubeType t;
        t.name = "zeroTop";
        t.faceTex[2] = 0;
        for (int i = 0; i < 6; ++i)
            if (i != 2) t.faceTex[i] = 1;
        reg2.add(t);

        VoxelWorld world(reg2);
        world.setVoxelByName(0, 0, 0, "zeroTop");
        world.remeshDirty();
        Chunk *c = world.getChunk(0, 0, 0);
        REQUIRE(c != nullptr);
        CHECK_EQ(c->faceRects(FaceDir::PosY)[0].tex(), 0);
        CHECK_EQ(c->faceRectCount(FaceDir::PosY), 1);
    }

    // Empty registry keeps the voxel id as the texture id.
    {
        VoxelWorld world2;
        world2.setVoxel(1, 2, 3, 7);
        world2.remeshDirty();
        Chunk *c2 = world2.getChunk(0, 0, 0);
        REQUIRE(c2 != nullptr);
        CHECK_EQ(c2->faceRects(FaceDir::PosY)[0].tex(), 7);
        CHECK_EQ(world2.getCubeTypeName(1, 2, 3), std::string(""));
        CHECK_EQ(int(world2.getCubeTypeTex(1, 2, 3, "posY")), 7);
    }
}

TEST_CASE("voxel.greedy.two_separated_slabs") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int x = 0; x < 4; ++x) chunk->set(x, 0, 0, 1);
    for (int x = 0; x < 4; ++x) chunk->set(x, 0, 5, 1);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 2);
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) {
        CHECK_EQ(r.width(), 4);
        CHECK_EQ(r.height(), 1);
    }
}

TEST_CASE("voxel.world.selectVisible_empty_world") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    float vp[16];
    std::memset(vp, 0, sizeof(vp));
    vp[0] = vp[5] = vp[10] = vp[15] = 1.f;
    world->selectVisible(vp, 0, 0, 5, 100, true);
    CHECK_EQ(world->getVisibleBatchCount(), 0);
    CHECK_EQ(world->getVisibleRectCount(), 0);
    CHECK_EQ(world->getChunkCount(), 0);
}

TEST_CASE("voxel.chunk.rawVoxels_matches_get") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->set(2, 3, 4, 11);
    chunk->set(10, 0, 0, 22);
    const uint8_t *raw = chunk->rawVoxels();
    CHECK_EQ(int(raw[2 + 3 * 32 + 4 * 32 * 32]), 11);
    CHECK_EQ(int(raw[10]), 22);
    CHECK_EQ(int(chunk->get(2, 3, 4)), 11);
}

TEST_CASE("voxel.world.cross_chunk_boundary_voxels") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(31, 0, 0, 1);
    world->setVoxel(32, 0, 0, 1);
    CHECK(world->hasChunk(0, 0, 0));
    CHECK(world->hasChunk(1, 0, 0));
    world->remeshDirty();
    // Each chunk treats the other as air at the seam → both expose a face on the boundary.
    CHECK(world->getChunk(0, 0, 0)->faceRectCount(FaceDir::PosX) >= 1);
    CHECK(world->getChunk(1, 0, 0)->faceRectCount(FaceDir::NegX) >= 1);
}

TEST_CASE("voxel.mesher.api_behavior") {
    uint8_t voxels[32 * 32 * 32];
    std::memset(voxels, 0, sizeof(voxels));
    voxels[0] = 1;

    // meshFace fills only the requested direction and appends (does not clear).
    std::vector<PackedRect> out;
    GreedyMesher::meshFace(voxels, FaceDir::PosY, out);
    CHECK_EQ(int(out.size()), 1);
    CHECK_EQ(out[0].tex(), 1);
    GreedyMesher::meshFace(voxels, FaceDir::PosY, out);
    CHECK_EQ(int(out.size()), 2);
    out.clear();
    GreedyMesher::meshFace(voxels, FaceDir::NegZ, out);
    CHECK_EQ(int(out.size()), 1);

    // meshChunk clears each face list, then emits one rect per face.
    std::vector<PackedRect> faces[6];
    for (int i = 0; i < 6; ++i) faces[i].push_back(PackedRect::pack(0, 0, 0, 1, 1, 99));
    GreedyMesher::meshChunk(voxels, faces);
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(int(faces[i].size()), 1);
        CHECK_EQ(faces[i][0].tex(), 1);
    }

    // Distinct textures stay distinct after a re-mesh.
    voxels[0] = 3;
    voxels[1] = 11;
    voxels[2] = 22;
    GreedyMesher::meshChunk(voxels, faces);
    const auto &top = faces[int(FaceDir::PosY)];
    CHECK_EQ(int(top.size()), 3);
    bool saw3 = false, saw11 = false, saw22 = false;
    for (const auto &r : top) {
        if (r.tex() == 3) saw3 = true;
        if (r.tex() == 11) saw11 = true;
        if (r.tex() == 22) saw22 = true;
    }
    CHECK(saw3);
    CHECK(saw11);
    CHECK(saw22);
}

TEST_CASE("voxel.greedy.remesh_replaces_previous_faces") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->fill(1);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 6);
    chunk->clear();
    chunk->set(0, 0, 0, 2);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 6);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].tex(), 2);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].width(), 1);
}

TEST_CASE("voxel.greedy.staircase_no_full_merge") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int i = 0; i < 6; ++i) chunk->set(i, i, 0, 1);
    chunk->remesh();
    // Each step has its own top face.
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 6);
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) {
        CHECK_EQ(r.width(), 1);
        CHECK_EQ(r.height(), 1);
    }
}

TEST_CASE("voxel.greedy.solid_3x3x3") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) chunk->set(x, y, z, 5);
    chunk->remesh();
    // Six outer faces, each a single 3×3 rect.
    for (int i = 0; i < faceDirCount(); ++i) {
        CHECK_EQ(chunk->faceRectCount(FaceDir(i)), 1);
        CHECK_EQ(chunk->faceRects(FaceDir(i))[0].width(), 3);
        CHECK_EQ(chunk->faceRects(FaceDir(i))[0].height(), 3);
        CHECK_EQ(chunk->faceRects(FaceDir(i))[0].tex(), 5);
    }
}

TEST_CASE("voxel.greedy.carve_center_increases_faces") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 3; ++z)
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) chunk->set(x, y, z, 1);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 6);
    chunk->set(1, 1, 1, 0);  // carve center
    chunk->remesh();
    // Outer 6 remain + inner cavity exposes 6 more faces.
    CHECK_EQ(chunk->totalRectCount(), 12);
}

TEST_CASE("voxel.greedy.stacked_textures_split_side_merge") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int y = 0; y < 4; ++y) chunk->set(0, y, 0, (y < 2) ? 1 : 2);
    chunk->remesh();
    // +X side: two rects (tex split), not one 1×4.
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosX), 2);
    int hSum = 0;
    for (const auto &r : chunk->faceRects(FaceDir::PosX)) {
        CHECK_EQ(r.width(), 1);
        hSum += r.height();
    }
    CHECK_EQ(hSum, 4);
}

TEST_CASE("voxel.chunk.worldAABB_matches_origin") {
    std::unique_ptr<Chunk> chunk(new Chunk(2, -1, 3));
    float minX, minY, minZ, maxX, maxY, maxZ;
    chunk->worldAABB(minX, minY, minZ, maxX, maxY, maxZ);
    CHECK_EQ(minX, 64.f);
    CHECK_EQ(minY, -32.f);
    CHECK_EQ(minZ, 96.f);
    CHECK_EQ(maxX, 96.f);
    CHECK_EQ(maxY, 0.f);
    CHECK_EQ(maxZ, 128.f);
    CHECK_EQ(chunk->originX(), 64.f);
    CHECK_EQ(chunk->originY(), -32.f);
    CHECK_EQ(chunk->originZ(), 96.f);
}

TEST_CASE("voxel.world.overwrite_tex_updates_packed") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(1, 1, 1, 3);
    world->remeshDirty();
    Chunk *c = world->getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    CHECK_EQ(c->faceRects(FaceDir::PosY)[0].tex(), 3);

    world->setVoxel(1, 1, 1, 9);
    world->remeshDirty();
    CHECK_EQ(c->faceRects(FaceDir::PosY)[0].tex(), 9);
}

TEST_CASE("voxel.world.ensureMeshed_via_selectVisible") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    Chunk *c = world->getOrCreateChunk(0, 0, 0);
    c->fill(1);
    CHECK(c->isDirty());
    float view[16], proj[16], vp[16];
    lookAtRH(16.f, 16.f, 80.f, 16.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 200.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 16.f, 16.f, 80.f, 200.f, false);
    CHECK(!c->isDirty());
    CHECK(c->totalRectCount() > 0);
}

TEST_CASE("voxel.world.three_axis_negative_chunks") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(-33, -1, -64, 4);
    CHECK(world->hasChunk(-2, -1, -2));
    CHECK_EQ(int(world->getVoxel(-33, -1, -64)), 4);
    Chunk *c = world->getChunk(-2, -1, -2);
    REQUIRE(c != nullptr);
    // local: wx - cx*32
    const int lx = -33 - (-2) * 32;  // -33 + 64 = 31
    const int ly = -1 - (-1) * 32;   // -1 + 32 = 31
    const int lz = -64 - (-2) * 32;  // -64 + 64 = 0
    CHECK_EQ(lx, 31);
    CHECK_EQ(ly, 31);
    CHECK_EQ(lz, 0);
    CHECK_EQ(int(c->get(lx, ly, lz)), 4);
}

TEST_CASE("voxel.faceDir.normals_are_unit_axis") {
    for (int i = 0; i < faceDirCount(); ++i) {
        float nx, ny, nz;
        faceNormal(FaceDir(i), nx, ny, nz);
        CHECK_EQ(nx * nx + ny * ny + nz * nz, 1.f);
        const int nonzero = (nx != 0.f) + (ny != 0.f) + (nz != 0.f);
        CHECK_EQ(nonzero, 1);
    }
}

TEST_CASE("voxel.faceDir.fromName_rejects_garbage") {
    FaceDir d = FaceDir::PosX;
    CHECK(!faceDirFromName("", d));
    CHECK(!faceDirFromName("PosX", d));  // case-sensitive
    CHECK(!faceDirFromName("x+", d));
    CHECK(!faceDirFromName("forward", d));
}

TEST_CASE("voxel.greedy.full_chunk_six_32x32") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->fill(7);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 6);
    for (int i = 0; i < faceDirCount(); ++i) {
        CHECK_EQ(chunk->faceRectCount(FaceDir(i)), 1);
        CHECK_EQ(chunk->faceRects(FaceDir(i))[0].width(), 32);
        CHECK_EQ(chunk->faceRects(FaceDir(i))[0].height(), 32);
        CHECK_EQ(chunk->faceRects(FaceDir(i))[0].tex(), 7);
    }
}

TEST_CASE("voxel.greedy.x_tunnel_hides_internal_yz") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    // Solid wall with a 1-cell tunnel along X through center.
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) {
                if (y == 2 && z == 2) continue;  // tunnel
                chunk->set(x, y, z, 1);
            }
    chunk->remesh();
    // Outer shell still present; tunnel adds internal faces on ±Y/±Z.
    CHECK(chunk->faceRectCount(FaceDir::PosY) >= 2);
    CHECK(chunk->faceRectCount(FaceDir::NegY) >= 2);
    CHECK(chunk->faceRectCount(FaceDir::PosZ) >= 2);
    CHECK(chunk->faceRectCount(FaceDir::NegZ) >= 2);
}

TEST_CASE("voxel.greedy.top_layer_y31") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x) chunk->set(x, 31, z, 3);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].y(), 31);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].width(), 4);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].height(), 4);
}

TEST_CASE("voxel.world.set_air_clears_and_remeshes") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(2, 2, 2, 5);
    world->remeshDirty();
    CHECK(world->getChunk(0, 0, 0)->totalRectCount() > 0);
    world->setVoxel(2, 2, 2, 0);
    CHECK(world->getChunk(0, 0, 0)->isDirty());
    world->remeshDirty();
    CHECK_EQ(world->getChunk(0, 0, 0)->totalRectCount(), 0);
    CHECK_EQ(int(world->getVoxel(2, 2, 2)), 0);
}

TEST_CASE("voxel.greedy.ring_in_layer_multiple_top_rects") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) {
            if (x == 0 || x == 4 || z == 0 || z == 4) chunk->set(x, 0, z, 1);
        }
    chunk->remesh();
    // Hollow ring: top faces are not a single 5×5.
    CHECK(chunk->faceRectCount(FaceDir::PosY) >= 4);
    int area = 0;
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) area += r.width() * r.height();
    CHECK_EQ(area, 5 * 5 - 3 * 3);
}

TEST_CASE("voxel.greedy.four_textures_in_a_row") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int x = 0; x < 4; ++x) chunk->set(x, 0, 0, uint8_t(x + 1));
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 4);
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) {
        CHECK_EQ(r.width(), 1);
        CHECK_EQ(r.height(), 1);
    }
}

TEST_CASE("voxel.greedy.same_tex_gap_no_merge_across_air") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->set(0, 0, 0, 5);
    chunk->set(2, 0, 0, 5);  // gap at x=1
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 2);
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) {
        CHECK_EQ(r.tex(), 5);
        CHECK_EQ(r.width(), 1);
    }
}

TEST_CASE("voxel.greedy.many_tex_ids_preserved") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int i = 1; i <= 16; ++i) chunk->set(i, 0, 0, uint8_t(i));
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 16);
    bool seen[128]{};
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) {
        CHECK(r.tex() >= 1);
        CHECK(r.tex() <= 16);
        seen[r.tex()] = true;
    }
    for (int i = 1; i <= 16; ++i) CHECK(seen[i]);
}

TEST_CASE("voxel.world.multi_tex_remesh_updates_all") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(0, 0, 0, 1);
    world->setVoxel(2, 0, 0, 2);
    world->setVoxel(4, 0, 0, 3);
    world->remeshDirty();
    Chunk *c = world->getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    CHECK_EQ(c->faceRectCount(FaceDir::PosY), 3);

    world->setVoxel(0, 0, 0, 7);
    world->setVoxel(2, 0, 0, 8);
    world->setVoxel(4, 0, 0, 9);
    world->remeshDirty();
    bool saw[128]{};
    for (const auto &r : c->faceRects(FaceDir::PosY)) saw[r.tex()] = true;
    CHECK(saw[7]);
    CHECK(saw[8]);
    CHECK(saw[9]);
    CHECK(!saw[1]);
}

TEST_CASE("voxel.greedy.eight_corners_of_chunk") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    const int corners[8][3] = {{0, 0, 0}, {31, 0, 0}, {0, 31, 0}, {31, 31, 0},
                               {0, 0, 31}, {31, 0, 31}, {0, 31, 31}, {31, 31, 31}};
    for (int i = 0; i < 8; ++i) chunk->set(corners[i][0], corners[i][1], corners[i][2], 1);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 8 * 6);
}

TEST_CASE("voxel.greedy.adjacent_same_tex_merges_width") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->set(0, 0, 0, 2);
    chunk->set(1, 0, 0, 2);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].width(), 2);
    CHECK_EQ(chunk->faceRects(FaceDir::PosY)[0].height(), 1);
}

TEST_CASE("voxel.greedy.vertical_stack_merges_side_height") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int y = 0; y < 5; ++y) chunk->set(0, y, 0, 3);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosX), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::PosX)[0].width(), 1);   // along Z
    CHECK_EQ(chunk->faceRects(FaceDir::PosX)[0].height(), 5);  // along Y
}

TEST_CASE("voxel.greedy.diagonal_plane_no_side_merge") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int i = 0; i < 8; ++i) chunk->set(i, 0, i, 1);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 8);
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) {
        CHECK_EQ(r.width(), 1);
        CHECK_EQ(r.height(), 1);
    }
}

TEST_CASE("voxel.greedy.fill_then_carve_layer") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    chunk->fill(1);
    chunk->remesh();
    CHECK_EQ(chunk->totalRectCount(), 6);
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) chunk->set(x, 16, z, 0);
    chunk->remesh();
    // Split solid → two slabs → more than 6 outer faces.
    CHECK(chunk->totalRectCount() > 6);
    CHECK(chunk->faceRectCount(FaceDir::PosY) >= 2);
}

TEST_CASE("voxel.world.selectVisible_replaces_previous") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->getOrCreateChunk(10, 0, 0)->fill(1);
    world->remeshDirty();
    float view[16], proj[16], vp[16];
    lookAtRH(16.f, 16.f, 48.f, 16.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 200.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 16.f, 16.f, 48.f, 80.f, true);
    const int first = world->getVisibleChunkCount();
    CHECK(first >= 1);
    bool sawOrigin = false;
    for (int i = 0; i < world->getVisibleChunkCount(); ++i) {
        int cx, cy, cz;
        world->getVisibleChunkCoord(i, cx, cy, cz);
        if (cx == 0 && cy == 0 && cz == 0) sawOrigin = true;
    }
    CHECK(sawOrigin);

    // Move eye near far chunk only (cx=10 → center x=336).
    lookAtRH(336.f, 16.f, 48.f, 336.f, 16.f, 16.f, 0, 1, 0, view);
    mul4(proj, view, vp);
    world->selectVisible(vp, 336.f, 16.f, 48.f, 80.f, true);
    CHECK(world->getVisibleChunkCount() >= 1);
    bool sawFar = false, stillOrigin = false;
    for (int i = 0; i < world->getVisibleChunkCount(); ++i) {
        int cx, cy, cz;
        world->getVisibleChunkCoord(i, cx, cy, cz);
        if (cx == 10) sawFar = true;
        if (cx == 0 && cy == 0 && cz == 0) stillOrigin = true;
    }
    CHECK(sawFar);
    CHECK(!stillOrigin);
}

TEST_CASE("voxel.world.setVoxel_marks_dirty_and_creates") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    CHECK_EQ(world->getChunkCount(), 0);
    world->setVoxel(40, 5, -3, 6);
    CHECK(world->hasChunk(1, 0, -1));
    CHECK(world->getChunk(1, 0, -1)->isDirty());
    CHECK_EQ(int(world->getVoxel(40, 5, -3)), 6);
}

TEST_CASE("voxel.world.visible_chunk_coords_consistent") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(-1, 0, 2)->fill(1);
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->remeshDirty();
    float view[16], proj[16], vp[16];
    lookAtRH(0.f, 16.f, 100.f, 0.f, 16.f, 0.f, 0, 1, 0, view);
    perspectiveRH_ZO(70.f * 3.14159265f / 180.f, 1.f, 0.1f, 400.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 0.f, 16.f, 100.f, 400.f, false);
    CHECK(world->getVisibleChunkCount() >= 1);
    for (int i = 0; i < world->getVisibleChunkCount(); ++i) {
        int cx, cy, cz;
        world->getVisibleChunkCoord(i, cx, cy, cz);
        CHECK(world->hasChunk(cx, cy, cz));
    }
}

TEST_CASE("voxel.greedy.shell_vs_solid_rect_delta") {
    std::unique_ptr<Chunk> solid(new Chunk(0, 0, 0));
    solid->fill(1);
    solid->remesh();
    CHECK_EQ(solid->totalRectCount(), 6);

    std::unique_ptr<Chunk> shell(new Chunk(0, 0, 0));
    for (int z = 0; z < 32; ++z)
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const bool border = (x == 0 || x == 31 || y == 0 || y == 31 || z == 0 || z == 31);
                if (border) shell->set(x, y, z, 1);
            }
    shell->remesh();
    // Hollow shell has inner faces too.
    CHECK(shell->totalRectCount() > solid->totalRectCount());
}

TEST_CASE("voxel.greedy.plus_shaped_floorplan") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    // Plus shape on y=0: arms along X and Z through center.
    for (int x = 0; x < 7; ++x) chunk->set(x, 0, 3, 1);
    for (int z = 0; z < 7; ++z) chunk->set(3, 0, z, 1);
    chunk->remesh();
    int area = 0;
    for (const auto &r : chunk->faceRects(FaceDir::PosY)) area += r.width() * r.height();
    CHECK_EQ(area, 7 + 7 - 1);  // center counted once
    CHECK(chunk->faceRectCount(FaceDir::PosY) >= 2);
}

TEST_CASE("voxel.greedy.full_xy_slab_at_each_z") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) chunk->set(x, y, 0, 2);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegZ), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::NegZ)[0].width(), 32);
    CHECK_EQ(chunk->faceRects(FaceDir::NegZ)[0].height(), 32);
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosZ), 1);
}

TEST_CASE("voxel.greedy.checkerboard_two_tex_no_merge") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x) chunk->set(x, 0, z, ((x + z) & 1) ? 1 : 2);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosY), 16);
}

TEST_CASE("voxel.world.three_chunks_range_picks_nearest") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->getOrCreateChunk(0, 0, 0)->fill(1);
    world->getOrCreateChunk(5, 0, 0)->fill(1);
    world->getOrCreateChunk(10, 0, 0)->fill(1);
    world->remeshDirty();
    float view[16], proj[16], vp[16];
    // Eye near chunk 0; short range excludes far ones.
    lookAtRH(16.f, 16.f, 40.f, 16.f, 16.f, 16.f, 0, 1, 0, view);
    perspectiveRH_ZO(60.f * 3.14159265f / 180.f, 1.f, 0.1f, 500.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 16.f, 16.f, 40.f, 60.f, true);
    CHECK_EQ(world->getVisibleChunkCount(), 1);
    int cx, cy, cz;
    world->getVisibleChunkCoord(0, cx, cy, cz);
    CHECK_EQ(cx, 0);
}

TEST_CASE("voxel.world.drawVisible_null_atlas_safe") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(0, 0, 0, 1);
    world->remeshDirty();
    float view[16], proj[16], vp[16];
    lookAtRH(0.5f, 0.5f, 8.f, 0.5f, 0.5f, 0.5f, 0, 1, 0, view);
    perspectiveRH_ZO(50.f * 3.14159265f / 180.f, 1.f, 0.1f, 100.f, proj);
    mul4(proj, view, vp);
    world->selectVisible(vp, 0.5f, 0.5f, 8.f, 100.f, true);
    // CPU-only: gfx null already covered; ensure batches exist for null-atlas GPU path.
    CHECK(world->getVisibleBatchCount() >= 1);
    world->drawVisible(nullptr, nullptr, 1);
}

TEST_CASE("voxel.chunk.coords_and_getters") {
    std::unique_ptr<Chunk> chunk(new Chunk(-3, 4, -5));
    CHECK_EQ(chunk->cx(), -3);
    CHECK_EQ(chunk->cy(), 4);
    CHECK_EQ(chunk->cz(), -5);
    CHECK_EQ(chunk->originX(), -96.f);
    CHECK_EQ(chunk->originY(), 128.f);
    CHECK_EQ(chunk->originZ(), -160.f);
}

TEST_CASE("voxel.greedy.meshFace_empty_volume") {
    uint8_t voxels[32 * 32 * 32];
    std::memset(voxels, 0, sizeof(voxels));
    std::vector<PackedRect> out;
    for (int d = 0; d < faceDirCount(); ++d) {
        out.clear();
        GreedyMesher::meshFace(voxels, FaceDir(d), out);
        CHECK_EQ(int(out.size()), 0);
    }
}

TEST_CASE("voxel.world.clear_then_rebuild") {
    std::unique_ptr<VoxelWorld> world(new VoxelWorld());
    world->setVoxel(1, 1, 1, 2);
    world->setVoxel(40, 1, 1, 3);
    world->remeshDirty();
    CHECK_EQ(world->getChunkCount(), 2);
    world->clear();
    CHECK_EQ(world->getChunkCount(), 0);
    CHECK_EQ(world->getVisibleBatchCount(), 0);
    world->setVoxel(1, 1, 1, 4);
    world->remeshDirty();
    CHECK_EQ(world->getChunkCount(), 1);
    CHECK_EQ(int(world->getVoxel(1, 1, 1)), 4);
}

TEST_CASE("voxel.module.newCubeTypes_and_newWorld") {
    auto *mod = Voxel::create();
    CubeTypeRegistry *types = mod->newCubeTypes();
    REQUIRE(types != nullptr);
    types->loadFromJson("[{\"name\":\"stone\",\"faceTex\":[9,9,9,9,9,9]}]");
    CHECK_EQ(types->count(), 1);

    VoxelWorld *w = mod->newWorld(types);
    REQUIRE(w != nullptr);
    w->setVoxelByName(0, 0, 0, "stone");
    CHECK(w->hasChunk(0, 0, 0));
    CHECK_EQ(w->getCubeTypeName(0, 0, 0), std::string("stone"));
    delete w;

    VoxelWorld *empty = mod->newWorld();
    REQUIRE(empty != nullptr);
    CHECK_EQ(empty->getChunkCount(), 0);
    delete empty;
    delete types;
}

TEST_CASE("voxel.greedy.border_only_x0_plane") {
    std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
    for (int z = 0; z < 32; ++z)
        for (int y = 0; y < 32; ++y) chunk->set(0, y, z, 5);
    chunk->remesh();
    CHECK_EQ(chunk->faceRectCount(FaceDir::NegX), 1);
    CHECK_EQ(chunk->faceRects(FaceDir::NegX)[0].width(), 32);
    CHECK_EQ(chunk->faceRects(FaceDir::NegX)[0].height(), 32);
    CHECK_EQ(chunk->faceRectCount(FaceDir::PosX), 1);
}

TEST_CASE("voxel.registry.loadFromJson") {
    CubeTypeRegistry reg;
    const int n = reg.loadFromJson(
        R"([
            {"name":"grass","faceTex":[1,1,2,3,1,1]},
            {"name":"furnace","faceTex":[4,4,4,4,5,4],"directional":true,"composeGroup":"furnace"}
        ])");
    CHECK_EQ(n, 2);
    CHECK_EQ(reg.count(), 2);
    // furnace 方向性展开成 4 个变体
    CHECK_EQ(reg.variantCount(), 5);  // grass + 4 furnace variants

    const CubeType *grass = reg.find("grass");
    REQUIRE(grass != nullptr);
    CHECK(!grass->directional);
    CHECK_EQ(int(grass->faceTex[2]), 2);

    const CubeType *furnace = reg.find("furnace");
    REQUIRE(furnace != nullptr);
    CHECK(furnace->directional);
    CHECK_EQ(furnace->composeGroup, std::string("furnace"));

    std::string err;
    CHECK_EQ(reg.loadFromJson("not json", &err), 0);
    CHECK(!err.empty());
}

TEST_CASE("voxel.registry.directional_variants_rotate_faces") {
    CubeTypeRegistry reg;
    CubeType furnace;
    furnace.name = "furnace";
    // 前脸在 +X（id=4），其它面 id=1，顶/底 ±Y 分别 5/6
    furnace.faceTex[0] = 4;  // PosX
    furnace.faceTex[1] = 1;  // NegX
    furnace.faceTex[2] = 5;  // PosY
    furnace.faceTex[3] = 6;  // NegY
    furnace.faceTex[4] = 1;  // PosZ
    furnace.faceTex[5] = 1;  // NegZ
    furnace.directional = true;
    const uint8_t base = reg.add(furnace);
    CHECK(base >= 1);
    CHECK_EQ(reg.variantCount(), 4);

    // 0 度变体：+X = 前脸
    CHECK_EQ(int(reg.find(base)->faceTex[0]), 4);
    // 90 度变体：前脸转到 +Z（绕 Y：+X→-Z→-X→+Z，因此 +X 的纹理会出现在 90° 变体的 -Z，
    // 而 +Z 由 -X 转来 = 1）。这里校验 variantId + resolveFaceTex 的一致性。
    const uint8_t v90 = reg.variantId("furnace", 1);
    CHECK_EQ(int(v90), int(base + 1));
    // 顶/底不随绕 Y 旋转变化
    CHECK_EQ(int(reg.find(v90)->faceTex[2]), 5);
    CHECK_EQ(int(reg.find(v90)->faceTex[3]), 6);

    // resolveFaceTex：未注册 id 退化为 id 本身
    CHECK_EQ(int(resolveFaceTex(reg, 200, FaceDir::PosX)), 200);
    // 已注册：按变体面纹理解析
    CHECK_EQ(int(resolveFaceTex(reg, base, FaceDir::PosX)), 4);
    CHECK_EQ(int(resolveFaceTex(reg, base, FaceDir::PosY)), 5);
}

TEST_CASE("voxel.world.registry_resolves_per_face_tex") {
    CubeTypeRegistry reg;
    CubeType grass;
    grass.name = "grass";
    grass.faceTex[0] = 1;
    grass.faceTex[1] = 1;
    grass.faceTex[2] = 2;  // 顶
    grass.faceTex[3] = 3;  // 底
    grass.faceTex[4] = 1;
    grass.faceTex[5] = 1;
    reg.add(grass);

    VoxelWorld world(reg);
    world.setVoxelByName(4, 5, 6, "grass");
    world.remeshDirty();

    Chunk *c = world.getChunk(0, 0, 0);
    REQUIRE(c != nullptr);
    // 顶面纹理解析为 2，其它面为 1
    CHECK_EQ(c->faceRects(FaceDir::PosY)[0].tex(), 2);
    CHECK_EQ(c->faceRects(FaceDir::NegY)[0].tex(), 3);
    CHECK_EQ(c->faceRects(FaceDir::PosX)[0].tex(), 1);

    CHECK_EQ(world.getCubeTypeName(4, 5, 6), std::string("grass"));
    CHECK_EQ(int(world.getCubeTypeTex(4, 5, 6, "posY")), 2);
    CHECK_EQ(int(world.getCubeTypeTex(4, 5, 6, "posX")), 1);
}

