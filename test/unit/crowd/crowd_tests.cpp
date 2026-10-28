// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "crowd.cpp",
    "crowd_avoidance.cpp",
    "crowd_contacts.cpp",
    "crowd_interaction.cpp",
    "crowd_spawn.cpp"
  ]
}
*/
// clang-format on

#include "PrivateTestScope.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd)
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
// crowd.cpp
namespace test_crowd {
using namespace eve::crowd;

namespace {

constexpr float kPi = 3.14159265358979323846f;

float dist(float ax, float ay, float bx, float by) {
    const float dx = bx - ax;
    const float dy = by - ay;
    return std::sqrt(dx * dx + dy * dy);
}

bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

}  // namespace

TEST_CASE("crowd.field.spec.costReachabilityAndSampling") {
    SUB_CASE("open field reaches goal with monotonically lower costs and zero flow at goal") {
        CrowdField field;
        field.resize(10, 10, 16.f, 0.f, 0.f);
        field.setGoal(9, 9);
        field.build();
        REQUIRE(field.isBuilt());
        REQUIRE(field.isReachable(0, 0));
        REQUIRE(field.costAtCell(9, 9) == 0.f);
        REQUIRE(field.costAtCell(0, 0) > field.costAtCell(5, 5));
        REQUIRE(field.costAtCell(5, 5) > field.costAtCell(8, 8));
        float x = 0.f, y = 0.f;
        field.flowAtCell(0, 0, x, y);
        REQUIRE(std::hypot(x, y) > .9f);
        field.flowAtCell(9, 9, x, y);
        REQUIRE(x == 0.f);
        REQUIRE(y == 0.f);
    };
    SUB_CASE("full-height wall makes far side unreachable and yields zero flow") {
        CrowdField field;
        field.resize(7, 7, 16.f, 0.f, 0.f);
        for (int y = 0; y < 7; ++y) field.setBlocked(3, y, true);
        field.setGoal(2, 3);
        field.build();
        REQUIRE(field.isReachable(0, 3));
        REQUIRE(!field.isReachable(4, 3));
        float x = 1.f, y = 1.f;
        field.flowAtCell(4, 3, x, y);
        REQUIRE(x == 0.f);
        REQUIRE(y == 0.f);
        REQUIRE(field.costAtWorld(4.f * 16.f, 3.f * 16.f) >= CrowdField::kUnreachable);
    };
    SUB_CASE("high-cost strip bends the route and world samples change smoothly") {
        CrowdField field;
        field.resize(5, 5, 16.f, 0.f, 0.f);
        for (int y = 0; y < 4; ++y) field.setCellCost(2, y, 10.f);
        field.setGoal(4, 4);
        field.build();
        REQUIRE(field.isReachable(0, 0));
        REQUIRE(field.costAtCell(0, 0) < 10.f);
        float x = 0.f, y = 0.f;
        field.flowAtCell(0, 0, x, y);
        REQUIRE(std::fabs(y) > .2f);
        CrowdField smooth;
        smooth.resize(16, 16, 10.f, 0.f, 0.f);
        smooth.setGoal(15, 15);
        smooth.build();
        float previousX = 0.f, previousY = 0.f;
        smooth.flowAtWorld(10.f, 10.f, previousX, previousY);
        for (int sample = 1; sample <= 20; ++sample) {
            smooth.flowAtWorld(10.f + sample * 4.f, 10.f + sample * 4.f, x, y);
            REQUIRE(x * previousX + y * previousY > .5f);
            previousX = x;
            previousY = y;
        }
    };
    SUB_CASE("blocked-cell penetration moves a circle out while free point is unchanged") {
        CrowdField field;
        field.resize(10, 10, 16.f, 0.f, 0.f);
        field.setBlocked(5, 5, true);
        float x = 88.f, y = 80.f;
        REQUIRE(field.resolvePenetration(x, y, 4.f));
        REQUIRE(!field.isBlocked(int(std::floor(x / 16.f)), int(std::floor(y / 16.f))));
        float freeX = 30.f, freeY = 30.f;
        REQUIRE(!field.resolvePenetration(freeX, freeY, 4.f));
        REQUIRE(freeX == 30.f);
        REQUIRE(freeY == 30.f);
    };
}

TEST_CASE("crowd.sim.spec.arrivalIdentityAndSteering") {
    SUB_CASE("seek stops near its target after movement") {
        Crowd crowd;
        crowd.resizeField(10, 10, 16.f, 0.f, 0.f);
        crowd.buildFlowField(9, 9);
        crowd.setArriveRadius(20.f);
        const int agent = crowd.addAgent(0.f, 0.f, 0.f, 4.f);
        REQUIRE(crowd.setAgentAction(agent, "seek"));
        crowd.setAgentTarget(agent, 100.f, 0.f);
        crowd.setAgentSpeed(agent, 120.f);
        for (int frame = 0; frame < 240; ++frame) crowd.step(1.f / 60.f);
        const auto state = crowd.getAgentState(agent);
        REQUIRE(dist(state.x, state.y, 100.f, 0.f) < 8.f);
        REQUIRE(state.speed < 5.f);
    };
    SUB_CASE("turn rate bounds one frame and later converges toward target heading") {
        Crowd crowd;
        crowd.resizeField(20, 20, 16.f, 0.f, 0.f);
        crowd.buildFlowField(19, 19);
        const int agent = crowd.addAgent(10.f, 10.f, 0.f, 4.f);
        REQUIRE(crowd.setAgentAction(agent, "seek"));
        crowd.setAgentTarget(agent, 10.f, -200.f);
        crowd.setAgentTurnRate(agent, 1.f);
        crowd.setAgentSpeed(agent, 80.f);
        crowd.step(.1f);
        REQUIRE(std::fabs(crowd.getAgentState(agent).heading) <= .101f);
        for (int frame = 0; frame < 300; ++frame) crowd.step(.1f);
        REQUIRE(std::fabs(crowd.getAgentState(agent).heading + kPi * .5f) < .15f);
    };
    SUB_CASE("stable name survives compact storage and duplicate name fails") {
        Crowd crowd;
        crowd.resizeField(8, 8, 16.f, 0.f, 0.f);
        crowd.buildFlowField(7, 7);
        const int anonymous = crowd.addAgent(0.f, 0.f, 0.f, 4.f);
        const int alpha     = crowd.addNamedAgent("unit.alpha", 16.f, 0.f, 0.f, 4.f);
        const int bravo     = crowd.addNamedAgent("unit.bravo", 32.f, 0.f, 0.f, 4.f);
        REQUIRE(anonymous >= 0);
        REQUIRE(alpha >= 0);
        REQUIRE(bravo >= 0);
        REQUIRE(crowd.addNamedAgent("unit.alpha", 0.f, 0.f, 0.f, 4.f) == -1);
        REQUIRE(crowd.removeAgent(alpha));
        REQUIRE(!crowd.hasNamedAgent("unit.alpha"));
        REQUIRE(crowd.getNamedAgentIndex("unit.bravo") == alpha);
        REQUIRE(crowd.getAgentStableId(alpha) == "unit.bravo");
        REQUIRE(crowd.getAgentStableId(anonymous).empty());
        REQUIRE(crowd.removeNamedAgent("unit.bravo"));
        REQUIRE(crowd.getNamedAgentIndex("unit.bravo") == -1);
    };
    SUB_CASE("exact overlap moves lower-priority unit independently of insertion order") {
        Crowd crowd;
        crowd.setClampToField(false);
        crowd.setResolveOverlaps(true);
        crowd.setSeparationRadius(4.f);
        crowd.setSeparationWeight(0.f);
        const auto run = [&](bool reverse) {
            crowd.clearAgents();
            REQUIRE(crowd.addNamedAgent(reverse ? "low" : "high", 0.f, 0.f, 0.f, 1.f) >= 0);
            REQUIRE(crowd.addNamedAgent(reverse ? "high" : "low", 0.f, 0.f, 0.f, 1.f) >= 0);
            const int high = crowd.getNamedAgentIndex("high");
            const int low  = crowd.getNamedAgentIndex("low");
            REQUIRE(crowd.setAgentAction(high, "idle"));
            REQUIRE(crowd.setAgentAction(low, "idle"));
            REQUIRE(crowd.setAgentAvoidancePriority(high, 10).ok());
            REQUIRE(crowd.setAgentAvoidancePriority(low, -10).ok());
            crowd.step(.1f);
            auto held  = crowd.getAgentState(high);
            auto moved = crowd.getAgentState(low);
            REQUIRE(std::hypot(held.x, held.y) < 1e-5f);
            REQUIRE(std::hypot(moved.x, moved.y) > 1.9f);
            return moved;
        };
        auto forward  = run(false);
        auto reversed = run(true);
        REQUIRE(approx(forward.x, reversed.x));
        REQUIRE(approx(forward.y, reversed.y));
    };
    SUB_CASE("boids separation increases minimum distance without a large population smoke") {
        Crowd crowd;
        crowd.resizeField(60, 60, 16.f, 0.f, 0.f);
        crowd.buildFlowField(59, 59);
        crowd.setSeparationRadius(28.f);
        crowd.setSeparationWeight(2.f);
        crowd.setAlignmentWeight(0.f);
        crowd.setCohesionWeight(0.f);
        crowd.setWanderWeight(0.f);
        crowd.setDefaultSpeed(60.f);
        std::vector<int> agents;
        for (int i = 0; i < 8; ++i) {
            const float angle = float(i) * kPi / 4.f;
            const int   id    = crowd.addAgent(480.f + std::cos(angle) * 8.f, 480.f + std::sin(angle) * 8.f, 0.f, 4.f);
            REQUIRE(id >= 0);
            REQUIRE(crowd.setAgentAction(id, "boids"));
            agents.push_back(id);
        }
        for (int frame = 0; frame < 90; ++frame) crowd.step(1.f / 60.f);
        float minimum = std::numeric_limits<float>::max();
        for (std::size_t a = 0; a < agents.size(); ++a)
            for (std::size_t b = a + 1; b < agents.size(); ++b) {
                auto first  = crowd.getAgentState(agents[a]);
                auto second = crowd.getAgentState(agents[b]);
                minimum     = std::min(minimum, dist(first.x, first.y, second.x, second.y));
            }
        REQUIRE(minimum > 10.f);
    };
}

}  // namespace test_crowd
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_avoidance)
#include "crowd/Crowd.h"
// crowd_avoidance.cpp
namespace test_crowd_avoidance {
using namespace eve::crowd;

namespace {
void configure(Crowd &crowd) {
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(false);  // Prove anticipatory steering, not contact repair.
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(1.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
}
int mover(Crowd &crowd, const std::string &name, float x, float y, float tx, float ty) {
    const int agent = crowd.addNamedAgent(name, x, y, 0.f, 0.5f);
    REQUIRE(crowd.setAgentAction(agent, "seek"));
    REQUIRE(crowd.setAgentTarget(agent, tx, ty));
    REQUIRE(crowd.setAgentAccel(agent, 20.f));
    return agent;
}
float separation(const Crowd &crowd, int a, int b) {
    const auto left = crowd.getAgentState(a), right = crowd.getAgentState(b);
    return std::hypot(left.x - right.x, left.y - right.y);
}
}  // namespace

TEST_CASE("crowd.avoidance.spec.anticipatorySteering") {
    SUB_CASE("head-on movers pass without contact repair or crossing radius") {
        Crowd crowd;
        configure(crowd);
        const int    a       = mover(crowd, "a", -8.f, 0.f, 8.f, 0.f);
        const int    b       = mover(crowd, "b", 8.f, 0.f, -8.f, 0.f);
        float        closest = 100.f, lateral = 0.f;
        std::int64_t checks = 0;
        for (int frame = 0; frame < 360; ++frame) {
            auto step = crowd.advance(1.f / 60.f);
            REQUIRE(step.ok());
            checks += step.value().avoidanceChecks;
            closest = std::min(closest, separation(crowd, a, b));
            lateral = std::max(lateral, std::fabs(crowd.getAgentState(a).y));
        }
        REQUIRE(closest >= .99f);
        REQUIRE(lateral > .3f);
        REQUIRE(crowd.getAgentState(a).x > 7.8f);
        REQUIRE(crowd.getAgentState(b).x < -7.8f);
        REQUIRE(checks > 0);
    };
    SUB_CASE("moving unit routes around a held unit without displacing the held unit") {
        Crowd crowd;
        configure(crowd);
        const int moving = mover(crowd, "moving", -8.f, 0.f, 8.f, 0.f);
        const int held   = crowd.addNamedAgent("held", 0.f, 0.f, 0.f, .5f);
        REQUIRE(crowd.setAgentInteraction(held, {0.f, true, 1, 1}).ok());
        float closest = 100.f;
        for (int frame = 0; frame < 360; ++frame) {
            REQUIRE(crowd.advance(1.f / 60.f).ok());
            closest = std::min(closest, separation(crowd, moving, held));
        }
        REQUIRE(closest >= .99f);
        REQUIRE(crowd.getAgentState(moving).x > 7.8f);
        REQUIRE(crowd.getAgentState(held).x == 0.f);
        REQUIRE(crowd.getAgentState(held).y == 0.f);
    };
    SUB_CASE("agent insertion order does not change steering or violate acceleration limit") {
        Crowd forward, reversed;
        configure(forward);
        configure(reversed);
        const int fa = mover(forward, "a", -8.f, 0.f, 8.f, 0.f);
        mover(forward, "b", 8.f, 0.f, -8.f, 0.f);
        mover(reversed, "b", 8.f, 0.f, -8.f, 0.f);
        const int ra = mover(reversed, "a", -8.f, 0.f, 8.f, 0.f);
        for (int frame = 0; frame < 120; ++frame) {
            auto before = forward.getAgentState(fa);
            REQUIRE(forward.advance(1.f / 60.f).ok());
            REQUIRE(reversed.advance(1.f / 60.f).ok());
            auto a = forward.getAgentState(fa);
            auto b = reversed.getAgentState(ra);
            REQUIRE(std::hypot(a.vx - before.vx, a.vy - before.vy) <= 20.f / 60.f + 1e-4f);
            REQUIRE(std::fabs(a.x - b.x) < 1e-4f);
            REQUIRE(std::fabs(a.y - b.y) < 1e-4f);
        }
    };
}

TEST_CASE("crowd.avoidance.spec.budgetAndLayerFiltering") {
    SUB_CASE("invalid configuration leaves max-neighbor setting intact; bounded budget reports truncation") {
        Crowd crowd;
        configure(crowd);
        REQUIRE(crowd.configureAvoidance({true, 2.f, .1f, 1}).ok());
        REQUIRE(!crowd.configureAvoidance({true, std::numeric_limits<float>::quiet_NaN(), 0.f, 1}).ok());
        REQUIRE(!crowd.configureAvoidance({true, 2.f, 0.f, 0}).ok());
        REQUIRE(crowd.getAvoidanceSettings().maxNeighbors == 1);
        for (int i = 0; i < 4; ++i) crowd.addAgent(float(i) * 2.f, 0.f, 0.f, .5f);
        auto report = crowd.advance(1.f / 60.f);
        REQUIRE(report.ok());
        REQUIRE(report.value().avoidanceTruncations == 4);
        REQUIRE(report.value().avoidanceChecks > 0);
        REQUIRE(crowd.configureAvoidance({false, 2.f, .1f, 1}).ok());
        auto disabled = crowd.advance(1.f / 60.f);
        REQUIRE(disabled.ok());
        REQUIRE(disabled.value().avoidanceChecks == 0);
    };
    SUB_CASE("non-interacting layers do not steer each other") {
        Crowd crowd;
        configure(crowd);
        const int a = mover(crowd, "a", -8.f, 0.f, 8.f, 0.f);
        const int b = mover(crowd, "b", 8.f, 0.f, -8.f, 0.f);
        REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 1}).ok());
        REQUIRE(crowd.setAgentInteraction(b, {1.f, false, 2, 2}).ok());
        for (int frame = 0; frame < 120; ++frame) {
            auto report = crowd.advance(1.f / 60.f);
            REQUIRE(report.ok());
            REQUIRE(report.value().avoidanceChecks == 0);
            REQUIRE(crowd.getAgentState(a).y == 0.f);
        }
    };
}
}  // namespace test_crowd_avoidance
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_contacts)
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
// crowd_contacts.cpp
namespace test_crowd_contacts_unit {
using namespace eve::crowd;

TEST_CASE("crowd.contacts.spec.geometryAndBounds") {
    SUB_CASE("physical contact uses agent radius even when steering query radius is smaller") {
        Crowd crowd;
        crowd.setClampToField(false);
        crowd.setSeparationWeight(0.f);
        crowd.setSeparationRadius(1.f);
        const int a = crowd.addAgent(0.f, 0.f, 0.f, 5.f);
        const int b = crowd.addAgent(8.f, 0.f, 0.f, 5.f);
        crowd.step(1.f / 60.f);
        auto left = crowd.getAgentState(a), right = crowd.getAgentState(b);
        REQUIRE(std::hypot(left.x - right.x, left.y - right.y) >= 9.999f);
    };
    SUB_CASE("field origin and agent radius clamp both axes") {
        Crowd crowd;
        crowd.resizeField(10, 10, 1.f, 20.f, -30.f);
        crowd.setSeparationWeight(0.f);
        const int id = crowd.addAgent(19.f, -31.f, 0.f, .4f);
        crowd.step(.01f);
        auto state = crowd.getAgentState(id);
        REQUIRE(state.x >= 20.4f - 1e-5f);
        REQUIRE(state.y >= -29.6f - 1e-5f);
        REQUIRE(crowd.setAgentPosition(id, 31.f, -19.f));
        crowd.step(.01f);
        state = crowd.getAgentState(id);
        REQUIRE(state.x <= 29.6f + 1e-5f);
        REQUIRE(state.y <= -20.4f + 1e-5f);
    };
    SUB_CASE("wall resolution respects independent origins and circle corner clearance") {
        CrowdField field;
        field.resize(10, 10, 1.f, 20.f, -30.f);
        field.setBlocked(5, 5, true);
        float x = 25.5f, y = -24.5f;
        REQUIRE(field.resolvePenetration(x, y, .25f));
        const bool outside = x <= 24.75f || x >= 26.25f || y <= -25.25f || y >= -23.75f;
        REQUIRE(outside);
        CrowdField corner;
        corner.resize(10, 10, 1.f, 0.f, 0.f);
        corner.setBlocked(5, 5, true);
        x = 4.6f;
        y = 4.6f;
        REQUIRE(!corner.resolvePenetration(x, y, .5f));
        REQUIRE(x == 4.6f);
        REQUIRE(y == 4.6f);
    };
}

TEST_CASE("crowd.contacts.spec.advanceSafetyAndDeterminism") {
    SUB_CASE("large dt subdivides motion and cannot tunnel through a wall") {
        Crowd crowd;
        crowd.resizeField(20, 10, 1.f, 0.f, 0.f);
        for (int y = 0; y < 10; ++y) crowd.setBlocked(10, y, true);
        crowd.setSeparationWeight(0.f);
        const int id = crowd.addAgent(5.f, 5.f, 0.f, .25f);
        REQUIRE(crowd.setAgentAction(id, "seek"));
        REQUIRE(crowd.setAgentTarget(id, 18.f, 5.f));
        REQUIRE(crowd.setAgentSpeed(id, 40.f));
        REQUIRE(crowd.setAgentAccel(id, 10000.f));
        crowd.setArriveRadius(.1f);
        auto step = crowd.advance(.5f);
        REQUIRE(step.ok());
        REQUIRE(step.value().substeps > 1);
        REQUIRE(step.value().unresolvedWalls == 0);
        REQUIRE(crowd.getAgentState(id).x <= 9.751f);
    };
    SUB_CASE("invalid dt rejects atomically; impossible packing is reported") {
        Crowd     crowd;
        const int id = crowd.addAgent(3.f, 4.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentAction(id, "seek"));
        REQUIRE(crowd.setAgentTarget(id, 10.f, 10.f));
        REQUIRE(!crowd.advance(std::numeric_limits<float>::quiet_NaN()).ok());
        REQUIRE(!crowd.advance(-1.f).ok());
        REQUIRE(!crowd.advance(1000.f).ok());
        REQUIRE(crowd.getAgentState(id).x == 3.f);
        REQUIRE(crowd.getAgentState(id).y == 4.f);
        Crowd packed;
        packed.resizeField(1, 1, 1.f, 0.f, 0.f);
        packed.setSeparationWeight(0.f);
        REQUIRE(packed.addAgent(.5f, .5f, 0.f, .5f) >= 0);
        REQUIRE(packed.addAgent(.5f, .5f, 0.f, .5f) >= 0);
        auto report = packed.advance(1.f / 60.f);
        REQUIRE(report.ok());
        REQUIRE(report.value().unresolvedContacts == 1);
        REQUIRE(report.value().maxPenetration > .99f);
    };
    SUB_CASE("one half-second advance agrees with thirty fixed substeps") {
        Crowd whole, partitioned;
        for (auto *crowd : {&whole, &partitioned}) {
            crowd->setClampToField(false);
            crowd->setDefaultSpeed(2.f);
            crowd->setSeparationWeight(0.f);
            const int id = crowd->addAgent(0.f, 0.f, 0.f, 1.f);
            REQUIRE(crowd->setAgentAction(id, "seek"));
            REQUIRE(crowd->setAgentTarget(id, 10.f, 4.f));
        }
        REQUIRE(whole.advance(.5f).ok());
        for (int i = 0; i < 30; ++i) REQUIRE(partitioned.advance(1.f / 60.f).ok());
        REQUIRE(std::fabs(whole.getAgentState(0).x - partitioned.getAgentState(0).x) < 1e-5f);
        REQUIRE(std::fabs(whole.getAgentState(0).vy - partitioned.getAgentState(0).vy) < 1e-5f);
    };
}
}  // namespace test_crowd_contacts_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_interaction)
#include "crowd/Crowd.h"
// crowd_interaction.cpp
namespace test_crowd_interaction {
using namespace eve::crowd;

TEST_CASE("crowd.interaction.spec.holdPushAndMask") {
    SUB_CASE("hold overrides seek and priority while neighbor moves; release resumes seek") {
        Crowd crowd;
        crowd.setClampToField(false);
        crowd.setSeparationWeight(0.f);
        const int held   = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
        const int moving = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentAction(held, "seek"));
        REQUIRE(crowd.setAgentTarget(held, 100.f, 0.f));
        REQUIRE(crowd.setAgentAvoidancePriority(moving, 100).ok());
        REQUIRE(crowd.setAgentInteraction(held, {1.f, true, 1, 1}).ok());
        auto report = crowd.advance(.1f);
        REQUIRE(report.ok());
        REQUIRE(report.value().unresolvedContacts == 0);
        REQUIRE(crowd.getAgentState(held).x == 0.f);
        REQUIRE(crowd.getAgentState(held).speed == 0.f);
        REQUIRE(crowd.getAgentState(moving).x >= 1.999f);
        REQUIRE(crowd.setAgentInteraction(held, {1.f, false, 1, 1}).ok());
        REQUIRE(crowd.removeAgent(moving));
        REQUIRE(crowd.advance(.1f).ok());
        REQUIRE(crowd.getAgentState(held).x > 0.f);
    };
    SUB_CASE("pushability controls displacement independently of locomotion") {
        Crowd crowd;
        crowd.setClampToField(false);
        crowd.setSeparationWeight(0.f);
        const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
        const int b = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentInteraction(a, {.25f, false, 1, 1}).ok());
        REQUIRE(crowd.setAgentInteraction(b, {.75f, false, 1, 1}).ok());
        REQUIRE(crowd.advance(.01f).ok());
        REQUIRE(std::fabs(crowd.getAgentState(a).x + .25f) < 1e-5f);
        REQUIRE(std::fabs(crowd.getAgentState(b).x - 1.75f) < 1e-5f);
        REQUIRE(crowd.setAgentInteraction(a, {0.f, false, 1, 1}).ok());
        REQUIRE(crowd.setAgentPosition(a, 0.f, 0.f));
        REQUIRE(crowd.setAgentPosition(b, 1.f, 0.f));
        REQUIRE(crowd.advance(.01f).ok());
        REQUIRE(crowd.getAgentState(a).x == 0.f);
        REQUIRE(crowd.getAgentState(b).x >= 1.999f);
    };
    SUB_CASE("interaction masks suppress contacts until both masks permit") {
        Crowd crowd;
        crowd.setClampToField(false);
        const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
        const int b = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 1}).ok());
        REQUIRE(crowd.setAgentInteraction(b, {1.f, false, 2, 3}).ok());
        REQUIRE(crowd.advance(.1f).ok());
        REQUIRE(crowd.getAgentState(a).x == 0.f);
        REQUIRE(crowd.getAgentState(b).x == 1.f);
        REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 3}).ok());
        REQUIRE(crowd.advance(.1f).ok());
        REQUIRE(crowd.getAgentState(a).x < 0.f);
        REQUIRE(crowd.getAgentState(b).x > 1.f);
    };
}

