// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "terrain_object_collision.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainStamp.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace eve::procgen;

namespace {

Heightmap filled(int width, int height, float value) {
    Heightmap image(width, height);
    for (auto& sample : image.data()) sample = value;
    return image;
}

TerrainObjectPlacementSettings objects() {
    TerrainObjectPlacementSettings settings;
    settings.width                  = 2;
    settings.depth                  = 1;
    settings.spacing                = 1;
    settings.spawnDensity           = 1;
    settings.jitterPercent          = 0;
    settings.failureRate            = 0;
    settings.minimumFitness         = 0;
    settings.minimumInstanceFitness = 0;
    settings.boundsCollisionCheck   = true;
    settings.boundsRadius           = 0.75f;
    settings.minimumDirection = settings.maximumDirection = 0;
    settings.prototype                                    = "rocks";
    settings.namespaceId                                  = 71;
    settings.seed                                         = 5;
    TerrainObjectInstanceSettings child;
    child.asset          = "rock-a";
    child.minimumOffsetX = child.maximumOffsetX = -0.1f;
    child.minimumOffsetZ = child.maximumOffsetZ = 0;
    settings.instances.push_back(child);
    return settings;
}

}  // namespace

TEST_CASE("procgen.terrainObject.export emits only accepted children with stable identity") {
    auto     settings = objects();
    auto     fitness  = filled(5, 5, 1);
    auto     heights  = filled(3, 3, 0.25f);
    PointSet first;
    auto     result = exportTerrainObjectPoints(first, fitness, heights, settings);
    REQUIRE(result.ok());
    CHECK_EQ(result.value(), 1);
    REQUIRE_EQ(first.getCount(), 1);
    CHECK(std::fabs(first.points()[0].x - 0.9f) < 1e-6f);
    CHECK(std::fabs(first.points()[0].z) < 1e-6f);

    PointSet again;
    auto     repeated = exportTerrainObjectPoints(again, fitness, heights, settings);
    REQUIRE(repeated.ok());
    REQUIRE_EQ(again.getCount(), first.getCount());
    CHECK_EQ(again.getPointId(0), first.getPointId(0));
    CHECK_EQ(again.points()[0].x, first.points()[0].x);
}

TEST_CASE("procgen.terrainObject.export publishes all children or preserves prior output") {
    auto settings  = objects();
    settings.width = settings.depth = 4.f;
    settings.startOffsetX = settings.startOffsetZ = 1.f;
    settings.instances.front().minimumInstances   = 2;
    settings.instances.front().maximumInstances   = 2;
    settings.instances.front().minimumOffsetZ = settings.instances.front().maximumOffsetZ = 0.1f;
    settings.instances.front().scaleMode             = TerrainObjectScaleMode::FitnessRandomized;
    settings.instances.front().scaleRandomPercentage = 0.2f;
    auto     fitness                                 = filled(5, 5, 1);
    auto     heights                                 = filled(3, 3, 0);
    PointSet output;
    auto     emitted = exportTerrainObjectPoints(output, fitness, heights, settings);
    REQUIRE(emitted.ok());
    CHECK_EQ(emitted.value(), 8);
    REQUIRE_EQ(output.getCount(), 8);
    const auto firstId = output.getPointId(0);

    settings.maxPoints = 1;
    CHECK(!exportTerrainObjectPoints(output, fitness, heights, settings).ok());
    CHECK_EQ(output.getCount(), 8);
    CHECK_EQ(output.getPointId(0), firstId);
    settings.maxPoints       = 100;
    auto invalidFitness      = filled(5, 5, 1);
    invalidFitness.data()[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!exportTerrainObjectPoints(output, invalidFitness, heights, settings).ok());
    CHECK_EQ(output.getCount(), 8);
}

TEST_CASE("procgen.terrainObject.multiTile shares collision semantics with single export") {
    auto                           settings = objects();
    auto                           heights  = filled(3, 3, 0.25f);
    PointSet                       output;
    std::vector<TerrainObjectTile> tiles{{"one", &output, &heights, 0., 0., 2., 1., 2, 1, false}};
    TerrainStampSettings           area;
    area.centerX = 1.f;
    area.centerZ = 0.5f;
    area.width   = 2.f;
    area.depth   = 1.f;
    auto mapped =
        applyTerrainObjectsMultiTile(tiles, filled(2, 1, 1.f), settings, area, TerrainObjectOperationMode::Add, false);
    REQUIRE(mapped.ok());
    REQUIRE_EQ(output.getCount(), 1);
    CHECK(std::fabs(output.points()[0].x - 0.9f) < 1e-6f);
    CHECK(std::fabs(output.points()[0].z) < 1e-6f);
}
