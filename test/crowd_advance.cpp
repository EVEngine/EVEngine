#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

using namespace eve;
using namespace eve::crowd;

TEST_CASE("crowd.advance.nonfiniteLegacySteeringRejectsBeforeIndexing") {
    Crowd crowd;
    REQUIRE(crowd.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    crowd.setSeparationWeight(std::numeric_limits<float>::quiet_NaN());
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(16666667), {}};
    REQUIRE(!crowd.advance(step).ok());
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.advance(step).ok());
}

TEST_CASE("crowd.advance.externalClockRejectsDuplicateSkippedAndMixedTicks") {
    Crowd     crowd;
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(16666667), {}};
    auto      first = crowd.advance(step);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().tick.value(), 1);
    CHECK_EQ(first.value().elapsed.nanoseconds(), 16666667);
    REQUIRE(!crowd.advance(step).ok());
    step.tick = SimulationTick(3);
    REQUIRE(!crowd.advance(step).ok());
    REQUIRE(!crowd.advance(0.01f).ok());
    step.tick = SimulationTick(2);
    REQUIRE(crowd.advance(step).ok());
    auto noop = crowd.advance({SimulationTick(2), Duration::zero(), {}});
    REQUIRE(noop.ok());
    CHECK_EQ(noop.value().elapsed.nanoseconds(), 33333334);
}

TEST_CASE("crowd.advance.workBudgetFailureRollsBackMotionAndClock") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(2.f);
    const int agent = crowd.addAgent(1.f, 2.f, 0.f, 0.5f);
    REQUIRE(agent >= 0);
    AgentFrame before;
    REQUIRE(crowd.copyAgentFrame(before).ok());
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(16666667), {}};
    step.budget.maxNeighborVisits = 0;
    REQUIRE(!crowd.advance(step).ok());
    AgentFrame after;
    REQUIRE(crowd.copyAgentFrame(after).ok());
    CHECK_EQ(after.tick.value(), before.tick.value());
    CHECK_EQ(crowd.getAgentState(agent).x, 1.f);
    CHECK_EQ(crowd.getAgentState(agent).y, 2.f);
    step.budget.maxNeighborVisits = 10000;
    auto retry                    = crowd.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().tick.value(), 1);
}

TEST_CASE("crowd.advance.emptyWorldConsumesTimeAndRejectsOverflow") {
    Crowd     crowd;
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(INT64_MAX), {}};
    auto      first = crowd.advance(step);
    REQUIRE(first.ok());
    step.tick  = SimulationTick(2);
    step.delta = Duration::fromNanoseconds(1);
    REQUIRE(!crowd.advance(step).ok());
    AgentFrame frame;
    REQUIRE(crowd.copyAgentFrame(frame).ok());
    CHECK_EQ(frame.tick.value(), 1);
}

TEST_CASE("crowd.advance.residualBudgetFailureRestoresAlreadyIntegratedMotion") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(false);
    crowd.setDefaultSpeed(2.f);
    const int first = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.addAgent(0.5f, 0.f, 0.f, 1.f) >= 0);
    const auto initial = crowd.getAgentState(first);
    CrowdStep  step{SimulationTick(1), Duration::fromNanoseconds(16666667), {}};
    step.budget.maxContactChecks = 0;
    REQUIRE(!crowd.advance(step).ok());
    auto restored = crowd.getAgentState(first);
    CHECK_EQ(restored.x, initial.x);
    CHECK_EQ(restored.y, initial.y);
    CHECK_EQ(restored.vx, initial.vx);
    CHECK_EQ(restored.vy, initial.vy);
    step.budget.maxContactChecks = 100;
    auto retry                   = crowd.advance(step);
    REQUIRE(retry.ok());
    CHECK(retry.value().residualPairsChecked > 0);
}

TEST_CASE("crowd.advance.scriptClockHasLosslessMetadataAndStrictIntegerInputs") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        local result = crowd.advanceTick({tick=1,deltaNanoseconds=16666667});
        assert(result.ok);
        assert(result.value.tick == "1");
        assert(result.value.elapsedNanoseconds == 16666667);
        assert(crowd.copyAgentFrame().value.tick == "1");
        assert(!crowd.advanceTick({tick=2.0,deltaNanoseconds=16666667}).ok);
        assert(!crowd.advanceTick({tick=2,deltaNanoseconds=1.5}).ok);
        assert(crowd.advanceTick({tick=2,deltaNanoseconds=16666667}).ok);
    )"));
}
