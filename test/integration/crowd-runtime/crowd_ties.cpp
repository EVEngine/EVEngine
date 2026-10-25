// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "crowd",
  "origins": [
    "crowd_ties.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::crowd;

TEST_CASE("crowd.ties.anonymousContactDirectionSurvivesDenseCompaction") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int  dummy    = crowd.addAgent(100.f, 100.f, 0.f, 1.f);
    const auto a        = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 1.f)).takeValue();
    const auto b        = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 1.f)).takeValue();
    auto       snapshot = crowd.captureSnapshot().takeValue();
    REQUIRE(crowd.advance(0.01f).ok());
    const auto expectedA = crowd.readAgent(a).takeValue();
    const auto expectedB = crowd.readAgent(b).takeValue();
    auto       plan      = crowd.prepareRestore(snapshot).takeValue();
    REQUIRE(crowd.commitRestore(std::move(plan)).ok());
    const auto newA = crowd.agentRef(1).takeValue();
    const auto newB = crowd.agentRef(2).takeValue();
    REQUIRE(crowd.removeAgent(dummy));
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.readAgent(newA).value().x, expectedA.x);
    CHECK_EQ(crowd.readAgent(newA).value().y, expectedA.y);
    CHECK_EQ(crowd.readAgent(newB).value().x, expectedB.x);
    CHECK_EQ(crowd.readAgent(newB).value().y, expectedB.y);
}

TEST_CASE("crowd.ties.namedWanderIsIndependentOfCreationOrder") {
    Crowd first, second;
    for (auto* crowd : {&first, &second}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setDefaultSpeed(2.f);
        crowd->setSeparationWeight(0.f);
        crowd->setWanderWeight(1.f);
    }
    REQUIRE(first.addNamedAgent("left", -10.f, 0.f, 0.f, 0.5f) >= 0);
    REQUIRE(first.addNamedAgent("right", 10.f, 0.f, 0.f, 0.5f) >= 0);
    REQUIRE(second.addNamedAgent("right", 10.f, 0.f, 0.f, 0.5f) >= 0);
    REQUIRE(second.addNamedAgent("left", -10.f, 0.f, 0.f, 0.5f) >= 0);
    for (auto* crowd : {&first, &second}) {
        AgentUpdateBatch updates;
        for (const auto* name : {"left", "right"}) {
            AgentUpdate update;
            update.reference = crowd->resolveNamedAgent(name).takeValue();
            update.intent    = {AgentAction::Boids, false, 0.f, 0.f};
            updates.agents.push_back(update);
        }
        REQUIRE(crowd->updateAgents(updates).ok());
    }
    for (std::uint64_t tick = 1; tick <= 10; ++tick) {
        CrowdStep step{SimulationTick(tick), Duration::fromNanoseconds(16666667), {}};
        REQUIRE(first.advance(step).ok());
        REQUIRE(second.advance(step).ok());
    }
    for (const auto* name : {"left", "right"}) {
        auto a = first.readAgent(first.resolveNamedAgent(name).takeValue()).takeValue();
        auto b = second.readAgent(second.resolveNamedAgent(name).takeValue()).takeValue();
        CHECK_EQ(a.x, b.x);
        CHECK_EQ(a.y, b.y);
        CHECK_EQ(a.vx, b.vx);
        CHECK_EQ(a.vy, b.vy);
    }
    auto            snapshot = first.captureSnapshot().takeValue();
    const CrowdStep next{SimulationTick(11), Duration::fromNanoseconds(16666667), {}};
    REQUIRE(first.advance(next).ok());
    const auto expected = first.readAgent(first.resolveNamedAgent("left").takeValue()).takeValue();
    auto       plan     = first.prepareRestore(snapshot).takeValue();
    REQUIRE(first.commitRestore(std::move(plan)).ok());
    REQUIRE(first.advance(next).ok());
    const auto replayed = first.readAgent(first.resolveNamedAgent("left").takeValue()).takeValue();
    CHECK_EQ(replayed.x, expected.x);
    CHECK_EQ(replayed.y, expected.y);
    CHECK_EQ(replayed.vx, expected.vx);
    CHECK_EQ(replayed.vy, expected.vy);
    CHECK_EQ(replayed.heading, expected.heading);
}
