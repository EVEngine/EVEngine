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


TEST_CASE("procgen.probe.realVmBinding") {
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 1.0F);
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainProbePlacementSettings();
        s.name="vm-probe";s.width=2.0;s.depth=2.0;s.spacing=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.namespaceId=902;
        local r=eve.generateTerrainProbes(output(),fitness(),heights(),s,0);
        assert(r.ok && r.value==9);
    )"));
    CHECK(output.getCount() == 9);
}

TEST_CASE("procgen.probe.graphicsProviderPublicationAndMissingProvider") {
    struct ProbeSink final : eve::IProcgenProbeSink {
        int              count = 0, reflections = 0, lights = 0;
        eve::Result<int> replaceProbeBatch(const std::string&,
                                           const std::vector<eve::ProcgenProbeDesc>& probes) override {
            count = reflections = lights = 0;
            for (const auto& probe : probes) {
                ++count;
                probe.type == 0 ? ++reflections : ++lights;
            }
            return eve::Result<int>::success(count);
        }
        eve::Result<int> removeProbeBatch(const std::string&) override {
            const int removed = count;
            count = reflections = lights = 0;
            return eve::Result<int>::success(removed);
        }
        eve::Result<int> tickProbeBatches(int, int, int) override { return eve::Result<int>::success(0); }
        int              probeCount(const std::string&) const override { return count; }
        int              reflectionProbeCount(const std::string&) const override { return reflections; }
        int              lightProbeCount(const std::string&) const override { return lights; }
    } sink;
    auto* previous = eve::cap::query<eve::IProcgenProbeSink>();
    struct RestoreProvider {
        eve::IProcgenProbeSink* previous;
        eve::IProcgenProbeSink* temporary;
        ~RestoreProvider() {
            eve::cap::revoke<eve::IProcgenProbeSink>(temporary);
            if (previous) eve::cap::provide<eve::IProcgenProbeSink>(previous);
        }
    } restore{previous, &sink};
    eve::cap::provide<eve::IProcgenProbeSink>(&sink);
    Heightmap fitness(2, 2), heights(2, 2);
    std::fill(fitness.data().begin(), fitness.data().end(), 1.0F);
    std::fill(heights.data().begin(), heights.data().end(), 1.0F);
    PointSet output;
    ssq::VM  vm(1024);
    auto     table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainProbePlacementSettings();
        s.name="provider-probe";s.width=1.0;s.depth=1.0;s.spacing=1.0;s.jitterPercent=0.0;
        s.minimumFitness=0.0;s.namespaceId=904;
        assert(eve.generateTerrainProbes(output(),fitness(),heights(),s,0).ok);
        local published=eve.publishTerrainProbes("probe-test",output());
        assert(published.ok && eve.getTerrainProbeBatchCount("probe-test")==published.value);
        assert(eve.getTerrainReflectionProbeCount("probe-test")==published.value);
        assert(eve.removeTerrainProbeBatch("probe-test").ok);
    )"));
    eve::cap::revoke<eve::IProcgenProbeSink>(&sink);
    vm.run(vm.compileSource(R"(
        local missing=eve.publishTerrainProbes("probe-test",output());
        assert(!missing.ok && eve.getTerrainProbeBatchCount("probe-test")==0);
    )"));
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


// terrain_splatmap.cpp
using namespace eve::procgen;


TEST_CASE("procgen.terrainTextureAligner.bindsRealVm") {
    TerrainSplatmap a, b;
    REQUIRE(a.initialize(2, 2, 2, 0).ok());
    REQUIRE(b.initialize(2, 2, 2, 1).ok());
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("a", [&]() { return &a; });
    vm.addFunc("b", [&]() { return &b; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainTextureAlignSettings();
        s.terrainAOriginX=0.0;s.terrainAOriginZ=0.0;s.terrainAWidth=2.0;s.terrainADepth=2.0;
        s.terrainBOriginX=2.0;s.terrainBOriginZ=0.0;s.terrainBWidth=2.0;s.terrainBDepth=2.0;
        s.blendStrength=1.0;s.blendWidth=1;s.adjacencyTolerance=0.1;
        local r=eve.alignTerrainSplatTextures(a(),b(),s);
        assert(r.ok);
    )"));
    CHECK(a.sample(0, 1, 0).value() < 1.0F);
    CHECK(b.sample(0, 0, 0).value() > 0.0F);
}


TEST_CASE("procgen.terrainSplatmap.scriptPaintsAndReadsRealVm") {
    Heightmap paint(2, 1), extracted(2, 1);
    paint.setHeight(0, 0, 0.25F);
    paint.setHeight(1, 0, 0.75F);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("paint", [&]() { return &paint; });
    vm.addFunc("extracted", [&]() { return &extracted; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainSplatmap();
        assert(s.initialize(2,1,3,0).value==2);
        assert(s.paint(paint(),1).value==2);
        assert(s.getWidth()==2 && s.getHeight()==1 && s.getLayerCount()==3);
        assert(s.sample(1,0,0).value==0.25);
        assert(s.sample(1,1,0).value==0.75);
        assert(s.copyLayer(1,extracted()).ok && extracted().height(1,0)==0.75);
        assert(s.getLastChangedSamples()==2);
    )"));
}

TEST_CASE("procgen.terrainSplatmap.workspaceOwnsHistoryAndScriptBinding") {
    TerrainSplatmap source, output;
    REQUIRE(source.initialize(2, 2, 2, 0).ok());
    Heightmap paint(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) paint.setHeight(x, y, x < 2 ? 0.25F : 0.75F);
    TerrainStampSettings operation;
    operation.centerX = 2;
    operation.centerZ = 1;
    operation.width   = 4;
    operation.depth   = 2;
    TerrainMultiSplatWorkspace workspace;
    REQUIRE(workspace.addTile("west", source, 0, 0, 2, 2).ok());
    REQUIRE(workspace.addTile("east", source, 2, 0, 2, 2).ok());
    REQUIRE(workspace.paint(paint, 1, operation).ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(1, 0, 0).value() == 0.75F);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(1, 0, 0).value() == 0);
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getTileCount() == 2);
    CHECK(workspace.getLastAffectedTiles() == 2);
    CHECK(workspace.getLastChangedSamples() == 8);
    CHECK(workspace.getOperationCount() == 1);
    CHECK(workspace.getAppliedCount() == 1);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("paintMap", [&]() { return &paint; });
    vm.addFunc("operation", [&]() { return &operation; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiSplatWorkspace();
        assert(w.addTile("west",source(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("east",source(),2.0,0.0,2.0,2.0,false).ok);
        assert(w.paint(paintMap(),1,operation(),false).value==8);
        assert(w.copyTile("east",output()).ok && output().sample(1,0,0).value==0.75);
        assert(w.undo().ok && w.copyTile("east",output()).ok && output().sample(1,0,0).value==0.0);
        assert(w.redo().ok && w.getTileCount()==2 && w.getLastAffectedTiles()==2);
        assert(w.getLastChangedSamples()==8 && w.getOperationCount()==1 && w.getAppliedCount()==1);
    )"));
}


TEST_CASE("procgen.gtsHeightBlend.reweightsAllLayersAtomicallyAndBinds") {
    TerrainSplatmap input, output;
    REQUIRE(input.initialize(1, 1, 2, 0).ok());
    Heightmap half(1, 1);
    half.setHeight(0, 0, 0.5F);
    REQUIRE(paintTerrainSplatLayer(input, half, 1).ok());
    Heightmap low(1, 1), high(1, 1);
    low.setHeight(0, 0, 0.2F);
    high.setHeight(0, 0, 0.8F);
    GtsHeightBlendSet layers;
    REQUIRE(layers.addLayer(low).ok());
    REQUIRE(layers.addLayer(high).ok());
    auto result = applyGtsHeightBlend(output, input, layers, 0.1F);
    REQUIRE(result.ok());
    CHECK(result.value() == 1);
    CHECK(output.sample(0, 0, 0).value() < 1e-5F);
    CHECK(output.sample(1, 0, 0).value() > 0.999F);
    const float preserved = output.sample(1, 0, 0).value();
    CHECK(!applyGtsHeightBlend(output, input, layers, -1).ok());
    CHECK(output.sample(1, 0, 0).value() == preserved);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("low", [&]() { return &low; });
    vm.addFunc("high", [&]() { return &high; });
    vm.run(vm.compileSource(R"(
        local h=eve.GtsHeightBlendSet();assert(h.addLayer(low(),1.0,1.0,0.0).ok);assert(h.addLayer(high(),1.0,1.0,0.0).ok);
        assert(h.getLayerCount()==2 && eve.applyGtsHeightBlend(output(),input(),h,0.1).ok);
    )"));
}

TEST_CASE("procgen.gtsPackedLayers.blendsMaterialChannelsAndBinds") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(1, 1, 2, 0).ok());
    Heightmap half(1, 1);
    half.setHeight(0, 0, 0.25F);
    REQUIRE(paintTerrainSplatLayer(splat, half, 1).ok());
    eve::image::ImageData a0(1, 1, "RGBA32F"), a1(1, 1, "RGBA32F"), n0(1, 1, "RGBA32F"), n1(1, 1, "RGBA32F");
    a0.setPixel(0, 0, {0.2F, 0.4F, 0.6F, 0.1F});
    a1.setPixel(0, 0, {0.8F, 0.2F, 0.1F, 0.9F});
    n0.setPixel(0, 0, {0.5F, 0.5F, 0.2F, 0.4F});
    n1.setPixel(0, 0, {0.7F, 0.3F, 0.8F, 0.6F});
    GtsPackedLayerSettings s0, s1;
    s0.geoAmount    = 0.2F;
    s0.detailAmount = 0.4F;
    s1.geoAmount    = 0.8F;
    s1.detailAmount = 1;
    GtsPackedLayerSet layers;
    REQUIRE(layers.addLayer(a0, n0, s0).ok());
    REQUIRE(layers.addLayer(a1, n1, s1).ok());
    eve::image::ImageData albedo(1, 1, "RGBA32F"), normal(1, 1, "RGBA32F");
    Heightmap             geo(1, 1), detail(1, 1), heights(1, 1);
    auto                  r = bakeGtsPackedLayers(albedo, normal, geo, detail, heights, splat, layers);
    REQUIRE(r.ok());
    CHECK(r.value() == 4);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.35F) < 1e-6F);
    CHECK(std::abs(normal.getPixel(0, 0).b - 0.35F) < 1e-6F);
    CHECK(std::abs(normal.getPixel(0, 0).a - 0.45F) < 1e-6F);
    CHECK(std::abs(geo.height(0, 0) - 0.35F) < 1e-6F);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    exposeTerrainImageAdapter(table);
    vm.addFunc("a0", [&]() { return &a0; });
    vm.addFunc("n0", [&]() { return &n0; });
    vm.run(vm.compileSource(
        R"(local s=eve.GtsPackedLayerSettings();local p=eve.GtsPackedLayerSet();assert(p.addLayer(a0(),n0(),s).ok&&p.getLayerCount()==1);)"));
}


TEST_CASE("procgen.gtsPackedLayers.bakesTopFourDisplacementWithCameraCutoff") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(1, 1, 2, 0).ok());
    Heightmap blend(1, 1);
    blend.setHeight(0, 0, 0.25F);
    REQUIRE(paintTerrainSplatLayer(splat, blend, 1).ok());
    eve::image::ImageData a0(1, 1, "RGBA32F"), a1(1, 1, "RGBA32F"), n(1, 1, "RGBA32F");
    a0.setPixel(0, 0, {0, 0, 0, 0.25F});
    a1.setPixel(0, 0, {0, 0, 0, 1});
    n.setPixel(0, 0, {0.5F, 0.5F, 1, 0});
    GtsPackedLayerSettings s0, s1;
    s0.displacementContrast   = 1;
    s0.displacementBrightness = 2;
    s0.tessellationAmount     = 4;
    s1.displacementContrast   = 1;
    s1.displacementBrightness = 2;
    s1.tessellationAmount     = 12;
    GtsPackedLayerSet layers;
    REQUIRE(layers.addLayer(a0, n, s0).ok());
    REQUIRE(layers.addLayer(a1, n, s1).ok());
    Heightmap heights(1, 1), displacement(1, 1), tessellation(1, 1);
    auto      result = bakeGtsPackedLayerDisplacement(displacement, tessellation, heights, splat, layers, 0, 0, 0, 2);
    REQUIRE(result.ok());
    CHECK(result.value() == 2);
    CHECK(std::abs(displacement.height(0, 0) - 0.875F) < 1e-6F);
    CHECK(std::abs(tessellation.height(0, 0) - 12.0F) < 1e-6F);
    REQUIRE(bakeGtsPackedLayerDisplacement(displacement, tessellation, heights, splat, layers, 400, 0, 0, 2).ok());
    CHECK(displacement.height(0, 0) == 0);
    CHECK(tessellation.height(0, 0) == 0);
    const auto before = displacement.data();
    CHECK(!bakeGtsPackedLayerDisplacement(displacement, tessellation, heights, splat, layers, 0, 0, 0, 2, 0, 0, 0, 0, 1)
               .ok());
    CHECK(displacement.data() == before);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    exposeTerrainImageAdapter(table);
    vm.addFunc("d", [&]() { return &displacement; });
    vm.addFunc("t", [&]() { return &tessellation; });
    vm.addFunc("h", [&]() { return &heights; });
    vm.addFunc("splat", [&]() { return &splat; });
    vm.addFunc("layers", [&]() { return &layers; });
    vm.run(vm.compileSource(
        R"(assert(eve.bakeGtsPackedLayerDisplacement(d(),t(),h(),splat(),layers(),0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,1.0).ok);)"));
    CHECK(std::abs(displacement.height(0, 0) - 0.875F) < 1e-6F);
}


