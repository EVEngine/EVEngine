#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
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

TEST_CASE("crowd.field.openFieldFlowsToGoal") {
    CrowdField f;
    f.resize(10, 10, 16.f, 0.f, 0.f);
    f.setGoal(9, 9);
    f.build();

    REQUIRE(f.isBuilt());
    REQUIRE(f.isReachable(0, 0));
    REQUIRE(f.isReachable(9, 9));
    REQUIRE(f.costAtCell(9, 9) == 0.f);

    // 积分代价应随接近目标单调下降。
    REQUIRE(f.costAtCell(0, 0) > f.costAtCell(5, 5));
    REQUIRE(f.costAtCell(5, 5) > f.costAtCell(8, 8));

    // (0,0) 的流方向应大致指向 (9,9)。
    float fx = 0.f, fy = 0.f;
    f.flowAtCell(0, 0, fx, fy);
    const float len = std::sqrt(fx * fx + fy * fy);
    REQUIRE(len > 0.9f);
    const float dot = (fx * 9.f + fy * 9.f) / (len * std::sqrt(162.f));
    REQUIRE(dot > 0.9f);

    // 目标格无方向。
    f.flowAtCell(9, 9, fx, fy);
    REQUIRE(fx == 0.f);
    REQUIRE(fy == 0.f);
}

TEST_CASE("crowd.field.blockedCellsUnreachable") {
    CrowdField f;
    f.resize(7, 7, 16.f, 0.f, 0.f);
    // 竖墙 x=3 全高阻挡，把场地左右隔开；目标在墙左侧。
    for (int y = 0; y < 7; ++y) f.setBlocked(3, y, true);
    f.setGoal(2, 3);
    f.build();

    REQUIRE(f.isReachable(0, 3));   // 墙左侧可达
    REQUIRE(f.isReachable(2, 3));   // 目标格可达
    REQUIRE(!f.isReachable(4, 3));  // 墙右侧被隔断
    REQUIRE(!f.isReachable(6, 3));

    float fx = 0.f, fy = 0.f;
    f.flowAtCell(4, 3, fx, fy);
    REQUIRE(fx == 0.f);
    REQUIRE(fy == 0.f);

    // 世界坐标采样：墙右侧被隔断的区域（四角全不可达）应返回不可达。
    REQUIRE(f.costAtWorld(4.f * 16.f, 3.f * 16.f) >= CrowdField::kUnreachable);
}

TEST_CASE("crowd.field.costBiasRoutesAroundSwamp") {
    CrowdField f;
    f.resize(5, 5, 16.f, 0.f, 0.f);
    // 沼泽：x=2、y=0..3 代价 10；目标 (4,4) 可沿底部绕行。
    for (int y = 0; y < 4; ++y) f.setCellCost(2, y, 10.f);
    f.setGoal(4, 4);
    f.build();

    REQUIRE(f.isReachable(0, 0));
    // 直穿沼泽 ≈ 17，底部绕行 ≈ 8；积分代价应体现绕行更便宜。
    REQUIRE(f.costAtCell(0, 0) < 10.f);

    // 起点流方向应向下偏离（先向下绕开沼泽），即 |fy| 明显非零。
    float fx = 0.f, fy = 0.f;
    f.flowAtCell(0, 0, fx, fy);
    REQUIRE(std::fabs(fy) > 0.2f);
}

TEST_CASE("crowd.field.bilinearSamplingSmooth") {
    CrowdField f;
    f.resize(16, 16, 10.f, 0.f, 0.f);
    f.setGoal(15, 15);
    f.build();

    // 沿直线采样，相邻样本方向不应突变。
    float prevX = 0.f, prevY = 0.f;
    f.flowAtWorld(10.f, 10.f, prevX, prevY);
    for (int k = 1; k <= 20; ++k) {
        const float wx = 10.f + k * 4.f;
        const float wy = 10.f + k * 4.f;
        float       x = 0.f, y = 0.f;
        f.flowAtWorld(wx, wy, x, y);
        const float dot = x * prevX + y * prevY;
        REQUIRE(dot > 0.5f);  // 角度差 < 60°
        prevX = x;
        prevY = y;
    }
}

