#pragma once

#include "common/Result.h"

namespace eve::physics {
class World3D;
void registerPhysicsCapabilities();
/** @brief Registers the backend-neutral generated-artifact collider provider. */
void registerPhysicsArtifactProvider();
void registerCameraObstructionWorld(World3D* world);
void unregisterCameraObstructionWorld(World3D* world);
/**
 * @brief Registers a borrowed live World3D with the targeting LOS adapter.
 * @param world Non-null World3D whose lifetime extends through registration.
 * @return Applied, NoOp for the same world, Conflict for another active world,
 *         or a structured failure for an invalid world.
 * @note Synchronous simulation-thread operation; the caller must remove the
 *       world before destruction.
 */
[[nodiscard]] eve::Result<void> registerTargetingLineOfSightWorld(World3D* world);
/**
 * @brief Removes a World3D from the targeting LOS adapter before destruction.
 * @param world Non-null World3D previously registered with the adapter.
 * @return Applied when removed, NoOp when absent, or InvalidArgument for null.
 * @note Synchronous simulation-thread operation; this does not destroy world.
 */
[[nodiscard]] eve::Result<void> unregisterTargetingLineOfSightWorld(World3D* world);
}
