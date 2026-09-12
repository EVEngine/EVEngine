#pragma once

#include "common/Module.h"

namespace eve::physics {
class World;
}
namespace eve::pixelworld {
class PixelWorld;
}

namespace eve::pixelworld_physics {

class PixelTerrainCollisionCache;

/** @brief Script-facing owner for PixelWorld-to-Box2D terrain projection. */
class PixelWorldPhysics final : public eve::Module {
public:
    Module_REG(PixelWorldPhysics);

    /**
     * @brief Create a caller-owned incremental terrain collision cache.
     * @ownership Ownership transfers to the script caller.
     * @lifetime The cache retains only stale-safe links; the supplied worlds are borrowed per call.
     * @thread Create and use on the simulation owner thread.
     */
    [[nodiscard("retain and delete the returned PixelTerrainCollisionCache")]]
    PixelTerrainCollisionCache* newTerrainCache();
};

}  // namespace eve::pixelworld_physics
