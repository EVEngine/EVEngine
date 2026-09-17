#include "ui/PcgPhotoModeRanges.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace eve::ui { namespace {
const std::vector<std::pair<std::string,PcgPhotoModeRange>>&defaults(){static const std::vector<std::pair<std::string,PcgPhotoModeRange>>v={
{"m_0To1",{0.0,1.0,false}},
{"m_lodBias",{0.001,10.0,false}},
{"m_targetFPS",{-1.0,240.0,true}},
{"m_shadowDistance",{0.0,5000.0,false}},
{"m_fieldOfView",{1.0,115.0,false}},
{"m_pcgCullinDistance",{-10000.0,10000.0,false}},
{"m_cameraAperture",{0.1,32.0,false}},
{"m_cameraFocalLength",{0.05,250.0,false}},
{"m_globalVolume",{0.0,1.0,false}},
{"m_cameraRoll",{-90.0,90.0,false}},
{"m_pcgLoadRange",{0.0,5000.0,false}},
{"m_pcgImpostorRange",{0.0,5000.0,false}},
{"m_pcgWindDirection",{0.0,1.0,false}},
{"m_pcgWindSpeed",{0.0,1.0,false}},
{"m_pcgTime",{0.0,24.0,false}},
{"m_pcgTimeScale",{0.0,200.0,false}},
{"m_pcgAdditionalLinearFog",{-5000.0,5000.0,false}},
{"m_pcgAdditionalExponentialFog",{0.0,0.05,false}},
{"m_sunRotation",{0.0,360.0,false}},
{"m_sunPitch",{0.0,360.0,false}},
{"m_fogDensity",{0.0,0.05,false}},
{"m_fogStart",{0.0,5000.0,false}},
{"m_fogEnd",{0.0,5000.0,false}},
{"m_fogEndHDRP",{1.0,5000.0,false}},
{"m_skyboxRotation",{0.0,360.0,false}},
{"m_skyboxExposure",{0.0,4.0,false}},
{"m_skyboxExposureHDRP",{0.0,30.0,false}},
{"m_ambientIntensity",{0.0,10.0,false}},
{"m_sunIntensity",{0.0,8.0,false}},
{"m_sunIntensityHDRP",{0.0,250000.0,false}},
{"m_sunKelvinValue",{1500.0,20000.0,false}},
{"m_densityVolumeFogDistance",{0.01,std::numeric_limits<double>::infinity(),false}},
{"m_pcgWaterReflectionDistance",{-5000.0,5000.0,false}},
{"m_pcgReflectionsLODBias",{0.0,10.0,false}},
{"m_pcgUnderwaterFogDistance",{0.1,200.0,false}},
{"m_pcgUnderwaterFogDensity",{0.0,1.0,false}},
{"m_postFXExposure",{0.0,5.0,false}},
{"m_postFXExposureURP",{-5.0,5.0,false}},
{"m_postFXExposureHDRP",{-5.0,15.0,false}},
{"m_postFXDOFFocusDistanceHDRP",{0.01,300.0,false}},
{"m_postFXDOFFocusDistanceURP",{0.01,300.0,false}},
{"m_postFXDOFFocusDistance",{0.01,200.0,false}},
{"m_postFXDOFNearBlurStart",{0.01,400.0,false}},
{"m_postFXDOFNearBlurEnd",{0.1,5000.0,false}},
{"m_postFXDOFFarBlurStart",{0.01,400.0,false}},
{"m_postFXDOFFarBlurEnd",{0.1,5000.0,false}},
{"m_postFXDOFGaussianBlurStartURP",{0.01,250.0,false}},
{"m_postFXDOFGaussianBlurEndURP",{0.1,5000.0,false}},
{"m_postFXDOFGaussianBlurMaxRadiusURP",{0.5,1.5,false}},
{"m_terrainDetailDensity",{0.0,1.0,false}},
{"m_terrainDetailDistance",{0.0,1000.0,false}},
{"m_terrainPixelError",{1.0,200.0,false}},
{"m_terrainBasemapDistance",{0.0,20000.0,false}},
{"m_globalGrassDensity",{0.01,4.0,false}},
{"m_globalGrassDistance",{0.01,4.0,false}},
{"m_cameraCellDistance",{0.01,4.0,false}},
{"m_cameraCellSubdivision",{-8.0,8.0,true}},
};return v;}
template<class T>Result<T>bad(const std::string&m){return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,m,{}, {},"ui.pcgPhotoModeRanges"));}
}
PcgPhotoModeRanges::PcgPhotoModeRanges(){resetDefaults();}void PcgPhotoModeRanges::resetDefaults(){std::unordered_map<std::string,PcgPhotoModeRange>n;for(const auto&v:defaults())n.emplace(v);ranges_=std::move(n);selected_={};}uint64_t PcgPhotoModeRanges::getCount()const noexcept{return defaults().size();}
Result<std::string> PcgPhotoModeRanges::getName(uint64_t i)const{if(i>=defaults().size())return bad<std::string>("photo-mode range index is out of bounds");return Result<std::string>::success(defaults()[i].first);}
Result<void> PcgPhotoModeRanges::select(const std::string&n){auto i=ranges_.find(n);if(i==ranges_.end())return bad<void>("unknown photo-mode range: "+n);selected_=i->second;return Result<void>::success();}
Result<void> PcgPhotoModeRanges::setRange(const std::string&n,double lo,double hi){auto i=ranges_.find(n);if(i==ranges_.end())return bad<void>("unknown photo-mode range: "+n);if(!std::isfinite(lo)||std::isnan(hi)||hi<lo)return bad<void>("photo-mode range endpoints are invalid");if(i->second.integral&&(std::trunc(lo)!=lo||std::trunc(hi)!=hi))return bad<void>("integer photo-mode range requires integral endpoints");i->second.minimum=lo;i->second.maximum=hi;return Result<void>::success();}
Result<double> PcgPhotoModeRanges::clamp(const std::string&n,double v)const{auto i=ranges_.find(n);if(i==ranges_.end())return bad<double>("unknown photo-mode range: "+n);if(!std::isfinite(v))return bad<double>("photo-mode clamp value must be finite");return Result<double>::success(std::clamp(v,i->second.minimum,i->second.maximum));}
void exposePcgPhotoModeRangesBindings(ssq::Table&t){auto vm=t.getHandle();auto c=t.addClass("PcgPhotoModeRanges",ssq::Class::Ctor<PcgPhotoModeRanges()>());c.addFunc("resetDefaults",[](PcgPhotoModeRanges*s){s->resetDefaults();});c.addFunc("getCount",[](const PcgPhotoModeRanges*s){return s->getCount();});c.addFunc("getName",[vm](const PcgPhotoModeRanges*s,uint64_t i){return eve::script::projectResult(vm,s->getName(i),[](const std::string&v){return v;});});c.addFunc("select",[vm](PcgPhotoModeRanges*s,const std::string&n){return eve::script::projectResult(vm,s->select(n));});c.addFunc("setRange",[vm](PcgPhotoModeRanges*s,const std::string&n,double a,double b){return eve::script::projectResult(vm,s->setRange(n,a,b));});c.addFunc("clamp",[vm](const PcgPhotoModeRanges*s,const std::string&n,double v){return eve::script::projectResult(vm,s->clamp(n,v),[](double x){return x;});});c.addFunc("getMinimum",[](const PcgPhotoModeRanges*s){return s->getMinimum();});c.addFunc("getMaximum",[](const PcgPhotoModeRanges*s){return s->getMaximum();});c.addFunc("getIntegral",[](const PcgPhotoModeRanges*s){return s->getIntegral();});}
}
