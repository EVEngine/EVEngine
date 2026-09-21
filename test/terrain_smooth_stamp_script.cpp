#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.smoothStamp.scriptBakeAndApply") {
    MeshBuild source;
    source.positions() = {0, 10, 0, 4, 10, 0, 0, 10, 4, 4, 10, 4};
    source.indices()   = {0, 1, 2, 1, 3, 2};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    // Production registers settings before the heightmap and mesh classes.
    exposeTerrainStampSettings(table);
    table.addClass("ProcgenMeshBuild", ssq::Class::Ctor<MeshBuild()>());
    auto heightmap = table.addClass("ProcgenHeightmap", ssq::Class::Ctor<Heightmap(int, int)>());
    heightmap.addFunc("height", &Heightmap::height);
    heightmap.addFunc("setHeight", &Heightmap::setHeight);
    // Test bridge observes the actual registered settings object in the native consumer.
    vm.addFunc("apply", [handle = vm.getHandle()](Heightmap* target, const Heightmap* stamp,
                                                  const TerrainStampSettings* settings, const Heightmap* one,
                                                  const Heightmap* coverage) {
        auto value      = *settings;
        value.operation = TerrainStampOperation::SmoothRaise;
        return eve::script::projectResult(handle, applyTerrainStamp(*target, *stamp, value, *one, *coverage),
                                          [](int changed) { return eve::Value(changed); });
    });
    vm.addFunc("source", [&]() { return &source; });
    vm.run(vm.compileSource(R"(
        local builder=eve.TerrainMeshStampBuilder();
        assert(builder.setSource(source()).ok);
        local stamp=eve.ProcgenHeightmap(5,5), coverage=eve.ProcgenHeightmap(5,5);
        local baked=builder.bake(stamp,coverage,0.0,0.0,4.0,4.0,0.0);
        assert(baked.ok && baked.value==25 && stamp.height(2,2)==10.0);
        assert(!builder.bake(stamp,stamp,0.0,0.0,4.0,4.0,0.0).ok);
        local terrain=eve.ProcgenHeightmap(5,5), one=eve.ProcgenHeightmap(1,1);
        one.setHeight(0,0,1.0);
        for(local z=0;z<5;++z) for(local x=0;x<5;++x) terrain.setHeight(x,z,10.0);
        local s=eve.TerrainStampSettings();
        s.setGrid(0.0,0.0,1.0,1.0); s.setCenter(2.0,2.0); s.setSize(4.0,4.0);
        s.smoothWidth=4.0; s.edgeFade=2.0;
        local result=apply(terrain,stamp,s,one,coverage);
        assert(result.ok && result.value==9);
        assert(terrain.height(2,2)==11.0 && terrain.height(0,2)==10.0);
        s.smoothWidth=-1.0;
        assert(!apply(terrain,stamp,s,one,coverage).ok);
        assert(terrain.height(2,2)==11.0);
    )"));
}
