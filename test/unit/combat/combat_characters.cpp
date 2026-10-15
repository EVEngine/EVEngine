#include <limits>
#include "combat/CombatAttributes.h"
#include "combat/CombatCharacter.h"
#include "combat/CombatLocomotion.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// combat_attributes.cpp
namespace {
namespace combat_attributes {
namespace {

eve::combat::CombatAttributeDefinition health() { return {"Attribute.Vital.Health", 75.0, 0.0, 100.0, 5.0}; }

}  // namespace

TEST_CASE("combatAttributes.registersTagAddressedBoundedValues") {
    eve::combat::CombatAttributeRuntime runtime("fighter");
    REQUIRE(runtime.registerAttribute(health()).ok());
    CHECK(!runtime.registerAttribute(health()).ok());
    auto value = runtime.value("Attribute.Vital.Health");
    REQUIRE(value.ok());
    CHECK_EQ(value.value(), 75.0);
    CHECK(!runtime.value("Attribute.Vital.Mana").ok());
}

TEST_CASE("combatAttributes.emitsGainLossAndDepletionEvents") {
    eve::combat::CombatAttributeRuntime runtime;
    REQUIRE(runtime.registerAttribute(health()).ok());
    auto loss = runtime.modify("Attribute.Vital.Health", -25.0);
    REQUIRE(loss.ok());
    REQUIRE_EQ(loss.value().size(), 1u);
    CHECK(static_cast<int>(loss.value()[0].kind) == static_cast<int>(eve::combat::CombatAttributeEventKind::Loss));
    auto depleted = runtime.modify("Attribute.Vital.Health", -1000.0);
    REQUIRE(depleted.ok());
    REQUIRE_EQ(depleted.value().size(), 2u);
    CHECK(static_cast<int>(depleted.value()[1].kind) ==
          static_cast<int>(eve::combat::CombatAttributeEventKind::Depleted));
    auto gain = runtime.setValue("Attribute.Vital.Health", 10.0);
    REQUIRE(gain.ok());
    REQUIRE_EQ(gain.value().size(), 1u);
    CHECK(static_cast<int>(gain.value()[0].kind) == static_cast<int>(eve::combat::CombatAttributeEventKind::Gain));
}

TEST_CASE("combatAttributes.regenerationUsesInjectedDurationAndClamps") {
    eve::combat::CombatAttributeRuntime runtime;
    REQUIRE(runtime.registerAttribute(health()).ok());
    auto advanced = runtime.advance(eve::Duration::fromNanoseconds(1000000000));
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value().events.size(), 1u);
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 80.0);
    REQUIRE(runtime.advance(eve::Duration::fromNanoseconds(10000000000)).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 100.0);
    auto noOp = runtime.advance(eve::Duration::zero());
    REQUIRE(noOp.ok());
    CHECK(noOp.value().events.empty());
}

TEST_CASE("combatAttributes.rejectsInvalidInputWithoutMutation") {
    eve::combat::CombatAttributeRuntime runtime;
    auto                                invalid = health();
    invalid.regenerationPerSecond               = -1.0;
    CHECK(!runtime.registerAttribute(invalid).ok());
    REQUIRE(runtime.registerAttribute(health()).ok());
    CHECK(!runtime.modify("Attribute.Vital.Health", std::numeric_limits<double>::infinity()).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 75.0);
    CHECK(!runtime.advance(eve::Duration::fromNanoseconds(-1)).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 75.0);
}
}  // namespace combat_attributes
}  // namespace

