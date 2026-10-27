#include "particles/ParticleRecording.h"
#include "particles/ParticleAppearance.h"
namespace eve::particles {
Result<void> prepareParticleRecording(ParticleEmitter& emitter, std::size_t budget) {
    auto       sim    = emitter.sim();
    const auto config = emitter.config();
    if ((config->renderMode != "billboard" && config->renderMode != "axis" && config->renderMode != "stretched") ||
        config->sortMode != "none")
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation,
            "sprite recording requires a supported sprite mode and stable birth ordering", "particleRecording"));
    if (sim->alive != 0 || emitter.config()->gpuSimulation || emitter.gpuSim()->residentActive ||
        sim->recording.state() == ParticleRecordState::Recording)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation,
            "recording requires an empty CPU emitter without an active recording", "particleRecording"));
    auto prepared = sim->recording.prepare(budget);
    if (!prepared) return prepared;
    sim->recordingSeconds = 0;
    return Result<void>::success();
}
void recordParticleEvent(const ParticleEmitter::Config& config, ParticleEmitter::Sim& sim, const Particle& particle,
                         ParticleRecordEventKind kind) {
    if (sim.recording.state() != ParticleRecordState::Recording) return;
    if (config.gpuSimulation || config.sortMode != "none" ||
        (config.renderMode != "billboard" && config.renderMode != "axis" && config.renderMode != "stretched")) {
        sim.recording.invalidate();
        return;
    }
    auto appearance = evaluateParticleAppearance(config, particle);
    if (!appearance) {
        sim.recording.invalidate();
        return;
    }
    auto appended = sim.recording.append({sim.recordingSeconds, particle.birthSerial, kind, appearance.value()});
    if (!appended) sim.recording.invalidate();
}
Result<std::vector<ParticleRecordEvent>> finishParticleRecording(ParticleEmitter& emitter) {
    auto sim = emitter.sim();
    if (emitter.gpuSim()->residentActive) sim->recording.invalidate();
    for (int i = 0; i < sim->alive; ++i)
        recordParticleEvent(*emitter.config(), *sim, sim->particles[std::size_t(i)], ParticleRecordEventKind::Death);
    return sim->recording.finish();
}
}  // namespace eve::particles
