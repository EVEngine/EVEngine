#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.sediment.scriptCombinedAndStandalone") {
    Heightmap heights(1, 1), sediment(1, 1), velocity(1, 1);
    heights.data()[0]  = 1;
    sediment.data()[0] = 2;
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("sediment", [&]() { return &sediment; });
    vm.addFunc("velocity", [&]() { return &velocity; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainWaterSettings(); w.dt=0.0;
        local r=eve.TerrainSedimentSettings(); r.depositRate=0.0;
        local t=eve.TerrainThermalSettings(); t.iterations=0;
        assert(heights().applySediment(sediment(),velocity(),velocity(),w,r).ok);
        local f=eve.TerrainWaterField(); assert(f.reset(1,1,1.0).ok);
        assert(f.advanceHydraulic(heights(),sediment(),w,r,t,2).ok);
        assert(!f.advanceHydraulic(heights(),heights(),w,r,t,1).ok);
    )"));
    CHECK(sediment.data()[0] == 16);
    CHECK(heights.data()[0] == 1);
}

TEST_CASE("procgen.sediment.sourceStationaryAccumulation") {
    Heightmap heights(2, 1), sediment(2, 1), vx(2, 1), vz(2, 1);
    heights.data()  = {2, 2};
    sediment.data() = {3, 4};
    TerrainWaterSettings water;
    water.dt = 0;
    TerrainSedimentSettings reaction;
    REQUIRE(applyTerrainSediment(heights, sediment, vx, vz, water, reaction).ok());
    CHECK(heights.data() == std::vector<float>({2, 2}));
    CHECK(sediment.data() == std::vector<float>({6, 8}));
}

TEST_CASE("procgen.sediment.sourceBacktraceWeights") {
    Heightmap heights(3, 3), sediment(3, 3), vx(3, 3), vz(3, 3);
    sediment.data() = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    vx.data()[8]    = 1.5F;
    vz.data()[8]    = 0.5F;
    TerrainWaterSettings water;
    water.dt = 1;
    TerrainSedimentSettings reaction;
    reaction.depositRate = 0;
    REQUIRE(applyTerrainSediment(heights, sediment, vx, vz, water, reaction).ok());
    CHECK(std::abs(sediment.data()[8] - (9.0F + 145.0F / 36.0F)) < 0.00001F);
}

TEST_CASE("procgen.sediment.atomicMultiIterationFailure") {
    Heightmap heights(1, 1), sediment(1, 1);
    heights.data()[0]          = 1;
    sediment.data()[0]         = std::numeric_limits<float>::max() * 0.4F;
    const auto        original = sediment.data();
    TerrainWaterField field;
    REQUIRE(field.reset(1, 1, 1).ok());
    TerrainWaterSettings water;
    water.dt               = 1;
    water.precipitation    = 0.1F;
    water.evaporation      = 0;
    water.flowAcceleration = 0;
    TerrainSedimentSettings reaction;
    reaction.depositRate = 0;
    TerrainThermalSettings thermal;
    thermal.iterations = 0;
    CHECK(!field.advanceHydraulic(heights, sediment, water, reaction, thermal, 2).ok());
    CHECK(heights.data()[0] == 1);
    CHECK(sediment.data() == original);
    auto sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1);
    REQUIRE(field.advanceHydraulic(heights, sediment, water, reaction, thermal, 1).ok());
    CHECK(sediment.data()[0] == original[0] * 2);
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1.1F);
}

TEST_CASE("procgen.sediment.velocityAliasesUseSnapshot") {
    Heightmap heights(2, 2), sediment(2, 2);
    heights.data()                          = {1, 2, 3, 4};
    sediment.data()                         = {4, 3, 2, 1};
    Heightmap               expectedHeights = heights, expectedSediment = sediment;
    const Heightmap         vx = heights, vz = sediment;
    TerrainWaterSettings    water;
    TerrainSedimentSettings reaction;
    REQUIRE(applyTerrainSediment(expectedHeights, expectedSediment, vx, vz, water, reaction).ok());
    REQUIRE(applyTerrainSediment(heights, sediment, heights, sediment, water, reaction).ok());
    CHECK(heights.data() == expectedHeights.data());
    CHECK(sediment.data() == expectedSediment.data());
    const auto previous = heights.data();
    CHECK(!applyTerrainSediment(heights, heights, vx, vz, water, reaction).ok());
    CHECK(heights.data() == previous);
}

TEST_CASE("procgen.sediment.combinedMatchesExplicitStages") {
    Heightmap heights(3, 2), sediment(3, 2);
    heights.data()                    = {2, 3, 1, 4, 2, 5};
    sediment.data()                   = {1, 2, 3, 4, 5, 6};
    Heightmap         expectedHeights = heights, expectedSediment = sediment, vx(3, 2), vz(3, 2);
    TerrainWaterField combined, separate;
    REQUIRE(combined.reset(3, 2, 1).ok());
    REQUIRE(separate.reset(3, 2, 1).ok());
    TerrainWaterSettings    water;
    TerrainSedimentSettings reaction;
    TerrainThermalSettings  thermal;
    thermal.reposeSlope = 0.1F;
    thermal.dt          = 0.01F;
    for (int iteration = 0; iteration < 3; ++iteration) {
        REQUIRE(separate.advance(expectedHeights, water).ok());
        for (int z = 0; z < 2; ++z)
            for (int x = 0; x < 3; ++x) {
                auto cell = separate.sample(x, z);
                REQUIRE(cell.ok());
                vx.data()[z * 3 + x] = cell.value().velocityX;
                vz.data()[z * 3 + x] = cell.value().velocityZ;
            }
        REQUIRE(applyTerrainSediment(expectedHeights, expectedSediment, vx, vz, water, reaction).ok());
        REQUIRE(applyTerrainThermal(expectedHeights, expectedSediment, thermal).ok());
    }
    REQUIRE(combined.advanceHydraulic(heights, sediment, water, reaction, thermal, 3).ok());
    CHECK(heights.data() == expectedHeights.data());
    CHECK(sediment.data() == expectedSediment.data());
    for (int z = 0; z < 2; ++z)
        for (int x = 0; x < 3; ++x) {
            auto a = combined.sample(x, z), b = separate.sample(x, z);
            REQUIRE(a.ok());
            REQUIRE(b.ok());
            CHECK(a.value().depth == b.value().depth);
            CHECK(a.value().velocityX == b.value().velocityX);
            CHECK(a.value().velocityZ == b.value().velocityZ);
            CHECK(a.value().fluxRight == b.value().fluxRight);
            CHECK(a.value().fluxLeft == b.value().fluxLeft);
            CHECK(a.value().fluxTop == b.value().fluxTop);
            CHECK(a.value().fluxBottom == b.value().fluxBottom);
        }
}
