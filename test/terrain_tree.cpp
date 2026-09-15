#include <cmath>
#include <limits>

#include <simplesquirrel/simplesquirrel.hpp>

#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

namespace {
TerrainTreePlacementSettings baseSettings() {
    TerrainTreePlacementSettings settings;
    settings.asset             = "tree/oak";
    settings.width             = 20;
    settings.depth             = 20;
    settings.spacing           = 10;
    settings.spawnDensity      = 1;
    settings.jitterPercent     = 0;
    settings.minimumFitness    = 0;
    settings.minimumWidth      = 2;
    settings.maximumWidth      = 4;
    settings.minimumHeight     = 3;
    settings.maximumHeight     = 7;
    settings.healthyR          = 0.8F;
    settings.dryR              = 0.2F;
    settings.bendFactor        = 0.35F;
    settings.boundsRadius      = 1.5F;
    settings.seed              = 17;
    settings.namespaceId       = 91;
    return settings;
}
}  // namespace

TEST_CASE("procgen.tree.fixedFitnessTransformsAndMetadata") {
    Heightmap fitness(2, 2), heights(2, 2);
    fitness.data() = {1, 1, 1, 1};
    heights.data() = {0, 1, 0, 1};
    auto settings = baseSettings();
    settings.scaleMode  = TerrainTreeScaleMode::Fixed;
    settings.heightScale = 10;
    PointSet points;
    auto result = exportTerrainTreePoints(points, fitness, heights, settings);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 9);
    for (int i = 0; i < points.getCount(); ++i) {
        CHECK(points.getPointId(i) != 0);
        CHECK(points.getScaleX(i) == 2);
        CHECK(points.getScaleY(i) == 3);
        CHECK(points.getScaleZ(i) == 2);
        CHECK(std::abs(points.getY(i) - points.getX(i) * 0.5F) < 0.00001F);
        CHECK(points.getColorR(i) == settings.healthyR);
        CHECK(points.getStringAttribute(i, "asset", "") == settings.asset);
        CHECK(points.getIntAttribute(i, "treeCandidate", -1) == i);
        CHECK(points.getFloatAttribute(i, "bendFactor", -1) == settings.bendFactor);
        CHECK(points.getBoundsMaxX(i) == settings.boundsRadius * settings.minimumWidth);
    }
}

TEST_CASE("procgen.tree.scaleOffsetColorAndDeterministicReplay") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 0.75F;
    heights.data()[0] = 0.4F;
    auto settings = baseSettings();
    settings.jitterPercent = 0.4F;
    settings.scaleMode = TerrainTreeScaleMode::FitnessRandomized;
    settings.snapToTerrain = false;
    settings.yOffsetMode = TerrainTreeYOffsetMode::Custom;
    settings.customOffset = 12;
    settings.minimumYOffset = settings.maximumYOffset = -2;
    PointSet first, repeat;
    REQUIRE(exportTerrainTreePoints(first, fitness, heights, settings).ok());
    REQUIRE(exportTerrainTreePoints(repeat, fitness, heights, settings).ok());
    REQUIRE(first.getCount() > 0);
    REQUIRE(first.getCount() == repeat.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.getPointId(i) == repeat.getPointId(i));
        CHECK(first.getX(i) == repeat.getX(i));
        CHECK(first.getZ(i) == repeat.getZ(i));
        CHECK(first.getYaw(i) == repeat.getYaw(i));
        CHECK(first.getScaleX(i) == repeat.getScaleX(i));
        CHECK(first.getY(i) == 10);
        CHECK(std::abs(first.getColorR(i) - 0.65F) < 0.00001F);
    }
}

TEST_CASE("procgen.tree.identityIgnoresPrototypeAppearanceAndFailuresAreAtomic") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    auto settings = baseSettings();
    PointSet original, changed;
    REQUIRE(exportTerrainTreePoints(original, fitness, heights, settings).ok());
    settings.healthyR = 0.4F;
    settings.minimumWidth = settings.maximumWidth = 6;
    REQUIRE(exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    REQUIRE(original.getCount() == changed.getCount());
    for (int i = 0; i < original.getCount(); ++i) {
        CHECK(original.getPointId(i) == changed.getPointId(i));
        CHECK(original.getX(i) == changed.getX(i));
        CHECK(original.getZ(i) == changed.getZ(i));
    }
    const auto beforeId = changed.getPointId(0);
    settings.maximumWidth = 5;
    CHECK(!exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    CHECK(changed.getPointId(0) == beforeId);
    settings.minimumWidth = settings.maximumWidth = 6;
    fitness.data()[0] = 1.1F;
    CHECK(!exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    CHECK(changed.getPointId(0) == beforeId);
    fitness.data()[0] = 1;
    settings.maxPoints = 1;
    CHECK(!exportTerrainTreePoints(changed, fitness, heights, settings).ok());
    CHECK(changed.getPointId(0) == beforeId);
}

TEST_CASE("procgen.tree.scriptSettingsModesAndExport") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    heights.data()[0] = 0.25F;
    PointSet output;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    table.addClass("PointSet", ssq::Class::Ctor<PointSet()>());
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainTreePlacementSettings();s.asset="tree/oak";
        s.width=20.0;s.depth=20.0;s.spacing=10.0;s.jitterPercent=0.0;s.minimumFitness=0.0;
        s.setScaleMode(0);s.setYOffsetMode(2);s.snapToTerrain=false;s.customOffset=7.0;
        s.minimumYOffset=0.0;s.maximumYOffset=0.0;
        s.minimumWidth=2.0;s.maximumWidth=2.0;s.minimumHeight=3.0;s.maximumHeight=3.0;
        assert(s.getScaleMode()==0 && s.getYOffsetMode()==2);
        local result=eve.exportTerrainTreePoints(output(),fitness(),heights(),s);
        assert(result.ok && result.value==9);
    )"));
    CHECK(output.getCount() == 9);
    CHECK(output.getY(0) == 7);
    CHECK(output.getScaleX(0) == 2);
}

