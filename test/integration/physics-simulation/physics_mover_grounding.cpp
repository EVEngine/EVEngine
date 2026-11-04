// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "module": "physics"
}
*/
// clang-format on

#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::physics;

TEST_CASE("physics.mover.grounding.supportAndFilteringContract") {
    SUB_CASE("leavingLedgeClearsSupportAtDestination") {
        World3D world(0.f, 0.f, 0.f, false);
        world.newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(2.f, 1.f, 4.f);

        auto result = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 3.f, 0.f, 0.f);
        REQUIRE(result.ok());
        REQUIRE_GT(result.value().deltaX, 2.9f);
        REQUIRE(!result.value().grounded);
        REQUIRE_LT(world.getMoverGroundDot(), 0.f);
    };
    SUB_CASE("horizontalMotionHasUsableSupportNormal") {
        World3D world(0.f, 0.f, 0.f, false);
        world.newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(10.f, 1.f, 10.f);

        auto result = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 0.5f, 0.f, 0.f);
        REQUIRE(result.ok());
        REQUIRE(result.value().grounded);
        REQUIRE_GT(result.value().normalY, 0.99f);
        REQUIRE_LT(std::fabs(result.value().normalX), 0.01f);
        REQUIRE_LT(std::fabs(result.value().normalZ), 0.01f);
    };
    SUB_CASE("jumpingClearsStartingFloorContact") {
        World3D world(0.f, 0.f, 0.f, false);
        world.newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(10.f, 1.f, 10.f);

        auto result = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 0.f, 1.f, 0.f);
        REQUIRE(result.ok());
        REQUIRE_GT(result.value().deltaY, 0.99f);
        REQUIRE(!result.value().grounded);
    };
    SUB_CASE("kinematicSupportRemovalIsObservedImmediately") {
        World3D world(0.f, 0.f, 0.f, false);
        auto*   platform = world.newBody("kinematic", 0.f, -0.5f, 0.f);
        platform->newBoxShape(2.f, 1.f, 4.f);
        auto before = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 0.f, 0.f, 0.f);
        REQUIRE(before.ok());
        REQUIRE(before.value().grounded);
        platform->setPosition(4.f, -0.5f, 0.f);
        auto after = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 0.f, 0.f, 0.f);
        REQUIRE(after.ok());
        REQUIRE(!after.value().grounded);
        REQUIRE(before.value().grounded);
    };
    SUB_CASE("destinationProbeHonorsSensorAndBodyExclusions") {
        World3D world(0.f, 0.f, 0.f, false);
        world.newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(2.f, 1.f, 4.f);
        auto* destination = world.newBody("static", 3.f, -0.5f, 0.f);
        auto* shape       = destination->newBoxShape(2.f, 1.f, 4.f);
        shape->setSensor(true);
        auto sensor = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 3.f, 0.f, 0.f);
        REQUIRE(sensor.ok());
        REQUIRE(!sensor.value().grounded);

        shape->setSensor(false);
        QueryFilter3D filter;
        filter.ignoredBodyId = destination->getId();
        auto ignored         = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 3.f, 0.f, 0.f, filter);
        REQUIRE(ignored.ok());
        REQUIRE(!ignored.value().grounded);
        auto supported = world.moveCapsuleOwned(0.f, 0.25f, 0.f, 0.f, 1.25f, 0.f, 0.25f, 3.f, 0.f, 0.f);
        REQUIRE(supported.ok());
        REQUIRE(supported.value().grounded);
    };
    SUB_CASE("sensorWallDoesNotBlockCapsuleTravel") {
        World3D world(0.f, 0.f, 0.f, false);
        world.newBody("static", 0.f, 0.f, 0.f)->newBoxShape(0.2f, 4.f, 4.f)->setSensor(true);
        auto result = world.moveCapsuleOwned(-2.f, 0.f, 0.f, -2.f, 1.f, 0.f, 0.25f, 4.f, 0.f, 0.f);
        REQUIRE(result.ok());
        REQUIRE(!result.value().constrained);
        REQUIRE_GT(result.value().deltaX, 3.99f);
        REQUIRE(!result.value().grounded);
    };
}
