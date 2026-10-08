#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogProfile.h"
#include "graphics/fog/FogTypes.h"

#include <cstdint>
#include <optional>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

/** @brief Analytic beam volume: capped cone frustum or pyramidal spot. */
struct AnalyticBeam {
    enum class Shape : uint8_t { ConeFrustum = 0, PyramidFrustum = 1 };

    Shape shape = Shape::ConeFrustum;
    glm::vec3 apex{0.f};
    glm::vec3 direction{0.f, 0.f, -1.f};
    float nearDistance = 0.25f;
    float farDistance = 12.f;
    float nearRadius = 0.1f;
    float farRadius = 2.5f;
    glm::vec3 color{1.f, 0.95f, 0.85f};
    float intensity = 2.f;
};

/**
 * @brief Analytic volumetric light: ray ∩ capped frustum, depth-clipped short integral.
 *
 * Segment counts of 2 / 4 / 8 map to Fast / Enhanced / PhysicalReference budgets.
 */
class EVENGINE_API_WORLD AnalyticalVolLight {
public:
    /** @brief Intersect a view ray with a capped beam; empty on miss. */
    [[nodiscard]] static std::optional<FogRayInterval> intersectBeam(const glm::vec3& origin,
                                                                     const glm::vec3& direction,
                                                                     const AnalyticBeam& beam);

    /**
     * @brief Integrate the beam contribution along a view ray.
     * @param sceneDepth Scene hit distance; <=0 means no opaque occluder.
     * @param segments 2, 4, or 8 short integral segments.
     * @param mediumDensity Approximate homogeneous density inside the beam.
     */
    [[nodiscard]] Result<FogRayResult> integrate(const AnalyticBeam& beam, const FogProfile& profile,
                                                 const glm::vec3& origin, const glm::vec3& direction,
                                                 float sceneDepth, int segments,
                                                 float mediumDensity) const;
};

}  // namespace eve::graphics::fog
