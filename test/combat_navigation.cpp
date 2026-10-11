#include "combat/navigation/PathfinderCombatNavigation.h"

#include "map/Pathfinder.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <utility>

namespace {

eve::SubjectRef fighterSubject() {
    auto parsed = eve::PersistentId::parse("51525354-5556-5758-d95a-5b5c5d5e5f60");
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

std::unique_ptr<eve::combat::navigation::PathfinderCombatNavigationProvider> provider(
    eve::map::Pathfinder& pathfinder) {
    auto created = eve::combat::navigation::PathfinderCombatNavigationProvider::create(pathfinder, {});
    REQUIRE(created.ok());
    return std::move(created).takeValue();
}

eve::combat::CombatLocomotionState stateAt(double x, double z) {
    eve::combat::CombatLocomotionState state;
    state.subject = fighterSubject();
    state.position = {x, z};
    return state;
}

}  // namespace

TEST_CASE("combatNavigation.pathfinderRoutesAroundBlockedCells") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    pathfinder.setBlocked(2, 0, true);
    pathfinder.setBlocked(2, 1, true);
    auto navigation = provider(pathfinder);

    auto steering = navigation->steer(stateAt(1.5, 1.5), {{4.5, 1.5}, 0.1}, eve::SimulationTick{1});
    REQUIRE(steering.ok());
    CHECK(steering.value().phase == eve::combat::CombatNavigationPhase::Moving);
    CHECK_EQ(steering.value().direction.x, 0.0);
    CHECK(steering.value().direction.z > 0.99);
}

TEST_CASE("combatNavigation.locomotionFollowsDetourAndArrives") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    pathfinder.setBlocked(2, 0, true);
    pathfinder.setBlocked(2, 1, true);
    auto navigation = provider(pathfinder);
    eve::combat::CombatLocomotionRuntime locomotion(*navigation);
    const auto fighter = fighterSubject();
    REQUIRE(locomotion.registerSubject({fighter, "fighter:path", {1.5, 1.5}, 2.0, 100.0}).ok());
    REQUIRE(locomotion.navigateTo(fighter, {{4.5, 1.5}, 0.05}).ok());

    bool arrived = false;
    for (std::uint64_t tick = 1; tick <= 64 && !arrived; ++tick) {
        auto advanced = locomotion.advance(
            {eve::SimulationTick{tick}, eve::Duration::fromNanoseconds(250'000'000)});
        REQUIRE(advanced.ok());
        auto current = locomotion.state(fighter);
        REQUIRE(current.ok());
        const int cellX = static_cast<int>(std::floor(current.value().position.x));
        const int cellY = static_cast<int>(std::floor(current.value().position.z));
        CHECK(pathfinder.isWalkable(cellX, cellY));
        arrived = !current.value().navigationGoal.has_value();
    }
    REQUIRE(arrived);
    auto current = locomotion.state(fighter);
    REQUIRE(current.ok());
    CHECK_EQ(current.value().position.x, 4.5);
    CHECK_EQ(current.value().position.z, 1.5);
}

TEST_CASE("combatNavigation.replansAfterCanonicalGridMutation") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    auto navigation = provider(pathfinder);
    const auto current = stateAt(1.5, 1.5);

    auto direct = navigation->steer(current, {{4.5, 1.5}, 0.1}, eve::SimulationTick{1});
    REQUIRE(direct.ok());
    CHECK(direct.value().direction.x > 0.99);
    CHECK_EQ(direct.value().direction.z, 0.0);

    pathfinder.setBlocked(2, 1, true);
    auto replanned = navigation->steer(current, {{4.5, 1.5}, 0.1}, eve::SimulationTick{2});
    REQUIRE(replanned.ok());
    CHECK(replanned.value().direction.x < 0.01);
    CHECK(std::abs(replanned.value().direction.z) > 0.99);
}

TEST_CASE("combatNavigation.unreachableGoalAbortsLocomotionAtomically") {
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setTopology("ortho4");
    for (int y = 0; y < 3; ++y) pathfinder.setBlocked(2, y, true);
    auto navigation = provider(pathfinder);
    eve::combat::CombatLocomotionRuntime locomotion(*navigation);
    const auto fighter = fighterSubject();
    REQUIRE(locomotion.registerSubject({fighter, "fighter:navigator", {1.5, 1.5}, 4.0, 8.0}).ok());
    REQUIRE(locomotion.navigateTo(fighter, {{4.5, 1.5}, 0.1}).ok());

    auto advanced = locomotion.advance(
        {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(250'000'000)});
    REQUIRE(!advanced.ok());
    REQUIRE(advanced.status().diagnostics().front().code() == eve::DiagnosticCode::NotFound);
    auto current = locomotion.state(fighter);
    REQUIRE(current.ok());
    CHECK_EQ(current.value().position.x, 1.5);
    CHECK_EQ(current.value().position.z, 1.5);
    CHECK(current.value().navigationGoal.has_value());
}

TEST_CASE("combatNavigation.rejectsInvalidGridProjection") {
    eve::map::Pathfinder pathfinder(2, 2);
    auto invalid = eve::combat::navigation::PathfinderCombatNavigationProvider::create(
        pathfinder, {{}, 0.0});
    CHECK(!invalid.ok());
}
