#include "particles/GroundParticleCulling.h"

#include "particles/ParticleEmitter.h"

namespace eve::particles {

Result<void> applyGroundParticleCulling(ParticleEmitter* emitter, int playerTag, int visitorTag,
                                         bool entered, bool exited) {
    if (!emitter || playerTag == 0 || entered == exited)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "particles.groundCulling: emitter, nonzero player tag and one event direction required"));
    if (visitorTag != playerTag) return Result<void>::success();
    if (entered) {
        if (emitter->isStopped()) emitter->start();
    } else if (!emitter->isStopped()) {
        emitter->stop();
    }
    return Result<void>::success();
}

}  // namespace eve::particles
