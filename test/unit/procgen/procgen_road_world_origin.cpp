// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_road_world_origin.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#include "procgen/PointSet.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadTerrainWorkspace.h"

#include <cmath>
#include <limits>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {

TerrainWorldCreationSettings worldSettings() {
    TerrainWorldCreationSettings settings;
    settings.originY                  = 100.f;
    settings.tileSize                 = 4.f;
    settings.tileHeight               = 10.f;
    settings.heightmapResolution      = 5;
    settings.controlTextureResolution = 4;
    settings.detailResolution         = 4;
    settings.treeResolution           = 4;
    settings.objectResolution         = 4;
    settings.splatLayers              = 2;
    return settings;
}

Heightmap fitness() {
    Heightmap map(4, 4);
    for (auto& sample : map.data()) sample = 1.f;
    return map;
}

TerrainStampSettings area() {
    TerrainStampSettings settings;
    settings.width = settings.depth = 4.f;
    return settings;
}

TerrainTreePlacementSettings tree() {
    TerrainTreePlacementSettings settings;
    settings.asset       = "oak";
    settings.heightScale = 10.f;
    settings.spacing = settings.spawnDensity = 1.f;
    settings.jitterPercent = settings.failureRate = settings.minimumFitness = 0.f;
    settings.minimumYOffset = settings.maximumYOffset = 0.f;
    settings.namespaceId                              = 201;
    return settings;
}

TerrainObjectPlacementSettings object() {
    TerrainObjectPlacementSettings settings;
    settings.prototype   = "rocks";
    settings.namespaceId = 202;
    settings.heightScale = 10.f;
    settings.spacing = settings.spawnDensity = 1.f;
    settings.jitterPercent                   = 0.f;
    settings.failureRate = settings.minimumFitness = settings.minimumInstanceFitness = 0.f;
    TerrainObjectInstanceSettings child;
    child.asset = "rock";
    auto added  = settings.addInstance(child);
    REQUIRE(added.ok());
    return settings;
}

TerrainProbePlacementSettings probe() {
    TerrainProbePlacementSettings settings;
    settings.type          = TerrainProbeType::Light;
    settings.name          = "light";
    settings.heightScale   = 10.f;
    settings.spacing       = 1.f;
    settings.jitterPercent = settings.minimumFitness = 0.f;
    settings.namespaceId                             = 203;
    return settings;
}

void expectElevation(const PointSet& points, float expected) {
    REQUIRE(!points.empty());
    for (const auto& point : points.points()) CHECK(std::fabs(point.y - expected) < 1e-5f);
}

MeshBuild roadSurface() {
    MeshBuild mesh;
    mesh.setActiveGroup("asphalt");
    for (const auto& point : {RoadControlPoint{-1.f, 106.f, -1.f}, RoadControlPoint{1.f, 106.f, -1.f},
                              RoadControlPoint{1.f, 106.f, 1.f}, RoadControlPoint{-1.f, 106.f, 1.f}})
        mesh.addVertex(point.x, point.y, point.z, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}

}  // namespace

TEST_CASE("procgen.road.worldOrigin converts absolute elevation to normalized tile height") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    auto origin = world.getTileOriginY(0);
    REQUIRE(origin.ok());
    CHECK_EQ(origin.value(), 100.0);
    CHECK(!world.getTileOriginY(1).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    Heightmap heights;
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK_EQ(heights.height(2, 2), 0.5f);

    const auto operations = world.getOperationCount();
    CHECK(!world.setHeightWorldUnits(std::numeric_limits<float>::infinity(), 10.f).ok());
    CHECK_EQ(world.getOperationCount(), operations);
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK_EQ(heights.height(2, 2), 0.5f);
}

