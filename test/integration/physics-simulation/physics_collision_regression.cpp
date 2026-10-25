// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "cases": {
    "physics-backend": [
      "physics.collision.ccd.sphereStopsAtThinWall",
      "physics.collision.ccd.dynamicPairMaintainsOrderWithFourSubsteps",
      "physics.collision.stack.remainsSupportedAfterTenSeconds",
      "physics.collision.query.primitiveHitsHaveNoSourceTriangle"
    ]
  },
  "module": "physics"
}
*/
// clang-format on
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <vector>

using namespace eve::physics;

TEST_CASE("physics.collision.ccd.sphereStopsAtThinWall") {
    // One tick travels 2 m; the wall is only 2 cm thick. Substeps must not
    // accidentally turn this into a discrete-collision test.
    World3D world(0.f, 0.f, 0.f, false);
    world.setContinuousCollisionEnabled(true);
    world.setMaximumLinearSpeed(200.f);
    auto* wall = world.newBody("static", 0.f, 0.f, 0.f);
    wall->newBoxShape(0.02f, 4.f, 4.f);
    auto* projectile = world.newBody("dynamic", -1.f, 0.f, 0.f);
    projectile->newSphereShape(0.1f);
    projectile->setBullet(true);
    projectile->setLinearVelocity(120.f, 0.f, 0.f);

    for (int tick = 0; tick < 10; ++tick) {
        world.updateFull(1.f / 60.f, 1);
        REQUIRE(std::isfinite(projectile->getX()));
        // CCD prevents crossing the wall. The soft contact solver may allow
        // temporary overlap after TOI; zero overlap is not its contract.
        REQUIRE_LT(projectile->getX(), 0.f);
    }
}

TEST_CASE("physics.collision.ccd.dynamicPairMaintainsOrderWithFourSubsteps") {
    World3D world(0.f, 0.f, 0.f, false);
    world.setContinuousCollisionEnabled(true);
    world.setMaximumLinearSpeed(200.f);
    // Only one body is a bullet. Keep the moved target clear of the bullet's
    // starting position: Box3D explicitly excludes initial-overlap CCD recovery.
    auto* left  = world.newBody("dynamic", -1.5f, 0.f, 0.f);
    auto* right = world.newBody("dynamic", 1.5f, 0.f, 0.f);
    left->newSphereShape(0.1f);
    right->newSphereShape(0.1f);
    left->setBullet(true);
    left->setLinearVelocity(120.f, 0.f, 0.f);
    right->setLinearVelocity(-120.f, 0.f, 0.f);

    for (int tick = 0; tick < 10; ++tick) {
        // This explicitly validates the default four-substep workload. The
        // upstream backend does not guarantee fast/fast CCD at one substep.
        world.updateFull(1.f / 60.f, 4);
        REQUIRE(std::isfinite(left->getX()));
        REQUIRE(std::isfinite(right->getX()));
        REQUIRE_LT(left->getX(), right->getX());
    }
}

TEST_CASE("physics.collision.stack.remainsSupportedAfterTenSeconds") {
    World3D world(0.f, -9.8f, 0.f, true);
    auto*   ground = world.newBody("static", 0.f, -0.5f, 0.f);
    ground->newBoxShape(20.f, 1.f, 20.f);
    std::vector<Body3D*> stack;
    for (int i = 0; i < 8; ++i) {
        auto* body = world.newBody("dynamic", 0.f, 0.5f + 1.01f * i, 0.f);
        body->newBoxShape(1.f, 1.f, 1.f, 1.f, 0.6f, 0.f);
        stack.push_back(body);
    }
    for (int tick = 0; tick < 600; ++tick) {
        world.updateFull(1.f / 60.f, 4);
        for (auto* body : stack) {
            REQUIRE(std::isfinite(body->getX()));
            REQUIRE(std::isfinite(body->getY()));
            REQUIRE(std::isfinite(body->getZ()));
            REQUIRE_GT(body->getY(), 0.35f);
        }
    }
    for (int i = 0; i < 8; ++i) {
        REQUIRE_LT(std::fabs(stack[i]->getY() - (0.5f + i)), 0.2f);
        REQUIRE_LT(std::fabs(stack[i]->getX()), 0.25f);
        REQUIRE_LT(std::fabs(stack[i]->getZ()), 0.25f);
        REQUIRE_LT(std::fabs(stack[i]->getLinearVelocityY()), 0.1f);
    }
}

TEST_CASE("physics.collision.query.primitiveHitsHaveNoSourceTriangle") {
    World3D world(0.f, 0.f, 0.f, false);
    auto*   body = world.newBody("static", 0.f, 0.f, 0.f);
    body->newBoxShape(2.f, 1.f, 2.f);
    auto owned = world.rayCastOwned(0.f, 2.f, 0.f, 0.f, -2.f, 0.f);
    REQUIRE(owned.ok());
    REQUIRE(owned.value().hit);
    REQUIRE_EQ(owned.value().triangleIndex, -1);
    const int rayBody = world.rayCast(0.f, 2.f, 0.f, 0.f, -2.f, 0.f);
    REQUIRE_EQ(rayBody, body->getId());
    REQUIRE_EQ(world.getRayHitTriangleIndex(), -1);
    REQUIRE_EQ(world.getRayResultTriangleIndex(0), -1);
    const int castBody = world.castSphere(0.f, 2.f, 0.f, 0.1f, 0.f, -4.f, 0.f);
    REQUIRE_EQ(castBody, body->getId());
    REQUIRE_EQ(world.getShapeCastTriangleIndex(), -1);
    REQUIRE_EQ(world.getShapeCastResultTriangleIndex(0), -1);
}