TEST_CASE("crowd.field.resolvePenetration") {
    CrowdField f;
    f.resize(10, 10, 16.f, 0.f, 0.f);
    f.setBlocked(5, 5, true);  // 单格柱

    // 圆心落在阻挡格内：应被推出到可走格，且圆不再与阻挡格重叠。
    float wx = 88.f, wy = 80.f;  // 格 (5,5) 中心
    REQUIRE(f.resolvePenetration(wx, wy, 4.f));
    const int cx = int(std::floor(wx / 16.f));
    const int cy = int(std::floor(wy / 16.f));
    REQUIRE(!f.isBlocked(cx, cy));
    const bool pushedOut = wx <= 76.f || wx >= 100.f || wy <= 76.f || wy >= 100.f;
    REQUIRE(pushedOut);

    // 不在阻挡格内的点不应被移动。
    const float ox = 30.f, oy = 30.f;
    float       px = ox, py = oy;
    REQUIRE(!f.resolvePenetration(px, py, 4.f));
    REQUIRE(px == ox);
    REQUIRE(py == oy);
}

TEST_CASE("crowd.sim.agentsFollowFlowToGoal") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(20, 20, 16.f, 0.f, 0.f);
    crowd->buildFlowField(18, 18);
    crowd->setDefaultSpeed(180.f);
    crowd->setSeparationWeight(0.f);   // 纯流场跟随；分离行为由 boids 测试覆盖
    crowd->setResolveOverlaps(false);  // 单点到达测试：允许单位重叠停靠

    std::vector<int> ids;
    ids.push_back(crowd->addAgent(16.f, 16.f, 0.f, 5.f));
    ids.push_back(crowd->addAgent(20.f, 16.f, 0.f, 5.f));
    REQUIRE_EQ(crowd->getAgentCount(), 2);

    // 目标格 (18,18) 的中心（格子世界坐标 = 中心偏移半格）。
    const float goalX = 18.5f * 16.f;
    const float goalY = 18.5f * 16.f;
    for (int step = 0; step < 420; ++step) crowd->step(1.f / 60.f);

    for (int id : ids) {
        const AgentState s = crowd->getAgentState(id);
        REQUIRE(s.action == 1);  // flow
        REQUIRE(dist(s.x, s.y, goalX, goalY) < 2.f * 16.f);
        REQUIRE(s.speed < 30.f);  // 已减速停车
    }

    crowd->clearAgents();
}

TEST_CASE("crowd.sim.agentsDoNotPenetrateWalls") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(24, 24, 16.f, 0.f, 0.f);
    // 竖墙 x=12，缺口 y=10..14；目标在墙右侧。
    for (int y = 0; y < 24; ++y) {
        if (y < 10 || y > 14) crowd->setBlocked(12, y, true);
    }
    crowd->buildFlowField(22, 12);
    crowd->setDefaultSpeed(120.f);

    for (int i = 0; i < 60; ++i) {
        crowd->addAgent(30.f + float(i % 10) * 8.f, 60.f + float(i / 10) * 8.f, 0.f, 4.f);
    }
    for (int step = 0; step < 600; ++step) crowd->step(1.f / 60.f);

    int passed = 0;
    for (int i = 0; i < crowd->getAgentCount(); ++i) {
        const AgentState s  = crowd->getAgentState(i);
        const int        cx = int(s.x / 16.f);
        const int        cy = int(s.y / 16.f);
        // 任何单位中心都不能停在阻挡格（= 不可达格）内。
        REQUIRE(crowd->isReachable(cx, cy));
        if (s.x > 12.f * 16.f + 8.f) ++passed;
    }
    // 大多数单位应穿过缺口到达墙右侧。
    REQUIRE(passed > 30);

    crowd->clearAgents();
}

