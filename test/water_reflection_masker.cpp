#include "zeroerr/unittest.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

#include "graphics/WaterPlanarReflection.h"
#include "graphics/WaterReflectionMasker.h"
#include "image/ImageData.h"

using namespace eve::graphics;

TEST_CASE("graphics.waterReflectionMasker.samplesPcgRoundedCoordinatesAndChannels") {
    eve::image::ImageData mask(2,2,"RGBA8");
    mask.setPixel(0,0,{.1f,.2f,.3f,.4f});mask.setPixel(1,0,{.6f,.1f,.1f,.1f});
    mask.setPixel(0,1,{.1f,.7f,.1f,.1f});mask.setPixel(1,1,{.1f,.1f,.8f,.1f});
    WaterReflectionMasker masker;WaterPlanarReflectionSettings settings;settings.enabled=false;
    REQUIRE(masker.configure(WaterReflectionMaskChannel::R,.5f,.7f,false).ok());REQUIRE(masker.setMask(mask).ok());
    auto enabled=masker.evaluate(25,10,0,0,100,100,settings);REQUIRE(enabled.ok());
    CHECK_EQ(enabled.value(),WaterReflectionMaskTransition::Enabled);CHECK(settings.enabled);
    CHECK_EQ(masker.getSampleX(),1);CHECK_EQ(masker.getSampleY(),0);
    auto disabled=masker.evaluate(75,75,0,0,100,100,settings);REQUIRE(disabled.ok());
    CHECK_EQ(disabled.value(),WaterReflectionMaskTransition::Disabled);CHECK(!settings.enabled);
    REQUIRE(masker.configure(WaterReflectionMaskChannel::RGBA,.75f,.85f,false).ok());
    REQUIRE(masker.evaluate(75,75,0,0,100,100,settings).ok());CHECK(settings.enabled);
}

TEST_CASE("graphics.waterReflectionMasker.handlesMissingEdgesAndFailureAtomically") {
    eve::image::ImageData mask(1,1,"RGBA8");mask.setPixel(0,0,{1,0,0,1});
    WaterReflectionMasker masker;WaterPlanarReflectionSettings settings;settings.enabled=true;
    REQUIRE(masker.configure(WaterReflectionMaskChannel::R,.9f,1.f,true).ok());REQUIRE(masker.setMask(mask).ok());
    auto unchanged=masker.evaluate(0,0,0,0,100,100,settings);REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value(),WaterReflectionMaskTransition::Unchanged);
    auto edge=masker.evaluate(100,100,0,0,100,100,settings);REQUIRE(edge.ok());
    CHECK_EQ(edge.value(),WaterReflectionMaskTransition::Disabled);CHECK_EQ(masker.getSampleX(),1);
    CHECK(!masker.evaluate(0,0,0,0,-1,1,settings).ok());CHECK(!settings.enabled);
    CHECK(!masker.configure(WaterReflectionMaskChannel::R,2,1,false).ok());
}

TEST_CASE("graphics.waterReflectionMasker.bindsRealVm") {
    ssq::VM vm(1024);auto table=vm.addTable("eve");exposeWaterPlanarReflectionBindings(table);exposeWaterReflectionMaskerBindings(table);
    vm.run(vm.compileSource(R"(
      local m=eve.WaterReflectionMasker();local s=eve.WaterPlanarReflectionSettings();s.enabled=true;
      assert(m.configure(0,0.35,1.0,true).ok);m.clearMask();
      local r=m.evaluate(0.0,0.0,0.0,0.0,100.0,100.0,s);assert(r.ok && r.value==2);
      assert(!m.getEnabled() && !m.getHeightFeaturesEnabled() && !s.enabled);
    )"));
}
