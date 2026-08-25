#pragma once

#include "common/Module.h"
#include "particles/ParticleEmitter.h"

#include <string>

namespace eve::graphics {
class Graphics;
}

namespace eve::particles {

/**
 * @brief Particles module — factory + script binding.
 * Per-frame: ParticleConfigSystem (hot reload) → ParticleSimSystem →
 * ParticleRenderSystem. Module `update`/`render` forward to Systems.
 */
class Particles : public Module {
public:
    Module_REG(Particles);
    Particles();
    ~Particles() override = default;

    ParticleEmitter *newEmitter(int bufferSize = 1000);
    /** @brief Create emitter from JSON config file (reads optional "buffer"). */
    ParticleEmitter *newEmitterFromFile(const std::string &path);

    void update(float dt);
    void render(graphics::Graphics *gfx);
    /** @brief Explicit hot-reload poll; also invoked from update(). */
    int pollConfigs();

    int getEmitterCount() const;

    /** @brief Set global soft particle and simulated-emitter budgets; zero is unlimited. */
    void setBudget(int maxParticles, int maxSimulatedEmitters);
    /** @brief Return the global live-particle soft cap, or zero when unlimited. */
    int getMaxParticles() const;
    /** @brief Return the per-frame simulated-emitter cap, or zero when unlimited. */
    int getMaxSimulatedEmitters() const;
    /** @brief Select the runtime VFX quality tier in [0,3]. */
    void setQualityLevel(int quality);
    /** @brief Return the current runtime VFX quality tier. */
    int getQualityLevel() const;

    /** @brief Return emitters simulated during the last update. */
    int getLastSimulatedEmitters() const;
    /** @brief Return emitters skipped by culling during the last update. */
    int getLastCulledEmitters() const;
    /** @brief Return emitters skipped by quality or capacity budgets. */
    int getLastBudgetSkippedEmitters() const;
    /** @brief Return live particles after the last update. */
    int getLastParticleCount() const;
    /** @brief Return particles spawned during the last update. */
    int getLastSpawnedParticles() const;
    /** @brief Return automatic spawns dropped by budget during the last update. */
    int getLastDroppedSpawns() const;
    /** @brief Return emitters using the resident GPU backend after the last update. */
    int getLastGpuResidentEmitters() const;
    /** @brief Return the CPU lifetime estimate for resident GPU particles. */
    int getLastGpuResidentParticles() const;
    /** @brief Return particles submitted during the last render. */
    int getLastRenderedParticles() const;
    /** @brief Return CPU simulation wall time in milliseconds. */
    float getLastSimulationMs() const;
    /** @brief Return particle render-build and submission wall time in milliseconds. */
    float getLastRenderMs() const;
};

}  // namespace eve::particles
