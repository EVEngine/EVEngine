// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "combat_camera_framing.cpp",
    "combat_attributes.cpp",
    "combat_character.cpp",
    "combat_locomotion.cpp",
    "combat_carrier.cpp",
    "combat_melee_hit.cpp",
    "combat_motion_warp.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_camera_framing)
#include "PrivateTestScope.h"
#include <cmath>
#include "combat/CombatCamera.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_camera_framing.cpp
namespace test_combat_camera_framing_unit {
TEST_CASE("combatCameraFraming.looksAtMidpointFromBehindPlayer") {
    eve::combat::CombatCameraFramingRequest request;
    request.player       = {0.0, 0.0, 0.0};
    request.playerFacing = {1.0, 0.0, 0.0};
    request.lockTarget   = eve::combat::CombatVector3{4.0, 0.0, 0.0};
    request.distance     = 6.0;
    request.height       = 2.0;
    request.lookHeight   = 1.2;
    auto view            = eve::combat::CombatCameraFraming::solve(request);
    REQUIRE(view.ok());
    CHECK(std::abs(view.value().lookAt.x - 2.0) < 1e-9);
    CHECK(std::abs(view.value().lookAt.y - 1.2) < 1e-9);
    CHECK(std::abs(view.value().eye.x + 6.0) < 1e-9);
    CHECK(std::abs(view.value().eye.y - 2.0) < 1e-9);
    CHECK(std::abs(view.value().eye.z) < 1e-9);
}

TEST_CASE("combatCameraFraming.followsFacingWithoutLock") {
    eve::combat::CombatCameraFramingRequest request;
    request.player       = {0.0, 0.0, 0.0};
    request.playerFacing = {0.0, 0.0, 1.0};
    request.distance     = 6.0;
    request.height       = 2.0;
    request.lookHeight   = 1.2;
    auto view            = eve::combat::CombatCameraFraming::solve(request);
    REQUIRE(view.ok());
    CHECK(std::abs(view.value().lookAt.z) < 1e-9);
    CHECK(std::abs(view.value().eye.z + 6.0) < 1e-9);
    CHECK(std::abs(view.value().eye.y - 2.0) < 1e-9);
}

TEST_CASE("combatCameraFraming.rejectsInvalidDistance") {
    eve::combat::CombatCameraFramingRequest request;
    request.player   = {0.0, 0.0, 0.0};
    request.distance = 0.0;
    CHECK(!eve::combat::CombatCameraFraming::solve(request).ok());
}
}  // namespace test_combat_camera_framing_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_attributes) || \
    defined(EVE_TEST_ORIGIN_combat_character) || defined(EVE_TEST_ORIGIN_combat_locomotion)
#include "PrivateTestScope.h"
#include <limits>
#include "combat/CombatAttributes.h"
#include "combat/CombatCharacter.h"
#include "combat/CombatLocomotion.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: combat_characters
namespace test_combat_characters {
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
}  // namespace test_combat_characters
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_carrier) || \
    defined(EVE_TEST_ORIGIN_combat_melee_hit)
#include "PrivateTestScope.h"
#include <cmath>
#include <map>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string_view>
#include "combat/MeleeHit.h"
#include "common/Diagnostic.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "weapon/CombatCarrier.h"
#include "weapon/Weapon.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: combat_hit_resolution
namespace test_combat_hit_resolution {
// combat_carrier.cpp
namespace {
namespace combat_carrier {
// combat_carrier.cpp
namespace {

[[maybe_unused]] eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

[[maybe_unused]] eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return duration.value();
}

class TargetProvider final : public eve::weapon::IProjectileTargetProvider {
public:
    [[nodiscard]] eve::Result<eve::weapon::ProjectilePoint> position(ecs::EntityHandle target) const override {
        auto found = positions.find(target.id);
        if (reject || found == positions.end())
            return eve::Result<eve::weapon::ProjectilePoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "target unavailable"));
        return eve::Result<eve::weapon::ProjectilePoint>::success(found->second);
    }

