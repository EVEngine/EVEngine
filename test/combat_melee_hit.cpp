#include "combat/MeleeHit.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

class FixedPose final : public eve::combat::IMeleePoseSource {
public:
    eve::combat::MeleePose attackerPose;
    [[nodiscard]] eve::Result<eve::combat::MeleePose> pose(eve::SubjectRef, std::string_view) const override {
        return eve::Result<eve::combat::MeleePose>::success(attackerPose);
    }
};

}  // namespace

TEST_CASE("meleeHit.sweepsArmedHitboxAgainstHurtboxAndDedups") {
    const auto attacker = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto victim = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    FixedPose poses;
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
    REQUIRE(melee
                .registerHurtbox({victim, "torso", "torso", {eve::combat::MeleeShapeKind::Capsule, 0.35, 0.45}})
                .ok());
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
    auto outcomes = melee.applyHits(damage, states, first.value().hits, {{"torso", 1.5}});
    REQUIRE(outcomes.ok());
    CHECK_EQ(states.at(victim.format()).health, 77.5);
}
