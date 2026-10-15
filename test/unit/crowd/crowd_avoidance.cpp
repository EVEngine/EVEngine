#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace eve::crowd;

namespace {
void configure(Crowd& crowd) {
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(false);  // Prove anticipatory steering, not contact repair.
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(1.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
}
int mover(Crowd& crowd, const std::string& name, float x, float y, float tx, float ty) {
    const int agent = crowd.addNamedAgent(name, x, y, 0.f, 0.5f);
    REQUIRE(crowd.setAgentAction(agent, "seek"));
    REQUIRE(crowd.setAgentTarget(agent, tx, ty));
    REQUIRE(crowd.setAgentAccel(agent, 20.f));
    return agent;
}
float separation(const Crowd& crowd, int a, int b) {
    const auto left = crowd.getAgentState(a), right = crowd.getAgentState(b);
    return std::hypot(left.x - right.x, left.y - right.y);
}
}  // namespace

TEST_CASE("crowd.avoidance.headOnPassesWithoutContactRepair") {
    Crowd crowd;
    configure(crowd);
    const int    a       = mover(crowd, "a", -8.f, 0.f, 8.f, 0.f);
    const int    b       = mover(crowd, "b", 8.f, 0.f, -8.f, 0.f);
    float        minimum = 100.f, maxLateral = 0.f;
    std::int64_t checks = 0;
    for (int frame = 0; frame < 600; ++frame) {
        auto result = crowd.advance(1.f / 60.f);
        REQUIRE(result.ok());
        checks += result.value().avoidanceChecks;
        minimum    = std::min(minimum, separation(crowd, a, b));
        maxLateral = std::max(maxLateral, std::abs(crowd.getAgentState(a).y));
    }
    CHECK(minimum >= 0.99f);
    CHECK(maxLateral > 0.3f);
    CHECK(crowd.getAgentState(a).x > 7.8f);
    CHECK(crowd.getAgentState(b).x < -7.8f);
    CHECK(checks > 0);
}

TEST_CASE("crowd.avoidance.perpendicularCrossingArrives") {
    Crowd crowd;
    configure(crowd);
    const int a       = mover(crowd, "a", -8.f, 0.f, 8.f, 0.f);
    const int b       = mover(crowd, "b", 0.f, -8.f, 0.f, 8.f);
    float     minimum = 100.f;
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        minimum = std::min(minimum, separation(crowd, a, b));
    }
    CHECK(minimum >= 0.99f);
    CHECK(crowd.getAgentState(a).x > 7.8f);
    CHECK(crowd.getAgentState(b).y > 7.8f);
}

TEST_CASE("crowd.avoidance.routesAroundHeldUnit") {
    Crowd crowd;
    configure(crowd);
    const int a = mover(crowd, "moving", -8.f, 0.f, 8.f, 0.f);
    const int b = crowd.addNamedAgent("held", 0.f, 0.f, 0.f, 0.5f);
    REQUIRE(crowd.setAgentInteraction(b, {0.f, true, 1, 1}).ok());
    float minimum = 100.f;
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        minimum = std::min(minimum, separation(crowd, a, b));
    }
    CHECK(minimum >= 0.99f);
    CHECK(crowd.getAgentState(a).x > 7.8f);
    CHECK_EQ(crowd.getAgentState(b).x, 0.f);
    CHECK_EQ(crowd.getAgentState(b).y, 0.f);
}

TEST_CASE("crowd.avoidance.insertionOrderAndAccelerationContract") {
    Crowd forward, reverse;
    configure(forward);
    configure(reverse);
    const int fa = mover(forward, "a", -8.f, 0.f, 8.f, 0.f);
    mover(forward, "b", 8.f, 0.f, -8.f, 0.f);
    mover(reverse, "b", 8.f, 0.f, -8.f, 0.f);
    const int ra = mover(reverse, "a", -8.f, 0.f, 8.f, 0.f);
    for (int frame = 0; frame < 300; ++frame) {
        const auto before = forward.getAgentState(fa);
        REQUIRE(forward.advance(1.f / 60.f).ok());
        REQUIRE(reverse.advance(1.f / 60.f).ok());
        const auto a = forward.getAgentState(fa), b = reverse.getAgentState(ra);
        CHECK(std::hypot(a.vx - before.vx, a.vy - before.vy) <= 20.f / 60.f + 1e-4f);
        CHECK(std::abs(a.x - b.x) < 1e-4f);
        CHECK(std::abs(a.y - b.y) < 1e-4f);
    }
}

