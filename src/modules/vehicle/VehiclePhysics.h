#pragma once

/**
 * @brief Optional physics backend for vehicle mobility and body attach.
 *
 * VehiclePhysics.cpp is compiled only when the physics module is enabled;
 * VehiclePhysicsAbsent.cpp is compiled otherwise. Callers always link one of
 * the two and must not include physics headers.
 */

#include "vehicle/VehicleTypes.h"

namespace eve::physics {
class World;
class World3D;
}  // namespace eve::physics

namespace eve::vehicle {

/** @brief Outcome of an optional physics attach, detach, or mobility step. */
enum class VehiclePhysicsStatus : uint8_t {
    Applied,      ///< Physics handled the request.
    Unavailable,  ///< No physics module, no body, or invalid arguments.
};

class VehiclePhysics {
public:
    /** @brief Attach a 2D dynamic body when physics is in the build. */
    [[nodiscard]] static VehiclePhysicsStatus attach2D(VehicleEntity* v, eve::physics::World* world);
    /** @brief Attach a 3D dynamic body when physics is in the build. */
    [[nodiscard]] static VehiclePhysicsStatus attach3D(VehicleEntity* v, eve::physics::World3D* world, float heightY);
    /** @brief Destroy attached bodies if any. */
    [[nodiscard]] static VehiclePhysicsStatus detach(VehicleEntity* v);
    /** @brief 3D body height, or 0 when no 3D body is attached. */
    static float height(VehicleEntity* v);
    /** @brief Step wheel/ship mobility from an attached body. */
    [[nodiscard]] static VehiclePhysicsStatus tryWheelMove(VehicleEntity& v, float dt);
    /** @brief Copy attached body pose into motion for tracked vehicles. */
    static void syncTrackFromBody(VehicleEntity& v);
    /** @brief Apply tracked heading/speed to an attached body. */
    [[nodiscard]] static VehiclePhysicsStatus tryTrackApply(VehicleEntity& v, float headingRad, float speed);
    /** @brief Register suspension mobility when physics is in the build. */
    static void registerBuiltinMobility();
};

}  // namespace eve::vehicle
