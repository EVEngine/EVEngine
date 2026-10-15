#pragma once
#include "common/Export.h"

/**
 * @brief Optional physics backend for vehicle mobility and body attach.
 *
 * VehiclePhysics.cpp is compiled only when the physics module is enabled;
 * VehiclePhysicsAbsent.cpp is compiled otherwise. Callers always link one of
 * the two and must not include physics headers.
 */

#include "common/Result.h"
#include "common/Time.h"
#include "vehicle/VehicleTypes.h"

namespace eve::physics {
class World;
class World3D;
}  // namespace eve::physics

namespace eve::vehicle {

/** @brief Outcome of an optional physics attach, detach, or mobility step. */
enum class VehiclePhysicsStatus : uint8_t {
    Applied,      ///< Physics handled the request.
    Unavailable,  ///< No physics module or no attached body.
    Pending,      ///< Forces submitted; the world has not completed their step.
    Cancelled,    ///< Restore or destruction cancelled a pending step.
    Rejected,     ///< Compatibility projection of a structured failure; do not fall back to kinematic motion.
};

/** @brief VehiclePhysics public API. */
class EVENGINE_API_DOMAINS VehiclePhysics {
public:
    /** @brief Attach a 2D dynamic body when physics is in the build. */
    [[nodiscard]] static VehiclePhysicsStatus attach2D(VehicleEntity* v, eve::physics::World* world);
    /** @brief Attach a 3D dynamic body when physics is in the build. */
    [[nodiscard]] static VehiclePhysicsStatus attach3D(VehicleEntity* v, eve::physics::World3D* world, float heightY);
    /** @brief Destroy attached bodies if any. */
    [[nodiscard]] static VehiclePhysicsStatus detach(VehicleEntity* v);
    /** @brief Whether the entity has a live, resolvable physics body. */
    [[nodiscard]] static bool isAttached(VehicleEntity* v);
    /** @brief 3D body height, or 0 when no 3D body is attached. */
    static float height(VehicleEntity* v);
    /** @brief Step wheel/ship mobility from an attached body. */
    [[nodiscard]] static VehiclePhysicsStatus tryWheelMove(VehicleEntity& v, float dt);
    /** @brief Submit top-down 2D or ray-suspension 3D wheel forces for an exact positive fixed step; no solver
     * advancement.
     * @return Pending, Applied for an already completed identical submission, Unavailable without a body,
     * or structured failure. Spatial contacts include equal/opposite dynamic-ground forces.
     * @ownership The binding owns candidate state and a read-only physics completion receipt.
     * @thread Simulation owner only, outside callbacks; non-reentrant.
     * @details Failed preparation changes no wheel state or body. Synchronize a completed step before the next submit.
     * @cost Per-wheel sampling and O(n log n) force preparation; one opaque pending operation per attached vehicle.
     */
    [[nodiscard]] static Result<VehiclePhysicsStatus> submitWheelStep(VehicleEntity& v, const SimulationStep& step);
    /** @brief Publish wheel candidates only after their physics step succeeds; no force submission.
     * @return Pending, Applied, Cancelled, or Unavailable. Stale body links cancel unpublished candidates.
     * @thread Simulation owner only; no callbacks. Completed wheel state belongs to the ECS component.
     */
    [[nodiscard]] static Result<VehiclePhysicsStatus> synchronizeWheelState(VehicleEntity& v);
    /** @brief Copy attached body pose into motion for tracked vehicles. */
    static void syncTrackFromBody(VehicleEntity& v);
    /** @brief Apply tracked heading/speed to an attached body. */
    [[nodiscard]] static VehiclePhysicsStatus tryTrackApply(VehicleEntity& v, float headingRad, float speed);
    /** @brief Register suspension mobility when physics is in the build. */
    static void registerBuiltinMobility();
};

}  // namespace eve::vehicle
