#include "common/Capability.h"
#include "common/ParticlesQuery.h"
#include "particles/Particles.h"
#include "particles/ParticleEmitter.h"

#include <string>

namespace eve::particles {
namespace {

class ParticlesQueryImpl final : public eve::IParticlesQuery {
public:
    int emitterCount() override {
        auto *p = eve::ModuleManager::getInstance<Particles>("Particles");
        return p ? p->getEmitterCount() : 0;
    }

    bool createEmitter(int bufferSize, float x, float y, const std::string &preset, int count,
                       float *outX, float *outY, int *outCount) override {
        auto *p = eve::ModuleManager::getInstance<Particles>("Particles");
        if (!p) return false;
        auto *em = p->newEmitter(bufferSize > 0 ? bufferSize : 1000);
        if (!em) return false;
        em->setPosition(x, y);
        if (!preset.empty()) em->applyPreset(preset);
        em->start();
        em->emit(count);
        if (outX) *outX = em->getX();
        if (outY) *outY = em->getY();
        if (outCount) *outCount = em->getCount();
        return true;
    }
};

}  // namespace

void registerParticlesCapabilities() {
    static ParticlesQueryImpl impl;
    eve::cap::provide<eve::IParticlesQuery>(&impl);
}

}  // namespace eve::particles
