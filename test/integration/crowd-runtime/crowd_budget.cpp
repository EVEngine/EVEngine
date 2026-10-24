#include "zeroerr/assert.h"
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <limits>

using namespace eve::crowd;

TEST_CASE("crowd.budget.observationsResetAndRejectedStepsPreserveState") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int agent = crowd.addNamedAgent("budget.agent", 1.f, 2.f, 0.f, 1.f);
    REQUIRE(agent >= 0);
    auto first = crowd.advance(1.f / 60.f);
    REQUIRE(first.ok());
    CHECK(first.value().gridBuilds > 0);
    CHECK(first.value().contactPasses > 0);
    CHECK(first.value().cellsVisited > 0);
    auto second = crowd.advance(1.f / 60.f);
    REQUIRE(second.ok());
    CHECK_EQ(first.value().gridBuilds, second.value().gridBuilds);
    CHECK_EQ(first.value().candidatesVisited, second.value().candidatesVisited);
    auto rejected = crowd.advance(std::numeric_limits<float>::infinity());
    REQUIRE(!rejected.ok());
    CHECK_EQ(crowd.getAgentState(agent).x, 1.f);
    CHECK_EQ(crowd.getAgentState(agent).y, 2.f);
    auto noop = crowd.advance(0.f);
    REQUIRE(noop.ok());
    CHECK_EQ(noop.value().gridBuilds, 0);
    CHECK_EQ(noop.value().candidatesVisited, 0);
    CHECK_EQ(noop.value().motionNanoseconds, 0);
}

TEST_CASE("crowd.budget.neighborLimitDoesNotBoundBroadphaseWork") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(false);
    crowd.setDefaultSpeed(1.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 1}).ok());
    for (int i = 0; i < 8; ++i) {
        const int agent = crowd.addAgent(float(i) * 0.1f, 0.f, 0.f, 0.5f);
        REQUIRE(agent >= 0);
    }
    auto result = crowd.advance(1.f / 60.f);
    REQUIRE(result.ok());
    CHECK(result.value().avoidanceTruncations > 0);
    CHECK(result.value().candidatesVisited > 8);
    CHECK_EQ(result.value().contactPasses, 0);
    CHECK_EQ(result.value().contactPairsChecked, 0);
    CHECK(result.value().unresolvedContacts > 0);
}

TEST_CASE("crowd.budget.contactWorkExcludesNonInteractingPairs") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    const int b = crowd.addAgent(0.5f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 1}).ok());
    REQUIRE(crowd.setAgentInteraction(b, {1.f, false, 2, 2}).ok());
    auto result = crowd.advance(1.f / 60.f);
    REQUIRE(result.ok());
    CHECK(result.value().candidatesVisited > 0);
    CHECK_EQ(result.value().contactPairsChecked, 0);
    CHECK_EQ(result.value().unresolvedContacts, 0);
}
