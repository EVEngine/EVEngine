#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/BeerLightCache.h"
#include "graphics/fog/FogDensityField.h"
#include "graphics/fog/FogProfile.h"
#include "graphics/fog/FogTypes.h"

#include <optional>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

/** @brief Analytic volume used to bound a camera ray before sampling. */
struct FogVolumeBound {
    enum class Kind : uint8_t { SkyShell, HeightLayer, Obb };

    Kind kind = Kind::HeightLayer;
    float skyRadius = 200.f;
    float heightMin = 0.f;
    float heightMax = 8.f;
    glm::vec3 obbCenter{0.f};
    glm::vec3 obbHalfExtents{4.f};
    glm::vec3 obbAxisX{1.f, 0.f, 0.f};
    glm::vec3 obbAxisY{0.f, 1.f, 0.f};
    glm::vec3 obbAxisZ{0.f, 0.f, 1.f};
};

/**
 * @brief World-space fog ray marcher with Beer–Lambert extinction and HG phase.
 *
 * Rays intersect sky shell / height layer / OBB first, then sample the density
 * field in world units so camera and actor motion cannot crawl the fog texture.
 */
class EVENGINE_API_BACKENDS FogRayMarch {
public:
    /** @brief Intersect a ray with a volume bound; returns empty when misses. */
    [[nodiscard]] static std::optional<FogRayInterval> intersectBound(const glm::vec3& origin,
                                                                     const glm::vec3& direction,
                                                                     const FogVolumeBound& bound);

    /**
     * @brief Integrate one view ray through the density field.
     * @param sceneDepth Optional scene hit distance; <=0 means infinite.
     * @param useAnalyticSegments Prefer closed-form Beer segments when density is stable.
     */
    [[nodiscard]] Result<FogRayResult> integrate(const FogDensityField& field, const FogProfile& profile,
                                                 const glm::vec3& origin, const glm::vec3& direction,
                                                 const FogVolumeBound& bound, const glm::vec3& lightDir,
                                                 const glm::vec3& lightColor, float lightIntensity,
                                                 const glm::vec3& skyAmbient, const glm::vec3& groundAmbient,
                                                 int sampleCount, float sceneDepth,
                                                 const BeerLightCache* beerCache,
                                                 bool useAnalyticSegments) const;
};

}  // namespace eve::graphics::fog
