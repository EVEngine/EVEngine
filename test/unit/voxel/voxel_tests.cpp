#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_voxel_face_dir)
#include <string>
#include "voxel/FaceDir.h"

namespace test_voxel_face_dir_unit {
using namespace eve::voxel;

// FaceDir is a serialized/rendering vocabulary. Accept documented aliases,
// emit one canonical name, and leave the output untouched on a parse failure.
TEST_CASE("voxel.faceDir.documented_names_and_aliases") {
    struct Entry {
        FaceDir     dir;
        const char *name;
        const char *alias;
    };
    const Entry entries[] = {
        {FaceDir::PosX, "posX", "+x"}, {FaceDir::NegX, "negX", "-x"}, {FaceDir::PosY, "posY", "+y"},
        {FaceDir::NegY, "negY", "-y"}, {FaceDir::PosZ, "posZ", "+z"}, {FaceDir::NegZ, "negZ", "-z"},
    };
    REQUIRE_EQ(faceDirCount(), 6);
    for (const auto &entry : entries) {
        FaceDir parsed = FaceDir::Count;
        REQUIRE(faceDirFromName(entry.name, parsed));
        CHECK(parsed == entry.dir);
        parsed = FaceDir::Count;
        REQUIRE(faceDirFromName(entry.alias, parsed));
        CHECK(parsed == entry.dir);
        CHECK(std::string(faceDirName(entry.dir)) == entry.name);
    }
    for (const auto *invalid : {"", "PosX", "x+", "front"}) {
        FaceDir parsed = FaceDir::NegY;
        CHECK(!faceDirFromName(invalid, parsed));
        CHECK(parsed == FaceDir::NegY);
    }
}

// Positive turns follow the engine's right-hand Y convention; vertical faces
// have no horizontal orientation. Turns repeat every four quarter rotations.
TEST_CASE("voxel.faceDir.y_rotation_cycle_and_normals") {
    const FaceDir cycle[] = {FaceDir::PosX, FaceDir::NegZ, FaceDir::NegX, FaceDir::PosZ};
    for (int i = 0; i < 4; ++i) {
        REQUIRE(rotateFaceY(cycle[i], 1) == cycle[(i + 1) % 4]);
        CHECK(rotateFaceY(cycle[i], 4) == cycle[i]);
        CHECK(rotateFaceY(cycle[i], -1) == cycle[(i + 3) % 4]);
    }
    CHECK(rotateFaceY(FaceDir::PosY, 3) == FaceDir::PosY);
    CHECK(rotateFaceY(FaceDir::NegY, -3) == FaceDir::NegY);
    float nx = 2.f, ny = 2.f, nz = 2.f;
    faceNormal(FaceDir::NegZ, nx, ny, nz);
    CHECK_EQ(nx, 0.f);
    CHECK_EQ(ny, 0.f);
    CHECK_EQ(nz, -1.f);
}
}  // namespace test_voxel_face_dir_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_voxel)
#include "voxel/Chunk.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelRectDecode.h"
#include "voxel/VoxelWorld.h"

#include <array>
#include <cstdint>
#include <vector>

