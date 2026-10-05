#include "combat/CombatCamera.h"
#include "combat/CombatCharacterPose.h"
#include "combat/CombatLoop.h"
#include "combat/CombatTarget.h"
#include "combat/HitFeel.h"
#include "combat/MeleeHit.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <map>
#include <string>

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatLoop.requiresCharacterAndMelee") {
    eve::combat::CombatLoopRuntime loop;
    auto missing = loop.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)});
    CHECK(!missing.ok());
}

TEST_CASE("combatLoop.warpsAttackerTowardLockAndFramesCamera") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.registerSubject({enemy, "fighter:enemy", {4.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.beginAttack(player).ok());

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(player).ok());
    REQUIRE(targets.hardLock(player, enemy).ok());

    eve::combat::MeleeHitRuntime   melee;
    eve::combat::CombatLoopRuntime loop;
    loop.setCharacters(characters);
    loop.setMelee(melee);
    loop.setTargets(&targets);
    loop.setCameraFocus(player);

    auto frame = loop.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)});
    REQUIRE(frame.ok());
    auto state = characters.state(player);
    REQUIRE(state.ok());
    CHECK(std::abs(state.value().position.x - 0.12) < 1e-9);
    CHECK(state.value().facing.x > 0.9);
    CHECK(std::abs(frame.value().camera.eye.x + 5.88) < 1e-6);
    CHECK(frame.value().camera.lookAt.x > 1.0);
}

TEST_CASE("combatLoop.appliesMeleeDamageFeelAndHitstopFreeze") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy  = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {0.0, 0.0, 0.0}}).ok());
    REQUIRE(characters.registerSubject({enemy, "fighter:enemy", {1.4, 0.0, 0.0}}).ok());

    eve::combat::CombatCharacterPoseSource poses(characters);
    eve::combat::MeleeHitRuntime           melee;
    melee.setPoseSource(poses);
    REQUIRE(melee
                .registerHitbox({"weapon.main",
                                 {eve::combat::MeleeShapeKind::Sphere, 0.5, 0.0},
                                 {0.0, 0.0, 0.6},
                                 12.0,
                                 4.0,
                                 "Damage.Physical.Slash"})
                .ok());
    REQUIRE(melee.registerHurtbox({enemy, "torso", "torso", {eve::combat::MeleeShapeKind::Sphere, 0.4, 0.0}}).ok());
    REQUIRE(melee.armHitbox(player, "weapon.main", eve::action::ActionExecutionId(9)).ok());

    std::map<std::string, eve::combat::CombatState, std::less<>> states;
    states.emplace(enemy.format(), eve::combat::CombatState{enemy, 100.0, 100.0, 40.0, 40.0});
    eve::combat::HitFeelRuntime feel;

    eve::combat::CombatLoopRuntime loop;
    loop.setCharacters(characters);
    loop.setMelee(melee);
    loop.setFeel(&feel);
    loop.setDamageStates(&states);
    loop.setCameraFocus(player);

    auto frame = loop.advance({eve::SimulationTick(1), eve::Duration::fromNanoseconds(16000000)});
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().melee.hits.size(), 1u);
    REQUIRE_EQ(frame.value().outcomes.size(), 1u);
    CHECK_EQ(states.at(enemy.format()).health, 88.0);
    CHECK(feel.isFrozen(player));

    REQUIRE(characters.setMoveIntent(player, {1.0, 0.0, 0.0}, 1.0).ok());
    const double frozenX = characters.state(player).value().position.x;
    REQUIRE(loop.advance({eve::SimulationTick(2), eve::Duration::fromNanoseconds(16000000)}).ok());
    CHECK_EQ(characters.state(player).value().position.x, frozenX);
}

TEST_CASE("combatCharacterPoseSource.samplesChestHeightAndYaw") {
    const auto                          player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    eve::combat::CombatCharacterRuntime characters;
    REQUIRE(characters.registerSubject({player, "fighter:player", {2.0, 0.5, -1.0}}).ok());
    REQUIRE(characters.setFacing(player, {0.0, 0.0, 1.0}).ok());
    eve::combat::CombatCharacterPoseSource poses(characters, 1.25);
    auto                                   pose = poses.pose(player, "weapon.main");
    REQUIRE(pose.ok());
    CHECK_EQ(pose.value().position.x, 2.0);
    CHECK_EQ(pose.value().position.y, 1.75);
    CHECK_EQ(pose.value().position.z, -1.0);
    CHECK(std::abs(pose.value().yawRadians) < 1e-9);
}