    mutable bool                                          reject = false;
    std::map<std::uint32_t, eve::weapon::ProjectilePoint> positions;
};

class HitProbe final : public eve::weapon::ICarrierHitProbe {
public:
    [[nodiscard]] eve::Result<std::vector<Contact>> query(eve::weapon::CarrierHandle, const eve::weapon::CarrierMotion&,
                                                          const eve::weapon::CarrierMotion& current) const override {
        ++queries;
        if (reject)
            return eve::Result<std::vector<Contact>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "hit probe failed"));
        std::vector<Contact> contacts;
        for (const auto& planned : plannedHits) {
            if (std::fabs(current.position.x - planned.atX) < 1e-9) {
                Contact contact;
                contact.target = planned.target;
                contact.point  = current.position;
                contact.normal = planned.normal;
                contacts.push_back(contact);
            }
        }
        return eve::Result<std::vector<Contact>>::success(std::move(contacts));
    }

    struct PlannedHit {
        double                        atX = 0.0;
        ecs::EntityHandle             target{};
        eve::weapon::ProjectileVector normal{-1.0, 0.0, 0.0};
    };

    mutable int             queries = 0;
    mutable bool            reject  = false;
    std::vector<PlannedHit> plannedHits;
};

[[maybe_unused]] eve::weapon::CarrierRecipe linearShell(std::string_view recipeId, double damage) {
    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id(recipeId);
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::EmitHit, eve::weapon::CarrierTriggerKind::OnHit, damage});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});
    return recipe;
}

}  // namespace

TEST_CASE("combatCarrier.composesGravityAndIntegrateMotion") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:ballistic");
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::ApplyGravity, 10.0});
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    auto spawned = runtime.spawn(recipe, {{}, {0.0, 1.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.5));
    REQUIRE(frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    // v0=(0,10,0), after gravity vy=5, integrate y=2.5
    CHECK(std::fabs(state->motion.position.y - 2.5) < 1e-9);
}

TEST_CASE("combatCarrier.fuseSplashReleasesWithoutHitProbe") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:grenade");
    recipe.lifetime = seconds(5.0);
    recipe.speed    = 1.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    eve::weapon::CarrierTrigger fuse;
    fuse.kind = eve::weapon::CarrierTriggerKind::OnFuse;
    fuse.fuse = seconds(0.4);
    recipe.triggers.push_back(fuse);
    eve::weapon::CarrierImpact splash;
    splash.kind         = eve::weapon::CarrierImpactKind::Splash;
    splash.on           = eve::weapon::CarrierTriggerKind::OnFuse;
    splash.damage       = 40.0;
    splash.splashRadius = 3.0;
    splash.damageType   = "Combat.Damage.Explosive";
    recipe.impacts.push_back(splash);
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnFuse});

    auto spawned = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto before = runtime.update(seconds(0.3));
    REQUIRE(before.ok());
    CHECK(before.value().events.empty());
    CHECK(runtime.find(spawned.value()).has_value());

    auto boom = runtime.update(seconds(0.2));
    REQUIRE(boom.ok());
    REQUIRE(boom.value().events.size() == 1);
    CHECK(boom.value().events[0].impact == eve::weapon::CarrierImpactKind::Splash);
    CHECK(std::fabs(boom.value().events[0].splashRadius - 3.0) < 1e-9);
    CHECK(boom.value().released.size() == 1);
    CHECK(!runtime.find(spawned.value()).has_value());
}

