#include <algorithm>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Capability.h"
#include "common/ProcgenProbeSink.h"
#include "common/Value.h"
#include "image/ImageData.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainBiomePreset.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainImageAdapter.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTerraceRemover.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
#include "zeroerr/unittest.h"

// terrain_probe.cpp
using namespace eve::procgen;

TEST_CASE("procgen.probe.reflectionLightHeightMetadataDeterminismAndAtomicFailure") {
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 0.5F);
    TerrainProbePlacementSettings settings;
    settings.name  = "forest-probe";
    settings.width = settings.depth = 2;
    settings.spacing                = 1;
    settings.jitterPercent          = 0;
    settings.minimumFitness         = 0;
    settings.heightScale            = 10;
    settings.seaLevelActive         = true;
    settings.seaLevel               = 4;
    settings.reflectionOffset       = 2;
    settings.namespaceId            = 901;
    PointSet first, second;
    REQUIRE(exportTerrainProbePoints(first, fitness, heights, settings).ok());
    REQUIRE(exportTerrainProbePoints(second, fitness, heights, settings).ok());
    REQUIRE(first.getCount() == 9);
    REQUIRE(second.getCount() == first.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.points()[i].id == second.points()[i].id);
        CHECK(first.points()[i].x == second.points()[i].x);
        CHECK(first.points()[i].z == second.points()[i].z);
    }
    CHECK(first.points()[0].y == 7);
    CHECK(first.getStringAttribute(0, "probeResource", "") == "forest-probe");
    CHECK(first.getIntAttribute(0, "probeType", -1) == 0);
    settings.type        = TerrainProbeType::Light;
    settings.lightOffset = 2.5F;
    REQUIRE(exportTerrainProbePoints(second, fitness, heights, settings).ok());
    CHECK(second.points()[0].y == 7.5F);
    CHECK(second.getIntAttribute(0, "probeType", -1) == 1);
    const int  beforeCount        = second.getCount();
    const auto beforeId           = second.points()[0].id;
    settings.reflectionResolution = 24;
    CHECK(!exportTerrainProbePoints(second, fitness, heights, settings).ok());
    CHECK(second.getCount() == beforeCount);
    CHECK(second.points()[0].id == beforeId);
}


TEST_CASE("procgen.probe.multiTileAddReplaceRemoveAndAtomicFailure") {
    Heightmap fitness(4, 2), leftHeight(3, 3), rightHeight(3, 3);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(leftHeight.data().begin(), leftHeight.data().end(), 0.25F);
    std::fill(rightHeight.data().begin(), rightHeight.data().end(), 0.75F);
    PointSet                      left, right;
    std::vector<TerrainProbeTile> tiles{{"left", &left, &leftHeight, -2, -1, 2, 2, 2, 2, false},
                                        {"right", &right, &rightHeight, 0, -1, 2, 2, 2, 2, false}};
    TerrainProbePlacementSettings settings;
    settings.name          = "world-probes";
    settings.spacing       = 1;
    settings.jitterPercent = settings.minimumFitness = 0;
    settings.heightScale                             = 8;
    settings.seaLevelActive                          = true;
    settings.reflectionOffset                        = 1;
    settings.namespaceId                             = 903;
    settings.maxPoints                               = 100;
    TerrainStampSettings operation;
    operation.width = 4;
    operation.depth = 2;
    auto added      = applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Add);
    REQUIRE(added.ok());
    CHECK(added.value().affectedTiles == 2);
    CHECK(!left.empty());
    CHECK(!right.empty());
    CHECK(left.points()[0].y == 3);
    CHECK(right.points()[0].y == 7);
    settings.type = TerrainProbeType::Light;
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Replace).ok());
    CHECK(left.getIntAttribute(0, "probeType", -1) == 1);
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Remove).ok());
    CHECK(left.empty());
    CHECK(right.empty());
    settings.name = "atomic";
    REQUIRE(applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Add).ok());
    const auto before             = left.points()[0].id;
    settings.reflectionResolution = 24;
    CHECK(!applyTerrainProbesMultiTile(tiles, fitness, settings, operation, TerrainProbeOperationMode::Replace).ok());
    CHECK(left.points()[0].id == before);
}

