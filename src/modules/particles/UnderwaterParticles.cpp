#include "particles/UnderwaterParticles.h"

#include "particles/ParticleEmitter.h"

namespace eve::particles {

Result<void> applyUnderwaterParticles(ParticleEmitter* ambience, ParticleEmitter* transition,
                                      bool active, bool transitionFx, bool entered, bool exited) {
    if (!ambience || (entered && exited))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "particles.underwater: ambience required and transition directions must be exclusive"));

    ambience->setVisible(active);
    if (active) {
        if (ambience->isStopped()) ambience->start();
    } else if (!ambience->isStopped()) {
        ambience->stop();
    }

    if (transition && transitionFx && (entered || exited)) {
        transition->setVisible(true);
        transition->stop();
        transition->start();
    }
    return Result<void>::success();
}

Result<void> applyUnderwaterSurfaceVfx(ParticleEmitter* surfaceVfx, bool active) {
    if (!surfaceVfx)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "particles.underwaterSurfaceVfx: non-null surface emitter required"));

    surfaceVfx->setVisible(active);
    if (active) {
        if (surfaceVfx->isStopped()) surfaceVfx->start();
    } else if (!surfaceVfx->isStopped()) {
        surfaceVfx->stop();
    }
    return Result<void>::success();
}

}  // namespace eve::particles
