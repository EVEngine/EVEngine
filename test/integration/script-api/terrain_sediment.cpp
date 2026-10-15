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
