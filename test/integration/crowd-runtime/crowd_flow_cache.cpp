// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "crowd",
  "origins": [
    "crowd_flow_cache.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include <algorithm>
#include <cmath>
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
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
