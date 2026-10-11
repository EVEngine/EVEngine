#include "zeroerr/unittest.h"
#include "common/Capability.h"
#include "common/AudioQuery.h"
#include "common/FramePresentation.h"
#include "ui/PcgPhotoModeApplyPlan.h"
#include "ui/PcgPhotoModePhoto.h"
#include "weather/Weather.h"
#include "camera/CameraController.h"
#include "daynight/DayNight.h"
#include "daynight/PcgLightingTimePhotoMode.h"
#include "daynight/PcgLightingSunPhotoMode.h"
#include "daynight/PcgLightingSkyboxPhotoMode.h"
#include "daynight/PcgLightingFogPhotoMode.h"
#include "daynight/PcgLightingAmbientPhotoMode.h"
#include "graphics/Volumetric.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/PcgGraphicsPhotoMode.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "GraphicsParitySupport.h"
#include "procgen/heightmap/PcgTerrainPhotoMode.h"
#include "procgen/heightmap/PcgTerrainStreaming.h"
#include "procgen/heightmap/TerrainPipeline.h"
#include "system/System.h"
#include <chrono>
#include <vector>
using namespace eve;
using namespace eve::ui;
namespace {
struct AudioMock final:IAudioQuery {
 float gain=1;float volume()const override{return gain;}void setVolume(float v)override{gain=v;}void stopAll()override{}
};
struct PresentationMock final:IFramePresentation{int count=0;void setVSyncCount(int v)override{count=v;}int getVSyncCount()const noexcept override{return count;}};
struct FieldSink final:IPhotoModeFieldSink {
 std::string field;std::vector<PhotoModeAssignment> calls;
 PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment&a)const noexcept override{return a.field==field?PhotoModeFieldAcceptance::Accepted:PhotoModeFieldAcceptance::Rejected;}
 Result<void> applyPhotoModeField(const PhotoModeAssignment&a)override{calls.push_back(a);return Result<void>::success();}
};
struct RecordingSink final:IPhotoModeApplySink {
 std::vector<PhotoModeAssignment> calls; int failAt=-1; bool failRollback=false;
 Result<void> applyPhotoModeAssignment(const PhotoModeAssignment&a)override{
  calls.push_back(a);const int index=static_cast<int>(calls.size())-1;
  if(index==failAt||(failRollback&&index>failAt))return Result<void>::failure(
   Diagnostic::error(DiagnosticCode::InvalidArgument,"injected",{},{},"test"));
  return Result<void>::success();
 }
};
PcgPhotoModeValues changed(){
 PcgPhotoModeValues v;v.setFloat("m_fieldOfView",75).value();v.setBool("m_pcgWeatherRain",true).value();
 v.setFloat("m_pcgUnderwaterVolume",0.2f).value();v.setFloat("m_globalGrassDistance",350).value();return v;
}
}
TEST_CASE("ui.pcgPhotoModeApply.compilesStableTypedDomainCommands"){
 PcgPhotoModeValues before,after=changed();PcgPhotoModeApplyPlan p;REQUIRE(p.compile(before,after).ok());
 REQUIRE_EQ(p.getCommandCount(),4u);
 auto f0=p.getCommandField(0);REQUIRE(f0.ok());CHECK_EQ(f0.value(),std::string("m_fieldOfView"));
 auto d0=p.getCommandDomain(0);REQUIRE(d0.ok());CHECK_EQ(d0.value(),static_cast<int>(PhotoModeDomain::Camera));
 auto d1=p.getCommandDomain(1);REQUIRE(d1.ok());CHECK_EQ(d1.value(),static_cast<int>(PhotoModeDomain::Audio));
 auto d2=p.getCommandDomain(2);REQUIRE(d2.ok());CHECK_EQ(d2.value(),static_cast<int>(PhotoModeDomain::Weather));
 auto d3=p.getCommandDomain(3);REQUIRE(d3.ok());CHECK_EQ(d3.value(),static_cast<int>(PhotoModeDomain::Grass));
 RecordingSink sink;REQUIRE(p.execute(sink).ok());REQUIRE_EQ(sink.calls.size(),4u);
 CHECK_EQ(std::get<float>(sink.calls[0].value),75.0f);CHECK_EQ(p.getAppliedCount(),4u);CHECK(p.getRollbackComplete());
}
TEST_CASE("ui.pcgPhotoModeApply.rollsBackInReverseAfterFailure"){
 PcgPhotoModeValues before,after=changed();PcgPhotoModeApplyPlan p;REQUIRE(p.compile(before,after).ok());
 RecordingSink sink;sink.failAt=2;auto result=p.execute(sink);CHECK(!result.ok());REQUIRE_EQ(sink.calls.size(),5u);
 CHECK_EQ(sink.calls[3].field,std::string("m_globalVolume"));CHECK_EQ(sink.calls[4].field,std::string("m_fieldOfView"));
 CHECK_EQ(std::get<float>(sink.calls[3].value),1.0f);CHECK_EQ(std::get<float>(sink.calls[4].value),60.0f);
 CHECK(p.getRollbackComplete());CHECK_EQ(p.getAppliedCount(),2u);
}
TEST_CASE("ui.pcgPhotoModeApply.isAtomicAndCapabilityAbsenceIsObservable"){
 PcgPhotoModeValues before,after=changed();auto a=before.snapshotJson();auto b=after.snapshotJson();REQUIRE(a.ok());REQUIRE(b.ok());
 PcgPhotoModeApplyPlan p;REQUIRE(p.compileJson(a.value(),b.value()).ok());auto count=p.getCommandCount();auto rev=p.getRevision();
 CHECK(!p.compileJson("bad",b.value()).ok());CHECK_EQ(p.getCommandCount(),count);CHECK_EQ(p.getRevision(),rev);
 cap::detail::clearAllRaw();CHECK(!p.executeRegistered().ok());
 RecordingSink sink;cap::provide<IPhotoModeApplySink>(&sink);REQUIRE(p.executeRegistered().ok());CHECK_EQ(sink.calls.size(),count);
 cap::revoke<IPhotoModeApplySink>(&sink);
}
TEST_CASE("ui.pcgPhotoModeApply.reportsIncompleteRollback"){
 PcgPhotoModeValues before,after=changed();PcgPhotoModeApplyPlan p;REQUIRE(p.compile(before,after).ok());
 RecordingSink sink;sink.failAt=2;sink.failRollback=true;CHECK(!p.execute(sink).ok());CHECK(!p.getRollbackComplete());
}
TEST_CASE("ui.pcgPhotoModeApply.registeredRouterWritesAudioAuthorityAndFieldProvider"){
 cap::detail::clearAllRaw();AudioMock audio;FieldSink camera;camera.field="m_fieldOfView";
 cap::provide<IAudioQuery>(&audio);cap::addListener<IPhotoModeFieldSink>(&camera);
 PcgPhotoModeValues before,after;after.setFloat("m_fieldOfView",82).value();after.setFloat("m_globalVolume",0.35f).value();
 PcgPhotoModeApplyPlan p;REQUIRE(p.compile(before,after).ok());REQUIRE(p.executeRegistered().ok());
 CHECK_EQ(audio.gain,0.35f);REQUIRE_EQ(camera.calls.size(),1u);CHECK_EQ(std::get<float>(camera.calls[0].value),82.0f);
 cap::removeListener<IPhotoModeFieldSink>(&camera);cap::revoke<IAudioQuery>(&audio);
}
TEST_CASE("ui.pcgPhotoModeApply.registeredRouterRejectsAmbiguousOwnership"){
 cap::detail::clearAllRaw();FieldSink first,second;first.field=second.field="m_fieldOfView";
 cap::addListener<IPhotoModeFieldSink>(&first);cap::addListener<IPhotoModeFieldSink>(&second);
 PcgPhotoModeValues before,after;after.setFloat("m_fieldOfView",82).value();PcgPhotoModeApplyPlan p;
 REQUIRE(p.compile(before,after).ok());CHECK(!p.executeRegistered().ok());CHECK(first.calls.empty());CHECK(second.calls.empty());
 cap::removeListener<IPhotoModeFieldSink>(&first);cap::removeListener<IPhotoModeFieldSink>(&second);
}
TEST_CASE("ui.pcgPhotoModeApply.weatherProviderOwnsSixPcgFieldsAndRollsBack"){
 cap::detail::clearAllRaw();weather::Weather weather;PcgPhotoModeValues before,after;
 after.setBool("m_pcgWeatherEnabled",true).value();after.setBool("m_pcgWeatherRain",true).value();
 after.setBool("m_pcgWeatherSnow",true).value();after.setBool("m_pcgWindSettingsOverride",true).value();
 after.setFloat("m_pcgWindDirection",0.625f).value();after.setFloat("m_pcgWindSpeed",0.8f).value();
 PcgPhotoModeApplyPlan p;REQUIRE(p.compile(before,after).ok());REQUIRE_EQ(p.getCommandCount(),6u);
 REQUIRE(p.executeRegistered().ok());CHECK(weather.getPcgWeatherEnabled());CHECK(weather.getPcgRainEnabled());
 CHECK(weather.getPcgSnowEnabled());CHECK(weather.getPcgWindOverride());CHECK_EQ(weather.getWindDirection(),0.625f);
 CHECK_EQ(weather.getWindSpeed(),0.8f);
 PcgPhotoModeValues failing=after;failing.setFloat("m_pcgWindDirection",0.9f).value();failing.setFloat("m_globalGrassDistance",300).value();
 PcgPhotoModeApplyPlan rollback;REQUIRE(rollback.compile(after,failing).ok());CHECK(!rollback.executeRegistered().ok());
 CHECK_EQ(weather.getWindSpeed(),0.8f);
}
TEST_CASE("ui.pcgPhotoModeApply.explicitCameraAuthorityAppliesSixLiveFields"){
 cap::detail::clearAllRaw();auto* camera=graphics::Camera3D::createCamera();camera->setEye(0,0,3);camera->setTarget(0,0,0);
 camera::CameraController controller;controller.setCamera(camera);controller.setPhotoModeAuthority(true);
 PcgPhotoModeValues before,after;after.setFloat("m_fieldOfView",72).value();after.setFloat("m_pcgCullinDistance",25).value();
 after.setFloat("m_cameraAperture",11).value();after.setFloat("m_cameraFocalLength",80).value();
 after.setFloat("m_cameraRoll",30).value();after.setFloat("m_farClipPlane",1500).value();
 PcgPhotoModeApplyPlan p;REQUIRE(p.compile(before,after).ok());REQUIRE_EQ(p.getCommandCount(),6u);
 REQUIRE(p.executeRegistered().ok());CHECK_EQ(camera->getFov(),72.0f);CHECK_EQ(camera->getFarClip(),1500.0f);
 CHECK_EQ(camera->getAperture(),11.0f);CHECK_EQ(camera->getFocalLength(),80.0f);
 CHECK_EQ(controller.getCameraRoll(),30.0f);CHECK_EQ(controller.getPcgCullingDistance(),25.0f);
 CHECK_EQ(camera->getLayerCullDistance(0),1525.0f);CHECK(std::abs(camera->data()->upX)>0.4f);
 controller.setPhotoModeAuthority(false);CHECK(!p.executeRegistered().ok());
}
TEST_CASE("ui.pcgPhotoModeApply.cameraAuthorityLifetimeRevokesProvider"){
 cap::detail::clearAllRaw();auto* camera=graphics::Camera3D::createCamera();PcgPhotoModeValues before,after;
 after.setFloat("m_fieldOfView",71).value();PcgPhotoModeApplyPlan p;REQUIRE(p.compile(before,after).ok());
 {camera::CameraController controller;controller.setCamera(camera);controller.setPhotoModeAuthority(true);REQUIRE(p.executeRegistered().ok());}
 CHECK(!p.executeRegistered().ok());
}
TEST_CASE("ui.pcgPhotoModeApply.cameraAuthorityProjectsAllPostFxFieldsToFinalComposite"){
 cap::detail::clearAllRaw();auto* camera=graphics::Camera3D::createCamera();camera::CameraController controller;
 controller.setCamera(camera);controller.setPhotoModeAuthority(true);PcgPhotoModeValues before,after;
 before.setBool("m_dofActive",false).value();before.setInt("m_dofFocusModeURP",1).value();
 after.setFloat("m_postFXExposure",2.25f).value();after.setInt("m_postFXExposureMode",1).value();
 after.setBool("m_dofActive",true).value();after.setBool("m_autoDOFFocus",false).value();
 after.setFloat("m_dofFocusDistance",24.f).value();after.setFloat("m_dofAperture",5.6f).value();
 after.setFloat("m_dofFocalLength",85.f).value();after.setInt("m_dofKernelSize",3).value();
 after.setInt("m_savedDofFocusMode",2).value();after.setInt("m_dofFocusModeHDRP",1).value();
 after.setInt("m_dofFocusModeURP",0).value();after.setInt("m_dofQualityHDRP",2).value();
 after.setFloat("m_dofNearBlurStart",1.f).value();after.setFloat("m_dofNearBlurEnd",4.f).value();
 after.setFloat("m_dofFarBlurStart",40.f).value();after.setFloat("m_dofFarBlurEnd",120.f).value();
 after.setFloat("m_dofStartBlurURP",8.f).value();after.setFloat("m_dofEndBlurURP",68.f).value();
 after.setFloat("m_dofMaxRadiusBlur",2.f).value();after.setBool("m_dofHighQualityURP",true).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),20u);
 REQUIRE(plan.executeRegistered().ok());const auto& state=controller.getPcgPostFxState();
 CHECK_EQ(state.savedFocusMode,2);CHECK_EQ(state.focusModeHdrp,1);CHECK_EQ(state.farBlurEnd,120.f);
 CHECK_EQ(camera->getExposure(),2.25f);CHECK(camera->isAutoExposure());CHECK(camera->data()->dofMaxBlurPx>0.f);
 CHECK_EQ(camera->data()->dofFocusDistance,24.f);CHECK_EQ(camera->data()->dofFocusRange,60.f);
 CHECK_EQ(camera->data()->dofMaxBlurPx,12.f);CHECK_EQ(camera->getAperture(),5.6f);
 CHECK_EQ(camera->getFocalLength(),85.f);
}
TEST_CASE("ui.pcgPhotoModeApply.postFxRollsBackLiveCompositeWhenFollowingDomainIsMissing"){
 cap::detail::clearAllRaw();auto* camera=graphics::Camera3D::createCamera();camera::CameraController controller;
 controller.setCamera(camera);controller.setPhotoModeAuthority(true);PcgPhotoModeValues before,after;
 after.setFloat("m_postFXExposure",3.f).value();after.setBool("m_dofActive",false).value();
 after.setFloat("m_terrainDetailDensity",0.8f).value();PcgPhotoModeApplyPlan plan;
 REQUIRE(plan.compile(before,after).ok());CHECK(!plan.executeRegistered().ok());CHECK(plan.getRollbackComplete());
 CHECK_EQ(controller.getPcgPostFxState().postFxExposure,13.5f);CHECK(controller.getPcgPostFxState().depthOfFieldActive);
 CHECK_EQ(camera->getExposure(),13.5f);CHECK(camera->data()->dofMaxBlurPx>0.f);
}
TEST_CASE("ui.pcgPhotoModeApply.terrainAuthorityDrivesLodBasemapAndDetailPolicy"){
 cap::detail::clearAllRaw();procgen::PcgTerrainPhotoModeAuthority terrain;terrain.setAuthority(true);
 PcgPhotoModeValues before,after;after.setBool("m_drawInstanced",false).value();
 after.setFloat("m_terrainDetailDensity",0.8f).value();after.setFloat("m_terrainDetailDistance",275.f).value();
 after.setFloat("m_terrainPixelError",1.f).value();after.setFloat("m_terrainBasemapDistance",500.f).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),5u);
 REQUIRE(plan.executeRegistered().ok());CHECK(!terrain.state().drawInstanced);CHECK_EQ(terrain.state().detailDensity,0.8f);
 CHECK_EQ(terrain.state().detailDistance,275.f);
 CHECK_EQ(terrain.textureTier(499.f),procgen::PcgTerrainTextureTier::Detailed);
 CHECK_EQ(terrain.textureTier(500.f),procgen::PcgTerrainTextureTier::Basemap);
 procgen::Heightmap heightmap(9,9);for(int y=0;y<9;++y)for(int x=0;x<9;++x)heightmap.setHeight(x,y,float((x+y)%3));
 procgen::TerrainMeshSettings settings;settings.cellsX=8;settings.cellsY=8;
 const int strict=terrain.selectLod(heightmap,settings,3,20.f,1080.f,60.f);
 PhotoModeAssignment relaxed{"m_terrainPixelError",PhotoModeDomain::Terrain,1000.f};REQUIRE(terrain.applyPhotoModeField(relaxed).ok());
 CHECK(terrain.selectLod(heightmap,settings,3,20.f,1080.f,60.f)>=strict);
}
TEST_CASE("ui.pcgPhotoModeApply.graphicsAuthorityProjectsFiveLiveQualityFields"){
 cap::detail::clearAllRaw();auto* graphics=graphics::parity_test::headlessGraphics();auto* camera=graphics::Camera3D::createCamera();
 auto* control=graphics->getRenderControl();graphics::PcgGraphicsPhotoModeAuthority authority;
 authority.setTargets(graphics,control,camera);authority.setAuthority(true);PcgPhotoModeValues before,after;
 after.setFloat("m_lodBias",4.f).value();after.setInt("m_antiAliasing",2).value();
 after.setFloat("m_shadowDistance",220.f).value();after.setInt("m_shadowResolution",1).value();
 after.setInt("m_shadowCascades",2).value();PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());
 REQUIRE_EQ(plan.getCommandCount(),5u);REQUIRE(plan.executeRegistered().ok());CHECK_EQ(camera->data()->lodBias,4.f);
 CHECK_EQ(camera->getShadowLayerCullDistance(31),220.f);CHECK(control->isEnabled("aa"));CHECK(!control->isEnabled("taa"));
 CHECK_EQ(graphics->pipelineAntiAliasing()->getMode(),std::string("smaa"));CHECK_EQ(authority.state().shadowResolution,1);
 CHECK_EQ(authority.state().shadowCascades,2);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingShadowMultiplierUsesGraphicsShadowAuthority"){
 cap::detail::clearAllRaw();auto* graphics=graphics::parity_test::headlessGraphics();auto* camera=graphics::Camera3D::createCamera();
 graphics::PcgGraphicsPhotoModeAuthority authority;authority.setTargets(graphics,graphics->getRenderControl(),camera);
 authority.setAuthority(true);PcgPhotoModeValues before,after;after.setFloat("m_shadowDistance",200.f).value();
 after.setFloat("m_globalShadowDistanceMultiplier",2.5f).value();PcgPhotoModeApplyPlan plan;
 REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),2u);REQUIRE(plan.executeRegistered().ok());
 CHECK_EQ(camera->getShadowLayerCullDistance(0),500.f);CHECK_EQ(camera->getShadowLayerCullDistance(31),500.f);
 CHECK_EQ(authority.state().globalShadowDistanceMultiplier,2.5f);PhotoModeAssignment invalid{
  "m_globalShadowDistanceMultiplier",PhotoModeDomain::Lighting,5.1f};CHECK(!authority.applyPhotoModeField(invalid).ok());
 CHECK_EQ(camera->getShadowLayerCullDistance(7),500.f);
}
TEST_CASE("ui.pcgPhotoModeApply.systemAuthorityAppliesVSyncAndRealFrameCap"){
 cap::detail::clearAllRaw();PresentationMock presentation;cap::provide<IFramePresentation>(&presentation);
 system::System system;PcgPhotoModeValues before,after;after.setInt("m_vSync",2).value();after.setInt("m_targetFPS",120).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),2u);
 REQUIRE(plan.executeRegistered().ok());CHECK_EQ(presentation.count,2);CHECK_EQ(system.getPhotoModeVSync(),2);
 CHECK_EQ(system.getPhotoModeTargetFPS(),120);PhotoModeAssignment disable{"m_vSync",PhotoModeDomain::System,int64_t{0}};
 REQUIRE(system.applyPhotoModeField(disable).ok());system.limitFrame();const auto start=std::chrono::steady_clock::now();
 system.limitFrame();const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
 CHECK_GE(elapsed,1);cap::revoke<IFramePresentation>(&presentation);
}
TEST_CASE("ui.pcgPhotoModeApply.streamingAuthorityDrivesRealChunkResidencyAndImpostorTier"){
 cap::detail::clearAllRaw();procgen::Heightmap heightmap(24,24);
 for(int y=0;y<24;++y)for(int x=0;x<24;++x)heightmap.setHeight(x,y,float(x+y));
 auto hydrology=procgen::TerrainPipeline::buildHydrology(heightmap,5.f,-1.f);
 auto climate=procgen::TerrainPipeline::buildClimate(heightmap,hydrology,-1.f,0.5f);
 std::vector<uint8_t> bytes;std::string error;REQUIRE(procgen::TerrainAsset::bake(heightmap,hydrology,climate,8,bytes,&error));
 procgen::TerrainStreamingCache cache;REQUIRE(cache.open(bytes.data(),bytes.size(),&error));
 procgen::PcgTerrainStreamingAuthority streaming;REQUIRE(streaming.setTarget(&cache,100.f).ok());streaming.setAuthority(true);
 PcgPhotoModeValues before,after;after.setFloat("m_pcgLoadRange",900.f).value();after.setFloat("m_pcgImpostorRange",1700.f).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),2u);
 REQUIRE(plan.executeRegistered().ok());CHECK_EQ(streaming.regularRadiusChunks(),2);CHECK_EQ(streaming.impostorRadiusChunks(),3);
 CHECK_EQ(streaming.tierForDistance(900.f),procgen::PcgTerrainStreamingTier::Regular);
 CHECK_EQ(streaming.tierForDistance(901.f),procgen::PcgTerrainStreamingTier::Impostor);
 CHECK_EQ(streaming.tierForDistance(1701.f),procgen::PcgTerrainStreamingTier::Unloaded);
 auto streamed=streaming.streamAround(12,12,2);REQUIRE(streamed.ok());CHECK_EQ(streamed.value().loaded,2);
 PhotoModeAssignment invalid{"m_pcgLoadRange",PhotoModeDomain::Streaming,-1.f};CHECK(!streaming.applyPhotoModeField(invalid).ok());
 CHECK_EQ(streaming.state().regularRange,900.f);
}
TEST_CASE("ui.pcgPhotoModeApply.photoAuthorityOwnsAllCapturePersistenceAndOverlayFields"){
 cap::detail::clearAllRaw();PcgPhotoModePhotoAuthority photo;photo.setAuthority(true);PcgPhotoModeValues before,after;
 after.setString("m_lastSceneName","Pcg World").value();after.setInt("m_screenshotResolution",9).value();
 after.setInt("m_screenshotImageFormat",0).value();after.setBool("m_loadSavedSettings",false).value();
 after.setBool("m_revertOnDisabled",false).value();after.setBool("m_showFPS",true).value();
 after.setBool("m_showReticle",true).value();after.setBool("m_showRuleOfThirds",true).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),8u);
 REQUIRE(plan.executeRegistered().ok());CHECK_EQ(photo.state().lastSceneName,"Pcg World");
 CHECK_EQ(photo.screenshotSize().width,7680);CHECK_EQ(photo.screenshotSize().height,4320);
 CHECK_EQ(std::string(photo.screenshotExtension()),"exr");CHECK(!photo.state().loadSavedSettings);
 CHECK(!photo.state().revertOnDisabled);CHECK(photo.state().showFps);CHECK(photo.state().showReticle);
 CHECK(photo.state().showRuleOfThirds);PhotoModeAssignment invalid{"m_screenshotResolution",PhotoModeDomain::Photo,int64_t{10}};
 CHECK(!photo.applyPhotoModeField(invalid).ok());CHECK_EQ(photo.screenshotSize().width,7680);
}
TEST_CASE("ui.pcgPhotoModeApply.photoAuthorityOwnsLightingProfileIdentity"){
 cap::detail::clearAllRaw();PcgPhotoModePhotoAuthority photo;photo.setAuthority(true);
 PcgPhotoModeValues before,after;after.setBool("m_isUsingPcgLighting",true).value();
 after.setInt("m_selectedPcgLightingProfile",3).value();PcgPhotoModeApplyPlan plan;
 REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),2u);REQUIRE(plan.executeRegistered().ok());
 CHECK(photo.lightingProfileMatches(3));CHECK(!photo.lightingProfileMatches(2));
 PhotoModeAssignment custom{"m_selectedPcgLightingProfile",PhotoModeDomain::Lighting,int64_t{-1}};
 REQUIRE(photo.applyPhotoModeField(custom).ok());CHECK(!photo.lightingProfileMatches(-1));
 PhotoModeAssignment invalid{"m_selectedPcgLightingProfile",PhotoModeDomain::Lighting,int64_t{-2}};
 CHECK(!photo.applyPhotoModeField(invalid).ok());CHECK_EQ(photo.state().lightingProfile,-1);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingTimeAuthorityDrivesDayNightClock"){
 cap::detail::clearAllRaw();daynight::DayNight clock;daynight::PcgLightingTimePhotoMode authority;
 authority.setTarget(&clock);authority.setAuthority(true);PcgPhotoModeValues before,after;
 after.setFloat("m_pcgTime",18.5f).value();after.setBool("m_pcgTimeOfDayEnabled",true).value();
 after.setFloat("m_pcgTimeScale",12.f).value();PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());
 REQUIRE_EQ(plan.getCommandCount(),3u);REQUIRE(plan.executeRegistered().ok());CHECK_EQ(clock.getTimeOfDay(),18.5f);
 CHECK_EQ(clock.getSpeed(),12.f);CHECK(!clock.isPaused());PhotoModeAssignment pause{"m_pcgTimeOfDayEnabled",PhotoModeDomain::Lighting,false};
 REQUIRE(authority.applyPhotoModeField(pause).ok());CHECK(clock.isPaused());
 PhotoModeAssignment invalid{"m_pcgTimeScale",PhotoModeDomain::Lighting,201.f};CHECK(!authority.applyPhotoModeField(invalid).ok());
 CHECK_EQ(clock.getSpeed(),12.f);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingSunAuthorityPersistsAcrossDayNightUpdates"){
 cap::detail::clearAllRaw();daynight::DayNight clock;daynight::PcgLightingSunPhotoMode authority;
 PhotoModeAssignment unavailable{"m_sunOverride",PhotoModeDomain::Lighting,true};
 CHECK(!authority.applyPhotoModeField(unavailable).ok());
 authority.setTarget(&clock);authority.setAuthority(true);PcgPhotoModeValues before,after;
 after.setFloat("m_sunRotation",90.f).value();after.setFloat("m_sunPitch",30.f).value();
 after.setBool("m_sunOverride",true).value();after.setFloat("m_sunIntensity",2.5f).value();
 after.setColor("m_sunColor",0.8f,0.6f,0.4f,1.f).value();
 after.setFloat("m_sunKelvinValue",4200.f).value();PcgPhotoModeApplyPlan plan;
 REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),6u);
 REQUIRE(plan.executeRegistered().ok());auto state=clock.getPcgManualSun();CHECK(state.enabled);
 CHECK_EQ(state.rotationDegrees,90.f);CHECK_EQ(state.pitchDegrees,30.f);CHECK_EQ(state.intensity,2.5f);
 auto* graphics=graphics::parity_test::headlessGraphics();clock.update(0.f,graphics);CHECK_EQ(clock.getSunIntensity(),2.5f);
 CHECK(std::abs(clock.getSunDirX()+0.8660254f)<0.0001f);CHECK(std::abs(clock.getSunDirY()-0.5f)<0.0001f);
 CHECK(std::abs(clock.getSunDirZ())<0.0001f);const float manualRed=clock.getSunR();clock.setTimeOfDay(2.f);
 clock.update(1.f,graphics);CHECK(std::abs(clock.getSunDirX()+0.8660254f)<0.0001f);
 CHECK(std::abs(clock.getSunR()-manualRed)<0.0001f);CHECK(manualRed>1.9f);
 PhotoModeAssignment disable{"m_sunOverride",PhotoModeDomain::Lighting,false};
 REQUIRE(authority.applyPhotoModeField(disable).ok());clock.setTimeOfDay(12.f);clock.update(0.f,graphics);
 CHECK_EQ(clock.getSunIntensity(),1.f);CHECK(clock.getSunDirY()>0.9f);
 PhotoModeAssignment invalid{"m_sunKelvinValue",PhotoModeDomain::Lighting,1000.f};
 CHECK(!authority.applyPhotoModeField(invalid).ok());CHECK_EQ(clock.getPcgManualSun().kelvin,4200.f);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingSkyboxAuthorityDrivesProceduralSkyAndRestores"){
 cap::detail::clearAllRaw();daynight::DayNight clock;auto* graphics=graphics::parity_test::headlessGraphics();
 clock.setTimeOfDay(12.f);clock.setPaused(true);clock.update(0.f,graphics);
 const float baseR=clock.getSkyR(),baseG=clock.getSkyG(),baseB=clock.getSkyB();
 daynight::PcgLightingSkyboxPhotoMode authority;PhotoModeAssignment unavailable{
  "m_skyboxOverride",PhotoModeDomain::Lighting,true};CHECK(!authority.applyPhotoModeField(unavailable).ok());
 authority.setTarget(&clock);authority.setAuthority(true);PcgPhotoModeValues before,after;
 after.setBool("m_skyboxOverride",true).value();after.setFloat("m_skyboxRotation",90.f).value();
 after.setFloat("m_skyboxExposure",2.f).value();after.setColor("m_skyboxTint",0.5f,0.25f,1.f,1.f).value();
 PcgPhotoModeApplyPlan plan;REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),4u);
 REQUIRE(plan.executeRegistered().ok());clock.update(0.f,graphics);const auto state=clock.getPcgSkybox();
 CHECK(state.enabled);CHECK_EQ(state.rotationDegrees,90.f);CHECK_EQ(state.exposure,2.f);
 CHECK_EQ(state.tintRed,0.5f);CHECK(clock.getSkyG()<clock.getSkyB());
 const bool skyChanged=std::abs(clock.getSkyR()-baseR)>0.0001f||
                       std::abs(clock.getSkyG()-baseG)>0.0001f;CHECK(skyChanged);
 clock.update(1.f,graphics);CHECK_EQ(clock.getPcgSkybox().rotationDegrees,90.f);
 PhotoModeAssignment disable{"m_skyboxOverride",PhotoModeDomain::Lighting,false};
 REQUIRE(authority.applyPhotoModeField(disable).ok());clock.update(0.f,graphics);
 CHECK(std::abs(clock.getSkyR()-baseR)<0.0001f);CHECK(std::abs(clock.getSkyG()-baseG)<0.0001f);
 CHECK(std::abs(clock.getSkyB()-baseB)<0.0001f);PhotoModeAssignment invalid{
  "m_skyboxExposure",PhotoModeDomain::Lighting,31.f};CHECK(!authority.applyPhotoModeField(invalid).ok());
 CHECK_EQ(clock.getPcgSkybox().exposure,2.f);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingFogAuthorityDrivesFogAndDensityVolume"){
 cap::detail::clearAllRaw();daynight::DayNight clock;daynight::PcgLightingFogPhotoMode authority;
 PhotoModeAssignment unavailable{"m_fogOverride",PhotoModeDomain::Lighting,true};
 CHECK(!authority.applyPhotoModeField(unavailable).ok());authority.setTarget(&clock);authority.setAuthority(true);
 PcgPhotoModeValues before,after;after.setFloat("m_pcgAdditionalLinearFog",100.f).value();
 after.setFloat("m_pcgAdditionalExponentialFog",0.02f).value();after.setBool("m_fogOverride",true).value();
 after.setInt("m_fogMode",2).value();after.setColor("m_fogColor",0.2f,0.3f,0.4f,1.f).value();
 after.setFloat("m_fogDensity",0.03f).value();after.setFloat("m_fogStart",50.f).value();
 after.setFloat("m_fogEnd",900.f).value();after.setFloat("m_globalFogDensityMultiplier",2.f).value();
 after.setBool("m_overrideDensityVolume",true).value();
 after.setColor("m_densityVolumeAlbedoColor",0.6f,0.7f,0.8f,1.f).value();
 after.setFloat("m_densityVolumeFogDistance",500.f).value();after.setInt("m_densityVolumeEffectType",4).value();
 after.setInt("m_densityVolumeTilingResolution",5).value();PcgPhotoModeApplyPlan plan;
 REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),14u);REQUIRE(plan.executeRegistered().ok());
 auto* graphics=graphics::parity_test::headlessGraphics();graphics::Volumetric fog(graphics);fog.setMode("raymarch");
 fog.setQuality("low");fog.setDensity(0.12f);const float baselineDensity=fog.getFloat("density");
 clock.applyAtmosphere(&fog);CHECK_EQ(fog.getMode(),std::string("fog"));CHECK_EQ(fog.getQuality(),std::string("high"));
 CHECK(std::abs(fog.getFloat("fogR")-0.6f)<0.0001f);CHECK(std::abs(fog.getFloat("density")-0.08f)<0.0001f);
 CHECK(std::abs(fog.getFloat("fogStart"))<0.0001f);CHECK(std::abs(fog.getFloat("fogEnd")-500.f)<0.0001f);
 PhotoModeAssignment noVolume{"m_overrideDensityVolume",PhotoModeDomain::Lighting,false};
 REQUIRE(authority.applyPhotoModeField(noVolume).ok());clock.applyAtmosphere(&fog);
 CHECK(std::abs(fog.getFloat("density")-0.15f)<0.0001f);CHECK(std::abs(fog.getFloat("fogStart")-150.f)<0.0001f);
 CHECK(std::abs(fog.getFloat("fogEnd")-1000.f)<0.0001f);
 REQUIRE(authority.applyPhotoModeField({"m_fogOverride",PhotoModeDomain::Lighting,false}).ok());
 REQUIRE(authority.applyPhotoModeField({"m_pcgAdditionalLinearFog",PhotoModeDomain::Lighting,0.f}).ok());
 REQUIRE(authority.applyPhotoModeField({"m_pcgAdditionalExponentialFog",PhotoModeDomain::Lighting,0.f}).ok());
 clock.applyAtmosphere(&fog);CHECK_EQ(fog.getMode(),std::string("raymarch"));CHECK_EQ(fog.getQuality(),std::string("low"));
 CHECK(std::abs(fog.getFloat("density")-baselineDensity)<0.0001f);
 PhotoModeAssignment invalid{"m_densityVolumeEffectType",PhotoModeDomain::Lighting,int64_t{5}};
 CHECK(!authority.applyPhotoModeField(invalid).ok());CHECK_EQ(clock.getPcgFog().densityVolumeEffect,4);
}
TEST_CASE("ui.pcgPhotoModeApply.lightingAmbientAuthorityDrivesGradientAndSunMultiplier"){
 cap::detail::clearAllRaw();daynight::DayNight clock;daynight::PcgLightingAmbientPhotoMode authority;
 CHECK(!authority.applyPhotoModeField({"m_ambientIntensity",PhotoModeDomain::Lighting,2.f}).ok());
 authority.setTarget(&clock);authority.setAuthority(true);PcgPhotoModeValues before,after;
 after.setFloat("m_ambientIntensity",2.f).value();after.setColor("m_ambientSkyColor",1.f,0.f,0.f,1.f).value();
 after.setColor("m_ambientEquatorColor",0.f,1.f,0.f,1.f).value();
 after.setColor("m_ambientGroundColor",0.f,0.f,1.f,1.f).value();
 after.setFloat("m_globalLightIntensityMultiplier",3.f).value();PcgPhotoModeApplyPlan plan;
 REQUIRE(plan.compile(before,after).ok());REQUIRE_EQ(plan.getCommandCount(),5u);REQUIRE(plan.executeRegistered().ok());
 CHECK(std::abs(clock.getAmbientR()-1.f)<0.0001f);CHECK(std::abs(clock.getAmbientG()-0.7f)<0.0001f);
 CHECK(std::abs(clock.getAmbientB()-0.3f)<0.0001f);auto* graphics=graphics::parity_test::headlessGraphics();
 clock.setTimeOfDay(12.f);clock.setPaused(true);clock.update(0.f,graphics);
 CHECK(clock.getSunR()>2.9f);const auto state=clock.getPcgAmbientLight();CHECK(state.active);
 CHECK_EQ(state.globalLightMultiplier,3.f);PhotoModeAssignment invalid{
  "m_globalLightIntensityMultiplier",PhotoModeDomain::Lighting,5.1f};
 CHECK(!authority.applyPhotoModeField(invalid).ok());CHECK_EQ(clock.getPcgAmbientLight().globalLightMultiplier,3.f);
 authority.setAuthority(false);CHECK(!clock.getPcgAmbientLight().active);CHECK(clock.getAmbientR()<1.f);
}
