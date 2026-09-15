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

TEST_CASE("procgen.session.mixedHistoryFailureKeepsEnabledState") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    baseline.data() = {-1};
    stamp.data()    = {4};
    one.data()      = {1};
    TerrainGenerationSession session;
    TerrainStampSettings     settings;
    settings.operation = TerrainStampOperation::Set;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, settings, one, one).ok());
    REQUIRE(session.power(one, 3.5F).ok());
    CHECK(!session.setOperationEnabled(0, false).ok());
    auto enabled = session.operationEnabled(0);
    REQUIRE(enabled.ok());
    CHECK(enabled.value());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 2);
}

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

TEST_CASE("procgen.session.effectHistoryMatchesDirectComposition") {
    Heightmap baseline(3, 3), mask(3, 3), curve(3, 1), out(3, 3);
    baseline.data() = {0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F, 0.7F, 0.6F};
    std::fill(mask.data().begin(), mask.data().end(), 0.5F);
    curve.data()                      = {0, 0.5F, 1};
    Heightmap                expected = baseline;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(applyTerrainContrast(expected, mask, 2, 1).ok());
    REQUIRE(session.contrast(mask, 2, 1).ok());
    TerrainSmoothSettings smooth;
    smooth.radius = 0.4F;
    REQUIRE(applyTerrainSmooth(expected, mask, smooth).ok());
    REQUIRE(session.smooth(mask, smooth).ok());
    TerrainRidgeSettings ridge;
    ridge.passes   = 1;
    ridge.exponent = 1.1F;
    ridge.maximum  = 2;
    REQUIRE(applyTerrainRidges(expected, mask, ridge).ok());
    REQUIRE(session.ridges(mask, ridge).ok());
    TerrainTerraceSettings terrace;
    terrace.count = 3;
    REQUIRE(applyTerrainTerrace(expected, mask, terrace).ok());
    REQUIRE(session.terrace(mask, terrace).ok());
    REQUIRE(applyTerrainPower(expected, mask, 3.5F).ok());
    REQUIRE(session.power(mask, 3.5F).ok());
    REQUIRE(applyTerrainHeightCurve(expected, mask, curve, 0, 2).ok());
    REQUIRE(session.heightCurve(mask, curve, 0, 2).ok());
    TerrainHeightMixSettings mix;
    mix.clipMaximum = 2;
    REQUIRE(applyTerrainHeightMix(expected, mask, mask, mix).ok());
    REQUIRE(session.heightMix(mask, mask, mix).ok());
    CHECK(session.getOperationCount() == 7);
    std::fill(mask.data().begin(), mask.data().end(), 0.0F);
    std::fill(curve.data().begin(), curve.data().end(), 0.0F);
    REQUIRE(session.undo().ok());
    REQUIRE(session.redo().ok());
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == expected.data());
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

TEST_CASE("procgen.session.ownedInputsUndoRedoAndBranch") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    stamp.data() = {2};
    one.data()   = {1};
    TerrainStampSettings s;
    s.operation = TerrainStampOperation::Add;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    stamp.data()[0] = 3;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    stamp.data()[0]    = 99;
    one.data()[0]      = 0;
    baseline.data()[0] = 100;
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 5);
    REQUIRE(session.undo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 2);
    s.width = 0;
    CHECK(!session.stamp(stamp, s, one, one).ok());
    CHECK(session.getOperationCount() == 2);
    CHECK(session.getAppliedCount() == 1);
    REQUIRE(session.redo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 5);
    REQUIRE(session.undo().ok());
    s.width         = 1;
    one.data()[0]   = 1;
    stamp.data()[0] = 4;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    CHECK(!session.redo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 6);
}

TEST_CASE("procgen.session.enabledReplayFailurePreservesHistoryAndTerrain") {
    Heightmap baseline(1, 1), stamp(1, 1), one(1, 1), out(1, 1);
    baseline.data()[0] = std::numeric_limits<float>::max() * 0.8F;
    one.data()[0]      = 1;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    TerrainStampSettings s;
    s.operation = TerrainStampOperation::Set;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    s.operation     = TerrainStampOperation::Add;
    stamp.data()[0] = std::numeric_limits<float>::max() * 0.4F;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    CHECK(!session.setOperationEnabled(0, false).ok());
    auto enabled = session.operationEnabled(0);
    REQUIRE(enabled.ok());
    CHECK(enabled.value());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == stamp.data()[0]);
    REQUIRE(session.setOperationEnabled(1, false).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 0);
    REQUIRE(session.setOperationEnabled(0, false).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == baseline.data()[0]);
}

