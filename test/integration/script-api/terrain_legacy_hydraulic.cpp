#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

// terrain_legacy_hydraulic.cpp
using namespace eve::procgen;


TEST_CASE("procgen.legacyHydraulic.atomicValidationAndScript") {
    Heightmap heights(2, 2), sediment(2, 2), hardness(2, 2), rain(2, 2);
    heights.data()                          = {0, 1, 1, 0};
    const auto                     original = heights.data();
    TerrainLegacyHydraulicSettings invalid;
    invalid.rainFrequency = 0;
    CHECK(!applyTerrainLegacyHydraulic(heights, sediment, hardness, rain, invalid).ok());
    CHECK(heights.data() == original);
    CHECK(!applyTerrainLegacyHydraulic(heights, heights, hardness, rain, {}).ok());

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("sediment", [&]() { return &sediment; });
    vm.addFunc("hardness", [&]() { return &hardness; });
    vm.addFunc("rain", [&]() { return &rain; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainLegacyHydraulicSettings(); s.iterations=0;
        assert(heights().applyLegacyHydraulic(sediment(),hardness(),rain(),s).ok);
    )"));
}

// terrain_legacy_thermal.cpp
using namespace eve::procgen;


TEST_CASE("procgen.legacyThermal.steepestOrderHardnessAndScript") {
    Heightmap map(3, 3), hardness(1, 1);
    map.setHeight(1, 1, 1);
    TerrainLegacySteepestErosionSettings settings;
    REQUIRE(applyTerrainLegacySteepestErosion(map, hardness, settings).ok());
    CHECK(map.height(1, 1) == 0.5F);
    CHECK(map.height(1, 2) == 0.5F);

    Heightmap protectedMap(3, 3);
    protectedMap.setHeight(1, 1, 1);
    hardness.setHeight(0, 0, 1);
    REQUIRE(applyTerrainLegacySteepestErosion(protectedMap, hardness, settings).ok());
    CHECK(protectedMap.height(1, 1) == 1);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("map", [&]() { return &map; });
    vm.addFunc("hardness", [&]() { return &hardness; });
    vm.run(vm.compileSource(R"(
        local a=eve.TerrainLegacyDistributedErosionSettings(); a.iterations=0;
        assert(map().applyLegacyDistributedErosion(a).ok);
        local b=eve.TerrainLegacySteepestErosionSettings(); b.iterations=0;
        assert(map().applyLegacySteepestErosion(hardness(),b).ok);
    )"));
}
