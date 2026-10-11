#include "combat/CombatLocomotion.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

class FailingNavigation final : public eve::combat::ICombatNavigationProvider {
public:
    eve::Result<eve::combat::CombatNavigationSteering> steer(
        const eve::combat::CombatLocomotionState&, const eve::combat::CombatNavigationGoal&,
        eve::SimulationTick) override {
        return eve::Result<eve::combat::CombatNavigationSteering>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected navigation failure"));
    }
};

}  // namespace

TEST_CASE("combatLocomotion.normalizesPlayerIntentAndAdvancesInjectedTime") {
    const auto fighter = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    eve::combat::CombatLocomotionRuntime runtime;
    REQUIRE(runtime.registerSubject({fighter, "fighter:player", {}, 4.0, 8.0}).ok());
    REQUIRE(runtime.setMoveIntent(fighter, {3.0, 4.0}, 0.5).ok());
    auto advanced = runtime.advance(
        {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500'000'000)});
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value().events.size(), 1u);
    auto state = runtime.state(fighter);
    REQUIRE(state.ok());
    CHECK_EQ(state.value().velocity.x, 1.2);
    CHECK_EQ(state.value().velocity.z, 1.6);
    CHECK_EQ(state.value().position.x, 0.6);
    CHECK_EQ(state.value().position.z, 0.8);
    CHECK_EQ(state.value().facing.x, 0.6);
    CHECK_EQ(state.value().facing.z, 0.8);
    CHECK(!runtime.advance(
        {eve::SimulationTick::zero(), eve::Duration::fromNanoseconds(16'000'000)}).ok());
}

TEST_CASE("combatLocomotion.directNavigationArrivesWithoutOvershoot") {
    const auto fighter = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::DirectCombatNavigationProvider navigation;
    eve::combat::CombatLocomotionRuntime runtime(navigation);
    REQUIRE(runtime.registerSubject({fighter, "fighter:bot", {}, 10.0, 100.0}).ok());
    REQUIRE(runtime.navigateTo(fighter, {{1.0, 0.0}, 0.05}).ok());
    auto advanced = runtime.advance(
        {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500'000'000)});
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value().events.size(), 2u);
    CHECK(advanced.value().events[1].kind == eve::combat::CombatLocomotionEventKind::Arrived);
    auto state = runtime.state(fighter);
    REQUIRE(state.ok());
    CHECK_EQ(state.value().position.x, 1.0);
    CHECK_EQ(state.value().position.z, 0.0);
    CHECK_EQ(state.value().velocity.x, 0.0);
    CHECK(!state.value().navigationGoal.has_value());

    REQUIRE(runtime.navigateTo(fighter, {{3.0, 0.0}, 0.05}).ok());
    runtime.clearNavigationProvider();
    CHECK(!runtime.advance(
        {eve::SimulationTick{2}, eve::Duration::fromNanoseconds(100'000'000)}).ok());
    state = runtime.state(fighter);
    REQUIRE(state.ok());
    CHECK_EQ(state.value().position.x, 1.0);
    CHECK(state.value().navigationGoal.has_value());
}

TEST_CASE("combatLocomotion.providerFailurePublishesNoPartialFrame") {
    const auto first = subject("21222324-2526-2728-a92a-2b2c2d2e2f30");
    const auto second = subject("31323334-3536-3738-b93a-3b3c3d3e3f40");
    FailingNavigation navigation;
    eve::combat::CombatLocomotionRuntime runtime(navigation);
    REQUIRE(runtime.registerSubject({first, "fighter:first", {}, 2.0, 10.0}).ok());
    REQUIRE(runtime.registerSubject({second, "fighter:second", {5.0, 0.0}, 2.0, 10.0}).ok());
    REQUIRE(runtime.setMoveIntent(first, {1.0, 0.0}, 1.0).ok());
    REQUIRE(runtime.navigateTo(second, {{0.0, 0.0}, 0.1}).ok());
    CHECK(!runtime.advance(
        {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500'000'000)}).ok());
    auto firstState = runtime.state(first);
    REQUIRE(firstState.ok());
    CHECK_EQ(firstState.value().position.x, 0.0);
    CHECK_EQ(firstState.value().velocity.x, 0.0);
    auto secondState = runtime.state(second);
    REQUIRE(secondState.ok());
    CHECK_EQ(secondState.value().position.x, 5.0);
    CHECK(secondState.value().navigationGoal.has_value());
}

TEST_CASE("combatLocomotion.providerAbsenceAndStaleSubjectsAreObservable") {
    const auto fighter = subject("41424344-4546-4748-c94a-4b4c4d4e4f50");
    eve::combat::CombatLocomotionRuntime runtime;
    REQUIRE(runtime.registerSubject({fighter, "fighter:player", {}, 3.0, 6.0}).ok());
    CHECK(!runtime.navigateTo(fighter, {{2.0, 0.0}, 0.1}).ok());
    CHECK(!runtime.registerSubject({fighter, "duplicate", {}, 3.0, 6.0}).ok());
    REQUIRE(runtime.unregisterSubject(fighter).ok());
    CHECK(!runtime.state(fighter).ok());
    CHECK(!runtime.setMoveIntent(fighter, {1.0, 0.0}, 1.0).ok());
}