// combat_character.cpp
namespace {
namespace combat_character {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatCharacter.jumpsDodgesAndAppliesRootMotionWhileAttacking") {
    const auto                          player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}, 5.0, 40.0, 6.0, 20.0, 10.0, 0.2, 1})
                .ok());
    REQUIRE(characters.setMoveIntent(player, {1.0, 0.0, 0.0}, 1.0).ok());
    REQUIRE(characters.jump(player).ok());
    auto airborne = characters.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(50000000)});
    REQUIRE(airborne.ok());
    auto state = characters.state(player);
    REQUIRE(state.ok());
    CHECK(state.value().position.y > 0.0);
    CHECK(state.value().mode == eve::combat::CombatCharacterMode::Airborne);

    REQUIRE(characters.dodge(player, eve::combat::CombatVector3{0.0, 0.0, 1.0}).ok());
    state = characters.state(player);
    REQUIRE(state.ok());
    CHECK(state.value().mode == eve::combat::CombatCharacterMode::Dodging);
    CHECK(state.value().invulnerable);

    for (std::uint64_t tick = 2; tick < 10; ++tick)
        REQUIRE(characters.advance({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(50000000)}).ok());
    state = characters.state(player);
    REQUIRE(state.ok());
    CHECK(!state.value().invulnerable);

    REQUIRE(characters.beginAttack(player).ok());
    REQUIRE(characters.setRootMotionDelta(player, {0.5, 0.0, 0.0}).ok());
    const double beforeX = characters.state(player).value().position.x;
    REQUIRE(characters.advance({eve::SimulationTick(10), eve::Duration::fromNanoseconds(16000000)}).ok());
    CHECK_EQ(characters.state(player).value().position.x, beforeX + 0.5);
    REQUIRE(characters.endAttack(player).ok());
}

class PlaneGround final : public eve::combat::ICombatGroundProvider {
public:
    double                            height = 0.0;
    [[nodiscard]] eve::Result<double> sampleHeight(double, double) const override {
        return eve::Result<double>::success(height);
    }
};

class WallProbe final : public eve::combat::ICombatMoveProbe {
public:
    double                                                maxX = 1.0;
    [[nodiscard]] eve::Result<eve::combat::CombatVector3> resolve(const eve::combat::CombatVector3&,
                                                                  const eve::combat::CombatVector3& to,
                                                                  double radius) const override {
        eve::combat::CombatVector3 result = to;
        const double               limit  = maxX - radius;
        if (result.x > limit) result.x = limit;
        return eve::Result<eve::combat::CombatVector3>::success(result);
    }
};

TEST_CASE("combatCharacter.followsGroundProviderAndMoveProbe") {
    const auto                          player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 2.0, 0.0}}).ok());
    PlaneGround ground;
    ground.height = 2.0;
    characters.setGroundProvider(ground);
    REQUIRE(characters.setMoveIntent(player, {1.0, 0.0, 0.0}, 1.0).ok());
    REQUIRE(characters.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(50000000)}).ok());
    auto state = characters.state(player);
    REQUIRE(state.ok());
    CHECK_EQ(state.value().position.y, 2.0);
    CHECK(state.value().mode == eve::combat::CombatCharacterMode::Grounded);
    CHECK(state.value().position.x > 0.0);

    WallProbe probe;
    probe.maxX = 0.5;
    characters.setMoveProbe(probe);
    for (std::uint64_t tick = 2; tick < 20; ++tick)
        REQUIRE(characters.advance({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(50000000)}).ok());
    state = characters.state(player);
    REQUIRE(state.ok());
    CHECK(state.value().position.x <= 0.5 - state.value().capsuleRadius + 1e-9);
}

TEST_CASE("combatCharacter.freezeSkipsIntegration") {
    const auto                          player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.setMoveIntent(player, {1.0, 0.0, 0.0}, 1.0).ok());
    REQUIRE(characters.setTimeFrozen(player, true).ok());
    REQUIRE(characters.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(50000000)}).ok());
    CHECK_EQ(characters.state(player).value().position.x, 0.0);
    REQUIRE(characters.setTimeFrozen(player, false).ok());
    REQUIRE(characters.advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(50000000)}).ok());
    CHECK(characters.state(player).value().position.x > 0.0);
}

TEST_CASE("combatCharacter.dodgeRelativeAndKnockbackWhileStunned") {
    const auto                          player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.dodgeRelative(player, {4.0, 0.0, 0.0}, eve::combat::CombatDodgeRelative::Back).ok());
    auto dodging = characters.state(player);
    REQUIRE(dodging.ok());
    CHECK(dodging.value().mode == eve::combat::CombatCharacterMode::Dodging);
    CHECK(dodging.value().velocity.x < 0.0);

    REQUIRE(characters
                .applyDamageReaction(player, eve::combat::HitReaction::Stagger,
                                     eve::Duration::fromNanoseconds(200000000), {3.0, 4.0, 0.0})
                .ok());
    auto stunned = characters.state(player);
    REQUIRE(stunned.ok());
    CHECK(stunned.value().mode == eve::combat::CombatCharacterMode::Stunned);
    CHECK_EQ(stunned.value().velocity.x, 3.0);
    CHECK_EQ(stunned.value().velocity.y, 4.0);
    const double beforeX = stunned.value().position.x;
    REQUIRE(characters.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(50000000)}).ok());
    CHECK(characters.state(player).value().position.x > beforeX);
    CHECK(characters.state(player).value().position.y > 0.0);
}
}  // namespace combat_character
}  // namespace

