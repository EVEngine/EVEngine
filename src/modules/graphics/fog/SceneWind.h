#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogTypes.h"

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

/**
 * @brief World-space wind provider for MAC fog transport.
 *
 * Main wind and curl remain independent so translation speed, swirl strength,
 * and time scale can be authored separately. Changes are rate-limited before
 * they reach the velocity solver.
 */
class EVENGINE_API_WORLD SceneWind {
public:
    /** @brief Target main wind in m/s (world space). */
    [[nodiscard]] Result<void> setMainWind(const glm::vec3& metersPerSecond);
    /** @brief Target curl amplitude in m/s. */
    [[nodiscard]] Result<void> setCurlStrength(float metersPerSecond);
    /** @brief Curl field spatial frequency (1/m). */
    [[nodiscard]] Result<void> setCurlFrequency(float frequency);
    /** @brief Time scale for curl animation (dimensionless, >= 0). */
    [[nodiscard]] Result<void> setCurlTimeScale(float scale);
    /** @brief Max response rate for wind changes (m/s per second). */
    [[nodiscard]] Result<void> setResponseRate(float metersPerSecondPerSecond);

    [[nodiscard]] glm::vec3 mainWind() const noexcept { return currentMain_; }
    [[nodiscard]] float curlStrength() const noexcept { return currentCurl_; }
    [[nodiscard]] float curlFrequency() const noexcept { return curlFrequency_; }
    [[nodiscard]] float curlTimeScale() const noexcept { return curlTimeScale_; }

    /**
     * @brief Advance the limited-response filters by dt seconds.
     * @return Ok, or Rejected when dt is non-finite / negative.
     */
    [[nodiscard]] Result<void> tick(float dt);

    /**
     * @brief Sample combined wind at a world position and simulation time.
     * Curl is divergence-free on average and independent of main translation.
     */
    [[nodiscard]] glm::vec3 sample(const glm::vec3& world, float timeSeconds) const noexcept;

private:
    glm::vec3 targetMain_{0.f};
    glm::vec3 currentMain_{0.f};
    float targetCurl_ = 0.f;
    float currentCurl_ = 0.f;
    float curlFrequency_ = 0.15f;
    float curlTimeScale_ = 1.f;
    float responseRate_ = 4.f;
};

}  // namespace eve::graphics::fog
