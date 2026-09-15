#include "zeroerr/unittest.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <limits>

#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainImageAdapter.h"
#include "image/ImageData.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"

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
    settings.terrainAWidth = 4;
    settings.terrainADepth = 4;
    settings.terrainBOriginX = 0;
    settings.terrainBOriginZ = 4;
    settings.terrainBWidth = 4;
    settings.terrainBDepth = 4;
    settings.blendStrength = 1;
    settings.blendWidth = 2;
    auto aligned = alignTerrainSplatTextures(a, b, settings);
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
    const float preserved = a.sample(0, 0, 3).value();
    settings.terrainBOriginZ = 20;
    CHECK(!alignTerrainSplatTextures(a, b, settings).ok());
    CHECK(a.sample(0, 0, 3).value() == preserved);
    CHECK(!alignTerrainSplatTextures(a, a, settings).ok());
}

TEST_CASE("procgen.terrainTextureAligner.bindsRealVm") {
    TerrainSplatmap a, b;
    REQUIRE(a.initialize(2, 2, 2, 0).ok());
    REQUIRE(b.initialize(2, 2, 2, 1).ok());
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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

TEST_CASE("procgen.terrainSplatmap.multiTileMapsAndPublishesTogether") {
    TerrainSplatmap west, east;
    REQUIRE(west.initialize(2, 2, 2, 0).ok());
    REQUIRE(east.initialize(2, 2, 2, 0).ok());
    Heightmap paint(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) paint.setHeight(x, y, x < 2 ? 0.25F : 0.75F);
    TerrainStampSettings operation;
    operation.centerX = 2; operation.centerZ = 1; operation.width = 4; operation.depth = 2;
    std::vector<TerrainSplatTile> tiles{{"west", &west, 0, 0, 2, 2, false},
                                        {"east", &east, 2, 0, 2, 2, false}};
    auto result = paintTerrainSplatLayerMultiTile(tiles, paint, 1, operation);
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

TEST_CASE("procgen.terrainSplatmap.scriptPaintsAndReadsRealVm") {
    Heightmap paint(2, 1), extracted(2, 1);
    paint.setHeight(0, 0, 0.25F); paint.setHeight(1, 0, 0.75F);
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
    operation.centerX = 2; operation.centerZ = 1; operation.width = 4; operation.depth = 2;
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
    auto table = vm.addTable("eve");
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

TEST_CASE("procgen.terrainSplatmap.bakesPaletteIntoAtomicAlbedo") {
    TerrainSplatmap splat;
    REQUIRE(splat.initialize(2, 1, 2, 0).ok());
    Heightmap paint(2, 1); paint.setHeight(0, 0, 0.25F); paint.setHeight(1, 0, 0.75F);
    REQUIRE(paintTerrainSplatLayer(splat, paint, 1).ok());
    TerrainSplatPalette palette;
    REQUIRE(palette.addColor(0.2F, 0.4F, 0.1F).ok());
    REQUIRE(palette.addColor(0.8F, 0.6F, 0.3F).ok());
    eve::image::ImageData image(2, 1, "RGBA32F");
    auto baked = bakeTerrainSplatAlbedo(image, splat, palette);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 2);
    CHECK(std::abs(image.getPixel(0, 0).r - 0.35F) < 1e-6F);
    CHECK(std::abs(image.getPixel(1, 0).r - 0.65F) < 1e-6F);
    const auto preserved = image.getPixel(0, 0);
    TerrainSplatPalette incomplete;
    REQUIRE(incomplete.addColor(1, 0, 0).ok());
    CHECK(!bakeTerrainSplatAlbedo(image, splat, incomplete).ok());
    CHECK(image.getPixel(0, 0).r == preserved.r);
}

TEST_CASE("procgen.gtsHeightBlend.reweightsAllLayersAtomicallyAndBinds") {
    TerrainSplatmap input, output;
    REQUIRE(input.initialize(1, 1, 2, 0).ok());
    Heightmap half(1, 1); half.setHeight(0, 0, 0.5F); REQUIRE(paintTerrainSplatLayer(input, half, 1).ok());
    Heightmap low(1, 1), high(1, 1); low.setHeight(0, 0, 0.2F); high.setHeight(0, 0, 0.8F);
    GtsHeightBlendSet layers; REQUIRE(layers.addLayer(low).ok()); REQUIRE(layers.addLayer(high).ok());
    auto result = applyGtsHeightBlend(output, input, layers, 0.1F);
    REQUIRE(result.ok()); CHECK(result.value() == 1);
    CHECK(output.sample(0, 0, 0).value() < 1e-5F); CHECK(output.sample(1, 0, 0).value() > 0.999F);
    const float preserved = output.sample(1, 0, 0).value();
    CHECK(!applyGtsHeightBlend(output, input, layers, -1).ok()); CHECK(output.sample(1, 0, 0).value() == preserved);
    ssq::VM vm(1024); auto table = vm.addTable("eve"); exposeHeightmap(table);
    vm.addFunc("input", [&]() { return &input; }); vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("low", [&]() { return &low; }); vm.addFunc("high", [&]() { return &high; });
    vm.run(vm.compileSource(R"(
        local h=eve.GtsHeightBlendSet();assert(h.addLayer(low(),1.0,1.0,0.0).ok);assert(h.addLayer(high(),1.0,1.0,0.0).ok);
        assert(h.getLayerCount()==2 && eve.applyGtsHeightBlend(output(),input(),h,0.1).ok);
    )"));
}

TEST_CASE("procgen.gtsPackedLayers.blendsMaterialChannelsAndBinds") {
    TerrainSplatmap splat; REQUIRE(splat.initialize(1,1,2,0).ok()); Heightmap half(1,1); half.setHeight(0,0,0.25F);
    REQUIRE(paintTerrainSplatLayer(splat,half,1).ok());
    eve::image::ImageData a0(1,1,"RGBA32F"),a1(1,1,"RGBA32F"),n0(1,1,"RGBA32F"),n1(1,1,"RGBA32F");
    a0.setPixel(0,0,{0.2F,0.4F,0.6F,0.1F}); a1.setPixel(0,0,{0.8F,0.2F,0.1F,0.9F});
    n0.setPixel(0,0,{0.5F,0.5F,0.2F,0.4F}); n1.setPixel(0,0,{0.7F,0.3F,0.8F,0.6F});
    GtsPackedLayerSettings s0,s1; s0.geoAmount=0.2F;s0.detailAmount=0.4F;s1.geoAmount=0.8F;s1.detailAmount=1;
    GtsPackedLayerSet layers; REQUIRE(layers.addLayer(a0,n0,s0).ok()); REQUIRE(layers.addLayer(a1,n1,s1).ok());
    eve::image::ImageData albedo(1,1,"RGBA32F"),normal(1,1,"RGBA32F"); Heightmap geo(1,1),detail(1,1),heights(1,1);
    auto r=bakeGtsPackedLayers(albedo,normal,geo,detail,heights,splat,layers); REQUIRE(r.ok()); CHECK(r.value()==4);
    CHECK(std::abs(albedo.getPixel(0,0).r-0.35F)<1e-6F); CHECK(std::abs(normal.getPixel(0,0).b-0.35F)<1e-6F);
    CHECK(std::abs(normal.getPixel(0,0).a-0.45F)<1e-6F); CHECK(std::abs(geo.height(0,0)-0.35F)<1e-6F);
    ssq::VM vm(1024);auto table=vm.addTable("eve");exposeHeightmap(table);exposeTerrainImageAdapter(table);
    vm.addFunc("a0",[&](){return &a0;});vm.addFunc("n0",[&](){return &n0;});
    vm.run(vm.compileSource(R"(local s=eve.GtsPackedLayerSettings();local p=eve.GtsPackedLayerSet();assert(p.addLayer(a0(),n0(),s).ok&&p.getLayerCount()==1);)"));
}

TEST_CASE("procgen.gtsPackedLayers.matchesTriplanarAndStochasticSampling") {
    TerrainSplatmap splat;REQUIRE(splat.initialize(2,2,1,0).ok());Heightmap heights(2,2);
    heights.setHeight(0,0,0);heights.setHeight(1,0,2);heights.setHeight(0,1,0);heights.setHeight(1,1,2);
    eve::image::ImageData albedoTexture(2,2,"RGBA32F"),normalTexture(1,1,"RGBA32F");
    albedoTexture.setPixel(0,0,{0,0,0,0});albedoTexture.setPixel(1,0,{1,0,0,0.25F});
    albedoTexture.setPixel(0,1,{0,1,0,0.75F});albedoTexture.setPixel(1,1,{0,0,1,1});
    normalTexture.setPixel(0,0,{0.5F,0.5F,1,0.5F});
    auto bake=[&](GtsPackedLayerSettings settings) {
        GtsPackedLayerSet layers;REQUIRE(layers.addLayer(albedoTexture,normalTexture,settings).ok());
        eve::image::ImageData albedo(2,2,"RGBA32F"),normal(2,2,"RGBA32F");Heightmap geo(2,2),detail(2,2);
        REQUIRE(bakeGtsPackedLayers(albedo,normal,geo,detail,heights,splat,layers,3,5,7,1,1).ok());
        return albedo.getPixel(1,1);
    };
    GtsPackedLayerSettings planar;const auto plain=bake(planar);
    GtsPackedLayerSettings stochastic;stochastic.stochastic=true;const auto random=bake(stochastic);
    GtsPackedLayerSettings triplanar;triplanar.triPlanar=true;triplanar.triPlanarSizeX=0.4F;
    triplanar.triPlanarSizeZ=0.7F;const auto projected=bake(triplanar);
    CHECK(std::abs(plain.r-random.r)+std::abs(plain.g-random.g)+std::abs(plain.b-random.b)>1e-5F);
    CHECK(std::abs(plain.r-projected.r)+std::abs(plain.g-projected.g)+std::abs(plain.b-projected.b)>1e-5F);
}

TEST_CASE("procgen.gtsPackedLayers.bakesTopFourDisplacementWithCameraCutoff") {
    TerrainSplatmap splat; REQUIRE(splat.initialize(1,1,2,0).ok()); Heightmap blend(1,1); blend.setHeight(0,0,0.25F);
    REQUIRE(paintTerrainSplatLayer(splat,blend,1).ok());
    eve::image::ImageData a0(1,1,"RGBA32F"),a1(1,1,"RGBA32F"),n(1,1,"RGBA32F");
    a0.setPixel(0,0,{0,0,0,0.25F});a1.setPixel(0,0,{0,0,0,1});n.setPixel(0,0,{0.5F,0.5F,1,0});
    GtsPackedLayerSettings s0,s1;s0.displacementContrast=1;s0.displacementBrightness=2;s0.tessellationAmount=4;
    s1.displacementContrast=1;s1.displacementBrightness=2;s1.tessellationAmount=12;
    GtsPackedLayerSet layers;REQUIRE(layers.addLayer(a0,n,s0).ok());REQUIRE(layers.addLayer(a1,n,s1).ok());
    Heightmap heights(1,1),displacement(1,1),tessellation(1,1);
    auto result=bakeGtsPackedLayerDisplacement(displacement,tessellation,heights,splat,layers,0,0,0,2);
    REQUIRE(result.ok());CHECK(result.value()==2);CHECK(std::abs(displacement.height(0,0)-0.875F)<1e-6F);
    CHECK(std::abs(tessellation.height(0,0)-12.0F)<1e-6F);
    REQUIRE(bakeGtsPackedLayerDisplacement(displacement,tessellation,heights,splat,layers,400,0,0,2).ok());
    CHECK(displacement.height(0,0)==0);CHECK(tessellation.height(0,0)==0);
    const auto before=displacement.data();CHECK(!bakeGtsPackedLayerDisplacement(displacement,tessellation,heights,splat,layers,0,0,0,2,0,0,0,0,1).ok());
    CHECK(displacement.data()==before);
    ssq::VM vm(1024);auto table=vm.addTable("eve");exposeHeightmap(table);exposeTerrainImageAdapter(table);
    vm.addFunc("d",[&](){return &displacement;});vm.addFunc("t",[&](){return &tessellation;});
    vm.addFunc("h",[&](){return &heights;});vm.addFunc("splat",[&](){return &splat;});vm.addFunc("layers",[&](){return &layers;});
    vm.run(vm.compileSource(R"(assert(eve.bakeGtsPackedLayerDisplacement(d(),t(),h(),splat(),layers(),0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,1.0).ok);)"));
    CHECK(std::abs(displacement.height(0,0)-0.875F)<1e-6F);
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
    snow.enabled = true;
    snow.minimumHeight = 100;
    snow.blendRange = 20;
    GtsRainSurfaceSettings rain;
    rain.enabled = true;
    rain.maximumHeight = 3000;
    auto baked = bakeGtsWeatherAlbedo(output, heights, snowAlbedo, snowMask, snow, rain);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 4);
    CHECK(std::abs(output.getPixel(0, 0).r - 0.41F) < 1e-6F);
    CHECK(std::abs(output.getPixel(0, 0).g - 0.533F) < 1e-6F);
    CHECK(std::abs(output.getPixel(0, 0).b - 0.451F) < 1e-6F);
    CHECK(output.getPixel(0, 0).a == 1);

    const auto preserved = output.getPixel(0, 0);
    snow.scale = 0;
    CHECK(!bakeGtsWeatherAlbedo(output, heights, snowAlbedo, snowMask, snow, rain).ok());
    CHECK(output.getPixel(0, 0).r == preserved.r);
}

TEST_CASE("procgen.gtsColorVariation.matchesGtsShaderOrderAndMath") {
    eve::image::ImageData albedo(1, 1, "RGBA32F"), colorMap(1, 1, "RGBA32F"), variation(1, 1, "RGBA32F");
    Heightmap blend(1, 1);
    albedo.setPixel(0, 0, {0.2F, 0.4F, 0.6F, 1});
    colorMap.setPixel(0, 0, {0.8F, 0.2F, 0.4F, 0.5F});
    variation.setPixel(0, 0, {0.25F, 0, 0, 1});
    blend.setHeight(0, 0, 0.5F);
    GtsColorMapSettings color; color.alphaIntensity = 2; color.colorIntensity = 0.5F;
    color.nearIntensity = 0.25F; color.farIntensity = 0.75F;
    REQUIRE(bakeGtsColorMapAlbedo(albedo, colorMap, blend, color).ok());
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).g - 0.25F) < 1e-6F);
    GtsMacroVariationSettings macro; macro.intensity = 0.5F;
    REQUIRE(bakeGtsMacroVariationAlbedo(albedo, variation, macro).ok());
    const float multiplier = 0.5F + 0.5F * (0.75F * 0.75F * 0.75F);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3F * multiplier) < 1e-6F);
    CHECK(albedo.getPixel(0, 0).a == 1);
}

