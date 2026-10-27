// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "module-composition": [
      "climbing.grounding.lowCeilingRejectsStepWithoutPublishingRaisedPose",
      "climbing.grounding.lowStepUsesProductionMotor",
      "climbing.grounding.tallStepRemainsBlocked"
    ]
  },
  "module": "climbing"
}
*/
// clang-format on

#include "climbing/ClimbingECS.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

class GroundingActor : public ecs::Entity {
public:
    ENTITY(GroundingActor, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    COMPONENT(eve::climbing::ClimbingBody, body)
    COMPONENT(eve::climbing::ClimbingIntent, intent)
    COMPONENT(eve::climbing::ClimbingState, state)
    COMPONENT(eve::climbing::ClimbingLinks, links)
};

eve::climbing::ClimbingBody crossStep(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT, float stepHeight, bool lowCeiling) {
    ecs::Table            table;
    ecs::ScopedTable      tableScope(table);
    eve::physics::World3D world(0.f, 0.f, 0.f, false);
    world.newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(10.f, 1.f, 10.f);
    world.newBody("static", 0.f, stepHeight * 0.5f, 0.f)->newBoxShape(0.5f, stepHeight, 2.f);
    if (lowCeiling) world.newBody("static", 0.f, 2.f, 0.f)->newBoxShape(10.f, 0.2f, 10.f);
    auto* character = world.newBody("kinematic", -0.8f, 0.9f, 0.f);
    character->newCapsuleShape(1.8f, 0.3f);

    auto* actor = GroundingActor::create();
    auto  state = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *actor->state() = std::move(state).takeValue();
    auto link       = eve::physics::PhysicsLink::fromBody(*character);
    REQUIRE(link.ok());
    actor->links()->physicsBody = std::move(link).takeValue();
    actor->body()->feet         = {-0.8f, 0.f, 0.f};
    actor->body()->velocity     = {2.f, 0.f, 0.f};
    actor->body()->grounded     = true;
    actor->intent()->move       = {2.f, 0.f, 0.f};

    auto moved = eve::climbing::ClimbingMotionSystem::step<GroundingActor>(
        world, {eve::SimulationTick(1), eve::Duration::fromNanoseconds(500000000)});
    REQUIRE(moved.ok());
    REQUIRE_EQ(moved.value(), std::size_t(1));
    if (lowCeiling) {
        // A rejected rise may leave a small airborne offset from the rounded
        // step edge. It must settle normally rather than publish the trial rise.
        REQUIRE_LT(actor->body()->feet.y, 0.1f);
        actor->intent()->move = {};
        for (int tick = 2; tick <= 31; ++tick) {
            auto settled = eve::climbing::ClimbingMotionSystem::step<GroundingActor>(
                world, {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(16666667)});
            REQUIRE(settled.ok());
            REQUIRE_LT(actor->body()->feet.y, 0.1f);
            REQUIRE_LT(actor->body()->feet.x, 0.f);
        }
    }
    const auto result = *actor->body();
    REQUIRE(actor->state()->releaseRuntime().ok());
    actor->release();
    return result;
}

}  // namespace

TEST_CASE("climbing.grounding.lowStepUsesProductionMotor") {
    const auto body = crossStep(_ZEROERR_TEST_CONTEXT, 0.2f, false);
    REQUIRE_GT(body.feet.x, 0.f);
    REQUIRE_GT(body.feet.y, 0.15f);
    REQUIRE(body.grounded);
    REQUIRE_GT(body.groundNormal.y, 0.9f);
}

TEST_CASE("climbing.grounding.tallStepRemainsBlocked") {
    const auto body = crossStep(_ZEROERR_TEST_CONTEXT, 0.8f, false);
    REQUIRE_LT(body.feet.x, -0.3f);
    REQUIRE_LT(body.feet.y, 0.1f);
    REQUIRE(body.grounded);
}

TEST_CASE("climbing.grounding.lowCeilingRejectsStepWithoutPublishingRaisedPose") {
    const auto body = crossStep(_ZEROERR_TEST_CONTEXT, 0.2f, true);
    REQUIRE_LT(body.feet.x, 0.f);
    REQUIRE_LT(body.feet.y, 0.1f);
    REQUIRE(body.grounded);
}
