#include "common/Capability.h"
#include "common/ECS.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/Targeting.h"
#include "tactics/LineOfSight.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsLineOfSightAdapter.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <utility>

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
    ecs::Table       world;
    ecs::ScopedTable guard(world);
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
    ecs::Table       world;
    ecs::ScopedTable guard(world);
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
    ecs::Table       world;
    ecs::ScopedTable guard(world);
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
