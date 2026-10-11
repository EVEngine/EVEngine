#include "zeroerr/unittest.h"

#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>

#include "procgen/PointSet.h"
#include "common/Value.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainBiomePreset.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"

using namespace eve::procgen;

namespace {
TerrainWorldCreationSettings worldSettings() {
    TerrainWorldCreationSettings settings;
    settings.tilesX = 2;
    settings.tilesZ = 1;
    settings.tileSize = 2;
    settings.heightmapResolution = 3;
    settings.controlTextureResolution = settings.detailResolution = 2;
    settings.treeResolution = settings.objectResolution = 2;
    settings.splatLayers = 2;
    return settings;
}
TerrainStampSettings bounds() {
    TerrainStampSettings value;
    value.centerX = value.centerZ = 0;
    value.width = 4;
    value.depth = 2;
    return value;
}
Heightmap fitness() {
    Heightmap value(4, 2);
    std::fill(value.data().begin(), value.data().end(), 1.0F);
    return value;
}
TerrainTreePlacementSettings trees() {
    TerrainTreePlacementSettings value;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = 0;
    value.minimumWidth = value.maximumWidth = value.minimumHeight = value.maximumHeight = 1;
    value.asset = "oak";
    value.namespaceId = 1201;
    value.maxPoints = 100;
    return value;
}
TerrainObjectPlacementSettings objects() {
    TerrainObjectPlacementSettings value;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius = 0.2F;
    value.prototype = "rocks";
    value.namespaceId = 1301;
    value.maxPoints = 100;
    TerrainObjectInstanceSettings child;
    child.asset = "rock";
    REQUIRE(value.addInstance(child).ok());
    return value;
}
}  // namespace

TEST_CASE("procgen.spawnPlan.mixedRulesPublishAndUndoAsOneWorldOperation") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    auto map = fitness();
    auto operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density = 3;
    detail.namespaceId = 1101;
    auto tree = trees();
    auto object = objects();
    TerrainProbePlacementSettings probe;
    probe.name = "reflection-grid";
    probe.spacing = 1;
    probe.jitterPercent = probe.minimumFitness = 0;
    probe.namespaceId = 1401;
    probe.maxPoints = 100;
    Heightmap modifier(1, 1), one(1, 1);
    modifier.data()[0] = one.data()[0] = 1;
    auto modifierOperation = operation;
    modifierOperation.operation = TerrainStampOperation::Set;
    TerrainSpawnPlan plan;
    REQUIRE(plan.addModifierStamp("modifier-guid", modifier, modifierOperation, one, one).ok());
    REQUIRE(plan.addSplat("texture-guid", map, 1, operation).ok());
    REQUIRE(plan.addDetail("detail-guid", map, detail, operation, TerrainDetailMode::Replace, 11).ok());
    REQUIRE(plan.addTrees("tree-guid", map, tree, operation, TerrainTreeOperationMode::Add).ok());
    REQUIRE(plan.addObjects("object-guid", map, object, operation, TerrainObjectOperationMode::Add).ok());
    REQUIRE(plan.addProbes("probe-guid", map, probe, operation, TerrainProbeOperationMode::Add).ok());
    CHECK(!plan.addTrees("tree-guid", map, tree, operation, TerrainTreeOperationMode::Add).ok());
    auto encoded = plan.snapshotJson();
    REQUIRE(encoded.ok());
    TerrainSpawnPlan roundTrip;
    REQUIRE(roundTrip.restoreJson(encoded.value()).ok());
    CHECK(roundTrip.snapshotJson().value() == encoded.value());
    std::fill(map.data().begin(), map.data().end(), 0.0F);
    tree.namespaceId = 0;
    REQUIRE(world.spawn(roundTrip).ok());
    CHECK(world.getOperationCount() == 1);
    TerrainSplatmap splatOut;
    TerrainDetailLayer detailOut;
    PointSet treeOut, objectOut, probeOut;
    Heightmap heightsOut;
    REQUIRE(world.copyHeightmap(0, heightsOut).ok());
    CHECK(std::any_of(heightsOut.data().begin(), heightsOut.data().end(), [](float value) { return value > 0; }));
    REQUIRE(world.copySplatmap(0, splatOut).ok());
    CHECK(splatOut.sample(1, 0, 0).value() == 1.0F);
    REQUIRE(world.copyDetail(0, detailOut).ok());
    CHECK(detailOut.sampleResource(1101, 0, 0).value() == 3);
    REQUIRE(world.copyTrees(0, treeOut).ok());
    REQUIRE(world.copyObjects(0, objectOut).ok());
    REQUIRE(world.copyProbes(0, probeOut).ok());
    CHECK(!treeOut.empty());
    CHECK(!objectOut.empty());
    CHECK(!probeOut.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splatOut).ok());
    CHECK(splatOut.sample(0, 0, 0).value() == 1.0F);
    CHECK(splatOut.sample(1, 0, 0).value() == 0.0F);
    REQUIRE(world.copyDetail(0, detailOut).ok());
    REQUIRE(world.copyTrees(0, treeOut).ok());
    REQUIRE(world.copyObjects(0, objectOut).ok());
    REQUIRE(world.copyProbes(0, probeOut).ok());
    CHECK(detailOut.sample(0, 0).value() == 0);
    CHECK(treeOut.empty());
    CHECK(objectOut.empty());
    CHECK(probeOut.empty());
}

