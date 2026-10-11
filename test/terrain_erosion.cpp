#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve::procgen;
namespace {
Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
}  // namespace

TEST_CASE("procgen.erosion.thermalEightNeighborShaderGolden") {
    auto height = filled(3, 3, 1), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    const auto             before = height.data();
    TerrainThermalSettings s;
    s.iterations  = 1;
    s.dt          = 0.1F;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(std::abs(height.height(1, 1) - 1.957325F) < 1e-6F);
    CHECK(std::abs(height.height(1, 0) - 1.00625F) < 1e-6F);
    CHECK(std::abs(height.height(0, 0) - 1.00441875F) < 1e-6F);
    for (size_t i = 0; i < before.size(); ++i)
        CHECK(std::abs(height.data()[i] + sediment.data()[i] - before[i]) < 1e-6F);
}

TEST_CASE("procgen.erosion.thermalWorldSlopeAndStrictThreshold") {
    auto height = filled(3, 3, 1), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    const auto             original = height;
    TerrainThermalSettings s;
    s.iterations  = 1;
    s.dt          = 1;
    s.reposeSlope = 1.5F;
    s.spacingX    = 1;
    s.spacingZ    = 100;
    s.heightScale = 2;
    auto r        = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(height.height(1, 1) == 1.75F);
    height        = original;
    sediment      = filled(3, 3, 0);
    s.reposeSlope = 2;
    r             = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(height.data() == original.data());
}

TEST_CASE("procgen.erosion.thermalMovementCapIncludesZeroFloor") {
    auto height = filled(3, 3, 0), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    TerrainThermalSettings s;
    s.iterations  = 1;
    s.dt          = 100;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(height, sediment, s);
    REQUIRE(r.ok());
    CHECK(height.height(1, 1) == 1);
    CHECK(sediment.height(1, 1) == 1);
    CHECK(height.height(0, 1) == 0);
    CHECK(sediment.height(0, 1) == 0);
    auto singleton = filled(1, 1, 2), singleSediment = filled(1, 1, -1);
    r = applyTerrainThermal(singleton, singleSediment, s);
    REQUIRE(r.ok());
    CHECK(singleton.height(0, 0) == 2);
    CHECK(singleSediment.height(0, 0) == -1);
}

TEST_CASE("procgen.erosion.thermalIterationsResumeBothOutputs") {
    auto batch = filled(4, 3, 0.2F), batchSediment = filled(4, 3, 0.1F);
    batch.setHeight(2, 1, 1);
    auto                   repeated = batch, repeatedSediment = batchSediment;
    TerrainThermalSettings s;
    s.iterations  = 3;
    s.dt          = 0.1F;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(batch, batchSediment, s);
    REQUIRE(r.ok());
    s.iterations = 1;
    for (int i = 0; i < 3; ++i) {
        r = applyTerrainThermal(repeated, repeatedSediment, s);
        REQUIRE(r.ok());
    }
    CHECK(batch.data() == repeated.data());
    CHECK(batchSediment.data() == repeatedSediment.data());
    const auto before = batch.data(), beforeSediment = batchSediment.data();
    s.dt = 0;
    r    = applyTerrainThermal(batch, batchSediment, s);
    REQUIRE(r.ok());
    CHECK(batch.data() == before && batchSediment.data() == beforeSediment);
}

TEST_CASE("procgen.erosion.thermalRejectsAliasesShapesAndOverflowAtomically") {
    const float largest = std::numeric_limits<float>::max();
    auto        height = filled(3, 1, 1), sediment = filled(3, 1, largest);
    height.setHeight(2, 0, largest);
    const auto             before = height.data(), beforeSediment = sediment.data();
    TerrainThermalSettings s;
    s.iterations  = 2;
    s.dt          = 100;
    s.reposeSlope = 0;
    auto r        = applyTerrainThermal(height, sediment, s);
    CHECK(!r.ok());
    CHECK(height.data() == before && sediment.data() == beforeSediment);
    r = applyTerrainThermal(height, height, s);
    CHECK(!r.ok());
    CHECK(height.data() == before);
    auto bad = filled(1, 1, 0);
    r        = applyTerrainThermal(height, bad, s);
    CHECK(!r.ok());
    CHECK(height.data() == before && bad.height(0, 0) == 0);
    s.dt = -1;
    r    = applyTerrainThermal(height, sediment, s);
    CHECK(!r.ok());
    CHECK(height.data() == before && sediment.data() == beforeSediment);
}

TEST_CASE("procgen.erosion.thermalScriptOutputsAndRejectedAlias") {
    auto height = filled(3, 3, 1), sediment = filled(3, 3, 0);
    height.setHeight(1, 1, 2);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("height", [&]() { return &height; });
    vm.addFunc("sediment", [&]() { return &sediment; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainThermalSettings();
        s.spacingX=1.0; s.spacingZ=1.0; s.heightScale=1.0;
        s.reposeSlope=0.0; s.dt=0.1; s.iterations=1;
        local r=height().applyThermal(sediment(),s);
        assert(r.ok && r.value == 9);
        assert(height().height(1,1)<2.0 && sediment().height(1,1)>0.0);
        assert(!height().applyThermal(height(),s).ok);
        s.spacingX=0.0;
        assert(!height().applyThermal(sediment(),s).ok);
    )"));
    CHECK(std::abs(height.height(1, 1) - 1.957325F) < 1e-6F);
}