// terrain_spawn_plan.cpp
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
[[maybe_unused]] TerrainWorldCreationSettings worldSettings() {
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
[[maybe_unused]] TerrainStampSettings bounds() {
    TerrainStampSettings value;
    value.centerX = value.centerZ = 0;
    value.width = 4;
    value.depth = 2;
    return value;
}
[[maybe_unused]] Heightmap fitness() {
    Heightmap value(4, 2);
    std::fill(value.data().begin(), value.data().end(), 1.0F);
    return value;
}
[[maybe_unused]] TerrainTreePlacementSettings trees() {
    TerrainTreePlacementSettings value;
    value.spacing = value.spawnDensity = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = 0;
    value.minimumWidth = value.maximumWidth = value.minimumHeight = value.maximumHeight = 1;
    value.asset = "oak";
    value.namespaceId = 1201;
    value.maxPoints = 100;
    return value;
}
[[maybe_unused]] TerrainObjectPlacementSettings objects() {
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
    // Strip the two v4 stamp fields before converting this 5x3 fixture to v0.
    const std::size_t rasterBytes   = 8 + map.data().size() * sizeof(float);
    const std::size_t oldStampBytes = 9 * sizeof(double) + 3 * sizeof(float) + sizeof(int);
    legacyPayload.erase((enabledByteOffset + 1 + rasterBytes + oldStampBytes) * 2, 16);
    legacyPayload.erase(enabledByteOffset * 2, 2);
    legacyValue.value().set("payload", legacyPayload);
    auto legacyJson = legacyValue.value().toJson();
    REQUIRE(legacyJson.ok());
    TerrainSpawnPlan migrated;
    REQUIRE(migrated.restoreJson(legacyJson.value()).ok());
    CHECK(migrated.getRuleCount() == 1);
    CHECK(migrated.snapshotJson().value() != json.value());
}

// terrain_splatmap.cpp
using namespace eve::procgen;

TEST_CASE("procgen.terrainSplatmap.renormalizesTargetAndOtherLayers") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 1, 3, 0).ok());
    Heightmap paint(2, 1);
    paint.setHeight(0, 0, 0.25F);
    paint.setHeight(1, 0, 1);
    auto changed = paintTerrainSplatLayer(splat, paint, 1);
    REQUIRE(changed.ok());
    CHECK(changed.value() == 2);
    CHECK(splat.sample(0, 0, 0).value() == 0.375F);
    CHECK(splat.sample(1, 0, 0).value() == 0.25F);
    CHECK(splat.sample(2, 0, 0).value() == 0.375F);
    CHECK(splat.sample(0, 1, 0).value() == 0);
    CHECK(splat.sample(1, 1, 0).value() == 1);
    CHECK(splat.sample(2, 1, 0).value() == 0);
    CHECK(splat.getLastChangedSamples() == 2);
    Heightmap extracted(2, 1);
    REQUIRE(splat.copyLayer(1, extracted).ok());
    CHECK(extracted.data()[0] == 0.25F);
    CHECK(extracted.data()[1] == 1.0F);
    auto beforeExtract = extracted.data();
    CHECK(!splat.copyLayer(3, extracted).ok());
    CHECK(extracted.data() == beforeExtract);

    Heightmap repaint(2, 1);
    repaint.setHeight(0, 0, 0.5F);
    repaint.setHeight(1, 0, 0.5F);
    REQUIRE(paintTerrainSplatLayer(splat, repaint, 2).ok());
    CHECK(splat.sample(0, 0, 0).value() == 0.25F);
    CHECK(splat.sample(1, 0, 0).value() == 0.25F / 1.5F);
    CHECK(splat.sample(2, 0, 0).value() == 0.5F);
}

TEST_CASE("procgen.terrainSplatmap.failureIsAtomic") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 2, 2, 0).ok());
    Heightmap wrong(1, 2);
    CHECK(!paintTerrainSplatLayer(splat, wrong, 1).ok());
    CHECK(splat.sample(0, 1, 1).value() == 1);
    CHECK(splat.sample(1, 1, 1).value() == 0);
    Heightmap invalid(2, 2);
    invalid.setHeight(0, 0, 2);
    CHECK(!paintTerrainSplatLayer(splat, invalid, 1).ok());
    CHECK(splat.getLastChangedSamples() == 0);
}

