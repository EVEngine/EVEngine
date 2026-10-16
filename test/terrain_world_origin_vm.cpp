#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
TEST_CASE("procgen.road.worldOrigin.squirrelCreationAndWorldHeightUseBaseline") {
    TerrainWorldWorkspace world;
    Heightmap             output;
    ssq::VM               vm(1024);
    auto                  table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWorldCreationSettings();s.tileSize=4.0;s.tileHeight=10.0;s.originY=100.0;
        s.heightmapResolution=5;s.controlTextureResolution=4;s.detailResolution=4;
        s.treeResolution=4;s.objectResolution=4;
        assert(world().create(s).ok);
        assert(world().getTileOriginY(0).value==100.0);
        assert(!world().getTileOriginY(1).ok);
        assert(world().setHeightWorldUnits(105.0,10.0).ok);
        assert(world().copyHeightmap(0,output()).ok && output().height(2,2)==0.5);
        assert(world().undo().ok && world().getTileOriginY(0).value==100.0);
        assert(world().copyHeightmap(0,output()).ok && output().height(2,2)==0.0);
    )"));
    auto origin = world.getTileOriginY(0);
    REQUIRE(origin.ok());
    REQUIRE_EQ(origin.value(), 100.);
}
