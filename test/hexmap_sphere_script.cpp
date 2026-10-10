#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
#include "hexmap/HexMapModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("hexmap.sphereScriptDirectionsAreNativeArrays") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto* module = eve::hexmap::HexMapModule::create();
    REQUIRE(module->sphere().reset(1, 100.0f, 7).ok());
    vm.run(vm.compileSource(R"(
        local map=eve.HexMap();
        foreach(v in [map.sphereDirection(0),map.sphereCornerDirection(0,0)]) {
            if(typeof v!="array" || v.len()!=3) throw "direction is not a native xyz array";
            local length=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];
            if(abs(length-1.0)>0.0001) throw "direction is not normalized";
        }
        local missing=map.sphereDirection(-1);
        if(typeof missing!="array" || missing[0]!=0.0 || missing[1]!=1.0 || missing[2]!=0.0)
            throw "invalid-cell north-direction contract changed";
    )"));
}