TEST_CASE("procgen.terrainTextureAligner.blendsAdjacentEdgesAndIsAtomic") {
    TerrainSplatmap a, b;
    REQUIRE(a.initialize(4, 4, 2, 0).ok());
    REQUIRE(b.initialize(4, 4, 2, 1).ok());
    TerrainTextureAlignSettings settings;
    settings.terrainAOriginX = 0;
    settings.terrainAOriginZ = 0;
    settings.terrainAWidth   = 4;
    settings.terrainADepth   = 4;
    settings.terrainBOriginX = 0;
    settings.terrainBOriginZ = 4;
    settings.terrainBWidth   = 4;
    settings.terrainBDepth   = 4;
    settings.blendStrength   = 1;
    settings.blendWidth      = 2;
    auto aligned             = alignTerrainSplatTextures(a, b, settings);
    REQUIRE(aligned.ok());
    CHECK(aligned.value() == 16);
    int mixedA = 0, mixedB = 0;
    for (int x = 0; x < 4; ++x) {
        const float a0 = a.sample(0, x, 3).value(), a1 = a.sample(1, x, 3).value();
        const float b0 = b.sample(0, x, 0).value(), b1 = b.sample(1, x, 0).value();
        mixedA += a0 > 0 && a0 < 1;
        mixedB += b0 > 0 && b0 < 1;
        CHECK(std::abs(a0 + a1 - 1) < 1e-6F);
        CHECK(std::abs(b0 + b1 - 1) < 1e-6F);
    }
    CHECK(mixedA > 0);
    CHECK(mixedB > 0);
    const float preserved    = a.sample(0, 0, 3).value();
    settings.terrainBOriginZ = 20;
    CHECK(!alignTerrainSplatTextures(a, b, settings).ok());
    CHECK(a.sample(0, 0, 3).value() == preserved);
    CHECK(!alignTerrainSplatTextures(a, a, settings).ok());
}


TEST_CASE("procgen.terrainSplatmap.multiTileMapsAndPublishesTogether") {
    TerrainSplatmap west, east;
    REQUIRE(west.initialize(2, 2, 2, 0).ok());
    REQUIRE(east.initialize(2, 2, 2, 0).ok());
    Heightmap paint(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) paint.setHeight(x, y, x < 2 ? 0.25F : 0.75F);
    TerrainStampSettings operation;
    operation.centerX = 2;
    operation.centerZ = 1;
    operation.width   = 4;
    operation.depth   = 2;
    std::vector<TerrainSplatTile> tiles{{"west", &west, 0, 0, 2, 2, false}, {"east", &east, 2, 0, 2, 2, false}};
    auto                          result = paintTerrainSplatLayerMultiTile(tiles, paint, 1, operation);
    REQUIRE(result.ok());
    CHECK(result.value().affectedTiles == 2);
    CHECK(result.value().changedSamples == 8);
    CHECK(west.sample(1, 1, 1).value() == 0.25F);
    CHECK(east.sample(1, 0, 0).value() == 0.75F);
    Heightmap wrong(3, 2);
    CHECK(!paintTerrainSplatLayerMultiTile(tiles, wrong, 1, operation).ok());
    CHECK(west.sample(1, 1, 1).value() == 0.25F);
    CHECK(east.sample(1, 0, 0).value() == 0.75F);
}


TEST_CASE("procgen.terrainSplatmap.bakesPaletteIntoAtomicAlbedo") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 1, 2, 0).ok());
    Heightmap paint(2, 1);
    paint.setHeight(0, 0, 0.25F);
    paint.setHeight(1, 0, 0.75F);
    REQUIRE(paintTerrainSplatLayer(splat, paint, 1).ok());
    TerrainSplatPalette palette;
    REQUIRE(palette.addColor(0.2F, 0.4F, 0.1F).ok());
    REQUIRE(palette.addColor(0.8F, 0.6F, 0.3F).ok());
    eve::image::ImageData image(2, 1, "RGBA32F");
    auto                  baked = bakeTerrainSplatAlbedo(image, splat, palette);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 2);
    CHECK(std::abs(image.getPixel(0, 0).r - 0.35F) < 1e-6F);
    CHECK(std::abs(image.getPixel(1, 0).r - 0.65F) < 1e-6F);
    const auto          preserved = image.getPixel(0, 0);
    TerrainSplatPalette incomplete;
    REQUIRE(incomplete.addColor(1, 0, 0).ok());
    CHECK(!bakeTerrainSplatAlbedo(image, splat, incomplete).ok());
    CHECK(image.getPixel(0, 0).r == preserved.r);
}


