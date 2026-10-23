#pragma once
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
namespace eve::physics::jolt {
/** @brief Native Jolt solver composition for independently configurable distance spring, limits and motor.
 * @ownership Owned by the joint provider and Jolt PhysicsSystem; both body references follow TwoBodyConstraint.
 * @thread Mutations run between updates. Solver callbacks use Jolt's locked bodies and never call engine code.
 */
/** @brief Creates the private Jolt solver composition; native bodies are borrowed until constraint removal.
 * @cost Allocates two distance solver parts and a motor once per joint creation.
 */
[[nodiscard]] JPH::Ref<JPH::TwoBodyConstraint> createDistanceConstraint(JPH::Body&, JPH::Body&,
                                                                        const JPH::DistanceConstraintSettings&);
/** @brief Updates an already validated private distance constraint, between simulation steps. */
void setDistanceLength(JPH::TwoBodyConstraint&, float);
/** @brief Configures its independent spring after validation; no allocation or callbacks. */
void setDistanceSpring(JPH::TwoBodyConstraint&, bool, float, float);
/** @brief Configures its independent hard limits after validation; no allocation or callbacks. */
void setDistanceLimits(JPH::TwoBodyConstraint&, bool, float, float);
/** @brief Configures its independent motor after validation; no allocation or callbacks. */
void setDistanceMotor(JPH::TwoBodyConstraint&, bool, float, float);
/** @brief Returns the owning scalar sum of distance solver impulses after an update. */
float distanceImpulse(const JPH::TwoBodyConstraint&);
}  // namespace eve::physics::jolt
