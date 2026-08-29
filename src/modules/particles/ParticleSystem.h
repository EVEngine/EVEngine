#pragma once

#include "common/Time.h"

namespace eve::graphics {
class Graphics;
}

namespace eve::particles {

/** @brief Advances all ParticleEmitter Sim components. */
class ParticleSimSystem {
public:
    /**
     * @brief Advances emitters using one scheduler-owned deterministic step.
     * @return Checked status; the step is never sourced from a wall clock.
     * @remarks CPU integration is deterministic for a fixed seed/tick within
     *          the documented float tolerance. A resident GPU path is
     *          tolerance-bounded and exposes its backend through emitter stats.
     */
    [[nodiscard]] static eve::Result<void> advance(const eve::SimulationStep& step);

    /** @brief Legacy seconds facade; conversion and Result consumption are explicit. */
    static void update(float dt);
};

/** @brief Draws all visible ParticleEmitter entities via Graphics batch path. */
class ParticleRenderSystem {
public:
    static void render(graphics::Graphics *gfx);
};

/** @brief Syncs pooled Light2D entities with alive particles (emitters with lights.enabled). */
class ParticleLightSystem {
public:
    static void update();
};

/**
 * @brief Polls bound config files (Resource.path) and hot-reloads when modtime changes.
 * Returns number of emitters reloaded.
 */
class ParticleConfigSystem {
public:
    static int poll();
};

}  // namespace eve::particles