TEST_CASE("procgen.gtsPackedLayers.matchesTriplanarAndStochasticSampling") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 2, 1, 0).ok());
    Heightmap heights(2, 2);
    heights.setHeight(0, 0, 0);
    heights.setHeight(1, 0, 2);
    heights.setHeight(0, 1, 0);
    heights.setHeight(1, 1, 2);
    eve::image::ImageData albedoTexture(2, 2, "RGBA32F"), normalTexture(1, 1, "RGBA32F");
    albedoTexture.setPixel(0, 0, {0, 0, 0, 0});
    albedoTexture.setPixel(1, 0, {1, 0, 0, 0.25F});
    albedoTexture.setPixel(0, 1, {0, 1, 0, 0.75F});
    albedoTexture.setPixel(1, 1, {0, 0, 1, 1});
    normalTexture.setPixel(0, 0, {0.5F, 0.5F, 1, 0.5F});
    auto bake = [&](GtsPackedLayerSettings settings) {
        GtsPackedLayerSet layers;
        REQUIRE(layers.addLayer(albedoTexture, normalTexture, settings).ok());
        eve::image::ImageData albedo(2, 2, "RGBA32F"), normal(2, 2, "RGBA32F");
        Heightmap             geo(2, 2), detail(2, 2);
        REQUIRE(bakeGtsPackedLayers(albedo, normal, geo, detail, heights, splat, layers, 3, 5, 7, 1, 1).ok());
        return albedo.getPixel(1, 1);
    };
    GtsPackedLayerSettings planar;
    const auto             plain = bake(planar);
    GtsPackedLayerSettings stochastic;
    stochastic.stochastic         = true;
    const auto             random = bake(stochastic);
    GtsPackedLayerSettings triplanar;
    triplanar.triPlanar      = true;
    triplanar.triPlanarSizeX = 0.4F;
    triplanar.triPlanarSizeZ = 0.7F;
    const auto projected     = bake(triplanar);
    CHECK(std::abs(plain.r - random.r) + std::abs(plain.g - random.g) + std::abs(plain.b - random.b) > 1e-5F);
    CHECK(std::abs(plain.r - projected.r) + std::abs(plain.g - projected.g) + std::abs(plain.b - projected.b) > 1e-5F);
}


TEST_CASE("procgen.gtsWeather.matchesSnowThenRainAlbedoBranches") {
    Heightmap heights(2, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) heights.setHeight(x, y, 100);
    eve::image::ImageData output(2, 2, "RGBA32F"), snowAlbedo(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F");
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) output.setPixel(x, y, {0.2F, 0.4F, 0.1F, 1});
    snowAlbedo.setPixel(0, 0, {0.8F, 0.9F, 1, 1});
    snowMask.setPixel(0, 0, {0.1F, 0.2F, 0.3F, 1});
    GtsSnowSurfaceSettings snow;
    snow.enabled       = true;
    snow.minimumHeight = 100;
    snow.blendRange    = 20;
    GtsRainSurfaceSettings rain;
    rain.enabled       = true;
    rain.maximumHeight = 3000;
    auto baked         = bakeGtsWeatherAlbedo(output, heights, snowAlbedo, snowMask, snow, rain);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 4);
    CHECK(std::abs(output.getPixel(0, 0).r - 0.41F) < 1e-6F);
    CHECK(std::abs(output.getPixel(0, 0).g - 0.533F) < 1e-6F);
    CHECK(std::abs(output.getPixel(0, 0).b - 0.451F) < 1e-6F);
    CHECK(output.getPixel(0, 0).a == 1);

    const auto preserved = output.getPixel(0, 0);
    snow.scale           = 0;
    CHECK(!bakeGtsWeatherAlbedo(output, heights, snowAlbedo, snowMask, snow, rain).ok());
    CHECK(output.getPixel(0, 0).r == preserved.r);
}

