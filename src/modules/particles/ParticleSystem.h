#pragma once

#include "common/Time.h"

namespace eve::graphics {
class Graphics;
}

namespace eve::filesystem {
class Filesystem;
}

namespace eve::particles {

class ParticleEmitter;

/** @brief Advances all ParticleEmitter Sim components. */
class ParticleSimSystem {
public:
    /**
     * @brief Advances emitters using one scheduler-owned deterministic step.
     * @param step Scheduler-owned step; the sim never sources time from a wall clock.
     * @param gfx Resolved Graphics provider used for the offscreen cull, or null.
     *            Passing it in keeps the per-emitter loop free of registry lookups.
     * @return Checked status.
     * @remarks CPU integration is deterministic for a fixed seed/tick within
     *          the documented float tolerance. A resident GPU path is
     *          tolerance-bounded and exposes its backend through emitter stats.
     */
    [[nodiscard]] static eve::Result<void> advance(const eve::SimulationStep& step, graphics::Graphics* gfx);

    /** @brief Legacy seconds facade with an explicit Graphics dependency. */
    static void update(float dt, graphics::Graphics* gfx);

    /**
     * @brief Compatibility overload that resolves the Graphics provider itself.
     * @return Checked status.
     * @cost One registry lookup while no Graphics module is registered; once one is,
     *       a cached pointer read. Never a per-emitter lookup.
     * @remarks Defined in Particles.cpp -- the module's composition boundary -- so the
     *          declared hot-path translation unit holds no lookup of its own.
     */
    [[nodiscard]] static eve::Result<void> advance(const eve::SimulationStep& step);
    /** @brief Legacy seconds facade; resolves Graphics like the overload above. */
    static void update(float dt);
};

/** @brief Draws all visible ParticleEmitter entities via Graphics batch path. */
class ParticleRenderSystem {
public:
    /**
     * @brief Draw exactly one emitter without traversing the global ECS registry.
     * @return Number of particles submitted, or zero when hidden/empty/unsupported.
     */
    static int renderEmitter(graphics::Graphics* gfx, ParticleEmitter* emitter);
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
    /**
     * @brief Poll with an explicit filesystem dependency.
     * @param fs Resolved Filesystem provider, or null to create a private one.
     * @return Number of emitters reloaded.
     */
    static int poll(eve::filesystem::Filesystem* fs);

    /**
     * @brief Compatibility overload that resolves the Filesystem provider itself.
     * @return Number of emitters reloaded.
     * @cost One registry lookup while no Filesystem module is registered; once one
     *       is, a cached pointer read.
     * @remarks Defined in Particles.cpp; see the note on ParticleSimSystem.
     */
    static int poll();
};

}  // namespace eve::particles
