#include "common/Module.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("procgen.stages.scriptOwnsFieldsAndComposesCaveWithoutGraphics") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        procgen <- eve.Procgen();
        function need(result) { if (!result.ok) throw result.status.summary; return result.value; }
        local p = need(procgen.newParams()); need(p.setInt("resolution",12));
        local generation = eve.CaveGeneration(); need(generation.configure(p));
        need(generation.buildSkeleton()); need(generation.sampleField());
        local field = need(generation.field());
        need(p.release());
        staleRejected <- !generation.configure(p).ok;
        need(field.setSample(0,0,0,need(field.sample(0,0,0))));
        need(generation.replaceField(field));
        need(generation.evolveSurface("curvature"));
        unknownRejected <- !generation.evolveSurface("unknown").ok;
        local mesh = need(generation.buildMesh());
        vertexCount <- mesh.getVertexCount();
    )"));
    REQUIRE(vm.get<bool>("unknownRejected"));
    REQUIRE(vm.get<bool>("staleRejected"));
    REQUIRE(vm.get<int>("vertexCount") > 0);
}