TEST_CASE("procgen.gtsColorVariation.matchesGtsShaderOrderAndMath") {
    eve::image::ImageData albedo(1, 1, "RGBA32F"), colorMap(1, 1, "RGBA32F"), variation(1, 1, "RGBA32F");
    Heightmap             blend(1, 1);
    albedo.setPixel(0, 0, {0.2F, 0.4F, 0.6F, 1});
    colorMap.setPixel(0, 0, {0.8F, 0.2F, 0.4F, 0.5F});
    variation.setPixel(0, 0, {0.25F, 0, 0, 1});
    blend.setHeight(0, 0, 0.5F);
    GtsColorMapSettings color;
    color.alphaIntensity = 2;
    color.colorIntensity = 0.5F;
    color.nearIntensity  = 0.25F;
    color.farIntensity   = 0.75F;
    REQUIRE(bakeGtsColorMapAlbedo(albedo, colorMap, blend, color).ok());
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).g - 0.25F) < 1e-6F);
    GtsMacroVariationSettings macro;
    macro.intensity = 0.5F;
    REQUIRE(bakeGtsMacroVariationAlbedo(albedo, variation, macro).ok());
    const float multiplier = 0.5F + 0.5F * (0.75F * 0.75F * 0.75F);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3F * multiplier) < 1e-6F);
    CHECK(albedo.getPixel(0, 0).a == 1);
}


TEST_CASE("procgen.gtsGeological.blendsHeightStripColorAndPackedNormal") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1);
    heights.setHeight(0, 0, 0);
    strength.setHeight(0, 0, 0.5F);
    distance.setHeight(0, 0, 0.25F);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F");
    eve::image::ImageData geoAlbedo(1, 1, "RGBA32F"), geoNormal(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.3F, 0.4F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    geoAlbedo.setPixel(0, 0, {0.5F, 0.25F, 0.75F, 1});
    geoNormal.setPixel(0, 0, {0, 0.75F, 0, 0.625F});
    GtsGeologicalSettings geo;
    geo.enabled            = true;
    geo.nearStrength       = 0.2F;
    geo.farStrength        = 0.4F;
    geo.nearNormalStrength = 0.4F;
    geo.farNormalStrength  = 0.8F;
    auto result = bakeGtsGeologicalSurface(albedo, packed, heights, strength, distance, geoAlbedo, geoNormal, geo);
    REQUIRE(result.ok());
    CHECK(result.value() == 2);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3225F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).g - 0.335F) < 1e-6F);
    CHECK(packed.getPixel(0, 0).r > 0.53F);
    CHECK(packed.getPixel(0, 0).g > 0.57F);
    CHECK(packed.getPixel(0, 0).b == 0.7F);
    CHECK(packed.getPixel(0, 0).a == 0.8F);
}


TEST_CASE("procgen.gtsDetail.matchesNearFarNormalAndMicroShadow") {
    Heightmap strength(1, 1), distance(1, 1), greyscale(1, 1);
    strength.setHeight(0, 0, 0.5F);
    distance.setHeight(0, 0, 0.25F);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F"), detail(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.8F, 0.4F, 0.2F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    detail.setPixel(0, 0, {0.75F, 0.25F, 0, 1});
    GtsDetailNormalSettings settings;
    settings.nearStrength = 0.4F;
    settings.farStrength  = 0.8F;
    auto result           = bakeGtsDetailSurface(albedo, packed, greyscale, strength, distance, detail, settings);
    REQUIRE(result.ok());
    CHECK(result.value() == 3);
    CHECK(std::abs(greyscale.height(0, 0) - 0.125F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.775F) < 1e-6F);
    CHECK(std::abs(packed.getPixel(0, 0).r - 0.5625F) < 1e-6F);
    CHECK(std::abs(packed.getPixel(0, 0).g - 0.4375F) < 1e-6F);
    CHECK(packed.getPixel(0, 0).b == 0.7F);
    CHECK(packed.getPixel(0, 0).a == 0.8F);
}


