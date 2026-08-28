#include "action/AbilityController.h"
#include "combat/CombatAttributes.h"
#include "combat/Damage.h"
#include "combat/StandardAbilities.h"
#include "effects/EffectContainer.h"
#include "tags/GameplayTag.h"
#include "weapon/ProjectileRuntime.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

}  // namespace

TEST_CASE("combatFramework.composesTagsPlayerAbilityBuffDamageAttributesAndProjectile") {
    eve::tags::GameplayTagRegistry tags;
    REQUIRE(tags.registerTag("Ability.Combat.Attack.Light").ok());
    REQUIRE(tags.registerTag("Effect.Buff.Attack").ok());
    REQUIRE(tags.registerTag("Attribute.Vital.Stamina").ok());

    auto catalog = eve::combat::standardCombatAbilities();
    REQUIRE(catalog.ok());
    const auto light = std::find_if(catalog.value().begin(), catalog.value().end(), [](const auto& definition) {
        return definition.id.format() == "combat-ability:light-attack";
    });
    REQUIRE(light != catalog.value().end());

    eve::action::ActionRuntime            actions;
    eve::action::AbilityRuntime           abilities(actions);
    eve::action::AbilityControllerRuntime controller(abilities);
    REQUIRE(abilities.registerDefinition(*light).ok());
    auto grant = abilities.grant("fighter:player", light->id);
    REQUIRE(grant.ok());
    eve::action::PlayerAbilityIntentQueue player;
    eve::action::AbilityIntent             intent;
    intent.grantId         = grant.value();
    intent.request.actionId = light->action.id;
    REQUIRE(player.enqueue(std::move(intent)).ok());
    auto activated = controller.processNext(player, eve::SimulationTick(10));
    REQUIRE(activated.ok());
    REQUIRE(activated.value().has_value());

    eve::effects::EffectContainer effects;
    auto buff = effects.apply("fighter:player", "buff.attack", "combat-demo", 1, 5.0, "attack",
                              eve::effects::StackPolicy::Refresh);
    REQUIRE(buff.ok());
    REQUIRE(effects.find(buff.value()) != nullptr);
    REQUIRE(effects.find(buff.value())->addTag("Effect.Buff.Attack").ok());

    eve::combat::CombatAttributeRuntime attributes("fighter:player");
    REQUIRE(attributes.registerAttribute({"Attribute.Vital.Stamina", 100.0, 0.0, 100.0, 10.0}).ok());
    auto spent = attributes.modify("Attribute.Vital.Stamina", -20.0);
    REQUIRE(spent.ok());
    REQUIRE_EQ(spent.value().size(), 1u);

    eve::combat::CombatState target{subject("01020304-0506-0708-890a-0b0c0d0e0f10"), 100.0, 100.0, 40.0, 40.0};
    eve::combat::DamageRequest damageRequest;
    damageRequest.source          = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    damageRequest.target          = target.subject;
    damageRequest.actionExecution = activated.value()->executionId;
    damageRequest.damageType      = "Damage.Physical.Slash";
    damageRequest.healthDamage    = 12.0;
    damageRequest.poiseDamage     = 8.0;
    eve::combat::DamageRuntime damage;
    auto                       outcome = damage.apply(target, damageRequest);
    REQUIRE(outcome.ok());
    CHECK_EQ(target.health, 88.0);

    eve::weapon::ProjectileDefinition projectile;
    auto projectileId = eve::LogicalId::fromParts("combat-projectile", "light-attack-tracer");
    REQUIRE(projectileId.has_value());
    projectile.id       = *projectileId;
    projectile.mode     = eve::weapon::ProjectileMode::Linear;
    projectile.speed    = 20.0;
    projectile.lifetime = eve::Duration::fromNanoseconds(1000000000);
    eve::weapon::ProjectileRuntime projectiles;
    auto spawned = projectiles.spawn(projectile, {{}, {1.0, 0.0, 0.0}, {}});
    REQUIRE(spawned.ok());
    REQUIRE(projectiles.update(eve::Duration::fromNanoseconds(100000000)).ok());
    REQUIRE(projectiles.find(spawned.value()).has_value());
    CHECK_EQ(projectiles.find(spawned.value())->position.x, 2.0);

    REQUIRE(attributes.advance(eve::Duration::fromNanoseconds(1000000000)).ok());
    CHECK_EQ(attributes.value("Attribute.Vital.Stamina").value(), 90.0);
    REQUIRE(effects.advance({eve::SimulationTick(11), eve::Duration::fromNanoseconds(1000000000)}).ok());
}
