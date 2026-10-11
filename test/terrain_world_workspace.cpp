#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"

using namespace eve::procgen;

namespace {
Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
TerrainWorldCreationSettings smallWorld() {
    TerrainWorldCreationSettings s;
    s.tilesX = 2; s.tilesZ = 1; s.tileSize = 2; s.tileHeight = 8;
    s.heightmapResolution = 3; s.controlTextureResolution = 2; s.detailResolution = 2;
    s.treeResolution = 2; s.objectResolution = 2; s.splatLayers = 2; s.nameSuffix = "stable";
    return s;
}
TerrainStampSettings wholeWorld() {
    TerrainStampSettings s; s.centerX = 0; s.centerZ = 0; s.width = 4; s.depth = 2; return s;
}
}  // namespace

TEST_CASE("procgen.terrainWorld.createsCenteredPcgTopologyAtomically") {
    TerrainWorldWorkspace world;
    auto settings = smallWorld();
    REQUIRE(world.create(settings).ok());
    CHECK(world.getTileCount() == 2);
    CHECK(world.getTileName(0).value() == "Terrain_0_0-stable");
    CHECK(world.getTileName(1).value() == "Terrain_1_0-stable");
    CHECK(world.getTileOriginX(0).value() == -2);
    CHECK(world.getTileOriginX(1).value() == 0);
    CHECK(world.getTileOriginZ(0).value() == -1);
    Heightmap height;
    REQUIRE(world.copyHeightmap(0, height).ok());
    CHECK(height.getWidth() == 3);
    CHECK(height.getHeight() == 3);
    settings.heightmapResolution = 4;
    CHECK(!world.create(settings).ok());
    CHECK(world.getTileCount() == 2);
    CHECK(world.getTileName(0).value() == "Terrain_0_0-stable");

    settings = smallWorld();
    settings.worldMap = true;
    REQUIRE(world.create(settings).ok());
    CHECK(world.getTileName(0).value() == "World Map_0_0-stable");
}

TEST_CASE("procgen.terrainWorld.ownsCrossDomainHistory") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto operation = wholeWorld();
    auto paint = filled(4, 2, 0.75F);
    REQUIRE(world.paintSplat(paint, 1, operation).ok());
    TerrainSplatmap splat;
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);

    TerrainObjectPlacementSettings objects;
    objects.spacing = objects.spawnDensity = 1; objects.jitterPercent = 0;
    objects.failureRate = objects.minimumFitness = objects.minimumInstanceFitness = 0;
    objects.boundsRadius = 0.2F; objects.prototype = "rocks"; objects.namespaceId = 501; objects.maxPoints = 100;
    TerrainObjectInstanceSettings child; child.asset = "rock-a"; REQUIRE(objects.addInstance(child).ok());
    REQUIRE(world.applyObjects(paint, objects, operation, TerrainObjectOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(!points.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(points.empty());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0);
    REQUIRE(world.redo().ok()); REQUIRE(world.redo().ok());
    CHECK(world.getOperationCount() == 2);
    CHECK(world.getAppliedCount() == 2);

    auto invalidPaint = filled(3, 3, 1);
    CHECK(!world.paintSplat(invalidPaint, 1, operation).ok());
    CHECK(world.getOperationCount() == 2);
    CHECK(world.getAppliedCount() == 2);
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(!points.empty());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);
}

TEST_CASE("procgen.terrainWorld.executesCreationAndHistoryThroughRealVm") {
    TerrainWorldWorkspace world;
    Heightmap output;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWorldCreationSettings();s.tilesX=2;s.tilesZ=1;s.tileSize=2.0;s.tileHeight=8.0;
        s.heightmapResolution=3;s.controlTextureResolution=2;s.detailResolution=2;
        s.treeResolution=2;s.objectResolution=2;s.splatLayers=2;s.nameSuffix="vm";
        assert(world().create(s).value==2);
        assert(world().getTileName(0).value=="Terrain_0_0-vm");
        assert(world().getTileOriginX(0).value==-2.0 && world().getTileOriginX(1).value==0.0);
        assert(world().copyHeightmap(0,output()).value==9);
        assert(world().getTileCount()==2 && world().getOperationCount()==0);
    )"));
}

