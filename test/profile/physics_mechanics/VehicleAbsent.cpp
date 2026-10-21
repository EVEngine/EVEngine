#include "vehicle/VehiclePhysics.h"
int main() {
    ecs::Table table;
    auto*      vehicle      = eve::vehicle::VehicleEntity::create(table);
    auto       submitted    = eve::vehicle::VehiclePhysics::submitWheelStep(*vehicle, {});
    auto       synchronized = eve::vehicle::VehiclePhysics::synchronizeWheelState(*vehicle);
    const bool valid =
        submitted.ok() && synchronized.ok() && submitted.value() == eve::vehicle::VehiclePhysicsStatus::Unavailable &&
        synchronized.value() == eve::vehicle::VehiclePhysicsStatus::Unavailable &&
        eve::vehicle::VehiclePhysics::tryWheelMove(*vehicle, .01f) == eve::vehicle::VehiclePhysicsStatus::Unavailable;
    ecs::DestroyEntity(vehicle);
    return valid ? 0 : 1;
}
