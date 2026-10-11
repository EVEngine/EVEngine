#include <algorithm>
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMask.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainWaterField.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.waterFlow.tracesStrictLowestAndTransposes") {
    Heightmap source(3, 3), output(3, 3);
    source.data() = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    output.data().assign(9, 0.7F);
    TerrainWaterFlowMapSettings settings;
    settings.dropletVolume = 0.11F;
    settings.absorptionRate = 0.05F;
    settings.smoothIterations = 0;
    REQUIRE(generateTerrainWaterFlowMap(output, source, settings).ok());
    CHECK(output.data() == std::vector<float>({0.05F, 0.05F, 0, 0, 0.05F, 0, 0, 0, 0}));
    CHECK(source.data() == std::vector<float>({0, 0, 0, 0, 1, 0, 0, 0, 0}));
}

TEST_CASE("procgen.waterFlow.rectangularFlipSmoothAndAtomicFailure") {
    Heightmap source(4, 3), output(3, 4);
    source.data() = {0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0};
    TerrainWaterFlowMapSettings settings;
    settings.dropletVolume = settings.absorptionRate = 0.2F;
    settings.smoothIterations = 1;
    REQUIRE(generateTerrainWaterFlowMap(output, source, settings).ok());
    CHECK(output.getWidth() == 3);
    CHECK(output.getHeight() == 4);
    for (float value : output.data()) {
        CHECK(value >= 0.f);
        CHECK(value <= 1.f);
    }
    const auto original = output.data();
    settings.absorptionRate = 0.f;
    CHECK(!generateTerrainWaterFlowMap(output, source, settings).ok());
    CHECK(output.data() == original);
    Heightmap wrong(4, 3);
    settings.absorptionRate = 0.2F;
    CHECK(!generateTerrainWaterFlowMap(wrong, source, settings).ok());
    CHECK(!generateTerrainWaterFlowMap(source, source, settings).ok());
}

TEST_CASE("procgen.waterFlow.scriptBinding") {
    Heightmap source(3, 3), output(3, 3);
    source.data()[4] = 1.f;
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local s=eve.TerrainWaterFlowMapSettings();
        s.dropletVolume=0.11;s.absorptionRate=0.05;s.smoothIterations=0;
        assert(eve.generateTerrainWaterFlowMap(output(),source(),s).ok);
        s.absorptionRate=0.0;
        assert(!eve.generateTerrainWaterFlowMap(output(),source(),s).ok);
    )"));
    CHECK(output.data()[4] == 0.05F);
}

TEST_CASE("procgen.velocityFlow.normalizesAndIsDeterministic") {
    Heightmap source(5, 5), first(5, 5), second(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) source.setHeight(x, y, 0.1F * float(x) + 0.03F * float(y * y));
    REQUIRE(generateTerrainVelocityFlowMap(first, source, 4).ok());
    REQUIRE(generateTerrainVelocityFlowMap(second, source, 4).ok());
    CHECK(first.data() == second.data());
    CHECK(*std::min_element(first.data().begin(), first.data().end()) == 0.F);
    CHECK(*std::max_element(first.data().begin(), first.data().end()) == 1.F);
    for (float value : first.data()) {
        CHECK(value >= 0.F);
        CHECK(value <= 1.F);
    }
}

TEST_CASE("procgen.velocityFlow.flatZeroAliasAndFailure") {
    Heightmap flat(4, 3), output(4, 3);
    flat.data().assign(12, 0.25F);
    REQUIRE(generateTerrainVelocityFlowMap(output, flat, 8).ok());
    CHECK(output.data() == std::vector<float>(12, 0.F));
    Heightmap aliased(3, 3);
    for (int i = 0; i < 9; ++i) aliased.data()[size_t(i)] = float(i) * 0.1F;
    REQUIRE(generateTerrainVelocityFlowMap(aliased, aliased, 2).ok());
    const auto before = aliased.data();
    CHECK(!generateTerrainVelocityFlowMap(aliased, aliased, -1).ok());
    CHECK(aliased.data() == before);
    Heightmap wrong(2, 2);
    CHECK(!generateTerrainVelocityFlowMap(wrong, flat, 1).ok());
}

