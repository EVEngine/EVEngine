#include "procgen/heightmap/Heightmap.h"
#include "procgen/road/RoadBake.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <array>
#include <cmath>

namespace {
using eve::procgen::Heightmap;
using eve::procgen::MeshBuild;
using eve::procgen::road::conformHeightmapToRoadWithReceipt;
using eve::procgen::road::restoreHeightmapFromRoad;
using eve::procgen::road::RoadTerrainConformOptions;

MeshBuild asphaltAtOrigin(float cellSize) {
    MeshBuild mesh;
    mesh.setActiveGroup("asphalt");
    const float radius = cellSize * 0.25f;
    mesh.addVertex(-radius, 0.f, -radius, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(radius, 0.f, -radius, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(0.f, 0.f, radius, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

Heightmap levelTerrain() {
    Heightmap terrain(9, 9);
    for (float& value : terrain.data()) value = 1.f;
    return terrain;
}

RoadTerrainConformOptions mapping(float cellSize, float blendCells) {
    RoadTerrainConformOptions options;
    options.originX = options.originZ = -4.f * cellSize;
    options.cellSize                  = cellSize;
    options.blendDistance             = blendCells * cellSize;
    return options;
}

}  // namespace

// Input: the same world-space asphalt patch under three grid scales and five
// blend widths. Expected: each cell's normalized height follows its physical
// distance from the road, independent of tessellation and cell scale.
TEST_CASE("procgen.road.terrainBlendUsesWorldDistance") {
    for (float cellSize : {0.5f, 1.f, 2.f}) {
        const auto mesh = asphaltAtOrigin(cellSize);
        for (float blendCells : {0.f, 0.25f, 1.f, 1.5f, 2.f}) {
            auto       terrain   = levelTerrain();
            const auto conformed = conformHeightmapToRoadWithReceipt(terrain, mesh, mapping(cellSize, blendCells));
            REQUIRE(conformed.ok());
            for (int z = 0; z < 9; ++z) {
                for (int x = 0; x < 9; ++x) {
                    const float distance = std::sqrt(float((x - 4) * (x - 4) + (z - 4) * (z - 4)));
                    const float expected = distance == 0.f ? 0.f : distance >= blendCells ? 1.f : distance / blendCells;
                    CHECK(std::fabs(terrain.height(x, z) - expected) < 1e-6f);
                }
            }
        }
    }
}

// Input: a successful conform receipt. Expected: it names the exact changed
// samples and restores the prior terrain without changing unrelated cells.
TEST_CASE("procgen.road.terrainReceiptRestoresExactPreviousSamples") {
    auto       terrain = levelTerrain();
    const auto before  = terrain.data();
    const auto receipt = conformHeightmapToRoadWithReceipt(terrain, asphaltAtOrigin(1.f), mapping(1.f, 2.f));
    REQUIRE(receipt.ok());
    REQUIRE(!receipt.value().sampleIndices.empty());
    CHECK_EQ(receipt.value().sampleIndices.size(), receipt.value().before.size());
    CHECK_EQ(receipt.value().sampleIndices.size(), receipt.value().after.size());
    const auto restored = restoreHeightmapFromRoad(terrain, receipt.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value(), int(receipt.value().sampleIndices.size()));
    CHECK_EQ(terrain.data(), before);
}

// Input: invalid mapping settings, or a receipt after a newer terrain edit.
// Expected: each failure is atomic and preserves the caller's latest heights.
TEST_CASE("procgen.road.terrainFailuresDoNotMutateHeightmap") {
    auto       terrain = levelTerrain();
    const auto mesh    = asphaltAtOrigin(1.f);
    auto       options = mapping(1.f, 2.f);
    const auto before  = terrain.data();
    options.cellSize   = 0.f;
    CHECK(!conformHeightmapToRoadWithReceipt(terrain, mesh, options).ok());
    options             = mapping(1.f, 2.f);
    options.maximumWork = 0;
    CHECK(!conformHeightmapToRoadWithReceipt(terrain, mesh, options).ok());
    CHECK_EQ(terrain.data(), before);

    const auto receipt = conformHeightmapToRoadWithReceipt(terrain, mesh, mapping(1.f, 2.f));
    REQUIRE(receipt.ok());
    terrain.setHeight(4, 4, 0.5f);
    const auto newer = terrain.data();
    CHECK(!restoreHeightmapFromRoad(terrain, receipt.value()).ok());
    CHECK_EQ(terrain.data(), newer);
}
