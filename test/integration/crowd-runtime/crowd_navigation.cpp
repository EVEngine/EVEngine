#include "zeroerr/assert.h"
#include "crowd/Crowd.h"
#include "zeroerr/unittest.h"
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
