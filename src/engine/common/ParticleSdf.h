#pragma once

#include "common/Export.h"

namespace eve {

/**
 * @brief Borrowed 2D signed-distance field consumed by particle collision.
 *
 * Negative samples are inside the solid. Providers live in higher modules
 * (physics, fluids, gameplay) and register through capability wiring; the
 * particles module only samples this interface and never includes provider
 * headers.
 *
 * @ownership Borrowed for the duration of the emitter binding. Callers must
 *            clear the binding before destroying the field.
 * @thread Owner thread only; not reentrant from particle callbacks.
 */
class EVENGINE_API_FOUNDATION_INLINE IParticleSdfField {
public:
    static constexpr const char* capabilityName = "IParticleSdfField";

    virtual ~IParticleSdfField() = default;

    /**
     * @brief Sample signed distance at a world-space particle position.
     * @return Finite signed distance in world units, or +inf when unsupported.
     */
    [[nodiscard]] virtual float sample(float x, float y) const = 0;

    /**
     * @brief Approximate outward unit gradient at a world-space position.
     * @return false when the gradient is undefined (empty field / singularity).
     */
    [[nodiscard]] virtual bool gradient(float x, float y, float& outNx, float& outNy) const = 0;
};

}  // namespace eve
