// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "rts",
  "origins": [
    "rts_crowd_zero_delta.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include "RtsCompositionFixtures.h"

TEST_CASE("rts.crowd.zeroDeltaReconcilesOnlyReachedGoals") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::crowd::Crowd crowd;
    auto* reached = Unit::createUnit();
    auto* moving  = Unit::createUnit();
    reached->crowd()->link = eve::rts::CrowdLink::bind("zero/reached").takeValue();
    moving->crowd()->link  = eve::rts::CrowdLink::bind("zero/moving").takeValue();
    moving->motion()->x = 5.f;
    reached->motion()->speed = moving->motion()->speed = 0.f;
    CommandSpec reachedOrder;
    reachedOrder.kind   = OrderKind::Move;
    reachedOrder.target = {0.f, 0.f};
    CommandSpec movingOrder;
    movingOrder.kind   = OrderKind::Move;
    movingOrder.target = {10.f, 0.f};
    REQUIRE(reached->orders()->values.enqueue(reachedOrder).ok());
    REQUIRE(moving->orders()->values.enqueue(movingOrder).ok());
    const eve::SimulationTick tick{1};
    REQUIRE(eve::rts::CrowdMotionSystem::step(
                {tick, eve::Duration::fromSeconds(0.01).expect("test dt")}, crowd)
                .ok());
    REQUIRE(eve::rts::CrowdMotionSystem::step({tick, eve::Duration{}}, crowd).ok());
    CHECK(reached->motion()->arrived);
    CHECK(!moving->motion()->arrived);
    const auto reachedState = crowd.getAgentState(crowd.getNamedAgentIndex("zero/reached"));
    const auto movingState  = crowd.getAgentState(crowd.getNamedAgentIndex("zero/moving"));
    CHECK_EQ(reachedState.action, static_cast<int>(eve::crowd::AgentAction::Idle));
    CHECK_EQ(movingState.action, static_cast<int>(eve::crowd::AgentAction::Seek));
    reached->release();
    moving->release();
}
