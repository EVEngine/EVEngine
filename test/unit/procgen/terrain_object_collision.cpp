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

#include <cmath>
#include <iostream>
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
namespace {
Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (auto& sample : result.data()) sample = value;
    return result;
}
TerrainObjectPlacementSettings settings() {
    TerrainObjectPlacementSettings value;
    value.width = value.depth = 2;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius                                                                            = .2f;
    value.prototype                                                                               = "rocks";
    value.namespaceId                                                                             = 71;
    value.seed                                                                                    = 5;
    TerrainObjectInstanceSettings child;
    child.asset = "rock-a";
    value.instances.push_back(child);
    return value;
}
}  // namespace
TEST_CASE("procgen.terrainObject.supportsPrototypeChildrenCollisionAndAtomicFailure") {
    auto fitness = filled(5, 5, 1), heights = filled(3, 3, 0);
    auto config                 = settings();
    config.boundsCollisionCheck = true;
    config.boundsRadius         = 0.75F;
    config.width = config.depth = 4.f;
    config.startOffsetX = config.startOffsetZ = 1.f;
    config.minimumDirection = config.maximumDirection = 0.f;
    config.instances.front().minimumInstances = config.instances.front().maximumInstances = 2;
    config.instances.front().minimumOffsetX = config.instances.front().maximumOffsetX = -0.1F;
    config.instances.front().minimumOffsetZ = config.instances.front().maximumOffsetZ = 0.1F;
    config.instances.front().scaleMode             = TerrainObjectScaleMode::FitnessRandomized;
    config.instances.front().scaleRandomPercentage = 0.2F;
    config.instances.front().minimumRotationY      = -10;
    config.instances.front().maximumRotationY      = 10;
    PointSet output;
    auto     emitted = exportTerrainObjectPoints(output, fitness, heights, config);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 8);
    PointSet preserved = output;
    config.maxPoints   = 1;
    REQUIRE(!exportTerrainObjectPoints(output, fitness, heights, config).ok());
    REQUIRE(output.getCount() == preserved.getCount());
    REQUIRE(output.getPointId(0) == preserved.getPointId(0));
}

TEST_CASE("procgen.terrainObject.emptyCandidatesDoNotReserveCollisionSpace") {
    auto config                 = settings();
    config.depth                = 1.f;
    config.boundsCollisionCheck = true;
    config.boundsRadius         = .75f;
    config.minimumDirection = config.maximumDirection = 0.f;
    config.instances.front().minimumOffsetX = config.instances.front().maximumOffsetX = -.1f;
    config.instances.front().minimumOffsetZ = config.instances.front().maximumOffsetZ = 0.f;
    auto     heights                                                                  = filled(3, 3, .25f);
    PointSet single;
    auto     result = exportTerrainObjectPoints(single, filled(5, 5, 1.f), heights, config);
    REQUIRE(result.ok());
    REQUIRE_EQ(single.getCount(), 1);
    const float                    singleX = single.points().front().x;
    const float                    singleZ = single.points().front().z;
    PointSet                       multi;
    std::vector<TerrainObjectTile> tiles{{"one", &multi, &heights, 0., 0., 2., 1., 2, 1, false}};
    TerrainStampSettings           operation;
    operation.centerX = 1.f;
    operation.centerZ = .5f;
    operation.width   = 2.f;
    operation.depth   = 1.f;
    auto mapped       = applyTerrainObjectsMultiTile(tiles, filled(2, 1, 1.f), config, operation,
                                                     TerrainObjectOperationMode::Add, false);
    REQUIRE(mapped.ok());
    REQUIRE_EQ(multi.getCount(), 1);
    if (std::fabs(singleX - .9f) >= 1e-6f || std::fabs(multi.points().front().x - .9f) >= 1e-6f)
        std::cerr << "singleX=" << singleX << " multiX=" << multi.points().front().x << "\n";
    REQUIRE(std::fabs(singleX - .9f) < 1e-6f);
    REQUIRE(std::fabs(singleZ) < 1e-6f);
    REQUIRE(std::fabs(multi.points().front().x - .9f) < 1e-6f);
    REQUIRE(std::fabs(multi.points().front().z) < 1e-6f);
}