TEST_CASE("crowd.avoidance.budgetAndConfigurationAreObservable") {
    Crowd crowd;
    configure(crowd);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 1}).ok());
    CHECK(!crowd.configureAvoidance({true, std::numeric_limits<float>::quiet_NaN(), 0.f, 1}).ok());
    CHECK(!crowd.configureAvoidance({true, 2.f, 0.f, 0}).ok());
    CHECK_EQ(crowd.getAvoidanceSettings().maxNeighbors, 1);
    for (int i = 0; i < 4; ++i) crowd.addAgent(float(i) * 2.f, 0.f, 0.f, 0.5f);
    auto result = crowd.advance(1.f / 60.f);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().avoidanceTruncations, 4);
    CHECK(result.value().avoidanceChecks > 0);
    REQUIRE(crowd.configureAvoidance({false, 2.f, 0.1f, 1}).ok());
    auto disabled = crowd.advance(1.f / 60.f);
    REQUIRE(disabled.ok());
    CHECK_EQ(disabled.value().avoidanceChecks, 0);
}

TEST_CASE("crowd.avoidance.eightWayCrossingArrivesWithoutContactRepair") {
    Crowd crowd;
    configure(crowd);
    std::vector<int>     agents;
    std::vector<FlowVec> goals;
    for (int i = 0; i < 8; ++i) {
        const float angle = float(i) * 6.283185307f / 8.f;
        const float x = std::cos(angle) * 8.f, y = std::sin(angle) * 8.f;
        agents.push_back(mover(crowd, std::to_string(i), x, y, -x, -y));
        goals.push_back({-x, -y});
    }
    float minimum = 100.f;
    for (int frame = 0; frame < 900; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        for (size_t i = 0; i < agents.size(); ++i)
            for (size_t j = i + 1; j < agents.size(); ++j)
                minimum = std::min(minimum, separation(crowd, agents[i], agents[j]));
    }
    CHECK(minimum >= 0.99f);
    for (size_t i = 0; i < agents.size(); ++i) {
        const auto position = crowd.getAgentState(agents[i]);
        CHECK(std::hypot(position.x - goals[i].x, position.y - goals[i].y) < 0.2f);
    }
}

TEST_CASE("crowd.avoidance.fasterUnitOvertakes") {
    Crowd crowd;
    configure(crowd);
    const int slow = mover(crowd, "slow", -3.f, 0.f, 15.f, 0.f);
    const int fast = mover(crowd, "fast", -8.f, 0.f, 20.f, 0.f);
    REQUIRE(crowd.setAgentSpeed(slow, 1.f));
    float minimum = 100.f;
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        minimum = std::min(minimum, separation(crowd, slow, fast));
    }
    CHECK(minimum >= 0.99f);
    CHECK(crowd.getAgentState(fast).x > 19.8f);
    CHECK(crowd.getAgentState(slow).x > 5.f);
}

TEST_CASE("crowd.avoidance.ignoredLayersDoNotSteer") {
    Crowd crowd;
    configure(crowd);
    const int a = mover(crowd, "a", -8.f, 0.f, 8.f, 0.f);
    const int b = mover(crowd, "b", 8.f, 0.f, -8.f, 0.f);
    REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 1}).ok());
    REQUIRE(crowd.setAgentInteraction(b, {1.f, false, 2, 2}).ok());
    for (int frame = 0; frame < 300; ++frame) {
        auto result = crowd.advance(1.f / 60.f);
        REQUIRE(result.ok());
        CHECK_EQ(result.value().avoidanceChecks, 0);
        CHECK_EQ(crowd.getAgentState(a).y, 0.f);
    }
    CHECK(crowd.getAgentState(a).x > 7.f);
}
