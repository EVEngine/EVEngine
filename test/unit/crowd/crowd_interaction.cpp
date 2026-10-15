#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::crowd;

TEST_CASE("crowd.interaction.holdOverridesOrdersAndPriority") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    const int held   = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    const int moving = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentAction(held, "seek"));
    REQUIRE(crowd.setAgentTarget(held, 100.f, 0.f));
    REQUIRE(crowd.setAgentAvoidancePriority(moving, 100).ok());
    REQUIRE(crowd.setAgentInteraction(held, {1.f, true, 1, 1}).ok());
    auto report = crowd.advance(0.1f);
    REQUIRE(report.ok());
    CHECK_EQ(report.value().unresolvedContacts, 0);
    CHECK_EQ(crowd.getAgentState(held).x, 0.f);
    CHECK_EQ(crowd.getAgentState(held).speed, 0.f);
    CHECK(crowd.getAgentState(moving).x >= 1.999f);
    REQUIRE(crowd.setAgentInteraction(held, {1.f, false, 1, 1}).ok());
    REQUIRE(crowd.removeAgent(moving));
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.getAgentState(held).x > 0.f);
}

TEST_CASE("crowd.interaction.pushabilityIsIndependentOfLocomotion") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    const int b = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(a, {0.25f, false, 1, 1}).ok());
    REQUIRE(crowd.setAgentInteraction(b, {0.75f, false, 1, 1}).ok());
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(std::abs(crowd.getAgentState(a).x + 0.25f) < 1e-5f);
    CHECK(std::abs(crowd.getAgentState(b).x - 1.75f) < 1e-5f);
    REQUIRE(crowd.setAgentInteraction(a, {0.f, false, 1, 1}).ok());
    REQUIRE(crowd.setAgentPosition(a, 0.f, 0.f));
    REQUIRE(crowd.setAgentPosition(b, 1.f, 0.f));
    REQUIRE(crowd.setAgentAvoidancePriority(b, 100).ok());
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.getAgentState(a).x, 0.f);
    CHECK(crowd.getAgentState(b).x >= 1.999f);
    REQUIRE(crowd.removeAgent(b));
    REQUIRE(crowd.setAgentAction(a, "seek"));
    REQUIRE(crowd.setAgentTarget(a, 100.f, 0.f));
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.getAgentState(a).x > 0.f);
}

TEST_CASE("crowd.interaction.masksFilterSteeringContactsAndReports") {
    Crowd crowd;
    crowd.setClampToField(false);
    const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    const int b = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 1}).ok());
    REQUIRE(crowd.setAgentInteraction(b, {1.f, false, 2, 3}).ok());
    auto report = crowd.advance(0.1f);
    REQUIRE(report.ok());
    CHECK_EQ(report.value().unresolvedContacts, 0);
    CHECK_EQ(crowd.getAgentState(a).x, 0.f);
    CHECK_EQ(crowd.getAgentState(b).x, 1.f);
    REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 3}).ok());
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.getAgentState(a).x < 0.f);
    CHECK(crowd.getAgentState(b).x > 1.f);
}

TEST_CASE("crowd.interaction.fixedOverlapRemainsObservable") {
    Crowd     crowd;
    const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    const int b = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(a, {1.f, true, 1, 1}).ok());
    REQUIRE(crowd.setAgentInteraction(b, {0.f, true, 1, 1}).ok());
    auto report = crowd.advance(0.01f);
    REQUIRE(report.ok());
    CHECK_EQ(report.value().unresolvedContacts, 1);
    CHECK_EQ(crowd.getAgentState(a).x, 0.f);
    CHECK_EQ(crowd.getAgentState(b).x, 1.f);
}

TEST_CASE("crowd.interaction.policyValidationAndCompactLifecycle") {
    Crowd     crowd;
    const int a = crowd.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
    const int b = crowd.addNamedAgent("b", 5.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(b, {0.3f, true, 4, 6}).ok());
    CHECK(!crowd.setAgentInteraction(b, {std::numeric_limits<float>::quiet_NaN(), false, 1, 1}).ok());
    CHECK(!crowd.setAgentInteraction(b, {0.f, false, -1, 1}).ok());
    REQUIRE(crowd.removeAgent(a));
    auto policy = crowd.getAgentInteraction(crowd.getNamedAgentIndex("b"));
    REQUIRE(policy.ok());
    CHECK_EQ(policy.value().pushability, 0.3f);
    CHECK(policy.value().holdPosition);
    CHECK_EQ(policy.value().layer, 4);
    CHECK_EQ(policy.value().mask, 6);
    CHECK(!crowd.getAgentInteraction(b).ok());
    crowd.clearAgents();
    const int c     = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    auto      fresh = crowd.getAgentInteraction(c);
    REQUIRE(fresh.ok());
    CHECK_EQ(fresh.value().pushability, 1.f);
    CHECK(!fresh.value().holdPosition);
}
