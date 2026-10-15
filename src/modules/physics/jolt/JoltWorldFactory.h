#pragma once
#include "common/Export.h"
#include "physics/rigid/WorldBackend3D.h"
namespace eve::physics::jolt {
/** @brief Creates the native Jolt provider; world owns it and calls it only on the simulation thread.
 * @cost Allocates broad phase, body pool and solver workspace once per world.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<std::unique_ptr<IWorldBackend3D>> makeWorld(World3D&, float, float, float,
                                                                                         bool);
}  // namespace eve::physics::jolt
