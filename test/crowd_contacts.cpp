#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>

using namespace eve::crowd;

TEST_CASE("crowd.contacts.largeAgentsIgnoreSteeringQueryRadius") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    crowd.setSeparationRadius(1.f);
    const int a = crowd.addAgent(0.f, 0.f, 0.f, 5.f);
    const int b = crowd.addAgent(8.f, 0.f, 0.f, 5.f);
    crowd.step(1.f / 60.f);
    const auto left  = crowd.getAgentState(a);
    const auto right = crowd.getAgentState(b);
    CHECK(std::hypot(left.x - right.x, left.y - right.y) >= 9.999f);
}

TEST_CASE("crowd.contacts.steeringReadsOneFrameSnapshot") {
    Crowd forward, reverse;
    for (auto* crowd : {&forward, &reverse}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setSeparationRadius(20.f);
        crowd->setDefaultSpeed(10.f);
    }
    const int fa = forward.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
    const int fb = forward.addNamedAgent("b", 8.f, 0.f, 0.f, 1.f);
    const int rb = reverse.addNamedAgent("b", 8.f, 0.f, 0.f, 1.f);
    const int ra = reverse.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
    forward.step(0.1f);
    reverse.step(0.1f);
    CHECK(std::abs(forward.getAgentState(fa).x - reverse.getAgentState(ra).x) < 1e-5f);
    CHECK(std::abs(forward.getAgentState(fb).vx - reverse.getAgentState(rb).vx) < 1e-5f);
}

TEST_CASE("crowd.contacts.fieldBoundsIncludeRadiusAndOrigin") {
    Crowd crowd;
    crowd.resizeField(10, 10, 1.f, 20.f, -30.f);
    crowd.setSeparationWeight(0.f);
    const int id = crowd.addAgent(19.f, -31.f, 0.f, 0.4f);
    crowd.step(0.01f);
    auto state = crowd.getAgentState(id);
    CHECK(state.x >= 20.4f - 1e-5f);
    CHECK(state.y >= -29.6f - 1e-5f);
    REQUIRE(crowd.setAgentPosition(id, 31.f, -19.f));
    crowd.step(0.01f);
    state = crowd.getAgentState(id);
    CHECK(state.x <= 29.6f + 1e-5f);
    CHECK(state.y <= -20.4f + 1e-5f);
}

TEST_CASE("crowd.contacts.wallUsesIndependentAxisOrigins") {
    CrowdField field;
    field.resize(10, 10, 1.f, 20.f, -30.f);
    field.setBlocked(5, 5, true);
    float x = 25.5f, y = -24.5f;
    CHECK(field.resolvePenetration(x, y, 0.25f));
    CHECK((x <= 24.75f || x >= 26.25f || y <= -25.25f || y >= -23.75f));
}

TEST_CASE("crowd.contacts.circularCornerClearance") {
    CrowdField field;
    field.resize(10, 10, 1.f, 0.f, 0.f);
    field.setBlocked(5, 5, true);
    float x = 4.6f, y = 4.6f;
    CHECK(!field.resolvePenetration(x, y, 0.5f));
    CHECK_EQ(x, 4.6f);
    CHECK_EQ(y, 4.6f);
}

TEST_CASE("crowd.contacts.boundedAdvanceCannotTunnelThroughWall") {
    Crowd crowd;
    crowd.resizeField(20, 10, 1.f, 0.f, 0.f);
    for (int y = 0; y < 10; ++y) crowd.setBlocked(10, y, true);
    crowd.setSeparationWeight(0.f);
    const int id = crowd.addAgent(5.f, 5.f, 0.f, 0.25f);
    REQUIRE(crowd.setAgentAction(id, "seek"));
    REQUIRE(crowd.setAgentTarget(id, 18.f, 5.f));
    REQUIRE(crowd.setAgentSpeed(id, 40.f));
    REQUIRE(crowd.setAgentAccel(id, 10000.f));
    crowd.setArriveRadius(0.1f);
    auto result = crowd.advance(0.5f);
    REQUIRE(result.ok());
    CHECK(result.value().substeps > 1);
    CHECK_EQ(result.value().unresolvedWalls, 0);
    CHECK(crowd.getAgentState(id).x <= 9.751f);
}

TEST_CASE("crowd.contacts.rejectedAdvanceDoesNotMutate") {
    Crowd     crowd;
    const int id = crowd.addAgent(3.f, 4.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentAction(id, "seek"));
    REQUIRE(crowd.setAgentTarget(id, 10.f, 10.f));
    CHECK(!crowd.advance(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(!crowd.advance(-1.f).ok());
    CHECK(!crowd.advance(1000.f).ok());
    CHECK_EQ(crowd.getAgentState(id).x, 3.f);
    CHECK_EQ(crowd.getAgentState(id).y, 4.f);
    CHECK_EQ(crowd.getAgentState(id).speed, 0.f);
}

TEST_CASE("crowd.contacts.impossiblePackingIsObservable") {
    Crowd crowd;
    crowd.resizeField(1, 1, 1.f, 0.f, 0.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.addAgent(0.5f, 0.5f, 0.f, 0.5f) >= 0);
    REQUIRE(crowd.addAgent(0.5f, 0.5f, 0.f, 0.5f) >= 0);
    auto result = crowd.advance(1.f / 60.f);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().unresolvedContacts, 1);
    CHECK(result.value().maxPenetration > 0.99f);
}

TEST_CASE("crowd.contacts.fixedSubstepsMatchPartitionedTime") {
    Crowd whole, partitioned;
    for (auto* crowd : {&whole, &partitioned}) {
        crowd->setClampToField(false);
        crowd->setDefaultSpeed(2.f);
        crowd->setSeparationWeight(0.f);
        const int id = crowd->addAgent(0.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd->setAgentAction(id, "seek"));
        REQUIRE(crowd->setAgentTarget(id, 10.f, 4.f));
    }
    REQUIRE(whole.advance(0.5f).ok());
    for (int i = 0; i < 30; ++i) REQUIRE(partitioned.advance(1.f / 60.f).ok());
    CHECK(std::abs(whole.getAgentState(0).x - partitioned.getAgentState(0).x) < 1e-5f);
    CHECK(std::abs(whole.getAgentState(0).vy - partitioned.getAgentState(0).vy) < 1e-5f);
}

TEST_CASE("crowd.contacts.scriptAdvanceUsesCanonicalResult") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        crowd.addAgent(3.0, 4.0, 0.0, 1.0);
        assert(crowd.configureAvoidance(true, 2.0, 0.1, 32).ok);
        assert(crowd.getAvoidanceSettings().enabled);
        assert(!crowd.configureAvoidance(true, -1.0, 0.1, 32).ok);
        assert(crowd.setAgentInteraction(0, 0.25, true, 1, 3).ok);
        local policy = crowd.getAgentInteraction(0);
        assert(policy.ok && policy.hasValue && policy.value.holdPosition);
        assert(policy.value.pushability == 0.25 && policy.value.mask == 3);
        assert(!crowd.setAgentInteraction(0, -1.0, false, 1, 3).ok);
        assert(!crowd.getAgentInteraction(123).ok);
        local rejected = crowd.advance(-1.0);
        assert(!rejected.ok && !rejected.hasValue);
        assert(rejected.diagnostics.len() > 0);
        local result = crowd.advance(0.016);
        assert(result.ok && result.hasValue);
        assert(result.value.substeps >= 1);
        assert(result.value.avoidanceChecks == 0);
        assert(result.value.avoidanceTruncations == 0);
        assert(result.value.unresolvedContacts == 0);
        assert(result.value.unresolvedWalls == 0);
    )"));
}
