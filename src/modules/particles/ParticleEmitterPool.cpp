#include "particles/ParticleEmitterPool.h"

#include "particles/ParticleEmitter.h"

#include <algorithm>

namespace eve::particles {

ParticleEmitterPool::~ParticleEmitterPool() { clear(); }

Result<ParticleEmitter*> ParticleEmitterPool::acquire(int minBuffer) {
    const int buffer = std::max(1, minBuffer);
    for (auto it = idle_.begin(); it != idle_.end(); ++it) {
        ParticleEmitter* emitter = *it;
        if (!emitter || emitter->getBufferSize() < buffer) continue;
        idle_.erase(it);
        emitter->reset();
        emitter->stop();
        emitter->setVisible(true);
        return Result<ParticleEmitter*>::success(emitter);
    }
    ParticleEmitter* created = ParticleEmitter::createEmitter(buffer);
    if (!created)
        return Result<ParticleEmitter*>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "Particle emitter pool could not create an emitter", "buffer"));
    created->stop();
    return Result<ParticleEmitter*>::success(created);
}

Result<void> ParticleEmitterPool::recycle(ParticleEmitter* emitter) {
    if (!emitter)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Cannot recycle a null particle emitter",
                                                       "emitter"));
    emitter->stop();
    emitter->reset();
    emitter->setVisible(false);
    emitter->clearSubEmitters();
    emitter->clearForceFields();
    emitter->clearFloatParameters();
    emitter->clearFloatParameterBindings();
    idle_.push_back(emitter);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void ParticleEmitterPool::clear() {
    for (auto* emitter : idle_)
        if (emitter) emitter->release();
    idle_.clear();
}

}  // namespace eve::particles
