#pragma once
#include "common/Export.h"
#include "physics/rigid/WorldBackend3D.h"
namespace eve::physics::box3d {
/** @brief Native-world composition boundary; returns exclusive ownership or a structured failure.
 * @cost One solver-world allocation per world creation; simulation thread, no callbacks.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<std::unique_ptr<IWorldBackend3D>> makeWorld(World3D& owner, float gx,
                                                                                         float gy, float gz,
                                                                                         bool sleep);
}  // namespace eve::physics::box3d