TEST_CASE("procgen.gtsGlobalBlend.matchesCameraSquaredDistanceAndBinding") {
    Heightmap heights(2, 1), output(2, 1);
    heights.setHeight(0, 0, 0);
    heights.setHeight(1, 0, 0);
    auto generated = generateGtsGlobalBlendDistance(output, heights, 0, 0, 0, 100, 2, 0, 0, 0, 5, 1);
    REQUIRE(generated.ok());
    CHECK(generated.value() == 1);
    CHECK(output.height(0, 0) == 0);
    CHECK(std::abs(output.height(1, 0) - 0.0625F) < 1e-6F);
    const auto before = output.data();
    CHECK(!generateGtsGlobalBlendDistance(output, heights, 0, 0, 0, -1, 1).ok());
    CHECK(output.data() == before);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    exposeTerrainImageAdapter(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local r=eve.generateGtsGlobalBlendDistance(output(),heights(),0.0,0.0,0.0,100.0,2.0,0.0,0.0,0.0,5.0,1.0);
        assert(r.ok && output().height(1,0)>0.0624 && output().height(1,0)<0.0626);
    )"));
}

TEST_CASE("procgen.gtsColorVariation.failureIsAtomicAndBindingsRun") {
    eve::image::ImageData albedo(1, 1, "RGBA32F"), colorMap(1, 1, "RGBA32F"), variation(1, 1, "RGBA32F");
    Heightmap             blend(1, 1);
    albedo.setPixel(0, 0, {0.4F, 0.4F, 0.4F, 1});
    colorMap.setPixel(0, 0, {1, 0, 0, 1});
    variation.setPixel(0, 0, {0.5F, 0, 0, 1});
    blend.setHeight(0, 0, 2);
    GtsColorMapSettings invalid;
    CHECK(!bakeGtsColorMapAlbedo(albedo, colorMap, blend, invalid).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.4F);
    blend.setHeight(0, 0, 0.5F);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; });
    vm.addFunc("map", [&]() { return &colorMap; });
    vm.addFunc("variation", [&]() { return &variation; });
    vm.addFunc("blend", [&]() { return &blend; });
    vm.run(vm.compileSource(R"(
        local c=eve.GtsColorMapSettings();c.nearIntensity=0.25;c.farIntensity=0.75;
        assert(eve.bakeGtsColorMapAlbedo(albedo(),map(),blend(),c).ok);
        local m=eve.GtsMacroVariationSettings();m.sizeA=10.0;m.sizeB=20.0;m.sizeC=30.0;m.intensity=0.5;m.objectSpace=true;
        assert(eve.bakeGtsMacroVariationAlbedo(albedo(),variation(),m,5.0,7.0,1.0,1.0).ok);
    )"));
}


