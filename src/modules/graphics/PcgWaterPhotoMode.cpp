#include "graphics/PcgWaterPhotoMode.h"
#include "common/Capability.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <cmath>
namespace eve::graphics {
namespace { Result<void> bad(const std::string&m,const std::string&field={}){
 return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,m,field,{},"graphics.pcgWaterPhotoMode"));}}
PcgWaterPhotoModeAuthority::~PcgWaterPhotoModeAuthority(){if(authority_)cap::removeListener<IPhotoModeFieldSink>(this);}
void PcgWaterPhotoModeAuthority::setAuthority(bool enabled){if(enabled==authority_)return;if(enabled)cap::addListener<IPhotoModeFieldSink>(this);else cap::removeListener<IPhotoModeFieldSink>(this);authority_=enabled;}
PhotoModeFieldAcceptance PcgWaterPhotoModeAuthority::acceptsPhotoModeField(const PhotoModeAssignment&a)const noexcept{return a.domain==PhotoModeDomain::Water?PhotoModeFieldAcceptance::Accepted:PhotoModeFieldAcceptance::Rejected;}
Result<void> PcgWaterPhotoModeAuthority::applyPhotoModeField(const PhotoModeAssignment&a){
 if(a.domain!=PhotoModeDomain::Water)return bad("assignment is not a Water field",a.field);
 if(a.field=="m_pcgWaterReflectionEnabled"){auto*v=std::get_if<bool>(&a.value);if(!v)return bad("reflection enabled requires bool",a.field);reflectionEnabled_=*v;}
 else if(a.field=="m_pcgWaterReflectionDistance"){auto*v=std::get_if<float>(&a.value);if(!v||!std::isfinite(*v))return bad("reflection distance requires finite float",a.field);reflectionDistance_=*v;}
 else if(a.field=="m_pcgWaterReflectionResolution"){auto*v=std::get_if<int64_t>(&a.value);if(!v||*v<0||*v>3)return bad("reflection resolution requires 0..3",a.field);reflectionResolution_=int(*v);}
 else if(a.field=="m_pcgReflectionsLODBias"){auto*v=std::get_if<float>(&a.value);if(!v||!std::isfinite(*v)||*v<=0)return bad("reflection LOD bias must be positive",a.field);reflectionLodBias_=*v;}
 else if(a.field=="m_pcgUnderwaterFogColor"){auto*v=std::get_if<PhotoModeColor>(&a.value);if(!v||!std::isfinite(v->r)||!std::isfinite(v->g)||!std::isfinite(v->b)||!std::isfinite(v->a))return bad("underwater fog color must be finite",a.field);fogColor_=*v;}
 else if(a.field=="m_pcgUnderwaterFogDensity"){auto*v=std::get_if<float>(&a.value);if(!v||!std::isfinite(*v)||*v<0)return bad("underwater fog density must be nonnegative",a.field);fogDensity_=*v;}
 else if(a.field=="m_pcgUnderwaterFogDistance"){auto*v=std::get_if<float>(&a.value);if(!v||!std::isfinite(*v)||*v<0)return bad("underwater fog distance must be nonnegative",a.field);fogDistance_=*v;}
 else return bad("unsupported Water field",a.field);
 return Result<void>::success();
}
Result<void> PcgWaterPhotoModeAuthority::applyToPlanarSettings(WaterPlanarReflectionSettings&s)const{
 if(!std::isfinite(s.customRenderDistance)||s.customRenderDistance<0)return bad("base planar reflection distance is invalid");
 s.enabled=reflectionEnabled_;s.resolutionMultiplier=static_cast<WaterReflectionResolution>(reflectionResolution_);
 s.enableRenderDistance=true;s.customRenderDistance=std::max(0.f,s.customRenderDistance+reflectionDistance_);s.lodBias=reflectionLodBias_;
 return Result<void>::success();
}
Result<void> PcgWaterPhotoModeAuthority::applyToUnderwaterSettings(WaterUnderwaterSettings&s)const{
 s.photoModeFogColorEnabled=true;s.photoModeFogColor={fogColor_.r,fogColor_.g,fogColor_.b};
 s.fogDensity=fogDensity_;s.fogDistance=fogDistance_;s.hdrpFogDistance=fogDistance_;return Result<void>::success();
}
void exposePcgWaterPhotoModeBindings(ssq::Table&t){auto c=t.addClass("PcgWaterPhotoModeAuthority",ssq::Class::Ctor<PcgWaterPhotoModeAuthority()>());auto vm=t.getHandle();
 c.addFunc("setAuthority",[](PcgWaterPhotoModeAuthority*s,bool v){s->setAuthority(v);});c.addFunc("getAuthority",[](const PcgWaterPhotoModeAuthority*s){return s->getAuthority();});
 c.addFunc("applyToPlanarSettings",[vm](const PcgWaterPhotoModeAuthority*s,WaterPlanarReflectionSettings*v){return script::projectResult(vm,v?s->applyToPlanarSettings(*v):bad("planar settings are required"));});
 c.addFunc("applyToUnderwaterSettings",[vm](const PcgWaterPhotoModeAuthority*s,WaterUnderwaterSettings*v){return script::projectResult(vm,v?s->applyToUnderwaterSettings(*v):bad("underwater settings are required"));});
 c.addFunc("getReflectionEnabled",[](const PcgWaterPhotoModeAuthority*s){return s->getReflectionEnabled();});c.addFunc("getReflectionDistance",[](const PcgWaterPhotoModeAuthority*s){return s->getReflectionDistance();});
 c.addFunc("getReflectionResolution",[](const PcgWaterPhotoModeAuthority*s){return s->getReflectionResolution();});c.addFunc("getReflectionLodBias",[](const PcgWaterPhotoModeAuthority*s){return s->getReflectionLodBias();});
 c.addFunc("getFogDensity",[](const PcgWaterPhotoModeAuthority*s){return s->getFogDensity();});c.addFunc("getFogDistance",[](const PcgWaterPhotoModeAuthority*s){return s->getFogDistance();});
 c.addFunc("getFogRed",[](const PcgWaterPhotoModeAuthority*s){return s->getFogRed();});c.addFunc("getFogGreen",[](const PcgWaterPhotoModeAuthority*s){return s->getFogGreen();});c.addFunc("getFogBlue",[](const PcgWaterPhotoModeAuthority*s){return s->getFogBlue();});}
}
