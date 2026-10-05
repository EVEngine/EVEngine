#include "combat/CombatCharacter.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
