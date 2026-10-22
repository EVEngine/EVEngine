#include "zeroerr/assert.h"
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::crowd;

TEST_CASE("crowd.snapshot.providerWitnessSurvivesRestoreAndExpiresOnDestruction") {
    LifetimeWitness witness;
    CHECK(!witness.isAlive());
    {
        Crowd crowd;
        witness = crowd.lifetimeWitness();
        CHECK(witness.isAlive());
        auto snapshot = crowd.captureSnapshot().takeValue();
        auto plan     = crowd.prepareRestore(snapshot).takeValue();
        REQUIRE(crowd.commitRestore(std::move(plan)).ok());
        CHECK(witness.isAlive());
        crowd.clearAgents();
        CHECK(witness.isAlive());
    }
    CHECK(!witness.isAlive());
}

TEST_CASE("crowd.snapshot.restoreRecoversMotionPolicyClockAndInvalidatesRefs") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    const int         slot = crowd.addNamedAgent("saved", 3.f, 4.f, 0.f, 0.5f);
    auto              old  = crowd.agentRef(slot).takeValue();
    InteractionPolicy policy;
    policy.avoidanceMask = 0;
    policy.contactLayer  = 2;
    REQUIRE(crowd.setInteractionPolicy(old, policy).ok());
    REQUIRE(crowd.advance({SimulationTick(1), Duration::fromNanoseconds(10000000), {}}).ok());
    auto snapshot = crowd.captureSnapshot().takeValue();
    REQUIRE(crowd.setAgentPosition(slot, 8.f, 9.f));
    REQUIRE(crowd.advance({SimulationTick(2), Duration::fromNanoseconds(10000000), {}}).ok());
    auto prepared = crowd.prepareRestore(snapshot);
    REQUIRE(prepared.ok());
    CHECK_EQ(crowd.readAgent(old).value().x, 8.f);
    auto plan = std::move(prepared).takeValue();
    REQUIRE(crowd.commitRestore(std::move(plan)).ok());
    CHECK(!crowd.isAgentAlive(old));
    auto restored = crowd.resolveNamedAgent("saved").takeValue();
    CHECK_EQ(crowd.readAgent(restored).value().x, 3.f);
    CHECK_EQ(crowd.readInteractionPolicy(restored).value().avoidanceMask, 0);
    CHECK_EQ(crowd.readInteractionPolicy(restored).value().contactLayer, 2);
    AgentFrame frame;
    REQUIRE(crowd.copyAgentFrame(frame).ok());
    CHECK_EQ(frame.tick.value(), 1);
    REQUIRE(crowd.advance({SimulationTick(2), Duration::fromNanoseconds(10000000), {}}).ok());
    REQUIRE(!crowd.commitRestore(std::move(plan)).ok());
}

TEST_CASE("crowd.snapshot.foreignEmptyAndStalePlansPreserveLiveState") {
    Crowd crowd, other;
    REQUIRE(crowd.addNamedAgent("saved", 1.f, 2.f, 0.f, 1.f) >= 0);
    Crowd::Snapshot empty;
    REQUIRE(!crowd.prepareRestore(empty).ok());
    auto snapshot = crowd.captureSnapshot().takeValue();
    auto first    = crowd.prepareRestore(snapshot).takeValue();
    auto stale    = crowd.prepareRestore(snapshot).takeValue();
    REQUIRE(!other.commitRestore(std::move(first)).ok());
    CHECK_EQ(other.getAgentCount(), 0);
    REQUIRE(crowd.commitRestore(std::move(first)).ok());
    REQUIRE(!crowd.commitRestore(std::move(stale)).ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
    auto second = crowd.prepareRestore(snapshot).takeValue();
    REQUIRE(crowd.commitRestore(std::move(second)).ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
}
