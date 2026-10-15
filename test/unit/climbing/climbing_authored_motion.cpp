#include <cmath>
#include <memory>
#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::climbing;
namespace {
eve::Duration seconds(double value) {
    auto result = eve::Duration::fromSeconds(value);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}
struct Fixture {
    std::unique_ptr<eve::physics::World3D> world{eve::physics::Physics::create()->newWorld3D(0, 0, 0, false)};
    ClimbingRuntime                        runtime;
    Fixture() {
        world->newBody("static", 0, .5f, 1)->newBoxShape(2, 1, .5f);
        ClimbingActionDefinition action{"mantle", .4f, 1.2f, 0, seconds(1), .6f, .7f, 0};
        REQUIRE(runtime.upsertAction(action).ok());
        REQUIRE(runtime.tryBegin(*world, {{0, 0, 0}, {0, 0, 1}, 1, -1}, eve::SimulationTick(1)).ok());
    }
};
}  // namespace
TEST_CASE("climbing.prewarpedMotionPreservesTimingAndRejectsInvalidPoliciesAtomically") {
    Fixture             f;
    ClimbingMotionInput input;
    input.rootMotionPolicy = ClimbingRootMotionPolicy::PreserveSuppliedDelta;
    auto rejected          = f.runtime.advance(*f.world, {eve::SimulationTick(2), seconds(.1)}, input);
    CHECK(!rejected.ok());
    input.hasRootMotion   = true;
    input.rootTranslation = {0, .02f, .03f};
    auto advanced         = f.runtime.advance(*f.world, {eve::SimulationTick(2), seconds(.1)}, input);
    REQUIRE(advanced.ok());
    CHECK(std::abs(advanced.value().feet.y - .02f) < .0001f);
    CHECK(std::abs(advanced.value().feet.z - .03f) < .0001f);
    CHECK(advanced.value().appliedWarp.y == 0.f);
    input.obstacleCollision = static_cast<ClimbingObstacleCollision>(99);
    CHECK(!f.runtime.advance(*f.world, {eve::SimulationTick(3), seconds(.1)}, input).ok());
    input.obstacleCollision = ClimbingObstacleCollision::Collide;
    REQUIRE(f.runtime.advance(*f.world, {eve::SimulationTick(3), seconds(.1)}, input).ok());
}
TEST_CASE("climbing.prewarpedCollisionExcludesOnlyTraversedShapeForOneCall") {
    Fixture             f;
    ClimbingMotionInput input;
    input.hasRootMotion     = true;
    input.rootMotionPolicy  = ClimbingRootMotionPolicy::PreserveSuppliedDelta;
    input.obstacleCollision = ClimbingObstacleCollision::IgnoreTraversedShape;
    input.rootTranslation   = {0, 0, .9f};
    auto crossed            = f.runtime.advance(*f.world, {eve::SimulationTick(2), seconds(.1)}, input);
    REQUIRE(crossed.ok());
    CHECK(std::abs(crossed.value().feet.z - .9f) < .001f);
    // A separate query still collides with the original obstacle.
    auto normal = f.world->moveCapsuleOwned(0, .3f, 0, 0, 1.5f, 0, .3f, 0, 0, .9f, {});
    REQUIRE(normal.ok());
    CHECK(normal.value().deltaZ < .6f);
    f.world->newBody("static", 0, .7f, 1.8f)->newBoxShape(2, 1.4f, .1f);
    input.rootTranslation = {0, 0, 1.f};
    auto blocked          = f.runtime.advance(*f.world, {eve::SimulationTick(3), seconds(.1)}, input);
    REQUIRE(blocked.ok());
    CHECK(blocked.value().constrained);
    CHECK(blocked.value().feet.z < 1.6f);
}
