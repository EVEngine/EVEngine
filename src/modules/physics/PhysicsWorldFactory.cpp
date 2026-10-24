#include "common/Exception.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/World3D.h"

namespace eve::physics {
World *Physics::newWorld(float gravityX, float gravityY, bool sleep) {
    return new World(gravityX, gravityY, sleep, meter_);
}

World3D *Physics::newWorld3D(float gravityX, float gravityY, float gravityZ, bool sleep) {
    return new World3D(gravityX, gravityY, gravityZ, sleep);
}

World3D *Physics::newWorld3DWithBackend(const std::string &backend, float gravityX, float gravityY, float gravityZ,
                                        bool sleep) {
    if (backend != "box3d" && backend != "jolt") throw eve::Exception("Unknown rigid backend: %s", backend.c_str());
    auto result = World3D::createWithBackend(backend == "jolt" ? RigidBackend3D::Jolt : RigidBackend3D::Box3D, gravityX,
                                             gravityY, gravityZ, sleep);
    if (!result) throw eve::Exception("%s", result.status().describe().c_str());
    return std::move(result).takeValue().release();
}

}  // namespace eve::physics
