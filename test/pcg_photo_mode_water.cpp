#include "zeroerr/unittest.h"
#include "common/Capability.h"
#include "graphics/PcgWaterPhotoMode.h"
#include "ui/PcgPhotoModeApplyPlan.h"
#include <cmath>
using namespace eve;
using namespace eve::graphics;
using namespace eve::ui;
TEST_CASE("graphics.pcgWaterPhotoMode.routesSevenFieldsIntoRealPlanners"){
 cap::detail::clearAllRaw();PcgWaterPhotoModeAuthority water;water.setAuthority(true);
 PcgPhotoModeValues before,after;after.setBool("m_pcgWaterReflectionEnabled",false).value();
 after.setFloat("m_pcgWaterReflectionDistance",100).value();after.setInt("m_pcgWaterReflectionResolution",3).value();
 after.setFloat("m_pcgReflectionsLODBias",2).value();after.setColor("m_pcgUnderwaterFogColor",0.1f,0.2f,0.3f,1).value();
 after.setFloat("m_pcgUnderwaterFogDensity",0.08f).value();after.setFloat("m_pcgUnderwaterFogDistance",70).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),7u);REQUIRE(plan.executeRegistered().ok());
 WaterPlanarReflectionSettings reflection;REQUIRE(water.applyToPlanarSettings(reflection).ok());
 WaterPlanarReflectionInput input;input.cameraPosition={0,5,0};input.cameraForward={0,-0.2f,-1};
 auto capture=buildWaterPlanarReflectionPlan(reflection,input);REQUIRE(capture.ok());
 CHECK(!capture.value().shouldRender);CHECK_EQ(capture.value().textureWidth,128);CHECK_EQ(capture.value().layerCullDistances[0],1200.0f);
 WaterUnderwaterSettings settings;settings.supportPostFx=false;REQUIRE(water.applyToUnderwaterSettings(settings).ok());
 WaterUnderwaterState state;WaterUnderwaterOutput output;UnderwaterColorGradient emptyColor;UnderwaterScalarCurve emptyCurve;
 WaterUnderwaterInput frame;frame.cameraY=-5;frame.seaLevel=0;
 REQUIRE(advanceWaterUnderwaterEffects(state,output,settings,frame,emptyColor,emptyColor,emptyCurve,emptyColor).ok());
 CHECK_EQ(output.fogColor.x,0.1f);CHECK_EQ(output.fogColor.y,0.2f);CHECK_EQ(output.fogColor.z,0.3f);
 CHECK_EQ(output.fogDensity,0.08f);CHECK_EQ(output.fogEnd,70.0f);
}
TEST_CASE("graphics.pcgWaterPhotoMode.rollsBackBeforeMissingAudioUnderwaterProvider"){
 cap::detail::clearAllRaw();PcgWaterPhotoModeAuthority water;water.setAuthority(true);PcgPhotoModeValues before,after;
 after.setColor("m_pcgUnderwaterFogColor",0.4f,0.5f,0.6f,1).value();after.setFloat("m_pcgUnderwaterVolume",0.2f).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());CHECK(!plan.executeRegistered().ok());CHECK(plan.getRollbackComplete());
 CHECK_EQ(water.getFogRed(),0.0f);CHECK_EQ(water.getFogGreen(),0.0f);CHECK_EQ(water.getFogBlue(),0.0f);
}
TEST_CASE("graphics.pcgWaterPhotoMode.authorityLeaseRevokesOnDestruction"){
 cap::detail::clearAllRaw();PcgPhotoModeValues before,after;after.setFloat("m_pcgWaterReflectionDistance",5).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());
 {PcgWaterPhotoModeAuthority water;water.setAuthority(true);REQUIRE(plan.executeRegistered().ok());}
 CHECK(!plan.executeRegistered().ok());
}
