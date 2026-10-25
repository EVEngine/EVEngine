// eve-hot-path
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include "physics/World3D.h"
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
bool WorldBackend::isContactAllowed(const Shape3D& a, const Shape3D& b, double x, double y, double z, float nx,
                                    float ny, float nz) {
    return isPairAllowed(a, b) && a.allowsOneWayContact(x, y, z, nx, ny, nz) &&
           b.allowsOneWayContact(x, y, z, -nx, -ny, -nz);
}
bool WorldBackend::isPairAllowed(const Shape3D& a, const Shape3D& b) {
    auto key = [](int first, int second) {
        return (uint64_t(uint32_t(std::max(first, second))) << 32) | uint32_t(std::min(first, second));
    };
    const auto& world = *a.world_;
    if (world.disabledBodyPairs_.contains(key(a.body_->getId(), b.body_->getId())) ||
        world.disabledShapePairs_.contains(key(a.getId(), b.getId())))
        return false;
    return true;
}
}  // namespace eve::physics::jolt