TEST_CASE("crowd.sim.turnRateLimitsHeading") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(20, 20, 16.f, 0.f, 0.f);
    crowd->buildFlowField(19, 19);

    const int id = crowd->addAgent(10.f, 10.f, 0.f, 4.f);
    crowd->setAgentAction(id, "seek");
    crowd->setAgentTarget(id, 10.f, -200.f);  // 正上方：目标角 -π/2
    crowd->setAgentTurnRate(id, 1.f);         // 1 rad/s
    crowd->setAgentSpeed(id, 80.f);

    // 单帧最多转 turnRate·dt。
    const float dt = 0.1f;
    crowd->step(dt);
    const AgentState s1 = crowd->getAgentState(id);
    REQUIRE(std::fabs(s1.heading) <= 0.1f + 1e-3f);

    // 长时间应收敛到 -π/2（允许 ±0.15）。
    for (int i = 0; i < 300; ++i) crowd->step(dt);
    const AgentState s2 = crowd->getAgentState(id);
    REQUIRE(std::fabs(s2.heading + kPi * 0.5f) < 0.15f);
    REQUIRE(s2.y < 10.f);  // 确实向上移动

    crowd->clearAgents();
}

TEST_CASE("crowd.sim.exactOverlapUsesStableIdentityAndAvoidancePriority") {
    Crowd crowd;
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(true);
    crowd.setSeparationRadius(4.0f);
    crowd.setSeparationWeight(0.0f);

    auto run = [&](bool reverse) {
        crowd.clearAgents();
        const int first  = crowd.addNamedAgent(reverse ? "low" : "high", 0.0f, 0.0f, 0.0f, 1.0f);
        const int second = crowd.addNamedAgent(reverse ? "high" : "low", 0.0f, 0.0f, 0.0f, 1.0f);
        REQUIRE(first >= 0);
        REQUIRE(second >= 0);
        const int high = crowd.getNamedAgentIndex("high");
        const int low  = crowd.getNamedAgentIndex("low");
        REQUIRE(crowd.setAgentAction(high, "idle"));
        REQUIRE(crowd.setAgentAction(low, "idle"));
        REQUIRE(crowd.setAgentAvoidancePriority(high, 10).ok());
        REQUIRE(crowd.setAgentAvoidancePriority(low, -10).ok());
        crowd.step(0.1f);
        CHECK_EQ(crowd.getAgentAvoidancePriority(high), 10);
        CHECK_EQ(crowd.getAgentAvoidancePriority(low), -10);
        const AgentState highState = crowd.getAgentState(high);
        const AgentState lowState  = crowd.getAgentState(low);
        CHECK(std::hypot(highState.x, highState.y) < 1e-5f);
        CHECK(std::hypot(lowState.x, lowState.y) > 1.9f);
        return lowState;
    };

    const AgentState forward  = run(false);
    const AgentState reversed = run(true);
    CHECK(approx(forward.x, reversed.x));
    CHECK(approx(forward.y, reversed.y));
}

TEST_CASE("crowd.sim.seekArrivesAndStops") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(10, 10, 16.f, 0.f, 0.f);
    crowd->buildFlowField(9, 9);
    crowd->setArriveRadius(20.f);

    const int id = crowd->addAgent(0.f, 0.f, 0.f, 4.f);
    crowd->setAgentAction(id, "seek");
    crowd->setAgentTarget(id, 100.f, 0.f);
    crowd->setAgentSpeed(id, 120.f);

    for (int i = 0; i < 240; ++i) crowd->step(1.f / 60.f);

    const AgentState s = crowd->getAgentState(id);
    REQUIRE(dist(s.x, s.y, 100.f, 0.f) < 8.f);
    REQUIRE(s.speed < 5.f);

    crowd->clearAgents();
}

