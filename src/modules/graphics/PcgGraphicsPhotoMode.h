#pragma once

#include "common/Export.h"
#include "common/PcgPhotoModeApply.h"
namespace eve::graphics {
class Graphics;class RenderControl;class Camera3D;
/** @brief Pcg graphics quality values retained by the active render composition. */
struct PcgGraphicsPhotoModeState{float lodBias=2.f;int64_t antiAliasing=1;float shadowDistance=512.f;int64_t shadowResolution=3;int64_t shadowCascades=4;float globalShadowDistanceMultiplier=1.f;};
/** @brief Explicit owner that projects Pcg graphics fields into the live render pipeline. */
class EVENGINE_API_BACKENDS PcgGraphicsPhotoModeAuthority final:public IPhotoModeFieldSink{
public:
 ~PcgGraphicsPhotoModeAuthority()override;
 /** @brief Bind borrowed owner-thread render targets; none are retained past this authority's lifetime. */
 void setTargets(Graphics* graphics,RenderControl* control,Camera3D* camera);
 /** @brief Register or revoke the unique graphics photo-mode authority. */void setAuthority(bool enabled);
 /** @brief Return the current complete graphics quality state. */const PcgGraphicsPhotoModeState& state()const noexcept{return state_;}
 PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment&)const noexcept override;
 [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment&)override;
private:void project();Graphics* graphics_=nullptr;RenderControl* control_=nullptr;Camera3D* camera_=nullptr;bool authority_=false;PcgGraphicsPhotoModeState state_{};
};
}
