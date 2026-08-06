#pragma once

namespace eve::graphics {
class Graphics;
}

namespace eve::particles {

/** Advances all ParticleEmitter Sim components. */
class ParticleSimSystem {
public:
    static void update(float dt);
};

/** Draws all visible ParticleEmitter entities via Graphics batch path. */
class ParticleRenderSystem {
public:
    static void render(graphics::Graphics *gfx);
};

/**
 * Polls bound config files (Resource.path) and hot-reloads when modtime changes.
 * Returns number of emitters reloaded.
 */
class ParticleConfigSystem {
public:
    static int poll();
};

}  // namespace eve::particles
