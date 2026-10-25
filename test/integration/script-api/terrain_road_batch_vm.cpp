// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "modules": [
    "procgen"
  ],
  "default": "script-binding",
  "origins": [
    "terrain_road_batch_vm.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "procgen/Procgen.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
eve::Result<std::string> batchSnapshot(float z) {
    RoadNetwork n;
    auto        a = n.addNode(-24, 106, z);
    if (!a.ok()) return eve::Result<std::string>::failure(a.status());
    auto b = n.addNode(24, 106, z);
    if (!b.ok()) return eve::Result<std::string>::failure(b.status());
    auto e = n.addEdge(a.value(), b.value(), {{-24, 106, z}, {24, 106, z}});
    if (!e.ok()) return eve::Result<std::string>::failure(e.status());
    auto snapshot = captureRoadNetwork(n);
    if (!snapshot.ok()) return eve::Result<std::string>::failure(snapshot.status());
    return snapshot.value().toJson();
}
}  // namespace
TEST_CASE("procgen.road.terrain.squirrelBatchProductionPoliciesAndAtomicFailure") {
    auto first = batchSnapshot(-12);
    REQUIRE(first.ok());
    auto second = batchSnapshot(12);
    REQUIRE(second.ok());
    TerrainWorldWorkspace world;
    TerrainSplatmap       splat;
    ssq::VM               vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("splat", [&]() { return &splat; });
    vm.addFunc("firstSnapshot", [&]() { return first.value(); });
    vm.addFunc("secondSnapshot", [&]() { return second.value(); });
    vm.addFunc("releaseSecond", [&](std::int64_t packed, std::int64_t epoch) {
        ProcgenMeshBuildHandleRef ref{
            eve::RuntimeHandle<ProcgenMeshBuildHandleTag>::fromPacked(static_cast<std::uint64_t>(packed)),
            static_cast<std::uint64_t>(epoch)};
        return eve::script::projectResult(vm.getHandle(), Procgen::create()->releaseMeshBuild(ref));
    });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWorldCreationSettings();s.originY=100.0;s.tileHeight=10.0;s.tileSize=64.0;
        s.heightmapResolution=65;s.controlTextureResolution=64;s.detailResolution=64;
        s.treeResolution=4;s.objectResolution=4;s.splatLayers=3;
        assert(world().create(s).ok && world().setHeightWorldUnits(105.0,10.0).ok);
        local proc=eve.Procgen();local p=proc.newParams();assert(p.ok);
        assert(p.value.setString("networkSnapshot",firstSnapshot()).ok);
        local a=proc.buildMesh("mesh.roadNetwork",p.value);assert(a.ok);
        assert(p.value.setString("networkSnapshot",secondSnapshot()).ok);
        local b=proc.buildMesh("mesh.roadNetwork",p.value);assert(b.ok);
        local one=eve.RoadTerrainWriteOptions(),two=eve.RoadTerrainWriteOptions();
        one.splatLayer=1;one.splatOpacity=0.4;one.blendDistance=0.0;one.clearTrees=false;one.clearDetails=false;
        two.splatLayer=2;two.splatOpacity=0.8;two.blendDistance=0.0;two.clearTrees=false;two.clearDetails=false;
        local before=world().getOperationCount();
        local written=world().conformRoadBatch([a.value,b.value],[one,two],2147483649);
        if(!written.ok)throw written.status.summary;
        assert(written.ok && written.checked && written.value>0);
        assert(world().getOperationCount()==before+1);
        assert(world().copySplatmap(0,splat()).ok);
        assert(fabs(splat().sample(1,32,20).value-0.4)<0.0001);
        assert(fabs(splat().sample(2,32,44).value-0.8)<0.0001);
        assert(world().undo().ok && world().copySplatmap(0,splat()).ok);
        assert(splat().sample(1,32,20).value==0.0 && splat().sample(2,32,44).value==0.0);
        local count=world().getOperationCount(),applied=world().getAppliedCount();
        assert(!world().conformRoadBatch(null,[one],100).ok);
        assert(!world().conformRoadBatch([a.value],{},100).ok);
        assert(!world().conformRoadBatch([],[],100).ok);
        assert(!world().conformRoadBatch([a.value],[],100).ok);
        assert(!world().conformRoadBatch(array(4097,a.value),array(4097,one),100).ok);
        assert(!world().conformRoadBatch([a.value],[one],0).ok);
        assert(!world().conformRoadBatch([a.value],[one],-1).ok);
        foreach(badBudget in [null,true,1.5,"100",{}]) {
            local bad=world().conformRoadBatch([a.value],[one],badBudget);
            assert(!bad.ok && bad.diagnostics[0].code=="type_mismatch" && bad.diagnostics[0].path=="maximumWork");
        }
        local wrong=world().conformRoadBatch([a.value,42],[one,two],33554432);
        assert(!wrong.ok && wrong.diagnostics[0].path=="meshes[1]");
        assert(!world().conformRoadBatch([a.value,b.value],[one,one.splatOpacity],33554432).ok);
        assert(two.setMaximumWork(1).ok);
        assert(!world().conformRoadBatch([a.value,b.value],[one,two],33554432).ok);
        assert(two.setMaximumWork(16777216).ok);
        assert(!world().conformRoadBatch([a.value,b.value],[one,two],1).ok);
        assert(world().getOperationCount()==count && world().getAppliedCount()==applied);
        assert(releaseSecond(b.handle,b.ownerEpoch).ok);
        local stale=world().conformRoadBatch([a.value,b.value],[one,two],33554432);
        assert(!stale.ok && stale.diagnostics[0].code=="stale_handle" && stale.diagnostics[0].path=="meshes[1]");
        assert(world().getOperationCount()==count && world().getAppliedCount()==applied);
        one.splatOpacity=0.0;two.splatOpacity=0.0;
        assert(world().redo().ok && world().copySplatmap(0,splat()).ok);
        assert(fabs(splat().sample(1,32,20).value-0.4)<0.0001);
        assert(fabs(splat().sample(2,32,44).value-0.8)<0.0001);
    )"));
    REQUIRE_EQ(world.getOperationCount(), 2);
    REQUIRE_EQ(world.getAppliedCount(), 2);
}