TEST_CASE("crowd.sim.boidsSeparationSpreadsCluster") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(60, 60, 16.f, 0.f, 0.f);
    crowd->buildFlowField(59, 59);
    crowd->setSeparationRadius(28.f);
    crowd->setSeparationWeight(2.f);
    crowd->setAlignmentWeight(0.f);
    crowd->setCohesionWeight(0.f);
    crowd->setWanderWeight(0.f);
    crowd->setDefaultSpeed(60.f);

    std::vector<int> ids;
    for (int i = 0; i < 24; ++i) {
        const float ang = float(i) * 0.2618f;  // 均匀散布
        // 场地中心（480,480）生成，避免一开始就被场边界钳制。
        const int id = crowd->addAgent(480.f + std::cos(ang) * 8.f, 480.f + std::sin(ang) * 8.f, 0.f, 4.f);
        crowd->setAgentAction(id, "boids");
        ids.push_back(id);
    }
    for (int i = 0; i < 90; ++i) crowd->step(1.f / 60.f);

    // 分离力应把紧密集群推开，任意两两距离应明显大于初始半径。
    float minD = std::numeric_limits<float>::max();
    for (size_t a = 0; a < ids.size(); ++a) {
        const AgentState sa = crowd->getAgentState(ids[a]);
        for (size_t b = a + 1; b < ids.size(); ++b) {
            const AgentState sb = crowd->getAgentState(ids[b]);
            minD                = std::min(minD, dist(sa.x, sa.y, sb.x, sb.y));
        }
    }
    REQUIRE(minD > 10.f);

    crowd->clearAgents();
}

TEST_CASE("crowd.sim.boidsCohesionPullsTogether") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(80, 80, 16.f, 0.f, 0.f);
    crowd->buildFlowField(79, 79);
    crowd->setSeparationWeight(0.f);
    crowd->setAlignmentWeight(0.f);
    crowd->setCohesionWeight(1.2f);
    crowd->setWanderWeight(0.f);
    crowd->setGoalWeight(0.f);
    crowd->setDefaultSpeed(60.f);
    crowd->setPerceptionRadius(600.f);  // 两单位相距 400，需要大感知半径

    const int a = crowd->addAgent(0.f, 0.f, 0.f, 4.f);
    const int b = crowd->addAgent(400.f, 0.f, 0.f, 4.f);
    crowd->setAgentAction(a, "boids");
    crowd->setAgentAction(b, "boids");

    for (int i = 0; i < 180; ++i) crowd->step(1.f / 60.f);

    const AgentState sa     = crowd->getAgentState(a);
    const AgentState sb     = crowd->getAgentState(b);
    const float      finalD = dist(sa.x, sa.y, sb.x, sb.y);
    REQUIRE(finalD < 300.f);  // 距离明显下降

    crowd->clearAgents();
}

TEST_CASE("crowd.sim.massiveAgentsSmoke") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(100, 100, 8.f, 0.f, 0.f);
    crowd->buildFlowField(99, 99);
    crowd->setDefaultSpeed(60.f);
    crowd->setSeparationRadius(16.f);
    crowd->setMaxAgents(40000);

    constexpr int kCount = 20000;
    for (int i = 0; i < kCount; ++i) {
        const float x = float((i * 2654435761u) % 800u);
        const float y = float((i * 40503u) % 800u);
        REQUIRE(crowd->addAgent(x, y, 0.f, 3.f) >= 0);
    }
    REQUIRE_EQ(crowd->getAgentCount(), kCount);

    crowd->step(1.f / 60.f);
    REQUIRE_EQ(crowd->getAgentCount(), kCount);

    // 全部单位仍在场内。
    for (int i = 0; i < 200; i += 7) {
        const AgentState s = crowd->getAgentState(i);
        REQUIRE(s.x >= -4.f);
        REQUIRE(s.x <= 804.f);
        REQUIRE(s.y >= -4.f);
        REQUIRE(s.y <= 804.f);
    }

    crowd->clearAgents();
}

