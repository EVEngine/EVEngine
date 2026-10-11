#include "graphics/WaterReflectionMasker.h"

#include "common/SquirrelBinding.h"
#include "graphics/WaterPlanarReflection.h"
#include "image/ImageData.h"

#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::graphics {
namespace {
template<class T> Result<T> invalidMasker(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, {}, {},
                                                 "graphics.waterReflectionMasker"));
}
bool finite6(float a,float b,float c,float d,float e,float f) {
    return std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)&&std::isfinite(d)&&std::isfinite(e)&&std::isfinite(f);
}
}  // namespace

Result<void> WaterReflectionMasker::configure(WaterReflectionMaskChannel channel, float minimum, float maximum,
                                               bool reflectionsEnabled) {
    const int c=static_cast<int>(channel);
    if(c<0||c>4||!std::isfinite(minimum)||!std::isfinite(maximum)||minimum>maximum)
        return invalidMasker<void>("valid channel and finite ordered thresholds are required");
    channel_=channel;minimum_=minimum;maximum_=maximum;enabled_=reflectionsEnabled;
    heightFeaturesEnabled_=reflectionsEnabled;return Result<void>::success();
}

Result<void> WaterReflectionMasker::setMask(const image::ImageData& mask) {
    const int width=mask.getWidth(),height=mask.getHeight();
    if(width<=0||height<=0)return invalidMasker<void>("mask dimensions must be positive");
    std::vector<Pixel> next;next.reserve(static_cast<size_t>(width)*static_cast<size_t>(height));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto c=mask.getPixel(x,y);next.push_back({c.r,c.g,c.b,c.a});}
    width_=width;height_=height;pixels_=std::move(next);sampleX_=sampleY_=-1;return Result<void>::success();
}

void WaterReflectionMasker::clearMask() noexcept { width_=height_=0;pixels_.clear();sampleX_=sampleY_=-1; }

Result<WaterReflectionMaskTransition> WaterReflectionMasker::evaluate(float playerX,float playerZ,
    float terrainX,float terrainZ,float terrainWidth,float terrainDepth,WaterPlanarReflectionSettings& settings) {
    if(!finite6(playerX,playerZ,terrainX,terrainZ,terrainWidth,terrainDepth)||terrainWidth<=0.f||terrainDepth<=0.f)
        return invalidMasker<WaterReflectionMaskTransition>("finite player, terrain origin and positive terrain size are required");
    bool next=false;sampleX_=sampleY_=-1;
    const float u=(playerX-terrainX)/terrainWidth,v=(playerZ-terrainZ)/terrainDepth;
    if(!pixels_.empty()&&u>=0.f&&u<=1.f&&v>=0.f&&v<=1.f){
        const int x=static_cast<int>(std::lround(u*static_cast<float>(width_)));
        const int y=static_cast<int>(std::lround(v*static_cast<float>(height_)));
        sampleX_=x;sampleY_=y;
        if(x>=0&&x<width_&&y>=0&&y<height_){
            const auto&p=pixels_[static_cast<size_t>(y)*static_cast<size_t>(width_)+static_cast<size_t>(x)];
            const auto hit=[&](float value){return value>=minimum_&&value<=maximum_;};
            switch(channel_){case WaterReflectionMaskChannel::R:next=hit(p.r);break;case WaterReflectionMaskChannel::G:next=hit(p.g);break;case WaterReflectionMaskChannel::B:next=hit(p.b);break;case WaterReflectionMaskChannel::A:next=hit(p.a);break;case WaterReflectionMaskChannel::RGBA:next=hit(p.r)||hit(p.g)||hit(p.b)||hit(p.a);break;}
        }
    }
    if(next==enabled_)return Result<WaterReflectionMaskTransition>::success(WaterReflectionMaskTransition::Unchanged);
    enabled_=next;heightFeaturesEnabled_=next;settings.enabled=next;
    return Result<WaterReflectionMaskTransition>::success(next?WaterReflectionMaskTransition::Enabled:WaterReflectionMaskTransition::Disabled);
}

void exposeWaterReflectionMaskerBindings(ssq::Table& table){auto c=table.addClass("WaterReflectionMasker",ssq::Class::Ctor<WaterReflectionMasker()>());auto vm=table.getHandle();
 c.addFunc("configure",[vm](WaterReflectionMasker*s,int channel,float min,float max,bool enabled){return eve::script::projectResult(vm,s->configure(static_cast<WaterReflectionMaskChannel>(channel),min,max,enabled));});
 c.addFunc("setMask",[vm](WaterReflectionMasker*s,const image::ImageData* mask){auto r=mask?s->setMask(*mask):invalidMasker<void>("ImageData mask is required");return eve::script::projectResult(vm,std::move(r));});
 c.addFunc("clearMask",[](WaterReflectionMasker*s){s->clearMask();});
 c.addFunc("evaluate",[vm](WaterReflectionMasker*s,float px,float pz,float tx,float tz,float tw,float td,WaterPlanarReflectionSettings* settings){auto r=settings?s->evaluate(px,pz,tx,tz,tw,td,*settings):invalidMasker<WaterReflectionMaskTransition>("reflection settings are required");return eve::script::projectResult(vm,std::move(r),[](WaterReflectionMaskTransition v){return eve::Value(static_cast<int>(v));});});
 c.addFunc("getEnabled",[](const WaterReflectionMasker*s){return s->getEnabled();});c.addFunc("getHeightFeaturesEnabled",[](const WaterReflectionMasker*s){return s->getHeightFeaturesEnabled();});c.addFunc("getSampleX",[](const WaterReflectionMasker*s){return s->getSampleX();});c.addFunc("getSampleY",[](const WaterReflectionMasker*s){return s->getSampleY();});}

}  // namespace eve::graphics