TEST_CASE("procgen.gtsWeather.bakesPackedPbrAndDisplacementAtomically") {
    Heightmap heights(1, 1), displacement(1, 1), tessellation(1, 1);
    heights.setHeight(0, 0, 100);
    eve::image::ImageData normal(1, 1, "RGBA32F"), mask(1, 1, "RGBA32F");
    eve::image::ImageData snowNormal(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F"), rainData(1, 1, "RGBA32F");
    normal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    mask.setPixel(0, 0, {0, 0, 0, 0});
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    snowMask.setPixel(0, 0, {0.2F, 0.4F, 0.5F, 0.6F});
    rainData.setPixel(0, 0, {0, 0, 0, 0});
    GtsSnowSurfaceSettings snow;
    snow.enabled       = true;
    snow.minimumHeight = 100;
    snow.blendRange    = 20;
    GtsRainSurfaceSettings rain;
    rain.enabled       = true;
    rain.maximumHeight = 3000;
    auto baked = bakeGtsWeatherPbr(normal, mask, displacement, tessellation, heights, snowNormal, snowMask, rainData,
                                   snow, rain, 0);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 4);
    CHECK(std::abs(mask.getPixel(0, 0).r - 0.1F) < 1e-6F);
    CHECK(std::abs(mask.getPixel(0, 0).b - 0.75F) < 1e-6F);
    CHECK(std::abs(mask.getPixel(0, 0).a - 0.75F) < 1e-6F);
    CHECK(std::abs(displacement.height(0, 0) - 0.35F) < 1e-6F);
    CHECK(std::abs(tessellation.height(0, 0) - 12.5F) < 1e-6F);
    const auto before = mask.getPixel(0, 0);
    CHECK(!bakeGtsWeatherPbr(normal, mask, displacement, tessellation, heights, snowNormal, snowMask, rainData, snow,
                             rain, std::numeric_limits<double>::infinity())
               .ok());
    CHECK(mask.getPixel(0, 0).a == before.a);
    CHECK(displacement.height(0, 0) == 0.35F);
}


TEST_CASE("procgen.gtsWeather.rainNormalsUseExplicitDeterministicTime") {
    Heightmap heights(2, 2), displacementA(2, 2), tessellationA(2, 2), displacementB(2, 2), tessellationB(2, 2);
    eve::image::ImageData normalA(2, 2, "RGBA32F"), normalB(2, 2, "RGBA32F");
    eve::image::ImageData maskA(2, 2, "RGBA32F"), maskB(2, 2, "RGBA32F");
    eve::image::ImageData snowNormal(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F"), rainData(2, 2, "RGBA32F");
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    snowMask.setPixel(0, 0, {0, 0, 0, 0});
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            heights.setHeight(x, y, 100);
            normalA.setPixel(x, y, {0.5F, 0.5F, 1, 1});
            normalB.setPixel(x, y, {0.5F, 0.5F, 1, 1});
            maskA.setPixel(x, y, {0, 0, 0, 0.2F});
            maskB.setPixel(x, y, {0, 0, 0, 0.2F});
            const float phase = 0.1F + 0.2F * x + 0.3F * y;
            rainData.setPixel(x, y, {phase, phase * 0.7F, phase * 0.4F, phase * 0.2F});
        }
    GtsSnowSurfaceSettings snow;
    GtsRainSurfaceSettings rain;
    rain.enabled       = true;
    rain.maximumHeight = 3000;
    rain.scale         = 3;
    REQUIRE(bakeGtsWeatherPbr(normalA, maskA, displacementA, tessellationA, heights, snowNormal, snowMask, rainData,
                              snow, rain, 0.125)
                .ok());
    REQUIRE(bakeGtsWeatherPbr(normalB, maskB, displacementB, tessellationB, heights, snowNormal, snowMask, rainData,
                              snow, rain, 0.625)
                .ok());
    const auto a = normalA.getPixel(0, 0), b = normalB.getPixel(0, 0);
    CHECK(std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) > 1e-5F);
}


// terrain_terrace_remover.cpp
using namespace eve::procgen;

