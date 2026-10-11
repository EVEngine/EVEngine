#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("procgen.mask.scriptWorldContextConfiguration") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.run(vm.compileSource(R"(
        local world=eve.TerrainStampSettings();world.setGrid(0.0,0.0,1.0,1.0);
        world.setCenter(1.0,1.0);world.setSize(2.0,2.0);
        local grid=eve.TerrainSampleGrid();grid.width=3;grid.height=3;
        grid.setOrigin(0.0,0.0);grid.setSpacing(1.0,1.0);
        local spatial=eve.TerrainBrushBlendSettings();spatial.strength=0.25;
        assert(spatial.configureWorld(world,3,3,grid).ok);
        assert(spatial.heightXX==1.0 && spatial.heightZZ==1.0 && spatial.heightOffsetX==0.0);
        assert(spatial.brushXX==1.5 && spatial.brushOffsetX==-0.25 && spatial.strength==0.25);
        grid.setSpacing(0.0,1.0);
        assert(!spatial.configureWorld(world,3,3,grid).ok);
        assert(spatial.brushXX==1.5);
    )"));
}

TEST_CASE("procgen.mask.scriptImageMaskChannelsAndSettings") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), channel(1, 1), curve(2, 1);
    target.data()  = {1};
    channel.data() = {0.5F};
    curve.data()   = {0, 1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("channel", [&]() { return &channel; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainImageMaskSettings();s.tiling=true;s.rotation=0.0;s.accuracy=0.5;
        assert(target().generateImageMask(target(),channel(),channel(),channel(),channel(),curve(),s,5,0).ok);
        assert(target().height(0,0)==0.5);
        assert(!target().generateImageMask(target(),channel(),channel(),channel(),channel(),curve(),s,99,0).ok);
    )"));
    CHECK(target.data()[0] == 0.5F);
}

TEST_CASE("procgen.mask.scriptGlobalSpawnerMask") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), input(1, 1), source(1, 1), curve(1, 1);
    input.data()[0] = 0.8F;
    source.data()[0] = curve.data()[0] = 0.5F;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local settings=eve.TerrainImageMaskSettings();
        assert(target().applyGlobalSpawnerMask(input(),source(),curve(),settings,0).ok);
        assert(target().height(0,0)==0.4);
    )"));
}

TEST_CASE("procgen.mask.scriptNoiseMask") {
    using eve::procgen::Heightmap;
    Heightmap target(3, 3), input(3, 3), curve(1, 1);
    std::fill(input.data().begin(), input.data().end(), 1.0F);
    curve.data()[0] = 0.75F;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local settings=eve.TerrainNoiseMaskSettings();
        settings.octaves=2.5;settings.warpIterations=1.5;settings.seed=99;
        assert(target().generateNoiseMask(input(),curve(),settings,0,0).ok);
        assert(target().height(1,1)==0.75);
        assert(!target().generateNoiseMask(input(),curve(),settings,99,0).ok);
    )"));
}

TEST_CASE("procgen.mask.scriptCollisionMaskStack") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), input(1, 1), mask(1, 1), curve(2, 1);
    input.data()[0] = 0.75F;
    mask.data()[0] = 1;
    curve.data() = {0, 1};
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local stack=eve.TerrainCollisionMaskStack();
        assert(stack.addLayer(mask(),2,true,false).value==1);
        assert(stack.getLayerCount()==1);
        assert(stack.apply(target(),input(),curve(),0).ok && target().height(0,0)==0.0);
        stack.clear();assert(stack.getLayerCount()==0);
    )"));
}