TEST_CASE("procgen.session.lockMoveAndSnapshotIsolation") {
    TerrainGenerationSession session;
    Heightmap                baseline(1, 1), out(1, 1);
    baseline.data()[0] = 7;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.setAccess(TerrainSessionAccess::Locked).ok());
    CHECK(!session.reset(baseline).ok());
    CHECK(!session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    out.data()[0] = 99;
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 7);
    TerrainGenerationSession moved = std::move(session);
    CHECK(moved.getAccess() == TerrainSessionAccess::Locked);
    CHECK(!session.copyTerrain(out).ok());
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(moved.setAccess(TerrainSessionAccess::Editable).ok());
    REQUIRE(moved.replay().ok());
    CHECK(!moved.undo().ok());
    CHECK(!moved.redo().ok());
}


TEST_CASE("procgen.session.erosionHistoryRestoresAllChannels") {
    Heightmap baseline(3, 3), out(3, 3), expectedSediment(3, 3);
    baseline.data()                   = {1, 2, 1, 2, 5, 2, 1, 2, 1};
    Heightmap                expected = baseline;
    TerrainGenerationSession session;
    TerrainWaterField        water;
    TerrainThermalSettings   thermal;
    thermal.reposeSlope = 0;
    thermal.dt          = 0.1F;
    thermal.iterations  = 2;
    TerrainWaterSettings flow;
    flow.precipitation = 0.2F;
    flow.evaporation   = 0;
    flow.dt            = 0.1F;
    TerrainSedimentSettings reaction;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.resetSimulation(1).ok());
    REQUIRE(water.reset(3, 3, 1).ok());
    REQUIRE(session.thermal(thermal).ok());
    REQUIRE(applyTerrainThermal(expected, expectedSediment, thermal).ok());
    const auto beforeHeight = expected.data(), beforeSediment = expectedSediment.data();
    REQUIRE(session.hydraulic(flow, reaction, thermal, 2).ok());
    REQUIRE(water.advanceHydraulic(expected, expectedSediment, flow, reaction, thermal, 2).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == expected.data());
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == expectedSediment.data());
    for (int channel = 0; channel < 7; ++channel) {
        Heightmap reference(3, 3);
        REQUIRE(water.exportChannel(reference, static_cast<TerrainWaterChannel>(channel)).ok());
        REQUIRE(session.exportWater(out, static_cast<TerrainWaterChannel>(channel)).ok());
        CHECK(out.data() == reference.data());
    }
    REQUIRE(session.undo().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == beforeHeight);
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == beforeSediment);
    for (int channel = 0; channel < 7; ++channel) {
        REQUIRE(session.exportWater(out, static_cast<TerrainWaterChannel>(channel)).ok());
        for (float value : out.data()) CHECK(value == (channel == 0 ? 1.0F : 0.0F));
    }
    // Mutating caller settings cannot alter an already recorded command.
    flow.dt              = -1;
    thermal.dt           = -1;
    reaction.depositRate = -1;
    CHECK(!session.hydraulic(flow, reaction, thermal, 2).ok());
    CHECK(session.getOperationCount() == 3);
    REQUIRE(session.redo().ok());
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == expected.data());
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == expectedSediment.data());
    REQUIRE(session.resetSimulation(0.5F).ok());
    REQUIRE(session.copySediment(out).ok());
    for (float value : out.data()) CHECK(value == 0);
    REQUIRE(session.undo().ok());
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == expectedSediment.data());
    REQUIRE(session.setAccess(TerrainSessionAccess::Locked).ok());
    CHECK(!session.resetSimulation(0).ok());
    CHECK(!session.thermal(thermal).ok());
    REQUIRE(session.exportWater(out, TerrainWaterChannel::Depth).ok());
    const auto saved = out.data();
    CHECK(!session.exportWater(out, static_cast<TerrainWaterChannel>(99)).ok());
    CHECK(out.data() == saved);
}

TEST_CASE("procgen.session.erosionReplayFailurePreservesSimulation") {
    Heightmap baseline(1, 1), stamp(1, 1), mask(1, 1), out(1, 1);
    baseline.data() = {-1};
    stamp.data()    = {1};
    mask.data()     = {1};
    TerrainGenerationSession session;
    TerrainStampSettings     settings;
    settings.operation = TerrainStampOperation::Set;
    TerrainWaterSettings flow;
    flow.precipitation = 1;
    flow.evaporation   = 0;
    TerrainThermalSettings thermal;
    thermal.iterations = 0;
    TerrainSedimentSettings sediment;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    REQUIRE(session.hydraulic(flow, sediment, thermal, 2).ok());
    REQUIRE(session.exportWater(out, TerrainWaterChannel::Depth).ok());
    const auto water = out.data();
    REQUIRE(session.copySediment(out).ok());
    const auto material = out.data();
    REQUIRE(session.copyTerrain(out).ok());
    const auto terrain = out.data();
    CHECK(!session.setOperationEnabled(0, false).ok());
    auto enabled = session.operationEnabled(0);
    REQUIRE(enabled.ok());
    CHECK(enabled.value());
    CHECK(!session.resetSimulation(-1).ok());
    CHECK(session.getOperationCount() == 2);
    REQUIRE(session.exportWater(out, TerrainWaterChannel::Depth).ok());
    CHECK(out.data() == water);
    REQUIRE(session.copySediment(out).ok());
    CHECK(out.data() == material);
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data() == terrain);
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

