#include "ui/PcgPhotoModeValues.h"
#include "common/SquirrelBinding.h"
#include "common/Value.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <cmath>
#include <limits>
namespace eve::ui { namespace {
const std::vector<PcgPhotoModeField>& fields(){static const std::vector<PcgPhotoModeField> v={
        {"m_isUsingPcgLighting", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_selectedPcgLightingProfile", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{-1}}},
        {"m_lastSceneName", PcgPhotoModeValueType::String, PcgPhotoModeValue{std::string{}}},
        {"m_screenshotResolution", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{0}}},
        {"m_screenshotImageFormat", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{2}}},
        {"m_loadSavedSettings", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{true}},
        {"m_revertOnDisabled", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{true}},
        {"m_showFPS", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_showReticle", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_showRuleOfThirds", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_lodBias", PcgPhotoModeValueType::Float, PcgPhotoModeValue{2.0f}},
        {"m_vSync", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{0}}},
        {"m_targetFPS", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{-1}}},
        {"m_antiAliasing", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{1}}},
        {"m_shadowDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{512.0f}},
        {"m_shadowResolution", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{3}}},
        {"m_shadowCascades", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{4}}},
        {"m_fieldOfView", PcgPhotoModeValueType::Float, PcgPhotoModeValue{60.0f}},
        {"m_pcgCullinDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_cameraAperture", PcgPhotoModeValueType::Float, PcgPhotoModeValue{19.0f}},
        {"m_cameraFocalLength", PcgPhotoModeValueType::Float, PcgPhotoModeValue{50.0f}},
        {"m_globalVolume", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_cameraRoll", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_farClipPlane", PcgPhotoModeValueType::Float, PcgPhotoModeValue{2000.0f}},
        {"m_pcgLoadRange", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1000.0f}},
        {"m_pcgImpostorRange", PcgPhotoModeValueType::Float, PcgPhotoModeValue{2000.0f}},
        {"m_pcgWeatherEnabled", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_pcgWeatherRain", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_pcgWeatherSnow", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_pcgWindSettingsOverride", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_pcgWindDirection", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_pcgWindSpeed", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.35f}},
        {"m_pcgTime", PcgPhotoModeValueType::Float, PcgPhotoModeValue{15.0f}},
        {"m_pcgTimeOfDayEnabled", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_pcgTimeScale", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_pcgAdditionalLinearFog", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_pcgAdditionalExponentialFog", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_sunRotation", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_sunPitch", PcgPhotoModeValueType::Float, PcgPhotoModeValue{75.0f}},
        {"m_fogOverride", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_fogMode", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{1}}},
        {"m_fogColor", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{0,0,0,1}}},
        {"m_fogDensity", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.01f}},
        {"m_fogStart", PcgPhotoModeValueType::Float, PcgPhotoModeValue{100.0f}},
        {"m_fogEnd", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1000.0f}},
        {"m_skyboxOverride", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_skyboxRotation", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_skyboxExposure", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_skyboxTint", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{0,0,0,1}}},
        {"m_sunOverride", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_sunIntensity", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_sunColor", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{0,0,0,1}}},
        {"m_sunKelvinValue", PcgPhotoModeValueType::Float, PcgPhotoModeValue{6500.0f}},
        {"m_ambientIntensity", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_ambientSkyColor", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{0.7027151f,0.881016f,1.001631f,0.4192761f}}},
        {"m_ambientEquatorColor", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{0.6302439f,0.7919513f,0.85f,0.0f}}},
        {"m_ambientGroundColor", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{0.5f,0.4142857f,0.3321428f,0.0f}}},
        {"m_globalLightIntensityMultiplier", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_globalFogDensityMultiplier", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_globalShadowDistanceMultiplier", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_pcgWaterReflectionEnabled", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{true}},
        {"m_pcgWaterReflectionDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_pcgWaterReflectionResolution", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{2}}},
        {"m_pcgReflectionsLODBias", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_pcgUnderwaterFogColor", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{0,0,0,1}}},
        {"m_pcgUnderwaterFogDensity", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.045f}},
        {"m_pcgUnderwaterFogDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{45.0f}},
        {"m_pcgUnderwaterVolume", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.5f}},
        {"m_postFXExposure", PcgPhotoModeValueType::Float, PcgPhotoModeValue{13.5f}},
        {"m_postFXExposureMode", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{0}}},
        {"m_dofActive", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{true}},
        {"m_autoDOFFocus", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{true}},
        {"m_dofFocusDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{100.0f}},
        {"m_dofAperture", PcgPhotoModeValueType::Float, PcgPhotoModeValue{16.0f}},
        {"m_dofFocalLength", PcgPhotoModeValueType::Float, PcgPhotoModeValue{50.0f}},
        {"m_dofKernelSize", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{1}}},
        {"m_savedDofFocusMode", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{0}}},
        {"m_dofFocusModeHDRP", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{0}}},
        {"m_dofFocusModeURP", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{0}}},
        {"m_dofQualityHDRP", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{1}}},
        {"m_dofNearBlurStart", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.0f}},
        {"m_dofNearBlurEnd", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.1f}},
        {"m_dofFarBlurStart", PcgPhotoModeValueType::Float, PcgPhotoModeValue{200.0f}},
        {"m_dofFarBlurEnd", PcgPhotoModeValueType::Float, PcgPhotoModeValue{2000.0f}},
        {"m_dofStartBlurURP", PcgPhotoModeValueType::Float, PcgPhotoModeValue{2.0f}},
        {"m_dofEndBlurURP", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1000.0f}},
        {"m_dofMaxRadiusBlur", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_dofHighQualityURP", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_terrainDetailDensity", PcgPhotoModeValueType::Float, PcgPhotoModeValue{0.5f}},
        {"m_terrainDetailDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{150.0f}},
        {"m_terrainPixelError", PcgPhotoModeValueType::Float, PcgPhotoModeValue{5.0f}},
        {"m_terrainBasemapDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1024.0f}},
        {"m_drawInstanced", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{true}},
        {"m_overrideDensityVolume", PcgPhotoModeValueType::Bool, PcgPhotoModeValue{false}},
        {"m_densityVolumeAlbedoColor", PcgPhotoModeValueType::Color, PcgPhotoModeValue{PcgPhotoModeColor{1,1,1,1}}},
        {"m_densityVolumeFogDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{250.0f}},
        {"m_densityVolumeEffectType", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{1}}},
        {"m_densityVolumeTilingResolution", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{3}}},
        {"m_globalGrassDensity", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_globalGrassDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_cameraCellDistance", PcgPhotoModeValueType::Float, PcgPhotoModeValue{1.0f}},
        {"m_cameraCellSubdivision", PcgPhotoModeValueType::Int, PcgPhotoModeValue{int64_t{0}}},
    };return v;}