TEST_CASE("crowd.interaction.spec.validationAndCompactStorage") {
    SUB_CASE("two fixed overlapping agents report unresolved contact without moving") {
        Crowd     crowd;
        const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
        const int b = crowd.addAgent(1.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentInteraction(a, {1.f, true, 1, 1}).ok());
        REQUIRE(crowd.setAgentInteraction(b, {0.f, true, 1, 1}).ok());
        auto report = crowd.advance(.01f);
        REQUIRE(report.ok());
        REQUIRE(report.value().unresolvedContacts == 1);
        REQUIRE(crowd.getAgentState(a).x == 0.f);
        REQUIRE(crowd.getAgentState(b).x == 1.f);
    };
    SUB_CASE("invalid policy fails and a named agent retains its policy after swap-pop") {
        Crowd     crowd;
        const int a = crowd.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
        const int b = crowd.addNamedAgent("b", 5.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentInteraction(b, {.3f, true, 4, 6}).ok());
        REQUIRE(!crowd.setAgentInteraction(b, {std::numeric_limits<float>::quiet_NaN(), false, 1, 1}).ok());
        REQUIRE(!crowd.setAgentInteraction(b, {0.f, false, -1, 1}).ok());
        REQUIRE(crowd.removeAgent(a));
        auto policy = crowd.getAgentInteraction(crowd.getNamedAgentIndex("b"));
        REQUIRE(policy.ok());
        REQUIRE(policy.value().pushability == .3f);
        REQUIRE(policy.value().holdPosition);
        REQUIRE(policy.value().layer == 4);
        REQUIRE(policy.value().mask == 6);
        REQUIRE(!crowd.getAgentInteraction(b).ok());
        crowd.clearAgents();
        const int fresh    = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
        auto      defaults = crowd.getAgentInteraction(fresh);
        REQUIRE(defaults.ok());
        REQUIRE(defaults.value().pushability == 1.f);
        REQUIRE(!defaults.value().holdPosition);
    };
}
}  // namespace test_crowd_interaction
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_spawn)
#include "crowd/Crowd.h"
// crowd_spawn.cpp
namespace test_crowd_spawn_unit {
using namespace eve::crowd;

TEST_CASE("crowd.spawn.spec.pushAndNearestPlacement") {
    SUB_CASE("push policy moves connected neighbors without advancing velocity or unrelated agents") {
        Crowd crowd;
        crowd.setSeparationWeight(0.f);
        const int a   = crowd.addNamedAgent("a", 1.5f, 0.f, 0.f, 1.f);
        const int b   = crowd.addNamedAgent("b", 3.5f, 0.f, 0.f, 1.f);
        const int far = crowd.addNamedAgent("far", 100.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentAction(a, "seek"));
        REQUIRE(crowd.setAgentTarget(a, 200.f, 0.f));
        SpawnBatch batch;
        batch.agents.push_back({"spawn", 0.f, 0.f, 0.f, 1.f, {}});
        auto placed = crowd.applySpawnBatch(batch);
        REQUIRE(placed.ok());
        REQUIRE(placed.value().created.size() == 1);
        REQUIRE(placed.value().displacedAgents == 2);
        REQUIRE(crowd.getAgentState(a).x >= 1.998f);
        REQUIRE(crowd.getAgentState(b).x >= 3.997f);
        REQUIRE(crowd.getAgentState(a).vx == 0.f);
        REQUIRE(crowd.getAgentAction(a) == "seek");
        REQUIRE(crowd.getAgentState(far).x == 100.f);
    };
    SUB_CASE("nearest-free policy bounds placement distance and leaves neighbor fixed") {
        Crowd      crowd;
        const int  old = crowd.addNamedAgent("old", 0.f, 0.f, 0.f, 1.f);
        SpawnBatch batch;
        batch.policy        = SpawnPolicy::NearestFree;
        batch.maxDistance   = 3.f;
        batch.searchSpacing = .5f;
        batch.agents        = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
        auto placed         = crowd.applySpawnBatch(batch);
        REQUIRE(placed.ok());
        REQUIRE(crowd.getAgentState(old).x == 0.f);
        auto state = crowd.getAgentState(crowd.getNamedAgentIndex("new"));
        REQUIRE(std::hypot(state.x, state.y) >= 1.999f);
        REQUIRE(std::hypot(state.x, state.y) <= 3.001f);
        REQUIRE(placed.value().displacedAgents == 0);
    };
    SUB_CASE("spawn displacement preserves moving agent's velocity and seek target") {
        Crowd crowd;
        crowd.setSeparationWeight(0.f);
        const int moving = crowd.addNamedAgent("moving", 0.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentAction(moving, "seek"));
        REQUIRE(crowd.setAgentTarget(moving, 100.f, 0.f));
        REQUIRE(crowd.advance(.1f).ok());
        auto before = crowd.getAgentState(moving);
        REQUIRE(before.vx > 0.f);
        SpawnBatch batch;
        batch.agents = {{"new", before.x - 1.5f, before.y, 0.f, 1.f, {}}};
        REQUIRE(crowd.applySpawnBatch(batch).ok());
        auto after = crowd.getAgentState(moving);
        REQUIRE(after.vx == before.vx);
        REQUIRE(after.vy == before.vy);
        REQUIRE(after.speed == before.speed);
        REQUIRE(after.action == before.action);
        REQUIRE(after.x > before.x);
        REQUIRE(crowd.advance(.1f).ok());
        REQUIRE(crowd.getAgentState(moving).x > after.x);
    };
}

TEST_CASE("crowd.spawn.spec.transactionAndPolicyValidation") {
    SUB_CASE("fixed blocker rolls back every member of a two-agent batch") {
        Crowd     crowd;
        const int old   = crowd.addNamedAgent("old", 1.5f, 0.f, 0.f, 1.f);
        const int fixed = crowd.addNamedAgent("fixed", 10.f, 0.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentInteraction(fixed, {0.f, false, 1, 1}).ok());
        SpawnBatch batch;
        batch.agents = {{"first", 0.f, 0.f, 0.f, 1.f, {}}, {"second", 10.f, 0.f, 0.f, 1.f, {}}};
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        REQUIRE(crowd.getAgentCount() == 2);
        REQUIRE(crowd.getAgentState(old).x == 1.5f);
        REQUIRE(!crowd.hasNamedAgent("first"));
        REQUIRE(!crowd.hasNamedAgent("second"));
    };
    SUB_CASE("overlap rejection, displacement distance, and work budget preserve state") {
        Crowd crowd;
        crowd.addNamedAgent("old", 0.f, 0.f, 0.f, 1.f);
        SpawnBatch batch;
        batch.agents = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
        batch.policy = SpawnPolicy::RejectOverlap;
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        batch.policy      = SpawnPolicy::PushNeighbors;
        batch.maxDistance = .1f;
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        batch.maxDistance = 10.f;
        batch.maxChecks   = 1;
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        REQUIRE(crowd.getAgentCount() == 1);
        REQUIRE(crowd.getAgentState(0).x == 0.f);
        REQUIRE(!crowd.hasNamedAgent("new"));
    };
    SUB_CASE("duplicate identity, capacity, same-position batch, and wall crossing all fail atomically") {
        Crowd      crowd;
        SpawnBatch batch;
        batch.agents = {{"same", 0.f, 0.f, 0.f, 1.f, {}}, {"same", 5.f, 0.f, 0.f, 1.f, {}}};
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        batch.agents[1].stableId = "other";
        crowd.setMaxAgents(1);
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        crowd.setMaxAgents(10);
        batch.agents[1].x = 0.f;
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        REQUIRE(crowd.getAgentCount() == 0);
        crowd.resizeField(10, 5, 1.f, 0.f, 0.f);
        for (int y = 0; y < 5; ++y) crowd.setBlocked(5, y, true);
        const int old = crowd.addNamedAgent("old", 4.5f, 2.5f, 0.f, .4f);
        batch.agents  = {{"new", 4.f, 2.5f, 0.f, .4f, {}}};
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        REQUIRE(crowd.getAgentCount() == 1);
        REQUIRE(crowd.getAgentState(old).x == 4.5f);
    };
    SUB_CASE("wall rejects placement while non-interacting layer can overlap") {
        Crowd crowd;
        crowd.resizeField(10, 10, 1.f, -5.f, -5.f);
        crowd.setBlocked(5, 5, true);
        SpawnBatch batch;
        batch.agents = {{"wall", .5f, .5f, 0.f, .2f, {}}};
        REQUIRE(!crowd.applySpawnBatch(batch).ok());
        REQUIRE(crowd.getAgentCount() == 0);
        const int old = crowd.addNamedAgent("old", -2.f, -2.f, 0.f, 1.f);
        REQUIRE(crowd.setAgentInteraction(old, {0.f, true, 1, 1}).ok());
        batch.agents = {{"other", -2.f, -2.f, 0.f, 1.f, {1.f, false, 2, 2}}};
        auto result  = crowd.applySpawnBatch(batch);
        REQUIRE(result.ok());
        REQUIRE(result.value().displacedAgents == 0);
        REQUIRE(crowd.getAgentCount() == 2);
    };
}
}  // namespace test_crowd_spawn_unit
#endif

