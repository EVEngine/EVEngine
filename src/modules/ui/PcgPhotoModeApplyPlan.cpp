#include "ui/PcgPhotoModeApplyPlan.h"
#include "common/Capability.h"
#include "common/AudioQuery.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <unordered_set>

namespace eve::ui {
namespace {
template<class T> Result<T> fail(const std::string& message) {
 return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,message,{},{},"ui.pcgPhotoModeApply"));
}
PhotoModeDomain domainFor(const std::string& n) {
 static const std::unordered_set<std::string> system={"m_vSync","m_targetFPS"};
 static const std::unordered_set<std::string> graphics={"m_lodBias","m_antiAliasing","m_shadowDistance","m_shadowResolution","m_shadowCascades"};
 static const std::unordered_set<std::string> camera={"m_fieldOfView","m_pcgCullinDistance","m_cameraAperture","m_cameraFocalLength","m_cameraRoll","m_farClipPlane"};
 static const std::unordered_set<std::string> streaming={"m_pcgLoadRange","m_pcgImpostorRange"};
 static const std::unordered_set<std::string> audio={"m_globalVolume","m_pcgUnderwaterVolume"};
 if(system.contains(n))return PhotoModeDomain::System;
 if(graphics.contains(n))return PhotoModeDomain::Graphics;
 if(camera.contains(n))return PhotoModeDomain::Camera;
 if(streaming.contains(n))return PhotoModeDomain::Streaming;
 if(audio.contains(n))return PhotoModeDomain::Audio;
 if(n.starts_with("m_pcgWeather")||n.starts_with("m_pcgWind"))return PhotoModeDomain::Weather;
 if(n.starts_with("m_pcgWater")||n.starts_with("m_pcgReflections")||n.starts_with("m_pcgUnderwaterFog"))return PhotoModeDomain::Water;
 if(n.starts_with("m_postFX")||n.starts_with("m_dof")||n=="m_autoDOFFocus"||n=="m_savedDofFocusMode")return PhotoModeDomain::PostFx;
 if(n.starts_with("m_terrain")||n=="m_drawInstanced")return PhotoModeDomain::Terrain;
 if(n.starts_with("m_globalGrass")||n.starts_with("m_cameraCell"))return PhotoModeDomain::Grass;
 if(n=="m_isUsingPcgLighting"||n=="m_selectedPcgLightingProfile"||n.starts_with("m_pcgTime")||
    n.starts_with("m_pcgAdditional")||n.starts_with("m_sun")||n.starts_with("m_fog")||
    n.starts_with("m_skybox")||n.starts_with("m_ambient")||n.starts_with("m_globalLight")||
    n.starts_with("m_globalFog")||n.starts_with("m_globalShadow")||n.starts_with("m_overrideDensity")||
    n.starts_with("m_densityVolume"))return PhotoModeDomain::Lighting;
 return PhotoModeDomain::Photo;
}
class RegisteredPhotoModeSink final:public IPhotoModeApplySink {
public:
 Result<void> applyPhotoModeAssignment(const PhotoModeAssignment& assignment)override{
  if(assignment.field=="m_globalVolume"){
   auto* audio=cap::query<IAudioQuery>();
   if(!audio)return unsupported("audio capability is unavailable");
   auto* value=std::get_if<float>(&assignment.value);
   if(!value)return fail<void>("global volume requires a float value");
   audio->setVolume(*value);return Result<void>::success();
  }
  IPhotoModeFieldSink* selected=nullptr;
  const size_t count=cap::listenerCount<IPhotoModeFieldSink>();
  for(size_t i=0;i<count;++i){
   auto* candidate=cap::listenerAt<IPhotoModeFieldSink>(i);
   if(candidate&&candidate->acceptsPhotoModeField(assignment)==PhotoModeFieldAcceptance::Accepted){
    if(selected)return fail<void>("multiple photo-mode providers accept field: "+assignment.field);
    selected=candidate;
   }
  }
  if(!selected)return unsupported("no photo-mode provider accepts field: "+assignment.field);
  return selected->applyPhotoModeField(assignment);
 }
private:
 static Result<void> unsupported(const std::string& message){
  return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,message,{},{},"ui.pcgPhotoModeApply"));
 }
};
PhotoModeValue convert(const PcgPhotoModeValue& value) {
 return std::visit([](const auto& v)->PhotoModeValue {
  using T=std::decay_t<decltype(v)>;
  if constexpr(std::is_same_v<T,PcgPhotoModeColor>)return PhotoModeColor{v.r,v.g,v.b,v.a};
  else return v;
 },value);
}
}

