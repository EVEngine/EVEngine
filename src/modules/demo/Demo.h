#pragma once

#include "common/Module.h"

#include <string>

namespace eve::graphics {
class Graphics;
class Texture;
}

namespace eve::sound {
class SoundData;
}

namespace eve::demo {

/**
 * @brief Optional demo-only helpers (procedural SFX / planet albedo).
 * Built only when EVENGINE_BUILD_DEMO=ON; other games need not link or compile this.
 */
class Demo : public Module {
public:
    Module_REG(Demo);

    Demo();
    ~Demo() override;

    /** @brief kind: "music" | "shoot" | "explode" | "hit" */
    sound::SoundData *newSound(const std::string &kind);

    /** @brief Equirectangular planet albedo uploaded via Graphics::newTexture. */
    graphics::Texture *newPlanetTexture(graphics::Graphics *gfx);
};

}  // namespace eve::demo