TEST_CASE("procgen.gtsGlobalBlend.matchesCameraSquaredDistanceAndBinding") {
    Heightmap heights(2, 1), output(2, 1);
    heights.setHeight(0, 0, 0); heights.setHeight(1, 0, 0);
    auto generated = generateGtsGlobalBlendDistance(output, heights, 0, 0, 0, 100, 2, 0, 0, 0, 5, 1);
    REQUIRE(generated.ok()); CHECK(generated.value() == 1);
    CHECK(output.height(0, 0) == 0); CHECK(std::abs(output.height(1, 0) - 0.0625F) < 1e-6F);
    const auto before = output.data();
    CHECK(!generateGtsGlobalBlendDistance(output, heights, 0, 0, 0, -1, 1).ok());
    CHECK(output.data() == before);
    ssq::VM vm(1024); auto table = vm.addTable("eve"); exposeHeightmap(table); exposeTerrainImageAdapter(table);
    vm.addFunc("heights", [&]() { return &heights; }); vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local r=eve.generateGtsGlobalBlendDistance(output(),heights(),0.0,0.0,0.0,100.0,2.0,0.0,0.0,0.0,5.0,1.0);
        assert(r.ok && output().height(1,0)>0.0624 && output().height(1,0)<0.0626);
    )"));
}