#include <stdexcept>
#include "crowd/CrowdSpatialIndex.h"
#include "crowd/CrowdWorkScheduler.h"

namespace test_crowd_advance_runtime {
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
}  // namespace test_crowd_advance_runtime

namespace test_crowd_avoidance_runtime {
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
}  // namespace test_crowd_avoidance_runtime

namespace test_crowd_budget_runtime {
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
}  // namespace test_crowd_budget_runtime

namespace test_crowd_contact_pairs_runtime {
using namespace eve;
using namespace eve::crowd;

TEST_CASE("crowd.contactPairs.namedClusterIsBitExactAcrossCreationOrderAndRestore") {
    Crowd first, second;
    for (auto* crowd : {&first, &second}) {
        crowd->setClampToField(false);
        crowd->setDefaultSpeed(0.f);
        crowd->setSeparationWeight(0.f);
    }
    for (int i = 0; i < 10; ++i) {
        REQUIRE(first.addNamedAgent("pair/" + std::to_string(i), float((i * 7) % 13) * 0.021f,
                                    float((i * 5) % 11) * 0.023f, 0.f, 1.f) >= 0);
        const int j = 9 - i;
        REQUIRE(second.addNamedAgent("pair/" + std::to_string(j), float((j * 7) % 13) * 0.021f,
                                     float((j * 5) % 11) * 0.023f, 0.f, 1.f) >= 0);
    }
    const CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    auto            saved = first.captureSnapshot();
    REQUIRE(saved.ok());
    auto a = first.advance(step), b = second.advance(step);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().contactPairsPeak > 0);
    CHECK(a.value().pairVisits > a.value().contactPairsChecked);
    for (int i = 0; i < 10; ++i) {
        const auto name  = "pair/" + std::to_string(i);
        auto       left  = first.readAgent(first.resolveNamedAgent(name).takeValue()).takeValue();
        auto       right = second.readAgent(second.resolveNamedAgent(name).takeValue()).takeValue();
        CHECK_EQ(left.x, right.x);
        CHECK_EQ(left.y, right.y);
    }
    const auto expected = first.readAgent(first.resolveNamedAgent("pair/0").takeValue()).takeValue();
    auto       restored = first.prepareRestore(saved.value());
    REQUIRE(restored.ok());
    REQUIRE(first.commitRestore(std::move(restored).takeValue()).ok());
    REQUIRE(first.advance(step).ok());
    auto replayed = first.readAgent(first.resolveNamedAgent("pair/0").takeValue()).takeValue();
    CHECK_EQ(replayed.x, expected.x);
    CHECK_EQ(replayed.y, expected.y);
}

