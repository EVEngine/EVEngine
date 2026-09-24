#pragma once

#include "common/Result.h"

namespace eve::particles {
class ParticleEmitter;

/**
 * @brief Shift live world-space particles after a floating-origin rebase.
 * @param emitter Required caller-owned world-space emitter.
 * @param shiftX Horizontal origin delta to subtract from particle positions.
 * @param shiftY Vertical particle-plane origin delta to subtract from positions.
 * @return Number of CPU and GPU-resident live particles scheduled for translation.
 * @ownership The emitter remains caller-owned and is never retained.
 * @thread Simulation owner thread only; call before the next particle update/render.
 * Playback, pause state, velocity, lifetime and emitter position remain unchanged.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> shiftWorldSpaceParticles(ParticleEmitter* emitter, float shiftX,
                                                                        float shiftY);
}