TEST_CASE("crowd.sim.removeAgentSwapPop") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);

    crowd->clearAgents();
    crowd->resizeField(5, 5, 16.f, 0.f, 0.f);
    crowd->buildFlowField(4, 4);
    const int a = crowd->addAgent(0.f, 0.f, 0.f, 4.f);
    const int b = crowd->addAgent(32.f, 0.f, 0.f, 4.f);
    crowd->addAgent(64.f, 0.f, 0.f, 4.f);
    REQUIRE_EQ(crowd->getAgentCount(), 3);

    REQUIRE(crowd->removeAgent(b));
    REQUIRE_EQ(crowd->getAgentCount(), 2);
    // swap-pop：槽位 b 现在装的是原 c（删除后 id 不稳定）。
    const AgentState swapped = crowd->getAgentState(b);
    REQUIRE(swapped.x == 64.f);
    REQUIRE(swapped.y == 0.f);

    REQUIRE(crowd->removeAgent(a));
    REQUIRE_EQ(crowd->getAgentCount(), 1);
    REQUIRE(crowd->getAgentState(a).action >= 0);
    REQUIRE(crowd->getAgentState(a).x == 64.f);  // 原 c 换到槽位 a
    REQUIRE(crowd->removeAgent(a));
    REQUIRE_EQ(crowd->getAgentCount(), 0);
    REQUIRE(!crowd->removeAgent(a));  // 已失效

    crowd->clearAgents();
}