TEST_CASE("crowd.contactPairs.capacityAndConstructionBudgetRejectAtomically") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(crowd.addAgent(0.3f, 0.f, 0.f, 1.f) >= 0);
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    step.budget.maxContactPairs = 0;
    REQUIRE(!crowd.advance(step).ok());
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK_EQ(crowd.getAgentState(1).x, 0.3f);
    step.budget.maxContactPairs = 1;
    step.budget.maxPairVisits   = 0;
    REQUIRE(!crowd.advance(step).ok());
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK_EQ(crowd.getAgentState(1).x, 0.3f);
    step.budget.maxPairVisits = 10000;
    auto accepted             = crowd.advance(step);
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value().contactPairsPeak, 1);
    CHECK(accepted.value().pairVisits > 0);
    CHECK(crowd.getAgentState(0).x < 0.f);
    // Fail one unit below the measured complete construction/gather cost, then retry the same tick.
    Crowd bounded;
    bounded.setClampToField(false);
    bounded.setDefaultSpeed(0.f);
    bounded.setSeparationWeight(0.f);
    REQUIRE(bounded.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(bounded.addAgent(0.3f, 0.f, 0.f, 1.f) >= 0);
    step.budget.maxPairVisits = accepted.value().pairVisits - 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 0.3f);
    step.budget.maxPairVisits = accepted.value().pairVisits;
    auto retry                = bounded.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().pairVisits, accepted.value().pairVisits);
    CHECK_EQ(bounded.getAgentState(0).x, crowd.getAgentState(0).x);
    CHECK_EQ(bounded.getAgentState(1).x, crowd.getAgentState(1).x);
}

TEST_CASE("crowd.contactPairs.filteredPairsDoNotUsePairBufferCapacity") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int a = crowd.addAgent(0.f, 0.f, 0.f, 1.f), b = crowd.addAgent(0.3f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(a, {1.f, false, 1, 1}).ok());
    REQUIRE(crowd.setAgentInteraction(b, {1.f, false, 2, 2}).ok());
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    step.budget.maxContactPairs = 0;
    auto result                 = crowd.advance(step);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().contactPairsPeak, 0);
    CHECK_EQ(result.value().contactPairsChecked, 0);
}
}  // namespace test_crowd_contact_pairs_runtime

namespace test_crowd_flow_cache_runtime {
using namespace eve::crowd;
namespace {
FlowLease build(Crowd& crowd, std::vector<FlowGoal> goals) {
    auto plan = crowd.prepareFlowBuild(goals).takeValue();
    while (plan.state() == Crowd::FlowBuildState::Pending) REQUIRE(crowd.advanceFlowBuild(plan, 3).ok());
    return crowd.acquireFlowField(std::move(plan)).takeValue();
}
}  // namespace

TEST_CASE("crowd.cache.fullGoalKeysReuseGenerationAndPressurePreservesPlan") {
    Crowd crowd;
    crowd.resizeField(3, 3, 1.f, 0.f, 0.f);
    REQUIRE(crowd.configureFlowCache(0).ok());
    auto plan = crowd.prepareFlowBuild(2, 1).takeValue();
    while (plan.state() == Crowd::FlowBuildState::Pending) REQUIRE(crowd.advanceFlowBuild(plan, 2).ok());
    REQUIRE(!crowd.acquireFlowField(std::move(plan)).ok());
    CHECK(plan.state() == Crowd::FlowBuildState::Ready);
    CHECK_EQ(crowd.getFlowCacheStatus().entries, 0u);
    REQUIRE(crowd.configureFlowCache(1024 * 1024).ok());
    auto                  lease = crowd.acquireFlowField(std::move(plan)).takeValue();
    std::vector<FlowGoal> key{{2, 1}, {2, 1}};
    auto                  again = crowd.acquireFlowField(key).takeValue();
    CHECK_EQ(lease.generation(), again.generation());
    CHECK_EQ(crowd.getFlowCacheStatus().entries, 1u);
    const auto bytes = crowd.getFlowCacheStatus().residentBytes;
    REQUIRE(!crowd.configureFlowCache(0).ok());
    CHECK_EQ(crowd.getFlowCacheStatus().residentBytes, bytes);
    key = {{0, 1}};
    REQUIRE(!crowd.acquireFlowField(key).ok());
    key = {{3, 1}};
    REQUIRE(!crowd.acquireFlowField(key).ok());
    CHECK(crowd.sampleFlowField(lease, 1.5f, 1.5f).value().direction.x > 0.f);
    auto outside = crowd.sampleFlowField(lease, 100.f, 100.f).takeValue();
    CHECK(outside.state == FlowSampleState::Unreachable);
    CHECK_EQ(outside.direction.x, 0.f);
    lease = {};
    again = {};
    REQUIRE(crowd.configureFlowCache(0).ok());
    CHECK_EQ(crowd.getFlowCacheStatus().entries, 0u);
    REQUIRE(crowd.configureFlowCache(1024 * 1024).ok());
    auto replacement = build(crowd, {{2, 1}});
    CHECK(replacement.generation() > 1);
}

TEST_CASE("crowd.cache.equalAccessTicksEvictByStableKeyIndependentOfCreationOrder") {
    for (bool reversed : {false, true}) {
        Crowd crowd;
        crowd.resizeField(3, 1, 1.f, 0.f, 0.f);
        auto       first = build(crowd, {{reversed ? 2 : 0, 0}});
        const auto bytes = crowd.getFlowCacheStatus().residentBytes;
        first            = {};
        auto second      = build(crowd, {{reversed ? 0 : 2, 0}});
        second           = {};
        const auto other = crowd.getFlowCacheStatus().residentBytes - bytes;
        REQUIRE(crowd.configureFlowCache(std::max(bytes, other)).ok());
        CHECK_EQ(crowd.getFlowCacheStatus().entries, 1u);
        std::vector<FlowGoal> left{{0, 0}}, right{{2, 0}};
        REQUIRE(!crowd.acquireFlowField(left).ok());
        REQUIRE(crowd.acquireFlowField(right).ok());
    }
}

TEST_CASE("crowd.cache.agentSelectionAndSnapshotRebindingKeepLatestTerrainAuthoritative") {
    Crowd crowd;
    crowd.resizeField(5, 5, 1.f, 0.f, 0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.setSeparationWeight(0.f);
    crowd.setClampToField(false);
    auto        left = build(crowd, {{0, 2}}), right = build(crowd, {{4, 2}});
    auto        a = crowd.agentRef(crowd.addNamedAgent("left", 2.5f, 1.5f, 3.14159265f, 0.1f)).takeValue();
    auto        b = crowd.agentRef(crowd.addNamedAgent("right", 2.5f, 3.5f, 0.f, 0.1f)).takeValue();
    AgentUpdate ua, ub;
    ua.reference = a;
    ua.flow      = left;
    ub.reference = b;
    ub.flow      = right;
    REQUIRE(crowd.updateAgents({{ua, ub}}).ok());
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.readAgent(a).value().vx < 0.f);
    CHECK(crowd.readAgent(b).value().vx > 0.f);
    CHECK_EQ(crowd.readAgent(a).value().flowGeneration, left.generation());
    auto saved = crowd.captureSnapshot().takeValue();
    REQUIRE(crowd.commitRestore(crowd.prepareRestore(saved).takeValue()).ok());
    REQUIRE(!crowd.sampleFlowField(left, 2.5f, 1.5f).ok());
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.readAgent(crowd.resolveNamedAgent("left").takeValue()).value().vx < 0.f);
    auto rebound = crowd.acquireFlowField(std::vector<FlowGoal>{{0, 2}}).takeValue();
    crowd.setBlocked(2, 2, true);
    REQUIRE(!crowd.sampleFlowField(rebound, 2.5f, 1.5f).ok());
    ua.reference = crowd.resolveNamedAgent("left").takeValue();
    ua.flow      = rebound;
    REQUIRE(!crowd.updateAgents({{ua}}).ok());
    CHECK_EQ(crowd.advance(0.f).value().navigationClassMismatches, 2);
}

TEST_CASE("crowd.cache.leaseOutlivesProviderButCannotBindOtherOwner") {
    FlowLease lease;
    {
        Crowd owner;
        owner.resizeField(3, 1, 1.f, 0.f, 0.f);
        lease = build(owner, {{2, 0}});
    }
    Crowd other;
    other.resizeField(3, 1, 1.f, 0.f, 0.f);
    REQUIRE(!other.sampleFlowField(lease, 1.5f, 0.5f).ok());
    AgentUpdate update;
    update.reference = other.agentRef(other.addAgent(1.5f, 0.5f, 0.f, 0.1f)).takeValue();
    update.flow      = lease;
    REQUIRE(!other.updateAgents({{update}}).ok());
    CHECK_EQ(other.readAgent(update.reference).value().flowGeneration, 0);
}

TEST_CASE("crowd.cache.unleasedSnapshotRecordsDoNotPinCurrentEviction") {
    Crowd crowd;
    crowd.resizeField(3, 1, 1.f, 0.f, 0.f);
    auto lease = build(crowd, {{2, 0}});
    lease      = {};
    auto saved = crowd.captureSnapshot().takeValue();
    REQUIRE(crowd.configureFlowCache(0).ok());
    CHECK_EQ(crowd.getFlowCacheStatus().entries, 0u);
    REQUIRE(crowd.commitRestore(crowd.prepareRestore(saved).takeValue()).ok());
    auto restored = crowd.acquireFlowField(std::vector<FlowGoal>{{2, 0}});
    REQUIRE(restored.ok());
    CHECK(restored.value().generation() != 0);
}

TEST_CASE("crowd.cache.interpolationDoesNotMakeBlockedContainingCellReachable") {
    Crowd crowd;
    crowd.resizeField(3, 3, 1.f, 0.f, 0.f);
    crowd.setBlocked(0, 1, true);
    auto lease  = build(crowd, {{2, 1}});
    auto sample = crowd.sampleFlowField(lease, 0.8f, 1.5f).takeValue();
    CHECK(sample.state == FlowSampleState::Unreachable);
    CHECK_EQ(sample.direction.x, 0.f);
    CHECK(crowd.sampleFlowField(lease, 1.1f, 1.5f).value().state == FlowSampleState::Reachable);
}

TEST_CASE("crowd.cache.moreThan64PinnedFieldsRespectByteBudget") {
    Crowd crowd;
    crowd.resizeField(9, 9, 1.f, 0.f, 0.f);
    std::vector<FlowLease> leases;
    leases.reserve(70);
    for (int goal = 0; goal < 70; ++goal) leases.push_back(build(crowd, {{goal % 9, goal / 9}}));
    REQUIRE_EQ(crowd.getFlowCacheStatus().entries, 70u);
    CHECK(crowd.getFlowCacheStatus().residentBytes <= crowd.getFlowCacheStatus().budgetBytes);
    CHECK(leases.front().generation() != leases.back().generation());
    REQUIRE(crowd.sampleFlowField(leases.back(), 6.5f, 7.5f).ok());
    const auto retained = crowd.getFlowCacheStatus().residentBytes;
    REQUIRE(!crowd.configureFlowCache(0).ok());
    CHECK_EQ(crowd.getFlowCacheStatus().entries, 70u);
    CHECK_EQ(crowd.getFlowCacheStatus().residentBytes, retained);
}
}  // namespace test_crowd_flow_cache_runtime