TEST_CASE("procgen.gtsColorVariation.failureIsAtomicAndBindingsRun") {
    eve::image::ImageData albedo(1, 1, "RGBA32F"), colorMap(1, 1, "RGBA32F"), variation(1, 1, "RGBA32F");
    Heightmap blend(1, 1); albedo.setPixel(0, 0, {0.4F, 0.4F, 0.4F, 1});
    colorMap.setPixel(0, 0, {1, 0, 0, 1}); variation.setPixel(0, 0, {0.5F, 0, 0, 1});
    blend.setHeight(0, 0, 2);
    GtsColorMapSettings invalid;
    CHECK(!bakeGtsColorMapAlbedo(albedo, colorMap, blend, invalid).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.4F);
    blend.setHeight(0, 0, 0.5F);
    ssq::VM vm(1024); auto table = vm.addTable("eve"); exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; }); vm.addFunc("map", [&]() { return &colorMap; });
    vm.addFunc("variation", [&]() { return &variation; }); vm.addFunc("blend", [&]() { return &blend; });
    vm.run(vm.compileSource(R"(
        local c=eve.GtsColorMapSettings();c.nearIntensity=0.25;c.farIntensity=0.75;
        assert(eve.bakeGtsColorMapAlbedo(albedo(),map(),blend(),c).ok);
        local m=eve.GtsMacroVariationSettings();m.sizeA=10.0;m.sizeB=20.0;m.sizeC=30.0;m.intensity=0.5;m.objectSpace=true;
        assert(eve.bakeGtsMacroVariationAlbedo(albedo(),variation(),m,5.0,7.0,1.0,1.0).ok);
    )"));
}