TEST_CASE("procgen.terraceRemoval.classificationPriorityAndEdges") {
    Heightmap source(5, 5), classes(5, 5);
    source.data().assign(25, 0.1F);
    source.setHeight(2, 1, 0.F);
    source.setHeight(2, 3, 0.3F);
    TerrainTerraceRemovalSettings settings;
    settings.minimumTerraceThreshold = 0.2F;
    settings.maximumTerraceThreshold = 0.4F;
    REQUIRE(analyzeTerrainTerraces(classes, source, settings).ok());
    CHECK(classes.height(2, 2) == float(TerrainTerraceClass::Green));
    CHECK(classes.height(0, 0) == float(TerrainTerraceClass::Black));

    const auto before                = classes.data();
    settings.minimumTerraceThreshold = 0.5F;
    settings.maximumTerraceThreshold = 0.4F;
    CHECK(!analyzeTerrainTerraces(classes, source, settings).ok());
    CHECK(classes.data() == before);
    CHECK(!analyzeTerrainTerraces(source, source, TerrainTerraceRemovalSettings{}).ok());
}

TEST_CASE("procgen.terraceRemoval.pipelineAndAtomicFailure") {
    Heightmap source(7, 7), classes(7, 7), output(7, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) source.setHeight(x, y, 0.005F * float(y) + (x == 3 ? 0.003F : 0.F));
    classes.data().assign(49, float(TerrainTerraceClass::Green));
    output.data().assign(49, -1.F);
    TerrainTerraceRemovalSettings settings;
    settings.perlinStrength = 0.F;
    REQUIRE(removeTerrainTerraces(output, source, classes, settings).ok());
    CHECK(output.height(0, 0) == source.height(0, 0));
    CHECK(output.height(6, 6) == source.height(6, 6));
    CHECK(output.height(3, 3) < source.height(3, 3));
    for (float value : output.data()) CHECK(std::isfinite(value));

    const auto before    = output.data();
    settings.perlinScale = -1.F;
    CHECK(!removeTerrainTerraces(output, source, classes, settings).ok());
    CHECK(output.data() == before);
    CHECK(!removeTerrainTerraces(classes, source, classes, TerrainTerraceRemovalSettings{}).ok());
}


// terrain_world_workspace.cpp
using namespace eve::procgen;

namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
[[maybe_unused]] TerrainWorldCreationSettings smallWorld() {
    TerrainWorldCreationSettings s;
    s.tilesX                   = 2;
    s.tilesZ                   = 1;
    s.tileSize                 = 2;
    s.tileHeight               = 8;
    s.heightmapResolution      = 3;
    s.controlTextureResolution = 2;
    s.detailResolution         = 2;
    s.treeResolution           = 2;
    s.objectResolution         = 2;
    s.splatLayers              = 2;
    s.nameSuffix               = "stable";
    return s;
}
[[maybe_unused]] TerrainStampSettings wholeWorld() {
    TerrainStampSettings s;
    s.centerX = 0;
    s.centerZ = 0;
    s.width   = 4;
    s.depth   = 2;
    return s;
}
}  // namespace

TEST_CASE("procgen.terrainWorld.createsCenteredPcgTopologyAtomically") {
    TerrainWorldWorkspace world;
    auto                  settings = smallWorld();
    REQUIRE(world.create(settings).ok());
    CHECK(world.getTileCount() == 2);
    CHECK(world.getTileName(0).value() == "Terrain_0_0-stable");
    CHECK(world.getTileName(1).value() == "Terrain_1_0-stable");
    CHECK(world.getTileOriginX(0).value() == -2);
    CHECK(world.getTileOriginX(1).value() == 0);
    CHECK(world.getTileOriginZ(0).value() == -1);
    Heightmap height;
    REQUIRE(world.copyHeightmap(0, height).ok());
    CHECK(height.getWidth() == 3);
    CHECK(height.getHeight() == 3);
    settings.heightmapResolution = 4;
    CHECK(!world.create(settings).ok());
    CHECK(world.getTileCount() == 2);
    CHECK(world.getTileName(0).value() == "Terrain_0_0-stable");

    settings          = smallWorld();
    settings.worldMap = true;
    REQUIRE(world.create(settings).ok());
    CHECK(world.getTileName(0).value() == "World Map_0_0-stable");
}