namespace test_crowd_navigation_runtime {
using namespace eve::crowd;

TEST_CASE("crowd.navigation.blockedGoalCompletesAsUnreachable") {
    Crowd crowd;
    crowd.resizeField(3, 3, 1.f, 0.f, 0.f);
    crowd.setBlocked(2, 2, true);
    auto plan = crowd.prepareFlowBuild(2, 2).takeValue();
    while (plan.state() == Crowd::FlowBuildState::Pending) REQUIRE(crowd.advanceFlowBuild(plan, 1).ok());
    CHECK(plan.state() == Crowd::FlowBuildState::Unreachable);
    REQUIRE(crowd.commitFlowBuild(std::move(plan)).ok());
    CHECK_EQ(crowd.flowAtCell(0, 0).x, 0.f);
    CHECK_EQ(crowd.flowAtCell(0, 0).y, 0.f);
}

TEST_CASE("crowd.navigation.largeFiniteCostsRetainUnitDirections") {
    CrowdField field;
    field.resize(2, 1, 1.f, 0.f, 0.f);
    field.setAllCellCost(1e30f);
    field.setGoal(1, 0);
    field.build();
    REQUIRE(field.isReachable(0, 0));
    float dx = 0.f, dy = 0.f;
    field.flowAtCell(0, 0, dx, dy);
    CHECK_EQ(dx, 1.f);
    CHECK_EQ(dy, 0.f);
}

TEST_CASE("crowd.navigation.workPartitionsMatchSynchronousKernel") {
    Crowd synchronous, sliced;
    for (auto* crowd : {&synchronous, &sliced}) {
        crowd->resizeField(5, 5, 1.f, 0.f, 0.f);
        for (int y = 0; y < 5; ++y)
            if (y != 2) crowd->setBlocked(2, y, true);
    }
    synchronous.buildFlowField(4, 4);
    auto plan = sliced.prepareFlowBuild(4, 4).takeValue();
    REQUIRE(!sliced.advanceFlowBuild(plan, -1).ok());
    int calls = 0, pops = 0, cells = 0;
    while (plan.state() == Crowd::FlowBuildState::Pending) {
        REQUIRE(++calls < 1000);
        auto progress = sliced.advanceFlowBuild(plan, 3);
        REQUIRE(progress.ok());
        CHECK(progress.value().heapPops + progress.value().directionCells <= 3);
        pops += progress.value().heapPops;
        cells += progress.value().directionCells;
    }
    CHECK(pops > 0);
    CHECK_EQ(cells, 25);
    REQUIRE(sliced.commitFlowBuild(std::move(plan)).ok());
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            const auto expected = synchronous.flowAtCell(x, y), actual = sliced.flowAtCell(x, y);
            CHECK_EQ(actual.x, expected.x);
            CHECK_EQ(actual.y, expected.y);
        }
}

TEST_CASE("crowd.navigation.preparedFieldPublishesOnceAndRejectsForeignOrStalePlans") {
    Crowd crowd, other;
    crowd.resizeField(8, 8, 1.f, 0.f, 0.f);
    auto prepared = crowd.prepareFlowBuild(7, 0);
    REQUIRE(prepared.ok());
    CHECK(crowd.getFieldStatus().state == FieldState::Unbuilt);
    auto plan = std::move(prepared).takeValue();
    REQUIRE(crowd.advanceFlowBuild(plan, 0).ok());
    CHECK(plan.state() == Crowd::FlowBuildState::Pending);
    REQUIRE(!crowd.commitFlowBuild(std::move(plan)).ok());
    while (plan.state() == Crowd::FlowBuildState::Pending) {
        auto progress = crowd.advanceFlowBuild(plan, 1);
        REQUIRE(progress.ok());
        CHECK(progress.value().heapPops + progress.value().directionCells <= 1);
        CHECK(crowd.getFieldStatus().state == FieldState::Unbuilt);
    }
    REQUIRE(!other.commitFlowBuild(std::move(plan)).ok());
    REQUIRE(crowd.commitFlowBuild(std::move(plan)).ok());
    CHECK(crowd.getFieldStatus().state == FieldState::Ready);
    REQUIRE(!crowd.commitFlowBuild(std::move(plan)).ok());
    auto stale = crowd.prepareFlowBuild(0, 7).takeValue();
    crowd.setBlocked(4, 4, true);
    const auto revision = crowd.getFieldStatus().revision;
    REQUIRE(!crowd.commitFlowBuild(std::move(stale)).ok());
    auto cancelled = crowd.advanceFlowBuild(stale, 32);
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value().state == Crowd::FlowBuildState::Cancelled);
    CHECK_EQ(cancelled.value().heapPops, 0);
    CHECK_EQ(cancelled.value().directionCells, 0);
    CHECK_EQ(crowd.getFieldStatus().revision, revision);
    CHECK(crowd.getFieldStatus().state == FieldState::Stale);
}

TEST_CASE("crowd.navigation.cancelAndInvalidGoalNeverPublish") {
    Crowd crowd;
    crowd.resizeField(8, 8, 1.f, 0.f, 0.f);
    REQUIRE(!crowd.prepareFlowBuild(-1, 0).ok());
    auto plan = crowd.prepareFlowBuild(7, 0).takeValue();
    REQUIRE(plan.cancel().ok());
    REQUIRE(plan.cancel().ok());
    REQUIRE(!crowd.commitFlowBuild(std::move(plan)).ok());
    CHECK(crowd.getFieldStatus().state == FieldState::Unbuilt);
}

TEST_CASE("crowd.navigation.multiGoalCanonicalizationAndMixedBlockedGoals") {
    Crowd a, b;
    for (auto* c : {&a, &b}) {
        c->resizeField(5, 3, 1.f, 0.f, 0.f);
        c->setBlocked(0, 0, true);
    }
    std::vector<FlowGoal> goals{{4, 1}, {0, 0}, {0, 1}, {4, 1}};
    auto                  first = a.prepareFlowBuild(goals).takeValue();
    goals                       = {{0, 1}, {4, 1}, {0, 0}};
    auto second                 = b.prepareFlowBuild(goals).takeValue();
    CHECK_EQ(a.getFieldStatus().goals, 0);
    while (first.state() == Crowd::FlowBuildState::Pending) REQUIRE(a.advanceFlowBuild(first, 1).ok());
    while (second.state() == Crowd::FlowBuildState::Pending) REQUIRE(b.advanceFlowBuild(second, 100).ok());
    CHECK(first.state() == Crowd::FlowBuildState::Ready);
    REQUIRE(a.commitFlowBuild(std::move(first)).ok());
    REQUIRE(b.commitFlowBuild(std::move(second)).ok());
    CHECK_EQ(a.getFieldStatus().goals, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) {
            CHECK_EQ(a.flowAtCell(x, y).x, b.flowAtCell(x, y).x);
            CHECK_EQ(a.flowAtCell(x, y).y, b.flowAtCell(x, y).y);
        }
    CHECK(a.flowAtCell(1, 1).x < 0.f);
    CHECK(a.flowAtCell(3, 1).x > 0.f);
    const auto revision = a.getFieldStatus().revision;
    REQUIRE(!a.prepareFlowBuild(std::span<const FlowGoal>{}).ok());
    goals = {{1, 1}, {5, 1}};
    REQUIRE(!a.prepareFlowBuild(goals).ok());
    CHECK_EQ(a.getFieldStatus().revision, revision);
    goals        = {{0, 0}};
    auto blocked = a.prepareFlowBuild(goals).takeValue();
    while (blocked.state() == Crowd::FlowBuildState::Pending) REQUIRE(a.advanceFlowBuild(blocked, 3).ok());
    CHECK(blocked.state() == Crowd::FlowBuildState::Unreachable);
}

TEST_CASE("crowd.navigation.independentGoalsDoNotInvalidateEachOthersInputEpoch") {
    Crowd crowd;
    crowd.resizeField(5, 5, 1.f, 0.f, 0.f);
    const auto input = crowd.getFieldStatus().inputRevision;
    auto       left  = crowd.prepareFlowBuild(0, 2).takeValue();
    auto       right = crowd.prepareFlowBuild(4, 2).takeValue();
    while (left.state() == Crowd::FlowBuildState::Pending) REQUIRE(crowd.advanceFlowBuild(left, 3).ok());
    REQUIRE(crowd.commitFlowBuild(std::move(left)).ok());
    const auto first = crowd.getFieldStatus().revision;
    CHECK_EQ(crowd.getFieldStatus().inputRevision, input);
    crowd.addFlowGoal(2, 4);
    CHECK_EQ(crowd.getFieldStatus().inputRevision, input);
    while (right.state() == Crowd::FlowBuildState::Pending) REQUIRE(crowd.advanceFlowBuild(right, 1).ok());
    REQUIRE(crowd.commitFlowBuild(std::move(right)).ok());
    CHECK(crowd.getFieldStatus().revision > first);
    CHECK_EQ(crowd.getFieldStatus().builtRevision, crowd.getFieldStatus().revision);
    CHECK_EQ(crowd.getFieldStatus().inputRevision, input);
    CHECK(crowd.flowAtCell(2, 2).x > 0.f);
    auto stale = crowd.prepareFlowBuild(0, 2).takeValue();
    crowd.setCellCost(1, 1, 1.f);
    CHECK_EQ(crowd.getFieldStatus().inputRevision, input);
    crowd.setCellCost(1, 1, 2.f);
    CHECK(crowd.getFieldStatus().inputRevision > input);
    auto cancelled = crowd.advanceFlowBuild(stale, 1);
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value().state == Crowd::FlowBuildState::Cancelled);
}
}  // namespace test_crowd_navigation_runtime

namespace test_crowd_policy_runtime {
using namespace eve::crowd;

TEST_CASE("crowd.policy.noPredictiveAvoidanceStillResolvesBodyContact") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
    const auto a = crowd.agentRef(crowd.addAgent(-0.1f, 0.f, 0.f, 0.5f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(0.1f, 0.f, 0.f, 0.5f)).takeValue();
    REQUIRE(crowd.setInteractionPolicy(a, {1.f, false, 1, 0, 1, 1}).ok());
    REQUIRE(crowd.setInteractionPolicy(b, {1.f, false, 1, 0, 1, 1}).ok());
    auto advanced = crowd.advance(1.f / 60.f);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().avoidanceChecks, 0);
    CHECK(advanced.value().contactPairsChecked > 0);
    CHECK_EQ(advanced.value().unresolvedContacts, 0);
    CHECK(std::abs(crowd.readAgent(a).value().x - crowd.readAgent(b).value().x) >= 0.999f);
}

TEST_CASE("crowd.policy.contactFilteringDoesNotDisablePredictiveSteering") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.configureAvoidance({true, 2.f, 0.1f, 32}).ok());
    const auto a = crowd.agentRef(crowd.addAgent(-0.1f, 0.f, 0.f, 0.5f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(0.1f, 0.f, 0.f, 0.5f)).takeValue();
    REQUIRE(crowd.setInteractionPolicy(a, {1.f, false, 1, 1, 0, 0}).ok());
    REQUIRE(crowd.setInteractionPolicy(b, {1.f, false, 1, 1, 0, 0}).ok());
    auto advanced = crowd.advance(1.f / 60.f);
    REQUIRE(advanced.ok());
    CHECK(advanced.value().avoidanceChecks > 0);
    CHECK_EQ(advanced.value().contactPairsChecked, 0);
    CHECK_EQ(advanced.value().unresolvedContacts, 0);
}

