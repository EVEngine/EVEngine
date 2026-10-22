#include "zeroerr/assert.h"
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

TEST_CASE("crowd.avoidance.finalArrivalAllowsPushableSettledUnitToYield") {
    Crowd crowd;
    configure(crowd);
    crowd.setResolveOverlaps(true);
    const int incoming = mover(crowd, "incoming", 0.f, -1.8f, 0.f, 1.f);
    const int settled  = mover(crowd, "settled", 0.f, 0.f, 0.f, 0.f);
    float     minimum  = 100.f;
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        minimum = std::min(minimum, separation(crowd, incoming, settled));
    }
    CHECK(minimum >= 0.99f);
    CHECK(crowd.getAgentState(incoming).y > 0.98f);
    CHECK(std::hypot(crowd.getAgentState(settled).x, crowd.getAgentState(settled).y) < 0.03f);
}

TEST_CASE("crowd.avoidance.finalArrivalDoesNotPushHeldUnit") {
    Crowd crowd;
    configure(crowd);
    crowd.setResolveOverlaps(true);
    const int incoming = mover(crowd, "incoming", 0.f, -1.8f, 0.f, 1.2f);
    const int settled  = mover(crowd, "settled", 0.f, 0.f, 0.f, 0.f);
    REQUIRE(crowd.setAgentInteraction(settled, {0.f, true, 1, 1}).ok());
    float minimum = 100.f;
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        minimum = std::min(minimum, separation(crowd, incoming, settled));
    }
    CHECK(minimum >= 0.99f);
    CHECK_EQ(crowd.getAgentState(settled).x, 0.f);
    CHECK_EQ(crowd.getAgentState(settled).y, 0.f);
    CHECK(crowd.getAgentState(incoming).y > 1.18f);
}


TEST_CASE("crowd.navigation.radiusAwareFlowPassesSingleCellPortal") {
    Crowd crowd;
    crowd.resizeField(12, 9, 1.f, 0.f, 0.f);
    for (int y = 0; y < 9; ++y)
        if (y != 4) crowd.setBlocked(5, y, true);
    crowd.buildFlowField(10, 4);
    crowd.setDefaultSpeed(3.f);
    crowd.setArriveRadius(1.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
    const int agent = crowd.addNamedAgent("portal", 2.5f, 2.5f, 0.f, 0.35f);
    REQUIRE(crowd.setAgentAction(agent, "flow"));
    REQUIRE(crowd.setAgentAccel(agent, 20.f));
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        const auto state = crowd.getAgentState(agent);
        if (state.x >= 5.f && state.x <= 6.f) REQUIRE((state.y >= 4.3499f && state.y <= 4.6501f));
    }
    CHECK(crowd.getAgentState(agent).x > 9.5f);
}


TEST_CASE("crowd.avoidance.finalArrivalCanPassPushableIdleBody") {
    Crowd crowd;
    configure(crowd);
    crowd.setResolveOverlaps(true);
    const int incoming = mover(crowd, "incoming", 0.f, -1.8f, 0.f, 1.2f);
    const int settled  = crowd.addNamedAgent("idle", 0.f, 0.f, 0.f, 0.5f);
    REQUIRE(crowd.setAgentAction(settled, "idle"));
    float minimum = 100.f;
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        minimum = std::min(minimum, separation(crowd, incoming, settled));
    }
    CHECK(minimum >= 0.99f);
    CHECK(crowd.getAgentState(incoming).y > 1.18f);
}


TEST_CASE("crowd.avoidance.seekPushesIdleBodyThroughLaneWithoutPassingSideways") {
    Crowd crowd;
    configure(crowd);
    crowd.resizeField(16, 1, 1.f, 0.f, 0.f);
    crowd.setClampToField(true);
    crowd.setResolveOverlaps(true);
    const int incoming = crowd.addNamedAgent("incoming", 2.5f, 0.5f, 0.f, 0.35f);
    REQUIRE(crowd.setAgentAction(incoming, "seek"));
    REQUIRE(crowd.setAgentTarget(incoming, 12.5f, 0.5f));
    REQUIRE(crowd.setAgentAccel(incoming, 20.f));
    const int idle = crowd.addNamedAgent("idle", 8.5f, 0.5f, 0.f, 0.35f);
    REQUIRE(crowd.setAgentAction(idle, "idle"));
    float minimum = 100.f;
    for (int frame = 0; frame < 600; ++frame) {
        REQUIRE(crowd.advance(1.f / 60.f).ok());
        minimum = std::min(minimum, separation(crowd, incoming, idle));
    }
    CHECK(minimum >= 0.699f);
    CHECK(crowd.getAgentState(incoming).x > 12.48f);
    CHECK(crowd.getAgentState(idle).x > 13.f);
}


TEST_CASE("crowd.avoidance.pacedZeroSpeedSeekBodyRemainsPushable") {
    for (const float pacedSpeed : {0.f, 0.01f}) {
        Crowd crowd;
        configure(crowd);
        crowd.resizeField(16, 1, 1.f, 0.f, 0.f);
        crowd.setClampToField(true);
        crowd.setResolveOverlaps(true);
        const int incoming = crowd.addNamedAgent("incoming", 2.5f, 0.5f, 0.f, 0.35f);
        REQUIRE(crowd.setAgentAction(incoming, "seek"));
        REQUIRE(crowd.setAgentTarget(incoming, 12.5f, 0.5f));
        REQUIRE(crowd.setAgentAccel(incoming, 20.f));
        const int paced = crowd.addNamedAgent("paced", 8.5f, 0.5f, 0.f, 0.35f);
        REQUIRE(crowd.setAgentAction(paced, "seek"));
        REQUIRE(crowd.setAgentTarget(paced, 15.5f, 0.5f));
        REQUIRE(crowd.setAgentSpeed(paced, pacedSpeed));
        float minimum = 100.f;
        for (int frame = 0; frame < 600; ++frame) {
            REQUIRE(crowd.advance(1.f / 60.f).ok());
            minimum = std::min(minimum, separation(crowd, incoming, paced));
        }
        CHECK(minimum >= 0.699f);
        CHECK(crowd.getAgentState(incoming).x > 12.48f);
        CHECK(crowd.getAgentState(paced).x > 13.f);
    }
}