TEST_CASE("procgen.gtsGeological.blendsHeightStripColorAndPackedNormal") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1);
    heights.setHeight(0, 0, 0); strength.setHeight(0, 0, 0.5F); distance.setHeight(0, 0, 0.25F);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F");
    eve::image::ImageData geoAlbedo(1, 1, "RGBA32F"), geoNormal(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.3F, 0.4F, 1}); packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    geoAlbedo.setPixel(0, 0, {0.5F, 0.25F, 0.75F, 1}); geoNormal.setPixel(0, 0, {0, 0.75F, 0, 0.625F});
    GtsGeologicalSettings geo; geo.enabled = true; geo.nearStrength = 0.2F; geo.farStrength = 0.4F;
    geo.nearNormalStrength = 0.4F; geo.farNormalStrength = 0.8F;
    auto result = bakeGtsGeologicalSurface(albedo, packed, heights, strength, distance, geoAlbedo, geoNormal, geo);
    REQUIRE(result.ok()); CHECK(result.value() == 2);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.3225F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).g - 0.335F) < 1e-6F);
    CHECK(packed.getPixel(0, 0).r > 0.53F); CHECK(packed.getPixel(0, 0).g > 0.57F);
    CHECK(packed.getPixel(0, 0).b == 0.7F); CHECK(packed.getPixel(0, 0).a == 0.8F);
}

