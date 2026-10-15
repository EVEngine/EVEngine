#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body3D.h"
#include "physics/DistanceField3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/rope/Rope.h"
#include "physics/rope/Rope3D.h"
#include "schema/SchemaRegistry.h"

#include <cmath>
#include <limits>
#include <memory>

using namespace eve::physics;


TEST_CASE("physics.rope.continuousCollisionStopsAtTriangleMesh") {
    Physics                         physics;
    std::unique_ptr<World3D>        world(physics.newWorld3D(0.f, 0.f, 0.f, false));
    const std::vector<float>        vertices{-4.f, 0.f, -4.f, 4.f, 0.f, -4.f, 4.f, 0.f, 4.f, -4.f, 0.f, 4.f};
    const std::vector<std::int32_t> indices{0, 2, 1, 0, 3, 2};
    REQUIRE(world->newBody("static", 0.f, 0.f, 0.f)->newTriangleMeshShape(vertices, indices) != nullptr);

    Rope3D rope(3, -1.f, 1.f, 0.f, 1.f, 1.f, 0.f);
    rope.setCollideWorld(world.get());
    rope.setRadius(0.1f);
    rope.setGravity(0.f, -3000.f, 0.f);
    rope.update(0.05f);
    CHECK(rope.getParticleY(1) >= 0.099f);
}


TEST_CASE("physics.rope.dynamicRigidBodyReceivesReactionImpulse") {
    Physics                  physics;
    std::unique_ptr<World3D> world(physics.newWorld3D(0.f, 0.f, 0.f, false));
    Body3D*                  body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    REQUIRE(body->newSphereShape(0.5f, 1.f) != nullptr);

    Rope3D rope(3, -0.5f, 1.f, 0.f, 0.5f, 1.f, 0.f);
    rope.setCollideWorld(world.get());
    rope.setRadius(0.1f);
    rope.setParticleMass(0.25f);
    rope.setGravity(0.f, -1000.f, 0.f);
    for (int i = 0; i < 4; ++i) rope.update(0.05f);
    CHECK(body->getLinearVelocityY() < -0.01f);
}