// combat_locomotion.cpp
namespace {
namespace combat_locomotion {
namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

class FailingNavigation final : public eve::combat::ICombatNavigationProvider {
public:
    eve::Result<eve::combat::CombatNavigationSteering> steer(const eve::combat::CombatLocomotionState&,
                                                             const eve::combat::CombatNavigationGoal&,
                                                             eve::SimulationTick) override {
        return eve::Result<eve::combat::CombatNavigationSteering>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected navigation failure"));
    }
};

}  // namespace

TEST_CASE("combatLocomotion.normalizesPlayerIntentAndAdvancesInjectedTime") {
    const auto                           fighter = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    eve::combat::CombatLocomotionRuntime runtime;
    REQUIRE(runtime.registerSubject({fighter, "fighter:player", {}, 4.0, 8.0}).ok());
    REQUIRE(runtime.setMoveIntent(fighter, {3.0, 4.0}, 0.5).ok());
    auto advanced = runtime.advance({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500'000'000)});
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
    CHECK(!runtime.advance({eve::SimulationTick::zero(), eve::Duration::fromNanoseconds(16'000'000)}).ok());
}

TEST_CASE("combatLocomotion.directNavigationArrivesWithoutOvershoot") {
    const auto                                  fighter = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::DirectCombatNavigationProvider navigation;
    eve::combat::CombatLocomotionRuntime        runtime(navigation);
    REQUIRE(runtime.registerSubject({fighter, "fighter:bot", {}, 10.0, 100.0}).ok());
    REQUIRE(runtime.navigateTo(fighter, {{1.0, 0.0}, 0.05}).ok());
    auto advanced = runtime.advance({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500'000'000)});
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
    CHECK(!runtime.advance({eve::SimulationTick{2}, eve::Duration::fromNanoseconds(100'000'000)}).ok());
    state = runtime.state(fighter);
    REQUIRE(state.ok());
    CHECK_EQ(state.value().position.x, 1.0);
    CHECK(state.value().navigationGoal.has_value());
}

TEST_CASE("combatLocomotion.providerFailurePublishesNoPartialFrame") {
    const auto                           first  = subject("21222324-2526-2728-a92a-2b2c2d2e2f30");
    const auto                           second = subject("31323334-3536-3738-b93a-3b3c3d3e3f40");
    FailingNavigation                    navigation;
    eve::combat::CombatLocomotionRuntime runtime(navigation);
    REQUIRE(runtime.registerSubject({first, "fighter:first", {}, 2.0, 10.0}).ok());
    REQUIRE(runtime.registerSubject({second, "fighter:second", {5.0, 0.0}, 2.0, 10.0}).ok());
    REQUIRE(runtime.setMoveIntent(first, {1.0, 0.0}, 1.0).ok());
    REQUIRE(runtime.navigateTo(second, {{0.0, 0.0}, 0.1}).ok());
    CHECK(!runtime.advance({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500'000'000)}).ok());
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
    const auto                           fighter = subject("41424344-4546-4748-c94a-4b4c4d4e4f50");
    eve::combat::CombatLocomotionRuntime runtime;
    REQUIRE(runtime.registerSubject({fighter, "fighter:player", {}, 3.0, 6.0}).ok());
    CHECK(!runtime.navigateTo(fighter, {{2.0, 0.0}, 0.1}).ok());
    CHECK(!runtime.registerSubject({fighter, "duplicate", {}, 3.0, 6.0}).ok());
    REQUIRE(runtime.unregisterSubject(fighter).ok());
    CHECK(!runtime.state(fighter).ok());
    CHECK(!runtime.setMoveIntent(fighter, {1.0, 0.0}, 1.0).ok());
}
}  // namespace combat_locomotion
}  // namespace