TEST_CASE("procgen.gtsGeological.failureIsAtomicAndBindingRuns") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1);
    strength.setHeight(0, 0, 2); distance.setHeight(0, 0, 0);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F");
    eve::image::ImageData geoAlbedo(1, 1, "RGBA32F"), geoNormal(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.3F, 0.4F, 1}); packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    geoAlbedo.setPixel(0, 0, {0.5F, 0.5F, 0.5F, 1}); geoNormal.setPixel(0, 0, {0, 0.5F, 0, 0.5F});
    GtsGeologicalSettings geo; geo.enabled = true;
    CHECK(!bakeGtsGeologicalSurface(albedo, packed, heights, strength, distance, geoAlbedo, geoNormal, geo).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.2F); CHECK(packed.getPixel(0, 0).b == 0.7F);
    strength.setHeight(0, 0, 1);
    ssq::VM vm(1024); auto table = vm.addTable("eve"); exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; }); vm.addFunc("packed", [&]() { return &packed; });
    vm.addFunc("heights", [&]() { return &heights; }); vm.addFunc("strength", [&]() { return &strength; });
    vm.addFunc("distance", [&]() { return &distance; }); vm.addFunc("geoAlbedo", [&]() { return &geoAlbedo; });
    vm.addFunc("geoNormal", [&]() { return &geoNormal; });
    vm.run(vm.compileSource(R"(
        local g=eve.GtsGeologicalSettings();g.enabled=true;g.objectSpace=true;g.nearScale=50.0;g.farScale=200.0;
        assert(eve.bakeGtsGeologicalSurface(albedo(),packed(),heights(),strength(),distance(),geoAlbedo(),geoNormal(),g,0.0).ok);
    )"));
}

