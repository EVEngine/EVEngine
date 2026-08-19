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
    Particles() = default;
    ~Particles() override = default;

    ParticleEmitter *newEmitter(int bufferSize = 1000);
    /** @brief Create emitter from JSON config file (reads optional "buffer"). */
    ParticleEmitter *newEmitterFromFile(const std::string &path);

    void update(float dt);
    void render(graphics::Graphics *gfx);
    /** @brief Explicit hot-reload poll; also invoked from update(). */
    int pollConfigs();

    int getEmitterCount() const;
};

}  // namespace eve::particles