TEST_CASE("procgen.tree.removeIsAssetScopedStrictAndAliasSafe") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    auto settings = baseSettings();
    settings.scaleMode = TerrainTreeScaleMode::Fixed;
    PointSet points;
    REQUIRE(exportTerrainTreePoints(points, fitness, heights, settings).ok());
    const int foreign = points.add(5, 0, 5);
    points.setStringAttribute(foreign, "asset", "tree/pine");
    const auto foreignId = points.getPointId(foreign);
    REQUIRE(removeTerrainTreePoints(points, points, fitness, settings).ok());
    REQUIRE(points.getCount() == 1);
    CHECK(points.getStringAttribute(0, "asset", "") == "tree/pine");
    CHECK(points.getPointId(0) == foreignId);

    PointSet unchanged;
    settings.minimumFitness = 1;
    REQUIRE(removeTerrainTreePoints(unchanged, points, fitness, settings).ok());
    CHECK(unchanged.getCount() == 1);
    fitness.data()[0] = -0.1F;
    CHECK(!removeTerrainTreePoints(unchanged, points, fitness, settings).ok());
    CHECK(unchanged.getCount() == 1);
}

TEST_CASE("procgen.tree.prototypeRefreshPreservesIdentityAndMapsScale") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    auto placement = baseSettings();
    placement.scaleMode = TerrainTreeScaleMode::Fitness;
    PointSet points;
    REQUIRE(exportTerrainTreePoints(points, fitness, heights, placement).ok());
    REQUIRE(points.getCount() > 0);
    const auto id = points.getPointId(0);
    const auto x = points.getX(0), yaw = points.getYaw(0), color = points.getColorR(0);
    TerrainTreeRescaleSettings refresh;
    refresh.asset = placement.asset;
    refresh.previousMinimumWidth = placement.minimumWidth;
    refresh.previousMaximumWidth = placement.maximumWidth;
    refresh.previousMinimumHeight = placement.minimumHeight;
    refresh.previousMaximumHeight = placement.maximumHeight;
    refresh.minimumWidth = 4;
    refresh.maximumWidth = 8;
    refresh.minimumHeight = 6;
    refresh.maximumHeight = 14;
    refresh.boundsRadius = 2;
    refresh.bendFactor = 0.8F;
    auto result = rescaleTerrainTreePoints(points, points, refresh);
    REQUIRE(result.ok());
    CHECK(result.value() == points.getCount());
    CHECK(points.getPointId(0) == id);
    CHECK(points.getX(0) == x);
    CHECK(points.getYaw(0) == yaw);
    CHECK(points.getColorR(0) == color);
    CHECK(points.getScaleX(0) == 8);
    CHECK(points.getScaleY(0) == 14);
    CHECK(points.getBoundsMaxX(0) == 16);
    CHECK(points.getFloatAttribute(0, "bendFactor", 0) == 0.8F);
    refresh.previousMaximumWidth = 1;
    CHECK(!rescaleTerrainTreePoints(points, points, refresh).ok());
    CHECK(points.getPointId(0) == id);
    CHECK(points.getScaleX(0) == 8);
}

TEST_CASE("procgen.tree.scriptRemoveAndPrototypeRefresh") {
    Heightmap fitness(1, 1), heights(1, 1);
    fitness.data()[0] = 1;
    PointSet input, output;
    auto settings = baseSettings();
    settings.scaleMode = TerrainTreeScaleMode::Fixed;
    REQUIRE(exportTerrainTreePoints(input, fitness, heights, settings).ok());
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    table.addClass("PointSet", ssq::Class::Ctor<PointSet()>());
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local refresh=eve.TerrainTreeRescaleSettings();refresh.asset="tree/oak";
        refresh.setScaleMode(0);refresh.minimumWidth=5.0;refresh.maximumWidth=5.0;
        refresh.minimumHeight=6.0;refresh.maximumHeight=6.0;
        assert(eve.rescaleTerrainTreePoints(output(),input(),refresh).value==9);
        local remove=eve.TerrainTreePlacementSettings();remove.asset="tree/oak";
        remove.width=20.0;remove.depth=20.0;remove.minimumFitness=0.5;
        assert(eve.removeTerrainTreePoints(output(),output(),fitness(),remove).value==9);
    )"));
    CHECK(output.getCount() == 0);
}
