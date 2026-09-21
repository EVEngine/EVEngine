#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMeshStamp.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace eve::procgen;
namespace {
Heightmap filled(int w, int h, float value) {
    Heightmap result(w, h);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
TerrainStampSettings settings() {
    TerrainStampSettings s;
    s.centerX = s.centerZ = 2;
    s.width = s.depth = 4;
    s.operation       = TerrainStampOperation::SmoothRaise;
    s.smoothWidth     = 4;
    return s;
}
MeshBuild roof(float height) {
    MeshBuild mesh;
    mesh.positions() = {0, height, 0, 4, height, 0, 0, height, 4, 4, height, 4};
    mesh.indices()   = {0, 1, 2, 1, 3, 2};
    return mesh;
}
}  // namespace

TEST_CASE("procgen.smoothStamp.heightBandMaskAndEdgeFade") {
    auto terrain = filled(5, 5, 10), stamp = filled(1, 1, 10), one = filled(1, 1, 1);
    auto s      = settings();
    s.edgeFade  = 2;
    auto result = applyTerrainStamp(terrain, stamp, s, one, one);
    REQUIRE(result.ok());
    CHECK_EQ(result.value(), 9);
    CHECK_EQ(terrain.height(2, 2), 11.f);
    CHECK_EQ(terrain.height(1, 2), 10.5f);
    CHECK_EQ(terrain.height(1, 1), 10.25f);
    CHECK_EQ(terrain.height(0, 2), 10.f);
    auto half = filled(1, 1, 0.5f);
    terrain   = filled(5, 5, 10);
    REQUIRE(applyTerrainStamp(terrain, stamp, s, one, half).ok());
    CHECK_EQ(terrain.height(2, 2), 10.5f);
    // Local mask alters stamp height; it must not be interpreted as coverage.
    terrain = filled(5, 5, 10);
    REQUIRE(applyTerrainStamp(terrain, stamp, s, half, one).ok());
    CHECK_EQ(terrain.height(2, 2), 10.f);
}

TEST_CASE("procgen.smoothStamp.zeroWidthAndFailureAreCompatible") {
    auto a = filled(5, 5, 2), b = a, stamp = filled(1, 1, 3.1f), half = filled(1, 1, 0.35f);
    auto s        = settings();
    s.smoothWidth = 0;
    s.amplitude   = 1.7f;
    s.baseHeight  = 4.f;
    REQUIRE(applyTerrainStamp(a, stamp, s, half, half).ok());
    s.operation = TerrainStampOperation::Raise;
    REQUIRE(applyTerrainStamp(b, stamp, s, half, half).ok());
    CHECK(a.data() == b.data());
    s.operation   = TerrainStampOperation::SmoothRaise;
    a             = filled(5, 5, 2);
    s.smoothWidth = 0.01f;
    REQUIRE(applyTerrainStamp(a, stamp, s, half, half).ok());
    CHECK(a.data() == b.data());  // Outside the band preserves double-intermediate Raise exactly.
    for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        s.smoothWidth = invalid;
        CHECK(!applyTerrainStamp(a, stamp, s, half, half).ok());
        CHECK(a.data() == b.data());
        s.smoothWidth = 4;
        s.edgeFade    = invalid;
        CHECK(!applyTerrainStamp(a, stamp, s, half, half).ok());
        CHECK(a.data() == b.data());
        s.edgeFade = 0;
    }
    auto       maximum = filled(5, 5, std::numeric_limits<float>::max());
    const auto before  = maximum.data();
    s.smoothWidth      = std::numeric_limits<float>::max();
    s.amplitude        = 1;
    auto one           = filled(1, 1, 1);
    CHECK(!applyTerrainStamp(maximum, maximum, s, one, one).ok());
    CHECK(maximum.data() == before);
}

TEST_CASE("procgen.smoothStamp.rotatedTilesAndAliasing") {
    auto whole = filled(9, 5, 2), left = filled(5, 5, 2), right = left, stamp = filled(2, 2, 0);
    stamp.data() = {0, 4, 5, 2};
    auto one     = filled(1, 1, 1);
    auto s       = settings();
    s.centerX    = 4;
    s.width      = 8;
    s.rotation   = 0.3;
    s.edgeFade   = 1;
    REQUIRE(applyTerrainStamp(whole, stamp, s, one, one).ok());
    REQUIRE(applyTerrainStamp(left, stamp, s, one, one).ok());
    s.originX = 4;
    REQUIRE(applyTerrainStamp(right, stamp, s, one, one).ok());
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) {
            CHECK_EQ(left.height(x, z), whole.height(x, z));
            CHECK_EQ(right.height(x, z), whole.height(x + 4, z));
        }
    auto expected = left, source = left;
    s          = settings();
    s.rotation = std::numbers::pi / 2;
    REQUIRE(applyTerrainStamp(expected, source, s, one, one).ok());
    REQUIRE(applyTerrainStamp(left, left, s, one, one).ok());
    CHECK(left.data() == expected.data());
}

TEST_CASE("procgen.meshStamp.highestSurfaceWindingAndOwnedSource") {
    auto mesh = roof(-3);
    // Overlapping reversed-winding face above the roof.
    mesh.positions().insert(mesh.positions().end(), {0, 7, 0, 4, 7, 0, 0, 7, 4});
    mesh.indices().insert(mesh.indices().end(), {6, 5, 4});
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mesh).ok());
    mesh.clear();
    Heightmap heights(5, 5), mask(5, 5);
    auto      result = builder.bake(heights, mask, 0, 0, 4, 4, 0);
    REQUIRE(result.ok());
    CHECK_EQ(result.value(), 25);
    CHECK_EQ(heights.height(1, 1), 7.f);
    CHECK_EQ(heights.height(3, 3), -3.f);
    CHECK_EQ(heights.height(2, 2), 7.f);
    CHECK_EQ(mask.height(4, 4), 1.f);
    REQUIRE(builder.bake(heights, mask, 0, 0, 4, 4, 2).ok());
    CHECK_EQ(mask.height(0, 2), 0.f);
    CHECK_EQ(mask.height(1, 2), 0.5f);
    CHECK_EQ(mask.height(2, 2), 1.f);
}