TEST_CASE("procgen.spawnPlan.failureAndDisabledRulesAreAtomic") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    auto map = fitness();
    auto operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density = 4;
    detail.namespaceId = 2101;
    auto invalidObject = objects();
    invalidObject.namespaceId = 0;
    TerrainSpawnPlan failing;
    REQUIRE(failing.addDetail("detail", map, detail, operation, TerrainDetailMode::Add, 2).ok());
    REQUIRE(failing.addObjects("bad-object", map, invalidObject, operation, TerrainObjectOperationMode::Add).ok());
    CHECK(!world.spawn(failing).ok());
    CHECK(world.getOperationCount() == 0);
    TerrainDetailLayer output;
    REQUIRE(world.copyDetail(0, output).ok());
    CHECK(output.sample(0, 0).value() == 0);

    TerrainSpawnPlan disabled;
    REQUIRE(disabled.addDetail("off", map, detail, operation, TerrainDetailMode::Add, 2).ok());
    REQUIRE(disabled.setEnabled("off", false).ok());
    CHECK(!world.spawn(disabled).ok());
    CHECK(!disabled.setEnabled("missing", true).ok());
}

TEST_CASE("procgen.spawnPlan.executesOwnedPlanThroughRealVm") {
    TerrainWorldWorkspace world;
    Heightmap map = fitness();
    auto settings = worldSettings();
    REQUIRE(world.create(settings).ok());
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("fitness", [&]() { return &map; });
    vm.run(vm.compileSource(R"(
        local bounds=eve.TerrainStampSettings();bounds.setCenter(0.0,0.0);bounds.setSize(4.0,2.0);
        local detail=eve.TerrainDetailSettings();detail.minimumFitness=0.0;detail.fadeStart=0.0;
        detail.density=2.0;detail.namespaceId=3101;
        local plan=eve.TerrainSpawnPlan();
        assert(plan.addModifierStamp("vm-modifier",fitness(),bounds,3,fitness(),fitness()).value==1);
        assert(plan.addSplat("vm-texture",fitness(),1,bounds).value==2);
        assert(plan.addDetail("vm-detail",fitness(),detail,bounds,0,17).value==3);
        local preset=eve.TerrainBiomePreset();
        assert(preset.addSpawner("forest",plan,true,false,true).value==1);
        assert(preset.getSpawnerCount()==1 && preset.getAutoAssignResources("forest").value);
        local presetJson=preset.snapshotJson().value;
        local restoredPreset=eve.TerrainBiomePreset();assert(restoredPreset.restoreJson(presetJson).ok);
        assert(world().beginSpawnBiome(restoredPreset).value==1);
        assert(world().stepSpawn(1).value==1 && world().getSpawnCompletedRules()==1);
        assert(world().cancelSpawn().value==3);
        assert(world().beginSpawnBiome(restoredPreset).value==1);
        assert(world().stepSpawn(10).value==2 && world().getSpawnStatus()==2);
        assert(!world().spawnStamper(restoredPreset).ok);
        assert(world().getOperationCount()==1);
    )"));
}

TEST_CASE("procgen.spawnPlan.stagedWorldSpawnPublishesOnceAndFailureRollsBack") {
    auto map = fitness();
    auto operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density = 7;
    detail.namespaceId = 6101;
    TerrainSpawnPlan plan;
    REQUIRE(plan.addSplat("texture", map, 1, operation).ok());
    REQUIRE(plan.addDetail("detail", map, detail, operation, TerrainDetailMode::Replace, 31).ok());
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.beginSpawn(plan).ok());
    auto pending = world.stepSpawn(1);
    REQUIRE(pending.ok());
    CHECK(pending.value() == TerrainWorldRunStatus::Pending);
    CHECK(world.getSpawnCompletedRules() == 1);
    TerrainSplatmap splat;
    TerrainDetailLayer details;
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(world.copyDetail(0, details).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.0F);
    CHECK(details.sample(0, 0).value() == 0);
    CHECK(!world.undo().ok());
    REQUIRE(world.cancelSpawn().ok());
    CHECK(world.getSpawnStatus() == TerrainWorldRunStatus::Cancelled);
    CHECK(world.getOperationCount() == 0);
    REQUIRE(world.beginSpawn(plan).ok());
    REQUIRE(plan.setEnabled("detail", false).ok());
    auto completed = world.stepSpawn(8);
    REQUIRE(completed.ok());
    CHECK(completed.value() == TerrainWorldRunStatus::Completed);
    CHECK(world.getOperationCount() == 1);
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(world.copyDetail(0, details).ok());
    CHECK(splat.sample(1, 0, 0).value() == 1.0F);
    CHECK(details.sampleResource(6101, 0, 0).value() == 7);

    auto invalidObject = objects();
    invalidObject.namespaceId = 0;
    TerrainSpawnPlan failing;
    REQUIRE(failing.addSplat("candidate-texture", map, 1, operation).ok());
    REQUIRE(failing.addObjects("invalid", map, invalidObject, operation, TerrainObjectOperationMode::Add).ok());
    TerrainWorldWorkspace clean;
    REQUIRE(clean.create(worldSettings()).ok());
    REQUIRE(clean.beginSpawn(failing).ok());
    CHECK(!clean.stepSpawn(2).ok());
    CHECK(clean.getSpawnStatus() == TerrainWorldRunStatus::Failed);
    CHECK(clean.getOperationCount() == 0);
    REQUIRE(clean.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.0F);
}

