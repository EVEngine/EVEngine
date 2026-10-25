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