TEST_CASE("crowd.policy.spawnAndRelocationRespectContactInsteadOfAvoidanceMasks") {
    Crowd crowd;
    crowd.setClampToField(false);
    const auto held = crowd.agentRef(crowd.addNamedAgent("held", 0.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE(crowd.setInteractionPolicy(held, {0.f, true, 1, 0, 1, 1}).ok());
    AgentCreateBatch batch;
    batch.policy           = SpawnPolicy::RejectOverlap;
    batch.agents           = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    batch.agents[0].policy = InteractionPolicy{1.f, false, 1, 0, 1, 1};
    REQUIRE(!crowd.createAgents(batch).ok());
    CHECK(!crowd.hasNamedAgent("new"));
    batch.agents[0].x = 4.f;
    auto born         = crowd.createAgents(batch);
    REQUIRE(born.ok());
    const auto ref = born.value().created[0].reference;
    REQUIRE(!crowd.relocateAgents({{{ref, 0.f, 0.f}}, 100}).ok());
    CHECK_EQ(crowd.readAgent(ref).value().x, 4.f);
    CHECK_EQ(crowd.readInteractionPolicy(ref).value().avoidanceMask, 0);
}

TEST_CASE("crowd.policy.legacyFacadeAndHoldUpdatesHaveExplicitSemantics") {
    Crowd      crowd;
    const int  slot = crowd.addAgent(0.f, 0.f, 0.f, 1.f);
    const auto ref  = crowd.agentRef(slot).takeValue();
    REQUIRE(crowd.setInteractionPolicy(ref, {0.25f, false, 1, 0, 2, 2}).ok());
    AgentUpdate hold;
    hold.reference    = ref;
    hold.holdPosition = true;
    REQUIRE(crowd.updateAgents({{hold}}).ok());
    auto modern = crowd.readInteractionPolicy(ref).takeValue();
    CHECK_EQ(modern.avoidanceMask, 0);
    CHECK_EQ(modern.contactLayer, 2);
    CHECK(modern.holdPosition);
    const auto compatibility = crowd.getAgentInteraction(slot).takeValue();
    CHECK_EQ(compatibility.layer, 2);
    REQUIRE(crowd.setAgentInteraction(slot, {1.f, false, 4, 4}).ok());
    modern = crowd.readInteractionPolicy(ref).takeValue();
    CHECK_EQ(modern.contactLayer, 4);
    CHECK_EQ(modern.avoidanceLayer, 4);
    auto bad          = modern;
    bad.avoidanceMask = -1;
    REQUIRE(!crowd.setInteractionPolicy(ref, bad).ok());
    bad             = modern;
    bad.pushability = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!crowd.setInteractionPolicy(ref, bad).ok());
    CHECK_EQ(crowd.readInteractionPolicy(ref).value().pushability, 1.f);
}
}  // namespace test_crowd_policy_runtime

namespace test_crowd_registry_runtime {
using namespace eve::crowd;

TEST_CASE("crowd.batch.relocationChecksWholeDestinationBatchAndPreservesIdentity") {
    Crowd crowd;
    crowd.resizeField(16, 16, 1.f, 0.f, 0.f);
    crowd.setBlocked(10, 10, true);
    const auto a = crowd.agentRef(crowd.addAgent(2.f, 2.f, 0.f, 0.5f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(4.f, 2.f, 0.f, 0.5f)).takeValue();
    REQUIRE(!crowd.relocateAgents({{{a, 6.f, 2.f}, {b, 6.f, 2.f}}, 100}).ok());
    CHECK_EQ(crowd.readAgent(a).value().x, 2.f);
    REQUIRE(!crowd.relocateAgents({{{a, 10.5f, 10.5f}}, 100}).ok());
    REQUIRE(!crowd.relocateAgents({{{a, 8.f, 2.f}}, 1}).ok());
    CHECK_EQ(crowd.readAgent(a).value().x, 2.f);
    REQUIRE(crowd.relocateAgents({{{a, 4.f, 2.f}, {b, 2.f, 2.f}}, 100}).ok());
    CHECK(crowd.isAgentAlive(a));
    CHECK(crowd.isAgentAlive(b));
    CHECK_EQ(crowd.readAgent(a).value().x, 4.f);
    CHECK_EQ(crowd.readAgent(b).value().x, 2.f);
}

TEST_CASE("crowd.batch.relocationResetsVelocityButKeepsSteering") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(0.1f);
    const auto  reference = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.5f)).takeValue();
    AgentUpdate update;
    update.reference = reference;
    update.intent    = {AgentAction::Seek, true, 20.f, 0.f};
    REQUIRE(crowd.updateAgents({{update}}).ok());
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.readAgent(reference).value().vx > 0.f);
    REQUIRE(crowd.relocateAgents({{{reference, 10.f, 0.f}}, 100}).ok());
    auto state = crowd.readAgent(reference).takeValue();
    CHECK_EQ(state.speed, 0.f);
    CHECK_EQ(state.vx, 0.f);
    CHECK_EQ(state.action, static_cast<int>(AgentAction::Seek));
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.readAgent(reference).value().x > 10.f);
}

TEST_CASE("crowd.registry.swapPopKeepsIdentityAndReuseRejectsOldGeneration") {
    Crowd      crowd;
    const int  a     = crowd.addNamedAgent("a", 1.f, 0.f, 0.f, 1.f);
    const int  b     = crowd.addNamedAgent("b", 2.f, 0.f, 0.f, 1.f);
    const auto first = crowd.agentRef(a).takeValue(), second = crowd.agentRef(b).takeValue();
    REQUIRE(crowd.removeAgent(a));
    CHECK(!crowd.isAgentAlive(first));
    CHECK(crowd.isAgentAlive(second));
    CHECK_EQ(crowd.readAgent(second).value().x, 2.f);
    CHECK_EQ(crowd.resolveNamedAgent("b").value(), second);
    const auto replacement = crowd.agentRef(crowd.addNamedAgent("replacement", 3.f, 0.f, 0.f, 1.f)).takeValue();
    CHECK_EQ(replacement.handle.index(), first.handle.index());
    CHECK(replacement.handle.generation() != first.handle.generation());
    CHECK(!crowd.readAgent(first).ok());
}

TEST_CASE("crowd.registry.clearAndWrongOwnerCannotResolve") {
    Crowd      first, second;
    const auto old   = first.agentRef(first.addAgent(1.f, 0.f, 0.f, 1.f)).takeValue();
    const auto other = second.agentRef(second.addAgent(2.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE_EQ(old.handle, other.handle);
    REQUIRE(old.owner != other.owner);
    REQUIRE(!second.readAgent(old).ok());
    first.clearAgents();
    const auto current = first.agentRef(first.addAgent(3.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE(!first.readAgent(old).ok());
    REQUIRE(first.isAgentAlive(current));
}

TEST_CASE("crowd.registry.spawnTransactionPreservesOwnerAndLiveReferences") {
    Crowd            crowd;
    const auto       old = crowd.agentRef(crowd.addNamedAgent("old", 1.5f, 0.f, 0.f, 1.f)).takeValue();
    AgentCreateBatch batch;
    batch.agents = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    auto created = crowd.createAgents(batch);
    REQUIRE(created.ok());
    const auto reference = created.value().created.front().reference;
    CHECK_EQ(reference.owner, old.owner);
    CHECK(crowd.isAgentAlive(old));
    CHECK(crowd.isAgentAlive(reference));
    CHECK(crowd.readAgent(old).value().x > 1.5f);
    REQUIRE(!crowd.createAgents(batch).ok());
    CHECK(crowd.isAgentAlive(reference));
    CHECK_EQ(crowd.resolveNamedAgent("new").value(), reference);
}

TEST_CASE("crowd.batch.updateAdmissionIsAtomicAndDuplicateReferencesReject") {
    Crowd       crowd;
    const auto  reference = crowd.agentRef(crowd.addAgent(1.f, 2.f, 0.f, 1.f)).takeValue();
    AgentUpdate update;
    update.reference        = reference;
    update.data             = 17;
    update.intent           = {AgentAction::Seek, true, 100.f, 0.f};
    AgentUpdate invalid     = update;
    invalid.reference.owner = 0;
    REQUIRE(!crowd.updateAgents({{update, invalid}}).ok());
    CHECK_EQ(crowd.readAgent(reference).value().data, 0);
    REQUIRE(!crowd.updateAgents({{update, update}}).ok());
    CHECK_EQ(crowd.readAgent(reference).value().data, 0);
    invalid          = update;
    invalid.maxSpeed = std::numeric_limits<float>::infinity();
    REQUIRE(!crowd.updateAgents({{invalid}}).ok());
    auto applied = crowd.updateAgents({{update}});
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value().applied, 1u);
    CHECK_EQ(crowd.readAgent(reference).value().data, 17);
    CHECK_EQ(crowd.readAgent(reference).value().action, static_cast<int>(AgentAction::Seek));
}

TEST_CASE("crowd.batch.removalReResolvesAfterSwapAndInvalidBatchDoesNothing") {
    Crowd      crowd;
    const auto a = crowd.agentRef(crowd.addAgent(1.f, 0.f, 0.f, 1.f)).takeValue();
    const auto b = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 1.f)).takeValue();
    const auto c = crowd.agentRef(crowd.addAgent(3.f, 0.f, 0.f, 1.f)).takeValue();
    REQUIRE(!crowd.removeAgents({{a, {}}}).ok());
    REQUIRE(!crowd.removeAgents({{a, a}}).ok());
    CHECK_EQ(crowd.getAgentCount(), 3);
    REQUIRE(crowd.removeAgents({{a, c}}).ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
    CHECK(!crowd.isAgentAlive(a));
    CHECK(!crowd.isAgentAlive(c));
    CHECK_EQ(crowd.readAgent(b).value().x, 2.f);
}

TEST_CASE("crowd.batch.selectedFrameOwnsValuesAndRejectPreservesOutput") {
    Crowd                       crowd;
    const auto                  a = crowd.agentRef(crowd.addAgent(1.f, 0.f, 0.f, 1.f)).takeValue();
    const auto                  b = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 1.f)).takeValue();
    AgentFrame                  frame;
    const std::vector<AgentRef> selected{b, a};
    REQUIRE(crowd.copyAgentStates(selected, frame).ok());
    CHECK_EQ(frame.agents[0].reference, b);
    CHECK_EQ(frame.agents[1].state.x, 1.f);
    REQUIRE(crowd.removeAgents({{a}}).ok());
    CHECK_EQ(frame.agents[1].state.x, 1.f);
    REQUIRE(!crowd.copyAgentStates(selected, frame).ok());
    CHECK_EQ(frame.agents.size(), 2u);
    CHECK_EQ(frame.agents[0].state.x, 2.f);
    REQUIRE(crowd.copyAgentFrame(frame).ok());
    CHECK_EQ(frame.agents.size(), 1u);
    CHECK_EQ(frame.agents[0].reference, b);
}

TEST_CASE("crowd.registry.updatePlanOwnsValuesAndRejectsMaskBeforeMutation") {
    Crowd            crowd;
    const auto       a = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
    const auto       b = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 0.2f)).takeValue();
    AgentUpdateBatch batch;
    AgentUpdate      first;
    first.reference = a;
    first.data      = 11;
    AgentUpdate second;
    second.reference = b;
    second.data      = 22;
    batch.agents     = {first, second};
    auto prepared    = crowd.prepareAgentUpdates(batch);
    REQUIRE(prepared.ok());
    batch.agents[0].data         = 99;
    auto               plan      = std::move(prepared).takeValue();
    const std::uint8_t invalid[] = {1, 2};
    REQUIRE(!crowd.commitAgentUpdates(std::move(plan), invalid).ok());
    CHECK_EQ(crowd.readAgent(a).takeValue().data, 0);
    const std::uint8_t selected[] = {1, 0};
    auto               committed  = crowd.commitAgentUpdates(std::move(plan), selected);
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value().applied, 1u);
    CHECK_EQ(crowd.readAgent(a).takeValue().data, 11);
    CHECK_EQ(crowd.readAgent(b).takeValue().data, 0);
    REQUIRE(!crowd.commitAgentUpdates(std::move(plan), {}).ok());
}