TEST_CASE("combatCarrier.pierceSuppressesReleaseUntilBudgetExhausted") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe = linearShell("carrier:piercing", 10.0);
    eve::weapon::CarrierImpact pierce;
    pierce.kind        = eve::weapon::CarrierImpactKind::Pierce;
    pierce.on          = eve::weapon::CarrierTriggerKind::OnHit;
    pierce.pierceCount = 2;
    // Insert pierce before Release so order is EmitHit, Pierce, Release.
    recipe.impacts.insert(recipe.impacts.begin() + 1, pierce);

    auto spawned = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());

    HitProbe          hits;
    ecs::EntityHandle targetA;
    targetA.id         = 1;
    targetA.generation = 1;
    ecs::EntityHandle targetB;
    targetB.id         = 2;
    targetB.generation = 1;
    ecs::EntityHandle targetC;
    targetC.id         = 3;
    targetC.generation = 1;
    hits.plannedHits.push_back({5.0, targetA});
    hits.plannedHits.push_back({10.0, targetB});
    hits.plannedHits.push_back({15.0, targetC});

    auto first = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(first.ok());
    REQUIRE(first.value().events.size() == 1);
    CHECK(first.value().released.empty());
    CHECK(runtime.find(spawned.value()).has_value());
    CHECK(runtime.find(spawned.value())->pierceRemaining == 1);

    auto second = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(second.ok());
    REQUIRE(second.value().events.size() == 1);
    CHECK(second.value().released.empty());
    CHECK(runtime.find(spawned.value())->pierceRemaining == 0);

    auto third = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(third.ok());
    REQUIRE(third.value().events.size() == 1);
    CHECK(third.value().released.size() == 1);
    CHECK(!runtime.find(spawned.value()).has_value());
}

TEST_CASE("combatCarrier.bounceReflectsVelocityAndKeepsCarrierAlive") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe = linearShell("carrier:bounce", 5.0);
    eve::weapon::CarrierImpact bounce;
    bounce.kind        = eve::weapon::CarrierImpactKind::Bounce;
    bounce.on          = eve::weapon::CarrierTriggerKind::OnHit;
    bounce.bounceCount = 1;
    bounce.restitution = 1.0;
    recipe.impacts.insert(recipe.impacts.begin() + 1, bounce);

    auto spawned = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());

    HitProbe          hits;
    ecs::EntityHandle wall;
    wall.id         = 9;
    wall.generation = 1;
    hits.plannedHits.push_back({5.0, wall, {-1.0, 0.0, 0.0}});

    auto frame = runtime.update(seconds(0.5), nullptr, &hits);
    REQUIRE(frame.ok());
    CHECK(frame.value().released.empty());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(state->motion.velocity.x < 0.0);
    CHECK(state->bounceRemaining == 0);
}

TEST_CASE("combatCarrier.spawnChildOnExpireUsesFreedSlot") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());

    eve::weapon::CarrierRecipe child = linearShell("carrier:shard", 3.0);
    // Child without OnHit so update needs no probe.
    child.triggers = {{eve::weapon::CarrierTriggerKind::OnExpire}};
    child.impacts  = {{eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire}};
    REQUIRE(runtime.registerRecipe(child).ok());

    eve::weapon::CarrierRecipe parent;
    parent.id       = id("carrier:cluster");
    parent.lifetime = seconds(0.5);
    parent.speed    = 4.0;
    parent.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    parent.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    eve::weapon::CarrierImpact spawnChild;
    spawnChild.kind          = eve::weapon::CarrierImpactKind::SpawnChild;
    spawnChild.on            = eve::weapon::CarrierTriggerKind::OnExpire;
    spawnChild.childRecipeId = child.id;
    parent.impacts.push_back(spawnChild);
    parent.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    auto spawned = runtime.spawn(parent, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    CHECK(runtime.activeCount() == 1);

    auto frame = runtime.update(seconds(0.5));
    REQUIRE(frame.ok());
    CHECK(frame.value().released.size() == 1);
    CHECK(frame.value().childSpawns.size() == 1);
    CHECK(runtime.activeCount() == 1);
    auto states = runtime.states();
    REQUIRE(states.size() == 1);
    CHECK(states[0].recipeId == child.id);
}

