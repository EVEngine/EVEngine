#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogTypes.h"

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

/**
 * @brief Unit-density optical profile for participating media.
 *
 * Art layers must not write these fields; they only consume rendered results.
 */
class EVENGINE_API_BACKENDS FogProfile {
public:
    /** @brief Extinction coefficient per unit density (σ_t), non-negative. */
    [[nodiscard]] float extinctionPerDensity() const noexcept { return extinctionPerDensity_; }
    /** @brief Single-scattering albedo in [0,1]. */
    [[nodiscard]] const glm::vec3& albedo() const noexcept { return albedo_; }
    /** @brief Ambient occlusion strength in [0,1]. */
    [[nodiscard]] float ambientOcclusion() const noexcept { return ambientOcclusion_; }
    /** @brief Upward ambient visibility scale in [0,1]. */
    [[nodiscard]] float ambientUp() const noexcept { return ambientUp_; }
    /** @brief Downward ambient visibility scale in [0,1]. */
    [[nodiscard]] float ambientDown() const noexcept { return ambientDown_; }
    /** @brief Henyey–Greenstein anisotropy in [-0.99, 0.99]. */
    [[nodiscard]] float anisotropy() const noexcept { return anisotropy_; }

    /**
     * @brief Atomically replace the optical profile.
     * @return Ok, or Rejected when any argument is non-finite / out of range.
     */
    [[nodiscard]] Result<void> configure(float extinctionPerDensity, const glm::vec3& albedo,
                                         float ambientOcclusion, float ambientUp, float ambientDown,
                                         float anisotropy);

    /** @brief Extinction for a sampled density value. */
    [[nodiscard]] float extinctionAt(float density) const noexcept;

    /** @brief Scattering coefficient σ_s = albedo * σ_t. */
    [[nodiscard]] glm::vec3 scatteringAt(float density) const noexcept;

    /**
     * @brief Dual-lobe Henyey–Greenstein phase evaluated for cosθ.
     * @param cosTheta Dot product of view and light directions in [-1,1].
     */
    [[nodiscard]] float phase(float cosTheta) const noexcept;

    /** @brief Ambient term using up/down visibility and AO. */
    [[nodiscard]] glm::vec3 ambientRadiance(const glm::vec3& skyUp, const glm::vec3& groundDown,
                                           float density) const noexcept;

private:
    float extinctionPerDensity_ = 0.08f;
    glm::vec3 albedo_{0.9f, 0.92f, 0.95f};
    float ambientOcclusion_ = 0.15f;
    float ambientUp_ = 1.f;
    float ambientDown_ = 0.55f;
    float anisotropy_ = 0.35f;
};

}  // namespace eve::graphics::fog
