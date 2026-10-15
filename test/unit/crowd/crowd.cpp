#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "crowd/Crowd.h"
#include "crowd/CrowdField.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

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
        float x = 0.f, y = 0.f;
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
    float px = ox, py = oy;
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
    crowd->setSeparationWeight(0.f);  // 纯流场跟随；分离行为由 boids 测试覆盖
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
        const AgentState s = crowd->getAgentState(i);
        const int cx = int(s.x / 16.f);
        const int cy = int(s.y / 16.f);
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
        const int first = crowd.addNamedAgent(reverse ? "low" : "high", 0.0f, 0.0f, 0.0f, 1.0f);
        const int second = crowd.addNamedAgent(reverse ? "high" : "low", 0.0f, 0.0f, 0.0f, 1.0f);
        REQUIRE(first >= 0);
        REQUIRE(second >= 0);
        const int high = crowd.getNamedAgentIndex("high");
        const int low = crowd.getNamedAgentIndex("low");
        REQUIRE(crowd.setAgentAction(high, "idle"));
        REQUIRE(crowd.setAgentAction(low, "idle"));
        REQUIRE(crowd.setAgentAvoidancePriority(high, 10).ok());
        REQUIRE(crowd.setAgentAvoidancePriority(low, -10).ok());
        crowd.step(0.1f);
        CHECK_EQ(crowd.getAgentAvoidancePriority(high), 10);
        CHECK_EQ(crowd.getAgentAvoidancePriority(low), -10);
        const AgentState highState = crowd.getAgentState(high);
        const AgentState lowState = crowd.getAgentState(low);
        CHECK(std::hypot(highState.x, highState.y) < 1e-5f);
        CHECK(std::hypot(lowState.x, lowState.y) > 1.9f);
        return lowState;
    };

    const AgentState forward = run(false);
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
        const int id =
            crowd->addAgent(480.f + std::cos(ang) * 8.f, 480.f + std::sin(ang) * 8.f, 0.f, 4.f);
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
            minD = std::min(minD, dist(sa.x, sa.y, sb.x, sb.y));
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

    const AgentState sa = crowd->getAgentState(a);
    const AgentState sb = crowd->getAgentState(b);
    const float finalD = dist(sa.x, sa.y, sb.x, sb.y);
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
    const int alpha = crowd->addNamedAgent("unit.alpha", 16.f, 0.f, 0.f, 4.f);
    const int bravo = crowd->addNamedAgent("unit.bravo", 32.f, 0.f, 0.f, 4.f);
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
