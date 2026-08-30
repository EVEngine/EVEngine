#pragma once

#include "common/Result.h"

namespace eve::physics {
class World3D;
void registerPhysicsCapabilities();
/** @brief Registers the backend-neutral generated-artifact collider provider. */
void registerPhysicsArtifactProvider();
void registerCameraObstructionWorld(World3D* world);
/**
 * @brief Registers a borrowed live World3D with the targeting LOS adapter.
 * @param world Non-null live World3D. Its lifetime is tracked weakly.
 * @return Applied, NoOp for the same world, Conflict for another active world,
 *         or a structured failure for an invalid world.
 * @note Synchronous simulation-thread operation.
 */
[[nodiscard]] eve::Result<void> registerTargetingLineOfSightWorld(World3D* world);
}  // namespace eve::physics