TEST_CASE("procgen.gtsDetail.matchesNearFarNormalAndMicroShadow") {
    Heightmap strength(1, 1), distance(1, 1), greyscale(1, 1);
    strength.setHeight(0, 0, 0.5F); distance.setHeight(0, 0, 0.25F);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F"), detail(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.8F, 0.4F, 0.2F, 1}); packed.setPixel(0, 0, {0.5F, 0.5F, 0.7F, 0.8F});
    detail.setPixel(0, 0, {0.75F, 0.25F, 0, 1});
    GtsDetailNormalSettings settings; settings.nearStrength = 0.4F; settings.farStrength = 0.8F;
    auto result = bakeGtsDetailSurface(albedo, packed, greyscale, strength, distance, detail, settings);
    REQUIRE(result.ok()); CHECK(result.value() == 3);
    CHECK(std::abs(greyscale.height(0, 0) - 0.125F) < 1e-6F);
    CHECK(std::abs(albedo.getPixel(0, 0).r - 0.775F) < 1e-6F);
    CHECK(std::abs(packed.getPixel(0, 0).r - 0.5625F) < 1e-6F);
    CHECK(std::abs(packed.getPixel(0, 0).g - 0.4375F) < 1e-6F);
    CHECK(packed.getPixel(0, 0).b == 0.7F); CHECK(packed.getPixel(0, 0).a == 0.8F);
}