TEST_CASE("procgen.session.versionedSnapshotRetainsEveryCommandCursorAccessAndIsAtomic") {
    Heightmap baseline(3, 3), mask(3, 3), stamp(1, 1), curve(2, 1), before(3, 3), after(3, 3);
    std::fill(baseline.data().begin(), baseline.data().end(), 0.5F);
    std::fill(mask.data().begin(), mask.data().end(), 1.0F);
    stamp.data()[0] = 0.6F; curve.data() = {0, 1};
    TerrainGenerationSession source;
    TerrainStampSettings stampSettings; stampSettings.operation = TerrainStampOperation::Set;
    REQUIRE(source.reset(baseline).ok());
    REQUIRE(source.stamp(stamp, stampSettings, mask, mask).ok());
    REQUIRE(source.contrast(mask, 0, 1).ok());
    TerrainSmoothSettings smooth; smooth.radius = 0;
    REQUIRE(source.smooth(mask, smooth).ok());
    TerrainRidgeSettings ridge; ridge.passes = 0;
    REQUIRE(source.ridges(mask, ridge).ok());
    TerrainTerraceSettings terrace; terrace.strength = 0;
    REQUIRE(source.terrace(mask, terrace).ok());
    REQUIRE(source.power(mask, 4).ok());
    REQUIRE(source.heightCurve(mask, curve, 0, 1).ok());
    TerrainHeightMixSettings mix; mix.strength = 0; mix.clipMaximum = 2;
    REQUIRE(source.heightMix(mask, mask, mix).ok());
    REQUIRE(source.resetSimulation(1).ok());
    TerrainThermalSettings thermal; thermal.iterations = 0;
    REQUIRE(source.thermal(thermal).ok());
    TerrainWaterSettings water;
    TerrainSedimentSettings sediment;
    REQUIRE(source.hydraulic(water, sediment, thermal, 0).ok());
    REQUIRE(source.setOperationEnabled(2, false).ok());
    REQUIRE(source.undo().ok());
    REQUIRE(source.setAccess(TerrainSessionAccess::Locked).ok());
    REQUIRE(source.copyTerrain(before).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    TerrainGenerationSession restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    CHECK(restored.getOperationCount() == 11);
    CHECK(restored.getAppliedCount() == 10);
    CHECK(restored.getAccess() == TerrainSessionAccess::Locked);
    CHECK(!restored.operationEnabled(2).value());
    REQUIRE(restored.copyTerrain(after).ok());
    CHECK(after.data() == before.data());
    CHECK(restored.snapshotJson().value() == json.value());
    REQUIRE(restored.setAccess(TerrainSessionAccess::Editable).ok());
    const auto stable = restored.snapshotJson().value();
    CHECK(!restored.restoreJson("{\"schema\":\"eve.procgen.terrain-generation-session\",\"version\":1,\"payload\":\"00\",\"extra\":1}").ok());
    CHECK(restored.snapshotJson().value() == stable);
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

TEST_CASE("procgen.session.stagedReplayLifecycleIsAtomicAndCancellable") {
    Heightmap baseline(1, 1), stamp(1, 1), mask(1, 1), out(1, 1);
    baseline.data()[0] = 1;
    stamp.data()[0] = mask.data()[0] = 1;
    TerrainStampSettings settings;
    settings.operation = TerrainStampOperation::Add;
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    REQUIRE(session.stamp(stamp, settings, mask, mask).ok());
    REQUIRE(session.beginReplay().ok());
    CHECK(session.getReplayStatus() == TerrainSessionRunStatus::Pending);
    CHECK(session.getReplayCompletedOperations() == 0);
    auto firstStep = session.stepReplay(1);
    REQUIRE(firstStep.ok());
    CHECK(firstStep.value() == TerrainSessionRunStatus::Pending);
    CHECK(session.getReplayCompletedOperations() == 1);
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 3);
    CHECK(!session.undo().ok());
    CHECK(!session.setAccess(TerrainSessionAccess::Locked).ok());
    auto cancelled = session.cancelReplay();
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value() == TerrainSessionRunStatus::Cancelled);
    CHECK(!session.stepReplay(1).ok());
    REQUIRE(session.copyTerrain(out).ok());
    CHECK(out.data()[0] == 3);

    REQUIRE(session.beginReplay().ok());
    CHECK(!session.stepReplay(0).ok());
    auto completed = session.stepReplay(8);
    REQUIRE(completed.ok());
    CHECK(completed.value() == TerrainSessionRunStatus::Completed);
    CHECK(session.getReplayCompletedOperations() == 2);
    CHECK(!session.cancelReplay().ok());
    REQUIRE(session.undo().ok());
    CHECK(session.getReplayStatus() == TerrainSessionRunStatus::Idle);
}
