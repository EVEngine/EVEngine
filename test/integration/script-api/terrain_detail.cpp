#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainDetailPlacement.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
namespace {
[[maybe_unused]] int countAt(const TerrainDetailLayer& layer, int x, int z = 0) {
    auto count = layer.sample(x, z);
    REQUIRE(count.ok());
    return count.value();
}
}  // namespace


TEST_CASE("procgen.detail.scriptOwnedLayer") {
    Heightmap fitness(1, 1);
    fitness.data() = {1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.run(vm.compileSource(R"(
        local layer=eve.TerrainDetailLayer();assert(layer.reset(1,1,0).ok);
        local settings=eve.TerrainDetailSettings();settings.density=9.0;
        assert(layer.apply(fitness(),settings,0,17).ok);
        assert(layer.sample(0,0).value==9);
        assert(layer.getWidth()==1 && layer.getHeight()==1);
        assert(layer.apply(fitness(),settings,2,17).ok);
        assert(layer.sample(0,0).value==0);
        assert(!layer.apply(fitness(),settings,99,17).ok);
    )"));
}

TEST_CASE("procgen.detail.resourceLayersClearIndependently") {
    TerrainDetailLayer layer;
    Heightmap fitness(2, 1);
    fitness.data() = {1, 1};
    REQUIRE(layer.reset(2, 1).ok());
    TerrainDetailSettings first;
    first.minimumFitness = first.fadeStart = 0;
    first.density = 3;
    first.namespaceId = 101;
    REQUIRE(layer.apply(fitness, first, 7).ok());
    auto second = first;
    second.density = 5;
    second.namespaceId = 202;
    REQUIRE(layer.apply(fitness, second, 9).ok());
    CHECK(layer.sample(0, 0).value() == 8);
    CHECK(layer.sampleResource(101, 0, 0).value() == 3);
    CHECK(layer.sampleResource(202, 0, 0).value() == 5);
    Heightmap heights(2, 2);
    TerrainDetailPlacementSettings placement;
    placement.asset = "flowers";
    placement.maxPoints = 20;
    placement.densityNamespaceId = 202;
    placement.namespaceId = 303;
    PointSet selected;
    REQUIRE(exportTerrainDetailPoints(selected, layer, heights, placement).ok());
    CHECK(selected.getCount() == 10);
    for (int i = 0; i < selected.getCount(); ++i)
        CHECK(selected.getStringAttribute(i, "spawnNamespace", "") == "303");
    REQUIRE(layer.clearResource(101).ok());
    CHECK(layer.sample(0, 0).value() == 5);
    CHECK(layer.sampleResource(101, 0, 0).value() == 0);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("layer", [&]() { return &layer; });
    vm.run(vm.compileSource(R"(
        assert(layer().sampleResource(202,0,0).value==5);
        assert(layer().clearResource(202).value==2);
        assert(layer().sample(0,0).value==0);
    )"));
}


TEST_CASE("procgen.detail.scriptPointExport") {
    TerrainDetailLayer layer;
    REQUIRE(layer.reset(1, 1, 2).ok());
    Heightmap heights(1, 1);
    heights.data() = {3};
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    table.addClass("PointSet", ssq::Class::Ctor<PointSet()>());
    exposeHeightmap(table);
    vm.addFunc("layer", [&]() { return &layer; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local settings=eve.TerrainDetailPlacementSettings();settings.asset="grass/test";
        settings.namespaceId=17;settings.seed=4;
        settings.minimumWidth=0.5;settings.maximumWidth=0.5;settings.minimumHeight=2.0;settings.maximumHeight=2.0;
        settings.healthyR=0.8;settings.dryR=0.2;settings.noiseSpread=0.0;settings.noiseSeed=-4;
        assert(eve.exportTerrainDetailPoints(output(),layer(),heights(),settings).ok);
    )"));
    CHECK(output.getCount() == 2);
    CHECK(output.getY(0) == 3);
    CHECK(output.getScaleX(0) == 0.5F);
    CHECK(output.getScaleY(0) == 2);
    CHECK(std::abs(output.getColorR(0) - 0.5F) < 0.000001F);
}
