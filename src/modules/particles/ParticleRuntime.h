#pragma once

#include <cstdint>

namespace eve::particles {

/** @brief Global particle scalability controls shared by CPU and future GPU backends. */
struct ParticleBudgetConfig {
    /** @brief Soft cap for live particles across all emitters; zero is unlimited. */
    int maxParticles = 0;
    /** @brief Maximum emitters simulated per frame; zero is unlimited. */
    int maxSimulatedEmitters = 0;
    /** @brief Runtime VFX quality tier in [0,3]. */
    int qualityLevel = 3;
};

/** @brief Counters from the most recent particle update and render pass. */
struct ParticleFrameStats {
    uint64_t frameIndex             = 0;
    int      emittersTotal          = 0;
    int      emittersSimulated      = 0;
    int      emittersCulled         = 0;
    int      emittersQualitySkipped = 0;
    int      emittersBudgetSkipped  = 0;
    int      particlesBefore        = 0;
    int      particlesAfter         = 0;
    int      particlesSpawned       = 0;
    int      particlesKilled        = 0;
    int      droppedSpawns          = 0;
    int      gpuResidentEmitters    = 0;
    int      gpuResidentParticles   = 0;
    int      renderedParticles      = 0;
    int      renderCulledEmitters   = 0;
    double   simulationMs           = 0.0;
    double   renderMs               = 0.0;
};

/** @brief Mutable process-wide scalability configuration. */
ParticleBudgetConfig& particleBudgetConfig();
/** @brief Statistics for the most recently processed particle frame. */
const ParticleFrameStats& particleFrameStats();
/** @brief Internal mutable statistics used by particle systems. */
ParticleFrameStats& mutableParticleFrameStats();

}  // namespace eve::particles
