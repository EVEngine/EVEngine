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
[[maybe_unused]] TerrainTreePlacementSettings baseSettings() {
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
