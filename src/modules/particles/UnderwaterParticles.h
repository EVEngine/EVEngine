#pragma once

#include "common/Result.h"

namespace eve::particles {
class ParticleEmitter;

/**
 * @brief Apply Pcg underwater ambience and transition visibility to particle emitters.
 * @param ambience Required caller-owned looping ambience emitter.
 * @param transition Optional caller-owned one-shot transition emitter.
 * @param active Whether underwater ambience should be visible and running.
 * @param transitionFx Whether transition effects are enabled by the profile.
 * @param entered Whether this frame crossed from surface to underwater.
 * @param exited Whether this frame crossed from underwater to surface.
 * @return Success or InvalidArgument; validation completes before mutation.
 * @ownership Emitters remain caller-owned and are never retained.
 * @thread Simulation owner thread only; callbacks are not invoked.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> applyUnderwaterParticles(ParticleEmitter* ambience,
                                                     ParticleEmitter* transition, bool active,
                                                     bool transitionFx, bool entered, bool exited);

/**
 * @brief Apply Pcg's inverse underwater state to one surface weather or VFX emitter.
 * @param surfaceVfx Required caller-owned emitter representing a surface-only effect.
 * @param active Whether the surface effect should be visible and running.
 * @return Success or InvalidArgument; a null emitter is rejected without mutation.
 * @ownership The emitter remains caller-owned and is never retained.
 * @thread Simulation owner thread only; callbacks are not invoked.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> applyUnderwaterSurfaceVfx(ParticleEmitter* surfaceVfx, bool active);
}
