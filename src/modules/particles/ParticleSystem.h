#pragma once

namespace eve::graphics {
class Graphics;
}

namespace eve::particles {

/** @brief Advances all ParticleEmitter Sim components. */
class ParticleSimSystem {
public:
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