template<class T> Result<T> bad(const std::string&m){return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,m,{}, {},"ui.pcgPhotoModeValues"));}
const PcgPhotoModeField* field(const std::string&n){for(const auto&f:fields())if(f.name==n)return &f;return nullptr;}
template<class T> Result<void> setTyped(std::unordered_map<std::string,PcgPhotoModeValue>&v,const std::string&n,PcgPhotoModeValueType t,T x){auto*f=field(n);if(!f)return bad<void>("unknown photo-mode field: "+n);if(f->type!=t)return bad<void>("photo-mode field type mismatch: "+n);v[n]=std::move(x);return Result<void>::success();}
template<class T> Result<T> getTyped(const std::unordered_map<std::string,PcgPhotoModeValue>&v,const std::string&n,PcgPhotoModeValueType t){auto*f=field(n);if(!f)return bad<T>("unknown photo-mode field: "+n);if(f->type!=t)return bad<T>("photo-mode field type mismatch: "+n);return Result<T>::success(std::get<T>(v.at(n)));}
}
PcgPhotoModeValues::PcgPhotoModeValues(){resetDefaults();}
void PcgPhotoModeValues::resetDefaults(){std::unordered_map<std::string,PcgPhotoModeValue> next;for(const auto&f:fields())next.emplace(f.name,f.defaultValue);values_=std::move(next);selectedColor_={};}
uint64_t PcgPhotoModeValues::getFieldCount()const noexcept{return fields().size();}
Result<std::string> PcgPhotoModeValues::getFieldName(uint64_t i)const{if(i>=fields().size())return bad<std::string>("photo-mode field index is out of range");return Result<std::string>::success(fields()[i].name);}
Result<int> PcgPhotoModeValues::getFieldType(const std::string&n)const{auto*f=field(n);if(!f)return bad<int>("unknown photo-mode field: "+n);return Result<int>::success(static_cast<int>(f->type));}
Result<void> PcgPhotoModeValues::setBool(const std::string&n,bool v){return setTyped(values_,n,PcgPhotoModeValueType::Bool,v);}
Result<void> PcgPhotoModeValues::setInt(const std::string&n,int64_t v){return setTyped(values_,n,PcgPhotoModeValueType::Int,v);}
Result<void> PcgPhotoModeValues::setFloat(const std::string&n,float v){if(!std::isfinite(v))return bad<void>("photo-mode float must be finite");return setTyped(values_,n,PcgPhotoModeValueType::Float,v);}
Result<void> PcgPhotoModeValues::setString(const std::string&n,const std::string&v){return setTyped(values_,n,PcgPhotoModeValueType::String,v);}
Result<void> PcgPhotoModeValues::setColor(const std::string&n,float r,float g,float b,float a){if(!std::isfinite(r)||!std::isfinite(g)||!std::isfinite(b)||!std::isfinite(a))return bad<void>("photo-mode color must be finite");return setTyped(values_,n,PcgPhotoModeValueType::Color,PcgPhotoModeColor{r,g,b,a});}
Result<bool> PcgPhotoModeValues::getBool(const std::string&n)const{return getTyped<bool>(values_,n,PcgPhotoModeValueType::Bool);}
Result<int64_t> PcgPhotoModeValues::getInt(const std::string&n)const{return getTyped<int64_t>(values_,n,PcgPhotoModeValueType::Int);}
Result<float> PcgPhotoModeValues::getFloat(const std::string&n)const{return getTyped<float>(values_,n,PcgPhotoModeValueType::Float);}
Result<std::string> PcgPhotoModeValues::getString(const std::string&n)const{return getTyped<std::string>(values_,n,PcgPhotoModeValueType::String);}
Result<PcgPhotoModeValue> PcgPhotoModeValues::getValue(const std::string&n)const{if(!field(n))return bad<PcgPhotoModeValue>("unknown photo-mode field: "+n);return Result<PcgPhotoModeValue>::success(values_.at(n));}
Result<void> PcgPhotoModeValues::selectColor(const std::string&n){auto r=getTyped<PcgPhotoModeColor>(values_,n,PcgPhotoModeValueType::Color);if(!r)return Result<void>::failure(*r.error());selectedColor_=r.value();return Result<void>::success();}
Result<std::string> PcgPhotoModeValues::snapshotJson() const {
    Value::Object encoded;
    for(const auto&f:fields()){const auto&v=values_.at(f.name);switch(f.type){
    case PcgPhotoModeValueType::Bool:encoded[f.name]=std::get<bool>(v);break;
    case PcgPhotoModeValueType::Int:encoded[f.name]=std::get<int64_t>(v);break;
    case PcgPhotoModeValueType::Float:encoded[f.name]=double(std::get<float>(v));break;
    case PcgPhotoModeValueType::String:encoded[f.name]=std::get<std::string>(v);break;
    case PcgPhotoModeValueType::Color:{auto c=std::get<PcgPhotoModeColor>(v);encoded[f.name]=Value::Array{double(c.r),double(c.g),double(c.b),double(c.a)};break;}}}
    return Value(Value::Object{{"schema","eve.ui.pcg-photo-mode-values"},{"values",std::move(encoded)},{"version",int64_t{1}}}).toJson();
}
Result<void> PcgPhotoModeValues::restoreJson(const std::string&json){
    if(json.size()>1024U*1024U)return bad<void>("photo-mode values JSON exceeds size limit");
    auto decoded=Value::fromJson(json);
    if(!decoded.ok())return Result<void>::failure(decoded.status());
    const auto*root=decoded.value().getIf<Value::Object>();if(!root||root->size()!=3||!root->contains("schema")||!root->contains("version")||!root->contains("values"))return bad<void>("photo-mode values has missing or unknown root fields");
    const auto*schema=root->at("schema").getIf<std::string>();const auto*version=root->at("version").getIf<int64_t>();const auto*object=root->at("values").getIf<Value::Object>();if(!schema||*schema!="eve.ui.pcg-photo-mode-values"||!version||*version!=1||!object||object->size()!=fields().size())return bad<void>("photo-mode values has unsupported schema, version or field count");
    PcgPhotoModeValues candidate;for(const auto&f:fields()){if(!object->contains(f.name))return bad<void>("photo-mode values is missing field: "+f.name);const auto&v=object->at(f.name);switch(f.type){
    case PcgPhotoModeValueType::Bool:{auto*x=v.getIf<bool>();if(!x)return bad<void>("invalid bool field: "+f.name);candidate.values_[f.name]=*x;break;}
    case PcgPhotoModeValueType::Int:{auto*x=v.getIf<int64_t>();if(!x)return bad<void>("invalid int field: "+f.name);candidate.values_[f.name]=*x;break;}
    case PcgPhotoModeValueType::Float:{auto*x=v.getIf<double>();if(!x||!std::isfinite(*x)||*x>std::numeric_limits<float>::max()||*x<-std::numeric_limits<float>::max())return bad<void>("invalid float field: "+f.name);candidate.values_[f.name]=float(*x);break;}
    case PcgPhotoModeValueType::String:{auto*x=v.getIf<std::string>();if(!x)return bad<void>("invalid string field: "+f.name);candidate.values_[f.name]=*x;break;}
    case PcgPhotoModeValueType::Color:{auto*x=v.getIf<Value::Array>();if(!x||x->size()!=4)return bad<void>("invalid color field: "+f.name);double c[4];for(int i=0;i<4;++i){auto*n=(*x)[i].getIf<double>();if(!n||!std::isfinite(*n))return bad<void>("invalid color component: "+f.name);c[i]=*n;}candidate.values_[f.name]=PcgPhotoModeColor{float(c[0]),float(c[1]),float(c[2]),float(c[3])};break;}}
    }values_.swap(candidate.values_);selectedColor_={};return Result<void>::success();
}
void exposePcgPhotoModeValuesBindings(ssq::Table&t){auto vm=t.getHandle();auto c=t.addClass("PcgPhotoModeValues",ssq::Class::Ctor<PcgPhotoModeValues()>());c.addFunc("resetDefaults",[](PcgPhotoModeValues*s){s->resetDefaults();});c.addFunc("getFieldCount",[](const PcgPhotoModeValues*s){return s->getFieldCount();});c.addFunc("getFieldName",[vm](const PcgPhotoModeValues*s,uint64_t i){return eve::script::projectResult(vm,s->getFieldName(i),[](const std::string&v){return v;});});c.addFunc("getFieldType",[vm](const PcgPhotoModeValues*s,const std::string&n){return eve::script::projectResult(vm,s->getFieldType(n),[](int v){return v;});});c.addFunc("setBool",[vm](PcgPhotoModeValues*s,const std::string&n,bool v){return eve::script::projectResult(vm,s->setBool(n,v));});c.addFunc("setInt",[vm](PcgPhotoModeValues*s,const std::string&n,int64_t v){return eve::script::projectResult(vm,s->setInt(n,v));});c.addFunc("setFloat",[vm](PcgPhotoModeValues*s,const std::string&n,float v){return eve::script::projectResult(vm,s->setFloat(n,v));});c.addFunc("setString",[vm](PcgPhotoModeValues*s,const std::string&n,const std::string&v){return eve::script::projectResult(vm,s->setString(n,v));});c.addFunc("setColor",[vm](PcgPhotoModeValues*s,const std::string&n,float r,float g,float b,float a){return eve::script::projectResult(vm,s->setColor(n,r,g,b,a));});c.addFunc("getBool",[vm](const PcgPhotoModeValues*s,const std::string&n){return eve::script::projectResult(vm,s->getBool(n),[](bool v){return v;});});c.addFunc("getInt",[vm](const PcgPhotoModeValues*s,const std::string&n){return eve::script::projectResult(vm,s->getInt(n),[](int64_t v){return v;});});c.addFunc("getFloat",[vm](const PcgPhotoModeValues*s,const std::string&n){return eve::script::projectResult(vm,s->getFloat(n),[](float v){return v;});});c.addFunc("getString",[vm](const PcgPhotoModeValues*s,const std::string&n){return eve::script::projectResult(vm,s->getString(n),[](const std::string&v){return v;});});c.addFunc("selectColor",[vm](PcgPhotoModeValues*s,const std::string&n){return eve::script::projectResult(vm,s->selectColor(n));});c.addFunc("snapshotJson",[vm](const PcgPhotoModeValues*s){return eve::script::projectResult(vm,s->snapshotJson(),[](const std::string&v){return v;});});c.addFunc("restoreJson",[vm](PcgPhotoModeValues*s,const std::string&v){return eve::script::projectResult(vm,s->restoreJson(v));});c.addFunc("getColorR",[](const PcgPhotoModeValues*s){return s->getColorR();});c.addFunc("getColorG",[](const PcgPhotoModeValues*s){return s->getColorG();});c.addFunc("getColorB",[](const PcgPhotoModeValues*s){return s->getColorB();});c.addFunc("getColorA",[](const PcgPhotoModeValues*s){return s->getColorA();});}
}
