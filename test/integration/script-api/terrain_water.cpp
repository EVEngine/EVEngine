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