TEST_CASE("procgen.mask.scriptPolygonMask") {
    using eve::procgen::Heightmap;
    Heightmap output(5, 5), brush(1, 1);
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("brush", [&]() { return &brush; });
    vm.run(vm.compileSource(R"(
        local poly = eve.TerrainPolygonMask();
        assert(poly.addNode(1.0, 1.0, 0.5, 1.0).ok);
        assert(poly.addNode(3.0, 1.0, 0.5, 1.0).ok);
        assert(poly.addNode(3.0, 3.0, 0.5, 1.0).ok);
        assert(poly.rasterize(output(), brush(), 0.0, 0.0, 1.0, 1.0, 1).ok);
        assert(output().height(2, 2) == 1.0);
        assert(poly.getNodeCount() == 3);
        poly.clear();
        assert(poly.getNodeCount() == 0);
    )"));
    CHECK(output.data()[12] == 1.0F);
}

TEST_CASE("procgen.mask.scriptWorldBiomeBakedMaskCache") {
    using eve::procgen::Heightmap;
    Heightmap source(1, 1), output(1, 1);
    source.data()[0] = 0.625F;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local cache=eve.TerrainBakedMaskCache();
        assert(cache.store("terrain-0","biome-a",source()).value==1);
        assert(cache.copyMask("terrain-0","biome-a",output()).ok && output().height(0,0)==0.625);
        assert(cache.markDirty("biome-a").value==1);
        assert(!cache.copyMask("terrain-0","biome-a",output()).ok);
        assert(cache.store("terrain-0","biome-a",source()).value==1);
        assert(cache.erase("terrain-0","biome-a").value==1 && cache.getEntryCount()==0);
        cache.clear();
    )"));
}

TEST_CASE("procgen.mask.scriptConcavityControls") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), heights(1, 1), curve(1, 1);
    target.data() = {0.25F};
    curve.data()  = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainConcavitySettings();s.featureSize=1.0;s.concavity=-1.0;
        assert(target().generateConcavityMask(target(),heights(),curve(),s,3).ok);
        assert(target().height(0,0)==0.75);
        s.featureSize=0.0;
        assert(!target().generateConcavityMask(target(),heights(),curve(),s,0).ok);
    )"));
    CHECK(target.data()[0] == 0.75F);
}

TEST_CASE("procgen.mask.scriptCurvatureSettingsAndModes") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), heights(1, 1), curve(1, 1);
    target.data() = {0.25F};
    curve.data()  = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainCurvatureSettings();
        s.steps=1; s.directions=4; s.radius=0.1; s.worldUnits=1.0; s.intensity=1.0;
        assert(target().generateCurvatureMask(target(),heights(),curve(),s,2).ok);
        assert(target().height(0,0)==0.5);
        s.steps=0;
        assert(!target().generateCurvatureMask(target(),heights(),curve(),s,0).ok);
    )"));
    CHECK(target.data()[0] == 0.5F);
}

TEST_CASE("procgen.mask.scriptGrowShrinkCurveEvenAtZero") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), curve(1, 1);
    target.data() = {1};
    curve.data()  = {0.25F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        assert(target().growShrinkMask(target(),curve(),0.0).ok);
        assert(target().height(0,0)==0.25);
        assert(target().growShrinkMask(target(),curve(),-0.25).ok);
    )"));
    CHECK(target.data()[0] == 0.25F);
}