TEST_CASE("procgen.meshStamp.missingAndDegenerateTriangles") {
    auto mesh      = roof(8);
    mesh.indices() = {0, 1, 2, 0, 0, 1};
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mesh).ok());
    Heightmap heights(5, 5), mask(5, 5);
    REQUIRE(builder.bake(heights, mask, 0, 0, 4, 4, 0).ok());
    CHECK_EQ(mask.height(4, 4), 0.f);
    CHECK_EQ(heights.height(4, 4), 0.f);
    CHECK_EQ(mask.height(0, 0), 1.f);
    REQUIRE(builder.bake(heights, mask, 10, 10, 4, 4, 0).ok());
    for (float value : mask.data()) CHECK_EQ(value, 0.f);
}

TEST_CASE("procgen.meshStamp.failuresPreserveBothOutputsAndSource") {
    auto                    mesh = roof(8);
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mesh).ok());
    mesh.indices()[0] = 99;
    CHECK(!builder.setSource(mesh).ok());
    auto       heights = filled(5, 5, 3), mask = filled(5, 5, 0.75f);
    const auto beforeH = heights.data(), beforeM = mask.data();
    CHECK(!builder.bake(heights, mask, 0, 0, 0, 4, 0).ok());
    CHECK(!builder.bake(heights, heights, 0, 0, 4, 4, 0).ok());
    CHECK(!builder.bake(heights, mask, 0, 0, 4, 4, -1).ok());
    CHECK(heights.data() == beforeH);
    CHECK(mask.data() == beforeM);
    REQUIRE(builder.bake(heights, mask, 0, 0, 4, 4, 0).ok());
    CHECK_EQ(heights.height(2, 2), 8.f);
    // Oversized projected overlap must fail without publishing already computed samples.
    mesh = roof(8);
    mesh.indices().clear();
    for (int i = 0; i < 150; ++i) mesh.indices().insert(mesh.indices().end(), {0, 1, 2});
    REQUIRE(builder.setSource(mesh).ok());
    heights = filled(1024, 1024, 3);
    mask    = filled(1024, 1024, 0.75f);
    CHECK(!builder.bake(heights, mask, 0, 0, 4, 4, 0).ok());
    CHECK_EQ(heights.height(0, 0), 3.f);
    CHECK_EQ(mask.height(0, 0), 0.75f);
    CHECK_EQ(heights.height(1023, 1023), 3.f);
}

TEST_CASE("procgen.meshStamp.bakedMountainComposesWithSmoothRaise") {
    MeshBuild mountain;
    mountain.positions() = {0, -4, 0, 4, -4, 0, 4, -4, 4, 0, -4, 4, 2, 10, 2};
    mountain.indices()   = {0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4};
    TerrainMeshStampBuilder builder;
    REQUIRE(builder.setSource(mountain).ok());
    Heightmap heights(17, 17), coverage(17, 17);
    REQUIRE(builder.bake(heights, coverage, 0, 0, 4, 4, 0.5).ok());
    auto terrain = filled(17, 17, 0), one = filled(1, 1, 1);
    auto s     = settings();
    s.spacingX = s.spacingZ = 0.25;
    REQUIRE(applyTerrainStamp(terrain, heights, s, one, coverage).ok());
    CHECK_EQ(terrain.height(8, 8), 10.f);
    for (int i = 0; i < 17; ++i) {
        CHECK_EQ(terrain.height(0, i), 0.f);
        CHECK_EQ(terrain.height(16, i), 0.f);
    }
    for (float h : terrain.data()) CHECK(h >= 0);
}

TEST_CASE("procgen.smoothStamp.multiTilePublicationIsAtomic") {
    auto       left = filled(5, 5, 10), right = left, whole = filled(9, 5, 10);
    const auto stamp = filled(1, 1, 10), one = filled(1, 1, 1);
    auto       s = settings();
    s.centerX    = 4;
    s.width      = 8;
    s.edgeFade   = 1;
    const std::vector<TerrainHeightTile> tiles{{"left", &left, 0, 0, 4, 4, false},
                                               {"right", &right, 4, 0, 4, 4, false}};
    REQUIRE(applyTerrainStamp(whole, stamp, s, one, one).ok());
    auto report = applyTerrainStampMultiTile(tiles, stamp, s, one, one);
    REQUIRE(report.ok());
    CHECK_EQ(report.value().affectedTiles, 2);
    for (int z = 0; z < 5; ++z)
        for (int x = 0; x < 5; ++x) {
            CHECK_EQ(left.height(x, z), whole.height(x, z));
            CHECK_EQ(right.height(x, z), whole.height(x + 4, z));
        }
    // The second tile overflows after the first tile has computed its candidate.
    right                 = filled(5, 5, std::numeric_limits<float>::max());
    const auto beforeLeft = left.data(), beforeRight = right.data();
    const auto highStamp = filled(1, 1, std::numeric_limits<float>::max());
    s.smoothWidth        = std::numeric_limits<float>::max();
    CHECK(!applyTerrainStampMultiTile(tiles, highStamp, s, one, one).ok());
    CHECK(left.data() == beforeLeft);
    CHECK(right.data() == beforeRight);
}
