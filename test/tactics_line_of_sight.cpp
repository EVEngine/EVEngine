#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/ECS.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/Targeting.h"
#include "tactics/LineOfSight.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsLineOfSightAdapter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// tactics_line_of_sight.cpp
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
            const bool               blocked = isBlocked(blockers, cell);
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
            const Cell               cell{q, r, 0};
            const bool               blocked = isBlocked(blockers, cell);
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
    auto       visible = eve::tactics::visibleCellsInRange(blocked, *policy, origin, 1, 2, CellRangeMetric::Chebyshev);
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

// tactics_line_of_sight_provider.cpp
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::sensing::TargetLocation gridAt(std::int32_t x, std::int32_t y) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid2D(x, y));
}

eve::sensing::TargetLocation grid3At(std::int32_t x, std::int32_t y, std::int32_t z) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid3D(x, y, z));
}

eve::sensing::TargetLocation worldAt(float x, float y, float z) {
    auto point = eve::sensing::WorldPoint::world3D(x, y, z);
    REQUIRE(point.ok());
    return eve::sensing::TargetLocation(std::move(point).takeValue());
}

}  // namespace

TEST_CASE("tactics.gridLineOfSightIsReachableFromTheSensingCapability") {
    // Importing the module claims the grid spaces with the sensing router: the targeting pipeline
    // asks through the capability and reaches the tactics adapter.
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto router = eve::sensing::ensureLineOfSightRouter();
    REQUIRE(router.ok());
    auto* registered = eve::cap::query<eve::sensing::ILineOfSightQuery>();
    REQUIRE(registered == router.value());
    const auto spaces = router.value()->spaces();
    // Both grid spaces are claimed by tactics; the world spaces stay available for physics.
    CHECK(router.value()->hasProvider(eve::sensing::CoordinateSpace::Grid2D));
    CHECK(router.value()->hasProvider(eve::sensing::CoordinateSpace::Grid3D));
    CHECK(!router.value()->hasProvider(eve::sensing::CoordinateSpace::World3D));
    CHECK_EQ(spaces.size(), std::size_t{2});

    // Nothing is bound yet: the answer is Unsupported, not "not visible".
    auto unbound = registered->query(gridAt(0, 0), gridAt(3, 0));
    CHECK(!unbound.ok());
    REQUIRE(unbound.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(unbound.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    CHECK_EQ(unbound.status().primaryDiagnostic()->message(),
             std::string("no tactics battle is bound for grid line of sight"));

    // A world-space query stays refused by the router even though the adapter is registered.
    auto crossSpace = registered->query(gridAt(0, 0), worldAt(1.f, 0.f, 0.f));
    CHECK(!crossSpace.ok());
    CHECK_EQ(crossSpace.status().primaryDiagnostic()->code(), eve::DiagnosticCode::InvalidArgument);
    (void)tactics;
}

TEST_CASE("tactics.boundBattleAnswersGridLineOfSightAndRefusesASecondBoard") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto first = tactics.newBattle(subject("00000000-0000-0000-0000-000000000c10"), 31);
    REQUIRE(first.ok());
    const auto firstBattle = std::move(first).takeValue();
    for (int x = 0; x < 4; ++x) REQUIRE(tactics.addCell(firstBattle, {x, 0, 0}).ok());

    auto* registered = eve::cap::query<eve::sensing::ILineOfSightQuery>();
    REQUIRE(registered != nullptr);
    REQUIRE(tactics.attachLineOfSightBoard(firstBattle).ok());

    // An open corridor is visible, and the layered variant maps z to the tactical layer.
    auto open = registered->query(gridAt(0, 0), gridAt(3, 0));
    REQUIRE(open.ok());
    CHECK(open.value().visible);
    // The blocker field is intentionally empty: the grid policy answers visibility, not identity.
    CHECK(!open.value().blocker.has_value());
    auto layered = registered->query(grid3At(0, 0, 0), grid3At(3, 0, 0));
    REQUIRE(layered.ok());
    CHECK(layered.value().visible);
    // A cell the board does not contain is missing data, not "visible".
    auto missing = registered->query(gridAt(0, 0), gridAt(9, 9));
    CHECK(!missing.ok());
    REQUIRE(missing.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(missing.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);

    // One board at a time: a second, different battle is refused rather than silently replacing
    // the first, because the answer would otherwise depend on binding order.
    auto second = tactics.newBattle(subject("00000000-0000-0000-0000-000000000c11"), 32);
    REQUIRE(second.ok());
    const auto secondBattle = std::move(second).takeValue();
    REQUIRE(tactics.addCell(secondBattle, {0, 0, 0}).ok());
    auto conflict = tactics.attachLineOfSightBoard(secondBattle);
    CHECK(!conflict.ok());
    REQUIRE(conflict.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(conflict.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Conflict);
    // Re-binding the same battle is a NoOp, and a foreign detach is a NoOp too.
    auto again = tactics.attachLineOfSightBoard(firstBattle);
    REQUIRE(again.ok());
    CHECK(again.code() == eve::StatusCode::NoOp);
    auto foreignDetach = tactics.detachLineOfSightBoard(secondBattle);
    REQUIRE(foreignDetach.ok());
    CHECK(foreignDetach.code() == eve::StatusCode::NoOp);

    // Detaching returns to "unbound": Unsupported again, never a stale board answer.
    auto detached = tactics.detachLineOfSightBoard(firstBattle);
    REQUIRE(detached.ok());
    CHECK(detached.code() == eve::StatusCode::Applied);
    CHECK(!eve::tactics::tacticsLineOfSightAdapter().hasBoundBattle());
    auto afterDetach = registered->query(gridAt(0, 0), gridAt(3, 0));
    CHECK(!afterDetach.ok());
    CHECK_EQ(afterDetach.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("tactics.sightBlockerCellMakesTheGridQueryOpaque") {
    ecs::Table            world;
    ecs::ScopedTable      guard(world);
    eve::tactics::Tactics tactics;

    auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000c20"), 33);
    REQUIRE(created.ok());
    const auto battle = std::move(created).takeValue();
    REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
    // The blocking fact is the persisted cell tag, written through the same setup path a game uses,
    // so the adapter reads exactly what the module's own range query reads: one authority for
    // "what blocks sight" and no second schema.
    eve::tactics::CellState blocker;
    blocker.tags.emplace_back(eve::tactics::kSightBlockerTag);
    REQUIRE(tactics.addCell(battle, {1, 0, 0}, blocker).ok());
    REQUIRE(tactics.addCell(battle, {2, 0, 0}).ok());
    auto* registered = eve::cap::query<eve::sensing::ILineOfSightQuery>();
    REQUIRE(registered != nullptr);
    REQUIRE(tactics.attachLineOfSightBoard(battle).ok());

    auto blocked = registered->query(gridAt(0, 0), gridAt(2, 0));
    REQUIRE(blocked.ok());
    CHECK(!blocked.value().visible);
    // Symmetry holds because the policy computes the trace from the canonical cell order.
    auto reversed = registered->query(gridAt(2, 0), gridAt(0, 0));
    REQUIRE(reversed.ok());
    CHECK_EQ(reversed.value().visible, blocked.value().visible);
    // Endpoints never block: the blocking cell is still visible from itself.
    auto self = registered->query(gridAt(1, 0), gridAt(1, 0));
    REQUIRE(self.ok());
    CHECK(self.value().visible);
}
