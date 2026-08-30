#include "tactics/TacticsPath.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

}  // namespace

TEST_CASE("tactics.reachabilityUsesFixedCostsAndOccupancy") {
    eve::tactics::BoardState board;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) REQUIRE(board.addCell({x, y, 0}).ok());
    }
    const auto mover   = subject("00000000-0000-0000-0000-000000000031");
    const auto blocker = subject("00000000-0000-0000-0000-000000000032");
    REQUIRE(board.place(mover, {0, 0, 0}).ok());
    REQUIRE(board.place(blocker, {1, 0, 0}).ok());

    auto reachable = eve::tactics::PathQuery::reachable(board, mover, 400);
    REQUIRE(reachable.ok());
    CHECK(reachable.value().contains({0, 0, 0}));
    CHECK(!reachable.value().contains({1, 0, 0}));
    CHECK(reachable.value().contains({2, 0, 0}));
    auto cost = reachable.value().cost({2, 0, 0});
    REQUIRE(cost.ok());
    CHECK_EQ(std::move(cost).takeValue(), 400);
}

TEST_CASE("tactics.equalCostPathHasStableCoordinateTieBreak") {
    eve::tactics::BoardState board;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) REQUIRE(board.addCell({x, y, 0}).ok());
    }
    const auto mover = subject("00000000-0000-0000-0000-000000000033");
    REQUIRE(board.place(mover, {0, 0, 0}).ok());

    auto path = eve::tactics::PathQuery::path(board, mover, {1, 1, 0}, 200);
    REQUIRE(path.ok());
    const auto cells = std::move(path).takeValue();
    REQUIRE_EQ(cells.size(), 3u);
    CHECK((cells[0] == eve::tactics::Cell{0, 0, 0}));
    CHECK((cells[1] == eve::tactics::Cell{1, 0, 0}));
    CHECK((cells[2] == eve::tactics::Cell{1, 1, 0}));
}

TEST_CASE("tactics.cellsInRangeUsesStableLogicalMetrics") {
    eve::tactics::BoardState board;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) REQUIRE(board.addCell({x, y, 0}).ok());

    auto manhattan = eve::tactics::PathQuery::cellsInRange(
        board, {0, 0, 0}, 1, 1, eve::tactics::CellRangeMetric::Manhattan);
    REQUIRE(manhattan.ok());
    CHECK_EQ(manhattan.value().size(), 4u);
    CHECK_EQ(manhattan.value().front(), (eve::tactics::Cell{0, -1, 0}));

    auto chebyshev = eve::tactics::PathQuery::cellsInRange(
        board, {0, 0, 0}, 2, 2, eve::tactics::CellRangeMetric::Chebyshev);
    REQUIRE(chebyshev.ok());
    CHECK_EQ(chebyshev.value().size(), 16u);

    auto invalid = eve::tactics::PathQuery::cellsInRange(
        board, {0, 0, 0}, 3, 2, eve::tactics::CellRangeMetric::Hex);
    CHECK(!invalid.ok());
}
