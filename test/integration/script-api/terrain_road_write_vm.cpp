#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "procgen/Procgen.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.terrain.squirrelProductionMeshWriteAndUndo") {
    RoadNetwork network;
    auto        start = network.addNode(-24.f, 106.f, 0.f);
    REQUIRE(start.ok());
    auto end = network.addNode(24.f, 106.f, 0.f);
    REQUIRE(end.ok());
    auto edge = network.addEdge(start.value(), end.value(), {{-24.f, 106.f, 0.f}, {24.f, 106.f, 0.f}});
    REQUIRE(edge.ok());
    auto baked = bakeRoadNetwork(network);
    REQUIRE(baked.ok());
    REQUIRE(!baked.value().mesh.empty());
    auto snapshot = captureRoadNetwork(network);
    REQUIRE(snapshot.ok());
    auto json = snapshot.value().toJson();
    REQUIRE(json.ok());
    TerrainWorldWorkspace world;
    Heightmap             output(65, 65), holes;
    TerrainSplatmap       splat;
    ssq::VM               vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("roadMesh", [&]() { return &baked.value().mesh; });
    vm.addFunc("roadSnapshot", [&]() { return json.value(); });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("holes", [&]() { return &holes; });
    vm.addFunc("splat", [&]() { return &splat; });
    vm.addFunc("releaseRoadMesh", [&](std::int64_t packed, std::int64_t epoch) {
        ProcgenMeshBuildHandleRef reference{
            eve::RuntimeHandle<ProcgenMeshBuildHandleTag>::fromPacked(static_cast<std::uint64_t>(packed)),
            static_cast<std::uint64_t>(epoch)};
        return eve::script::projectResult(vm.getHandle(), Procgen::create()->releaseMeshBuild(reference));
    });
    vm.addFunc("releaseRoadRaster", [&](std::int64_t packed, std::int64_t epoch) {
        ProcgenHeightmapHandleRef reference{
            eve::RuntimeHandle<ProcgenHeightmapHandleTag>::fromPacked(static_cast<std::uint64_t>(packed)),
            static_cast<std::uint64_t>(epoch)};
        return eve::script::projectResult(vm.getHandle(), Procgen::create()->releaseHeightmap(reference));
    });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWorldCreationSettings();
        s.tileSize=64.0;s.tileHeight=10.0;s.originY=100.0;s.heightmapResolution=65;
        s.controlTextureResolution=64;s.detailResolution=64;s.treeResolution=4;s.objectResolution=4;
        s.splatLayers=2;
        assert(world().create(s).ok);
        assert(world().setHeightWorldUnits(105.0,10.0).ok);
        local o=eve.RoadTerrainWriteOptions();
        assert(o.splatOpacity==1.0 && o.splatLayer == -1);
        assert(o.getMaximumWork()==16777216);
        o.blendDistance=0.0;o.splatLayer=1;o.splatOpacity=0.5;
        o.clearDetails=false;o.clearTrees=false;o.cutTunnelHoles=false;o.tunnelHolesAtPortalsOnly=true;
        local proc=eve.Procgen();
        local params=proc.newParams();assert(params.ok);
        assert(params.value.setString("networkSnapshot",roadSnapshot()).ok);
        local produced=proc.buildMesh("mesh.roadNetwork",params.value);
        assert(produced.ok && produced.ownership=="owned" && produced.value.getVertexCount()>0);
        local written=world().conformRoad(produced.value,o);
        if(!written.ok) throw written.status.summary+":"+written.diagnostics[0].path;
        assert(written.ok && written.hasValue && written.checked && written.value>0);
        assert(world().copyHeightmap(0,output()).ok);
        assert(fabs(output().height(32,32)-0.6)<0.0001);
        assert(world().copySplatmap(0,splat()).ok);
        assert(fabs(splat().sample(1,32,32).value-0.5)<0.0001);
        assert(world().copyHoles(0,holes()).ok && holes().height(32,32)==0.0);
        assert(world().undo().ok);
        assert(world().copySplatmap(0,splat()).ok && splat().sample(1,32,32).value==0.0);
        local count=world().getOperationCount(), applied=world().getAppliedCount();
        o.splatOpacity=-1.0;
        local invalid=world().conformRoad(roadMesh(),o);
        assert(!invalid.ok && invalid.checked && invalid.diagnostics.len()>0);
        assert(!world().conformRoad(null,o).ok && !world().conformRoad(roadMesh(),null).ok);
        assert(!world().conformRoad(o,o).ok && !world().conformRoad(roadMesh(),output()).ok);
        assert(!world().conformRoad(42,o).ok && !world().conformRoad(roadMesh(),{}).ok);
        assert(!world().copyHoles(0,null).ok && !world().copyHoles(1,holes()).ok);
        assert(!world().copyHoles(0,o).ok && !world().copyHoles(0,42).ok);
        assert(world().getOperationCount()==count && world().getAppliedCount()==applied);
        assert(world().redo().ok);
        o.splatOpacity=0.5;
        assert(o.setMaximumWork(1).ok && o.getMaximumWork()==1);
        assert(!o.setMaximumWork(0).ok && !o.setMaximumWork(-1).ok && o.getMaximumWork()==1);
        local before=world().getOperationCount();
        assert(!world().conformRoad(roadMesh(),o).ok);
        assert(world().getOperationCount()==before);
        assert(world().copySplatmap(0,splat()).ok && fabs(splat().sample(1,32,32).value-0.5)<0.0001);
        assert(o.setMaximumWork(16777216).ok);
        assert(world().undo().ok && world().redo().ok);
        local stableCount=world().getOperationCount();
        assert(releaseRoadMesh(produced.handle,produced.ownerEpoch).ok);
        local staleMesh=world().conformRoad(produced.value,o);
        assert(!staleMesh.ok && staleMesh.diagnostics[0].path=="mesh" && staleMesh.diagnostics[0].code=="stale_handle");
        assert(world().getOperationCount()==stableCount);
        local fresh=proc.buildMesh("mesh.roadNetwork",params.value);assert(fresh.ok);
        local freshWrite=world().conformRoad(fresh.value,o);
        if(!freshWrite.ok) throw freshWrite.status.summary+":fresh mesh";
        assert(freshWrite.ok);
        assert(world().undo().ok && world().getAppliedCount()==applied+1);
        stableCount=world().getOperationCount();
        local rasterParams=proc.newParams();assert(rasterParams.ok);
        assert(rasterParams.value.setInt("width",5).ok && rasterParams.value.setInt("height",5).ok);
        local raster=proc.generateHeightmap(rasterParams.value);assert(raster.ok);
        assert(world().copyHoles(0,raster.value).ok);
        assert(releaseRoadRaster(raster.handle,raster.ownerEpoch).ok);
        local staleOutput=world().copyHoles(0,raster.value);
        assert(!staleOutput.ok && staleOutput.diagnostics[0].path=="output" && staleOutput.diagnostics[0].code=="stale_handle");
        assert(world().getOperationCount()==stableCount);
    )"));
    REQUIRE(world.copyHeightmap(0, output).ok());
    REQUIRE(std::fabs(output.height(32, 32) - .6f) < 1e-4f);
    REQUIRE(world.copySplatmap(0, splat).ok());
    auto weight = splat.sample(1, 32, 32);
    REQUIRE(weight.ok());
    REQUIRE(std::fabs(weight.value() - .5f) < 1e-4f);
}
