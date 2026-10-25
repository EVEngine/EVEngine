#pragma once

#include "common/Result.h"

namespace eve::particles {
class ParticleEmitter;

/**
 * @brief Apply one Pcg GroundParticlesCulling trigger event to a caller-owned emitter.
 * @param emitter Required ground-weather emitter.
 * @param playerTag Stable nonzero tag configured for the player visitor shape.
 * @param visitorTag Stable tag copied from the physics trigger event.
 * @param entered True for a begin-overlap event.
 * @param exited True for an end-overlap event.
 * @return Success, or InvalidArgument before mutation for malformed input.
 * @ownership The emitter remains caller-owned and is never retained.
 * @thread Simulation owner thread only; callbacks are not invoked.
 * A matching enter starts the emitter and a matching exit stops it. Unrelated visitors are ignored.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> applyGroundParticleCulling(ParticleEmitter* emitter, int playerTag,
                                                                           int visitorTag, bool entered, bool exited);
}
