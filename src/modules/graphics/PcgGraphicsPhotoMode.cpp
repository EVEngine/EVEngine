#include "graphics/PcgGraphicsPhotoMode.h"
#include "common/Capability.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Graphics.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include <cmath>
namespace eve::graphics{
PcgGraphicsPhotoModeAuthority::~PcgGraphicsPhotoModeAuthority(){if(authority_)cap::removeListener<IPhotoModeFieldSink>(this);}
void PcgGraphicsPhotoModeAuthority::setTargets(Graphics*g,RenderControl*r,Camera3D*c){graphics_=g;control_=r;camera_=c;project();}
void PcgGraphicsPhotoModeAuthority::setAuthority(bool e){if(e==authority_)return;if(e)cap::addListener<IPhotoModeFieldSink>(this);else cap::removeListener<IPhotoModeFieldSink>(this);authority_=e;}
PhotoModeFieldAcceptance PcgGraphicsPhotoModeAuthority::acceptsPhotoModeField(const PhotoModeAssignment&a)const noexcept{return a.domain==PhotoModeDomain::Graphics||(a.domain==PhotoModeDomain::Lighting&&a.field=="m_globalShadowDistanceMultiplier")?PhotoModeFieldAcceptance::Accepted:PhotoModeFieldAcceptance::Rejected;}
Result<void> PcgGraphicsPhotoModeAuthority::applyPhotoModeField(const PhotoModeAssignment&a){auto fail=[&](const char*m){return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,m,a.field,{},"graphics.pcgPhotoMode"));};auto n=state_;auto f=std::get_if<float>(&a.value);auto i=std::get_if<int64_t>(&a.value);
 if(a.field=="m_lodBias"){if(!f||!std::isfinite(*f)||*f<=0)return fail("LOD bias must be positive and finite");n.lodBias=*f;}
 else if(a.field=="m_antiAliasing"){if(!i||*i<0||*i>3)return fail("anti-aliasing mode must be in [0,3]");n.antiAliasing=*i;}
 else if(a.field=="m_shadowDistance"){if(!f||!std::isfinite(*f)||*f<0)return fail("shadow distance must be finite and non-negative");n.shadowDistance=*f;}
 else if(a.field=="m_shadowResolution"){if(!i||*i<0||*i>3)return fail("shadow resolution must be in [0,3]");n.shadowResolution=*i;}
 else if(a.field=="m_shadowCascades"){if(!i||*i<0||*i>4)return fail("shadow cascades must be in [0,4]");n.shadowCascades=*i;}
 else if(a.field=="m_globalShadowDistanceMultiplier"){if(!f||!std::isfinite(*f)||*f<0||*f>5)return fail("global shadow distance multiplier must be in [0,5]");n.globalShadowDistanceMultiplier=*f;}
 else return fail("unsupported graphics photo-mode field");state_=n;project();return Result<void>::success();}
void PcgGraphicsPhotoModeAuthority::project(){if(camera_){camera_->data()->lodBias=state_.lodBias;const float shadowDistance=state_.shadowDistance*state_.globalShadowDistanceMultiplier;for(int l=0;l<32;++l)camera_->setShadowLayerCullDistance(l,shadowDistance);}if(!graphics_||!control_)return;
 auto*aa=graphics_->pipelineAntiAliasing();const int m=int(state_.antiAliasing);if(m==0){control_->disable("aa");control_->disable("taa");}else{control_->enable("aa");control_->enable(m==3?"taa":"aa");if(m!=3)control_->disable("taa");aa->setMode(m==2?"smaa":m==3?"taa":"fxaa");}control_->compile();}
}
