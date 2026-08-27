#pragma once
/**
 * @file PhysicsHandles.h
 * @brief Process-local, generation-qualified handles owned by the physics domain.
 *
 * These handles are deliberately not UUIDs and are not persistent identity.
 * Save data stores the owning domain's persistent identity and rebuilds these
 * handles while restoring the world.
 */

#include "common/RuntimeHandle.h"

namespace eve::physics {

/** @brief Tag for a process-local physics-world handle. */
struct PhysicsWorldHandleTag {};
/** @brief Tag for a process-local physics-body handle. */
struct PhysicsBodyHandleTag {};
/** @brief Tag for a process-local physics-shape handle. */
struct PhysicsShapeHandleTag {};
/** @brief Tag for a process-local physics-joint handle. */
struct PhysicsJointHandleTag {};

/** @brief Generation-qualified runtime identity for a physics world. */
using PhysicsWorldHandle = eve::RuntimeHandle<PhysicsWorldHandleTag>;
/** @brief Generation-qualified runtime identity for a physics body. */
using PhysicsBodyHandle = eve::RuntimeHandle<PhysicsBodyHandleTag>;
/** @brief Generation-qualified runtime identity for a physics shape. */
using PhysicsShapeHandle = eve::RuntimeHandle<PhysicsShapeHandleTag>;
/** @brief Generation-qualified runtime identity for a physics joint. */
using PhysicsJointHandle = eve::RuntimeHandle<PhysicsJointHandleTag>;

namespace detail {

/**
 * @brief Allocates a process-local world handle from the physics owner.
 * @return A live-world candidate with generation one.
 * @throws eve::Exception when the process-local index space is exhausted.
 *
 * The returned value is not persistent. World owns the occupancy and invalidates
 * the handle before its solver state is destroyed.
 */
[[nodiscard("retain the handle while the physics world is live")]]
PhysicsWorldHandle allocatePhysicsWorldHandle();

}  // namespace detail
}  // namespace eve::physics