TEST_CASE("procgen.biomePreset.activationScopesOwnershipAndStableComposition") {
    auto map = fitness();
    auto operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density = 5;
    detail.namespaceId = 5101;
    TerrainSpawnPlan ground, canopy;
    REQUIRE(ground.addSplat("shared", map, 1, operation).ok());
    REQUIRE(ground.addDetail("detail", map, detail, operation, TerrainDetailMode::Replace, 23).ok());
    REQUIRE(canopy.addTrees("shared", map, trees(), operation, TerrainTreeOperationMode::Add).ok());
    TerrainBiomePreset preset;
    REQUIRE(preset.addSpawner("ground", ground, true, false, true).ok());
    REQUIRE(preset.addSpawner("canopy", canopy, false, true, false).ok());
    CHECK(!preset.addSpawner("ground", canopy, true, true, true).ok());
    REQUIRE(preset.getAutoAssignResources("ground").ok());
    CHECK(preset.getAutoAssignResources("ground").value());
    CHECK(!preset.getAutoAssignResources("canopy").value());
    auto biome = preset.compileForBiome();
    auto stamper = preset.compileForStamper();
    REQUIRE(biome.ok());
    REQUIRE(stamper.ok());
    CHECK(biome.value().getRuleCount() == 2);
    CHECK(stamper.value().getRuleCount() == 1);

    REQUIRE(ground.addObjects("late", map, objects(), operation, TerrainObjectOperationMode::Add).ok());
    CHECK(preset.compileForBiome().value().getRuleCount() == 2);
    TerrainWorldWorkspace world;
    REQUIRE(world.create(worldSettings()).ok());
    REQUIRE(world.spawnBiome(preset).ok());
    TerrainSplatmap splat;
    TerrainDetailLayer details;
    PointSet treePoints;
    REQUIRE(world.copySplatmap(0, splat).ok());
    REQUIRE(world.copyDetail(0, details).ok());
    REQUIRE(world.copyTrees(0, treePoints).ok());
    CHECK(splat.sample(1, 0, 0).value() == 1.0F);
    CHECK(details.sampleResource(5101, 0, 0).value() == 5);
    CHECK(treePoints.empty());
    REQUIRE(world.spawnStamper(preset).ok());
    REQUIRE(world.copyTrees(0, treePoints).ok());
    CHECK(!treePoints.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyTrees(0, treePoints).ok());
    CHECK(treePoints.empty());
    CHECK(!preset.setActiveInBiome("missing", true).ok());
}