TEST_CASE("procgen.gtsDetail.bindingFeedsSnowDetailAndFailureIsAtomic") {
    Heightmap heights(1, 1), strength(1, 1), distance(1, 1), greyscale(1, 1);
    heights.setHeight(0, 0, 100); strength.setHeight(0, 0, 1); distance.setHeight(0, 0, 0);
    eve::image::ImageData albedo(1, 1, "RGBA32F"), packed(1, 1, "RGBA32F"), detail(1, 1, "RGBA32F");
    eve::image::ImageData snowAlbedo(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F");
    albedo.setPixel(0, 0, {0.2F, 0.2F, 0.2F, 1}); packed.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    detail.setPixel(0, 0, {1, 0.5F, 0, 1}); snowAlbedo.setPixel(0, 0, {1, 1, 1, 1});
    snowMask.setPixel(0, 0, {1, 1, 1, 1});
    GtsDetailNormalSettings bad; bad.nearTiling = 0;
    CHECK(!bakeGtsDetailSurface(albedo, packed, greyscale, strength, distance, detail, bad).ok());
    CHECK(albedo.getPixel(0, 0).r == 0.2F); CHECK(greyscale.height(0, 0) == 0);
    ssq::VM vm(1024); auto table = vm.addTable("eve"); exposeTerrainImageAdapter(table);
    vm.addFunc("albedo", [&]() { return &albedo; }); vm.addFunc("packed", [&]() { return &packed; });
    vm.addFunc("greyscale", [&]() { return &greyscale; }); vm.addFunc("strength", [&]() { return &strength; });
    vm.addFunc("distance", [&]() { return &distance; }); vm.addFunc("detail", [&]() { return &detail; });
    vm.addFunc("heights", [&]() { return &heights; }); vm.addFunc("snowAlbedo", [&]() { return &snowAlbedo; });
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
    auto table = vm.addTable("eve");
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
    snow.enabled = true; snow.minimumHeight = 100; snow.blendRange = 20;
    GtsRainSurfaceSettings rain;
    rain.enabled = true; rain.maximumHeight = 3000;
    auto baked = bakeGtsWeatherPbr(normal, mask, displacement, tessellation, heights, snowNormal, snowMask,
                                   rainData, snow, rain, 0);
    REQUIRE(baked.ok());
    CHECK(baked.value() == 4);
    CHECK(std::abs(mask.getPixel(0, 0).r - 0.1F) < 1e-6F);
    CHECK(std::abs(mask.getPixel(0, 0).b - 0.75F) < 1e-6F);
    CHECK(std::abs(mask.getPixel(0, 0).a - 0.75F) < 1e-6F);
    CHECK(std::abs(displacement.height(0, 0) - 0.35F) < 1e-6F);
    CHECK(std::abs(tessellation.height(0, 0) - 12.5F) < 1e-6F);
    const auto before = mask.getPixel(0, 0);
    CHECK(!bakeGtsWeatherPbr(normal, mask, displacement, tessellation, heights, snowNormal, snowMask, rainData,
                             snow, rain, std::numeric_limits<double>::infinity()).ok());
    CHECK(mask.getPixel(0, 0).a == before.a);
    CHECK(displacement.height(0, 0) == 0.35F);
}

TEST_CASE("procgen.gtsWeather.exposesPbrBakeToRealVm") {
    Heightmap heights(1, 1), displacement(1, 1), tessellation(1, 1);
    heights.setHeight(0, 0, 100);
    eve::image::ImageData normal(1, 1, "RGBA32F"), mask(1, 1, "RGBA32F"), snowNormal(1, 1, "RGBA32F");
    eve::image::ImageData snowMask(1, 1, "RGBA32F"), rainData(1, 1, "RGBA32F");
    normal.setPixel(0, 0, {0.5F, 0.5F, 1, 1}); mask.setPixel(0, 0, {0, 0, 0, 0});
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1});
    snowMask.setPixel(0, 0, {0.2F, 0.4F, 0.5F, 0.6F}); rainData.setPixel(0, 0, {0, 0, 0, 0});
    ssq::VM vm(1024); auto table = vm.addTable("eve"); exposeTerrainImageAdapter(table);
    vm.addFunc("normal", [&]() { return &normal; }); vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("displacement", [&]() { return &displacement; }); vm.addFunc("tessellation", [&]() { return &tessellation; });
    vm.addFunc("heights", [&]() { return &heights; }); vm.addFunc("snowNormal", [&]() { return &snowNormal; });
    vm.addFunc("snowMask", [&]() { return &snowMask; }); vm.addFunc("rainData", [&]() { return &rainData; });
    vm.run(vm.compileSource(R"(
        local snow=eve.GtsSnowSurfaceSettings();snow.enabled=true;snow.minimumHeight=100.0;snow.blendRange=20.0;
        snow.setMaskRemapMin(0.0,0.0,0.0,0.0);snow.setMaskRemapMax(1.0,1.0,3.0,1.0);
        local rain=eve.GtsRainSurfaceSettings();rain.enabled=true;rain.maximumHeight=3000.0;
        local r=eve.bakeGtsWeatherPbr(normal(),mask(),displacement(),tessellation(),heights(),snowNormal(),snowMask(),rainData(),snow,rain,0.0,0.0,0.0,1.0,1.0);
        assert(r.ok);
    )"));
    CHECK(std::abs(mask.getPixel(0, 0).a - 0.75F) < 1e-6F);
}