TEST_CASE("combatCarrier.hitProbeFailureRollsBackWholeFrame") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());
    auto spawned = runtime.spawn(linearShell("carrier:rollback", 1.0), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());

    HitProbe hits;
    hits.reject = true;
    auto frame  = runtime.update(seconds(0.5), nullptr, &hits);
    CHECK(!frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(std::fabs(state->motion.position.x) < 1e-9);
}

TEST_CASE("combatCarrier.bridgeFromProjectileDefinitionPreservesLinearMotion") {
    eve::weapon::ProjectileDefinition definition;
    definition.id       = id("projectile:bridge");
    definition.mode     = eve::weapon::ProjectileMode::Linear;
    definition.speed    = 10.0;
    definition.lifetime = seconds(2.0);
    auto recipe         = eve::weapon::carrierRecipeFromProjectile(definition, 12.0);
    REQUIRE(recipe.ok());

    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());
    auto spawned = runtime.spawn(recipe.value(), {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.5), nullptr, nullptr);
    // Recipe includes OnHit — missing probe must fail observably.
    CHECK(!frame.ok());

    // Rebuild without OnHit for motion-only check via custom recipe from bridge fields.
    eve::weapon::CarrierRecipe motionOnly = recipe.value();
    motionOnly.triggers                   = {{eve::weapon::CarrierTriggerKind::OnExpire}};
    motionOnly.impacts = {{eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire}};
    eve::weapon::CombatCarrierRuntime runtime2;
    REQUIRE(runtime2.configurePool(2).ok());
    auto spawned2 = runtime2.spawn(motionOnly, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned2.ok());
    auto ok = runtime2.update(seconds(0.5));
    REQUIRE(ok.ok());
    auto state = runtime2.find(spawned2.value());
    REQUIRE(state.has_value());
    CHECK(std::fabs(state->motion.position.x - 5.0) < 1e-9);
}

class BodyDefenseProvider final : public eve::weapon::ICarrierBodyDefenseProvider {
public:
    [[nodiscard]] eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>> sample(
        eve::weapon::CarrierHandle, ecs::EntityHandle, const eve::weapon::CarrierMotion&) const override {
        return eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>>::success(bodies);
    }

    std::vector<eve::weapon::CarrierBodyDefenseSample> bodies;
};

TEST_CASE("combatCarrier.homingAvoidsFrontBodyDefenseLaterally") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:smart-missile");
    recipe.lifetime = seconds(5.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::SteerHoming, 0.0, 360.0});
    eve::weapon::CarrierMotionOp avoid;
    avoid.kind               = eve::weapon::CarrierMotionOpKind::SteerAvoidBody;
    avoid.maxTurnRateDegrees = 360.0;
    avoid.avoidLookAhead     = 8.0;
    avoid.avoidStrength      = 4.0;
    avoid.avoidRadiusPadding = 0.5;
    recipe.motionOps.push_back(avoid);
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    ecs::EntityHandle target;
    target.id         = 7;
    target.generation = 1;
    TargetProvider targets;
    targets.positions[target.id] = {20.0, 0.0, 0.0};

    BodyDefenseProvider                   bodies;
    eve::weapon::CarrierBodyDefenseSample shield;
    shield.center           = {5.0, 0.0, 0.0};
    shield.radius           = 2.0;
    shield.facing           = {-1.0, 0.0, 0.0};  // faces the incoming carrier
    shield.frontConeDegrees = 90.0;
    bodies.bodies.push_back(shield);

    auto spawned = runtime.spawn(recipe, {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, target});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.2), &targets, nullptr, &bodies);
    REQUIRE(frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    // Avoidance should introduce a lateral (Z) component instead of flying straight into the shield.
    CHECK(std::fabs(state->motion.velocity.z) > 1e-3);
}

