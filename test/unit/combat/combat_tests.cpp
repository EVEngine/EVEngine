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

#include "zeroerr/unittest.h"

#include <cstddef>

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_camera_framing)
#include "combat/CombatCamera.h"

#include <cmath>

namespace test_combat_camera_contract {

TEST_CASE("combat.CombatCameraFraming.solve: target lock facing and invalid distance have explicit outcomes") {
    eve::combat::CombatCameraFramingRequest request;
    request.player     = {0.0, 0.0, 0.0};
    request.distance   = 6.0;
    request.height     = 2.0;
    request.lookHeight = 1.2;

    SUB_CASE("locked target sets the midpoint look point and places the eye behind the player") {
        request.playerFacing = {1.0, 0.0, 0.0};
        request.lockTarget   = eve::combat::CombatVector3{4.0, 0.0, 0.0};
        auto view            = eve::combat::CombatCameraFraming::solve(request);
        REQUIRE(view.ok());
        CHECK_LT(std::fabs(view.value().lookAt.x - 2.0), 1e-9);
        CHECK_LT(std::fabs(view.value().lookAt.y - 1.2), 1e-9);
        CHECK_LT(std::fabs(view.value().eye.x + 6.0), 1e-9);
        CHECK_LT(std::fabs(view.value().eye.y - 2.0), 1e-9);
    };

    SUB_CASE("without a lock the eye follows the player's facing axis") {
        request.playerFacing = {0.0, 0.0, 1.0};
        auto view            = eve::combat::CombatCameraFraming::solve(request);
        REQUIRE(view.ok());
        CHECK_LT(std::fabs(view.value().lookAt.z), 1e-9);
        CHECK_LT(std::fabs(view.value().eye.z + 6.0), 1e-9);
    };

    SUB_CASE("zero distance fails instead of producing a degenerate camera") {
        request.distance = 0.0;
        CHECK(!eve::combat::CombatCameraFraming::solve(request).ok());
    };
}
}  // namespace test_combat_camera_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_carrier)
#include "weapon/CombatCarrier.h"

namespace test_combat_carrier_contract {
eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::Duration seconds(double value) {
    auto parsed = eve::Duration::fromSeconds(value);
    REQUIRE(parsed.ok());
    return parsed.value();
}

eve::weapon::CarrierRecipe linearRecipe(std::string_view name) {
    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id(name);
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});
    return recipe;
}

class FailingProbe final : public eve::weapon::ICarrierHitProbe {
public:
    [[nodiscard]] eve::Result<std::vector<Contact>> query(eve::weapon::CarrierHandle, const eve::weapon::CarrierMotion&,
                                                          const eve::weapon::CarrierMotion&) const override {
        return eve::Result<std::vector<Contact>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected probe failure"));
    }
};

TEST_CASE("combat.CombatCarrierRuntime update: linear motion and expiry release") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());
    auto recipe  = linearRecipe("carrier:linear");
    auto spawned = runtime.spawn(recipe, {{1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto moving = runtime.update(seconds(0.5));
    REQUIRE(moving.ok());
    REQUIRE(runtime.find(spawned.value()).has_value());
    CHECK_EQ(runtime.find(spawned.value())->motion.position.x, 6.0);
    auto expired = runtime.update(seconds(1.5));
    REQUIRE(expired.ok());
    CHECK_EQ(expired.value().released.size(), 1u);
    CHECK(!runtime.find(spawned.value()).has_value());
}

TEST_CASE("combat.CombatCarrierRuntime triggers: fuse emits splash before lifetime expires") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());
    auto recipe     = linearRecipe("carrier:fuse");
    recipe.lifetime = seconds(5.0);
    recipe.triggers = {{eve::weapon::CarrierTriggerKind::OnFuse, seconds(0.4)}};
    eve::weapon::CarrierImpact splash;
    splash.kind         = eve::weapon::CarrierImpactKind::Splash;
    splash.on           = eve::weapon::CarrierTriggerKind::OnFuse;
    splash.damage       = 40.0;
    splash.splashRadius = 3.0;
    recipe.impacts      = {splash, {eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnFuse}};
    auto spawned        = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    auto before = runtime.update(seconds(0.3));
    REQUIRE(before.ok());
    CHECK(before.value().events.empty());
    auto after = runtime.update(seconds(0.2));
    REQUIRE(after.ok());
    REQUIRE_EQ(after.value().events.size(), 1u);
    CHECK_EQ(after.value().events[0].impact, eve::weapon::CarrierImpactKind::Splash);
    CHECK_EQ(after.value().events[0].splashRadius, 3.0);
    CHECK(!runtime.find(spawned.value()).has_value());
}