TEST_CASE("procgen.road.worldOrigin terrain-relative trees objects and probes use one baseline") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    const auto mask   = fitness();
    const auto window = area();
    REQUIRE(world.applyTrees(mask, tree(), window, TerrainTreeOperationMode::Add).ok());
    REQUIRE(world.applyObjects(mask, object(), window, TerrainObjectOperationMode::Add).ok());
    REQUIRE(world.applyProbes(mask, probe(), window, TerrainProbeOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    expectElevation(points, 105.f);
    REQUIRE(world.copyObjects(0, points).ok());
    expectElevation(points, 105.f);
    REQUIRE(world.copyProbes(0, points).ok());
    expectElevation(points, 107.5f);
}

TEST_CASE("procgen.road.worldOrigin road conform and undo restore height and tree occupancy together") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    REQUIRE(world.applyTrees(fitness(), tree(), area(), TerrainTreeOperationMode::Add).ok());
    PointSet before;
    REQUIRE(world.copyTrees(0, before).ok());
    REQUIRE(!before.empty());

    RoadTerrainWriteOptions options;
    options.blendDistance = 0;
    options.splatLayer    = 1;
    REQUIRE(world.conformRoad(roadSurface(), options).ok());
    Heightmap heights;
    PointSet  after;
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(std::fabs(heights.height(2, 2) - 0.6f) < 1e-6f);
    REQUIRE(world.copyTrees(0, after).ok());
    CHECK(after.getCount() < before.getCount());

    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK_EQ(heights.height(2, 2), 0.5f);
    REQUIRE(world.copyTrees(0, after).ok());
    CHECK_EQ(after.getCount(), before.getCount());
    REQUIRE(world.redo().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(std::fabs(heights.height(2, 2) - 0.6f) < 1e-6f);
}

TEST_CASE("procgen.road.worldOrigin staged spawn hides intermediate candidates and rejects invalid re-create") {
    TerrainWorldWorkspace world;
    auto                  settings = worldSettings();
    REQUIRE(world.create(settings).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    TerrainSpawnPlan plan;
    REQUIRE(plan.addTrees("trees", fitness(), tree(), area(), TerrainTreeOperationMode::Add).ok());
    REQUIRE(plan.addObjects("objects", fitness(), object(), area(), TerrainObjectOperationMode::Add).ok());
    REQUIRE(plan.addProbes("probes", fitness(), probe(), area(), TerrainProbeOperationMode::Add).ok());
    REQUIRE(world.beginSpawn(plan).ok());
    auto first = world.stepSpawn(1);
    REQUIRE(first.ok());
    CHECK(first.value() == TerrainWorldRunStatus::Pending);
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(points.empty());
    REQUIRE(world.stepSpawn(1).ok());
    auto last = world.stepSpawn(1);
    REQUIRE(last.ok());
    CHECK(last.value() == TerrainWorldRunStatus::Completed);
    REQUIRE(world.copyTrees(0, points).ok());
    expectElevation(points, 105.f);
    REQUIRE(world.copyObjects(0, points).ok());
    expectElevation(points, 105.f);
    REQUIRE(world.copyProbes(0, points).ok());
    expectElevation(points, 107.5f);

    const auto count = world.getOperationCount();
    settings.originY = std::numeric_limits<float>::infinity();
    CHECK(!world.create(settings).ok());
    CHECK_EQ(world.getOperationCount(), count);
    auto origin = world.getTileOriginY(0);
    REQUIRE(origin.ok());
    CHECK_EQ(origin.value(), 100.0);
}

TEST_CASE("procgen.road.worldOrigin explicit sea and custom elevation modes remain absolute") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    auto trees          = tree();
    trees.snapToTerrain = false;
    trees.yOffsetMode   = TerrainTreeYOffsetMode::SeaLevel;
    trees.seaLevel      = 200.f;
    REQUIRE(world.applyTrees(fitness(), trees, area(), TerrainTreeOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    expectElevation(points, 200.f);

    auto objects                      = object();
    objects.instances[0].yOffsetMode  = TerrainObjectYOffsetMode::Custom;
    objects.instances[0].customOffset = 7.f;
    REQUIRE(world.applyObjects(fitness(), objects, area(), TerrainObjectOperationMode::Add).ok());
    REQUIRE(world.copyObjects(0, points).ok());
    expectElevation(points, 7.f);

    auto probes           = probe();
    probes.type           = TerrainProbeType::Reflection;
    probes.seaLevelActive = true;
    probes.seaLevel       = 200.f;
    REQUIRE(world.applyProbes(fitness(), probes, area(), TerrainProbeOperationMode::Add).ok());
    REQUIRE(world.copyProbes(0, points).ok());
    expectElevation(points, 201.f);
}
