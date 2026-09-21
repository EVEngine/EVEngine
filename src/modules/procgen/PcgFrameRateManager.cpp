#include "procgen/PcgFrameRateManager.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <array>
#include <cmath>
namespace eve::procgen { namespace {
template<class T> Result<T> bad(const char* m,const char* p){return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,m,p));}
constexpr std::array<PcgTerrainQualityPreset,6> k{{
 {250,30,5,5,40,.1f,20,1,100},{500,50,10,10,50,.25f,10,1,250},{650,75,25,20,60,.4f,8,0,500},
 {800,100,40,30,80,.7f,5,0,800},{1000,150,50,50,120,1,5,0,1000},{2000,200,50,100,150,1,5,0,1000}}};
}
Result<void> PcgFrameRateManager::configure(int target,float interval,int minimum,int maximum,int current){
 if(target<=0||!std::isfinite(interval)||interval<=0||minimum<0||maximum>=int(k.size())||minimum>maximum||current<minimum||current>maximum)return bad<void>("invalid frame-rate manager configuration","procgen.frameRate.configure");
 target_=target;interval_=interval;min_=minimum;max_=maximum;quality_=current;left_=interval;fpsLeft_=.5f;accum_=fps_=0;frames_=0;changed_=false;return Result<void>::success();}
Result<int> PcgFrameRateManager::update(float dt,float scale){
 if(!std::isfinite(dt)||dt<=0||!std::isfinite(scale)||scale<0)return bad<int>("dt must be positive and timeScale non-negative","procgen.frameRate.update");
 changed_=false;fpsLeft_-=dt;accum_+=scale/dt;++frames_;if(fpsLeft_<=0){fps_=accum_/float(frames_);fpsLeft_=.5f;accum_=0;frames_=0;}
 if(automatic_){left_-=dt;if(left_<0){int old=quality_;if(fps_+10>=target_&&quality_<max_)++quality_;else if(fps_-10<=target_&&quality_>min_)--quality_;changed_=old!=quality_;left_=interval_;}}
 return Result<int>::success(quality_);}
Result<void> PcgFrameRateManager::selectManualQuality(int q){if(q<min_||q>max_)return bad<void>("quality is outside configured range","procgen.frameRate.quality");changed_=q!=quality_;quality_=q;automatic_=false;return Result<void>::success();}
PcgTerrainQualityPreset PcgFrameRateManager::getPreset()const{return k[size_t(quality_)];}
void exposePcgFrameRateManagerBindings(ssq::Table& t){auto p=t.addClass("PcgTerrainQualityPreset",ssq::Class::Ctor<PcgTerrainQualityPreset()>());
 p.addVar("treeDistance",&PcgTerrainQualityPreset::treeDistance);p.addVar("treeBillboardDistance",&PcgTerrainQualityPreset::treeBillboardDistance);p.addVar("treeCrossFadeLength",&PcgTerrainQualityPreset::treeCrossFadeLength);p.addVar("treeMaximumFullLodCount",&PcgTerrainQualityPreset::treeMaximumFullLodCount);p.addVar("detailObjectDistance",&PcgTerrainQualityPreset::detailObjectDistance);p.addVar("detailObjectDensity",&PcgTerrainQualityPreset::detailObjectDensity);p.addVar("heightmapPixelError",&PcgTerrainQualityPreset::heightmapPixelError);p.addVar("heightmapMaximumLod",&PcgTerrainQualityPreset::heightmapMaximumLod);p.addVar("basemapDistance",&PcgTerrainQualityPreset::basemapDistance);
 auto m=t.addClass("PcgFrameRateManager",ssq::Class::Ctor<PcgFrameRateManager()>());auto vm=t.getHandle();m.addFunc("configure",[vm](PcgFrameRateManager*s,int a,float b,int c,int d,int e){return eve::script::projectResult(vm,s->configure(a,b,c,d,e));});m.addFunc("update",[vm](PcgFrameRateManager*s,float d,float scale){return eve::script::projectResult(vm,s->update(d,scale),[](int q){return eve::Value(q);});});m.addFunc("setAutomatic",[](PcgFrameRateManager*s,bool v){s->setAutomatic(v);});m.addFunc("getAutomatic",[](const PcgFrameRateManager*s){return s->getAutomatic();});m.addFunc("selectManualQuality",[vm](PcgFrameRateManager*s,int q){return eve::script::projectResult(vm,s->selectManualQuality(q));});m.addFunc("getQuality",[](const PcgFrameRateManager*s){return s->getQuality();});m.addFunc("getFps",[](const PcgFrameRateManager*s){return s->getFps();});m.addFunc("getQualityChanged",[](const PcgFrameRateManager*s){return s->getQualityChanged();});m.addFunc("getPreset",&PcgFrameRateManager::getPreset);}
}