TEST_CASE("procgen.velocityFlow.scriptBinding") {
    Heightmap source(3, 3), output(3, 3);
    source.data() = {0.F, 0.1F, 0.2F, 0.F, 0.2F, 0.4F, 0.F, 0.3F, 0.6F};
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("sourceVelocityFlow", [&]() { return &source; });
    vm.addFunc("outputVelocityFlow", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(eve.generateTerrainVelocityFlowMap(outputVelocityFlow(),sourceVelocityFlow(),3).ok);
        assert(!eve.generateTerrainVelocityFlowMap(outputVelocityFlow(),sourceVelocityFlow(),-1).ok);
    )"));
}

TEST_CASE("procgen.water.exportsSignedMaskChannelsAtomically") {
    Heightmap terrain(2, 1), output(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings settings;
    settings.dt               = 1;
    settings.flowAcceleration = 1;
    REQUIRE(field.advance(terrain, settings).ok());
    REQUIRE(field.exportChannel(output, TerrainWaterChannel::VelocityX).ok());
    CHECK(output.data() == std::vector<float>({-0.5F, 0}));
    const auto original = output.data();
    CHECK(!field.exportChannel(output, static_cast<TerrainWaterChannel>(99)).ok());
    CHECK(output.data() == original);
    Heightmap wrong(1, 1);
    CHECK(!field.exportChannel(wrong, TerrainWaterChannel::Depth).ok());
    REQUIRE(field.exportChannel(output, TerrainWaterChannel::FluxRight).ok());
    CHECK(output.data() == std::vector<float>({1, 0}));
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("field", [&]() { return &field; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        assert(field().exportChannel(output(),1).ok);
        assert(!field().exportChannel(output(),-1).ok);
    )"));
    CHECK(output.data() == original);
}

TEST_CASE("procgen.water.exportCurveMaskComposition") {
    Heightmap terrain(2, 1), channel(2, 1), curve(2, 1), mask(2, 1);
    terrain.data() = {1, 0};
    curve.data()   = {0.2F, 0.8F};
    mask.data()    = {0.5F, 0.5F};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings settings;
    settings.dt               = 1;
    settings.flowAcceleration = 1;
    REQUIRE(field.advance(terrain, settings).ok());
    REQUIRE(field.exportChannel(channel, TerrainWaterChannel::FluxRight).ok());
    REQUIRE(transformTerrainMask(channel, channel, curve).ok());
    REQUIRE(blendTerrainMask(mask, channel, TerrainMaskBlend::Multiply, 1, false).ok());
    CHECK(std::abs(mask.data()[0] - 0.4F) < 1e-6F);
    CHECK(std::abs(mask.data()[1] - 0.1F) < 1e-6F);
    auto source = field.sample(0, 0);
    REQUIRE(source.ok());
    CHECK(source.value().fluxRight == 1);
    CHECK(terrain.data() == std::vector<float>({1, 0}));
}

TEST_CASE("procgen.water.sourceFluxVelocityAndDepthOverwrite") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings s;
    s.dt               = 1;
    s.flowAcceleration = 1;
    s.precipitation    = 0.1F;
    s.evaporation      = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto left = field.sample(0, 0), right = field.sample(1, 0);
    REQUIRE(left.ok());
    REQUIRE(right.ok());
    CHECK(left.value().fluxRight == 1);
    CHECK(left.value().fluxLeft == 0);
    CHECK(left.value().velocityX == -0.5F);
    CHECK(right.value().velocityX == 0);
    CHECK(left.value().depth == 1.1F);
    CHECK(right.value().depth == 1.1F);
    REQUIRE(field.advance(terrain, s).ok());
    left = field.sample(0, 0);
    REQUIRE(left.ok());
    CHECK(left.value().fluxRight == 2);
    CHECK(terrain.data() == std::vector<float>({1, 0}));
}

TEST_CASE("procgen.water.dryFlatFieldAndScaledRainEvaporation") {
    Heightmap         terrain(1, 1);
    TerrainWaterField field;
    REQUIRE(field.reset(1, 1, 0).ok());
    TerrainWaterSettings s;
    REQUIRE(field.advance(terrain, s).ok());
    auto sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().velocityX == 0);
    CHECK(sample.value().velocityZ == 0);
    CHECK(sample.value().depth == 0);
    s.waterScale    = 3;
    s.dt            = 1;
    s.precipitation = 0.5F;
    s.evaporation   = 0;
    REQUIRE(field.advance(terrain, s).ok());
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1.5F);
    s.precipitation = 0;
    s.evaporation   = 2;
    REQUIRE(field.advance(terrain, s).ok());
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 0);
}

