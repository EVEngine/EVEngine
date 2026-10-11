#include "common/Capability.h"
#include "common/ECS.h"
#include "tactics/LineOfSight.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <utility>
#include <vector>

namespace {

using eve::tactics::BoardState;
using eve::tactics::BoardTopology;
using eve::tactics::Cell;
using eve::tactics::CellRangeMetric;
using eve::tactics::CellState;

bool isBlocked(const std::vector<Cell>& blockers, Cell cell) {
    return std::find(blockers.begin(), blockers.end(), cell) != blockers.end();
}

/**
 * @brief A 5x5 square board, with chosen cells created as sight blockers or left off.
 *
 * Blockers are created as **passable** cells carrying the sight-blocker tag, because that is
 * the case the tag exists for: smoke or a low wall stops sight without stopping movement.
 */
BoardState squareBoard(const std::vector<Cell>& blockers = {}, const std::vector<Cell>& missing = {}) {
    BoardState board;
    board.setTopology(BoardTopology::Square8);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            const Cell cell{x, y, 0};
            if (isBlocked(missing, cell)) continue;
            const bool blocked = isBlocked(blockers, cell);
            std::vector<std::string> tags;
            if (blocked) tags.emplace_back(eve::tactics::kSightBlockerTag);
            REQUIRE(board.addCell(cell, CellState{100, 0, true, std::move(tags)}).ok());
        }
    return board;
}

/** @brief A hex board large enough for the axis traces. */
BoardState hexBoard(const std::vector<Cell>& blockers = {}) {
    BoardState board;
    board.setTopology(BoardTopology::HexAxial);
    for (int q = -3; q <= 3; ++q)
        for (int r = -3; r <= 3; ++r) {
            const Cell cell{q, r, 0};
            const bool blocked = isBlocked(blockers, cell);
            std::vector<std::string> tags;
            if (blocked) tags.emplace_back(eve::tactics::kSightBlockerTag);
            REQUIRE(board.addCell(cell, CellState{100, 0, true, std::move(tags)}).ok());
        }
    return board;
}

}  // namespace

TEST_CASE("tactics.gridLineOfSightBlocksOnlyStrictlyBetweenTheEndpoints") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();
    const Cell origin{0, 0, 0};
    const Cell far{4, 0, 0};

    CHECK(policy->visible(squareBoard(), origin, far).value());
    // A blocker on the line blocks.
    CHECK(!policy->visible(squareBoard({{2, 0, 0}}), origin, far).value());
    // Endpoints never block: a unit in smoke can still see out of its own cell, and a blocker
    // on the target cell protects the target without hiding it.
    CHECK(policy->visible(squareBoard({{0, 0, 0}, {4, 0, 0}}), origin, far).value());
    // A blocker off the line does not block.
    CHECK(policy->visible(squareBoard({{2, 1, 0}}), origin, far).value());
    // A cell the board does not contain blocks: the unused grid is not a free firing lane.
    CHECK(!policy->visible(squareBoard({}, {{2, 0, 0}}), origin, far).value());
}

TEST_CASE("tactics.gridLineOfSightIsSymmetricAcrossAWholeBoardSweep") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();

    // The gap analysis flagged that the repository had no field-of-view symmetry coverage at
    // all. This sweeps every ordered pair on a board with several blockers, so a trace that
    // disagreed with its own reverse cannot hide behind one hand-picked case.
    const auto board = squareBoard({{1, 1, 0}, {2, 3, 0}, {3, 1, 0}});

    std::size_t pairs = 0;
    for (int ay = 0; ay < 5; ++ay)
        for (int ax = 0; ax < 5; ++ax)
            for (int by = 0; by < 5; ++by)
                for (int bx = 0; bx < 5; ++bx) {
                    const Cell a{ax, ay, 0};
                    const Cell b{bx, by, 0};
                    const auto forward  = policy->visible(board, a, b);
                    const auto backward = policy->visible(board, b, a);
                    REQUIRE(forward.ok());
                    REQUIRE(backward.ok());
                    CHECK_EQ(forward.value(), backward.value());
                    ++pairs;
                }
    CHECK_EQ(pairs, std::size_t{625});
}

