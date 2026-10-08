#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogTypes.h"

namespace eve::graphics::fog {

/**
 * @brief Post-physics stylization layer.
 *
 * Consumes FogRayResult only. Never writes density, velocity, extinction, or
 * transmittance — art parameters are presentation-only.
 */
class EVENGINE_API_WORLD ContinuousArt {
public:
    [[nodiscard]] const ContinuousArtParams& params() const noexcept { return params_; }

    /** @brief Replace art parameters after validation. */
    [[nodiscard]] Result<void> setParams(const ContinuousArtParams& params);

    /**
     * @brief Stylize a physical fog integration result.
     * @param physical Optical ray result from FogRayMarch / froxel sample.
     * @param viewZ Linear view depth used for edge/silhouette cues.
     * @param neighborOpacity Optional neighbor opacity for edge boost (0..1).
     */
    [[nodiscard]] ContinuousArtOutput stylize(const FogRayResult& physical, float viewZ,
                                              float neighborOpacity = 0.f) const noexcept;

private:
    ContinuousArtParams params_{};
};

}  // namespace eve::graphics::fog
