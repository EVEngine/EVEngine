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
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
}  // namespace


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