TEST_CASE("procgen.water.rejectsInvalidAndOverflowWithoutPublishing") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField    field;
    TerrainWaterSettings s;
    CHECK(!field.advance(terrain, s).ok());
    CHECK(!field.sample(0, 0).ok());
    REQUIRE(field.reset(2, 1, 1).ok());
    CHECK(!field.reset(-1, 1, 0).ok());
    CHECK(!field.reset(std::numeric_limits<int>::max(), 2, 0).ok());
    CHECK(field.getWidth() == 2);
    s.dt               = std::numeric_limits<float>::max();
    s.flowAcceleration = std::numeric_limits<float>::max();
    CHECK(!field.advance(terrain, s).ok());
    auto sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1);
    CHECK(sample.value().fluxRight == 0);
    s                  = TerrainWaterSettings();
    s.dt               = 1;
    s.flowAcceleration = 1;
    s.waterScale       = std::numeric_limits<float>::max();
    s.precipitation    = std::numeric_limits<float>::max();
    CHECK(!field.advance(terrain, s).ok());
    sample = field.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().fluxRight == 0);
    CHECK(sample.value().depth == 1);
    CHECK(!field.sample(2, 0).ok());
    TerrainWaterField moved = std::move(field);
    CHECK(field.getWidth() == 0);
    CHECK(!field.sample(0, 0).ok());
    sample = moved.sample(0, 0);
    REQUIRE(sample.ok());
    CHECK(sample.value().depth == 1);
    REQUIRE(field.reset(1, 1, 0).ok());
}

TEST_CASE("procgen.water.signedSourceAccelerationAndZeroDt") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(2, 1, 1).ok());
    TerrainWaterSettings s;
    s.dt               = 1;
    s.flowAcceleration = -1;
    s.precipitation    = 0;
    s.evaporation      = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto right = field.sample(1, 0);
    REQUIRE(right.ok());
    CHECK(right.value().fluxLeft == 1);
    CHECK(right.value().velocityX == 0.5F);
    s.dt = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto unchanged = field.sample(1, 0);
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value().fluxLeft == right.value().fluxLeft);
    CHECK(unchanged.value().velocityX == right.value().velocityX);
}

TEST_CASE("procgen.water.scriptOwnsFieldAndSamplesValues") {
    Heightmap terrain(2, 1);
    terrain.data() = {1, 0};
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("terrain", [&]() { return &terrain; });
    vm.run(vm.compileSource(R"(
        local field=eve.TerrainWaterField();
        assert(!field.sample(0,0).ok);
        assert(field.reset(2,1,1.0).ok && field.getWidth()==2 && field.getHeight()==1);
        local s=eve.TerrainWaterSettings();
        s.dt=1.0; s.flowAcceleration=1.0; s.precipitation=0.0; s.evaporation=0.0;
        assert(field.advance(terrain(),s).ok);
        local copy=field.sample(0,0).value;
        assert(copy.depth==1.0 && copy.fluxRight==1.0 && copy.velocityX==-0.5);
        assert(copy.fluxTop==0.0 && copy.fluxBottom==0.0 && copy.velocityZ==0.0);
        assert(field.advance(terrain(),s).ok);
        assert(copy.fluxRight==1.0 && field.sample(0,0).value.fluxRight==2.0);
        assert(!field.sample(-1,0).ok);
        s.spacingX=0.0;
        assert(!field.advance(terrain(),s).ok);
    )"));
}

TEST_CASE("procgen.water.rectangularZFlowPreservesSourceSignsAndSpacing") {
    Heightmap terrain(1, 2);
    terrain.data() = {1, 0};
    TerrainWaterField field;
    REQUIRE(field.reset(1, 2, 1).ok());
    TerrainWaterSettings s;
    s.dt               = 1;
    s.flowAcceleration = 1;
    s.spacingZ         = 2;
    s.precipitation    = 0;
    s.evaporation      = 0;
    REQUIRE(field.advance(terrain, s).ok());
    auto top = field.sample(0, 0), bottom = field.sample(0, 1);
    REQUIRE(top.ok());
    REQUIRE(bottom.ok());
    CHECK(top.value().fluxBottom == 0.5F);
    CHECK(top.value().fluxTop == 0);
    CHECK(top.value().velocityX == 0);
    CHECK(top.value().velocityZ == 0.25F);
    CHECK(std::abs(bottom.value().velocityZ - 1.0F / 9) < 1e-6F);
    CHECK(bottom.value().depth == 1);
}