TEST_CASE("tactics.gridLineOfSightUsesTheBoardsOwnTopology") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();

    // Hex axial: a straight line along one axis is blocked by the cell in the middle.
    CHECK(policy->visible(hexBoard(), {-3, 0, 0}, {3, 0, 0}).value());
    const auto blocked = hexBoard({{0, 0, 0}});
    CHECK(!policy->visible(blocked, {-3, 0, 0}, {3, 0, 0}).value());
    // A different axial direction also blocks through the shared cell.
    CHECK(!policy->visible(blocked, {-2, 2, 0}, {2, -2, 0}).value());
    // And the hex trace is symmetric too.
    CHECK(policy->visible(blocked, {3, 0, 0}, {-3, 0, 0}).value() ==
          policy->visible(blocked, {-3, 0, 0}, {3, 0, 0}).value());

    // Refusals are structured, not silent: another layer is a rule this policy does not own,
    // and an unknown cell is missing data rather than "not visible".
    auto crossLayer = policy->visible(hexBoard(), {-3, 0, 0}, {3, 0, 1});
    CHECK(!crossLayer.ok());
    REQUIRE(crossLayer.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(crossLayer.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    auto unknown = policy->visible(hexBoard(), {-3, 0, 0}, {9, 9, 0});
    CHECK(!unknown.ok());
    REQUIRE(unknown.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(unknown.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);
}

TEST_CASE("tactics.gridCoverSeparatesInTheWayFromBeside") {
    const auto policy = eve::tactics::gridCoverPolicy();
    const Cell attacker{0, 2, 0};
    const Cell target{4, 2, 0};
    CHECK(policy->cover(squareBoard(), attacker, target).value() == eve::tactics::CoverLevel::None);

    // A blocker between the two is full cover: the shot is absorbed. Note the blocker is
    // passable, which is what makes sight a separate fact from movement.
    CHECK(policy->cover(squareBoard({{2, 2, 0}}), attacker, target).value() == eve::tactics::CoverLevel::Full);
    // A blocker merely adjacent to the target is half cover: harder to hit, not absorbed.
    CHECK(policy->cover(squareBoard({{4, 1, 0}}), attacker, target).value() == eve::tactics::CoverLevel::Half);
    // A blocker next to the attacker is neither: it does not protect the target.
    CHECK(policy->cover(squareBoard({{1, 2, 0}}), attacker, target).value() == eve::tactics::CoverLevel::None);
    // Cover must not depend on which way the query runs.
    CHECK(policy->cover(squareBoard({{4, 1, 0}}), target, attacker).value() == eve::tactics::CoverLevel::Half);
    CHECK_EQ(eve::tactics::coverLevelName(eve::tactics::CoverLevel::Full), std::string_view("full"));
}

TEST_CASE("tactics.visibleCellsInRangeComposesRangeWithSight") {
    const auto policy = eve::tactics::gridLineOfSightPolicy();
    const auto board  = squareBoard();
    const Cell origin{2, 2, 0};

    auto open = eve::tactics::visibleCellsInRange(board, *policy, origin, 1, 2, CellRangeMetric::Chebyshev);
    REQUIRE(open.ok());
    // With nothing blocking, the visible set is exactly the range set.
    auto range = eve::tactics::PathQuery::cellsInRange(board, origin, 1, 2, CellRangeMetric::Chebyshev);
    REQUIRE(range.ok());
    CHECK(open.value() == range.value());
    CHECK(!open.value().empty());

    // Blocking one ring cell removes everything strictly behind it from the same query.
    const auto blocked = squareBoard({{3, 2, 0}});
    auto visible = eve::tactics::visibleCellsInRange(blocked, *policy, origin, 1, 2, CellRangeMetric::Chebyshev);
    REQUIRE(visible.ok());
    CHECK(visible.value().size() < range.value().size());
    const Cell behind{4, 2, 0};
    CHECK(std::find(visible.value().begin(), visible.value().end(), behind) == visible.value().end());

    // Invalid bounds and unknown origins are refusals, and the range rule stays their
    // authority: this query composes the two rules instead of restating either.
    CHECK(!eve::tactics::visibleCellsInRange(board, *policy, origin, 3, 1, CellRangeMetric::Chebyshev).ok());
    CHECK(!eve::tactics::visibleCellsInRange(board, *policy, {9, 9, 0}, 1, 2, CellRangeMetric::Chebyshev).ok());
}

TEST_CASE("tactics.lineOfSightPoliciesAreCapabilityProviders") {
    // The interfaces exist so a different board can answer these questions instead of the
    // tactics core growing a second geometry. This proves the capability shape works, which is
    // what an adapter (for example a hex map with its own projection) registers through.
    const auto sight = eve::tactics::gridLineOfSightPolicy();
    eve::cap::provide<eve::tactics::ILineOfSightPolicy>(sight.get());
    auto* queriedSight = eve::cap::query<eve::tactics::ILineOfSightPolicy>();
    REQUIRE(queriedSight != nullptr);
    CHECK_EQ(std::string(queriedSight->id()), std::string("grid_line_of_sight"));

    const auto cover = eve::tactics::gridCoverPolicy();
    eve::cap::provide<eve::tactics::ICoverPolicy>(cover.get());
    auto* queriedCover = eve::cap::query<eve::tactics::ICoverPolicy>();
    REQUIRE(queriedCover != nullptr);
    CHECK_EQ(std::string(queriedCover->id()), std::string("grid_cover"));
    // Revoking leaves the slot empty again, which is the "provider absent" configuration a
    // caller has to handle: an absent provider is a null query, never a silent default.
    eve::cap::revoke<eve::tactics::ILineOfSightPolicy>(sight.get());
    CHECK(eve::cap::query<eve::tactics::ILineOfSightPolicy>() == nullptr);
    eve::cap::revoke<eve::tactics::ICoverPolicy>(cover.get());

    CHECK_EQ(std::string(eve::tactics::ILineOfSightPolicy::capabilityName),
             std::string("eve.tactics.ILineOfSightPolicy"));
    CHECK_EQ(std::string(eve::tactics::ICoverPolicy::capabilityName), std::string("eve.tactics.ICoverPolicy"));
}
