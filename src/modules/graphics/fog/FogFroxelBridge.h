#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/BeerLightCache.h"
#include "graphics/fog/FogDensityField.h"
#include "graphics/fog/FogProfile.h"
#include "graphics/fog/FogTypes.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace eve::graphics {
class AtmosphereVolume;
}

namespace eve::graphics::fog {

/**
 * @brief Maps world-space density into a camera froxel volume and integrates it.
 *
 * Occupancy skipping and temporal history follow the active FogQualityBudget.
 */
class EVENGINE_API_WORLD FogFroxelBridge {
public:
    /**
     * @brief Inject density-field media into an AtmosphereVolume froxel grid.
     * @param invViewProj Inverse view-projection used to reconstruct froxel centers.
     * @param occupancySkip Skip empty froxels when the quality budget allows it.
     * @return Number of froxels that received non-zero media.
     */
    [[nodiscard]] Result<int> inject(AtmosphereVolume& volume, const FogDensityField& field,
                                     const FogProfile& profile, const glm::mat4& invViewProj,
                                     bool occupancySkip);

    /**
     * @brief Integrate froxels with optional Beer-cache visibility.
     * @param invViewProj Inverse view-projection; must match the matrix used by inject().
     */
    [[nodiscard]] Result<void> integrate(AtmosphereVolume& volume, const FogDensityField& field,
                                         const FogProfile& profile, const glm::mat4& invViewProj,
                                         const glm::vec3& lightDir, const glm::vec3& lightColor,
                                         float intensity, const BeerLightCache* beerCache);
};

}  // namespace eve::graphics::fog