TEST_CASE("procgen.terrainWorld.ownsCrossDomainHistory") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto operation = wholeWorld();
    auto paint     = filled(4, 2, 0.75F);
    REQUIRE(world.paintSplat(paint, 1, operation).ok());
    TerrainSplatmap splat;
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);

    TerrainObjectPlacementSettings objects;
    objects.spacing = objects.spawnDensity = 1;
    objects.jitterPercent                  = 0;
    objects.failureRate = objects.minimumFitness = objects.minimumInstanceFitness = 0;
    objects.boundsRadius                                                          = 0.2F;
    objects.prototype                                                             = "rocks";
    objects.namespaceId                                                           = 501;
    objects.maxPoints                                                             = 100;
    TerrainObjectInstanceSettings child;
    child.asset = "rock-a";
    REQUIRE(objects.addInstance(child).ok());
    REQUIRE(world.applyObjects(paint, objects, operation, TerrainObjectOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(!points.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(points.empty());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0);
    REQUIRE(world.redo().ok());
    REQUIRE(world.redo().ok());
    CHECK(world.getOperationCount() == 2);
    CHECK(world.getAppliedCount() == 2);

    auto invalidPaint = filled(3, 3, 1);
    CHECK(!world.paintSplat(invalidPaint, 1, operation).ok());
    CHECK(world.getOperationCount() == 2);
    CHECK(world.getAppliedCount() == 2);
    REQUIRE(world.copyObjects(0, points).ok());
    CHECK(!points.empty());
    REQUIRE(world.copySplatmap(0, splat).ok());
    CHECK(splat.sample(1, 0, 0).value() == 0.75F);
}


TEST_CASE("procgen.terrainWorld.routesHeightDetailAndTreeDomains") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto operation = wholeWorld();
    auto one = filled(1, 1, 1), stamp = filled(1, 1, 0.5F);
    REQUIRE(world.stamp(stamp, operation, one, one).ok());
    Heightmap heights;
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(heights.height(1, 1) > 0);

    auto                  fitness = filled(4, 2, 1);
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density                           = 4;
    detail.namespaceId                       = 701;
    REQUIRE(world.applyDetail(fitness, detail, operation, TerrainDetailMode::Replace, 17).ok());
    TerrainDetailLayer details;
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 4);
    auto flowers        = detail;
    flowers.density     = 2;
    flowers.namespaceId = 702;
    REQUIRE(world.applyDetail(fitness, flowers, operation, TerrainDetailMode::Add, 19).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 6);
    TerrainWorldClearSettings detailSourceClear;
    detailSourceClear.trees = detailSourceClear.objects = false;
    detailSourceClear.sourceNamespace                   = 701;
    REQUIRE(world.clearSpawns(detailSourceClear).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 2);
    REQUIRE(world.undo().ok());

    TerrainTreePlacementSettings trees;
    trees.spacing = trees.spawnDensity = 1;
    trees.jitterPercent = trees.failureRate = trees.minimumFitness = 0;
    trees.minimumWidth = trees.maximumWidth = trees.minimumHeight = trees.maximumHeight = 1;
    trees.asset                                                                         = "oak";
    trees.namespaceId                                                                   = 601;
    trees.maxPoints                                                                     = 100;
    REQUIRE(world.applyTrees(fitness, trees, operation, TerrainTreeOperationMode::Add).ok());
    PointSet points;
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(!points.empty());
    CHECK(world.getOperationCount() == 4);

    auto pines        = trees;
    pines.asset       = "pine";
    pines.namespaceId = 602;
    REQUIRE(world.applyTrees(fitness, pines, operation, TerrainTreeOperationMode::Add).ok());
    TerrainWorldClearSettings sourceClear;
    sourceClear.details = sourceClear.objects = false;
    sourceClear.sourceNamespace               = 601;
    REQUIRE(world.clearSpawns(sourceClear).ok());
    REQUIRE(world.copyTrees(0, points).ok());
    REQUIRE(!points.empty());
    for (int i = 0; i < points.getCount(); ++i) CHECK(points.getStringAttribute(i, "spawnNamespace", "") == "602");
    REQUIRE(world.undo().ok());

    TerrainWorldClearSettings clear;
    clear.objects = false;
    REQUIRE(world.clearSpawns(clear).ok());
    REQUIRE(world.copyDetail(1, details).ok());
    CHECK(details.sample(0, 0).value() == 0);
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(points.empty());
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyTrees(0, points).ok());
    CHECK(!points.empty());

    REQUIRE(world.flatten().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    for (float value : heights.data()) CHECK(value == 0);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(0, heights).ok());
    CHECK(heights.height(1, 1) > 0);
}
