#include "particles/ParticleEmitter.h"

namespace eve::particles {

void ParticleEmitter::setPriority(int priority) { config()->priority = priority; }
int  ParticleEmitter::getPriority() { return config()->priority; }

void ParticleEmitter::setMinimumQuality(int quality) {
    config()->minimumQuality = quality < 0 ? 0 : (quality > 3 ? 3 : quality);
}

int ParticleEmitter::getMinimumQuality() { return config()->minimumQuality; }

void ParticleEmitter::setCullingMode(const std::string& mode) {
    config()->cullingMode = (mode == "pause" || mode == "always") ? mode : "automatic";
}

std::string ParticleEmitter::getCullingMode() { return config()->cullingMode; }

void ParticleEmitter::setCullDistance(float distance) {
    config()->cullDistance = distance > 0.f ? distance : 0.f;
}

float ParticleEmitter::getCullDistance() { return config()->cullDistance; }

void ParticleEmitter::setMaxSpawnPerFrame(int count) {
    config()->maxSpawnPerFrame = count > 0 ? count : 0;
}

int ParticleEmitter::getMaxSpawnPerFrame() { return config()->maxSpawnPerFrame; }

}  // namespace eve::particles