TEST_CASE("combat.CombatCarrierRuntime update: probe failure leaves the previous frame intact") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());
    auto recipe = linearRecipe("carrier:probe");
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnHit});
    auto spawned = runtime.spawn(recipe, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    FailingProbe probe;
    CHECK(!runtime.update(seconds(0.5), nullptr, &probe).ok());
    auto state = runtime.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK_EQ(state->motion.position.x, 0.0);
}

TEST_CASE("combat.CombatCarrierRuntime snapshot: restore preserves handle and integrated position") {
    eve::weapon::CombatCarrierRuntime runtime;
    REQUIRE(runtime.configurePool(2).ok());
    auto recipe  = linearRecipe("carrier:snapshot");
    auto spawned = runtime.spawn(recipe, {{1.0, 2.0, 3.0}, {0.0, 1.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    REQUIRE(runtime.update(seconds(0.25)).ok());
    eve::weapon::CombatCarrierRuntime restored;
    REQUIRE(restored.configurePool(2).ok());
    REQUIRE(restored.registerRecipe(recipe).ok());
    REQUIRE(restored.restore(runtime.snapshot()).ok());
    auto state = restored.find(spawned.value());
    REQUIRE(state.has_value());
    CHECK_EQ(state->motion.position.y, 4.5);
}
}  // namespace test_combat_carrier_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_melee_hit)
#include <map>
#include "combat/MeleeHit.h"

namespace test_combat_melee_hit_contract {
class FixedPose final : public eve::combat::IMeleePoseSource {
public:
    eve::combat::MeleePose                            attackerPose{{0.0, 1.0, 0.0}, 0.0};
    [[nodiscard]] eve::Result<eve::combat::MeleePose> pose(eve::SubjectRef, std::string_view) const override {
        return eve::Result<eve::combat::MeleePose>::success(attackerPose);
    }
};

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

TEST_CASE("combat.MeleeHitRuntime advance: one contact per action execution and body part damage") {
    const auto                   attacker = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto                   victim   = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    FixedPose                    poses;
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
    auto repeated = melee.advance(eve::SimulationTick(2));
    REQUIRE(repeated.ok());
    CHECK(repeated.value().hits.empty());
    std::map<std::string, eve::combat::CombatState, std::less<>> states;
    states.emplace(victim.format(), eve::combat::CombatState{victim, 100.0, 100.0, 40.0, 40.0});
    eve::combat::DamageRuntime damage;
    REQUIRE(melee.applyHits(damage, states, first.value().hits, {{"torso", 1.5}}).ok());
    CHECK_EQ(states.at(victim.format()).health, 77.5);
}
}  // namespace test_combat_melee_hit_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_motion_warp)
#include <limits>
#include "combat/CombatMotionWarp.h"

namespace test_combat_motion_warp_contract {
TEST_CASE("combat.CombatMotionWarp solve: constrained standoff movement and invalid requests") {
    eve::combat::CombatWarpRequest request;
    request.attacker        = {0.0, 0.0, 0.0};
    request.target          = {4.0, 0.0, 0.0};
    request.attackerFacing  = {0.0, 0.0, 1.0};
    request.desiredDistance = 1.4;
    request.maxTranslation  = 0.12;
    request.remainingBudget = 0.6;
    SUB_CASE("farther than standoff: translation is capped per step") {
        auto result = eve::combat::CombatMotionWarp::solve(request);
        REQUIRE(result.ok());
        CHECK_EQ(result.value().translation.x, 0.12);
        CHECK_EQ(result.value().translation.z, 0.0);
        CHECK(result.value().facing.x > 0.9);
    };
    SUB_CASE("at standoff: no movement") {
        request.attacker = {2.6, 0.0, 0.0};
        auto result      = eve::combat::CombatMotionWarp::solve(request);
        REQUIRE(result.ok());
        CHECK_EQ(result.status().code(), eve::StatusCode::NoOp);
        CHECK_EQ(result.value().translation.x, 0.0);
    };
    SUB_CASE("budget below step: conflict") {
        request.remainingBudget = 0.05;
        auto result             = eve::combat::CombatMotionWarp::solve(request);
        CHECK(!result.ok());
        CHECK_EQ(result.status().code(), eve::StatusCode::Conflict);
    };
    SUB_CASE("nonfinite position: rejection") {
        request.attacker.x = std::numeric_limits<double>::quiet_NaN();
        CHECK(!eve::combat::CombatMotionWarp::solve(request).ok());
    };
}
}  // namespace test_combat_motion_warp_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_attributes)
#include "combat/CombatAttributes.h"

namespace test_combat_attributes_contract {

eve::combat::CombatAttributeDefinition health() { return {"Attribute.Vital.Health", 75.0, 0.0, 100.0, 5.0}; }

TEST_CASE("combat.CombatAttributeRuntime registerAttribute and value: tags own bounded current values") {
    eve::combat::CombatAttributeRuntime runtime("fighter");
    REQUIRE(runtime.registerAttribute(health()).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 75.0);
    CHECK(!runtime.registerAttribute(health()).ok());
    CHECK(!runtime.value("Attribute.Vital.Mana").ok());
}

TEST_CASE("combat.CombatAttributeRuntime modify advance: deltas emit events and clamp at zero or maximum") {
    eve::combat::CombatAttributeRuntime runtime("fighter");
    REQUIRE(runtime.registerAttribute(health()).ok());
    auto loss = runtime.modify("Attribute.Vital.Health", -25.0);
    REQUIRE(loss.ok());
    REQUIRE_EQ(loss.value().size(), std::size_t{1});
    CHECK(loss.value()[0].kind == eve::combat::CombatAttributeEventKind::Loss);
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 50.0);

    auto depletion = runtime.modify("Attribute.Vital.Health", -1000.0);
    REQUIRE(depletion.ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 0.0);
    REQUIRE_EQ(depletion.value().size(), std::size_t{2});
    CHECK(depletion.value()[1].kind == eve::combat::CombatAttributeEventKind::Depleted);

    auto restored = runtime.setValue("Attribute.Vital.Health", 75.0);
    REQUIRE(restored.ok());
    REQUIRE_EQ(restored.value().size(), std::size_t{1});
    CHECK(restored.value()[0].kind == eve::combat::CombatAttributeEventKind::Gain);
    auto regenerated = runtime.advance(eve::Duration::fromNanoseconds(1000000000));
    REQUIRE(regenerated.ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 80.0);
    REQUIRE(runtime.advance(eve::Duration::fromNanoseconds(10000000000)).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 100.0);
}
}  // namespace test_combat_attributes_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_character)
#include "combat/CombatCharacter.h"

namespace test_combat_character_contract {

eve::SubjectRef fighter() {
    auto parsed = eve::PersistentId::parse("11121314-1516-1718-991a-1b1c1d1e1f20");
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

class PlaneGround final : public eve::combat::ICombatGroundProvider {
public:
    [[nodiscard]] eve::Result<double> sampleHeight(double, double) const override {
        return eve::Result<double>::success(2.0);
    }
};

TEST_CASE("combat.CombatCharacterRuntime: injected time advances jumps dodges and attack root motion") {
    const auto id = fighter();

    SUB_CASE("jump raises the actor and dodge grants temporary invulnerability") {
        eve::combat::CombatCharacterRuntime runtime;
        REQUIRE(
            runtime.registerSubject({id, "fighter:player", {0.0, 0.0, 0.0}, 5.0, 40.0, 6.0, 20.0, 10.0, 0.2, 1}).ok());
        REQUIRE(runtime.jump(id).ok());
        REQUIRE(runtime.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(50000000)}).ok());
        auto airborne = runtime.state(id);
        REQUIRE(airborne.ok());
        CHECK_GT(airborne.value().position.y, 0.0);
        CHECK(airborne.value().mode == eve::combat::CombatCharacterMode::Airborne);
        REQUIRE(runtime.dodge(id, eve::combat::CombatVector3{0.0, 0.0, 1.0}).ok());
        auto dodging = runtime.state(id);
        REQUIRE(dodging.ok());
        CHECK(dodging.value().invulnerable);
        CHECK(dodging.value().mode == eve::combat::CombatCharacterMode::Dodging);
    };

    SUB_CASE("freeze prevents integration and unfreeze restores movement") {
        eve::combat::CombatCharacterRuntime runtime;
        REQUIRE(runtime.registerSubject({id, "fighter:player", {0.0, 0.0, 0.0}}).ok());
        REQUIRE(runtime.setMoveIntent(id, {1.0, 0.0, 0.0}, 1.0).ok());
        REQUIRE(runtime.setTimeFrozen(id, true).ok());
        REQUIRE(runtime.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(50000000)}).ok());
        CHECK_EQ(runtime.state(id).value().position.x, 0.0);
        REQUIRE(runtime.setTimeFrozen(id, false).ok());
        REQUIRE(runtime.advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(50000000)}).ok());
        CHECK_GT(runtime.state(id).value().position.x, 0.0);
    };

    SUB_CASE("attack root motion applies an exact authored displacement") {
        eve::combat::CombatCharacterRuntime runtime;
        REQUIRE(runtime.registerSubject({id, "fighter:player", {0.0, 0.0, 0.0}}).ok());
        REQUIRE(runtime.beginAttack(id).ok());
        REQUIRE(runtime.setRootMotionDelta(id, {0.5, 0.0, 0.0}).ok());
        REQUIRE(runtime.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)}).ok());
        CHECK_EQ(runtime.state(id).value().position.x, 0.5);
        REQUIRE(runtime.endAttack(id).ok());
    };

    SUB_CASE("ground provider keeps the actor on its surface") {
        eve::combat::CombatCharacterRuntime runtime;
        REQUIRE(runtime.registerSubject({id, "fighter:player", {0.0, 2.0, 0.0}}).ok());
        PlaneGround ground;
        runtime.setGroundProvider(ground);
        REQUIRE(runtime.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(50000000)}).ok());
        CHECK_EQ(runtime.state(id).value().position.y, 2.0);
        CHECK(runtime.state(id).value().mode == eve::combat::CombatCharacterMode::Grounded);
    };
}
}  // namespace test_combat_character_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_combat_locomotion)
#include "combat/CombatLocomotion.h"

