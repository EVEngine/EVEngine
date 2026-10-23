#include "zeroerr/assert.h"
#include <algorithm>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "crowd/Crowd.h"
#include "crowd/CrowdSpatialIndex.h"
#include "zeroerr/unittest.h"
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
    CHECK(index.cellCount() <= xs.size());
    CHECK(comparisons > 0);
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
        CHECK(actual == expected);
        CHECK(visits >= cells);
    }
    xs   = {0.1f, 0.2f, 0.3f};
    ys   = {0.f, 0.f, 0.f};
    ties = {{true, 1}, {false, 2}, {false, 1}};
    index.rebuild(xs, ys, ties, 2.0, 1, comparisons);
    std::vector<int> order;
    std::int64_t     visits = 0, cells = 0;
    index.visit(0.2, 0.0, 1.0, 100, visits, cells, [&](int slot) { order.push_back(slot); });
    CHECK(order == std::vector<int>({2, 1, 0}));
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

TEST_CASE("crowd.spatial.scriptBudgetExtensionIsOptionalAndStrictInteger") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    auto    cls   = table.addClass<Crowd>("Crowd");
    Crowd::expose(cls);
    vm.run(vm.compileSource(R"(
        local crowd = eve.Crowd();
        crowd.setClampToField(false);
        crowd.setDefaultSpeed(0.0);
        crowd.addAgent(0.0,0.0,0.0,0.5);
        local budget = {maxSubsteps=1024,maxContactPasses=12,maxGridCells=10,
            maxNeighborVisits=10000,maxPredictionChecks=10000,maxContactChecks=10000};
        local first = crowd.advanceTick({tick=1,deltaNanoseconds=10000000,budget=budget});
        assert(first.ok && first.value.spatialVisits > 0 && first.value.gridEntriesPeak == 1);
        budget.maxSpatialVisits <- 0;
        assert(!crowd.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxSpatialVisits = 1.5;
        assert(!crowd.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
        budget.maxSpatialVisits = 10000;
        assert(crowd.advanceTick({tick=2,deltaNanoseconds=10000000,budget=budget}).ok);
    )"));
}
