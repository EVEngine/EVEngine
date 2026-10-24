#pragma once
#include <span>
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "physics/bake/ParticleTrack.h"
namespace eve::particles {
struct ParticleRecordEvent;
}
namespace eve::physics {
class BakeRecipe;
/** @brief Borrowed complete journal for one uniquely identified emitter recording session.
 * @ownership Events are borrowed only during buildParticleTrack; no view survives that call.
 */
struct ParticleRecordingSource {
    PersistentId                                    emitterSession;
    std::span<const particles::ParticleRecordEvent> events;
};
/** @brief Combine complete emitter journals into one immutable particle clip with a common recording recipe.
 * @ownership Borrows all event views and recipe only during conversion; result owns independent values.
 * @thread Inputs must be stable for this call; no callbacks or simulation.
 * @remarks Recipe must describe every source and named RNG stream. Session IDs must be unique, even for empty logs.
 * @cost Linear in total events plus birth/session ordering, once per completed recording outside playback.
 * @return Complete clip or diagnostic; up to 65536 sessions and 2 million total events, without partial publication.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<ParticleTrack> buildParticleTrack(
    float duration, std::span<const ParticleRecordingSource> sources, const BakeRecipe& recipe);

/** @brief Convert a completed emitter-session journal to an immutable planar particle track.
 * @ownership Borrows events and recipe only during conversion; result owns all values and survives source destruction.
 * @thread Inputs must remain stable; no callbacks, simulation, ECS or resource lookups.
 * @remarks Same-time snapshots replace earlier snapshots for that birth (post-inheritance appearance wins).
 * Every birth must have one terminal death; zero-duration births are validated then omitted as never visible.
 * Distinct times that collapse to one float are rejected, rather than silently losing event boundaries.
 * @cost Linear in event count plus logarithmic birth lookup; allocates once per completed recording outside playback.
 * @return Complete track or invalid event order, missing death, duplicate lifetime, budget or provenance diagnostic.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION Result<ParticleTrack> buildParticleTrack(
    PersistentId emitterSession, float duration, std::span<const particles::ParticleRecordEvent> events,
    const BakeRecipe& recipe);
}  // namespace eve::physics
