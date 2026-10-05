#include "combat/BodyPartDamage.h"
#include "combat/CombatEnemyAI.h"
#include "combat/CombatTarget.h"
#include "combat/StandardAbilities.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatTargetEnemyAiAndBodyPartDamage") {
    const auto player = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    const auto enemy = subject("01020304-0506-0708-890a-0b0c0d0e0f10");

    eve::combat::CombatTargetRuntime targets;
    REQUIRE(targets.registerOwner(player).ok());
    REQUIRE(targets.registerOwner(enemy).ok());
    auto soft = targets.softLock(player, {{enemy, 2.0, 1.0, 0.0, 0.0}});
    REQUIRE(soft.ok());
    REQUIRE(soft.value().target.has_value());
    CHECK_EQ(soft.value().target->format(), enemy.format());
    REQUIRE(targets.hardLock(enemy, player).ok());

    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    const auto light = std::find_if(catalog.value().begin(), catalog.value().end(), [](const auto& definition) {
        return definition.id.format() == "combat-ability:light-attack";
    });
    REQUIRE(light != catalog.value().end());
    eve::action::ActionRuntime actions;
    eve::action::AbilityRuntime abilities(actions);
    REQUIRE(abilities.registerDefinition(*light).ok());
    auto grant = abilities.grant("fighter:enemy", light->id);
    REQUIRE(grant.ok());

    eve::combat::CombatEnemyIntentSource ai;
    ai.setTargetRuntime(targets);
    REQUIRE(ai.registerEnemy({enemy, "fighter:enemy", grant.value(), 2.0, 5.0, 10}).ok());
    REQUIRE(ai.setLightAction(enemy, light->action.id).ok());
    REQUIRE(ai.setPosition(enemy, 0.0, 0.0, 0.0).ok());
    REQUIRE(ai.setPosition(player, 1.0, 0.0, 0.0).ok());
    CHECK(ai.band(enemy) == eve::combat::CombatEnemyBand::Near);
    auto intent = ai.nextIntent(eve::SimulationTick(1));
    REQUIRE(intent.ok());
    REQUIRE(intent.value().has_value());
    CHECK_EQ(intent.value()->grantId, grant.value());

    eve::combat::BodyPartDamageRule parts;
    REQUIRE(parts.setMultiplier("head", 2.0).ok());
    eve::combat::CombatState victim{player, 100.0, 100.0, 20.0, 20.0};
    eve::combat::DamageRequest request;
    request.source = enemy;
    request.target = player;
    request.damageType = "Damage.Physical.Slash";
    request.healthDamage = 10.0;
    auto amounts = parts.evaluateForPart(request, victim, "head");
    REQUIRE(amounts.ok());
    CHECK_EQ(amounts.value().healthDamage, 20.0);
}
