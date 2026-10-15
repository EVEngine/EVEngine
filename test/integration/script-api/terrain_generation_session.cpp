#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainEffect.h"
#include "procgen/heightmap/TerrainErosion.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;


TEST_CASE("procgen.session.scriptEffectCommands") {
    Heightmap baseline(1, 1), mask(1, 1), curve(1, 1), out(1, 1);
    baseline.data() = {0.5F};
    mask.data()     = {0.5F};
    curve.data()    = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainGenerationSession();assert(s.reset(baseline()).ok);
        assert(s.contrast(mask(),2.0,1.0).ok);
        assert(s.smooth(mask(),eve.TerrainSmoothSettings()).ok);
        local ridges=eve.TerrainRidgeSettings();ridges.passes=1;
        assert(s.ridges(mask(),ridges).ok);
        assert(s.terrace(mask(),eve.TerrainTerraceSettings()).ok);
        assert(s.power(mask(),3.0).ok);
        assert(s.heightCurve(mask(),curve(),0.0,1.0).ok);
        assert(s.heightMix(mask(),mask(),eve.TerrainHeightMixSettings()).ok);
        assert(s.getOperationCount()==7 && s.getAppliedCount()==7);
        assert(s.copyTerrain(out()).ok);local saved=out().height(0,0);
        assert(s.undo().ok && s.redo().ok && s.replay().ok);
        assert(s.copyTerrain(out()).ok && out().height(0,0)==saved);
    )"));
}


TEST_CASE("procgen.session.scriptLifecycleHistoryAndLocks") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    stamp.data() = {2};
    one.data()   = {1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("stamp", [&]() { return &stamp; });
    vm.addFunc("one", [&]() { return &one; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local session=eve.TerrainGenerationSession();local s=eve.TerrainStampSettings();
        assert(session.reset(baseline()).ok);
        assert(session.stamp(stamp(),s,4,one(),one()).ok);
        assert(session.getOperationCount()==1 && session.getAppliedCount()==1);
        assert(session.operationEnabled(0).value);
        assert(session.undo().ok && session.redo().ok && session.replay().ok);
        assert(session.setOperationEnabled(0,false).ok);
        assert(session.copyTerrain(out()).ok && out().height(0,0)==0.0);
        assert(session.setOperationEnabled(0,true).ok);
        assert(session.setAccess(1).ok && session.getAccess()==1);
        assert(!session.undo().ok);
        assert(session.copyTerrain(out()).ok && out().height(0,0)==2.0);
        assert(session.setAccess(0).ok);
    )"));
    CHECK(out.data()[0] == 2);
}


TEST_CASE("procgen.session.scriptErosionCommands") {
    Heightmap baseline(1, 1), out(1, 1);
    baseline.data() = {1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainGenerationSession(); assert(s.reset(baseline()).ok);
        assert(s.resetSimulation(1.0).ok);
        local thermal=eve.TerrainThermalSettings(); thermal.iterations=0;
        assert(s.thermal(thermal).ok);
        local water=eve.TerrainWaterSettings(); water.precipitation=1.0;water.evaporation=0.0;water.dt=0.5;
        assert(s.hydraulic(water,eve.TerrainSedimentSettings(),thermal,2).ok);
        assert(s.exportWater(out(),0).ok && out().height(0,0)==2.0);
        assert(s.copySediment(out()).ok);
        assert(s.undo().ok);
        assert(s.exportWater(out(),0).ok && out().height(0,0)==1.0);
        assert(s.redo().ok && s.replay().ok);
        assert(s.exportWater(out(),0).ok && out().height(0,0)==2.0);
        assert(!s.exportWater(out(),99).ok);
    )"));
}


TEST_CASE("procgen.session.snapshotRealVmRoundTrip") {
    Heightmap baseline(1, 1), out(1, 1);
    baseline.data()[0] = 0.25F;
    ssq::VM vm(1024); auto table = vm.addTable("eve"); exposeHeightmap(table);
    vm.addFunc("baseline", [&]() { return &baseline; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local source=eve.TerrainGenerationSession();assert(source.reset(baseline()).ok);
        assert(source.resetSimulation(0.5).ok);local json=source.snapshotJson();assert(json.ok);
        local restored=eve.TerrainGenerationSession();assert(restored.restoreJson(json.value).ok);
        assert(restored.getOperationCount()==1 && restored.copyTerrain(out()).ok);
        assert(restored.snapshotJson().value==json.value);
    )"));
    CHECK(out.data()[0] == 0.25F);
}