TEST_CASE("procgen.gtsWeather.rainNormalsUseExplicitDeterministicTime") {
    Heightmap heights(2, 2), displacementA(2, 2), tessellationA(2, 2), displacementB(2, 2), tessellationB(2, 2);
    eve::image::ImageData normalA(2, 2, "RGBA32F"), normalB(2, 2, "RGBA32F");
    eve::image::ImageData maskA(2, 2, "RGBA32F"), maskB(2, 2, "RGBA32F");
    eve::image::ImageData snowNormal(1, 1, "RGBA32F"), snowMask(1, 1, "RGBA32F"), rainData(2, 2, "RGBA32F");
    snowNormal.setPixel(0, 0, {0.5F, 0.5F, 1, 1}); snowMask.setPixel(0, 0, {0, 0, 0, 0});
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        heights.setHeight(x, y, 100); normalA.setPixel(x, y, {0.5F, 0.5F, 1, 1});
        normalB.setPixel(x, y, {0.5F, 0.5F, 1, 1}); maskA.setPixel(x, y, {0, 0, 0, 0.2F});
        maskB.setPixel(x, y, {0, 0, 0, 0.2F});
        const float phase = 0.1F + 0.2F * x + 0.3F * y;
        rainData.setPixel(x, y, {phase, phase * 0.7F, phase * 0.4F, phase * 0.2F});
    }
    GtsSnowSurfaceSettings snow;
    GtsRainSurfaceSettings rain; rain.enabled = true; rain.maximumHeight = 3000; rain.scale = 3;
    REQUIRE(bakeGtsWeatherPbr(normalA, maskA, displacementA, tessellationA, heights, snowNormal, snowMask,
                              rainData, snow, rain, 0.125).ok());
    REQUIRE(bakeGtsWeatherPbr(normalB, maskB, displacementB, tessellationB, heights, snowNormal, snowMask,
                              rainData, snow, rain, 0.625).ok());
    const auto a = normalA.getPixel(0, 0), b = normalB.getPixel(0, 0);
    CHECK(std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) > 1e-5F);
}

TEST_CASE("procgen.pcgMaskMapExport.combinesRedChannelsAndRejectsAtomically") {
    eve::image::ImageData output(2, 1, "RGBA32F"), red(2, 1, "RGBA32F");
    eve::image::ImageData green(2, 1, "RGBA32F"), blue(2, 1, "RGBA32F"), alpha(2, 1, "RGBA32F");
    output.setPixel(0, 0, {0.9F, 0.8F, 0.7F, 0.6F});
    output.setPixel(1, 0, {0.6F, 0.7F, 0.8F, 0.9F});
    red.setPixel(0, 0, {0.1F, 0.9F, 0.9F, 0.9F}); red.setPixel(1, 0, {1.4F, 0, 0, 0});
    green.setPixel(0, 0, {0.2F, 0.8F, 0.8F, 0.8F}); green.setPixel(1, 0, {0.3F, 0, 0, 0});
    blue.setPixel(0, 0, {0.4F, 0.7F, 0.7F, 0.7F}); blue.setPixel(1, 0, {0.5F, 0, 0, 0});
    alpha.setPixel(0, 0, {0.6F, 0.6F, 0.6F, 0.6F}); alpha.setPixel(1, 0, {-0.2F, 0, 0, 0});
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
    const auto before = output.getPixel(0, 0);
    eve::image::ImageData wrong(1, 1, "RGBA32F");
    CHECK(!combinePcgMaskMapChannels(output, wrong, green, blue, alpha, 15).ok());
    const auto after = output.getPixel(0, 0);
    CHECK(before.r == after.r); CHECK(before.g == after.g); CHECK(before.b == after.b); CHECK(before.a == after.a);

    eve::image::ImageData atlas(4, 2, "RGBA32F");
    auto placed = placePcgMaskMapTileInto(atlas, output, 1, 0, 2, 2);
    REQUIRE(placed.ok());
    CHECK_EQ(placed.value(), 4);
    CHECK(std::abs(atlas.getPixel(1, 0).r - output.getPixel(0, 0).r) < 1e-6F);
    CHECK(std::abs(atlas.getPixel(2, 1).g - output.getPixel(1, 0).g) < 1e-6F);
    const auto atlasBefore = atlas.getPixel(1, 0);
    CHECK(!placePcgMaskMapTileInto(atlas, output, 3, 0, 2, 2).ok());
    CHECK(atlas.getPixel(1, 0).r == atlasBefore.r);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
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