TEST_CASE("crowd.registry.updatePlanResolvesCompactionAndRejectsStaleUnselectedIdentity") {
    Crowd            crowd;
    const int        removed = crowd.addAgent(-2.f, 0.f, 0.f, 0.2f);
    const auto       a       = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
    const auto       b       = crowd.agentRef(crowd.addAgent(2.f, 0.f, 0.f, 0.2f)).takeValue();
    AgentUpdateBatch batch;
    AgentUpdate      first;
    first.reference = a;
    first.data      = 11;
    AgentUpdate second;
    second.reference = b;
    second.data      = 22;
    batch.agents     = {first, second};
    auto prepared    = crowd.prepareAgentUpdates(batch);
    REQUIRE(prepared.ok());
    REQUIRE(crowd.removeAgent(removed));
    REQUIRE(crowd.commitAgentUpdates(std::move(prepared).takeValue(), {}).ok());
    CHECK_EQ(crowd.readAgent(a).takeValue().data, 11);
    CHECK_EQ(crowd.readAgent(b).takeValue().data, 22);
    first.data      = 33;
    batch.agents[0] = first;
    auto stale      = crowd.prepareAgentUpdates(batch);
    REQUIRE(stale.ok());
    REQUIRE(crowd.removeAgent(crowd.getAgentCount() - 1));
    const std::uint8_t selection[] = {0, 1};
    REQUIRE(!crowd.commitAgentUpdates(std::move(stale).takeValue(), selection).ok());
    CHECK_EQ(crowd.readAgent(b).takeValue().data, 22);
}

TEST_CASE("crowd.registry.updatePlanRestoreAndProviderDestructionAreSafe") {
    Crowd::AgentUpdatePlan orphan;
    {
        Crowd       crowd;
        const auto  ref = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
        AgentUpdate update;
        update.reference = ref;
        update.data      = 4;
        AgentUpdateBatch batch;
        batch.agents.push_back(update);
        auto prepared = crowd.prepareAgentUpdates(batch);
        REQUIRE(prepared.ok());
        orphan = std::move(prepared).takeValue();
    }
    Crowd      crowd;
    const auto ref = crowd.agentRef(crowd.addAgent(0.f, 0.f, 0.f, 0.2f)).takeValue();
    REQUIRE(!crowd.commitAgentUpdates(std::move(orphan), {}).ok());
    auto saved = crowd.captureSnapshot();
    REQUIRE(saved.ok());
    AgentUpdate update;
    update.reference = ref;
    update.data      = 4;
    AgentUpdateBatch batch;
    batch.agents.push_back(update);
    auto prepared = crowd.prepareAgentUpdates(batch);
    REQUIRE(prepared.ok());
    auto restored = crowd.prepareRestore(saved.value());
    REQUIRE(restored.ok());
    REQUIRE(crowd.commitRestore(std::move(restored).takeValue()).ok());
    REQUIRE(!crowd.commitAgentUpdates(std::move(prepared).takeValue(), {}).ok());
    CHECK_EQ(crowd.getAgentState(0).data, 0);
}
}  // namespace test_crowd_registry_runtime

namespace test_crowd_revision_runtime {
using namespace eve::crowd;

TEST_CASE("crowd.revision.resizeRejectsInvalidGeometryWithoutPublishingState") {
    CrowdField field;
    field.resize(8, 8, 1.f, 0.f, 0.f);
    field.setGoal(7, 0);
    field.build();
    const auto revision = field.revision();
    field.resize(8, 8, 1.f, 0.f, 0.f);
    CHECK_EQ(field.revision(), revision);
    CHECK(field.isBuilt());
    bool rejected = false;
    try {
        field.resize(8, 8, 1.f, std::numeric_limits<float>::quiet_NaN(), 0.f);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
    rejected = false;
    try {
        field.resize(65536, 65536, 1.f, 0.f, 0.f);
    } catch (const std::length_error&) {
        rejected = true;
    }
    REQUIRE(rejected);
    CHECK_EQ(field.revision(), revision);
    CHECK_EQ(field.getWidth(), 8);
    CHECK(field.isBuilt());
    CHECK(field.isReachable(0, 0));
}

TEST_CASE("crowd.revision.terrainAndGoalsInvalidateSamplingUntilRebuild") {
    CrowdField field;
    field.resize(8, 8, 1.f, 0.f, 0.f);
    REQUIRE(field.state() == FieldState::Unbuilt);
    field.setGoal(7, 0);
    field.build();
    REQUIRE(field.state() == FieldState::Ready);
    const auto built = field.builtRevision();
    field.setGoal(7, 0);
    field.addGoal(7, 0);
    REQUIRE_EQ(field.revision(), built);
    REQUIRE(field.state() == FieldState::Ready);
    float dx = 0.f, dy = 0.f;
    field.flowAtCell(0, 0, dx, dy);
    REQUIRE(dx > 0.f);
    field.setBlocked(4, 4, true);
    REQUIRE(field.state() == FieldState::Stale);
    REQUIRE(field.revision() > built);
    field.flowAtCell(0, 0, dx, dy);
    REQUIRE_EQ(dx, 0.f);
    REQUIRE_EQ(dy, 0.f);
    REQUIRE_EQ(field.costAtWorld(0.5f, 0.5f), CrowdField::kUnreachable);
    const auto revision = field.revision();
    field.setBlocked(4, 4, true);
    REQUIRE_EQ(field.revision(), revision);
    field.build();
    REQUIRE(field.isBuilt());
    REQUIRE_EQ(field.builtRevision(), field.revision());
    field.addGoal(0, 7);
    REQUIRE(field.state() == FieldState::Stale);
    field.flowAtWorld(0.5f, 0.5f, dx, dy);
    REQUIRE_EQ(dx, 0.f);
    REQUIRE_EQ(dy, 0.f);
}

TEST_CASE("crowd.revision.staleFlowStopsDrivingButLatestWallStillConstrains") {
    Crowd crowd;
    crowd.resizeField(8, 8, 1.f, 0.f, 0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.setSeparationWeight(0.f);
    crowd.buildFlowField(7, 0);
    const int agent = crowd.addAgent(0.5f, 0.5f, 0.f, 0.1f);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.getAgentState(agent).vx > 0.f);
    crowd.setBlocked(4, 4, true);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.getAgentState(agent).vx, 0.f);
    REQUIRE(crowd.setAgentPosition(agent, 4.5f, 4.5f));
    REQUIRE(crowd.advance(0.01f).ok());
    const auto state = crowd.getAgentState(agent);
    CHECK((state.x < 4.f || state.x > 5.f || state.y < 4.f || state.y > 5.f));
}
}  // namespace test_crowd_revision_runtime

namespace test_crowd_scheduler_runtime {
using namespace eve;
using namespace eve::crowd;
namespace {
class FailedScheduler final : public CrowdWorkScheduler {
public:
    Result<void> execute(int partitions, const std::function<void(int)>& body) override {
        for (int index = 0; index < partitions; ++index) body(index);
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "Injected joined failure", "scheduler"));
    }
};
}  // namespace

TEST_CASE("crowd.scheduler.parallelWithoutProviderRejectsAndSerialRemainsAvailable") {
    Crowd crowd;
    REQUIRE(!crowd.setWorkScheduler(CrowdExecution::Parallel, {}).ok());
    REQUIRE(!crowd.setWorkScheduler(static_cast<CrowdExecution>(99), {}).ok());
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Serial, {}).ok());
    REQUIRE(crowd.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(crowd.advance(CrowdStep{SimulationTick(1), Duration::fromNanoseconds(10000000), {}}).ok());
}

TEST_CASE("crowd.scheduler.joinedFailureRollsBackAndSnapshotPreservesCurrentConfiguration") {
    Crowd crowd;
    crowd.setClampToField(false);
    REQUIRE(crowd.addAgent(0.f, 0.f, 0.f, 1.f) >= 0);
    REQUIRE(crowd.addAgent(0.3f, 0.f, 0.f, 1.f) >= 0);
    auto snapshot = crowd.captureSnapshot();
    REQUIRE(snapshot.ok());
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Parallel, std::make_shared<FailedScheduler>()).ok());
    auto restore = crowd.prepareRestore(snapshot.value());
    REQUIRE(restore.ok());
    REQUIRE(crowd.commitRestore(std::move(restore).takeValue()).ok());
    const CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    REQUIRE(!crowd.advance(step).ok());
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK_EQ(crowd.getAgentState(1).x, 0.3f);
    REQUIRE(crowd.setWorkScheduler(CrowdExecution::Serial, {}).ok());
    REQUIRE(crowd.advance(step).ok());
}

TEST_CASE("crowd.scheduler.predictionBudgetPreflightIsExactAndRetryable") {
    Crowd oracle, bounded;
    for (auto* crowd : {&oracle, &bounded}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setDefaultSpeed(4.f);
        crowd->setSeparationWeight(0.f);
        REQUIRE(crowd->configureAvoidance({true, 2.f, 0.1f, 16}).ok());
        REQUIRE(crowd->addAgent(0.f, 0.f, 0.f, 0.2f) >= 0);
        REQUIRE(crowd->addAgent(2.f, 0.f, 0.f, 0.2f) >= 0);
        REQUIRE(crowd->setAgentAction(0, "seek"));
        REQUIRE(crowd->setAgentAction(1, "seek"));
        REQUIRE(crowd->setAgentTarget(0, 10.f, 0.f));
        REQUIRE(crowd->setAgentTarget(1, -10.f, 0.f));
    }
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    auto      expected = oracle.advance(step);
    REQUIRE(expected.ok());
    CHECK_EQ(expected.value().avoidanceChecks, 90);
    step.budget.maxPredictionChecks = expected.value().avoidanceChecks - 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 2.f);
    step.budget.maxPredictionChecks += 1;
    auto retry = bounded.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().avoidanceChecks, expected.value().avoidanceChecks);
    CHECK_EQ(bounded.getAgentState(0).vx, oracle.getAgentState(0).vx);
    CHECK_EQ(bounded.getAgentState(1).vx, oracle.getAgentState(1).vx);
}

TEST_CASE("crowd.scheduler.preferredCapacityAndWorkBudgetRejectAtomically") {
    Crowd oracle, bounded;
    for (auto* crowd : {&oracle, &bounded}) {
        crowd->setClampToField(false);
        crowd->setResolveOverlaps(false);
        crowd->setDefaultSpeed(4.f);
        crowd->setSeparationWeight(1.f);
        crowd->setSeparationRadius(1.f);
        REQUIRE(crowd->addAgent(0.f, 0.f, 0.f, 0.2f) >= 0);
        REQUIRE(crowd->addAgent(0.3f, 0.f, 0.f, 0.2f) >= 0);
    }
    CrowdStep step{SimulationTick(1), Duration::fromNanoseconds(10000000), {}};
    auto      expected = oracle.advance(step);
    REQUIRE(expected.ok());
    CHECK_EQ(expected.value().preferredNeighborsPeak, 2);
    CHECK(expected.value().preferredVisits > 0);
    step.budget.maxPreferredNeighbors = 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 0.3f);
    step.budget.maxPreferredNeighbors = 2;
    step.budget.maxPreferredVisits    = expected.value().preferredVisits - 1;
    REQUIRE(!bounded.advance(step).ok());
    CHECK_EQ(bounded.getAgentState(0).x, 0.f);
    CHECK_EQ(bounded.getAgentState(1).x, 0.3f);
    step.budget.maxPreferredVisits += 1;
    auto retry = bounded.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().preferredVisits, expected.value().preferredVisits);
    CHECK_EQ(bounded.getAgentState(0).vx, oracle.getAgentState(0).vx);
    CHECK_EQ(bounded.getAgentState(1).vx, oracle.getAgentState(1).vx);
}

TEST_CASE("crowd.scheduler.preferredRetainsFloatForceBoundarySemantics") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(false);
    crowd.setDefaultSpeed(4.f);
    crowd.setSeparationWeight(0.f);
    crowd.setSeparationRadius(2.f);
    crowd.setCohesionWeight(1.f);
    crowd.setPerceptionRadius(1.f);
    REQUIRE(crowd.addAgent(-0.0000000298023223876953125f, 0.f, 0.f, 0.2f) >= 0);
    REQUIRE(crowd.addAgent(1.f, 0.f, 0.f, 0.2f) >= 0);
    // Double query distance exceeds one, but the existing float force subtraction rounds to one.
    auto result = crowd.advance(CrowdStep{SimulationTick(1), Duration::fromNanoseconds(10000000), {}});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().preferredNeighborsPeak, 2);
    CHECK(crowd.getAgentState(0).vx > 0.f);
    CHECK(crowd.getAgentState(1).vx < 0.f);
}
}  // namespace test_crowd_scheduler_runtime

