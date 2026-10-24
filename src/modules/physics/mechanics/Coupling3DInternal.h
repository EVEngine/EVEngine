#pragma once
// Internal step-local row representation; never exposed as a gameplay API.
#include <box3d/box3d.h>
#include <array>
#include "physics/PhysicsHandles.h"
#include "physics/mechanics/Coupling3D.h"

namespace eve::physics {
namespace coupling_detail {
struct Term {
    PhysicsBodyHandle body;
    b3Vec3            linear{}, angular{};
};
struct Row {
    std::array<Term, 4>   terms{};
    int                   count = 0;
    std::array<double, 2> coordinate{};
    std::array<float, 2>  wrapped{};
    double                error  = 0;
    bool                  active = true;
    void                  add(PhysicsBodyHandle body, b3Vec3 linear, b3Vec3 angular, float scale);
};
[[nodiscard]] Result<void> validateResponse(World3D& world, const Row& row);
}  // namespace coupling_detail
struct Coupling3D::Data {
    World3D*                                   world = nullptr;
    int                                        id    = 0;
    bool                                       gear = false, enabled = true;
    GearCoupling3DSettings                     gearSettings;
    PulleyCoupling3DSettings                   pulleySettings;
    std::array<PhysicsJointHandle, 2>          sources{};
    std::array<PhysicsBodyHandle, 4>           bodies{};
    std::array<bool, 2>                        revolute{};
    std::array<float, 2>                       previous{};
    std::array<double, 2>                      coordinate{};
    double                                     reference = 0, lastImpulse = 0;
    bool                                       saturated = false;
    [[nodiscard]] Result<coupling_detail::Row> row() const;
    [[nodiscard]] const CouplingLimits3D& limits() const { return gear ? gearSettings.limits : pulleySettings.limits; }
};
}  // namespace eve::physics
