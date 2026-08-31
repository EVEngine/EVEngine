#pragma once

#include "common/Module.h"
#include "pixelworld/PixelWorld.h"

#include <cstdint>

namespace eve::pixelworld {

/** @brief Script-facing owner/factory for deterministic pixel material worlds. */
class PixelWorldModule : public Module {
public:
    Module_REG(PixelWorldModule);

    PixelWorldModule();
    ~PixelWorldModule() override;

    /** @brief Create a caller-owned world using the supplied deterministic seed. */
    [[nodiscard("retain and delete the returned PixelWorld")]] PixelWorld* newWorld(std::uint64_t seed = 1);
};

}  // namespace eve::pixelworld
