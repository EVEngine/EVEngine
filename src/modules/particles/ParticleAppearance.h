#pragma once
#include "common/Result.h"
#include "particles/ParticleAppearanceValue.h"
#include "particles/ParticleEmitter.h"
namespace eve::particles {
/** @brief Evaluate the same CPU sprite curves and render mode for live rendering and bake capture.
 * @ownership Borrows config and particle only during this call; returns independent values.
 * @thread Call while config and particle are stable; does not mutate them, invoke callbacks or advance simulation.
 * @remarks UV evaluation is independent of whether a texture is currently loaded.
 * @return Finite sprite appearance or a structured diagnostic; no resources are retained.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<ParticleAppearance> evaluateParticleAppearance(
    const ParticleEmitter::Config& config, const Particle& particle);
}  // namespace eve::particles
