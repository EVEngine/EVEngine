#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.legacyHydraulic.sourceOneStepAndHardness") {
    Heightmap heights(3, 1), sediment(3, 1), hardness(3, 1), rain(3, 1);
    heights.data() = {0, 1, 0};
    rain.data()[1] = 1;
    TerrainLegacyHydraulicSettings settings;
    settings.sedimentDissolveRate = 1;
    REQUIRE(applyTerrainLegacyHydraulic(heights, sediment, hardness, rain, settings).ok());
    CHECK(heights.data() == std::vector<float>({0.2F, 0.6F, 0.2F}));
    CHECK(sediment.data() == std::vector<float>({0.2F, -0.4F, 0.2F}));

    Heightmap protectedHeight(3, 1), protectedSediment(3, 1);
    protectedHeight.data() = {0, 1, 0};
    hardness.data()[1] = 1;
    REQUIRE(applyTerrainLegacyHydraulic(protectedHeight, protectedSediment, hardness, rain, settings).ok());
    CHECK(protectedHeight.data() == std::vector<float>({0, 1, 0}));
    CHECK(protectedSediment.data() == std::vector<float>({0, 0, 0}));
}

TEST_CASE("procgen.legacyHydraulic.atomicValidationAndScript") {
    Heightmap heights(2, 2), sediment(2, 2), hardness(2, 2), rain(2, 2);
    heights.data() = {0, 1, 1, 0};
    const auto original = heights.data();
    TerrainLegacyHydraulicSettings invalid;
    invalid.rainFrequency = 0;
    CHECK(!applyTerrainLegacyHydraulic(heights, sediment, hardness, rain, invalid).ok());
    CHECK(heights.data() == original);
    CHECK(!applyTerrainLegacyHydraulic(heights, heights, hardness, rain, {}).ok());

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
