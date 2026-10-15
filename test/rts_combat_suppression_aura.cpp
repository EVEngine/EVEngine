#include "RtsCompositionFixtures.h"

TEST_CASE("rts.combat.suppressionAuraUsesLiveProviderState") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            blue     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000081"));
    auto*            red      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000082"));
    auto*            attacker = Unit::createUnit(subject("00000000-0000-7000-8000-000000000083"));
    auto*            target   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000084"));
    auto*            leader   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000085"));
    attacker->faction()->link = eve::rts::FactionLink::bind(ecs::handle_of(blue)).takeValue();
    target->faction()->link   = eve::rts::FactionLink::bind(ecs::handle_of(red)).takeValue();
    leader->faction()->link   = eve::rts::FactionLink::bind(ecs::handle_of(red)).takeValue();
    target->motion()->x = leader->motion()->x = 5.f;
    attacker->combat()->target                = ecs::handle_of(target);
    attacker->combat()->acquisitionRange      = 10.f;
    attacker->combat()->suppressionPerShot    = 3.f;
    target->morale()->capacity                = 20.f;
    target->durability()->state.health        = 100.0;
    target->durability()->state.maxHealth     = 100.0;
    leader->morale()->auraRange               = 4.f;
    leader->morale()->auraSuppressionFactor   = 0.5f;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                   = "aura-test-rifle";
    definition.kind                 = eve::weapon::WeaponKind::Ranged;
    definition.logic                = "rts-hitscan";
    definition.damage               = 1.f;
    definition.range                = 8.f;
    definition.cooldown             = 0.1f;
    definition.magSize              = 10;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 10.f;
    weapon->state()->resource.max   = 10.f;
    attacker->weapon()->link        = eve::rts::WeaponLink::bind(ecs::handle_of(weapon)).takeValue();

    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    const auto                        dt = eve::Duration::fromSeconds(0.2).expect("aura test dt");
    auto fired = eve::rts::CombatFireSystem::step({eve::SimulationTick{1}, dt}, state, sensing, damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 1u);
    CHECK(std::abs(target->morale()->suppression - 1.5f) < 1e-5f);

    leader->durability()->alive = false;
    fired = eve::rts::CombatFireSystem::step({eve::SimulationTick{2}, dt}, state, sensing, damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 1u);
    CHECK(std::abs(target->morale()->suppression - 4.5f) < 1e-5f);

    weapon->release();
    attacker->release();
    target->release();
    leader->release();
    blue->release();
    red->release();
}
