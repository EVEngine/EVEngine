#include "particles/ParticleRuntime.h"

namespace eve::particles {

namespace {
ParticleBudgetConfig g_budget;
ParticleFrameStats   g_stats;
}  // namespace

ParticleBudgetConfig&     particleBudgetConfig() { return g_budget; }
const ParticleFrameStats& particleFrameStats() { return g_stats; }
ParticleFrameStats&       mutableParticleFrameStats() { return g_stats; }

}  // namespace eve::particles
