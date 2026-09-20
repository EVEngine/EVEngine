#pragma once
#include "common/PcgPhotoModeApply.h"
#include "graphics/WaterPlanarReflection.h"
#include "graphics/WaterUnderwaterEffects.h"
namespace ssq { class Table; }
namespace eve::graphics {
/** @brief Explicit, caller-owned authority for Pcg photo-mode water reflection and underwater fog values. */
class EVENGINE_API_BACKENDS PcgWaterPhotoModeAuthority final:public IPhotoModeFieldSink {
public:
 PcgWaterPhotoModeAuthority()=default;
 ~PcgWaterPhotoModeAuthority()override;
 /** @brief Register or revoke this object as the unique water-field provider. */
 void setAuthority(bool enabled);
 /** @brief Return whether the authority lease is active. */ bool getAuthority()const noexcept{return authority_;}
 /** @brief Return whether this provider owns the assignment. */
 PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment)const noexcept override;
 /** @brief Apply one Water-domain assignment atomically. */
 [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment)override;
 /** @brief Copy reflection values into the real planar-reflection planner settings. */
 [[nodiscard]] Result<void> applyToPlanarSettings(WaterPlanarReflectionSettings& settings)const;
 /** @brief Copy fog values into the real underwater-effects settings. */
 [[nodiscard]] Result<void> applyToUnderwaterSettings(WaterUnderwaterSettings& settings)const;
 bool getReflectionEnabled()const noexcept{return reflectionEnabled_;}
 float getReflectionDistance()const noexcept{return reflectionDistance_;}
 int getReflectionResolution()const noexcept{return reflectionResolution_;}
 float getReflectionLodBias()const noexcept{return reflectionLodBias_;}
 float getFogDensity()const noexcept{return fogDensity_;}
 float getFogDistance()const noexcept{return fogDistance_;}
 float getFogRed()const noexcept{return fogColor_.r;}
 float getFogGreen()const noexcept{return fogColor_.g;}
 float getFogBlue()const noexcept{return fogColor_.b;}
private:
 bool authority_=false,reflectionEnabled_=true;float reflectionDistance_=0,reflectionLodBias_=1;
 int reflectionResolution_=2;PhotoModeColor fogColor_{0,0,0,1};float fogDensity_=0.045f,fogDistance_=45;
};
/** @brief Register Pcg water photo-mode authority bindings. */ void exposePcgWaterPhotoModeBindings(ssq::Table& table);
}
