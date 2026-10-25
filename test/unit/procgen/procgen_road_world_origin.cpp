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

#include <cmath>
#include <limits>
#include "procgen/PointSet.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
TerrainWorldCreationSettings worldSettings() {
    TerrainWorldCreationSettings s;
    s.originY                  = 100.f;
    s.tileSize                 = 4.f;
    s.tileHeight               = 10.f;
    s.heightmapResolution      = 5;
    s.controlTextureResolution = s.detailResolution = 4;
    s.treeResolution = s.objectResolution = 4;
    s.splatLayers                         = 2;
    return s;
}
Heightmap fitnessMap() {
    Heightmap h(4, 4);
    for (auto& v : h.data()) v = 1.f;
    return h;
}
TerrainStampSettings window() {
    TerrainStampSettings s;
    s.width = s.depth = 4.f;
    return s;
}
TerrainTreePlacementSettings treeSettings() {
    TerrainTreePlacementSettings s;
    s.asset       = "oak";
    s.heightScale = 10.f;
    s.spacing = s.spawnDensity = 1.f;
    s.jitterPercent = s.failureRate = s.minimumFitness = 0.f;
    s.minimumYOffset = s.maximumYOffset = 0.f;
    s.namespaceId                       = 201;
    return s;
}
TerrainProbePlacementSettings probeSettings() {
    TerrainProbePlacementSettings s;
    s.type          = TerrainProbeType::Light;
    s.name          = "light";
    s.heightScale   = 10.f;
    s.spacing       = 1.f;
    s.jitterPercent = s.minimumFitness = 0.f;
    s.namespaceId                      = 203;
    return s;
}
MeshBuild roadMesh() {
    MeshBuild m;
    m.setActiveGroup("asphalt");
    for (const auto& p : {RoadControlPoint{-1.f, 106.f, -1.f}, RoadControlPoint{1.f, 106.f, -1.f},
                          RoadControlPoint{1.f, 106.f, 1.f}, RoadControlPoint{-1.f, 106.f, 1.f}})
        m.addVertex(p.x, p.y, p.z, 0.f, 1.f, 0.f, 0.f, 0.f);
    m.addTriangle(0, 1, 2);
    m.addTriangle(0, 2, 3);
    return m;
}
}  // namespace
TEST_CASE("procgen.road.worldOrigin.roadAndPlacementsShareBaselineAndUndo") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    Heightmap heights;
    REQUIRE(world.copyHeightmap(0, heights).ok());
    REQUIRE_EQ(heights.height(2, 2), .5f);
    auto origin = world.getTileOriginY(0);
    REQUIRE(origin.ok());
    REQUIRE_EQ(origin.value(), 100.);
    REQUIRE(!world.getTileOriginY(1).ok());
    auto fitness   = fitnessMap();
    auto operation = window();
    REQUIRE(world.applyTrees(fitness, treeSettings(), operation, TerrainTreeOperationMode::Add).ok());
    PointSet trees;
    REQUIRE(world.copyTrees(0, trees).ok());
    REQUIRE(!trees.empty());
    for (const auto& p : trees.points()) REQUIRE(std::fabs(p.y - 105.f) < 1e-5f);
    TerrainObjectPlacementSettings objects;
    objects.prototype   = "rocks";
    objects.namespaceId = 202;
    objects.heightScale = 10.f;
    objects.spacing = objects.spawnDensity = 1.f;
    objects.jitterPercent                  = 0;
    objects.failureRate = objects.minimumFitness = objects.minimumInstanceFitness = 0;
    objects.boundsRadius                                                          = .2f;
    TerrainObjectInstanceSettings child;
    child.asset = "rock";
    REQUIRE(objects.addInstance(child).ok());
    REQUIRE(world.applyObjects(fitness, objects, operation, TerrainObjectOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyObjects(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& p : points.points()) REQUIRE(std::fabs(p.y - 105.f) < 1e-5f);
    REQUIRE(world.applyProbes(fitness, probeSettings(), operation, TerrainProbeOperationMode::Add).ok());
    REQUIRE(world.copyProbes(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& p : points.points()) REQUIRE(std::fabs(p.y - 107.5f) < 1e-5f);
    RoadTerrainWriteOptions write;
    write.blendDistance = 0;
    write.splatLayer    = 1;
    REQUIRE(world.conformRoad(roadMesh(), write).ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    REQUIRE(std::fabs(heights.height(2, 2) - .6f) < 1e-6f);
    PointSet remaining;
    REQUIRE(world.copyTrees(0, remaining).ok());
    REQUIRE_LT(remaining.getCount(), trees.getCount());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    REQUIRE_EQ(heights.height(2, 2), .5f);
    REQUIRE(world.copyTrees(0, remaining).ok());
    REQUIRE_EQ(remaining.getCount(), trees.getCount());
    REQUIRE(world.redo().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    REQUIRE(std::fabs(heights.height(2, 2) - .6f) < 1e-6f);
}
TEST_CASE("procgen.road.worldOrigin.stagedSpawnRetainsBaselineAndRejectsInvalidCreate") {
    TerrainWorldWorkspace world;
    auto                  settings = worldSettings();
    REQUIRE(world.create(settings).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    auto             fitness   = fitnessMap();
    auto             operation = window();
    TerrainSpawnPlan plan;
    REQUIRE(plan.addTrees("trees", fitness, treeSettings(), operation, TerrainTreeOperationMode::Add).ok());
    TerrainObjectPlacementSettings objects;
    objects.prototype   = "rocks";
    objects.namespaceId = 202;
    objects.heightScale = 10.f;
    objects.spacing = objects.spawnDensity = 1.f;
    objects.jitterPercent                  = 0;
    objects.failureRate = objects.minimumFitness = objects.minimumInstanceFitness = 0;
    TerrainObjectInstanceSettings child;
    child.asset = "rock";
    REQUIRE(objects.addInstance(child).ok());
    REQUIRE(plan.addObjects("objects", fitness, objects, operation, TerrainObjectOperationMode::Add).ok());
    REQUIRE(plan.addProbes("probes", fitness, probeSettings(), operation, TerrainProbeOperationMode::Add).ok());
    REQUIRE(world.beginSpawn(plan).ok());
    auto first = world.stepSpawn(1);
    REQUIRE(first.ok());
    REQUIRE_EQ(first.value(), TerrainWorldRunStatus::Pending);
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    REQUIRE(points.empty());
    REQUIRE(world.stepSpawn(1).ok());
    auto last = world.stepSpawn(1);
    REQUIRE(last.ok());
    REQUIRE_EQ(last.value(), TerrainWorldRunStatus::Completed);
    REQUIRE(world.copyTrees(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& p : points.points()) REQUIRE(std::fabs(p.y - 105.f) < 1e-5f);
    REQUIRE(world.copyObjects(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& p : points.points()) REQUIRE(std::fabs(p.y - 105.f) < 1e-5f);
    REQUIRE(world.copyProbes(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& p : points.points()) REQUIRE(std::fabs(p.y - 107.5f) < 1e-5f);
    const auto count = world.getOperationCount();
    settings.originY = std::numeric_limits<float>::infinity();
    REQUIRE(!world.create(settings).ok());
    REQUIRE_EQ(world.getOperationCount(), count);
    auto origin = world.getTileOriginY(0);
    REQUIRE(origin.ok());
    REQUIRE_EQ(origin.value(), 100.);
}

TEST_CASE("procgen.road.worldOrigin.explicitAbsoluteHeightModesRemainAbsolute") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.setHeightWorldUnits(105.f, 10.f).ok());
    auto fitness       = fitnessMap();
    auto operation     = window();
    auto tree          = treeSettings();
    tree.snapToTerrain = false;
    tree.yOffsetMode   = TerrainTreeYOffsetMode::SeaLevel;
    tree.seaLevel      = 200.f;
    REQUIRE(world.applyTrees(fitness, tree, operation, TerrainTreeOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& point : points.points()) REQUIRE(std::fabs(point.y - 200.f) < 1e-5f);
    TerrainObjectPlacementSettings objects;
    objects.prototype   = "absolute";
    objects.namespaceId = 301;
    objects.heightScale = 10.f;
    objects.spacing = objects.spawnDensity = 1;
    objects.jitterPercent                  = 0;
    objects.failureRate = objects.minimumFitness = objects.minimumInstanceFitness = 0;
    TerrainObjectInstanceSettings child;
    child.asset        = "rock";
    child.yOffsetMode  = TerrainObjectYOffsetMode::Custom;
    child.customOffset = 7.f;
    REQUIRE(objects.addInstance(child).ok());
    REQUIRE(world.applyObjects(fitness, objects, operation, TerrainObjectOperationMode::Add).ok());
    REQUIRE(world.copyObjects(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& point : points.points()) REQUIRE(std::fabs(point.y - 7.f) < 1e-5f);
    auto probe           = probeSettings();
    probe.type           = TerrainProbeType::Reflection;
    probe.seaLevelActive = true;
    probe.seaLevel       = 200.f;
    REQUIRE(world.applyProbes(fitness, probe, operation, TerrainProbeOperationMode::Add).ok());
    REQUIRE(world.copyProbes(0, points).ok());
    REQUIRE(!points.empty());
    for (const auto& point : points.points()) REQUIRE(std::fabs(point.y - 201.f) < 1e-5f);
}
