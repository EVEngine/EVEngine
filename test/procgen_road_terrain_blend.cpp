#include "procgen/heightmap/Heightmap.h"
#include "procgen/road/RoadBake.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.terrainBlendUsesWorldDistance") {
    // A small asphalt patch covers exactly the central heightmap sample.
    // Test physical distances independently of road tessellation and width.
    for (float cellSize : {0.5f, 1.f, 2.f}) {
        MeshBuild mesh;
        mesh.setActiveGroup("asphalt");
        const float radius = cellSize * 0.25f;
        mesh.addVertex(-radius, 0.f, -radius, 0.f, 1.f, 0.f, 0.f, 0.f);
        mesh.addVertex(radius, 0.f, -radius, 0.f, 1.f, 0.f, 0.f, 0.f);
        mesh.addVertex(0.f, 0.f, radius, 0.f, 1.f, 0.f, 0.f, 0.f);
        mesh.addTriangle(0, 1, 2);
        for (float radiusInCells : {0.f, 0.25f, 1.f, 1.5f, 2.f}) {
            Heightmap terrain(9, 9);
            for (float& value : terrain.data()) value = 1.f;
            const auto                baseline = terrain.data();
            RoadTerrainConformOptions options;
            options.originX = options.originZ = -4.f * cellSize;
            options.cellSize                  = cellSize;
            options.blendDistance             = radiusInCells * cellSize;
            auto receipt                      = conformHeightmapToRoadWithReceipt(terrain, mesh, options);
            REQUIRE(receipt.ok());
            for (int z = 0; z < 9; ++z) {
                for (int x = 0; x < 9; ++x) {
                    const float distance = std::sqrt(static_cast<float>((x - 4) * (x - 4) + (z - 4) * (z - 4)));
                    const float expected =
                        distance == 0.f ? 0.f : (distance >= radiusInCells ? 1.f : distance / radiusInCells);
                    REQUIRE(std::fabs(terrain.height(x, z) - expected) < 1e-6f);
                }
            }
            auto restored = restoreHeightmapFromRoad(terrain, receipt.value());
            REQUIRE(restored.ok());
            CHECK_EQ(terrain.data(), baseline);
        }
    }
}