TEST_CASE("combatCarrier.spawnVolleyFanFiresDistinctDirections") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(8).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:pellet");
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    eve::weapon::CarrierVolleySpec volley;
    volley.count         = 3;
    volley.pattern       = eve::weapon::CarrierVolleyPattern::Fan;
    volley.spreadDegrees = 90.0;

    auto handles = runtime.spawnVolley(recipe, {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {}}, volley);
    REQUIRE(handles.ok());
    REQUIRE(handles.value().size() == 3);
    CHECK(runtime.activeCount() == 3);

    REQUIRE(runtime.update(seconds(0.5)).ok());
    auto states = runtime.states();
    REQUIRE(states.size() == 3);
    // Outer pellets should have diverged in Z after flying forward.
    double minZ = states[0].motion.position.z;
    double maxZ = states[0].motion.position.z;
    for (const auto& state : states) {
        minZ = std::min(minZ, state.motion.position.z);
        maxZ = std::max(maxZ, state.motion.position.z);
    }
    CHECK(maxZ - minZ > 1.0);
}

TEST_CASE("combatCarrier.curveSwayAddsLateralOscillation") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());

    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id("carrier:wavy");
    recipe.lifetime = seconds(3.0);
    recipe.speed    = 10.0;
    eve::weapon::CarrierMotionOp sway;
    sway.kind             = eve::weapon::CarrierMotionOpKind::CurveSway;
    sway.curveAmplitude   = 1.0;
    sway.curveFrequencyHz = 1.0;
    recipe.motionOps.push_back(sway);
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});

    auto spawned = runtime.spawn(recipe, {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto frame = runtime.update(seconds(0.25));
    REQUIRE(frame.ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(state->motion.position.x > 0.0);
    CHECK(std::fabs(state->motion.position.z) > 1e-6);
}

TEST_CASE("combatCarrier.snapshotRestoreRoundTrip") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(4).ok());
    eve::weapon::CarrierRecipe recipe = linearShell("carrier:snap", 1.0);
    recipe.triggers                   = {{eve::weapon::CarrierTriggerKind::OnExpire}};
    recipe.impacts = {{eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire}};
    auto spawned   = runtime.spawn(recipe, {{1.0, 2.0, 3.0}, {0.0, 1.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    REQUIRE(runtime.update(seconds(0.25)).ok());

    auto                              snap = runtime.snapshot();
    eve::weapon::CombatCarrierRuntime restored;
    REQUIRE(restored.configurePool(4).ok());
    REQUIRE(restored.registerRecipe(recipe).ok());
    REQUIRE(restored.restore(snap).ok());
    auto state = restored.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK(std::fabs(state->motion.position.y - 4.5) < 1e-9);
}

// combat_carrier_script.cpp
}  // namespace combat_carrier
}  // namespace

// combat_melee_hit.cpp
namespace {
namespace combat_melee_hit {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

class FixedPose final : public eve::combat::IMeleePoseSource {
public:
    eve::combat::MeleePose                            attackerPose;
    [[nodiscard]] eve::Result<eve::combat::MeleePose> pose(eve::SubjectRef, std::string_view) const override {
        return eve::Result<eve::combat::MeleePose>::success(attackerPose);
    }
};

}  // namespace

TEST_CASE("meleeHit.sweepsArmedHitboxAgainstHurtboxAndDedups") {
    const auto attacker = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto victim   = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    FixedPose  poses;
    poses.attackerPose = {{0.0, 1.0, 0.0}, 0.0};

    eve::combat::MeleeHitRuntime melee;
    melee.setPoseSource(poses);
    REQUIRE(melee
                .registerHitbox({"weapon.main",
                                 {eve::combat::MeleeShapeKind::Sphere, 0.4, 0.0},
                                 {0.6, 0.0, 0.0},
                                 15.0,
                                 8.0,
                                 "Damage.Physical.Slash"})
                .ok());
    REQUIRE(melee.registerHurtbox({victim, "torso", "torso", {eve::combat::MeleeShapeKind::Capsule, 0.35, 0.45}}).ok());
    REQUIRE(melee.setHurtboxPose(victim, "torso", {{1.0, 1.0, 0.0}, 0.0}).ok());

    const eve::action::ActionExecutionId execution(42);
    REQUIRE(melee.armHitbox(attacker, "weapon.main", execution).ok());
    auto first = melee.advance(eve::SimulationTick(1));
    REQUIRE(first.ok());
    REQUIRE_EQ(first.value().hits.size(), 1u);
    CHECK_EQ(first.value().hits.front().bodyPart, std::string("torso"));
    CHECK_EQ(first.value().hits.front().healthDamage, 15.0);

    auto second = melee.advance(eve::SimulationTick(2));
    REQUIRE(second.ok());
    CHECK_EQ(second.value().hits.size(), 0u);

    REQUIRE(melee.disarmHitbox(attacker, "weapon.main", execution).ok());
    poses.attackerPose.position.x = 10.0;
    REQUIRE(melee.armHitbox(attacker, "weapon.main", eve::action::ActionExecutionId(43)).ok());
    auto miss = melee.advance(eve::SimulationTick(3));
    REQUIRE(miss.ok());
    CHECK_EQ(miss.value().hits.size(), 0u);

    std::map<std::string, eve::combat::CombatState, std::less<>> states;
    states.emplace(victim.format(), eve::combat::CombatState{victim, 100.0, 100.0, 40.0, 40.0});
    eve::combat::DamageRuntime damage;
    auto                       outcomes = melee.applyHits(damage, states, first.value().hits, {{"torso", 1.5}});
    REQUIRE(outcomes.ok());
    CHECK_EQ(states.at(victim.format()).health, 77.5);
}
}  // namespace combat_melee_hit
}  // namespace
}  // namespace test_combat_hit_resolution
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_motion_warp)
#include "PrivateTestScope.h"
#include <limits>
#include "combat/CombatMotionWarp.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// combat_motion_warp.cpp
namespace test_combat_motion_warp_unit {
TEST_CASE("combatMotionWarp.pullsTowardStandoffAndRejectsOverBudget") {
    eve::combat::CombatWarpRequest request;
    request.attacker        = {0.0, 0.0, 0.0};
    request.target          = {4.0, 0.0, 0.0};
    request.attackerFacing  = {0.0, 0.0, 1.0};
    request.desiredDistance = 1.4;
    request.maxTranslation  = 0.12;
    request.remainingBudget = 0.6;
    auto pulled             = eve::combat::CombatMotionWarp::solve(request);
    REQUIRE(pulled.ok());
    CHECK(pulled.value().translation.x > 0.0);
    CHECK_EQ(pulled.value().translation.x, 0.12);
    CHECK_EQ(pulled.value().translation.z, 0.0);
    CHECK(pulled.value().facing.x > 0.9);

    request.attacker        = {2.6, 0.0, 0.0};
    request.desiredDistance = 1.4;
    auto already            = eve::combat::CombatMotionWarp::solve(request);
    REQUIRE(already.ok());
    CHECK_EQ(already.value().translation.x, 0.0);
    CHECK_EQ(already.status().code(), eve::StatusCode::NoOp);

    request.attacker        = {0.0, 0.0, 0.0};
    request.remainingBudget = 0.05;
    auto over               = eve::combat::CombatMotionWarp::solve(request);
    CHECK(!over.ok());
    CHECK_EQ(over.status().code(), eve::StatusCode::Conflict);

    request.remainingBudget = 0.6;
    request.attacker        = {0.0, 0.0, 0.0};
    request.target          = {0.5, 0.0, 0.0};
    request.desiredDistance = 1.4;
    auto pushBack           = eve::combat::CombatMotionWarp::solve(request);
    REQUIRE(pushBack.ok());
    CHECK(pushBack.value().translation.x < 0.0);
}

TEST_CASE("combatMotionWarp.rejectsNonFiniteInput") {
    eve::combat::CombatWarpRequest request;
    request.attacker = {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0};
    request.target   = {1.0, 0.0, 0.0};
    CHECK(!eve::combat::CombatMotionWarp::solve(request).ok());
}
}  // namespace test_combat_motion_warp_unit
#endif