namespace test_combat_locomotion_contract {

eve::SubjectRef fighter() {
    auto parsed = eve::PersistentId::parse("01020304-0506-0708-890a-0b0c0d0e0f10");
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

TEST_CASE("combat.CombatLocomotionRuntime advance: intent is normalized and integrated using injected dt") {
    const auto                           id = fighter();
    eve::combat::CombatLocomotionRuntime runtime;
    REQUIRE(runtime.registerSubject({id, "fighter:player", {}, 4.0, 8.0}).ok());
    REQUIRE(runtime.setMoveIntent(id, {3.0, 4.0}, 0.5).ok());
    auto advanced = runtime.advance({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500000000)});
    REQUIRE(advanced.ok());
    auto state = runtime.state(id);
    REQUIRE(state.ok());
    CHECK_EQ(state.value().velocity.x, 1.2);
    CHECK_EQ(state.value().velocity.z, 1.6);
    CHECK_EQ(state.value().position.x, 0.6);
    CHECK_EQ(state.value().position.z, 0.8);
    CHECK(!runtime.advance({eve::SimulationTick::zero(), eve::Duration::fromNanoseconds(16000000)}).ok());
}

TEST_CASE("combat.CombatLocomotionRuntime navigation: arrival does not overshoot and provider failure is atomic") {
    SUB_CASE("direct navigation reaches the target and clears its goal") {
        const auto                                  id = fighter();
        eve::combat::DirectCombatNavigationProvider provider;
        eve::combat::CombatLocomotionRuntime        runtime(provider);
        REQUIRE(runtime.registerSubject({id, "fighter:bot", {}, 10.0, 100.0}).ok());
        REQUIRE(runtime.navigateTo(id, {{1.0, 0.0}, 0.05}).ok());
        auto advanced = runtime.advance({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500000000)});
        REQUIRE(advanced.ok());
        auto state = runtime.state(id);
        REQUIRE(state.ok());
        CHECK_EQ(state.value().position.x, 1.0);
        CHECK_EQ(state.value().velocity.x, 0.0);
        CHECK(!state.value().navigationGoal.has_value());
    };

    SUB_CASE("provider failure does not publish a partial frame for another actor") {
        const auto first  = fighter();
        auto       parsed = eve::PersistentId::parse("31323334-3536-3738-b93a-3b3c3d3e3f40");
        REQUIRE(parsed.has_value());
        const auto                           second = eve::SubjectRef::fromPersistentId(*parsed);
        FailingNavigation                    provider;
        eve::combat::CombatLocomotionRuntime runtime(provider);
        REQUIRE(runtime.registerSubject({first, "fighter:first", {}, 2.0, 10.0}).ok());
        REQUIRE(runtime.registerSubject({second, "fighter:second", {5.0, 0.0}, 2.0, 10.0}).ok());
        REQUIRE(runtime.setMoveIntent(first, {1.0, 0.0}, 1.0).ok());
        REQUIRE(runtime.navigateTo(second, {{0.0, 0.0}, 0.1}).ok());
        CHECK(!runtime.advance({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500000000)}).ok());
        CHECK_EQ(runtime.state(first).value().position.x, 0.0);
        CHECK_EQ(runtime.state(second).value().position.x, 5.0);
    };
}
}  // namespace test_combat_locomotion_contract
#endif
