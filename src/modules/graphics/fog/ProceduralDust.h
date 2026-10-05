#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "graphics/fog/FogTypes.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::fog {

/** @brief Deterministic dust mote spawned inside an analytic volume. */
struct DustParticle {
    glm::vec3 position{0.f};
    float size = 0.02f;
    float brightness = 1.f;
    enum class Band : uint8_t { Fine = 0, Mid = 1 } band = Band::Fine;
};

/**
 * @brief Procedural dust that does not use persistent particle buffers or screen trails.
 *
 * Fine and Mid bands share a stable world-space hash distribution with a continuous
 * local turbulence offset so camera motion never leaves fixed screen-space noise.
 */
class EVENGINE_API_BACKENDS ProceduralDust {
public:
    /**
     * @brief Generate dust inside an AABB for the current view time.
     * @param count Requested mote count in [1, 4096].
     * @param turbulenceMeters Peak local offset amplitude.
     * @param seed Stable world hash seed.
     */
    [[nodiscard]] Result<std::vector<DustParticle>> generate(const FogWorldBounds& volume, int count,
                                                             float timeSeconds, float turbulenceMeters,
                                                             std::uint32_t seed) const;
};

}  // namespace eve::graphics::fog
