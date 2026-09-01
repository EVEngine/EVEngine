#include "vehicle/VehiclePhysics.h"

namespace eve::vehicle {

VehiclePhysicsStatus VehiclePhysics::attach2D(VehicleEntity*, eve::physics::World*) {
    return VehiclePhysicsStatus::Unavailable;
}

VehiclePhysicsStatus VehiclePhysics::attach3D(VehicleEntity*, eve::physics::World3D*, float) {
    return VehiclePhysicsStatus::Unavailable;
}

VehiclePhysicsStatus VehiclePhysics::detach(VehicleEntity*) { return VehiclePhysicsStatus::Unavailable; }

float VehiclePhysics::height(VehicleEntity*) { return 0.f; }

VehiclePhysicsStatus VehiclePhysics::tryWheelMove(VehicleEntity&, float) {
    return VehiclePhysicsStatus::Unavailable;
}

void VehiclePhysics::syncTrackFromBody(VehicleEntity&) {}

VehiclePhysicsStatus VehiclePhysics::tryTrackApply(VehicleEntity&, float, float) {
    return VehiclePhysicsStatus::Unavailable;
}

void VehiclePhysics::registerBuiltinMobility() {}

}  // namespace eve::vehicle
