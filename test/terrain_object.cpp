#include "zeroerr/unittest.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>

#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"

using namespace eve::procgen;

namespace {
Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
TerrainObjectPlacementSettings settings() {
    TerrainObjectPlacementSettings value;
    value.width = value.depth = 2;
    value.spacing = 1;
    value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius = 0.2F;
    value.prototype = "rocks";
    value.namespaceId = 71;
    value.seed = 5;
    TerrainObjectInstanceSettings instance;
    instance.asset = "rock-a";
    instance.minimumInstances = instance.maximumInstances = 1;
    value.instances.push_back(instance);
    return value;
}
}  // namespace

TEST_CASE("procgen.terrainObject.exportsStableAttributedTransformsAndRemoval") {
    auto fitness = filled(3, 3, 1), heights = filled(2, 2, 0.25F);
    auto config = settings();
    PointSet first, second;
    auto emitted = exportTerrainObjectPoints(first, fitness, heights, config);
    REQUIRE(emitted.ok());
    CHECK(emitted.value() == 9);
    REQUIRE(exportTerrainObjectPoints(second, fitness, heights, config).ok());
    CHECK(first.getCount() == second.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.getPointId(i) == second.getPointId(i));
        CHECK(first.getX(i) == second.getX(i));
        CHECK(first.getY(i) == 0.25F);
        CHECK(first.getStringAttribute(i, "asset", "") == "rock-a");
        CHECK(first.getStringAttribute(i, "objectPrototype", "") == "rocks");
        CHECK(first.hasIntAttribute(i, "objectCandidate"));
        CHECK(first.getIntAttribute(i, "objectResource", -1) == 0);
    }
    PointSet removed;
    auto count = removeTerrainObjectPoints(removed, first, fitness, config, 0.5F);
    REQUIRE(count.ok());
    CHECK(count.value() == 9);
    CHECK(removed.empty());
}

TEST_CASE("procgen.terrainObject.supportsPrototypeChildrenCollisionAndAtomicFailure") {
    auto fitness = filled(5, 5, 1), heights = filled(3, 3, 0);
    auto config = settings();
    config.boundsCollisionCheck = true;
    config.boundsRadius = 0.75F;
    config.instances.front().minimumInstances = config.instances.front().maximumInstances = 2;
    config.instances.front().minimumOffsetX = config.instances.front().maximumOffsetX = -0.1F;
    config.instances.front().minimumOffsetZ = config.instances.front().maximumOffsetZ = 0.1F;
    config.instances.front().scaleMode = TerrainObjectScaleMode::FitnessRandomized;
    config.instances.front().scaleRandomPercentage = 0.2F;
    config.instances.front().minimumRotationY = -10;
    config.instances.front().maximumRotationY = 10;
    PointSet output;
    auto emitted = exportTerrainObjectPoints(output, fitness, heights, config);
    REQUIRE(emitted.ok());
    CHECK(emitted.value() == 8);
    PointSet preserved = output;
    config.maxPoints = 1;
    CHECK(!exportTerrainObjectPoints(output, fitness, heights, config).ok());
    CHECK(output.getCount() == preserved.getCount());
    CHECK(output.getPointId(0) == preserved.getPointId(0));
}

TEST_CASE("procgen.terrainObject.scriptConfiguresAndExecutesRealVm") {
    auto fitness = filled(3, 3, 1), heights = filled(2, 2, 0.25F);
    PointSet output;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("pointCount", [](const PointSet* points) { return points->getCount(); });
    vm.run(vm.compileSource(R"(
        local child=eve.TerrainObjectInstanceSettings();
        child.asset="rock-a"; child.minimumInstances=1; child.maximumInstances=1;
        child.setScaleMode(0); child.setYOffsetMode(0);
        local s=eve.TerrainObjectPlacementSettings();
        s.width=2.0;s.depth=2.0;s.spacing=1.0;s.spawnDensity=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.minimumInstanceFitness=0.0;s.prototype="rocks";s.namespaceId=81;
        assert(s.addInstance(child).ok);
        assert(s.exportPoints(output(),fitness(),heights()).value==9);
        assert(pointCount(output())==9);
        assert(s.removePoints(output(),output(),fitness(),0.5).value==9);
        assert(pointCount(output())==0);
    )"));
}

TEST_CASE("procgen.terrainObject.multiTileSharesRngCollisionAndOwnsHistory") {
    PointSet empty, west, east;
    auto heights = filled(2, 2, 0.25F), fitness = filled(4, 2, 1);
    auto config = settings();
    config.boundsCollisionCheck = true;
    config.boundsRadius = 0.6F;
    config.maxPoints = 100;
    TerrainStampSettings bounds;
    bounds.centerX = 2; bounds.centerZ = 1; bounds.width = 4; bounds.depth = 2;
    TerrainMultiObjectWorkspace workspace;
    REQUIRE(workspace.addTile("west", empty, heights, 0, 0, 2, 2, 2, 2).ok());
    REQUIRE(workspace.addTile("east", empty, heights, 2, 0, 2, 2, 2, 2).ok());
    auto added = workspace.apply(fitness, config, bounds, TerrainObjectOperationMode::Add);
    REQUIRE(added.ok());
    REQUIRE(workspace.copyTile("west", west).ok());
    REQUIRE(workspace.copyTile("east", east).ok());
    CHECK(added.value() == west.getCount() + east.getCount());
    CHECK(added.value() > 0);
    for (const auto& a : west.points()) for (const auto& b : east.points())
        CHECK(std::hypot(double(a.x) - b.x, double(a.z) - b.z) >= 1.2 - 1e-5);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("west", west).ok());
    CHECK(west.empty());
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getAppliedCount() == 1);
    REQUIRE(workspace.apply(fitness, config, bounds, TerrainObjectOperationMode::Replace).ok());
    CHECK(!workspace.redo().ok());
}

TEST_CASE("procgen.terrainObject.multiTileExecutesThroughRealVm") {
    PointSet empty, output;
    auto heights = filled(2, 2, 0.25F), fitness = filled(4, 2, 1);
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("emptyObjects", [&]() { return &empty; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("pointCount", [](const PointSet* points) { return points->getCount(); });
    vm.run(vm.compileSource(R"(
        local child=eve.TerrainObjectInstanceSettings();child.asset="rock-a";
        local s=eve.TerrainObjectPlacementSettings();s.spacing=1.0;s.spawnDensity=1.0;
        s.jitterPercent=0.0;s.minimumFitness=0.0;s.minimumInstanceFitness=0.0;
        s.prototype="rocks";s.namespaceId=92;s.boundsRadius=0.2;s.maxPoints=100;
        assert(s.addInstance(child).ok);
        local op=eve.TerrainStampSettings();op.setCenter(2.0,1.0);op.setSize(4.0,2.0);
        local w=eve.TerrainMultiObjectWorkspace();
        assert(w.addTile("west",emptyObjects(),heights(),0.0,0.0,2.0,2.0,2,2,false).ok);
        assert(w.addTile("east",emptyObjects(),heights(),2.0,0.0,2.0,2.0,2,2,false).ok);
        assert(w.apply(fitness(),s,op,0,false).ok);
        assert(w.copyTile("east",output()).ok && pointCount(output())>0);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2);
    )"));
}