TEST_CASE("procgen.gtsGeological.failureIsAtomicAndBindingRuns") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1);
    strength.setHeight(0, 0, 2);
    distance.setHeight(0, 0, 0);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F");
    eve::image::ImageData geoAlbedo(1, 1, "RGBA32F"), geoNormal(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.3F, 0.4F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    geoAlbedo.setPixel(0, 0, {0.5F, 0.5F, 0.5F, 1});
    geoNormal.setPixel(0, 0, {0, 0.5F, 0, 0.5F});
    GtsGeologicalSettings geo;
    geo.enabled = true;
    CHECK(!bakeGtsGeologicalSurface(albedo, packed, heights, strength, distance, geoAlbedo, geoNormal, geo).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.2F);
    CHECK(packed.getPixel(0, 0).b == 0.7F);
    strength.setHeight(0, 0, 1);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; });
    vm.addFunc("packed", [&]() { return &packed; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("strength", [&]() { return &strength; });
    vm.addFunc("distance", [&]() { return &distance; });
    vm.addFunc("geoAlbedo", [&]() { return &geoAlbedo; });
    vm.addFunc("geoNormal", [&]() { return &geoNormal; });
    vm.run(vm.compileSource(R"(
        local g=eve.GtsGeologicalSettings();g.enabled=true;g.objectSpace=true;g.nearScale=50.0;g.farScale=200.0;
        assert(eve.bakeGtsGeologicalSurface(albedo(),packed(),heights(),strength(),distance(),geoAlbedo(),geoNormal(),g,0.0).ok);
    )"));
}


TEST_CASE("procgen.gtsDetail.bindingFeedsSnowDetailAndFailureIsAtomic") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1), greyscale(1, 1);
    heights.setHeight(0, 0, 100);
    strength.setHeight(0, 0, 1);
    distance.setHeight(0, 0, 0);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F"), detail(1, 1, "RGBA32F");
    eve::image::ImageData snowAlbedo(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.2F, 0.2F, 1});
    packed.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    detail.setPixel(0, 0, {1, 0.5F, 0, 1});
    snowAlbedo.setPixel(0, 0, {1, 1, 1, 1});
    snowMask.setPixel(0, 0, {1, 1, 1, 1});
    GtsDetailNormalSettings bad;
    bad.nearTiling = 0;
    CHECK(!bakeGtsDetailSurface(albedo, packed, greyscale, strength, distance, detail, bad).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.2F);
    CHECK(greyscale.height(0, 0) == 0);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; });
    vm.addFunc("packed", [&]() { return &packed; });
    vm.addFunc("greyscale", [&]() { return &greyscale; });
    vm.addFunc("strength", [&]() { return &strength; });
    vm.addFunc("distance", [&]() { return &distance; });
    vm.addFunc("detail", [&]() { return &detail; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("snowAlbedo", [&]() { return &snowAlbedo; });
    vm.addFunc("snowMask", [&]() { return &snowMask; });
    vm.run(vm.compileSource(R"(
        local d=eve.GtsDetailNormalSettings();d.nearTiling=100.0;d.farTiling=1000.0;
        assert(eve.bakeGtsDetailSurface(albedo(),packed(),greyscale(),strength(),distance(),detail(),d,0.0,0.0,1.0,1.0).ok);
        local s=eve.GtsSnowSurfaceSettings();s.enabled=true;s.minimumHeight=100.0;s.blendRange=20.0;s.slopeBlend=0.0;
        local r=eve.GtsRainSurfaceSettings();
        assert(eve.bakeGtsWeatherAlbedoDetailed(albedo(),heights(),snowAlbedo(),snowMask(),s,r,greyscale(),0.0,0.0,1.0,1.0).ok);
    )"));
    CHECK(albedo.getPixel(0, 0).r < 0.5F);
}

TEST_CASE("procgen.gtsWeather.exposesProfileAndBakeToRealVm") {
    Heightmap heights(1, 1);
    heights.setHeight(0, 0, 100);
    eve::image::ImageData output(1, 1, "RGBA32F"), snowAlbedo(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F");
    output.setPixel(0, 0, {0.2F, 0.4F, 0.1F, 1});
    snowAlbedo.setPixel(0, 0, {0.8F, 0.9F, 1, 1});
    snowMask.setPixel(0, 0, {1, 1, 1, 1});
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("snowAlbedo", [&]() { return &snowAlbedo; });
    vm.addFunc("snowMask", [&]() { return &snowMask; });
    vm.run(vm.compileSource(R"(
        local snow=eve.GtsSnowSurfaceSettings();
        snow.enabled=true;snow.minimumHeight=100.0;snow.blendRange=20.0;
        local rain=eve.GtsRainSurfaceSettings();rain.enabled=true;rain.maximumHeight=3000.0;
        local result=eve.bakeGtsWeatherAlbedo(output(),heights(),snowAlbedo(),snowMask(),snow,rain,0.0,0.0,1.0,1.0);
        assert(result.ok && result.value==1);
    )"));
    CHECK(std::abs(output.getPixel(0, 0).r - 0.41F) < 1e-6F);
}


TEST_CASE("procgen.gtsWeather.exposesPbrBakeToRealVm") {
    Heightmap heights(1, 1), displacement(1, 1), tessellation(1, 1);
    heights.setHeight(0, 0, 100);
    eve::image::ImageData normal(1, 1, "RGBA32F"), mask(1, 1, "RGBA32F"), snowNormal(1, 1, "RGBA32F");
    eve::image::ImageData snowMask(1, 1, "RGBA32F"), rainData(1, 1, "RGBA32F");
    normal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    mask.setPixel(0, 0, {0, 0, 0, 0});
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    snowMask.setPixel(0, 0, {0.2F, 0.4F, 0.5F, 0.6F});
    rainData.setPixel(0, 0, {0, 0, 0, 0});
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("normal", [&]() { return &normal; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("displacement", [&]() { return &displacement; });
    vm.addFunc("tessellation", [&]() { return &tessellation; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("snowNormal", [&]() { return &snowNormal; });
    vm.addFunc("snowMask", [&]() { return &snowMask; });
    vm.addFunc("rainData", [&]() { return &rainData; });
    vm.run(vm.compileSource(R"(
        local snow=eve.GtsSnowSurfaceSettings();snow.enabled=true;snow.minimumHeight=100.0;snow.blendRange=20.0;
        snow.setMaskRemapMin(0.0,0.0,0.0,0.0);snow.setMaskRemapMax(1.0,1.0,3.0,1.0);
        local rain=eve.GtsRainSurfaceSettings();rain.enabled=true;rain.maximumHeight=3000.0;
        local r=eve.bakeGtsWeatherPbr(normal(),mask(),displacement(),tessellation(),heights(),snowNormal(),snowMask(),rainData(),snow,rain,0.0,0.0,0.0,1.0,1.0);
        assert(r.ok);
    )"));
    CHECK(std::abs(mask.getPixel(0, 0).a - 0.75F) < 1e-6F);
}


TEST_CASE("procgen.pcgMaskMapExport.combinesRedChannelsAndRejectsAtomically") {
    eve::image::ImageData output(2, 1, "RGBA32F"), red(2, 1, "RGBA32F");
    eve::image::ImageData green(2, 1, "RGBA32F"), blue(2, 1, "RGBA32F"), alpha(2, 1, "RGBA32F");
    output.setPixel(0, 0, {0.9F, 0.8F, 0.7F, 0.6F});
    output.setPixel(1, 0, {0.6F, 0.7F, 0.8F, 0.9F});
    red.setPixel(0, 0, {0.1F, 0.9F, 0.9F, 0.9F});
    red.setPixel(1, 0, {1.4F, 0, 0, 0});
    green.setPixel(0, 0, {0.2F, 0.8F, 0.8F, 0.8F});
    green.setPixel(1, 0, {0.3F, 0, 0, 0});
    blue.setPixel(0, 0, {0.4F, 0.7F, 0.7F, 0.7F});
    blue.setPixel(1, 0, {0.5F, 0, 0, 0});
    alpha.setPixel(0, 0, {0.6F, 0.6F, 0.6F, 0.6F});
    alpha.setPixel(1, 0, {-0.2F, 0, 0, 0});
    auto combined = combinePcgMaskMapChannels(output, red, green, blue, alpha, 0b1011);
    REQUIRE(combined.ok());
    CHECK_EQ(combined.value(), 2);
    auto first = output.getPixel(0, 0), second = output.getPixel(1, 0);
    CHECK(std::abs(first.r - 0.1F) < 1e-6F);
    CHECK(std::abs(first.g - 0.2F) < 1e-6F);
    CHECK(first.b == 0.0F);
    CHECK(std::abs(first.a - 0.6F) < 1e-6F);
    CHECK(second.r == 1.0F);
    CHECK(second.a == 0.0F);
    const auto            before = output.getPixel(0, 0);
    eve::image::ImageData wrong(1, 1, "RGBA32F");
    CHECK(!combinePcgMaskMapChannels(output, wrong, green, blue, alpha, 15).ok());
    const auto after = output.getPixel(0, 0);
    CHECK(before.r == after.r);
    CHECK(before.g == after.g);
    CHECK(before.b == after.b);
    CHECK(before.a == after.a);

    eve::image::ImageData atlas(4, 2, "RGBA32F");
    auto                  placed = placePcgMaskMapTileInto(atlas, output, 1, 0, 2, 2);
    REQUIRE(placed.ok());
    CHECK_EQ(placed.value(), 4);
    CHECK(std::abs(atlas.getPixel(1, 0).r - output.getPixel(0, 0).r) < 1e-6F);
    CHECK(std::abs(atlas.getPixel(2, 1).g - output.getPixel(1, 0).g) < 1e-6F);
    const auto atlasBefore = atlas.getPixel(1, 0);
    CHECK(!placePcgMaskMapTileInto(atlas, output, 3, 0, 2, 2).ok());
    CHECK(atlas.getPixel(1, 0).r == atlasBefore.r);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeTerrainImageAdapter(table);
    vm.addFunc("maskAtlas", [&]() { return &atlas; });
    vm.addFunc("maskOut", [&]() { return &output; });
    vm.addFunc("maskR", [&]() { return &red; });
    vm.addFunc("maskG", [&]() { return &green; });
    vm.addFunc("maskB", [&]() { return &blue; });
    vm.addFunc("maskA", [&]() { return &alpha; });
    vm.run(vm.compileSource(R"(
        local result=eve.combinePcgMaskMapChannels(maskOut(),maskR(),maskG(),maskB(),maskA(),15);
        assert(result.ok && result.value==2);
        local placed=eve.placePcgMaskMapTileInto(maskAtlas(),maskOut(),0,0,2,1);
        assert(placed.ok && placed.value==2);
    )"));
}

// terrain_terrace_remover.cpp
using namespace eve::procgen;


TEST_CASE("procgen.terraceRemoval.scriptBinding") {
    Heightmap source(7, 7), classes(7, 7), output(7, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) source.setHeight(x, y, 0.004F * float(y));
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("classes", [&]() { return &classes; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainTerraceRemovalSettings();
        s.perlinStrength=0.0;s.noiseSeed=73;s.terrainWorkflow=true;
        assert(eve.analyzeTerrainTerraces(classes(),source(),s).ok);
        assert(eve.removeTerrainTerraces(output(),source(),classes(),s).ok);
        s.perlinScale=-1.0;
        assert(!eve.removeTerrainTerraces(output(),source(),classes(),s).ok);
    )"));
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


TEST_CASE("procgen.terrainWorld.executesCreationAndHistoryThroughRealVm") {
    TerrainWorldWorkspace world;
    Heightmap             output;
    ssq::VM               vm(1024);
    auto                  table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWorldCreationSettings();s.tilesX=2;s.tilesZ=1;s.tileSize=2.0;s.tileHeight=8.0;
        s.heightmapResolution=3;s.controlTextureResolution=2;s.detailResolution=2;
        s.treeResolution=2;s.objectResolution=2;s.splatLayers=2;s.nameSuffix="vm";
        assert(world().create(s).value==2);
        assert(world().getTileName(0).value=="Terrain_0_0-vm");
        assert(world().getTileOriginX(0).value==-2.0 && world().getTileOriginX(1).value==0.0);
        assert(world().copyHeightmap(0,output()).value==9);
        assert(world().getTileCount()==2 && world().getOperationCount()==0);
    )"));
}

TEST_CASE("procgen.terrainHeightAdjuster.setsWorldUnitsAtomicallyAndBinds") {
    TerrainWorldWorkspace world;
    REQUIRE(world.create(smallWorld()).ok());
    auto adjusted = world.setHeightWorldUnits(250, 1000);
    REQUIRE(adjusted.ok());
    CHECK(adjusted.value() == 18);
    Heightmap output;
    REQUIRE(world.copyHeightmap(0, output).ok());
    for (float value : output.data()) CHECK(value == 0.25F);
    REQUIRE(world.setHeightWorldUnits(2000, 1000).ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    for (float value : output.data()) CHECK(value == 1);
    REQUIRE(world.undo().ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    CHECK(output.height(1, 1) == 0.25F);
    CHECK(!world.setHeightWorldUnits(1, 0).ok());
    CHECK(!world.setHeightWorldUnits(std::numeric_limits<float>::quiet_NaN(), 1000).ok());
    REQUIRE(world.copyHeightmap(1, output).ok());
    CHECK(output.height(1, 1) == 0.25F);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("world", [&]() { return &world; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(world().setHeightWorldUnits(400.0,1000.0).value==18);
        assert(world().copyHeightmap(0,output()).ok && output().height(1,1)==0.4);
        assert(world().undo().ok && world().copyHeightmap(0,output()).ok);
        assert(output().height(1,1)==0.25);
    )"));
}