TEST_CASE("crowd.authoring.namedAgentsSurviveCompactStorage") {
    auto *crowd = Crowd::create();
    REQUIRE(crowd != nullptr);
    crowd->clearAgents();
    crowd->resizeField(8, 8, 16.f, 0.f, 0.f);
    crowd->buildFlowField(7, 7);

    const int anonymous = crowd->addAgent(0.f, 0.f, 0.f, 4.f);
    const int alpha     = crowd->addNamedAgent("unit.alpha", 16.f, 0.f, 0.f, 4.f);
    const int bravo     = crowd->addNamedAgent("unit.bravo", 32.f, 0.f, 0.f, 4.f);
    REQUIRE(anonymous >= 0);
    REQUIRE(alpha >= 0);
    REQUIRE(bravo >= 0);
    CHECK(!crowd->hasNamedAgent(""));
    CHECK_EQ(crowd->addNamedAgent("unit.alpha", 0.f, 0.f, 0.f, 4.f), -1);

    REQUIRE(crowd->removeAgent(alpha));
    CHECK(!crowd->hasNamedAgent("unit.alpha"));
    CHECK(crowd->hasNamedAgent("unit.bravo"));
    const int compactedBravo = crowd->getNamedAgentIndex("unit.bravo");
    CHECK_EQ(compactedBravo, alpha);
    CHECK_EQ(crowd->getAgentStableId(compactedBravo), std::string("unit.bravo"));
    CHECK(crowd->getAgentStableId(anonymous).empty());
    REQUIRE(crowd->removeNamedAgent("unit.bravo"));
    CHECK_EQ(crowd->getNamedAgentIndex("unit.bravo"), -1);
    crowd->clearAgents();
}
}  // namespace test_crowd
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_avoidance)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
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
}  // namespace test_crowd_avoidance
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_contacts)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"
#include "zeroerr/unittest.h"
// crowd_contacts.cpp
namespace test_crowd_contacts_unit {
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
    for (auto *crowd : {&forward, &reverse}) {
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
    for (auto *crowd : {&whole, &partitioned}) {
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
}  // namespace test_crowd_contacts_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_interaction)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
// crowd_interaction.cpp
namespace test_crowd_interaction {
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
}  // namespace test_crowd_interaction
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_crowd_spawn)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
// crowd_spawn.cpp
namespace test_crowd_spawn_unit {
using namespace eve::crowd;

TEST_CASE("crowd.spawn.pushesConnectedNeighborsWithoutAdvancingTime") {
    Crowd crowd;
    crowd.setSeparationWeight(0.f);
    const int a         = crowd.addNamedAgent("a", 1.5f, 0.f, 0.f, 1.f);
    const int b         = crowd.addNamedAgent("b", 3.5f, 0.f, 0.f, 1.f);
    const int untouched = crowd.addNamedAgent("far", 100.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentAction(a, "seek"));
    REQUIRE(crowd.setAgentTarget(a, 200.f, 0.f));
    SpawnBatch batch;
    batch.agents.push_back({"spawn", 0.f, 0.f, 0.f, 1.f, {}});
    auto result = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().created.size(), size_t(1));
    CHECK_EQ(result.value().displacedAgents, 2);
    CHECK(crowd.getAgentState(a).x >= 1.998f);
    CHECK(crowd.getAgentState(b).x >= 3.997f);
    CHECK_EQ(crowd.getAgentState(a).vx, 0.f);
    CHECK_EQ(crowd.getAgentAction(a), "seek");
    CHECK_EQ(crowd.getAgentState(untouched).x, 100.f);
    CHECK_EQ(crowd.getAgentState(crowd.getNamedAgentIndex("spawn")).x, 0.f);
}

TEST_CASE("crowd.spawn.fixedBlockerRollsBackWholeBatch") {
    Crowd     crowd;
    const int a     = crowd.addNamedAgent("a", 1.5f, 0.f, 0.f, 1.f);
    const int fixed = crowd.addNamedAgent("fixed", 10.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(fixed, {0.f, false, 1, 1}).ok());
    SpawnBatch batch;
    batch.agents = {{"first", 0.f, 0.f, 0.f, 1.f, {}}, {"second", 10.f, 0.f, 0.f, 1.f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    CHECK(!result.ok());
    CHECK_EQ(crowd.getAgentCount(), 2);
    CHECK_EQ(crowd.getAgentState(a).x, 1.5f);
    CHECK(!crowd.hasNamedAgent("first"));
    CHECK(!crowd.hasNamedAgent("second"));
}

TEST_CASE("crowd.spawn.nearestFreeIsBoundedAndLeavesNeighborsAlone") {
    Crowd      crowd;
    const int  a = crowd.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
    SpawnBatch batch;
    batch.policy        = SpawnPolicy::NearestFree;
    batch.maxDistance   = 3.f;
    batch.searchSpacing = 0.5f;
    batch.agents        = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    auto result         = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(crowd.getAgentState(a).x, 0.f);
    const auto position = crowd.getAgentState(crowd.getNamedAgentIndex("new"));
    CHECK(std::hypot(position.x, position.y) >= 1.999f);
    CHECK(std::hypot(position.x, position.y) <= 3.001f);
    CHECK_EQ(result.value().displacedAgents, 0);
}

TEST_CASE("crowd.spawn.rejectBudgetAndDistanceFailuresAreAtomic") {
    Crowd crowd;
    crowd.addNamedAgent("a", 0.f, 0.f, 0.f, 1.f);
    SpawnBatch batch;
    batch.agents = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    batch.policy = SpawnPolicy::RejectOverlap;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    batch.policy      = SpawnPolicy::PushNeighbors;
    batch.maxDistance = 0.1f;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    batch.maxDistance = 10.f;
    batch.maxChecks   = 1;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
    CHECK_EQ(crowd.getAgentState(0).x, 0.f);
    CHECK(!crowd.hasNamedAgent("new"));
}

TEST_CASE("crowd.spawn.respectsWallsAndInteractionLayers") {
    Crowd crowd;
    crowd.resizeField(10, 10, 1.f, -5.f, -5.f);
    crowd.setBlocked(5, 5, true);
    SpawnBatch batch;
    batch.agents = {{"wall", 0.5f, 0.5f, 0.f, 0.2f, {}}};
    CHECK(!crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    const int original = crowd.addNamedAgent("old", -2.f, -2.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentInteraction(original, {0.f, true, 1, 1}).ok());
    batch.agents = {{"otherLayer", -2.f, -2.f, 0.f, 1.f, {1.f, false, 2, 2}}};
    auto result  = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().displacedAgents, 0);
    CHECK_EQ(crowd.getAgentCount(), 2);
}

TEST_CASE("crowd.spawn.identityCapacityAndIntraBatchConflicts") {
    Crowd      crowd;
    SpawnBatch batch;
    batch.agents = {{"same", 0.f, 0.f, 0.f, 1.f, {}}, {"same", 5.f, 0.f, 0.f, 1.f, {}}};
    CHECK(!crowd.applySpawnBatch(batch).ok());
    batch.agents[1].stableId = "other";
    crowd.setMaxAgents(1);
    CHECK(!crowd.applySpawnBatch(batch).ok());
    crowd.setMaxAgents(10);
    batch.agents[1].x = 0.f;
    CHECK(!crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    batch.agents[1].x = 5.f;
    REQUIRE(crowd.applySpawnBatch(batch).ok());
    CHECK_EQ(crowd.getAgentCount(), 2);
    CHECK(!crowd.applySpawnBatch(batch).ok());
}

TEST_CASE("crowd.spawn.wallCannotBeCrossedToMakeRoom") {
    Crowd crowd;
    crowd.resizeField(10, 5, 1.f, 0.f, 0.f);
    for (int y = 0; y < 5; ++y) crowd.setBlocked(5, y, true);
    const int  original = crowd.addNamedAgent("old", 4.5f, 2.5f, 0.f, 0.4f);
    SpawnBatch batch;
    batch.agents = {{"new", 4.f, 2.5f, 0.f, 0.4f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    CHECK(!result.ok());
    CHECK_EQ(crowd.getAgentCount(), 1);
    CHECK_EQ(crowd.getAgentState(original).x, 4.5f);
}

TEST_CASE("crowd.spawn.unrelatedPreexistingOverlapIsNotModified") {
    Crowd crowd;
    crowd.addNamedAgent("farA", 50.f, 50.f, 0.f, 1.f);
    crowd.addNamedAgent("farB", 50.f, 50.f, 0.f, 1.f);
    SpawnBatch batch;
    batch.agents = {{"new", 0.f, 0.f, 0.f, 1.f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().displacedAgents, 0);
    CHECK_EQ(crowd.getAgentState(0).x, 50.f);
    CHECK_EQ(crowd.getAgentState(1).x, 50.f);
}


TEST_CASE("crowd.spawn.preservesMovingAgentVelocityAndTarget") {
    Crowd crowd;
    crowd.setSeparationWeight(0.f);
    const int original = crowd.addNamedAgent("moving", 0.f, 0.f, 0.f, 1.f);
    REQUIRE(crowd.setAgentAction(original, "seek"));
    REQUIRE(crowd.setAgentTarget(original, 100.f, 0.f));
    REQUIRE(crowd.advance(0.1f).ok());
    const auto before = crowd.getAgentState(original);
    REQUIRE(before.vx > 0.f);
    SpawnBatch batch;
    batch.agents = {{"new", before.x - 1.5f, before.y, 0.f, 1.f, {}}};
    auto result  = crowd.applySpawnBatch(batch);
    REQUIRE(result.ok());
    const auto after = crowd.getAgentState(original);
    CHECK_EQ(after.vx, before.vx);
    CHECK_EQ(after.vy, before.vy);
    CHECK_EQ(after.speed, before.speed);
    CHECK_EQ(after.action, before.action);
    CHECK(after.x > before.x);
    REQUIRE(crowd.advance(0.1f).ok());
    CHECK(crowd.getAgentState(original).x > after.x);
}
}  // namespace test_crowd_spawn_unit
#endif