TEST_CASE("procgen.mask.scriptErosionStrengthWorkflow") {
    using eve::procgen::Heightmap;
    Heightmap target(1, 1), oldHeights(1, 1), erosion(1, 1), brush(1, 1), curve(1, 1);
    oldHeights.data() = {0.25F};
    erosion.data()    = {0.75F};
    brush.data()      = {1};
    curve.data()      = {0.25F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("oldHeights", [&]() { return &oldHeights; });
    vm.addFunc("erosion", [&]() { return &erosion; });
    vm.addFunc("brush", [&]() { return &brush; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainBrushBlendSettings();
        assert(target().generateErosionMask(oldHeights(),erosion(),brush(),s,curve(),0,1.0,false).ok);
        assert(target().height(0,0)==0.75);
        assert(target().applyStrength(target(),curve(),3,1.0,false).ok);
        assert(target().height(0,0)==1.0);
        assert(!target().generateErosionMask(oldHeights(),erosion(),brush(),s,curve(),99,1.0,false).ok);
        assert(target().height(0,0)==1.0);
    )"));
    CHECK(target.data()[0] == 1);
}

TEST_CASE("procgen.mask.scriptBrushSpatialBlend") {
    using eve::procgen::Heightmap;
    Heightmap target(2, 1), source(1, 1), brush(1, 1);
    target.data() = {2, 4};
    source.data() = {10};
    brush.data()  = {0.5F};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("brush", [&]() { return &brush; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainBrushBlendSettings();
        s.brushXX=2.0;
        assert(target().blendBrush(target(),source(),brush(),s).ok);
        assert(target().height(0,0)==6.0 && target().height(1,0)==4.0);
    )"));
    CHECK(target.data() == std::vector<float>({6, 4}));
}

TEST_CASE("procgen.stamp.scriptBindingsAndFailures") {
    using eve::procgen::Heightmap;
    Heightmap terrain(3, 3), stamp(1, 1), local(1, 1), global(1, 1);
    stamp.setHeight(0, 0, 4);
    local.setHeight(0, 0, 0.5F);
    global.setHeight(0, 0, 1);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("getTerrain", [&terrain]() { return &terrain; });
    vm.addFunc("getStamp", [&stamp]() { return &stamp; });
    vm.addFunc("getLocal", [&local]() { return &local; });
    vm.addFunc("getGlobal", [&global]() { return &global; });
    vm.run(vm.compileSource(R"(
        local target = getTerrain();
        local s = eve.TerrainStampSettings();
        s.setCenter(1.0, 1.0); s.setSize(2.0, 2.0);
        s.baseHeight = 1.0;
        local r = target.applyStamp(getStamp(), s, 3, getLocal(), getGlobal());
        assert(r.ok && r.value == 9);
        assert(target.height(1, 1) == 3);
        local blended = getGlobal().blendMask(getLocal(), 0, 1.0, false);
        assert(blended.ok && blended.value == 1);
        s.setSize(0.0, 2.0);
        local rejected = target.applyStamp(getStamp(), s, 3, getLocal(), getGlobal());
        assert(!rejected.ok && target.height(1, 1) == 3);
        local badMode = getGlobal().blendMask(getLocal(), 999, 1.0, false);
        assert(!badMode.ok);
    )"));
    CHECK(terrain.height(1, 1) == 3);
    CHECK(global.height(0, 0) == 0.5F);
}

TEST_CASE("procgen.mask.scriptTerrainMaskStampComposition") {
    using eve::procgen::Heightmap;
    Heightmap heights(3, 3), mask(3, 3), distance(3, 3), target(3, 3);
    Heightmap curve(2, 1), reverse(2, 1), stamp(1, 1), one(1, 1);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) heights.setHeight(x, y, float(x));
    curve.setHeight(1, 0, 1);
    reverse.setHeight(0, 0, 1);
    stamp.setHeight(0, 0, 2);
    one.setHeight(0, 0, 1);
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("distance", [&]() { return &distance; });
    vm.addFunc("target", [&]() { return &target; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.addFunc("reverse", [&]() { return &reverse; });
    vm.addFunc("stamp", [&]() { return &stamp; });
    vm.addFunc("one", [&]() { return &one; });
    vm.run(vm.compileSource(R"(
        local slopes = mask().deriveSlope(heights(), 1.0, 1.0, 1.0);
        assert(slopes.ok && mask().height(1, 1) > 0.29 && mask().height(1, 1) < 0.30);
        local ranged = mask().rangeMask(heights(), 0.0, 2.0, curve(), curve());
        assert(ranged.ok && mask().height(1, 1) == 0.5);
        assert(mask().transformMask(mask(), curve()).ok);
        local d = eve.TerrainDistanceMaskSettings();
        d.scaleX = 1.0; d.scaleZ = 1.0; d.tiling = false;
        assert(distance().distanceMask(d, 0, reverse(), curve()).ok);
        assert(mask().blendMask(distance(), 0, 1.0, false).ok);
        local s = eve.TerrainStampSettings();
        s.setCenter(1.0, 1.0); s.setSize(2.0, 2.0);
        assert(target().applyStamp(stamp(), s, 3, mask(), one()).ok);
        assert(target().height(1, 1) == 1.0 && target().height(0, 0) == 0.0);
        assert(!distance().distanceMask(d, 999, curve(), curve()).ok);
    )"));
    CHECK(target.height(1, 1) == 1);
}

TEST_CASE("procgen.mask.scriptSmoothMask") {
    using eve::procgen::Heightmap;
    Heightmap input(15, 15), output(15, 15);
    input.setHeight(7, 7, 1);
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("input", [&]() { return &input; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local r=output().smoothMask(input(),0.0,1.0);
        assert(r.ok && output().height(7,7)>0.017 && output().height(7,7)<0.018);
        local before=output().height(7,7);
        assert(!output().smoothMask(input(),2.0,1.0).ok);
        assert(output().height(7,7)==before);
    )"));
}

TEST_CASE("procgen.effect.scriptAllEffectsAndFailures") {
    using eve::procgen::Heightmap;
    Heightmap terrain(3, 1), mask(3, 1), curve(2, 1);
    terrain.data() = {0.125F, 0.25F, 0.375F};
    mask.data()    = {1, 1, 1};
    curve.data()   = {0, 1};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::procgen::exposeHeightmap(table);
    vm.addFunc("terrain", [&]() { return &terrain; });
    vm.addFunc("mask", [&]() { return &mask; });
    vm.addFunc("curve", [&]() { return &curve; });
    vm.run(vm.compileSource(R"(
        local t = terrain();
        assert(t.applyContrast(mask(),2.0,1.0).ok);
        local smooth = eve.TerrainSmoothSettings();
        smooth.radius = 0.5; smooth.verticality = 0.0; smooth.strength = 0.5;
        assert(t.applySmooth(mask(),smooth).ok);
        local ridge = eve.TerrainRidgeSettings();
        assert(ridge.passes == 18);
        ridge.passes = 2; ridge.minimum = 0.0; ridge.maximum = 1.0;
        ridge.mixStrength = 0.5; ridge.exponent = 2.0; ridge.strength = 1.0;
        assert(t.applyRidges(mask(),ridge).ok);
        local terrace = eve.TerrainTerraceSettings();
        terrace.count = 4.0; terrace.bevel = 0.0; terrace.strength = 1.0;
        assert(t.applyTerrace(mask(),terrace).ok);
        assert(t.applyPower(mask(),2.0).ok);
        assert(t.applyHeightCurve(mask(),curve(),0.0,1.0).ok);
        local mix = eve.TerrainHeightMixSettings();
        mix.minimum = 0.0; mix.maximum = 1.0; mix.midpoint = 0.5;
        mix.strength = 2.0; mix.clipMinimum = 0.0; mix.clipMaximum = 0.5;
        assert(t.applyHeightMix(mask(),mask(),mix).ok);
        assert(t.height(0,0) == 0.5 && t.height(2,0) == 0.5);
        terrace.count = 0.0;
        assert(!t.applyTerrace(mask(),terrace).ok);
        ridge.passes = -1;
        assert(!t.applyRidges(mask(),ridge).ok);
        smooth.verticality = 2.0;
        assert(!t.applySmooth(mask(),smooth).ok);
        assert(!t.applyHeightCurve(mask(),curve(),1.0,0.0).ok);
        mix.clipMaximum = -1.0;
        assert(!t.applyHeightMix(mask(),mask(),mix).ok);
        t.setHeight(2,0,-1.0);
        assert(!t.applyPower(mask(),2.0).ok);
        assert(t.height(0,0) == 0.5 && t.height(2,0) == -1.0);
    )"));
    CHECK(terrain.data() == std::vector<float>({0.5F, 0.5F, -1}));
}
