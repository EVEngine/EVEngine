#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.legacyThermal.distributedSourceFormula") {
    Heightmap map(3, 3);
    map.setHeight(1, 1, 1);
    TerrainLegacyDistributedErosionSettings settings;
    settings.minimumThreshold = 0.1F;
    REQUIRE(applyTerrainLegacyDistributedErosion(map, settings).ok());
    CHECK(map.height(1, 1) == 0.5F);
    CHECK(map.height(1, 2) == 0.125F);
    CHECK(map.height(0, 1) == 0.125F);
    CHECK(map.height(2, 1) == 0.125F);
    CHECK(map.height(1, 0) == 0.125F);
}

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
    auto table = vm.addTable("eve");
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

TEST_CASE("procgen.legacyThermal.invalidInputIsAtomic") {
    Heightmap map(3, 3), hardness(1, 1);
    map.setHeight(1, 1, 1);
    const auto original = map.data();
    TerrainLegacyDistributedErosionSettings distributed;
    distributed.minimumThreshold = distributed.maximumThreshold;
    CHECK(!applyTerrainLegacyDistributedErosion(map, distributed).ok());
    CHECK(map.data() == original);
    TerrainLegacySteepestErosionSettings steepest;
    steepest.talusMaximum = 2;
    CHECK(!applyTerrainLegacySteepestErosion(map, hardness, steepest).ok());
    CHECK(map.data() == original);
}