Result<void> PcgPhotoModeApplyPlan::compile(const PcgPhotoModeValues& before,const PcgPhotoModeValues& after) {
 std::vector<Command> candidate;
 for(uint64_t i=0;i<before.getFieldCount();++i){
  auto name=before.getFieldName(i); if(!name.ok())return Result<void>::failure(name.status());
  auto a=before.getValue(name.value()); if(!a.ok())return Result<void>::failure(a.status());
  auto b=after.getValue(name.value()); if(!b.ok())return Result<void>::failure(b.status());
  if(a.value()!=b.value())candidate.push_back({name.value(),domainFor(name.value()),convert(a.value()),convert(b.value())});
 }
 commands_=std::move(candidate); appliedCount_=0; rollbackComplete_=true; ++revision_;
 return Result<void>::success();
}
Result<void> PcgPhotoModeApplyPlan::compileJson(const std::string& beforeJson,const std::string& afterJson) {
 PcgPhotoModeValues before,after;
 auto a=before.restoreJson(beforeJson);if(!a.ok())return Result<void>::failure(a.status());
 auto b=after.restoreJson(afterJson);if(!b.ok())return Result<void>::failure(b.status());
 return compile(before,after);
}
Result<void> PcgPhotoModeApplyPlan::execute(IPhotoModeApplySink& sink) {
 appliedCount_=0; rollbackComplete_=true;
 for(size_t i=0;i<commands_.size();++i){
  auto result=sink.applyPhotoModeAssignment({commands_[i].field,commands_[i].domain,commands_[i].after});
  if(!result.ok()){
   bool restored=true;
   for(size_t j=i;j>0;--j){
    auto rollback=sink.applyPhotoModeAssignment({commands_[j-1].field,commands_[j-1].domain,commands_[j-1].before});
    if(!rollback.ok())restored=false;
   }
   rollbackComplete_=restored;
   return fail<void>(restored?"photo-mode assignment failed; applied assignments were rolled back":
                              "photo-mode assignment failed and rollback was incomplete");
  }
  ++appliedCount_;
 }
 ++revision_; return Result<void>::success();
}
Result<void> PcgPhotoModeApplyPlan::executeRegistered(){
 if(auto* sink=cap::query<IPhotoModeApplySink>())return execute(*sink);
 RegisteredPhotoModeSink sink;return execute(sink);
}
Result<std::string> PcgPhotoModeApplyPlan::getCommandField(uint64_t index)const{
 if(index>=commands_.size())return fail<std::string>("photo-mode command index is out of range");
 return Result<std::string>::success(commands_[index].field);
}
Result<int> PcgPhotoModeApplyPlan::getCommandDomain(uint64_t index)const{
 if(index>=commands_.size())return fail<int>("photo-mode command index is out of range");
 return Result<int>::success(static_cast<int>(commands_[index].domain));
}
void exposePcgPhotoModeApplyPlanBindings(ssq::Table&t){
 auto vm=t.getHandle();auto c=t.addClass("PcgPhotoModeApplyPlan",ssq::Class::Ctor<PcgPhotoModeApplyPlan()>());
 c.addFunc("compileJson",[vm](PcgPhotoModeApplyPlan*s,const std::string&a,const std::string&b){return script::projectResult(vm,s->compileJson(a,b));});
 c.addFunc("executeRegistered",[vm](PcgPhotoModeApplyPlan*s){return script::projectResult(vm,s->executeRegistered());});
 c.addFunc("getCommandCount",[](const PcgPhotoModeApplyPlan*s){return s->getCommandCount();});
 c.addFunc("getCommandField",[vm](const PcgPhotoModeApplyPlan*s,uint64_t i){return script::projectResult(vm,s->getCommandField(i),[](const std::string&v){return v;});});
 c.addFunc("getCommandDomain",[vm](const PcgPhotoModeApplyPlan*s,uint64_t i){return script::projectResult(vm,s->getCommandDomain(i),[](int v){return v;});});
 c.addFunc("getAppliedCount",[](const PcgPhotoModeApplyPlan*s){return s->getAppliedCount();});
 c.addFunc("getRollbackComplete",[](const PcgPhotoModeApplyPlan*s){return s->getRollbackComplete();});
 c.addFunc("getRevision",[](const PcgPhotoModeApplyPlan*s){return s->getRevision();});
}
}