TEST_CASE("procgen.biomePreset.strictSnapshotMigrationAndAtomicFailure") {
    auto map = fitness();
    auto operation = bounds();
    TerrainSpawnPlan plan;
    REQUIRE(plan.addTrees("tree", map, trees(), operation, TerrainTreeOperationMode::Add).ok());
    TerrainBiomePreset source;
    REQUIRE(source.addSpawner("forest", plan, true, false, false).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    TerrainBiomePreset restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    CHECK(restored.snapshotJson().value() == json.value());
    const auto before = restored.snapshotJson().value();
    CHECK(!restored.restoreJson("{\"schema\":\"eve.procgen.terrain-biome-preset\",\"version\":1,\"entries\":[],\"extra\":1}").ok());
    CHECK(restored.snapshotJson().value() == before);
    CHECK(!restored.restoreJson("{broken").ok());
    CHECK(restored.snapshotJson().value() == before);

    auto legacy = eve::Value::fromJson(json.value());
    REQUIRE(legacy.ok());
    legacy.value().set("version", 0);
    auto* entriesValue = legacy.value().find("entries");
    REQUIRE(entriesValue != nullptr);
    auto* entries = entriesValue->getIf<eve::Value::Array>();
    REQUIRE(entries != nullptr);
    auto* first = entries->at(0).getIf<eve::Value::Object>();
    REQUIRE(first != nullptr);
    first->erase("autoAssignResources");
    auto legacyJson = legacy.value().toJson();
    REQUIRE(legacyJson.ok());
    TerrainBiomePreset migrated;
    REQUIRE(migrated.restoreJson(legacyJson.value()).ok());
    CHECK(migrated.getAutoAssignResources("forest").value());
}

TEST_CASE("procgen.spawnPlan.strictSnapshotRoundTripMigrationAndAtomicFailure") {
    auto map = fitness();
    auto operation = bounds();
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density = 6;
    detail.namespaceId = 4101;
    TerrainSpawnPlan source;
    REQUIRE(source.addDetail("stable-rule", map, detail, operation, TerrainDetailMode::Add, -17).ok());
    REQUIRE(source.setEnabled("stable-rule", false).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    TerrainSpawnPlan restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    REQUIRE(restored.snapshotJson().ok());
    CHECK(restored.snapshotJson().value() == json.value());

    const auto before = restored.snapshotJson().value();
    CHECK(!restored.restoreJson("{\"schema\":\"eve.procgen.terrain-spawn-plan\",\"version\":1,\"payload\":\"00\",\"extra\":1}").ok());
    CHECK(restored.snapshotJson().value() == before);
    CHECK(!restored.restoreJson("{broken").ok());
    CHECK(restored.snapshotJson().value() == before);

    auto legacyValue = eve::Value::fromJson(json.value());
    REQUIRE(legacyValue.ok());
    legacyValue.value().set("version", 0);
    auto* payload = legacyValue.value().find("payload");
    REQUIRE(payload != nullptr);
    REQUIRE(payload->isString());
    std::string legacyPayload = payload->asString();
    const std::size_t enabledByteOffset = 4 + 1 + 4 + std::string("stable-rule").size();
    legacyPayload.erase(enabledByteOffset * 2, 2);
    legacyValue.value().set("payload", legacyPayload);
    auto legacyJson = legacyValue.value().toJson();
    REQUIRE(legacyJson.ok());
    TerrainSpawnPlan migrated;
    REQUIRE(migrated.restoreJson(legacyJson.value()).ok());
    CHECK(migrated.getRuleCount() == 1);
    CHECK(migrated.snapshotJson().value() != json.value());
}