namespace test_crowd_snapshot_runtime {
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
}  // namespace test_crowd_snapshot_runtime

namespace test_crowd_spatial_runtime {
using namespace eve::crowd;

TEST_CASE("crowd.spatial.sparseQueriesMatchBruteForceAcrossNegativeAndFarCoordinates") {
    CrowdSpatialIndex        index;
    std::vector<float>       xs, ys;
    std::vector<CrowdTieKey> ties;
    for (int i = 0; i < 80; ++i) {
        xs.push_back(float((i * 13) % 19) - 9.125f);
        ys.push_back(float((i * 7) % 17) - 8.5f);
        ties.push_back({false, std::uint64_t(100 - i)});
    }
    xs.push_back(1e9f);
    ys.push_back(-1e9f);
    ties.push_back({true, 1});
    std::int64_t comparisons = 0;
    index.rebuild(xs, ys, ties, 2.0, 100, comparisons);
    REQUIRE(index.cellCount() <= xs.size());
    REQUIRE(comparisons > 0);
    for (int i = 0; i < 40; ++i) {
        const double     x      = i == 39 ? 1e9 : double((i * 11) % 23) - 11.25;
        const double     y      = i == 39 ? -1e9 : double((i * 3) % 19) - 9.25;
        const double     radius = double(i % 5) + 0.75;
        std::vector<int> actual, expected;
        std::int64_t     visits = 0, cells = 0;
        index.visit(x, y, radius, 10000, visits, cells, [&](int slot) {
            const double dx = double(xs[slot]) - x, dy = double(ys[slot]) - y;
            if (dx * dx + dy * dy <= radius * radius) actual.push_back(slot);
        });
        for (std::size_t slot = 0; slot < xs.size(); ++slot) {
            const double dx = double(xs[slot]) - x, dy = double(ys[slot]) - y;
            if (dx * dx + dy * dy <= radius * radius) expected.push_back(static_cast<int>(slot));
        }
        std::sort(actual.begin(), actual.end());
        REQUIRE(actual == expected);
        REQUIRE(visits >= cells);
    }
    xs   = {0.1f, 0.2f, 0.3f};
    ys   = {0.f, 0.f, 0.f};
    ties = {{true, 1}, {false, 2}, {false, 1}};
    index.rebuild(xs, ys, ties, 2.0, 1, comparisons);
    std::vector<int> order;
    std::int64_t     visits = 0, cells = 0;
    index.visit(0.2, 0.0, 1.0, 100, visits, cells, [&](int slot) { order.push_back(slot); });
    REQUIRE(order == std::vector<int>({2, 1, 0}));
}

TEST_CASE("crowd.spatial.farGroupsUseOccupiedCellsAndLookupBudgetRollsBackClock") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.addAgent(-1e9f, -1e9f, 0.f, 0.5f) >= 0);
    REQUIRE(crowd.addAgent(1e9f, 1e9f, 0.f, 0.5f) >= 0);
    CrowdStep step{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000), {}};
    step.budget.maxGridCells = 2;
    auto first               = crowd.advance(step);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().gridCellsPeak, 2);
    CHECK_EQ(first.value().gridEntriesPeak, 2);
    CHECK(first.value().gridSortComparisons > 0);
    CHECK(first.value().spatialVisits > first.value().cellsVisited);
    step.tick                    = eve::SimulationTick(2);
    step.budget.maxSpatialVisits = 0;
    auto rejected                = crowd.advance(step);
    REQUIRE(!rejected.ok());
    CHECK_EQ(crowd.getAgentState(0).x, -1e9f);
    CHECK_EQ(crowd.getAgentState(1).x, 1e9f);
    step.budget.maxSpatialVisits = 1000;
    auto retry                   = crowd.advance(step);
    REQUIRE(retry.ok());
    CHECK_EQ(retry.value().tick.value(), 2);
}

TEST_CASE("crowd.spatial.cellLimitAndUnsupportedCoordinateRejectWithoutMotion") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    REQUIRE(crowd.addAgent(-1000.f, 0.f, 0.f, 0.5f) >= 0);
    REQUIRE(crowd.addAgent(1000.f, 0.f, 0.f, 0.5f) >= 0);
    CrowdStep step{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000), {}};
    step.budget.maxGridCells = 1;
    REQUIRE(!crowd.advance(step).ok());
    CHECK_EQ(crowd.getAgentState(0).x, -1000.f);
    step.budget.maxGridCells = 2;
    REQUIRE(crowd.advance(step).ok());
    Crowd unsupported;
    unsupported.setClampToField(false);
    REQUIRE(unsupported.addAgent(std::numeric_limits<float>::max(), 0.f, 0.f, 0.5f) >= 0);
    REQUIRE(!unsupported.advance(step).ok());
    CHECK_EQ(unsupported.getAgentState(0).x, std::numeric_limits<float>::max());
}
}  // namespace test_crowd_spatial_runtime

namespace test_crowd_surface_runtime {
using namespace eve::crowd;
namespace {
NavigationSurfaceSnapshot surface() {
    NavigationSurfaceSnapshot result;
    result.stamp = {NavigationSurfaceId(1), MovementClassId(1), 1, 1, 1, 0.5f};
    result.width = result.height = 5;
    result.cellSize              = 1.f;
    result.terrainCosts.assign(25, 1.f);
    result.navigationCosts.assign(25, 1.f);
    result.terrainCosts[12]    = 0.f;
    result.navigationCosts[12] = result.navigationCosts[11] = 0.f;
    return result;
}
}  // namespace

TEST_CASE("crowd.surface.atomicPublicationRejectsConflictsAndSeparatesPhysicalWalls") {
    Crowd crowd, other;
    auto  input = surface();
    auto  plan  = crowd.prepareNavigationSurface(input).takeValue();
    CHECK(crowd.getFieldStatus().surface.surface.isZero());
    REQUIRE(!other.commitNavigationSurface(std::move(plan)).ok());
    REQUIRE(crowd.commitNavigationSurface(std::move(plan)).ok());
    REQUIRE(!crowd.commitNavigationSurface(std::move(plan)).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.surface.value(), 1);
    auto       identical = crowd.prepareNavigationSurface(input).takeValue();
    const auto revision  = crowd.getFieldStatus().revision;
    REQUIRE(crowd.commitNavigationSurface(std::move(identical)).ok());
    CHECK_EQ(crowd.getFieldStatus().revision, revision);
    auto conflict            = input;
    conflict.terrainCosts[0] = conflict.navigationCosts[0] = 2.f;
    REQUIRE(!crowd.prepareNavigationSurface(conflict).ok());
    conflict                     = input;
    conflict.navigationCosts[12] = 1.f;
    REQUIRE(!crowd.prepareNavigationSurface(conflict).ok());
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int agent = crowd.addAgent(1.5f, 2.5f, 0.f, 0.1f);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.getAgentState(agent).x, 1.5f);
    CHECK_EQ(crowd.getAgentState(agent).y, 2.5f);
    crowd.buildFlowField(4, 2);
    CHECK_EQ(crowd.flowAtCell(1, 2).x, 0.f);
    CHECK_EQ(crowd.flowAtCell(1, 2).y, 0.f);
    auto newer                  = input;
    newer.stamp.surfaceRevision = 2;
    auto ready                  = crowd.prepareNavigationSurface(newer).takeValue();
    auto stale                  = crowd.prepareNavigationSurface(newer).takeValue();
    REQUIRE(crowd.commitNavigationSurface(std::move(ready)).ok());
    REQUIRE(!crowd.commitNavigationSurface(std::move(stale)).ok());
    REQUIRE(!crowd.prepareNavigationSurface(input).ok());
    auto snapshot = crowd.captureSnapshot().takeValue();
    crowd.setBlocked(0, 0, true);
    CHECK(crowd.getFieldStatus().surface.surface.isZero());
    auto restored = crowd.prepareRestore(snapshot).takeValue();
    REQUIRE(crowd.commitRestore(std::move(restored)).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.surfaceRevision, 2);
}

TEST_CASE("crowd.surface.oversizedFootprintDoesNotUseImportedNavigation") {
    Crowd crowd;
    auto  input = surface();
    auto  plan  = crowd.prepareNavigationSurface(input).takeValue();
    REQUIRE(crowd.commitNavigationSurface(std::move(plan)).ok());
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.buildFlowField(4, 0);
    const int agent    = crowd.addAgent(0.5f, 0.5f, 0.f, 1.f);
    auto      advanced = crowd.advance(0.01f);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().navigationClassMismatches, 1);
    CHECK_EQ(crowd.getAgentState(agent).vx, 0.f);
    CHECK_EQ(crowd.getAgentState(agent).vy, 0.f);
}

TEST_CASE("crowd.surface.perAgentMovementBindingsSurviveSwapAndRestore") {
    Crowd crowd;
    auto  input = surface();
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.buildFlowField(4, 0);
    const int   first  = crowd.addNamedAgent("first", 0.5f, 0.5f, 0.f, 0.1f);
    const int   second = crowd.addNamedAgent("second", 0.5f, 4.5f, 0.f, 0.1f);
    const auto  a = crowd.agentRef(first).takeValue(), b = crowd.agentRef(second).takeValue();
    AgentUpdate ua, ub;
    ua.reference = a;
    ua.movement  = MovementBinding{MovementClassId(1), 1};
    ub.reference = b;
    ub.movement  = MovementBinding{MovementClassId(2), 1};
    REQUIRE(crowd.updateAgents({{ua, ub}}).ok());
    auto advanced = crowd.advance(0.01f);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().navigationClassMismatches, 1);
    CHECK(crowd.readAgent(a).value().speed > 0.f);
    CHECK_EQ(crowd.readAgent(b).value().speed, 0.f);
    REQUIRE(crowd.removeAgents({{a}}).ok());
    CHECK(crowd.readAgent(b).value().movement == *ub.movement);
    auto saved  = crowd.captureSnapshot().takeValue();
    ub.movement = MovementBinding{MovementClassId(1), 0};
    REQUIRE(!crowd.updateAgents({{ub}}).ok());
    ub.movement = MovementBinding{};
    REQUIRE(crowd.updateAgents({{ub}}).ok());
    REQUIRE(crowd.commitRestore(crowd.prepareRestore(saved).takeValue()).ok());
    auto restored = crowd.resolveNamedAgent("second").takeValue();
    CHECK_EQ(crowd.readAgent(restored).value().movement.movement.value(), 2);
    ub.reference = restored;
    ub.movement  = MovementBinding{MovementClassId(1), 2};
    REQUIRE(crowd.updateAgents({{ub}}).ok());
    CHECK_EQ(crowd.advance(0.f).value().navigationClassMismatches, 1);
    input.stamp.movementRevision = 2;
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    crowd.buildFlowField(4, 0);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.readAgent(restored).value().speed > 0.f);
}

TEST_CASE("crowd.surface.explicitMovementRequiresImportedProviderAndAtomicCreation") {
    Crowd crowd;
    crowd.resizeField(6, 6, 1.f, 0.f, 0.f);
    crowd.setClampToField(false);
    crowd.setSeparationWeight(0.f);
    crowd.setDefaultSpeed(2.f);
    crowd.buildFlowField(5, 5);
    SpawnBatch batch;
    batch.policy = SpawnPolicy::RejectOverlap;
    SpawnRequest request;
    request.stableId = "explicit";
    request.x = request.y = 2.5f;
    request.radius        = 0.1f;
    request.movement      = {MovementClassId(5), 0};
    batch.agents.push_back(request);
    REQUIRE(!crowd.createAgents(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    batch.agents[0].movement.revision = 1;
    auto receipt                      = crowd.createAgents(batch);
    REQUIRE(receipt.ok());
    auto reference = receipt.value().created[0].reference;
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.readAgent(reference).value().speed, 0.f);
    AgentUpdate update;
    update.reference = reference;
    update.movement  = MovementBinding{};
    REQUIRE(crowd.updateAgents({{update}}).ok());
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK(crowd.readAgent(reference).value().speed > 0.f);
}
}  // namespace test_crowd_surface_runtime

namespace test_crowd_ties_runtime {
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
}  // namespace test_crowd_ties_runtime
