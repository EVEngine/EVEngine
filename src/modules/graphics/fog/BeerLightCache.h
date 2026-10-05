#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogDensityField.h"
#include "graphics/fog/FogProfile.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

/**
 * @brief Cached light-path transmittance used by Fast / Enhanced quality tiers.
 *
 * Invalidates when density revision, light state, or view/light direction change.
 */
class EVENGINE_API_BACKENDS BeerLightCache {
public:
    struct LightKey {
        glm::vec3 direction{0.f, 1.f, 0.f};
        glm::vec3 color{1.f};
        float intensity = 1.f;
        std::uint64_t densityRevision = 0;
        std::uint64_t version = 0;
    };

    [[nodiscard]] std::uint64_t version() const noexcept { return key_.version; }
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] const LightKey& key() const noexcept { return key_; }

    /**
     * @brief Rebuild transmittance along the primary light for each density cell.
     * @return Ok after a successful rebuild, or Rejected on invalid inputs.
     */
    [[nodiscard]] Result<void> rebuild(const FogDensityField& field, const FogProfile& profile,
                                       const glm::vec3& lightDir, const glm::vec3& lightColor,
                                       float intensity, int samplesPerCell = 6);

    /** @brief Mark cache stale (quality change, light cut, etc.). */
    void invalidate();

    /** @brief Sample cached transmittance at a world position (1 = fully lit). */
    [[nodiscard]] float sampleTransmittance(const FogDensityField& field,
                                            const glm::vec3& world) const noexcept;

    /**
     * @brief True when the cache matches the supplied state.
     */
    [[nodiscard]] bool matches(std::uint64_t densityRevision, const glm::vec3& lightDir,
                               float intensity) const noexcept;

private:
    LightKey key_{};
    bool valid_ = false;
    int width_ = 0;
    int height_ = 0;
    int depth_ = 0;
    std::vector<float> transmittance_;
};

}  // namespace eve::graphics::fog
