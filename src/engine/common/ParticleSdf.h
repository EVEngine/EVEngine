#pragma once

#include "common/Export.h"

namespace eve {

/** @brief Outcome of sampling an SDF gradient for particle collision. */
enum class ParticleSdfGradientStatus {
    Defined,    ///< Finite outward unit gradient written to the output components.
    Undefined,  ///< Empty field or singularity; output components are unspecified.
};

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
     * @param[out] outNx X component of the unit gradient when the status is Defined.
     * @param[out] outNy Y component of the unit gradient when the status is Defined.
     * @return Defined when a finite gradient was written; Undefined at empty
     *         fields or singularities.
     */
    [[nodiscard]] virtual ParticleSdfGradientStatus gradient(float x, float y, float& outNx,
                                                             float& outNy) const = 0;
};

}  // namespace eve
