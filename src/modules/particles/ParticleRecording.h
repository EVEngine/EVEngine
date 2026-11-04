#pragma once
#include "particles/ParticleEmitter.h"
namespace eve::particles {
/** @brief Prepare bounded CPU event capture before the first live birth.
 * @ownership Journal belongs to the emitter Sim; no observer or source pointer is retained.
 * @thread Simulation owner thread, outside any active step or sub-emitter callback.
 * @cost Linear in event budget for reservation, once per recording.
 * @return Success or invalid state/allocation diagnostic; existing recording is preserved on failure.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> prepareParticleRecording(ParticleEmitter& emitter, std::size_t budget);
/** @brief Close all live births at the current explicit recording time and transfer owned events.
 * @ownership Returned events survive emitter destruction; live simulation itself is not stopped or reset.
 * @thread Simulation owner thread, outside any active step.
 * @cost Linear in live births for final appearance evaluation; once per completed recording.
 * @return Complete journal or diagnostic; failed journals cannot publish incomplete events.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<ParticleRecordEvent>> finishParticleRecording(
    ParticleEmitter& emitter);
/** @brief Internal simulation hook; captures at Sim recordingSeconds and records sticky failures.
 * @ownership Borrows all arguments only during this call; journal stores owning values.
 * @thread Simulation owner thread. No callbacks, resource lookups or allocations on successful capture.
 */
void recordParticleEvent(const ParticleEmitter::Config& config, ParticleEmitter::Sim& sim, const Particle& particle,
                         ParticleRecordEventKind kind);
}  // namespace eve::particles