namespace test_voxel_unit {
using namespace eve::voxel;

// The 32-bit instance format is the CPU/GPU contract: local coordinates and
// rectangle extents have five bits; texture has seven bits.
TEST_CASE("voxel.packedRect.round_trip_extremes_and_bit_masks") {
    SUB_CASE("minimum representable coordinate and size") {
        const auto rect = PackedRect::pack(0, 0, 0, 1, 1, 0);
        REQUIRE_EQ(rect.bits, 0u);
        CHECK_EQ(rect.width(), 1);
        CHECK_EQ(rect.height(), 1);
    };
    SUB_CASE("maximum representable coordinate, size and texture") {
        const auto rect = PackedRect::pack(31, 31, 31, 32, 32, 127);
        CHECK_EQ(rect.bits, UINT32_MAX);
        CHECK_EQ(rect.x(), 31);
        CHECK_EQ(rect.y(), 31);
        CHECK_EQ(rect.z(), 31);
        CHECK_EQ(rect.width(), 32);
        CHECK_EQ(rect.height(), 32);
        CHECK_EQ(rect.tex(), 127);
    };
    SUB_CASE("out-of-range input is masked to its bit width") {
        const auto rect = PackedRect::pack(32, 33, 34, 33, 34, 128);
        CHECK_EQ(rect.x(), 0);
        CHECK_EQ(rect.y(), 1);
        CHECK_EQ(rect.z(), 2);
        CHECK_EQ(rect.width(), 1);
        CHECK_EQ(rect.height(), 2);
        CHECK_EQ(rect.tex(), 0);
    };
}

// Decoding establishes the exact face plane, world translation, UV atlas cell,
// winding and AO corner order used by the instanced shader.
TEST_CASE("voxel.rectDecode.geometry_uv_and_ao") {
    const auto packed = PackedRect::pack(2, 3, 4, 5, 6, 18);
    const auto top    = decodePackedRect(packed, FaceDir::PosY, 32.f, -32.f, 64.f, 16, 0xE4u);
    REQUIRE_EQ(top.corners[0][0], 34.f);
    CHECK_EQ(top.corners[0][1], -28.f);
    CHECK_EQ(top.corners[0][2], 68.f);
    CHECK_EQ(top.corners[2][0], 39.f);
    CHECK_EQ(top.corners[2][2], 74.f);
    CHECK_EQ(top.normal[1], 1.f);
    CHECK_EQ(top.uv[0][0], 2.f / 16.f);
    CHECK_EQ(top.uv[0][1], 1.f / 16.f);
    CHECK_EQ(top.uv[2][0], 3.f / 16.f);
    CHECK_EQ(top.ao[0], 0);
    CHECK_EQ(top.ao[1], 1);
    CHECK_EQ(top.ao[2], 2);
    CHECK_EQ(top.ao[3], 3);
    for (int i = 0; i < faceDirCount(); ++i) {
        const auto face = decodePackedRect(packed, FaceDir(i), 0.f, 0.f, 0.f);
        CHECK(decodedWindingMatchesNormal(face));
        CHECK(decodedSecondTriangleMatchesNormal(face));
    }
}

// A chunk owns only [0,32) coordinates. Out-of-range reads return air and
// writes do not change storage. Mesh invalidation follows every valid write.
TEST_CASE("voxel.chunk.bounds_origin_and_dirty_lifecycle") {
    Chunk chunk(-1, 2, 3);
    REQUIRE_EQ(chunk.originX(), -32.f);
    CHECK_EQ(chunk.originY(), 64.f);
    CHECK_EQ(chunk.originZ(), 96.f);
    CHECK(chunk.isDirty());
    chunk.ensureMeshed();
    CHECK(!chunk.isDirty());
    chunk.set(-1, 0, 0, 5);
    chunk.set(32, 0, 0, 5);
    CHECK(!chunk.isDirty());
    CHECK_EQ(chunk.get(-1, 0, 0), 0);
    CHECK_EQ(chunk.get(32, 0, 0), 0);
    chunk.set(31, 31, 31, 5);
    CHECK(chunk.isDirty());
    CHECK_EQ(chunk.get(31, 31, 31), 5);
    chunk.ensureMeshed();
    CHECK(!chunk.isDirty());
    CHECK_EQ(chunk.totalRectCount(), 6);
    chunk.clear();
    chunk.ensureMeshed();
    CHECK_EQ(chunk.totalRectCount(), 0);
}

// Greedy meshing joins coplanar faces with matching texture, and suppresses
// completely hidden interior faces. Texture changes split the merged surface.
TEST_CASE("voxel.chunk.greedy_meshing_surface_contract") {
    Chunk cube;
    cube.fill(7);
    cube.remesh();
    CHECK_EQ(cube.totalRectCount(), 6);
    for (int i = 0; i < faceDirCount(); ++i) {
        const auto dir = FaceDir(i);
        REQUIRE_EQ(cube.faceRectCount(dir), 1);
        CHECK_EQ(cube.faceRects(dir)[0].width(), 32);
        CHECK_EQ(cube.faceRects(dir)[0].height(), 32);
        CHECK_EQ(cube.faceRects(dir)[0].tex(), 7);
        CHECK(cube.facePackedData(dir) != nullptr);
        CHECK(cube.faceAOPackedData(dir) != nullptr);
    }
    Chunk pair;
    pair.set(0, 0, 0, 1);
    pair.set(1, 0, 0, 1);
    pair.remesh();
    CHECK_EQ(pair.totalRectCount(), 6);
    pair.set(1, 0, 0, 2);
    pair.remesh();
    CHECK(pair.totalRectCount() > 6);
}

// World coordinates use floor division at negative chunk boundaries. Air
// writes are sparse and do not allocate a missing chunk.
TEST_CASE("voxel.world.sparse_coordinates_and_revision") {
    VoxelWorld world;
    const auto initial = world.getRevision();
    REQUIRE_EQ(world.getVoxel(-1, -32, 32), 0);
    world.setVoxel(50, 0, 0, 0);
    CHECK_EQ(world.getChunkCount(), 0);
    world.setVoxel(-1, -32, 32, 9);
    CHECK(world.hasChunk(-1, -1, 1));
    CHECK_EQ(world.getVoxel(-1, -32, 32), 9);
    CHECK_EQ(world.getChunk(-1, -1, 1)->get(31, 0, 0), 9);
    CHECK(world.getRevision() > initial);
    world.setVoxel(-1, -32, 32, 0);
    CHECK_EQ(world.getVoxel(-1, -32, 32), 0);
}

// A shared chunk boundary has no pair of hidden faces; changing or removing
// one neighbor invalidates the remaining chunk's seam mesh.
TEST_CASE("voxel.world.cross_chunk_seams_and_remesh") {
    VoxelWorld world;
    world.setVoxel(31, 0, 0, 1);
    world.setVoxel(32, 0, 0, 1);
    const auto remeshed = world.remeshDirty();
    REQUIRE_EQ(remeshed, 2);
    CHECK_EQ(world.getChunk(0, 0, 0)->totalRectCount(), 5);
    CHECK_EQ(world.getChunk(1, 0, 0)->totalRectCount(), 5);
    world.setVoxel(32, 0, 0, 0);
    world.remeshDirty();
    CHECK_EQ(world.getChunk(0, 0, 0)->totalRectCount(), 6);
    world.removeChunk(1, 0, 0);
    CHECK_EQ(world.getChunkCount(), 1);
}

// Raycast reports hit cell, previous empty cell and face normal. Degenerate
// direction and nonpositive range cannot hit.
TEST_CASE("voxel.world.raycast_hit_miss_and_entry_face") {
    VoxelWorld world;
    world.setVoxel(2, 0, 0, 1);
    int hx = -1, hy = -1, hz = -1, px = -1, py = -1, pz = -1;
    int fx = 0, fy = 0, fz = 0;
    CHECK(!world.raycast(0.5f, 0.5f, 0.5f, 0.f, 0.f, 0.f, 5.f, hx, hy, hz, px, py, pz, fx, fy, fz));
    CHECK(!world.raycast(0.5f, 0.5f, 0.5f, 1.f, 0.f, 0.f, 1.f, hx, hy, hz, px, py, pz, fx, fy, fz));
    REQUIRE(world.raycast(0.5f, 0.5f, 0.5f, 1.f, 0.f, 0.f, 5.f, hx, hy, hz, px, py, pz, fx, fy, fz));
    CHECK_EQ(hx, 2);
    CHECK_EQ(px, 1);
    CHECK_EQ(fx, -1);
    CHECK_EQ(hy, 0);
    CHECK_EQ(hz, 0);
}

// Version and magic protect persisted worlds. Rejected headers must retain
// the current live world; a valid snapshot restores sparse chunk positions.
TEST_CASE("voxel.world.persistence_round_trip_and_rejected_header") {
    VoxelWorld world;
    world.setVoxel(-33, 2, 0, 6);
    world.setVoxel(64, 0, 1, 12);
    std::vector<uint8_t> bytes;
    world.serializeWorld(bytes);
    REQUIRE(bytes.size() > 9);
    VoxelWorld restored;
    restored.setVoxel(1, 1, 1, 3);
    std::array<uint8_t, 9> bad = {'B', 'A', 'D', '!', 1, 0, 0, 0, 0};
    CHECK(!restored.deserializeWorld(bad.data(), bad.size()));
    CHECK_EQ(restored.getVoxel(1, 1, 1), 3);
    CHECK(!restored.deserializeWorld(bytes.data(), bytes.size() - 1));
    CHECK_EQ(restored.getVoxel(1, 1, 1), 3);
    REQUIRE(restored.deserializeWorld(bytes.data(), bytes.size()));
    CHECK_EQ(restored.getVoxel(-33, 2, 0), 6);
    CHECK_EQ(restored.getVoxel(64, 0, 1), 12);
    CHECK_EQ(restored.getVoxel(1, 1, 1), 0);
}

// Named type ids separate block identity from face texture. The empty
// registry's documented compatibility path treats an unknown id as texture.
TEST_CASE("voxel.types.named_orientation_and_unknown_id_fallback") {
    CubeTypeRegistry registry;
    CubeType         furnace;
    furnace.name                        = "furnace";
    furnace.directional                 = true;
    furnace.faceTex[int(FaceDir::PosX)] = 4;
    furnace.faceTex[int(FaceDir::NegX)] = 4;
    furnace.faceTex[int(FaceDir::PosY)] = 2;
    furnace.faceTex[int(FaceDir::NegY)] = 3;
    furnace.faceTex[int(FaceDir::PosZ)] = 9;
    furnace.faceTex[int(FaceDir::NegZ)] = 4;
    const auto base                     = registry.add(furnace);
    REQUIRE(base != 0);
    CHECK_EQ(registry.count(), 1);
    CHECK_EQ(registry.variantCount(), 4);
    CHECK(registry.find("furnace") != nullptr);
    CHECK(registry.find("absent") == nullptr);
    CHECK_EQ(resolveFaceTex(registry, base, FaceDir::PosZ), 9);
    const auto rotated = registry.variantId("furnace", 1);
    REQUIRE(rotated != base);
    CHECK_EQ(resolveFaceTex(registry, rotated, FaceDir::PosX), 9);
    CHECK_EQ(resolveFaceTex(registry, rotated, FaceDir::PosY), 2);
    CHECK_EQ(resolveFaceTex(CubeTypeRegistry::empty(), 77, FaceDir::PosX), 77);
}

// Streaming centers are chunk coordinates. The hysteresis cache retains one
// chunk after leaving the create radius, then evicts it beyond the cache.
TEST_CASE("voxel.world.synchronous_streaming_and_cache_hysteresis") {
    VoxelWorld world;
    int        generated = 0;
    const auto fill      = [&generated](Chunk &chunk, int, int, int) {
        ++generated;
        chunk.set(0, 0, 0, 5);
    };
    const auto first = world.streamAround(0, 0, 0, 0, fill);
    REQUIRE_EQ(first.created, 1);
    CHECK_EQ(generated, 1);
    CHECK_EQ(world.getVoxel(0, 0, 0), 5);
    world.setStreamCacheChunks(1);
    const auto nearby = world.streamAround(1, 0, 0, 0, fill);
    CHECK_EQ(nearby.created, 1);
    CHECK(world.hasChunk(0, 0, 0));
    CHECK(world.hasChunk(1, 0, 0));
    const auto distant = world.streamAround(3, 0, 0, 0, fill);
    CHECK(distant.evicted >= 1);
    CHECK(!world.hasChunk(0, 0, 0));
    CHECK(world.hasChunk(3, 0, 0));
    const auto count   = world.getChunkCount();
    const auto ignored = world.streamAround(3, 0, 0, -1, fill);
    CHECK_EQ(ignored.created, 0);
    CHECK_EQ(world.getChunkCount(), count);
}

// A null camera matrix is an explicit empty selection. Invalid visible indices
// produce zero coordinates rather than exposing stale selection state.
TEST_CASE("voxel.world.visibility_query_empty_and_invalid_index") {
    VoxelWorld world;
    world.setVoxel(0, 0, 0, 1);
    world.selectVisible(nullptr, 0.f, 0.f, 0.f, 0.f);
    REQUIRE_EQ(world.getVisibleChunkCount(), 0);
    CHECK_EQ(world.getVisibleBatchCount(), 0);
    CHECK_EQ(world.getVisibleRectCount(), 0);
    int x = 9, y = 9, z = 9;
    world.getVisibleChunkCoord(-1, x, y, z);
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 0);
    CHECK_EQ(z, 0);
    world.getVisibleChunkCoord(0, x, y, z);
    CHECK_EQ(x, 0);
    CHECK_EQ(y, 0);
    CHECK_EQ(z, 0);
}
}  // namespace test_voxel_unit
#endif
