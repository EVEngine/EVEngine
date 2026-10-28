#include "particles/FloatingPointFixParticles.h"

#include "particles/ParticleEmitter.h"

#include <cmath>

namespace eve::particles {

Result<int> shiftWorldSpaceParticles(ParticleEmitter* emitter, float shiftX, float shiftY) {
    if (!emitter || !std::isfinite(shiftX) || !std::isfinite(shiftY))
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "particles.floatingOrigin: emitter and finite shift required"));
    auto cfg = emitter->config();
    if (cfg->simSpace != "world")
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "particles.floatingOrigin: only world-space emitters can be shifted"));

    auto sim = emitter->sim();
    for (int i = 0; i < sim->alive; ++i) {
        auto& particle = sim->particles[static_cast<std::size_t>(i)];
        particle.x -= shiftX;
        particle.y -= shiftY;
    }
    auto gpu = emitter->gpuSim();
    if (gpu->residentActive) {
        gpu->pendingWorldOffsetX -= shiftX;
        gpu->pendingWorldOffsetY -= shiftY;
    }
    return Result<int>::success(sim->alive + gpu->estimatedAlive);
}

}  // namespace eve::particles