TEST_CASE("procgen.terrainHeightAdjuster.setsWorldUnitsAtomicallyAndBinds") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto adjusted = world.setHeightWorldUnits(250, 1000);
    REQUIRE(adjusted.ok());
    CHECK(adjusted.value() == 18);
    Heightmap output;
    REQUIRE(world.copyHeightmap(0, output).ok());
    for (float value : output.data()) CHECK(value == 0.25F);
    REQUIRE(world.setHeightWorldUnits(2000, 1000).ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    for (float value : output.data()) CHECK(value == 1);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    CHECK(output.height(1, 1) == 0.25F);
    CHECK(!world.setHeightWorldUnits(1, 0).ok());
    CHECK(!world.setHeightWorldUnits(std::numeric_limits<float>::quiet_NaN(), 1000).ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    CHECK(output.height(1, 1) == 0.25F);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(world().setHeightWorldUnits(400.0,1000.0).value==18);
        assert(world().copyHeightmap(0,output()).ok && output().height(1,1)==0.4);
        assert(world().undo().ok && world().copyHeightmap(0,output()).ok);
        assert(output().height(1,1)==0.25);
    )"));
}

TEST_CASE("procgen.terrainWorld.routesHeightDetailAndTreeDomains") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto operation = wholeWorld();
    auto one = filled(1, 1, 1), stamp = filled(1, 1, 0.5F);
    REQUIRE(world.stamp(stamp, operation, one, one).ok());
    Heightmap heights;
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(heights.height(1, 1) > 0);

    auto fitness = filled(4, 2, 1);
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0; detail.density = 4; detail.namespaceId = 701;
    REQUIRE(world.applyDetail(fitness, detail, operation, TerrainDetailMode::Replace, 17).ok());
    TerrainDetailLayer details;
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 4);
    auto flowers = detail;
    flowers.density = 2;
    flowers.namespaceId = 702;
    REQUIRE(world.applyDetail(fitness, flowers, operation, TerrainDetailMode::Add, 19).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 6);
    TerrainWorldClearSettings detailSourceClear;
    detailSourceClear.trees = detailSourceClear.objects = false;
    detailSourceClear.sourceNamespace = 701;
    REQUIRE(world.clearSpawns(detailSourceClear).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 2);
    REQUIRE(world.undo().ok());

    TerrainTreePlacementSettings trees;
    trees.spacing = trees.spawnDensity = 1; trees.jitterPercent = trees.failureRate = trees.minimumFitness = 0;
    trees.minimumWidth = trees.maximumWidth = trees.minimumHeight = trees.maximumHeight = 1;
    trees.asset = "oak"; trees.namespaceId = 601; trees.maxPoints = 100;
    REQUIRE(world.applyTrees(fitness, trees, operation, TerrainTreeOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(!points.empty());
    CHECK(world.getOperationCount() == 4);

    auto pines = trees;
    pines.asset = "pine";
    pines.namespaceId = 602;
    REQUIRE(world.applyTrees(fitness, pines, operation, TerrainTreeOperationMode::Add).ok());
    TerrainWorldClearSettings sourceClear;
    sourceClear.details = sourceClear.objects = false;
    sourceClear.sourceNamespace = 601;
    REQUIRE(world.clearSpawns(sourceClear).ok());
    REQUIRE(world.copyTrees(0, points).ok());
    REQUIRE(!points.empty());
    for (int i = 0; i < points.getCount(); ++i)
        CHECK(points.getStringAttribute(i, "spawnNamespace", "") == "602");
    REQUIRE(world.undo().ok());

    TerrainWorldClearSettings clear;
    clear.objects = false;
    REQUIRE(world.clearSpawns(clear).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 0);
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(points.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(!points.empty());

    REQUIRE(world.flatten().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    for (float value : heights.data()) CHECK(value == 0);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(heights.height(1, 1) > 0);
}
