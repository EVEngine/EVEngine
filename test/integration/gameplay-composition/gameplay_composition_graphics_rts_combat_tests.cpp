// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "rts",
  "origins": [
    "rts_combat.cpp",
    "rts_combat_3.cpp",
    "rts_composition.cpp",
    "rts_economy.cpp",
    "rts_economy_2.cpp"
  ]
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: rts combat
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include "RtsCompositionFixtures.h"
// rts_combat.cpp
namespace test_rts_combat {
// rts_combat.cpp
TEST_CASE("rts.autoCombatUsesCanonicalSensingWeaponAndDamage") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000031"));
    Faction*         red      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000032"));
    Unit*            attacker = Unit::createUnit(subject("00000000-0000-7000-8000-000000000033"));
    Unit*            target   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000034"));
    auto             blueLink = eve::rts::FactionLink::bind(ecs::handle_of(blue));
    auto             redLink  = eve::rts::FactionLink::bind(ecs::handle_of(red));
    REQUIRE(blueLink.ok());
    REQUIRE(redLink.ok());
    attacker->faction()->link                  = std::move(blueLink).takeValue();
    target->faction()->link                    = std::move(redLink).takeValue();
    attacker->motion()->x                      = 0.0f;
    target->motion()->x                        = 5.0f;
    attacker->durability()->state.health       = 20.0;
    attacker->durability()->state.maxHealth    = 20.0;
    target->durability()->state.health         = 20.0;
    target->durability()->state.maxHealth      = 20.0;
    target->shield()->capacity                 = 4.0f;
    target->shield()->value                    = 4.0f;
    target->shield()->regenDelay               = 1.0f;
    attacker->veterancy()->veteranThreshold    = 20.0f;
    attacker->veterancy()->eliteThreshold      = 40.0f;
    attacker->veterancy()->veteranDamageFactor = 1.25f;
    attacker->veterancy()->eliteDamageFactor   = 1.5f;
    attacker->veterancy()->veteranHealthFactor = 1.2f;
    attacker->veterancy()->eliteHealthFactor   = 1.5f;
    attacker->combat()->acquisitionRange       = 10.0f;
    attacker->combat()->leashRange             = 12.0f;
    attacker->combat()->suppressionPerShot     = 3.0f;
    target->morale()->capacity                 = 10.0f;

    eve::weapon::WeaponEntity*    weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                   = "rts-rifle";
    definition.kind                 = eve::weapon::WeaponKind::Ranged;
    definition.logic                = "rts-hitscan";
    definition.damage               = 6.0f;
    definition.damageType           = "damage.physical";
    definition.range                = 8.0f;
    definition.cooldown             = 0.1f;
    definition.magSize              = 10;
    definition.reserveSize          = 10;
    definition.reloadTime           = 1.0f;
    definition.blockedByObstacles   = true;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 10.0f;
    weapon->state()->resource.max   = 10.0f;
    auto weaponLink                 = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    attacker->weapon()->link = std::move(weaponLink).takeValue();

    eve::sensing::SensingWorld             sensing;
    eve::combat::DamageRuntime             damage;
    eve::rts::CombatFireSystem::State      combatState;
    const eve::SimulationStep              obstructedStep{eve::SimulationTick{1},
                                             eve::Duration::fromSeconds(0.2).expect("obstructed combat dt")};
    std::vector<eve::rts::CombatFireEvent> blockedEvents;
    auto blockedLine = [&](eve::rts::WorldPosition origin, eve::rts::WorldPosition destination,
                           ecs::EntityHandle source, ecs::EntityHandle targetHandle,
                           const eve::weapon::WeaponDefinition& queriedWeapon) {
        CHECK(std::abs(origin.x) < 1e-5f);
        CHECK(std::abs(destination.x - 5.0f) < 1e-5f);
        CHECK(ecs::try_get(source) == attacker);
        CHECK(ecs::try_get(targetHandle) == target);
        CHECK_EQ(queriedWeapon.id, "rts-rifle");
        return eve::Result<bool>::success(false);
    };
    auto blockedSink = [&](const eve::rts::CombatFireEvent& event, eve::SimulationTick tick) {
        CHECK_EQ(tick.value(), 1u);
        blockedEvents.push_back(event);
    };
    auto obstructed = eve::rts::CombatFireSystem::step(obstructedStep, combatState, sensing, damage, nullptr,
                                                       blockedLine, nullptr, {}, {}, {}, blockedSink);
    REQUIRE(obstructed.ok());
    CHECK_EQ(obstructed.value(), 0u);
    CHECK(std::abs(target->durability()->state.health - 20.0) < 1e-5);
    CHECK(std::abs(weapon->state()->resource.value - 10.0f) < 1e-5f);
    REQUIRE_EQ(blockedEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(blockedEvents[0].kind), static_cast<int>(eve::rts::CombatFireEventKind::FireBlocked));
    CHECK(blockedEvents[0].source == attacker->identity()->subject);
    CHECK(blockedEvents[0].target == target->identity()->subject);
    auto stillObstructed = eve::rts::CombatFireSystem::step(obstructedStep, combatState, sensing, damage, nullptr,
                                                            blockedLine, nullptr, {}, {}, {}, blockedSink);
    REQUIRE(stillObstructed.ok());
    CHECK_EQ(blockedEvents.size(), 1u);

    auto inaccurate                 = definition;
    inaccurate.accuracy             = 0.0f;
    inaccurate.scatterRadius        = 3.0f;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(inaccurate);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->resource.value = 1.0f;
    std::vector<eve::rts::CombatFireEvent> fireEvents;
    auto                                   missed = eve::rts::CombatFireSystem::step(
        {eve::SimulationTick{2}, eve::Duration::fromSeconds(0.2).expect("miss dt")}, combatState, sensing, damage,
        nullptr, {}, nullptr, {}, {}, {}, [&](const eve::rts::CombatFireEvent& event, eve::SimulationTick tick) {
            CHECK_EQ(tick.value(), 2u);
            fireEvents.push_back(event);
        });
    REQUIRE(missed.ok());
    CHECK_EQ(missed.value(), 1u);
    REQUIRE_EQ(fireEvents.size(), 3u);
    CHECK_EQ(static_cast<int>(fireEvents[0].kind), static_cast<int>(eve::rts::CombatFireEventKind::WeaponFired));
    CHECK_EQ(static_cast<int>(fireEvents[1].kind), static_cast<int>(eve::rts::CombatFireEventKind::ShotMissed));
    CHECK_EQ(static_cast<int>(fireEvents[2].kind), static_cast<int>(eve::rts::CombatFireEventKind::WeaponDry));
    CHECK(fireEvents[0].source == attacker->identity()->subject);
    CHECK(fireEvents[0].target == target->identity()->subject);
    CHECK(std::abs(fireEvents[0].point.x - fireEvents[1].point.x) < 1e-5f);
    CHECK(std::abs(fireEvents[0].point.y - fireEvents[1].point.y) < 1e-5f);
    CHECK(std::abs(target->durability()->state.health - 20.0) < 1e-5);
    CHECK_EQ(attacker->combat()->shotSequence, 1u);
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();

    weapon->state()->resource.value   = 0.0f;
    weapon->state()->resource.reserve = 10;
    weapon->state()->cooldown         = 0.1f;
    std::vector<eve::rts::CombatFireEvent> reloadEvents;
    auto                                   reloading = eve::rts::CombatFireSystem::step(
        {eve::SimulationTick{3}, eve::Duration::fromSeconds(0.2).expect("reload dt")}, combatState, sensing, damage,
        nullptr, {}, nullptr, {}, {}, {}, [&](const eve::rts::CombatFireEvent& event, eve::SimulationTick tick) {
            CHECK_EQ(tick.value(), 3u);
            reloadEvents.push_back(event);
        });
    REQUIRE(reloading.ok());
    REQUIRE_EQ(reloadEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(reloadEvents[0].kind), static_cast<int>(eve::rts::CombatFireEventKind::ReloadStarted));
    attacker->combat()->stance = eve::rts::CombatStance::Passive;
    attacker->combat()->target = {};
    reloadEvents.clear();
    auto reloaded = eve::rts::CombatFireSystem::step(
        {eve::SimulationTick{4}, eve::Duration::fromSeconds(0.8).expect("reload completion dt")}, combatState, sensing,
        damage, nullptr, {}, nullptr, {}, {}, {},
        [&](const eve::rts::CombatFireEvent& event, eve::SimulationTick tick) {
            CHECK_EQ(tick.value(), 4u);
            reloadEvents.push_back(event);
        });
    REQUIRE(reloaded.ok());
    REQUIRE_EQ(reloadEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(reloadEvents[0].kind), static_cast<int>(eve::rts::CombatFireEventKind::ReloadCompleted));
    attacker->combat()->stance               = eve::rts::CombatStance::Defensive;
    weapon->state()->resource.reloading      = false;
    weapon->state()->resource.reloadProgress = 0.0f;
    weapon->state()->resource.value          = 10.0f;
    weapon->state()->cooldown                = 0.0f;

    for (std::uint64_t tick = 3; tick <= 6; ++tick) {
        const eve::SimulationStep step{eve::SimulationTick{tick}, eve::Duration::fromSeconds(0.2).expect("combat dt")};
        auto                      fired = eve::rts::CombatFireSystem::step(step, combatState, sensing, damage);
        REQUIRE(fired.ok());
        CHECK_EQ(fired.value(), 1u);
    }
    CHECK(!target->durability()->alive);
    CHECK(std::abs(target->durability()->state.health) < 1e-5);
    CHECK(std::abs(target->shield()->value) < 1e-5f);
    CHECK_EQ(attacker->veterancy()->level, 1);
    CHECK(std::abs(attacker->veterancy()->experience - 20.0f) < 1e-5f);
    CHECK(std::abs(attacker->combat()->upgradeDamageFactor - 1.25f) < 1e-5f);
    CHECK(std::abs(attacker->durability()->state.maxHealth - 24.0) < 1e-5);
    CHECK(target->morale()->active);
    CHECK(std::abs(target->morale()->suppression - 10.0f) < 1e-5f);
    CHECK(ecs::try_get(attacker->combat()->target) == nullptr);

    weapon->release();
    attacker->release();
    target->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.shieldsRegenerateOnlyAfterDamageCooldown") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            unit               = Unit::createUnit();
    unit->durability()->state.health    = 10.0;
    unit->durability()->state.maxHealth = 10.0;
    unit->shield()->capacity            = 10.0f;
    unit->shield()->value               = 2.0f;
    unit->shield()->regenRate           = 3.0f;
    unit->shield()->regenDelay          = 2.0f;
    unit->shield()->cooldown            = 2.0f;
    auto first =
        eve::rts::ShieldSystem::step({eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("shield dt")});
    REQUIRE(first.ok());
    CHECK(std::abs(unit->shield()->value - 2.0f) < 1e-5f);
    auto second =
        eve::rts::ShieldSystem::step({eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("shield dt")});
    REQUIRE(second.ok());
    CHECK(std::abs(unit->shield()->value - 5.0f) < 1e-5f);
    std::vector<eve::rts::LifecycleEvent> shieldEvents;
    auto                                  full =
        eve::rts::ShieldSystem::step({eve::SimulationTick{3}, eve::Duration::fromSeconds(2.0).expect("shield full dt")},
                                     [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick tick) {
                                         CHECK_EQ(tick.value(), 3u);
                                         shieldEvents.push_back(event);
                                     });
    REQUIRE(full.ok());
    CHECK_EQ(unit->shield()->value, 10.0f);
    REQUIRE_EQ(shieldEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(shieldEvents[0].kind), static_cast<int>(eve::rts::LifecycleEventKind::ShieldRecharged));
    unit->release();
}

TEST_CASE("rts.moraleAuraAcceleratesSuppressionRecovery") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction           = Faction::createFaction();
    Unit*            suppressed        = Unit::createUnit();
    Unit*            leader            = Unit::createUnit();
    auto             suppressedFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto             leaderFaction     = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(suppressedFaction.ok());
    REQUIRE(leaderFaction.ok());
    suppressed->faction()->link         = std::move(suppressedFaction).takeValue();
    leader->faction()->link             = std::move(leaderFaction).takeValue();
    suppressed->morale()->capacity      = 10.0f;
    suppressed->morale()->suppression   = 6.0f;
    suppressed->morale()->recoveryRate  = 1.0f;
    suppressed->morale()->active        = true;
    leader->morale()->auraRange         = 4.0f;
    leader->morale()->auraRecoveryBonus = 2.0f;

    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("morale dt")};
    std::vector<eve::rts::LifecycleEvent> moraleEvents;
    auto                                  recovered =
        eve::rts::MoraleSystem::step(step, [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick tick) {
            CHECK_EQ(tick.value(), 1u);
            moraleEvents.push_back(event);
        });
    REQUIRE(recovered.ok());
    CHECK(std::abs(suppressed->morale()->suppression - 3.0f) < 1e-5f);
    CHECK(!suppressed->morale()->active);
    REQUIRE_EQ(moraleEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(moraleEvents[0].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::SuppressionRecovered));

    suppressed->release();
    leader->release();
    faction->release();
}

TEST_CASE("rts.attackGroundFiresThroughCanonicalWeaponRuntime") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000041"));
    Unit*            artillery   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000042"));
    auto             factionLink = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(factionLink.ok());
    artillery->faction()->link = std::move(factionLink).takeValue();
    artillery->motion()->x     = 0.0f;

    eve::weapon::WeaponEntity*    weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                   = "rts-howitzer";
    definition.kind                 = eve::weapon::WeaponKind::Ranged;
    definition.logic                = "rts-indirect";
    definition.range                = 20.0f;
    definition.cooldown             = 0.1f;
    definition.magSize              = 4;
    definition.projectile.speed     = 12.0f;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 4.0f;
    weapon->state()->resource.max   = 4.0f;
    auto weaponLink                 = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    artillery->weapon()->link = std::move(weaponLink).takeValue();

    CommandSpec attackGround;
    attackGround.kind   = OrderKind::AttackGround;
    attackGround.target = {10.0f, 2.0f};
    auto queued         = artillery->orders()->values.enqueue(attackGround);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();
    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.2).expect("artillery dt")};
    auto                      fired = eve::rts::CombatFireSystem::step(step, state, sensing, damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 1u);
    CHECK(artillery->orders()->values.empty());
    CHECK(std::abs(weapon->state()->resource.value - 3.0f) < 1e-5f);

    weapon->release();
    artillery->release();
    faction->release();
}

TEST_CASE("rts.armedBuildingAcquiresAndFiresAtEnemyUnit") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000051"));
    Faction*         red      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000052"));
    Building*        turret   = Building::createBuilding(subject("00000000-0000-7000-8000-000000000053"));
    Unit*            target   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000054"));
    auto             blueLink = eve::rts::FactionLink::bind(ecs::handle_of(blue));
    auto             redLink  = eve::rts::FactionLink::bind(ecs::handle_of(red));
    REQUIRE(blueLink.ok());
    REQUIRE(redLink.ok());
    turret->faction()->link               = std::move(blueLink).takeValue();
    target->faction()->link               = std::move(redLink).takeValue();
    turret->placement()->placed           = true;
    target->motion()->x                   = 4.0f;
    target->durability()->state.health    = 12.0;
    target->durability()->state.maxHealth = 12.0;
    turret->combat()->acquisitionRange    = 8.0f;

    eve::weapon::WeaponEntity*    weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                   = "rts-turret";
    definition.kind                 = eve::weapon::WeaponKind::Ranged;
    definition.logic                = "rts-hitscan";
    definition.damage               = 12.0f;
    definition.range                = 8.0f;
    definition.magSize              = 2;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 2.0f;
    weapon->state()->resource.max   = 2.0f;
    auto weaponLink                 = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    turret->weapon()->link = std::move(weaponLink).takeValue();

    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    const eve::SimulationStep         step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.2).expect("turret dt")};
    auto                              fired = eve::rts::CombatFireSystem::step(step, state, sensing, damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 1u);
    CHECK(!target->durability()->alive);

    weapon->release();
    turret->release();
    target->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.escortAndCombatGroupCoordinateTargets") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue            = Faction::createFaction();
    Faction*         red             = Faction::createFaction();
    auto             protectedHandle = ecs::handle_of(Unit::createUnit());
    auto             escortHandle    = ecs::handle_of(Unit::createUnit());
    auto             shooterAHandle  = ecs::handle_of(Unit::createUnit());
    auto             shooterBHandle  = ecs::handle_of(Unit::createUnit());
    auto             enemyAHandle    = ecs::handle_of(Unit::createUnit());
    auto             enemyBHandle    = ecs::handle_of(Unit::createUnit());
    auto*            protectedUnit   = dynamic_cast<Unit*>(ecs::try_get(protectedHandle));
    auto*            escort          = dynamic_cast<Unit*>(ecs::try_get(escortHandle));
    auto*            shooterA        = dynamic_cast<Unit*>(ecs::try_get(shooterAHandle));
    auto*            shooterB        = dynamic_cast<Unit*>(ecs::try_get(shooterBHandle));
    auto*            enemyA          = dynamic_cast<Unit*>(ecs::try_get(enemyAHandle));
    auto*            enemyB          = dynamic_cast<Unit*>(ecs::try_get(enemyBHandle));
    auto             bindFaction     = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bindFaction(*protectedUnit, *blue);
    bindFaction(*escort, *blue);
    bindFaction(*shooterA, *blue);
    bindFaction(*shooterB, *blue);
    bindFaction(*enemyA, *red);
    bindFaction(*enemyB, *red);
    protectedUnit->motion()->x         = 10.0f;
    escort->tactics()->escortOffsetX   = -2.0f;
    escort->tactics()->protectionRange = 5.0f;
    enemyA->motion()->x                = 12.0f;
    enemyA->durability()->state.health = 5.0;
    enemyB->motion()->x                = 6.0f;
    enemyB->durability()->state.health = 5.0;
    CommandSpec escortOrder;
    escortOrder.kind         = OrderKind::Escort;
    escortOrder.targetEntity = ecs::handle_of(protectedUnit);
    auto queued              = escort->orders()->values.enqueue(escortOrder);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();

    eve::weapon::WeaponEntity*    weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "group-rifle";
    definition.damage           = 10.0f;
    definition.range            = 20.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    auto weaponA                = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    auto weaponB                = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponA.ok());
    REQUIRE(weaponB.ok());
    shooterA->weapon()->link             = std::move(weaponA).takeValue();
    shooterB->weapon()->link             = std::move(weaponB).takeValue();
    shooterA->tactics()->combatGroup     = 7;
    shooterB->tactics()->combatGroup     = 7;
    shooterA->combat()->acquisitionRange = 20.0f;
    shooterB->combat()->acquisitionRange = 20.0f;

    auto coordinated = eve::rts::TacticsSystem::step();
    REQUIRE(coordinated.ok());
    CHECK(std::abs(escort->tactics()->guardX - 8.0f) < 1e-5f);
    CHECK(ecs::try_get(escort->combat()->target) == enemyA);
    CHECK(ecs::try_get(shooterA->combat()->target) != nullptr);
    CHECK(ecs::try_get(shooterB->combat()->target) != nullptr);
    CHECK(ecs::try_get(shooterA->combat()->target) != ecs::try_get(shooterB->combat()->target));

    weapon->release();
    protectedUnit->release();
    escort->release();
    shooterA->release();
    shooterB->release();
    enemyA->release();
    enemyB->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.escortGroupPrioritizesDirectThreatAndDistributesInterceptors") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue          = Faction::createFaction();
    Faction*         red           = Faction::createFaction();
    Unit*            protectedUnit = Unit::createUnit(subject("00000000-0000-7000-8000-000000000301"));
    Unit*            first         = Unit::createUnit(subject("00000000-0000-7000-8000-000000000302"));
    Unit*            second        = Unit::createUnit(subject("00000000-0000-7000-8000-000000000303"));
    Unit*            nearby        = Unit::createUnit(subject("00000000-0000-7000-8000-000000000304"));
    Unit*            attacker      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000305"));
    auto             bind          = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*protectedUnit, *blue);
    bind(*first, *blue);
    bind(*second, *blue);
    bind(*nearby, *red);
    bind(*attacker, *red);
    nearby->motion()->x        = 1.0f;
    attacker->motion()->x      = 5.0f;
    attacker->combat()->target = ecs::handle_of(protectedUnit);
    for (Unit* escort : {first, second}) {
        escort->tactics()->protectionRange = 8.0f;
        CommandSpec order;
        order.kind         = OrderKind::Escort;
        order.targetEntity = ecs::handle_of(protectedUnit);
        REQUIRE(escort->orders()->values.enqueue(order).ok());
    }
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK(ecs::try_get(first->combat()->target) == attacker);
    CHECK(ecs::try_get(second->combat()->target) == nearby);

    protectedUnit->release();
    first->release();
    second->release();
    nearby->release();
    attacker->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.escortProtectsWholeSupplyConvoyAndRotatesScreen") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction();
    Faction*         red      = Faction::createFaction();
    Unit*            leader   = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003b1"));
    Unit*            tail     = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003b2"));
    Unit*            escort   = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003b3"));
    Unit*            attacker = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003b4"));
    auto             bind     = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*leader, *blue);
    bind(*tail, *blue);
    bind(*escort, *blue);
    bind(*attacker, *red);
    leader->motion()->x            = 0.0f;
    tail->motion()->x              = 10.0f;
    leader->supply()->convoyLeader = leader->identity()->self;
    tail->supply()->convoyLeader   = leader->identity()->self;
    CommandSpec supplyMission;
    supplyMission.kind         = OrderKind::SupplyRelay;
    supplyMission.target       = {20.0f, 0.0f};
    supplyMission.targetEntity = tail->identity()->self;
    REQUIRE(leader->orders()->values.replace(supplyMission).ok());
    escort->tactics()->escortOffsetX   = -2.0f;
    escort->tactics()->protectionRange = 3.0f;
    CommandSpec escortOrder;
    escortOrder.kind         = OrderKind::Escort;
    escortOrder.targetEntity = leader->identity()->self;
    REQUIRE(escort->orders()->values.replace(escortOrder).ok());
    attacker->motion()->x      = 11.0f;
    attacker->combat()->target = tail->identity()->self;

    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK(std::abs(escort->combat()->guardX - 5.0f) < 1e-5f);
    CHECK(std::abs(escort->tactics()->guardX - 3.0f) < 1e-5f);
    CHECK(escort->combat()->leashRange >= 7.9f);
    CHECK_EQ(ecs::try_get(escort->combat()->target), attacker);

    attacker->release();
    escort->release();
    tail->release();
    leader->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.convoyEscortMatchesThreatSectorsAndReinforcesGap") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue        = Faction::createFaction();
    Faction*         red         = Faction::createFaction();
    Unit*            leader      = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003d1"));
    Unit*            tail        = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003d2"));
    Unit*            vanguard    = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003d3"));
    Unit*            rearguard   = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003d4"));
    Unit*            frontThreat = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003d5"));
    Unit*            rearThreat  = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003d6"));
    auto             bind        = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    for (Unit* unit : {leader, tail, vanguard, rearguard}) bind(*unit, *blue);
    bind(*frontThreat, *red);
    bind(*rearThreat, *red);
    leader->motion()->x            = 4.0f;
    tail->motion()->x              = 0.0f;
    leader->supply()->convoyLeader = leader->identity()->self;
    tail->supply()->convoyLeader   = leader->identity()->self;
    CommandSpec mission;
    mission.kind         = OrderKind::SupplyRelay;
    mission.targetEntity = tail->identity()->self;
    mission.target       = {20.0f, 0.0f};
    REQUIRE(leader->orders()->values.replace(mission).ok());
    vanguard->tactics()->escortOffsetX  = 3.0f;
    rearguard->tactics()->escortOffsetX = -3.0f;
    for (Unit* escort : {vanguard, rearguard}) {
        escort->tactics()->protectionRange = 12.0f;
        CommandSpec order;
        order.kind         = OrderKind::Escort;
        order.targetEntity = leader->identity()->self;
        REQUIRE(escort->orders()->values.replace(order).ok());
    }
    frontThreat->motion()->x = 9.0f;
    rearThreat->motion()->x  = -5.0f;

    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK_EQ(ecs::try_get(vanguard->combat()->target), frontThreat);
    CHECK_EQ(ecs::try_get(rearguard->combat()->target), rearThreat);
    CHECK(vanguard->tactics()->escortSectorMatched);
    CHECK(rearguard->tactics()->escortSectorMatched);

    rearThreat->durability()->alive = false;
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK_EQ(ecs::try_get(rearguard->combat()->target), frontThreat);
    CHECK(rearguard->tactics()->escortReinforcing);
    CHECK_EQ(rearguard->tactics()->escortReinforcementSector, 2);

    rearThreat->release();
    frontThreat->release();
    rearguard->release();
    vanguard->release();
    tail->release();
    leader->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.escortsFormStableRearLineDuringSuppressionRetreat") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction       = Faction::createFaction();
    Unit*            protectedUnit = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003e1"));
    Unit*            first         = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003e2"));
    Unit*            second        = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003e3"));
    for (Unit* unit : {protectedUnit, first, second}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        unit->faction()->link = std::move(link).takeValue();
    }
    protectedUnit->motion()->x               = 10.0f;
    protectedUnit->morale()->retreating      = true;
    protectedUnit->navigation()->plannedGoal = {20.0f, 0.0f};
    for (Unit* escort : {first, second}) {
        escort->tactics()->escortOffsetX = -2.0f;
        CommandSpec order;
        order.kind         = OrderKind::Escort;
        order.targetEntity = protectedUnit->identity()->self;
        REQUIRE(escort->orders()->values.replace(order).ok());
    }

    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK(first->tactics()->escortRearGuard);
    CHECK(second->tactics()->escortRearGuard);
    CHECK(first->tactics()->guardX < protectedUnit->motion()->x);
    CHECK(second->tactics()->guardX < protectedUnit->motion()->x);
    CHECK_NE(first->tactics()->guardY, second->tactics()->guardY);

    second->release();
    first->release();
    protectedUnit->release();
    faction->release();
}

TEST_CASE("rts.retreatingCombatGroupAlternatesMovementAndCoverFire") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue   = Faction::createFaction(subject("00000000-0000-7000-8000-000000000325"));
    Faction*         red    = Faction::createFaction(subject("00000000-0000-7000-8000-000000000326"));
    Unit*            first  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000327"));
    Unit*            second = Unit::createUnit(subject("00000000-0000-7000-8000-000000000328"));
    Unit*            enemy  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000329"));
    auto             bind   = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*first, *blue);
    bind(*second, *blue);
    bind(*enemy, *red);
    first->motion()->y                   = -1.0f;
    second->motion()->y                  = 1.0f;
    enemy->motion()->x                   = -5.0f;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "retreat-rifle";
    definition.damage           = 1.0f;
    definition.range            = 20.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    for (Unit* unit : {first, second}) {
        auto weaponLink = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(weaponLink.ok());
        unit->weapon()->link             = std::move(weaponLink).takeValue();
        unit->combat()->acquisitionRange = 20.0f;
        unit->tactics()->combatGroup     = 17;
        unit->morale()->retreating       = true;
        unit->motion()->speed            = 2.0f;
        CommandSpec retreat;
        retreat.kind   = OrderKind::Move;
        retreat.target = {20.0f, unit->motion()->y};
        REQUIRE(unit->orders()->values.replace(retreat).ok());
    }
    const eve::SimulationStep firstPhase{eve::SimulationTick{1},
                                         eve::Duration::fromSeconds(0.1).expect("retreat cover first phase")};
    REQUIRE(eve::rts::TacticsSystem::step(nullptr, firstPhase).ok());
    CHECK_EQ(first->tactics()->retreatFireTeam, 0);
    CHECK_EQ(second->tactics()->retreatFireTeam, 1);
    CHECK(first->tactics()->retreatCovering);
    CHECK(!second->tactics()->retreatCovering);
    CHECK_EQ(ecs::try_get(first->combat()->target), enemy);
    CHECK_EQ(ecs::try_get(second->combat()->target), nullptr);
    const float firstX  = first->motion()->x;
    const float secondX = second->motion()->x;
    REQUIRE(eve::rts::MotionSystem::step(firstPhase).ok());
    CHECK_EQ(first->motion()->x, firstX);
    CHECK(second->motion()->x > secondX);

    const eve::SimulationStep rotate{eve::SimulationTick{2},
                                     eve::Duration::fromSeconds(1.4).expect("retreat cover rotation")};
    REQUIRE(eve::rts::TacticsSystem::step(nullptr, rotate).ok());
    CHECK(!first->tactics()->retreatCovering);
    CHECK(second->tactics()->retreatCovering);
    CHECK_EQ(ecs::try_get(first->combat()->target), nullptr);
    CHECK_EQ(ecs::try_get(second->combat()->target), enemy);
    const float firstBeforeRotate  = first->motion()->x;
    const float secondBeforeRotate = second->motion()->x;
    REQUIRE(eve::rts::MotionSystem::step(firstPhase).ok());
    CHECK(first->motion()->x > firstBeforeRotate);
    CHECK_EQ(second->motion()->x, secondBeforeRotate);

    weapon->release();
    first->release();
    second->release();
    enemy->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.combatGroupThreatSectorsPartitionTargetsAndFallBack") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue        = Faction::createFaction();
    Faction*         red         = Faction::createFaction();
    Unit*            left        = Unit::createUnit();
    Unit*            right       = Unit::createUnit();
    Unit*            leftTarget  = Unit::createUnit();
    Unit*            rightTarget = Unit::createUnit();
    auto             bind        = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*left, *blue);
    bind(*right, *blue);
    bind(*leftTarget, *red);
    bind(*rightTarget, *red);
    left->motion()->x                    = -1.0f;
    right->motion()->x                   = 1.0f;
    leftTarget->motion()->x              = -5.0f;
    rightTarget->motion()->x             = 5.0f;
    eve::weapon::WeaponEntity*    weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "sector-rifle";
    definition.damage           = 1.0f;
    definition.range            = 20.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    for (Unit* shooter : {left, right}) {
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        shooter->weapon()->link             = std::move(link).takeValue();
        shooter->combat()->acquisitionRange = 20.0f;
        shooter->tactics()->combatGroup     = 11;
    }
    left->tactics()->threatSector  = -1;
    right->tactics()->threatSector = 1;
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK(ecs::try_get(left->combat()->target) == leftTarget);
    CHECK(ecs::try_get(right->combat()->target) == rightTarget);

    leftTarget->durability()->alive = false;
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK(ecs::try_get(left->combat()->target) == rightTarget);
    weapon->release();
    left->release();
    right->release();
    leftTarget->release();
    rightTarget->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.combatFireControlMatchesDamageTypesToArmor") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000331"));
    Faction*         red       = Faction::createFaction(subject("00000000-0000-7000-8000-000000000332"));
    Unit*            hunter    = Unit::createUnit(subject("00000000-0000-7000-8000-000000000333"));
    Unit*            grenadier = Unit::createUnit(subject("00000000-0000-7000-8000-000000000334"));
    Unit*            armored   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000335"));
    Unit*            light     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000336"));
    auto             bind      = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*hunter, *blue);
    bind(*grenadier, *blue);
    bind(*armored, *red);
    bind(*light, *red);
    armored->motion()->x = light->motion()->x = 8.0f;
    armored->motion()->y                      = -1.0f;
    light->motion()->y                        = 1.0f;
    for (Unit* target : {armored, light})
        target->durability()->state.health = target->durability()->state.maxHealth = 100.0;

    std::vector<eve::weapon::WeaponEntity*> weapons;
    auto                                    equip = [&](Unit& shooter, const char* id, const char* damageType) {
        auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id         = id;
        definition.damage     = 10.0f;
        definition.damageType = damageType;
        definition.range      = 20.0f;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def = weapon->definition()->owned.get();
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        shooter.weapon()->link             = std::move(link).takeValue();
        shooter.combat()->acquisitionRange = 20.0f;
        shooter.tactics()->combatGroup     = 23;
        weapons.push_back(weapon);
    };
    equip(*hunter, "tank-hunter", "damage.piercing");
    equip(*grenadier, "grenadier", "damage.explosive");
    FireControlArmorRule rule;
    rule.armored = armored->identity()->subject;
    rule.light   = light->identity()->subject;
    eve::combat::DamageRuntime damage(&rule);

    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    CHECK_EQ(ecs::try_get(hunter->combat()->target), armored);
    CHECK_EQ(ecs::try_get(grenadier->combat()->target), light);
    CHECK(std::abs(hunter->tactics()->fireControlEffectiveness - 2.0f) < 1e-5f);
    CHECK(std::abs(grenadier->tactics()->fireControlEffectiveness - 2.0f) < 1e-5f);

    armored->durability()->alive = false;
    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    CHECK_EQ(ecs::try_get(hunter->combat()->target), light);
    CHECK(std::abs(hunter->tactics()->fireControlEffectiveness - 0.5f) < 1e-5f);

    for (auto* weapon : weapons) weapon->release();
    hunter->release();
    grenadier->release();
    armored->release();
    light->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.separateCombatGroupsShareAutomaticVolleyBudget") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue       = Faction::createFaction(subject("00000000-0000-7000-8000-000000000341"));
    Faction*         red        = Faction::createFaction(subject("00000000-0000-7000-8000-000000000342"));
    Unit*            first      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000343"));
    Unit*            second     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000344"));
    Unit*            nearTarget = Unit::createUnit(subject("00000000-0000-7000-8000-000000000345"));
    Unit*            farTarget  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000346"));
    auto             bind       = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*first, *blue);
    bind(*second, *blue);
    bind(*nearTarget, *red);
    bind(*farTarget, *red);
    nearTarget->motion()->x = 5.0f;
    farTarget->motion()->x  = 7.0f;
    for (Unit* target : {nearTarget, farTarget})
        target->durability()->state.health = target->durability()->state.maxHealth = 10.0;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "shared-budget-cannon";
    definition.damage           = 10.0f;
    definition.range            = 20.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    std::uint64_t group         = 1;
    for (Unit* shooter : {first, second}) {
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        shooter->weapon()->link             = std::move(link).takeValue();
        shooter->combat()->acquisitionRange = 20.0f;
        shooter->tactics()->combatGroup     = group++;
    }

    eve::combat::DamageRuntime damage;
    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    CHECK_EQ(ecs::try_get(first->combat()->target), nearTarget);
    CHECK_EQ(ecs::try_get(second->combat()->target), farTarget);

    CommandSpec focus;
    focus.kind         = OrderKind::Attack;
    focus.targetEntity = ecs::handle_of(nearTarget);
    REQUIRE(first->orders()->values.replace(focus).ok());
    REQUIRE(second->orders()->values.replace(focus).ok());
    first->combat()->target  = ecs::handle_of(nearTarget);
    second->combat()->target = ecs::handle_of(nearTarget);
    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    CHECK_EQ(ecs::try_get(first->combat()->target), nearTarget);
    CHECK_EQ(ecs::try_get(second->combat()->target), nearTarget);

    weapon->release();
    first->release();
    second->release();
    nearTarget->release();
    farTarget->release();
    blue->release();
    red->release();
}

// rts_combat_2.cpp
TEST_CASE("rts.automaticTurretsShareDamageCommitmentsWhileExplicitAttackFocuses") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue         = Faction::createFaction(subject("00000000-0000-7000-8000-000000000351"));
    Faction*         red          = Faction::createFaction(subject("00000000-0000-7000-8000-000000000352"));
    Building*        first        = Building::createBuilding(subject("00000000-0000-7000-8000-000000000353"));
    Building*        second       = Building::createBuilding(subject("00000000-0000-7000-8000-000000000354"));
    Unit*            nearTarget   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000355"));
    Unit*            farTarget    = Unit::createUnit(subject("00000000-0000-7000-8000-000000000356"));
    auto             bindBuilding = [&](Building& building) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(blue));
        REQUIRE(link.ok());
        building.faction()->link = std::move(link).takeValue();
    };
    auto bindTarget = [&](Unit& unit) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(red));
        REQUIRE(link.ok());
        unit.faction()->link            = std::move(link).takeValue();
        unit.durability()->state.health = unit.durability()->state.maxHealth = 50.0;
    };
    bindBuilding(*first);
    bindBuilding(*second);
    bindTarget(*nearTarget);
    bindTarget(*farTarget);
    second->placement()->worldY = 1.0f;
    nearTarget->motion()->x     = 4.0f;
    farTarget->motion()->x      = 6.0f;
    std::vector<eve::weapon::WeaponEntity*> weapons;
    for (Building* turret : {first, second}) {
        auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id               = "commitment-cannon";
        definition.damage           = 80.0f;
        definition.range            = 10.0f;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def   = weapon->definition()->owned.get();
        auto link                   = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        turret->weapon()->link             = std::move(link).takeValue();
        turret->combat()->acquisitionRange = 10.0f;
        weapons.push_back(weapon);
    }

    eve::combat::DamageRuntime damage;
    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    CHECK_EQ(ecs::try_get(first->combat()->target), nearTarget);
    CHECK_EQ(ecs::try_get(second->combat()->target), farTarget);

    CommandSpec focus;
    focus.kind         = OrderKind::Attack;
    focus.targetEntity = ecs::handle_of(nearTarget);
    REQUIRE(first->orders()->values.replace(focus).ok());
    REQUIRE(second->orders()->values.replace(focus).ok());
    first->combat()->target  = ecs::handle_of(nearTarget);
    second->combat()->target = ecs::handle_of(nearTarget);
    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    CHECK_EQ(ecs::try_get(first->combat()->target), nearTarget);
    CHECK_EQ(ecs::try_get(second->combat()->target), nearTarget);

    for (auto* weapon : weapons) weapon->release();
    first->release();
    second->release();
    nearTarget->release();
    farTarget->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.splashCommitmentsDistributeAutomaticFireAcrossHostileClusters") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    Faction*           blue = Faction::createFaction(subject("00000000-0000-7000-8000-000000000361"));
    Faction*           red  = Faction::createFaction(subject("00000000-0000-7000-8000-000000000362"));
    std::vector<Unit*> artillery;
    std::vector<Unit*> targets;
    for (const char* id : {"00000000-0000-7000-8000-000000000363", "00000000-0000-7000-8000-000000000364",
                           "00000000-0000-7000-8000-000000000365"})
        artillery.push_back(Unit::createUnit(subject(id)));
    for (const char* id : {"00000000-0000-7000-8000-000000000366", "00000000-0000-7000-8000-000000000367",
                           "00000000-0000-7000-8000-000000000368", "00000000-0000-7000-8000-000000000369"})
        targets.push_back(Unit::createUnit(subject(id)));
    Unit* ally = Unit::createUnit(subject("00000000-0000-7000-8000-00000000036a"));
    auto  bind = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    for (Unit* unit : artillery) bind(*unit, *blue);
    for (Unit* unit : targets) {
        bind(*unit, *red);
        unit->durability()->state.health = unit->durability()->state.maxHealth = 80.0;
    }
    bind(*ally, *blue);
    targets[0]->motion()->x = targets[1]->motion()->x = 5.0f;
    targets[0]->motion()->y                           = 0.0f;
    targets[1]->motion()->y                           = 1.0f;
    targets[2]->motion()->x = targets[3]->motion()->x = 9.0f;
    targets[2]->motion()->y                           = 0.0f;
    targets[3]->motion()->y                           = 1.0f;
    ally->motion()->x                                 = 5.0f;
    ally->motion()->y                                 = 0.5f;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                        = "cluster-shell";
    definition.damage                    = 100.0f;
    definition.range                     = 12.0f;
    definition.projectile.speed          = 10.0f;
    definition.projectile.aoe            = 2.0f;
    definition.splashMinimumDamageFactor = 0.1f;
    weapon->definition()->owned          = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def            = weapon->definition()->owned.get();
    for (Unit* shooter : artillery) {
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        shooter->weapon()->link             = std::move(link).takeValue();
        shooter->combat()->acquisitionRange = 12.0f;
        shooter->tactics()->combatGroup     = 29;
    }

    eve::combat::DamageRuntime damage;
    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    int firstCluster  = 0;
    int secondCluster = 0;
    for (Unit* shooter : artillery) {
        ecs::Entity* selected = ecs::try_get(shooter->combat()->target);
        firstCluster += selected == targets[0] || selected == targets[1];
        secondCluster += selected == targets[2] || selected == targets[3];
        CHECK_NE(selected, ally);
    }
    CHECK_EQ(firstCluster, 2);
    CHECK_EQ(secondCluster, 1);

    weapon->release();
    for (Unit* unit : artillery) unit->release();
    for (Unit* unit : targets) unit->release();
    ally->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.autoSupplyRefillsCanonicalWeaponAmmo") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction          = Faction::createFaction();
    Unit*            supplier         = Unit::createUnit();
    Unit*            recipient        = Unit::createUnit();
    auto             supplierFaction  = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto             recipientFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(supplierFaction.ok());
    REQUIRE(recipientFaction.ok());
    supplier->faction()->link        = std::move(supplierFaction).takeValue();
    recipient->faction()->link       = std::move(recipientFaction).takeValue();
    supplier->supply()->stock        = 8.0f;
    supplier->supply()->capacity     = 8.0f;
    supplier->supply()->range        = 3.0f;
    supplier->supply()->transferRate = 4.0f;
    supplier->supply()->autoDispatch = true;

    eve::weapon::WeaponEntity*    weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                     = "supply-rifle";
    definition.kind                   = eve::weapon::WeaponKind::Ranged;
    definition.magSize                = 10;
    definition.reserveSize            = 10;
    weapon->definition()->owned       = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def         = weapon->definition()->owned.get();
    weapon->state()->resource.kind    = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value   = 2.0f;
    weapon->state()->resource.max     = 10.0f;
    weapon->state()->resource.reserve = 0;
    auto weaponLink                   = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    recipient->weapon()->link = std::move(weaponLink).takeValue();

    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("supply dt")};
    std::vector<eve::rts::LifecycleEvent> events;
    const eve::rts::LifecycleEventSink    eventSink = [&](const eve::rts::LifecycleEvent& event,
                                                       eve::SimulationTick             tick) {
        CHECK_EQ(tick.value(), 1u);
        events.push_back(event);
    };
    auto supplied = eve::rts::SupplySystem::step(step, {}, nullptr, {}, eventSink);
    REQUIRE(supplied.ok());
    CHECK(supplied.value() >= 2u);
    CHECK(std::abs(weapon->state()->resource.value - 6.0f) < 1e-5f);
    CHECK(std::abs(supplier->supply()->stock - 4.0f) < 1e-5f);
    CHECK(std::abs(supplier->supply()->reservedStock - 4.0f) < 1e-5f);
    CHECK(!supplier->orders()->values.empty());
    REQUIRE_EQ(events.size(), 2u);
    CHECK_EQ(static_cast<int>(events[0].kind), static_cast<int>(eve::rts::LifecycleEventKind::SupplyDispatched));
    CHECK_EQ(static_cast<int>(events[1].kind), static_cast<int>(eve::rts::LifecycleEventKind::AmmoResupplied));
    CHECK_EQ(events[1].value, 4.0);

    events.clear();
    supplied = eve::rts::SupplySystem::step(step, {}, nullptr, {}, eventSink);
    REQUIRE(supplied.ok());
    CHECK(std::abs(weapon->state()->resource.value - 10.0f) < 1e-5f);
    CHECK(std::abs(supplier->supply()->reservedStock) < 1e-5f);
    CHECK(supplier->supply()->returning);
    REQUIRE_EQ(events.size(), 2u);
    CHECK_EQ(static_cast<int>(events[0].kind), static_cast<int>(eve::rts::LifecycleEventKind::AmmoResupplied));
    CHECK_EQ(static_cast<int>(events[1].kind), static_cast<int>(eve::rts::LifecycleEventKind::SupplyReturning));
    auto returnOrder = supplier->orders()->values.current();
    REQUIRE(returnOrder.ok());
    CHECK_EQ(static_cast<int>(returnOrder.value().kind), static_cast<int>(OrderKind::Move));

    weapon->release();
    supplier->release();
    recipient->release();
    faction->release();
}

TEST_CASE("rts.attackMovePausesInWeaponRangeThenResumesAndGuardsDestination") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            attacker           = Unit::createUnit();
    Unit*            enemy              = Unit::createUnit();
    attacker->motion()->speed           = 5.0f;
    attacker->combat()->engagementRange = 3.0f;
    attacker->combat()->target          = ecs::handle_of(enemy);
    enemy->motion()->x                  = 2.0f;
    CommandSpec command;
    command.kind   = OrderKind::AttackMove;
    command.target = {5.0f, 0.0f};
    REQUIRE(attacker->orders()->values.enqueue(command).ok());
    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("attack move dt")};
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(std::abs(attacker->motion()->x) < 1e-5f);

    attacker->combat()->target = {};
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(std::abs(attacker->motion()->x - 5.0f) < 1e-5f);
    REQUIRE(eve::rts::MovementOrderSystem::step().ok());
    CHECK(!attacker->orders()->values.empty());
    attacker->release();
    enemy->release();
}

TEST_CASE("rts.stopSettlesImmediatelyAndHoldAnchorsAutomaticCombat") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            unit  = Unit::createUnit();
    unit->motion()->x      = 4.0f;
    unit->motion()->y      = 2.0f;
    unit->combat()->target = ecs::handle_of(unit);
    CommandSpec hold;
    hold.kind = OrderKind::HoldPosition;
    REQUIRE(unit->orders()->values.replace(hold).ok());
    REQUIRE(eve::rts::CommandStateSystem::step().ok());
    CHECK(unit->combat()->holdPosition);
    CHECK(unit->combat()->guardSet);
    CHECK(std::abs(unit->combat()->guardX - 4.0f) < 1e-5f);

    CommandSpec stop;
    stop.kind = OrderKind::Stop;
    REQUIRE(unit->orders()->values.replace(stop).ok());
    auto stopped = eve::rts::CommandStateSystem::step();
    REQUIRE(stopped.ok());
    CHECK_EQ(stopped.value(), 1u);
    CHECK(unit->orders()->values.empty());
    CHECK(!unit->combat()->holdPosition);
    CHECK(!unit->combat()->guardSet);
    CHECK(unit->combat()->target.table == nullptr);
    unit->release();
}

TEST_CASE("rts.matchTeamsDriveAbilityRelationsAndAutomaticCombatAlliances") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         alpha = Faction::createFaction(subject("00000000-0000-7000-8000-00000000d101"));
    Faction*         bravo = Faction::createFaction(subject("00000000-0000-7000-8000-00000000d102"));
    Faction*         red   = Faction::createFaction(subject("00000000-0000-7000-8000-00000000d103"));
    Match*           match = Match::createMatch(subject("00000000-0000-7000-8000-00000000d104"));
    REQUIRE(eve::rts::MatchSystem::addParticipant(*match, *alpha, 1).ok());
    REQUIRE(eve::rts::MatchSystem::addParticipant(*match, *bravo, 1).ok());
    REQUIRE(eve::rts::MatchSystem::addParticipant(*match, *red, 2).ok());
    CHECK(eve::rts::FactionRelationSystem::isAllied(alpha, bravo));
    CHECK(!eve::rts::FactionRelationSystem::isAllied(alpha, red));

    Unit* caster   = Unit::createUnit(subject("00000000-0000-7000-8000-00000000d105"));
    Unit* teammate = Unit::createUnit(subject("00000000-0000-7000-8000-00000000d106"));
    auto  bind     = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link            = std::move(link).takeValue();
        unit.durability()->state.health = unit.durability()->state.maxHealth = 10.0;
    };
    bind(*caster, *alpha);
    bind(*teammate, *bravo);
    teammate->motion()->x = 2.0f;
    eve::combat::DamageRuntime damage;
    eve::rts::AbilitySpec      hostile;
    hostile.id     = "hostile";
    hostile.target = eve::rts::AbilityTarget::Enemy;
    hostile.range  = 4.0f;
    hostile.damage = 2.0f;
    CHECK(!eve::rts::AbilitySystem::cast(*caster, hostile, ecs::handle_of(teammate), {}, damage).ok());

    eve::weapon::WeaponEntity*    weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                   = "team-rifle";
    definition.kind                 = eve::weapon::WeaponKind::Ranged;
    definition.damage               = 3.0f;
    definition.damageType           = "damage.physical";
    definition.range                = 5.0f;
    definition.cooldown             = 0.1f;
    definition.magSize              = 4;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = weapon->state()->resource.max = 4.0f;
    auto weaponLink = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    caster->weapon()->link             = std::move(weaponLink).takeValue();
    caster->combat()->acquisitionRange = 5.0f;
    eve::sensing::SensingWorld        sensing;
    eve::rts::CombatFireSystem::State combatState;
    auto                              fired = eve::rts::CombatFireSystem::step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(0.2).expect("team combat dt")}, combatState, sensing,
        damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 0u);
    CHECK_EQ(teammate->durability()->state.health, 10.0);

    eve::weapon::WeaponDefinition rocket;
    rocket.id               = "team-rocket";
    rocket.damage           = 5.0f;
    rocket.damageType       = "damage.explosive";
    rocket.range            = 5.0f;
    rocket.projectile.speed = 2.0f;
    eve::rts::RTSProjectileSystem projectiles;
    REQUIRE(projectiles
                .launch(caster->identity()->subject, caster->faction()->link.handle(), {}, ecs::handle_of(teammate),
                        {2.0f, 0.0f}, rocket)
                .ok());
    auto impact = projectiles.step(
        {eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("team projectile dt")}, damage);
    REQUIRE(impact.ok());
    CHECK_EQ(impact.value(), 1u);
    CHECK_EQ(teammate->durability()->state.health, 10.0);

    caster->command()->range             = 5.0f;
    caster->command()->capacity          = 2;
    teammate->command()->requiresCommand = true;
    teammate->command()->cost            = 1;
    REQUIRE(eve::rts::CommandNetworkSystem::step().ok());
    CHECK(teammate->command()->inCommand);
    CHECK(ecs::try_get(teammate->command()->source) == caster);

    Unit* enemy = Unit::createUnit(subject("00000000-0000-7000-8000-00000000d108"));
    bind(*enemy, *red);
    enemy->motion()->x      = 3.0f;
    alpha->intel()->enabled = true;
    CHECK(!eve::rts::FactionIntelSystem::isTargetable(alpha, enemy->identity()->subject));
    alpha->intel()->contacts.push_back(
        {subject("ffffffff-ffff-7fff-bfff-ffffffffffff"), "remembered", {}, 0.0, false, false});
    alpha->intel()->contacts.push_back({enemy->identity()->subject, "unit", {3.0f, 0.0f}, 1.0, false, false});
    CHECK(!eve::rts::AbilitySystem::cast(*caster, hostile, ecs::handle_of(enemy), {}, damage).ok());
    alpha->intel()->contacts.back().visible  = true;
    alpha->intel()->contacts.back().detected = true;
    REQUIRE(eve::rts::AbilitySystem::cast(*caster, hostile, ecs::handle_of(enemy), {}, damage).ok());
    CHECK_EQ(enemy->durability()->state.health, 8.0);
    alpha->intel()->contacts.back().visible  = false;
    alpha->intel()->contacts.back().detected = false;
    eve::rts::CommandSpec hiddenAttack;
    hiddenAttack.kind         = eve::rts::OrderKind::Attack;
    hiddenAttack.targetEntity = ecs::handle_of(enemy);
    REQUIRE(caster->orders()->values.replace(hiddenAttack).ok());
    auto hiddenFire = eve::rts::CombatFireSystem::step(
        {eve::SimulationTick{3}, eve::Duration::fromSeconds(0.2).expect("hidden target dt")}, combatState, sensing,
        damage);
    REQUIRE(hiddenFire.ok());
    CHECK_EQ(hiddenFire.value(), 0u);
    CHECK_EQ(enemy->durability()->state.health, 8.0);

    Building* influence      = Building::createBuilding(subject("00000000-0000-7000-8000-00000000d107"));
    auto      influenceOwner = eve::rts::FactionLink::bind(ecs::handle_of(alpha));
    REQUIRE(influenceOwner.ok());
    influence->faction()->link                        = std::move(influenceOwner).takeValue();
    influence->placement()->placed                    = true;
    influence->construction()->progress               = 1.0f;
    influence->integrity()->alive                     = true;
    influence->infrastructure()->powered              = true;
    influence->infrastructure()->buildInfluenceRadius = 5.0f;
    const auto outpost                                = eve::LogicalId::fromParts("building", "outpost");
    REQUIRE(outpost.has_value());
    CHECK(eve::rts::BuildInfluenceSystem::validate(
              *bravo, {2.0f, 0.0f}, *outpost,
              [](eve::rts::WorldPosition, eve::LogicalId) { return eve::Result<void>::success(); })
              .ok());

    influence->release();
    enemy->release();
    weapon->release();
    teammate->release();
    caster->release();
    match->release();
    red->release();
    bravo->release();
    alpha->release();
}

TEST_CASE("rts.abilitiesUseCanonicalDamageEconomyCooldownAndInterruptibleChannels") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000231"));
    Faction*         red      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000232"));
    Unit*            caster   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000233"));
    Unit*            target   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000234"));
    auto             blueLink = eve::rts::FactionLink::bind(ecs::handle_of(blue));
    auto             redLink  = eve::rts::FactionLink::bind(ecs::handle_of(red));
    REQUIRE(blueLink.ok());
    REQUIRE(redLink.ok());
    caster->faction()->link            = std::move(blueLink).takeValue();
    target->faction()->link            = std::move(redLink).takeValue();
    caster->durability()->state.health = caster->durability()->state.maxHealth = 10.0;
    target->durability()->state.health = target->durability()->state.maxHealth = 10.0;
    target->motion()->x                                                        = 3.0f;
    target->shield()->capacity = target->shield()->value = 3.0f;
    target->shield()->regenDelay                         = 2.0f;
    eve::combat::DamageRuntime damage;

    eve::rts::AbilitySpec bolt;
    bolt.id              = "arc-bolt";
    bolt.target          = eve::rts::AbilityTarget::Enemy;
    bolt.range           = 5.0f;
    bolt.cooldown        = 1.0f;
    bolt.resourceType    = "energy";
    bolt.resourceCost    = 2;
    bolt.damage          = 5.0f;
    std::int64_t debited = 0;
    auto         cast    = eve::rts::AbilitySystem::cast(
        *caster, bolt, ecs::handle_of(target), {}, damage, [&](Unit& source, const eve::resource::CostSpec& cost) {
            CHECK(&source == caster);
            debited += cost.items()[0].amount.value();
            return eve::Result<eve::resource::Receipt>::success(eve::resource::Receipt{});
        });
    REQUIRE(cast.ok());
    CHECK_EQ(debited, 2);
    CHECK(std::abs(target->shield()->value) < 1e-5f);
    CHECK(std::abs(target->durability()->state.health - 8.0) < 1e-5);
    auto cooling = eve::rts::AbilitySystem::cast(*caster, bolt, ecs::handle_of(target), {}, damage);
    CHECK(!cooling.ok());

    eve::rts::AbilitySpec channel;
    channel.id                         = "repair-channel";
    channel.target                     = eve::rts::AbilityTarget::Self;
    channel.range                      = 0.0f;
    channel.castTime                   = 1.0f;
    channel.healing                    = 4.0f;
    channel.interruptOnDamage          = true;
    caster->durability()->state.health = 5.0;
    std::vector<eve::rts::LifecycleEvent> abilityEvents;
    auto collectAbility = [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick) {
        abilityEvents.push_back(event);
    };
    REQUIRE(eve::rts::AbilitySystem::cast(*caster, channel, {}, {}, damage, {}, {}, {}, collectAbility).ok());
    caster->durability()->state.health = 4.0;
    auto interrupted                   = eve::rts::AbilitySystem::step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("ability dt")}, damage, {}, collectAbility);
    REQUIRE(interrupted.ok());
    CHECK(!caster->abilities()->channel.has_value());
    CHECK(std::abs(caster->durability()->state.health - 4.0) < 1e-5);
    REQUIRE_EQ(abilityEvents.size(), 2u);
    CHECK_EQ(static_cast<int>(abilityEvents[0].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::AbilityChannelStarted));
    CHECK_EQ(static_cast<int>(abilityEvents[1].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::AbilityInterrupted));

    caster->veterancy()->veteranThreshold    = 10.0f;
    caster->veterancy()->eliteThreshold      = 20.0f;
    caster->veterancy()->veteranDamageFactor = 1.25f;
    caster->veterancy()->eliteDamageFactor   = 1.5f;
    caster->veterancy()->veteranHealthFactor = 1.2f;
    caster->veterancy()->eliteHealthFactor   = 1.5f;
    target->durability()->alive              = true;
    target->durability()->state.health       = 2.0;
    target->durability()->state.maxHealth    = 10.0;
    target->shield()->value                  = 0.0f;
    eve::rts::AbilitySpec finisher;
    finisher.id     = "finisher";
    finisher.target = eve::rts::AbilityTarget::Enemy;
    finisher.range  = 5.0f;
    finisher.damage = 2.0f;
    REQUIRE(eve::rts::AbilitySystem::cast(*caster, finisher, ecs::handle_of(target), {}, damage).ok());
    CHECK(!target->durability()->alive);
    CHECK_EQ(caster->veterancy()->experience, 10.0f);
    CHECK_EQ(caster->veterancy()->level, 1);
    CHECK_EQ(caster->combat()->upgradeDamageFactor, 1.25f);
    CHECK_EQ(caster->durability()->state.maxHealth, 12.0);

    target->release();
    caster->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.projectilesUseCanonicalTrajectorySweptImpactAndHostileSplashDamage") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue   = Faction::createFaction(subject("00000000-0000-7000-8000-000000000241"));
    Faction*         red    = Faction::createFaction(subject("00000000-0000-7000-8000-000000000242"));
    Unit*            source = Unit::createUnit(subject("00000000-0000-7000-8000-000000000243"));
    Unit*            target = Unit::createUnit(subject("00000000-0000-7000-8000-000000000244"));
    Unit*            splash = Unit::createUnit(subject("00000000-0000-7000-8000-000000000245"));
    Unit*            ally   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000246"));
    auto             bind   = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link            = std::move(link).takeValue();
        unit.durability()->state.health = unit.durability()->state.maxHealth = 10.0;
    };
    bind(*source, *blue);
    bind(*ally, *blue);
    bind(*target, *red);
    bind(*splash, *red);
    target->motion()->x        = 4.0f;
    splash->motion()->x        = 4.5f;
    ally->motion()->x          = 4.5f;
    target->shield()->capacity = target->shield()->value = 2.0f;

    eve::weapon::WeaponDefinition definition;
    definition.id               = "rts-rocket";
    definition.damage           = 6.0f;
    definition.damageType       = "damage.explosive";
    definition.range            = 10.0f;
    definition.projectile.speed = 4.0f;
    definition.projectile.aoe   = 2.0f;
    eve::rts::RTSProjectileSystem projectiles;
    REQUIRE(projectiles
                .launch(source->identity()->subject, source->faction()->link.handle(), {}, ecs::handle_of(target),
                        {4.0f, 0.0f}, definition)
                .ok());
    eve::combat::DamageRuntime damage;
    auto                       flying =
        projectiles.step({eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("projectile dt")}, damage);
    REQUIRE(flying.ok());
    CHECK_EQ(flying.value(), 0u);
    CHECK_EQ(projectiles.activeCount(), 1u);
    const auto                    inFlight = projectiles.snapshot();
    eve::rts::RTSProjectileSystem restoredProjectiles;
    REQUIRE(restoredProjectiles
                .restore(inFlight,
                         [&](eve::SubjectRef stable) -> ecs::Entity* {
                             for (Unit* unit : {source, target, splash, ally})
                                 if (unit->identity()->subject == stable) return unit;
                             if (blue->identity()->subject == stable) return blue;
                             if (red->identity()->subject == stable) return red;
                             return nullptr;
                         })
                .ok());
    CHECK_EQ(restoredProjectiles.activeCount(), 1u);
    auto impacted = restoredProjectiles.step(
        {eve::SimulationTick{2}, eve::Duration::fromSeconds(0.5).expect("projectile dt")}, damage);
    REQUIRE(impacted.ok());
    CHECK_EQ(impacted.value(), 1u);
    CHECK_EQ(restoredProjectiles.activeCount(), 0u);
    CHECK(std::abs(target->shield()->value) < 1e-5f);
    CHECK(std::abs(target->durability()->state.health - 6.0) < 1e-5);
    CHECK(std::abs(splash->durability()->state.health - 5.5) < 1e-5);
    CHECK(std::abs(ally->durability()->state.health - 10.0) < 1e-5);

    Unit* blocker = Unit::createUnit(subject("00000000-0000-7000-8000-000000000247"));
    bind(*blocker, *red);
    blocker->motion()->x               = 1.5f;
    target->durability()->state.health = 10.0;
    definition.projectile.aoe          = 0.0f;
    definition.blockedByObstacles      = true;
    REQUIRE(restoredProjectiles
                .launch(source->identity()->subject, source->faction()->link.handle(), {}, ecs::handle_of(target),
                        {4.0f, 0.0f}, definition)
                .ok());
    bool queriedCollision = false;
    auto blocked          = restoredProjectiles.step(
        {eve::SimulationTick{3}, eve::Duration::fromSeconds(0.5).expect("blocked projectile dt")}, damage,
        [&](eve::rts::WorldPosition from, float fromHeight, eve::rts::WorldPosition to, float toHeight,
            eve::SubjectRef projectileSource, ecs::EntityHandle intended) {
            queriedCollision = true;
            CHECK(from.x <= 1e-5f);
            CHECK(to.x >= 1.9f);
            CHECK(std::abs(fromHeight) < 1e-5f);
            CHECK(std::abs(toHeight) < 1e-5f);
            CHECK(projectileSource == source->identity()->subject);
            CHECK(ecs::try_get(intended) == target);
            return eve::Result<std::optional<eve::rts::ProjectileCollision>>::success(
                eve::rts::ProjectileCollision{{1.5f, 0.0f}, ecs::handle_of(blocker)});
        });
    REQUIRE(blocked.ok());
    CHECK(queriedCollision);
    CHECK_EQ(blocked.value(), 1u);
    CHECK_EQ(restoredProjectiles.activeCount(), 0u);
    CHECK(std::abs(blocker->durability()->state.health - 4.0) < 1e-5);
    CHECK(std::abs(target->durability()->state.health - 10.0) < 1e-5);

    source->veterancy()->veteranThreshold    = 10.0f;
    source->veterancy()->eliteThreshold      = 20.0f;
    source->veterancy()->veteranDamageFactor = 1.25f;
    source->veterancy()->eliteDamageFactor   = 1.5f;
    source->veterancy()->veteranHealthFactor = 1.2f;
    source->veterancy()->eliteHealthFactor   = 1.5f;
    target->durability()->alive              = true;
    target->durability()->state.health = target->durability()->state.maxHealth = 10.0;
    definition.damage                                                          = 12.0f;
    definition.blockedByObstacles                                              = false;
    REQUIRE(restoredProjectiles
                .launch(source->identity()->subject, source->faction()->link.handle(), {}, ecs::handle_of(target),
                        {4.0f, 0.0f}, definition)
                .ok());
    auto lethal = restoredProjectiles.step(
        {eve::SimulationTick{4}, eve::Duration::fromSeconds(1.0).expect("lethal projectile dt")}, damage);
    REQUIRE(lethal.ok());
    CHECK(!target->durability()->alive);
    CHECK_EQ(source->veterancy()->level, 1);
    CHECK_EQ(source->veterancy()->experience, 10.0f);
    CHECK_EQ(source->combat()->upgradeDamageFactor, 1.25f);
    CHECK_EQ(source->durability()->state.maxHealth, 12.0);

    blocker->release();
    ally->release();
    splash->release();
    target->release();
    source->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.indirectFireRequiresFriendlyObserverAndFreezesAimAfterContactLoss") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction(subject("00000000-0000-7000-8000-0000000003a1"));
    Faction*         red      = Faction::createFaction(subject("00000000-0000-7000-8000-0000000003a2"));
    Unit*            gun      = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003a3"));
    Unit*            observer = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003a4"));
    Unit*            target   = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003a5"));
    auto             bind     = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*gun, *blue);
    bind(*observer, *blue);
    bind(*target, *red);
    gun->vision()->sightRange          = 3.0f;
    gun->combat()->acquisitionRange    = 15.0f;
    observer->motion()->x              = 10.0f;
    observer->vision()->sightRange     = 4.0f;
    target->motion()->x                = 12.0f;
    target->durability()->state.health = target->durability()->state.maxHealth = 100.0;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                   = "observed-howitzer";
    definition.kind                 = eve::weapon::WeaponKind::Ranged;
    definition.logic                = "rts-indirect";
    definition.damage               = 10.0f;
    definition.range                = 15.0f;
    definition.cooldown             = 0.1f;
    definition.magSize              = 4;
    definition.projectile.speed     = 2.0f;
    definition.projectile.gravity   = 1.0f;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 4.0f;
    weapon->state()->resource.max   = 4.0f;
    auto weaponLink                 = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    gun->weapon()->link = std::move(weaponLink).takeValue();

    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State combatState;
    eve::rts::RTSProjectileSystem     projectiles;
    auto                              step =
        eve::SimulationStep{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("observed fire step")};
    auto fired = eve::rts::CombatFireSystem::step(step, combatState, sensing, damage, &projectiles);
    REQUIRE(fired.ok());
    CHECK_EQ(ecs::try_get(gun->combat()->target), target);
    CHECK_EQ(fired.value(), 1u);
    CHECK(gun->artillery()->usingObservedFire);
    CHECK_EQ(ecs::try_get(gun->artillery()->observedFireSpotter), observer);
    auto inFlight = projectiles.snapshot();
    REQUIRE_EQ(inFlight.payloads.size(), 1u);
    CHECK_EQ(inFlight.payloads.front().target, target->identity()->subject);
    CHECK_EQ(inFlight.payloads.front().observer, observer->identity()->subject);
    eve::rts::RTSProjectileSystem restoredProjectiles;
    REQUIRE(restoredProjectiles
                .restore(inFlight,
                         [&](eve::SubjectRef stable) -> ecs::Entity* {
                             for (Unit* unit : {gun, observer, target})
                                 if (unit->identity()->subject == stable) return unit;
                             if (blue->identity()->subject == stable) return blue;
                             if (red->identity()->subject == stable) return red;
                             return nullptr;
                         })
                .ok());

    observer->motion()->x = 30.0f;
    target->motion()->x   = 14.0f;
    auto advanced         = restoredProjectiles.step(step, damage);
    REQUIRE(advanced.ok());
    inFlight = restoredProjectiles.snapshot();
    REQUIRE_EQ(inFlight.payloads.size(), 1u);
    CHECK(!inFlight.payloads.front().target.isValid());
    CHECK(!inFlight.payloads.front().observer.isValid());
    CHECK(std::abs(inFlight.payloads.front().targetPoint.x - 12.0f) < 1e-5f);

    gun->combat()->target = {};
    step.tick             = eve::SimulationTick{2};
    auto blind            = eve::rts::CombatFireSystem::step(step, combatState, sensing, damage, &projectiles);
    REQUIRE(blind.ok());
    CHECK_EQ(blind.value(), 0u);
    CHECK_EQ(ecs::try_get(gun->combat()->target), nullptr);

    weapon->release();
    gun->release();
    observer->release();
    target->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.fireSupportAssignsSuppressionAndCounterBatteryUsesRecentExposure") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000251"));
    Faction*         red       = Faction::createFaction(subject("00000000-0000-7000-8000-000000000252"));
    Unit*            requester = Unit::createUnit(subject("00000000-0000-7000-8000-000000000253"));
    Unit*            battery   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000254"));
    Unit*            hostile   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000255"));
    auto             bind      = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link            = std::move(link).takeValue();
        unit.durability()->state.health = unit.durability()->state.maxHealth = 10.0;
    };
    bind(*requester, *blue);
    bind(*battery, *blue);
    bind(*hostile, *red);
    hostile->motion()->x = 8.0f;
    eve::weapon::WeaponDefinition indirect;
    indirect.id                 = "rts-howitzer-support";
    indirect.range              = 20.0f;
    indirect.projectile.speed   = 10.0f;
    indirect.projectile.gravity = 1.0f;
    auto makeWeapon             = [&]() {
        auto* weapon                = eve::weapon::WeaponEntity::createWeapon();
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(indirect);
        weapon->definition()->def   = weapon->definition()->owned.get();
        weapon->state()->stages     = &weapon->definition()->def->stages;
        return weapon;
    };
    auto* batteryWeapon = makeWeapon();
    auto* hostileWeapon = makeWeapon();
    auto  batteryLink   = eve::rts::WeaponLink::bind(ecs::handle_of(batteryWeapon));
    auto  hostileLink   = eve::rts::WeaponLink::bind(ecs::handle_of(hostileWeapon));
    REQUIRE(batteryLink.ok());
    REQUIRE(hostileLink.ok());
    battery->weapon()->link = std::move(batteryLink).takeValue();
    hostile->weapon()->link = std::move(hostileLink).takeValue();

    auto assigned = eve::rts::FireSupportSystem::request(*requester, {10.0f, 0.0f}, 2.0f, 3, 1);
    REQUIRE(assigned.ok());
    CHECK_EQ(assigned.value(), 1u);
    auto supportOrder = battery->orders()->values.current();
    REQUIRE(supportOrder.ok());
    CHECK_EQ(static_cast<int>(supportOrder.value().kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK_EQ(battery->artillery()->suppressionShotsRemaining, 3);
    CHECK(ecs::try_get(battery->artillery()->fireSupportRequester) == requester);
    auto cancelled = eve::rts::FireSupportSystem::cancel(*requester);
    REQUIRE(cancelled.ok());
    CHECK_EQ(cancelled.value(), 1u);

    battery->artillery()->autoCounterBattery        = true;
    battery->artillery()->counterBatteryWindowTicks = 5;
    hostile->artillery()->lastFireTick              = eve::SimulationTick{10};
    hostile->artillery()->lastFirePosition          = {8.0f, 0.0f};
    auto countered = eve::rts::FireSupportSystem::step({eve::SimulationTick{12}, eve::Duration::zero()});
    REQUIRE(countered.ok());
    CHECK_EQ(countered.value(), 1u);
    auto counterOrder = battery->orders()->values.current();
    REQUIRE(counterOrder.ok());
    CHECK_EQ(static_cast<int>(counterOrder.value().kind), static_cast<int>(OrderKind::AttackGround));
    CHECK(std::abs(counterOrder.value().target.x - 8.0f) < 1e-5f);

    hostileWeapon->release();
    batteryWeapon->release();
    hostile->release();
    battery->release();
    requester->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.artilleryRelocationChoosesReachableLowThreatFreshPosition") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue = Faction::createFaction(subject("00000000-0000-7000-8000-000000000291"));
    Faction*         red  = Faction::createFaction(subject("00000000-0000-7000-8000-000000000292"));
    Unit*            gun  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000293"));
    auto             bind = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link            = std::move(link).takeValue();
        unit.durability()->state.health = unit.durability()->state.maxHealth = 10.0;
    };
    bind(*gun, *blue);
    gun->motion()->x = 10.0f;
    gun->motion()->y = 10.0f;

    eve::map::Pathfinder     pathfinder(32, 32);
    eve::rts::NavigationGrid grid;
    auto initial = eve::rts::ArtilleryRelocationSystem::select(*gun, {20.0f, 10.0f}, 4.0f, 20.0f, pathfinder, grid);
    REQUIRE(initial.ok());
    const WorldPosition departed          = initial.value().target;
    gun->artillery()->departedPosition    = departed;
    gun->artillery()->hasDepartedPosition = true;

    std::vector<Unit*>                      threats;
    std::vector<eve::weapon::WeaponEntity*> weapons;
    for (const auto& position : std::array<WorldPosition, 2>{{{9.03f, 13.88f}, {9.03f, 6.12f}}}) {
        Unit* threat = Unit::createUnit();
        bind(*threat, *red);
        threat->motion()->x                  = position.x;
        threat->motion()->y                  = position.y;
        auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id               = "relocation-threat";
        definition.damage           = 10.0f;
        definition.range            = 1.25f;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def   = weapon->definition()->owned.get();
        auto weaponLink             = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(weaponLink.ok());
        threat->weapon()->link = std::move(weaponLink).takeValue();
        threats.push_back(threat);
        weapons.push_back(weapon);
    }

    auto selected = eve::rts::ArtilleryRelocationSystem::select(*gun, {20.0f, 10.0f}, 4.0f, 20.0f, pathfinder, grid);
    REQUIRE(selected.ok());
    CHECK(selected.value().avoidedThreat);
    CHECK(selected.value().avoidedDepartedPosition);
    CHECK(std::hypot(selected.value().target.x - departed.x, selected.value().target.y - departed.y) > 1.0f);
    CHECK(pathfinder.isWalkable(static_cast<int>(std::lround(selected.value().target.x)),
                                static_cast<int>(std::lround(selected.value().target.y))));

    for (auto* weapon : weapons) weapon->release();
    for (Unit* threat : threats) threat->release();
    gun->release();
    red->release();
    blue->release();
}
}  // namespace test_rts_combat

// Contract section: rts combat 3
#include "PrivateTestScope.h"

// rts_combat_3.cpp
namespace test_rts_combat_3 {
TEST_CASE("rts.suppressionAndEscortFacadesRoundTripDedicatedTacticalState") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    const auto       factionSubject   = subject("00000000-0000-7000-8000-00000000d211");
    const auto       firstSubject     = subject("00000000-0000-7000-8000-00000000d212");
    const auto       secondSubject    = subject("00000000-0000-7000-8000-00000000d213");
    const auto       protectedSubject = subject("00000000-0000-7000-8000-00000000d214");
    auto*            faction          = module.newFaction(factionSubject).value();
    auto*            first            = module.newFactionUnit(*faction, firstSubject).value();
    auto*            second           = module.newFactionUnit(*faction, secondSubject).value();
    auto*            protectedUnit    = module.newFactionUnit(*faction, protectedSubject).value();
    protectedUnit->motion()->x        = 10.0f;
    protectedUnit->motion()->y        = 8.0f;
    auto* weapon                      = eve::weapon::WeaponEntity::createWeapon();
    auto  firstWeapon                 = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    auto  secondWeapon                = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(firstWeapon.ok());
    REQUIRE(secondWeapon.ok());
    first->weapon()->link  = std::move(firstWeapon).takeValue();
    second->weapon()->link = std::move(secondWeapon).takeValue();

    eve::rts::RTSReplayCommand suppress;
    suppress.tick                    = eve::SimulationTick{4};
    suppress.operation               = eve::rts::RTSReplayOperation::SuppressArea;
    suppress.units                   = {secondSubject, firstSubject};
    suppress.command.kind            = OrderKind::SuppressArea;
    suppress.command.target          = {12.0f, 3.0f};
    suppress.command.secondaryTarget = {18.0f, 7.0f};
    suppress.command.radius          = 2.0f;
    suppress.priority                = 3;
    eve::rts::RTSReplayCommand escort;
    escort.tick              = eve::SimulationTick{5};
    escort.operation         = eve::rts::RTSReplayOperation::Escort;
    escort.units             = {secondSubject, firstSubject};
    escort.targetEntity      = protectedSubject;
    escort.command.kind      = OrderKind::Escort;
    escort.command.radius    = 6.0f;
    escort.formation.spacing = 1.5f;
    eve::rts::RTSCommandLog recorded;
    REQUIRE(recorded.queue(suppress).ok());
    REQUIRE(recorded.queue(escort).ok());
    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 2\n"));
    eve::rts::RTSCommandLog replay;
    REQUIRE(replay.importText(text).ok());
    CHECK_EQ(replay.exportText(), text);

    auto suppressed = replay.apply(eve::SimulationTick{4}, module);
    REQUIRE(suppressed.ok());
    CHECK_EQ(suppressed.value(), std::size_t{2});
    auto suppressionOrder = first->orders()->values.current();
    REQUIRE(suppressionOrder.ok());
    CHECK_EQ(static_cast<int>(suppressionOrder.value().kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK_EQ(first->artillery()->suppressionShotsRemaining, 3);
    CHECK_EQ(second->artillery()->suppressionShotsRemaining, 3);
    CHECK(std::abs(suppressionOrder.value().secondaryTarget.x - 18.0f) < 1e-5f);

    auto escorted = replay.apply(eve::SimulationTick{5}, module);
    REQUIRE(escorted.ok());
    CHECK_EQ(escorted.value(), std::size_t{2});
    auto escortOrder = first->orders()->values.current();
    REQUIRE(escortOrder.ok());
    CHECK_EQ(static_cast<int>(escortOrder.value().kind), static_cast<int>(OrderKind::Escort));
    CHECK(ecs::try_get(escortOrder.value().targetEntity) == protectedUnit);
    CHECK(first->tactics()->escortOffsetX > 0.0f);
    CHECK(second->tactics()->escortOffsetX < 0.0f);
    CHECK(std::abs(first->tactics()->protectionRange - 6.0f) < 1e-5f);
    CHECK(std::abs(first->combat()->leashRange - 6.0f) < 1e-5f);
    weapon->release();
}

TEST_CASE("rts.automaticTargetPoliciesAndWeaponPreferencesOverrideDistanceButExplicitAttackStillWins") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto blueHandle       = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002a4")));
    auto redHandle        = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002a5")));
    auto workerDefinition = eve::LogicalId::parse("rts:worker");
    auto tankDefinition   = eve::LogicalId::parse("rts:tank");
    REQUIRE(workerDefinition.has_value());
    REQUIRE(tankDefinition.has_value());
    auto attackerHandle =
        ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002a1"), *workerDefinition));
    auto closeHandle =
        ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002a2"), *workerDefinition));
    auto valuableHandle =
        ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002a3"), *tankDefinition));
    auto* blue        = dynamic_cast<Faction*>(ecs::try_get(blueHandle));
    auto* red         = dynamic_cast<Faction*>(ecs::try_get(redHandle));
    auto* attacker    = dynamic_cast<Unit*>(ecs::try_get(attackerHandle));
    auto* close       = dynamic_cast<Unit*>(ecs::try_get(closeHandle));
    auto* valuable    = dynamic_cast<Unit*>(ecs::try_get(valuableHandle));
    auto  bindFaction = [&](Unit& unit, ecs::EntityHandle faction) {
        auto link = eve::rts::FactionLink::bind(faction);
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bindFaction(*attacker, blueHandle);
    bindFaction(*close, redHandle);
    bindFaction(*valuable, redHandle);
    attacker->combat()->acquisitionRange                           = 10.0f;
    attacker->combat()->targetPriorities[tankDefinition->format()] = 5.0f;
    close->motion()->x                                             = 2.0f;
    valuable->motion()->x                                          = 6.0f;
    close->durability()->state.health = close->durability()->state.maxHealth = 100.0;
    valuable->durability()->state.health = valuable->durability()->state.maxHealth = 100.0;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "priority-rifle";
    definition.damage           = 1.0f;
    definition.range            = 10.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    weapon->state()->stages     = &weapon->definition()->def->stages;
    auto weaponLink             = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    attacker->weapon()->link = std::move(weaponLink).takeValue();
    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    auto                              step =
        eve::SimulationStep{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("priority combat step")};
    REQUIRE(eve::rts::CombatFireSystem::step(step, state, sensing, damage).ok());
    CHECK_EQ(ecs::try_get(attacker->combat()->target), valuable);

    REQUIRE(valuable->tags()->values.add("armored").ok());
    attacker->combat()->targetPriorities.clear();
    attacker->combat()->target      = {};
    definition.preferredTargetTags  = {"armored"};
    definition.preferredTargetBonus = 4.0f;
    weapon->definition()->owned     = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def       = weapon->definition()->owned.get();
    weapon->state()->stages         = &weapon->definition()->def->stages;
    step.tick                       = eve::SimulationTick{2};
    REQUIRE(eve::rts::CombatFireSystem::step(step, state, sensing, damage).ok());
    CHECK_EQ(ecs::try_get(attacker->combat()->target), valuable);

    CommandSpec explicitAttack;
    explicitAttack.kind         = OrderKind::Attack;
    explicitAttack.targetEntity = closeHandle;
    REQUIRE(attacker->orders()->values.replace(explicitAttack).ok());
    step.tick = eve::SimulationTick{3};
    REQUIRE(eve::rts::CombatFireSystem::step(step, state, sensing, damage).ok());
    CHECK_EQ(ecs::try_get(attacker->combat()->target), close);

    weapon->release();
    valuable->release();
    close->release();
    attacker->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.weaponTargetDomainAndTagsFilterAutomaticAndExplicitAttacks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto  blueHandle     = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002c1")));
    auto  redHandle      = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002c2")));
    auto  attackerHandle = ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002c3")));
    auto  groundHandle   = ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002c4")));
    auto  airHandle      = ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002c5")));
    auto* attacker       = dynamic_cast<Unit*>(ecs::try_get(attackerHandle));
    auto* ground         = dynamic_cast<Unit*>(ecs::try_get(groundHandle));
    auto* air            = dynamic_cast<Unit*>(ecs::try_get(airHandle));
    auto  bind           = [&](Unit& unit, ecs::EntityHandle faction) {
        auto link = eve::rts::FactionLink::bind(faction);
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*attacker, blueHandle);
    bind(*ground, redHandle);
    bind(*air, redHandle);
    attacker->combat()->acquisitionRange = 10.0f;
    ground->motion()->x                  = 2.0f;
    air->motion()->x                     = 5.0f;
    air->motion()->airborne              = true;
    REQUIRE(air->tags()->values.add("armored").ok());
    ground->durability()->state.health = ground->durability()->state.maxHealth = 100.0;
    air->durability()->state.health = air->durability()->state.maxHealth = 100.0;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                 = "anti-air";
    definition.damage             = 0.0f;
    definition.range              = 10.0f;
    definition.targetsGround      = false;
    definition.targetsAir         = true;
    definition.requiredTargetTags = {"armored"};
    definition.excludedTargetTags = {"cloaked"};
    weapon->definition()->owned   = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def     = weapon->definition()->owned.get();
    weapon->state()->stages       = &weapon->definition()->def->stages;
    auto weaponLink               = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    attacker->weapon()->link = std::move(weaponLink).takeValue();
    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    auto                              step =
        eve::SimulationStep{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("target filter step")};
    REQUIRE(eve::rts::CombatFireSystem::step(step, state, sensing, damage).ok());
    CHECK_EQ(ecs::try_get(attacker->combat()->target), air);

    CommandSpec explicitGround;
    explicitGround.kind         = OrderKind::Attack;
    explicitGround.targetEntity = groundHandle;
    REQUIRE(attacker->orders()->values.replace(explicitGround).ok());
    step.tick = eve::SimulationTick{2};
    REQUIRE(eve::rts::CombatFireSystem::step(step, state, sensing, damage).ok());
    CHECK_EQ(ecs::try_get(attacker->combat()->target), nullptr);
    CHECK(attacker->orders()->values.empty());

    REQUIRE(air->tags()->values.add("cloaked").ok());
    step.tick = eve::SimulationTick{3};
    REQUIRE(eve::rts::CombatFireSystem::step(step, state, sensing, damage).ok());
    CHECK_EQ(ecs::try_get(attacker->combat()->target), nullptr);

    weapon->release();
    dynamic_cast<Unit*>(ecs::try_get(airHandle))->release();
    dynamic_cast<Unit*>(ecs::try_get(groundHandle))->release();
    attacker->release();
    dynamic_cast<Faction*>(ecs::try_get(redHandle))->release();
    dynamic_cast<Faction*>(ecs::try_get(blueHandle))->release();
}

TEST_CASE("rts.weaponRangeFalloffScalesHitscanDamageAtConfiguredRange") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto  blueHandle     = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002d1")));
    auto  redHandle      = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002d2")));
    auto  attackerHandle = ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002d3")));
    auto  targetHandle   = ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002d4")));
    auto* attacker       = dynamic_cast<Unit*>(ecs::try_get(attackerHandle));
    auto* target         = dynamic_cast<Unit*>(ecs::try_get(targetHandle));
    auto  blueLink       = eve::rts::FactionLink::bind(blueHandle);
    auto  redLink        = eve::rts::FactionLink::bind(redHandle);
    REQUIRE(blueLink.ok());
    REQUIRE(redLink.ok());
    attacker->faction()->link            = std::move(blueLink).takeValue();
    target->faction()->link              = std::move(redLink).takeValue();
    attacker->combat()->acquisitionRange = 10.0f;
    target->motion()->x                  = 10.0f;
    target->durability()->state.health = target->durability()->state.maxHealth = 100.0;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                  = "falloff-rifle";
    definition.damage              = 20.0f;
    definition.range               = 10.0f;
    definition.falloffStart        = 0.0f;
    definition.minimumDamageFactor = 0.5f;
    weapon->definition()->owned    = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def      = weapon->definition()->owned.get();
    weapon->state()->stages        = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 1.0f;
    weapon->state()->resource.max   = 1.0f;
    auto weaponLink                = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    attacker->weapon()->link = std::move(weaponLink).takeValue();
    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    auto                              fired = eve::rts::CombatFireSystem::step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("falloff shot")}, state, sensing, damage);
    REQUIRE(fired.ok());
    CHECK_EQ(fired.value(), 1u);
    CHECK(std::abs(target->durability()->state.health - 90.0) < 1e-5);

    weapon->release();
    target->release();
    attacker->release();
    dynamic_cast<Faction*>(ecs::try_get(redHandle))->release();
    dynamic_cast<Faction*>(ecs::try_get(blueHandle))->release();
}

TEST_CASE("rts.combatWaitsForTurretToTraverseBeforeFiring") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto  blueHandle     = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002b1")));
    auto  redHandle      = ecs::handle_of(Faction::createFaction(subject("00000000-0000-7000-8000-0000000002b2")));
    auto  attackerHandle = ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002b3")));
    auto  targetHandle   = ecs::handle_of(Unit::createUnit(subject("00000000-0000-7000-8000-0000000002b4")));
    auto* blue           = dynamic_cast<Faction*>(ecs::try_get(blueHandle));
    auto* red            = dynamic_cast<Faction*>(ecs::try_get(redHandle));
    auto* attacker       = dynamic_cast<Unit*>(ecs::try_get(attackerHandle));
    auto* target         = dynamic_cast<Unit*>(ecs::try_get(targetHandle));
    auto  blueLink       = eve::rts::FactionLink::bind(blueHandle);
    auto  redLink        = eve::rts::FactionLink::bind(redHandle);
    REQUIRE(blueLink.ok());
    REQUIRE(redLink.ok());
    attacker->faction()->link               = std::move(blueLink).takeValue();
    target->faction()->link                 = std::move(redLink).takeValue();
    attacker->combat()->acquisitionRange    = 10.0f;
    attacker->combat()->turnRateDegrees     = 90.0f;
    attacker->combat()->aimToleranceDegrees = 1.0f;
    target->motion()->x                     = -5.0f;
    target->durability()->state.health = target->durability()->state.maxHealth = 100.0;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "traversing-rifle";
    definition.damage           = 10.0f;
    definition.range            = 10.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    weapon->state()->stages     = &weapon->definition()->def->stages;
    weapon->state()->resource.kind  = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 1.0f;
    weapon->state()->resource.max   = 1.0f;
    auto weaponLink             = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    attacker->weapon()->link = std::move(weaponLink).takeValue();

    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    auto                              step =
        eve::SimulationStep{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("turret traversal step")};
    auto first = eve::rts::CombatFireSystem::step(step, state, sensing, damage);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), 0u);
    CHECK(std::abs(std::abs(weapon->aim()->yaw) - 90.0f) < 1e-5f);
    CHECK(std::abs(target->durability()->state.health - 100.0) < 1e-5);

    step.tick   = eve::SimulationTick{2};
    auto second = eve::rts::CombatFireSystem::step(step, state, sensing, damage);
    REQUIRE(second.ok());
    CHECK_EQ(second.value(), 1u);
    CHECK(std::abs(target->durability()->state.health - 90.0) < 1e-5);

    weapon->release();
    target->release();
    attacker->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.facadeHealsCombatRootsAndAppliesCanonicalStatusDefinitions") {
    ecs::Table                           world;
    ecs::ScopedTable                     guard(world);
    eve::rts::RTS                        rts;
    eve::definitions::DefinitionRegistry definitions;
    const std::string                    content = R"json({
      "statusEffects":[{
        "id":"field_repair","duration":6,"speedMultiplier":0.8,
        "damageMultiplier":1.1,"incomingDamageMultiplier":0.9,"healingPerSecond":3
      }]
    })json";
    REQUIRE(rts.loadContent(definitions, content).ok());
    const auto sourceSubject   = subject("00000000-0000-7000-8000-0000000002f1");
    const auto targetSubject   = subject("00000000-0000-7000-8000-0000000002f2");
    const auto buildingSubject = subject("00000000-0000-7000-8000-0000000002f3");
    auto       sourceCreated   = rts.newUnit(sourceSubject);
    auto       targetCreated   = rts.newUnit(targetSubject);
    auto       buildingCreated = rts.newBuilding(buildingSubject);
    REQUIRE(sourceCreated.ok());
    REQUIRE(targetCreated.ok());
    REQUIRE(buildingCreated.ok());
    auto* target                           = targetCreated.value();
    auto* building                         = buildingCreated.value();
    target->durability()->state.health     = 35.0;
    target->durability()->state.maxHealth  = 50.0;
    building->integrity()->state.health    = 70.0;
    building->integrity()->state.maxHealth = 100.0;

    auto unitHealing     = rts.heal(sourceSubject, targetSubject, 100.0);
    auto buildingHealing = rts.heal(sourceSubject, buildingSubject, 12.0);
    REQUIRE(unitHealing.ok());
    REQUIRE(buildingHealing.ok());
    CHECK_EQ(unitHealing.value(), 15.0);
    CHECK_EQ(buildingHealing.value(), 12.0);
    CHECK_EQ(target->durability()->state.health, 50.0);
    CHECK_EQ(building->integrity()->state.health, 82.0);
    CHECK(!rts.heal(sourceSubject, targetSubject, -1.0).ok());
    CHECK_EQ(target->durability()->state.health, 50.0);

    auto applied = rts.applyStatusEffect(sourceSubject, targetSubject, "field_repair", 2.5);
    REQUIRE(applied.ok());
    CHECK(applied.value().isValid());
    CHECK_EQ(target->effects()->values.count(), 1u);
    CHECK(std::abs(target->effects()->values.multiplier("speedMultiplier") - 0.8) < 1e-6);
    CHECK(std::abs(target->effects()->values.additive("healingPerSecond") - 3.0) < 1e-6);
    auto missing = rts.applyStatusEffect(sourceSubject, targetSubject, "missing", -1.0);
    CHECK(!missing.ok());
    CHECK_EQ(target->effects()->values.count(), 1u);

    auto snapshot = rts.snapshotState();
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().units.back().effects.effects.effectCount(), 1);
    auto inspected = rts.inspectState().toJson();
    REQUIRE(inspected.ok());
    CHECK(inspected.value().find("\"activeEffects\":1") != std::string::npos);
}
}  // namespace test_rts_combat_3

// Contract section: rts composition
#include <bit>
#include "PrivateTestScope.h"

// rts_composition.cpp
namespace test_rts_composition {
TEST_CASE("rts.compositionRootsAndBatchViews") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Unit*     unit               = Unit::createUnit();
    Building* building           = Building::createBuilding();
    unit->motion()->x            = 10.0f;
    building->placement()->cellX = 4;

    int  units    = 0;
    auto unitView = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders>();
    for (auto it = unitView.begin(); it != unitView.end(); ++it) {
        auto [identity, motion, orders] = *it;
        CHECK(identity != nullptr);
        CHECK(motion != nullptr);
        CHECK(orders != nullptr);
        ++units;
    }
    CHECK_EQ(units, 1);

    int  buildings    = 0;
    auto buildingView = ecs::View<Building, Building::Identity, Building::Placement>();
    for (auto it = buildingView.begin(); it != buildingView.end(); ++it) {
        auto [identity, placement] = *it;
        CHECK(identity != nullptr);
        CHECK(placement != nullptr);
        ++buildings;
    }
    CHECK_EQ(buildings, 1);

    unit->release();
    building->release();
}

TEST_CASE("rts.baseViewIncludesSubclassButNotOtherRoot") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Unit*  unit  = Unit::createUnit();
    Scout* scout = Scout::create();
    initializeScout(*scout);
    Building* building = Building::createBuilding();

    int  allUnits = 0;
    auto baseView = ecs::View<Unit, Unit::Identity, Unit::Motion>();
    for (auto it = baseView.begin(); it != baseView.end(); ++it) {
        auto [identity, motion] = *it;
        CHECK(identity != nullptr);
        CHECK(motion != nullptr);
        ++allUnits;
    }
    CHECK_EQ(allUnits, 2);

    int  exactScouts = 0;
    auto scoutView   = ecs::View<Scout, Unit::Identity, Unit::Motion>();
    for (auto it = scoutView.begin(); it != scoutView.end(); ++it) ++exactScouts;
    CHECK_EQ(exactScouts, 1);

    int  unitOrderCount = 0;
    auto orderView      = ecs::View<Unit, Unit::Orders>();
    for (auto it = orderView.begin(); it != orderView.end(); ++it) ++unitOrderCount;
    CHECK_EQ(unitOrderCount, 2);

    int  buildingAsUnit = 0;
    auto noBuildingView = ecs::View<Unit, Unit::Identity, Unit::Motion>();
    for (auto it = noBuildingView.begin(); it != noBuildingView.end(); ++it) {
        auto [identity, motion] = *it;
        (void)identity;
        (void)motion;
        ++buildingAsUnit;
    }
    CHECK_EQ(buildingAsUnit, 2);

    unit->release();
    scout->release();
    building->release();
}

TEST_CASE("rts.typedLinkDetectsStaleTarget") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Faction* faction = Faction::createFaction();
    Unit*    unit    = Unit::createUnit();
    auto     linked  = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(linked.ok());
    unit->faction()->link = std::move(linked).takeValue();
    CHECK(unit->faction()->link.isBound());
    CHECK(!unit->faction()->link.isStale());
    CHECK(unit->faction()->link.resolve() == faction);

    faction->release();
    CHECK(unit->faction()->link.isStale());
    CHECK(unit->faction()->link.resolve() == nullptr);

    unit->release();
}

TEST_CASE("rts.exactOverlapPublishesShortDeterministicMotion") {
    auto simulate = [&] {
        ecs::Table        world;
        ecs::ScopedTable  guard(world);
        eve::crowd::Crowd crowd;
        crowd.setClampToField(false);
        crowd.setResolveOverlaps(true);
        crowd.setSeparationRadius(2.0f);
        crowd.setSeparationWeight(1.0f);
        std::vector<Unit*> units;
        units.reserve(4);
        for (int index = 0; index < 4; ++index) {
            Unit* unit = Unit::createUnit();
            REQUIRE(unit != nullptr);
            unit->motion()->x = 0.f;
            unit->motion()->y = 0.f;
            auto  link = eve::rts::CrowdLink::bind("rts/overlap/" + std::to_string(index));
            REQUIRE(link.ok());
            unit->crowd()->link   = std::move(link).takeValue();
            unit->crowd()->radius = 0.45f;
            unit->motion()->speed = 4.0f;
            CommandSpec move;
            move.kind   = OrderKind::Move;
            move.target = {20.0f, 10.0f};
            REQUIRE(unit->orders()->values.enqueue(move).ok());
            units.push_back(unit);
        }
        for (std::uint64_t tick = 1; tick <= 8; ++tick) {
            const eve::SimulationStep step{eve::SimulationTick{tick},
                                           eve::Duration::fromSeconds(1.0 / 120.0).expect("short crowd dt")};
            const auto                advanced = eve::rts::CrowdMotionSystem::step(step, crowd);
            REQUIRE(advanced.ok());
            REQUIRE_EQ(advanced.value(), units.size());
        }
        std::vector<WorldPosition> positions;
        positions.reserve(units.size());
        for (Unit* unit : units) positions.push_back({unit->motion()->x, unit->motion()->y});
        for (Unit* unit : units) unit->release();
        return positions;
    };

    const auto first  = simulate();
    const auto second = simulate();
    REQUIRE_EQ(first.size(), second.size());
    REQUIRE_EQ(first.size(), size_t(4));
    float forwardProjection = 0.f;
    for (std::size_t index = 0; index < first.size(); ++index) {
        REQUIRE(std::isfinite(first[index].x));
        REQUIRE(std::isfinite(first[index].y));
        REQUIRE_EQ(std::bit_cast<uint32_t>(first[index].x), std::bit_cast<uint32_t>(second[index].x));
        REQUIRE_EQ(std::bit_cast<uint32_t>(first[index].y), std::bit_cast<uint32_t>(second[index].y));
        forwardProjection += first[index].x * 20.f + first[index].y * 10.f;
        for (std::size_t other = 0; other < index; ++other) {
            const float dx = first[index].x - first[other].x;
            const float dy = first[index].y - first[other].y;
            // All pair distances start at exactly zero. Positive distance proves
            // the overlap was unlocked, without demanding long-distance travel.
            REQUIRE(dx * dx + dy * dy > 0.f);
        }
    }
    // The initial projection is exactly zero. Positive net motion toward the
    // common (20,10) order observes the RTS -> crowd -> Unit publication path.
    REQUIRE_GT(forwardProjection, 0.f);
}

TEST_CASE("rts.linkedAirDefensesFormStableNetworksAndDistributeAircraft") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue         = Faction::createFaction(subject("00000000-0000-7000-8000-000000000061"));
    Faction*         red          = Faction::createFaction(subject("00000000-0000-7000-8000-000000000062"));
    Building*        first        = Building::createBuilding(subject("00000000-0000-7000-8000-000000000063"));
    Building*        second       = Building::createBuilding(subject("00000000-0000-7000-8000-000000000064"));
    Unit*            left         = Unit::createUnit(subject("00000000-0000-7000-8000-000000000065"));
    Unit*            right        = Unit::createUnit(subject("00000000-0000-7000-8000-000000000066"));
    auto             bindBuilding = [&](Building& building) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(blue));
        REQUIRE(link.ok());
        building.faction()->link = std::move(link).takeValue();
    };
    auto bindAircraft = [&](Unit& unit) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(red));
        REQUIRE(link.ok());
        unit.faction()->link            = std::move(link).takeValue();
        unit.motion()->airborne         = true;
        unit.durability()->state.health = unit.durability()->state.maxHealth = 100.0;
    };
    bindBuilding(*first);
    bindBuilding(*second);
    bindAircraft(*left);
    bindAircraft(*right);
    first->placement()->worldX  = 0.0f;
    second->placement()->worldX = 6.0f;
    left->motion()->x           = 3.0f;
    left->motion()->y           = 4.0f;
    right->motion()->x          = 7.0f;
    right->motion()->y          = 4.0f;
    std::vector<eve::weapon::WeaponEntity*> weapons;
    for (Building* defense : {first, second}) {
        auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id               = "network-sam";
        definition.damage           = 10.0f;
        definition.range            = 20.0f;
        definition.targetsGround    = false;
        definition.targetsAir       = true;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def   = weapon->definition()->owned.get();
        weapon->state()->stages     = &weapon->definition()->def->stages;
        auto link                   = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        defense->weapon()->link                   = std::move(link).takeValue();
        defense->combat()->acquisitionRange       = 20.0f;
        defense->combat()->airDefenseNetworkRange = 8.0f;
        weapons.push_back(weapon);
    }

    auto assigned = eve::rts::TacticsSystem::step();
    REQUIRE(assigned.ok());
    CHECK_EQ(first->combat()->airDefenseNetworkSize, 2u);
    CHECK_EQ(second->combat()->airDefenseNetworkSize, 2u);
    CHECK_EQ(ecs::try_get(first->combat()->airDefenseNetworkRoot), first);
    CHECK_EQ(ecs::try_get(second->combat()->airDefenseNetworkRoot), first);
    CHECK(ecs::try_get(first->combat()->target) != nullptr);
    CHECK(ecs::try_get(second->combat()->target) != nullptr);
    CHECK_NE(ecs::try_get(first->combat()->target), ecs::try_get(second->combat()->target));

    for (auto* weapon : weapons) weapon->release();
    first->release();
    second->release();
    left->release();
    right->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.convoyFlanksAtomicallyHandoffCrossingThreats") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue   = Faction::createFaction();
    Faction*         red    = Faction::createFaction();
    Unit*            leader = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f1"));
    Unit*            tail   = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f2"));
    Unit*            left   = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f3"));
    Unit*            right  = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f4"));
    Unit*            upper  = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f5"));
    Unit*            lower  = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f6"));
    auto             bind   = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    for (Unit* unit : {leader, tail, left, right}) bind(*unit, *blue);
    bind(*upper, *red);
    bind(*lower, *red);
    leader->motion()->x            = 4.0f;
    tail->motion()->x              = 0.0f;
    leader->supply()->convoyLeader = leader->identity()->self;
    tail->supply()->convoyLeader   = leader->identity()->self;
    CommandSpec mission;
    mission.kind         = OrderKind::SupplyRelay;
    mission.targetEntity = tail->identity()->self;
    mission.target       = {20.0f, 0.0f};
    REQUIRE(leader->orders()->values.replace(mission).ok());
    left->tactics()->escortOffsetY  = 3.0f;
    right->tactics()->escortOffsetY = -3.0f;
    for (Unit* escort : {left, right}) {
        escort->tactics()->protectionRange = 20.0f;
        CommandSpec order;
        order.kind         = OrderKind::Escort;
        order.targetEntity = leader->identity()->self;
        REQUIRE(escort->orders()->values.replace(order).ok());
    }
    upper->motion()->x = lower->motion()->x = 2.0f;
    upper->motion()->y                      = 7.0f;
    lower->motion()->y                      = -7.0f;
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK_EQ(ecs::try_get(left->combat()->target), upper);
    CHECK_EQ(ecs::try_get(right->combat()->target), lower);
    CHECK_EQ(left->tactics()->escortHandoffCount, 0u);
    CHECK_EQ(right->tactics()->escortHandoffCount, 0u);

    upper->motion()->y = -7.0f;
    lower->motion()->y = 7.0f;
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    CHECK_EQ(ecs::try_get(left->combat()->target), lower);
    CHECK_EQ(ecs::try_get(right->combat()->target), upper);
    CHECK(left->tactics()->escortSectorMatched);
    CHECK(right->tactics()->escortSectorMatched);
    CHECK_EQ(left->tactics()->escortHandoffCount, 1u);
    CHECK_EQ(right->tactics()->escortHandoffCount, 1u);
    CHECK_EQ(left->tactics()->escortInterceptTarget, lower->identity()->subject);
    CHECK_EQ(right->tactics()->escortInterceptTarget, upper->identity()->subject);

    lower->release();
    upper->release();
    right->release();
    left->release();
    tail->release();
    leader->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.coordinatedVolleyHoldsAndReleasesGroupWhileExplicitAttackBypassesSchedule") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue   = Faction::createFaction(subject("00000000-0000-7000-8000-000000000321"));
    Faction*         red    = Faction::createFaction(subject("00000000-0000-7000-8000-000000000322"));
    Unit*            first  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000323"));
    Unit*            second = Unit::createUnit(subject("00000000-0000-7000-8000-000000000324"));
    Unit*            target = Unit::createUnit(subject("00000000-0000-7000-8000-000000000325"));
    auto             bind   = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*first, *blue);
    bind(*second, *blue);
    bind(*target, *red);
    target->motion()->x                = 6.0f;
    target->durability()->state.health = target->durability()->state.maxHealth = 100.0;
    std::vector<eve::weapon::WeaponEntity*> weapons;
    for (Unit* shooter : {first, second}) {
        auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id               = "volley-shell";
        definition.damage           = 10.0f;
        definition.range            = 12.0f;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def   = weapon->definition()->owned.get();
        weapon->state()->stages     = &weapon->definition()->def->stages;
        auto link                   = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        shooter->weapon()->link                       = std::move(link).takeValue();
        shooter->combat()->acquisitionRange           = 12.0f;
        shooter->tactics()->combatGroup               = 19;
        shooter->tactics()->coordinatedVolleyInterval = 0.5f;
        weapons.push_back(weapon);
    }
    eve::sensing::SensingWorld        sensing;
    eve::combat::DamageRuntime        damage;
    eve::rts::CombatFireSystem::State state;
    auto step = eve::SimulationStep{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("volley step")};
    for (int index = 0; index < 4; ++index) {
        REQUIRE(eve::rts::TacticsSystem::step().ok());
        auto fired = eve::rts::CombatFireSystem::step(step, state, sensing, damage);
        REQUIRE(fired.ok());
        CHECK_EQ(fired.value(), 0u);
        step.tick = eve::SimulationTick{step.tick.value() + 1};
    }
    CHECK(first->tactics()->volleyHolding);
    CHECK(second->tactics()->volleyHolding);
    CHECK(first->tactics()->volleyReleaseRemaining <= 0.11f);
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    auto released = eve::rts::CombatFireSystem::step(step, state, sensing, damage);
    REQUIRE(released.ok());
    CHECK_EQ(released.value(), 2u);
    CHECK(!first->tactics()->volleyHolding);
    CHECK(!second->tactics()->volleyHolding);
    CHECK(std::abs(target->durability()->state.health - 80.0) < 1e-5);

    CommandSpec explicitAttack;
    explicitAttack.kind         = OrderKind::Attack;
    explicitAttack.targetEntity = ecs::handle_of(target);
    REQUIRE(first->orders()->values.replace(explicitAttack).ok());
    step.tick = eve::SimulationTick{step.tick.value() + 1};
    REQUIRE(eve::rts::TacticsSystem::step().ok());
    auto explicitShot = eve::rts::CombatFireSystem::step(step, state, sensing, damage);
    REQUIRE(explicitShot.ok());
    CHECK_EQ(explicitShot.value(), 1u);
    CHECK(std::abs(target->durability()->state.health - 70.0) < 1e-5);

    for (auto* weapon : weapons) weapon->release();
    first->release();
    second->release();
    target->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.actionAdapterReusesSharedActionRuntime") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Unit* unit            = Unit::createUnit();
    unit->motion()->speed = 10.0f;
    const CommandSpec move{OrderKind::Move, {20.0f, 0.0f}, {}, 0, 0.0};
    auto              order = unit->orders()->values.enqueue(move);
    REQUIRE(order.ok());
    std::move(order).takeValue();

    eve::action::ActionRuntime runtime;
    eve::rts::ActionAdapter    adapter(runtime);
    const eve::SimulationStep  first{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("first dt")};
    auto                       moved = eve::rts::MotionSystem::step(first);
    REQUIRE(moved.ok());
    std::move(moved).takeValue();
    auto pending = eve::rts::OrderActionSystem::step(first, adapter);
    REQUIRE(pending.ok());
    std::move(pending).takeValue();
    CHECK(!unit->orders()->values.empty());
    CHECK_EQ(runtime.executionCount(), 1u);

    const eve::SimulationStep second{eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("second dt")};
    auto                      completed = eve::rts::MotionSystem::step(second);
    REQUIRE(completed.ok());
    std::move(completed).takeValue();
    auto action = eve::rts::OrderActionSystem::step(second, adapter);
    REQUIRE(action.ok());
    std::move(action).takeValue();
    CHECK(unit->orders()->values.empty());

    unit->release();
}

TEST_CASE("rts.destroyedContainerCleanupCascadesOccupantsAndRunsAtStepBoundary") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    auto             faction = module.newFaction(subject("00000000-0000-7000-8000-00000000d111"));
    REQUIRE(faction.ok());
    auto building = module.newFactionBuilding(*faction.value(), subject("00000000-0000-7000-8000-00000000d112"));
    auto occupant = module.newFactionUnit(*faction.value(), subject("00000000-0000-7000-8000-00000000d113"));
    REQUIRE(building.ok());
    REQUIRE(occupant.ok());
    auto container = eve::rts::ContainerLink::bind(ecs::handle_of(building.value()));
    REQUIRE(container.ok());
    occupant.value()->containment()->container = std::move(container).takeValue();
    building.value()->garrison()->occupants.push_back(ecs::handle_of(occupant.value()));
    building.value()->capture()->blockedByGarrison = true;
    building.value()->integrity()->alive           = false;
    building.value()->integrity()->state.health    = 0.0;

    eve::action::ActionRuntime runtime;
    eve::rts::ActionAdapter    adapter(runtime);
    const eve::SimulationStep  simulationStep{eve::SimulationTick{1},
                                             eve::Duration::fromSeconds(0.1).expect("cleanup dt")};
    auto                       stepped = module.step(simulationStep, adapter);
    REQUIRE(stepped.ok());
    CHECK(module.findBuilding(subject("00000000-0000-7000-8000-00000000d112")) == nullptr);
    CHECK(module.findUnit(subject("00000000-0000-7000-8000-00000000d113")) == nullptr);
    CHECK_EQ(module.buildingCount(), 0u);
    CHECK_EQ(module.unitCount(), 0u);
}

TEST_CASE("rts.mobileSupplierTracksTheRecipientsLivePosition") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction   = Faction::createFaction(subject("00000000-0000-7000-8000-000000000391"));
    Unit*            supplier  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000392"));
    Unit*            recipient = Unit::createUnit(subject("00000000-0000-7000-8000-000000000393"));
    for (Unit* unit : {supplier, recipient}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        unit->faction()->link = std::move(link).takeValue();
    }
    supplier->motion()->speed = 5.0f;
    supplier->supply()->stock = supplier->supply()->capacity = 4.0f;
    supplier->supply()->range                                = 1.0f;
    supplier->supply()->transferRate                         = 2.0f;
    supplier->supply()->autoDispatch                         = true;
    recipient->motion()->x                                   = 10.0f;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                     = "moving-recipient-rifle";
    definition.magSize                = 4;
    weapon->definition()->owned       = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def         = weapon->definition()->owned.get();
    weapon->state()->resource.kind    = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value   = 0.0f;
    weapon->state()->resource.max     = 4.0f;
    weapon->state()->resource.reserve = -1;
    auto weaponLink                   = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    recipient->weapon()->link = std::move(weaponLink).takeValue();

    const eve::SimulationStep noTime{eve::SimulationTick{1},
                                     eve::Duration::fromSeconds(0.0).expect("zero dispatch dt")};
    REQUIRE(eve::rts::SupplySystem::step(noTime).ok());
    CHECK_EQ(ecs::try_get(supplier->supply()->assignedTarget), recipient);
    recipient->motion()->x = 0.0f;
    recipient->motion()->y = 10.0f;
    const eve::SimulationStep movement{eve::SimulationTick{2},
                                       eve::Duration::fromSeconds(1.0).expect("supplier movement dt")};
    REQUIRE(eve::rts::MotionSystem::step(movement).ok());
    CHECK(std::abs(supplier->motion()->x) < 1e-5f);
    CHECK(supplier->motion()->y > 4.9f);

    weapon->release();
    supplier->release();
    recipient->release();
    faction->release();
}

TEST_CASE("rts.moduleResolvesStableSubjectsAndRejectsCrossRootDuplicates") {
    eve::rts::RTS module;
    REQUIRE(module.configureScriptWorld(16, 16, 1.0f).ok());
    REQUIRE(module.setScriptNavigationBlocked(8, 8, true).ok());
    REQUIRE(module.setScriptNavigationCost(3, 3, 0.5f).ok());
    const auto stable         = subject("00000000-0000-7000-8000-000000000071");
    const auto factionSubject = subject("00000000-0000-7000-8000-000000000072");
    auto       factionResult  = module.newFaction(factionSubject);
    REQUIRE(factionResult.ok());
    Faction* faction = std::move(factionResult).takeValue();
    REQUIRE(module.addScriptResource(*faction, "minerals", 300).ok());
    auto minerals = module.scriptResource(*faction, "minerals");
    REQUIRE(minerals.ok());
    CHECK_EQ(minerals.value(), 300);
    CHECK_EQ(module.findFaction(factionSubject), faction);
    CHECK_EQ(module.findFaction(stable), nullptr);
    auto unitResult = module.newFactionUnit(*faction, stable);
    REQUIRE(unitResult.ok());
    Unit* unit = std::move(unitResult).takeValue();
    CHECK_EQ(faction->members()->units.size(), 1u);
    CHECK_EQ(ecs::try_get(faction->members()->units.front()), unit);
    CHECK(unit->crowd()->link.isBound());
    CHECK(unit->sensing()->link.isBound());
    CHECK_EQ(module.findUnit(stable), unit);
    CHECK_EQ(module.findBuilding(stable), nullptr);

    auto duplicate = module.newBuilding(stable);
    CHECK(!duplicate.ok());
    CHECK_EQ(module.unitCount(), 1u);
    CHECK_EQ(module.buildingCount(), 0u);

    eve::rts::CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {4.0f, 7.0f};
    const std::array selected{stable};
    auto             commanded = module.commandUnits(selected, move, {eve::rts::FormationKind::Grid, 1.0f, 0});
    REQUIRE(commanded.ok());
    CHECK_EQ(commanded.value().accepted, 1u);
    auto active = unit->orders()->values.current();
    REQUIRE(active.ok());
    CHECK_EQ(active.value().target.x, 4.0f);
    CHECK_EQ(active.value().target.y, 7.0f);
    auto stepped = module.stepScript(0.5);
    REQUIRE(stepped.ok());
    CHECK(unit->motion()->x > 0.0f);
    CHECK(unit->motion()->y > 0.0f);

    const std::array missing{subject("00000000-0000-7000-8000-000000000073")};
    auto             rejected = module.commandUnits(missing, move);
    CHECK(!rejected.ok());
    CHECK_EQ(unit->orders()->values.orderCount(), 1u);

    auto inspected = module.inspectState().toJson();
    REQUIRE(inspected.ok());
    CHECK(inspected.value().find(stable.format()) != std::string::npos);
    CHECK(inspected.value().find(factionSubject.format()) != std::string::npos);
}

TEST_CASE("rts.lifecycleTransitionsProjectThroughFrameEventsAndClearNextStep") {
    class PendingExecutor final : public eve::rts::IRTSActionExecutor {
    public:
        eve::Result<eve::rts::ActionExecutionResult> execute(Unit&, const eve::rts::OrderRecord&,
                                                             const eve::SimulationStep&) override {
            return eve::Result<eve::rts::ActionExecutionResult>::success({eve::rts::ActionDisposition::Pending});
        }
    } executor;

    eve::rts::RTS module;
    auto          created = module.newUnit(subject("00000000-0000-7000-8000-00000000ec01"));
    REQUIRE(created.ok());
    Unit* unit                = std::move(created).takeValue();
    unit->shield()->capacity  = 10.0f;
    unit->shield()->value     = 9.0f;
    unit->shield()->regenRate = 2.0f;
    REQUIRE(
        module
            .step({eve::SimulationTick{7}, eve::Duration::fromSeconds(1.0).expect("lifecycle projection dt")}, executor)
            .ok());
    const auto  frameEvents = module.inspectFrameEvents();
    const auto* events      = frameEvents.getIf<eve::Value::Array>();
    REQUIRE(events != nullptr);
    REQUIRE_EQ(events->size(), 1u);
    const auto* projected = events->front().getIf<eve::Value::Object>();
    REQUIRE(projected != nullptr);
    CHECK_EQ(*projected->at("type").getIf<std::string>(), "shield_recharged");
    CHECK_EQ(*projected->at("source").getIf<std::string>(), unit->identity()->subject.format());
    CHECK_EQ(*projected->at("tick").getIf<std::int64_t>(), std::int64_t{7});

    REQUIRE(
        module.step({eve::SimulationTick{8}, eve::Duration::fromSeconds(0.1).expect("lifecycle clear dt")}, executor)
            .ok());
    const auto  clearedEvents = module.inspectFrameEvents();
    const auto* cleared       = clearedEvents.getIf<eve::Value::Array>();
    REQUIRE(cleared != nullptr);
    CHECK(cleared->empty());
}

TEST_CASE("rts.fogUsesCanonicalFovForTeamSharingAndLastKnownContacts") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         alpha     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000101"));
    Faction*         ally      = Faction::createFaction(subject("00000000-0000-7000-8000-000000000102"));
    Faction*         enemy     = Faction::createFaction(subject("00000000-0000-7000-8000-000000000103"));
    Unit*            scout     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000104"));
    Unit*            target    = Unit::createUnit(subject("00000000-0000-7000-8000-000000000105"));
    auto             alphaLink = eve::rts::FactionLink::bind(ecs::handle_of(alpha));
    auto             enemyLink = eve::rts::FactionLink::bind(ecs::handle_of(enemy));
    REQUIRE(alphaLink.ok());
    REQUIRE(enemyLink.ok());
    scout->faction()->link      = std::move(alphaLink).takeValue();
    target->faction()->link     = std::move(enemyLink).takeValue();
    scout->motion()->x          = 1.0f;
    scout->motion()->y          = 1.0f;
    scout->vision()->sightRange = 5.0f;
    target->motion()->x         = 3.0f;
    target->motion()->y         = 1.0f;

    eve::map::Fov                   shared(10, 10);
    eve::map::Fov                   hostile(10, 10);
    eve::rts::FogOfWarSystem::State fog;
    const auto provider = [&](Faction& faction) -> eve::map::Fov* { return &faction == enemy ? &hostile : &shared; };
    const eve::SimulationStep first{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("fog dt")};
    auto                      revealed = eve::rts::FogOfWarSystem::step(first, {}, fog, provider);
    REQUIRE(revealed.ok());
    CHECK(shared.isVisible(3, 1));
    const auto* alphaContact = eve::rts::FogOfWarSystem::contact(*alpha, target->identity()->subject);
    const auto* allyContact  = eve::rts::FogOfWarSystem::contact(*ally, target->identity()->subject);
    REQUIRE(alphaContact != nullptr);
    REQUIRE(allyContact != nullptr);
    CHECK(alphaContact->visible);
    CHECK(allyContact->visible);

    target->motion()->x = 9.0f;
    target->motion()->y = 9.0f;
    const eve::SimulationStep second{eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("fog age")};
    auto                      hidden = eve::rts::FogOfWarSystem::step(second, {}, fog, provider);
    REQUIRE(hidden.ok());
    CHECK(shared.isExplored(3, 1));
    alphaContact = eve::rts::FogOfWarSystem::contact(*alpha, target->identity()->subject);
    REQUIRE(alphaContact != nullptr);
    CHECK(!alphaContact->visible);
    CHECK(std::abs(alphaContact->ageSeconds - 1.0) < 1e-6);
    CHECK(std::abs(alphaContact->position.x - 3.0f) < 1e-5f);

    eve::rts::FogOfWarSystem::clear(fog);
    target->release();
    scout->release();
    enemy->release();
    ally->release();
    alpha->release();
}

TEST_CASE("rts.contentPackPublishesAtomicallyToCanonicalDefinitions") {
    eve::definitions::DefinitionRegistry registry;
    const std::string                    pack   = R"JSON({
      "weapons":[{"id":"rifle","damage":8,"range":5,"cooldown":0.5,"magazineSize":6,"reloadTime":2,
        "falloffStart":2,"minimumDamageFactor":0.4,"splashMinimumDamageFactor":0.2,
        "accuracy":0.75,"scatterRadius":1.5,
        "targetsGround":false,"targetsAir":true,"requiredTargetTags":["armored"],
        "preferredTargetTags":["biological"],"preferredTargetBonus":3.5,
        "excludedTargetTags":["cloaked"],"friendlyFire":true,"blockedByObstacles":true}],
      "buildings":[{"id":"barracks","health":700}],
      "units":[{"id":"marine","weaponType":"rifle","producer":"barracks","health":80}],
      "upgrades":[{"id":"weapons_1","producer":"barracks","targetUnit":"marine","attackMultiplier":1.2}]
    })JSON";
    auto                                 loaded = eve::rts::RTSContentLoader::load(registry, pack);
    REQUIRE(loaded.ok());
    CHECK_EQ(loaded.value().inserted, 4u);
    CHECK_EQ(registry.countType("unit"), 1);
    CHECK_EQ(registry.countType("building"), 1);
    auto weapon = registry.resolve("weapon", "rifle");
    REQUIRE(weapon.ok());
    auto typed = eve::weapon::parseWeaponDefinition(weapon.value().get());
    REQUIRE(typed.ok());
    CHECK_EQ(typed.value().magSize, 6);
    CHECK(!typed.value().targetsGround);
    CHECK(typed.value().targetsAir);
    CHECK(typed.value().friendlyFire);
    CHECK(typed.value().blockedByObstacles);
    CHECK_EQ(typed.value().falloffStart, 2.0f);
    CHECK_EQ(typed.value().minimumDamageFactor, 0.4f);
    CHECK_EQ(typed.value().splashMinimumDamageFactor, 0.2f);
    CHECK_EQ(typed.value().accuracy, 0.75f);
    CHECK_EQ(typed.value().scatterRadius, 1.5f);
    REQUIRE_EQ(typed.value().requiredTargetTags.size(), 1u);
    CHECK_EQ(typed.value().requiredTargetTags.front(), "armored");
    REQUIRE_EQ(typed.value().excludedTargetTags.size(), 1u);
    CHECK_EQ(typed.value().excludedTargetTags.front(), "cloaked");
    REQUIRE_EQ(typed.value().preferredTargetTags.size(), 1u);
    CHECK_EQ(typed.value().preferredTargetTags.front(), "biological");
    CHECK_EQ(typed.value().preferredTargetBonus, 3.5f);
    CHECK(std::abs(typed.value().projectile.speed) < 1e-5f);

    const std::string before = registry.snapshotJson();
    auto              rejected =
        eve::rts::RTSContentLoader::load(registry, R"JSON({"units":[{"id":"broken","weaponType":"missing"}]})JSON");
    CHECK(!rejected.ok());
    rejected.ignore("expected atomic RTS content rejection");
    CHECK_EQ(registry.snapshotJson(), before);

    auto replaced = eve::rts::RTSContentLoader::load(registry, pack);
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value().replaced, 4u);
}
}  // namespace test_rts_composition

// Contract section: rts economy
#include "PrivateTestScope.h"

// rts_economy.cpp
namespace test_rts_economy {
TEST_CASE("rts.autoAssignedWorkerGathersAndCreditsCanonicalAccount") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Unit*         worker           = Unit::createUnit();
    Building*     base             = Building::createBuilding();
    ResourceNode* node             = ResourceNode::createResourceNode();
    base->placement()->worldX      = 0.0f;
    base->placement()->worldY      = 0.0f;
    node->position()->x            = 0.0f;
    node->position()->y            = 0.0f;
    node->stock()->resourceType    = "ore";
    node->stock()->remaining       = 10.0f;
    node->stock()->maximum         = 10.0f;
    worker->worker()->resourceType = "ore";
    worker->worker()->capacity     = 2.0f;
    worker->worker()->gatherRate   = 2.0f;
    worker->worker()->autoAssign   = true;
    auto dropoff                   = eve::rts::BuildingLink::bind(ecs::handle_of(base));
    REQUIRE(dropoff.ok());
    worker->worker()->dropoff = std::move(dropoff).takeValue();

    auto assigned = eve::rts::WorkerAssignmentSystem::step();
    REQUIRE(assigned.ok());
    CHECK_EQ(assigned.value(), 1u);
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("mining dt")};
    auto                      moved = eve::rts::MotionSystem::step(step);
    REQUIRE(moved.ok());
    std::int64_t             creditedOre = 0;
    eve::rts::ResourceCredit credit      = [&creditedOre](Unit&, const eve::resource::CostSpec& cost) {
        creditedOre += cost.items().front().amount.value();
        return eve::Result<eve::resource::Receipt>::success(eve::resource::Receipt{});
    };
    auto gathered = eve::rts::MiningSystem::step(step, credit);
    REQUIRE(gathered.ok());
    CHECK(std::abs(worker->worker()->cargo - 2.0f) < 1e-5f);
    moved = eve::rts::MotionSystem::step(step);
    REQUIRE(moved.ok());
    auto delivered = eve::rts::MiningSystem::step(step, credit);
    REQUIRE(delivered.ok());
    CHECK_EQ(creditedOre, 2);
    CHECK(worker->worker()->cargo < 1.0f);

    worker->release();
    base->release();
    node->release();
}

TEST_CASE("rts.workerAssignmentFacadeMaintainsMiningLinksAndAutoAssignmentAtomically") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto*            faction   = rts.newFaction(subject("00000000-0000-7000-8000-0000000003a1")).value();
    auto*            worker    = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-0000000003a2")).value();
    auto*            nonWorker = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-0000000003a3")).value();
    auto*            base = rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-0000000003a4")).value();
    auto*            first =
        rts.newResourceNode(subject("00000000-0000-7000-8000-0000000003a5"), "ore", 20.0f, {4.0f, 5.0f}, 1).value();
    auto* second =
        rts.newResourceNode(subject("00000000-0000-7000-8000-0000000003a6"), "ore", 20.0f, {7.0f, 8.0f}, 1).value();
    worker->worker()->resourceType = "ore";
    worker->worker()->capacity     = 5.0f;
    worker->worker()->gatherRate   = 1.0f;
    base->dropoff()->acceptedResources.push_back("ore");

    REQUIRE(rts.assignWorker(*worker, *first, *base).ok());
    CHECK(worker->worker()->resourceNode.resolve() == first);
    CHECK(worker->worker()->dropoff.resolve() == base);
    CHECK_EQ(first->harvest()->workers.size(), std::size_t{1});
    auto current = worker->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::Gather));

    REQUIRE(rts.assignWorker(*worker, *second, *base).ok());
    CHECK(first->harvest()->workers.empty());
    CHECK_EQ(second->harvest()->workers.size(), std::size_t{1});
    const std::vector<eve::SubjectRef> invalid{worker->identity()->subject, nonWorker->identity()->subject};
    auto                               rejected = rts.setWorkerAutoAssignment(invalid, true);
    CHECK(!rejected.ok());
    CHECK(!worker->worker()->autoAssign);
    const std::vector<eve::SubjectRef> workers{worker->identity()->subject};
    REQUIRE(rts.setWorkerAutoAssignment(workers, true).ok());
    CHECK(worker->worker()->autoAssign);
}

TEST_CASE("rts.constructionRepairAndCaptureUseTypedBuildingTargets") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Faction*  defenders    = Faction::createFaction();
    Faction*  attackers    = Faction::createFaction();
    Building* building     = Building::createBuilding();
    Unit*     builder      = Unit::createUnit();
    Unit*     capturer     = Unit::createUnit();
    auto      defenderLink = eve::rts::FactionLink::bind(ecs::handle_of(defenders));
    auto      attackerLink = eve::rts::FactionLink::bind(ecs::handle_of(attackers));
    REQUIRE(defenderLink.ok());
    REQUIRE(attackerLink.ok());
    building->faction()->link = std::move(defenderLink).takeValue();
    auto builderFaction       = eve::rts::FactionLink::bind(ecs::handle_of(defenders));
    auto capturerFaction      = eve::rts::FactionLink::bind(ecs::handle_of(attackers));
    REQUIRE(builderFaction.ok());
    REQUIRE(capturerFaction.ok());
    builder->faction()->link  = std::move(builderFaction).takeValue();
    capturer->faction()->link = std::move(capturerFaction).takeValue();

    building->construction()->progress         = 0.0f;
    building->construction()->buildTimeSeconds = 2.0f;
    builder->worker()->buildRate               = 1.0f;
    CommandSpec build;
    build.kind         = OrderKind::Build;
    build.targetEntity = ecs::handle_of(building);
    auto buildOrder    = builder->orders()->values.enqueue(build);
    REQUIRE(buildOrder.ok());
    std::move(buildOrder).takeValue();
    const eve::SimulationStep oneSecond{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("building dt")};
    auto                      construction = eve::rts::ConstructionSystem::step(oneSecond);
    REQUIRE(construction.ok());
    CHECK(std::abs(building->construction()->progress - 0.5f) < 1e-5f);
    std::vector<eve::rts::LifecycleEvent> constructionEvents;
    construction = eve::rts::ConstructionSystem::step(
        oneSecond, [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick tick) {
            CHECK_EQ(tick.value(), 1u);
            constructionEvents.push_back(event);
        });
    REQUIRE(construction.ok());
    CHECK(std::abs(building->construction()->progress - 1.0f) < 1e-5f);
    CHECK(builder->orders()->values.empty());
    REQUIRE_EQ(constructionEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(constructionEvents[0].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::ConstructionCompleted));

    building->integrity()->state.subject       = subject("00000000-0000-7000-8000-000000000021");
    building->integrity()->state.health        = 8.0;
    building->integrity()->state.maxHealth     = 10.0;
    building->integrity()->repairCostPerHealth = 0.5f;
    building->integrity()->repairResource      = "ore";
    builder->worker()->repairRate              = 2.0f;
    CommandSpec repair;
    repair.kind         = OrderKind::Repair;
    repair.targetEntity = ecs::handle_of(building);
    auto repairOrder    = builder->orders()->values.enqueue(repair);
    REQUIRE(repairOrder.ok());
    std::move(repairOrder).takeValue();
    std::int64_t          debited = 0;
    eve::rts::RepairDebit debit   = [&debited](Unit&, Building&, const eve::resource::CostSpec& cost) {
        debited += cost.items().front().amount.value();
        return eve::Result<eve::resource::Receipt>::success(eve::resource::Receipt{});
    };
    eve::combat::DamageRuntime settlement;
    auto                       repaired = eve::rts::RepairSystem::step(oneSecond, settlement, debit);
    REQUIRE(repaired.ok());
    CHECK(std::abs(building->integrity()->state.health - 10.0) < 1e-5);
    CHECK_EQ(debited, 1);

    building->capture()->capturable                  = true;
    building->capture()->durationSeconds             = 5.0f;
    building->rally()->enabled                       = true;
    building->rally()->combatGroup                   = 41;
    building->rally()->reinforcementCapped           = true;
    building->rally()->reinforcementPolicyPausedTask = "old-owner-task";
    building->rally()->reinforcements.push_back(builder->identity()->self);
    const auto oldRallyGroup  = building->rally()->combatGroup;
    capturer->capture()->rate = 1.0f;
    CommandSpec capture;
    capture.kind         = OrderKind::Capture;
    capture.targetEntity = ecs::handle_of(building);
    auto captureOrder    = capturer->orders()->values.enqueue(capture);
    REQUIRE(captureOrder.ok());
    std::move(captureOrder).takeValue();
    const eve::SimulationStep fiveSeconds{eve::SimulationTick{2}, eve::Duration::fromSeconds(5.0).expect("capture dt")};
    std::vector<eve::rts::LifecycleEvent> captureEvents;
    auto                                  captured = eve::rts::CaptureSystem::step(fiveSeconds,
                                                                                   [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick tick) {
                                                      CHECK_EQ(tick.value(), 2u);
                                                      captureEvents.push_back(event);
                                                  });
    REQUIRE(captured.ok());
    CHECK(building->faction()->link.resolve() == attackers);
    CHECK_NE(building->rally()->combatGroup, oldRallyGroup);
    CHECK(building->rally()->reinforcements.empty());
    CHECK(!building->rally()->reinforcementCapped);
    CHECK(building->rally()->reinforcementPolicyPausedTask.empty());
    CHECK(capturer->orders()->values.empty());
    REQUIRE_EQ(captureEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(captureEvents[0].kind), static_cast<int>(eve::rts::LifecycleEventKind::BuildingCaptured));
    CHECK(captureEvents[0].source == building->identity()->subject);
    CHECK(captureEvents[0].target == capturer->identity()->subject);

    builder->release();
    capturer->release();
    building->release();
    defenders->release();
    attackers->release();
}

TEST_CASE("rts.volleyCommitmentsBudgetShieldBeforeArmoredHealth") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    Faction*           blue = Faction::createFaction(subject("00000000-0000-7000-8000-000000000371"));
    Faction*           red  = Faction::createFaction(subject("00000000-0000-7000-8000-000000000372"));
    std::vector<Unit*> shooters;
    for (const char* id : {"00000000-0000-7000-8000-000000000373", "00000000-0000-7000-8000-000000000374",
                           "00000000-0000-7000-8000-000000000375"})
        shooters.push_back(Unit::createUnit(subject(id)));
    Unit* shielded = Unit::createUnit(subject("00000000-0000-7000-8000-000000000376"));
    Unit* reserve  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000377"));
    auto  bind     = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    for (Unit* shooter : shooters) bind(*shooter, *blue);
    bind(*shielded, *red);
    bind(*reserve, *red);
    shielded->motion()->x                = 5.0f;
    shielded->durability()->state.health = shielded->durability()->state.maxHealth = 10.0;
    shielded->shield()->value = shielded->shield()->capacity = 10.0f;
    reserve->motion()->x                                     = 7.0f;
    reserve->durability()->state.health = reserve->durability()->state.maxHealth = 100.0;

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "shield-budget-gun";
    definition.damage           = 10.0f;
    definition.range            = 12.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    for (Unit* shooter : shooters) {
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        shooter->weapon()->link             = std::move(link).takeValue();
        shooter->combat()->acquisitionRange = 12.0f;
        shooter->tactics()->combatGroup     = 31;
    }

    eve::combat::DamageRuntime damage;
    REQUIRE(eve::rts::TacticsSystem::step(&damage).ok());
    CHECK_EQ(ecs::try_get(shooters[0]->combat()->target), shielded);
    CHECK_EQ(ecs::try_get(shooters[1]->combat()->target), shielded);
    CHECK_EQ(ecs::try_get(shooters[2]->combat()->target), reserve);

    weapon->release();
    for (Unit* shooter : shooters) shooter->release();
    shielded->release();
    reserve->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.facadeRemovalRepairsRelationshipsAndPreservesExplicitBuildingEvacuation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    auto             factionResult = module.newFaction(subject("00000000-0000-7000-8000-00000000d101"));
    auto             playerResult  = module.newPlayer(subject("00000000-0000-7000-8000-00000000d102"));
    auto             nodeResult =
        module.newResourceNode(subject("00000000-0000-7000-8000-00000000d103"), "ore", 100.0f, {2.0f, 3.0f});
    REQUIRE(factionResult.ok());
    REQUIRE(playerResult.ok());
    REQUIRE(nodeResult.ok());
    Faction*      faction         = factionResult.value();
    Player*       player          = playerResult.value();
    ResourceNode* node            = nodeResult.value();
    auto          workerResult    = module.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000d104"));
    auto          transportResult = module.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000d105"));
    auto          passengerResult = module.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000d106"));
    auto          buildingResult = module.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000d107"));
    auto          occupantResult = module.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000d108"));
    REQUIRE(workerResult.ok());
    REQUIRE(transportResult.ok());
    REQUIRE(passengerResult.ok());
    REQUIRE(buildingResult.ok());
    REQUIRE(occupantResult.ok());
    Unit*     worker    = workerResult.value();
    Unit*     transport = transportResult.value();
    Unit*     passenger = passengerResult.value();
    Building* building  = buildingResult.value();
    Unit*     occupant  = occupantResult.value();

    auto nodeLink = eve::rts::ResourceNodeLink::bind(ecs::handle_of(node));
    REQUIRE(nodeLink.ok());
    worker->worker()->resourceNode = std::move(nodeLink).takeValue();
    node->harvest()->workers.push_back(ecs::handle_of(worker));
    player->selection()->units     = {ecs::handle_of(worker), ecs::handle_of(transport)};
    player->selection()->buildings = {ecs::handle_of(building)};
    auto transportLink             = eve::rts::ContainerLink::bind(ecs::handle_of(transport));
    REQUIRE(transportLink.ok());
    passenger->containment()->container = std::move(transportLink).takeValue();
    transport->containment()->occupants.push_back(ecs::handle_of(passenger));
    auto buildingLink = eve::rts::ContainerLink::bind(ecs::handle_of(building));
    REQUIRE(buildingLink.ok());
    occupant->containment()->container = std::move(buildingLink).takeValue();
    building->garrison()->occupants.push_back(ecs::handle_of(occupant));
    building->capture()->blockedByGarrison = true;
    building->placement()->worldX          = 9.0f;
    building->placement()->worldY          = 4.0f;

    REQUIRE(module.remove(transport->identity()->subject).ok());
    CHECK(module.findUnit(subject("00000000-0000-7000-8000-00000000d105")) == nullptr);
    CHECK(module.findUnit(subject("00000000-0000-7000-8000-00000000d106")) == nullptr);
    CHECK_EQ(player->selection()->units.size(), 1u);

    REQUIRE(module.remove(building->identity()->subject).ok());
    CHECK(module.findBuilding(subject("00000000-0000-7000-8000-00000000d107")) == nullptr);
    Unit* released = module.findUnit(subject("00000000-0000-7000-8000-00000000d108"));
    REQUIRE(released != nullptr);
    CHECK(!released->containment()->container.isBound());
    CHECK_EQ(released->motion()->x, 9.0f);
    CHECK_EQ(released->motion()->y, 4.0f);
    CHECK(player->selection()->buildings.empty());

    REQUIRE(module.remove(node->identity()->subject).ok());
    CHECK(!worker->worker()->resourceNode.isBound());
    CHECK(worker->orders()->values.empty());
    CHECK_EQ(module.resourceNodeCount(), 0u);
}

TEST_CASE("rts.autoSupplyReservationsAvoidDuplicateDispatchAndManualOrdersCancelMission") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction  = Faction::createFaction(subject("00000000-0000-7000-8000-000000000381"));
    Unit*            first    = Unit::createUnit(subject("00000000-0000-7000-8000-000000000382"));
    Unit*            second   = Unit::createUnit(subject("00000000-0000-7000-8000-000000000383"));
    Unit*            critical = Unit::createUnit(subject("00000000-0000-7000-8000-000000000384"));
    Unit*            waiting  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000385"));
    auto             bind     = [&](Unit& unit) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    for (Unit* unit : {first, second, critical, waiting}) bind(*unit);
    for (Unit* supplier : {first, second}) {
        supplier->supply()->stock = supplier->supply()->capacity = 10.0f;
        supplier->supply()->range                                = 2.0f;
        supplier->supply()->transferRate                         = 4.0f;
        supplier->supply()->autoDispatch                         = true;
    }
    std::vector<eve::weapon::WeaponEntity*> weapons;
    auto                                    arm = [&](Unit& unit, float rounds) {
        auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id      = "reservation-rifle";
        definition.magSize = 4;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def         = weapon->definition()->owned.get();
        weapon->state()->resource.kind    = eve::weapon::ResourceKind::Ammo;
        weapon->state()->resource.value   = rounds;
        weapon->state()->resource.max     = 4.0f;
        weapon->state()->resource.reserve = -1;
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        unit.weapon()->link = std::move(link).takeValue();
        weapons.push_back(weapon);
    };
    arm(*critical, 0.0f);
    arm(*waiting, 1.0f);
    const eve::SimulationStep noTime{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.0).expect("zero supply dt")};

    REQUIRE(eve::rts::SupplySystem::step(noTime).ok());
    CHECK_EQ(ecs::try_get(first->supply()->assignedTarget), critical);
    CHECK_EQ(ecs::try_get(second->supply()->assignedTarget), waiting);
    CHECK(std::abs(first->supply()->reservedStock - 4.0f) < 1e-5f);
    CHECK(std::abs(second->supply()->reservedStock - 3.0f) < 1e-5f);

    CommandSpec manualMove;
    manualMove.kind   = OrderKind::Move;
    manualMove.target = {-4.0f, 3.0f};
    REQUIRE(first->orders()->values.replace(manualMove).ok());
    REQUIRE(eve::rts::SupplySystem::step(noTime).ok());
    CHECK(first->supply()->assignedTarget.table == nullptr);
    CHECK(std::abs(first->supply()->reservedStock) < 1e-5f);
    CHECK(!first->supply()->returning);
    auto active = first->orders()->values.current();
    REQUIRE(active.ok());
    CHECK_EQ(static_cast<int>(active.value().kind), static_cast<int>(OrderKind::Move));

    for (auto* weapon : weapons) weapon->release();
    first->release();
    second->release();
    critical->release();
    waiting->release();
    faction->release();
}

TEST_CASE("rts.upstreamSupplyAutoDispatchesAndLeadsMovingRelay") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction  = Faction::createFaction(subject("00000000-0000-7000-8000-00000000039a"));
    Unit*            upstream = Unit::createUnit(subject("00000000-0000-7000-8000-00000000039b"));
    Unit*            relay    = Unit::createUnit(subject("00000000-0000-7000-8000-00000000039c"));
    for (Unit* unit : {upstream, relay}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        unit->faction()->link = std::move(link).takeValue();
    }
    upstream->motion()->speed = 5.0f;
    upstream->supply()->stock = upstream->supply()->capacity = 20.0f;
    upstream->supply()->range                                = 1.0f;
    upstream->supply()->transferRate                         = 4.0f;
    upstream->supply()->autoDispatch                         = true;
    upstream->supply()->autoThreshold                        = 0.75f;
    relay->motion()->x                                       = 10.0f;
    relay->motion()->speed                                   = 2.0f;
    relay->supply()->capacity                                = 8.0f;
    relay->supply()->stock                                   = 0.0f;
    relay->supply()->range                                   = 2.0f;
    relay->supply()->transferRate                            = 2.0f;
    relay->supply()->relayEnabled                            = true;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {20.0f, 0.0f};
    REQUIRE(relay->orders()->values.replace(move).ok());

    const eve::SimulationStep noTime{eve::SimulationTick{1},
                                     eve::Duration::fromSeconds(0.0).expect("zero relay dispatch dt")};
    auto                      dispatched = eve::rts::SupplySystem::step(noTime);
    REQUIRE(dispatched.ok());
    CHECK_EQ(ecs::try_get(upstream->supply()->assignedTarget), relay);
    auto order = upstream->orders()->values.current();
    REQUIRE(order.ok());
    CHECK_EQ(static_cast<int>(order.value().kind), static_cast<int>(OrderKind::SupplyRelay));
    CHECK(upstream->supply()->rendezvousActive);
    CHECK(upstream->supply()->rendezvousPoint.x > relay->motion()->x + 7.9f);
    CHECK(std::abs(upstream->supply()->reservedStock - 8.0f) < 1e-5f);

    const eve::SimulationStep movement{eve::SimulationTick{2},
                                       eve::Duration::fromSeconds(1.0).expect("relay intercept dt")};
    REQUIRE(eve::rts::MotionSystem::step(movement).ok());
    CHECK(upstream->motion()->x > 4.9f);
    CHECK(relay->motion()->x > 11.9f);

    relay->release();
    upstream->release();
    faction->release();
}

TEST_CASE("rts.supplyRendezvousAvoidsVisibleHostileCoverage") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction(subject("00000000-0000-7000-8000-0000000003a1"));
    Faction*         red      = Faction::createFaction(subject("00000000-0000-7000-8000-0000000003a2"));
    Unit*            supplier = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003a3"));
    Unit*            relay    = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003a4"));
    Unit*            hostile  = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003a5"));
    auto             bind     = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*supplier, *blue);
    bind(*relay, *blue);
    bind(*hostile, *red);
    supplier->motion()->y     = 10.0f;
    supplier->supply()->range = 1.0f;
    relay->motion()->x        = 10.0f;
    relay->motion()->y        = 10.0f;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {20.0f, 10.0f};
    REQUIRE(relay->orders()->values.replace(move).ok());
    hostile->motion()->x                 = 18.0f;
    hostile->motion()->y                 = 10.0f;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "rendezvous-threat";
    definition.damage           = 20.0f;
    definition.cooldown         = 1.0f;
    definition.range            = 2.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    auto weaponLink             = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    hostile->weapon()->link = std::move(weaponLink).takeValue();

    eve::map::Pathfinder pathfinder(32, 32);
    auto selected = eve::rts::SupplyRendezvousSystem::select(*supplier, *relay, {18.0f, 10.0f}, pathfinder, {});
    REQUIRE(selected.ok());
    CHECK(selected.value().avoidedThreat);
    CHECK(std::abs(selected.value().threat) < 1e-5f);
    CHECK(std::hypot(selected.value().target.x - 18.0f, selected.value().target.y - 10.0f) > 3.9f);

    supplier->motion()->speed = 5.0f;
    supplier->supply()->stock = supplier->supply()->capacity = 20.0f;
    supplier->supply()->range                                = 1.0f;
    supplier->supply()->transferRate                         = 4.0f;
    supplier->supply()->autoDispatch                         = true;
    relay->supply()->capacity                                = 8.0f;
    relay->supply()->stock                                   = 0.0f;
    relay->supply()->relayEnabled                            = true;
    const eve::SimulationStep noTime{eve::SimulationTick{1},
                                     eve::Duration::fromSeconds(0.0).expect("safe relay dispatch dt")};
    REQUIRE(eve::rts::SupplySystem::step(noTime, {}, &pathfinder, {}).ok());
    CHECK_EQ(ecs::try_get(supplier->supply()->assignedTarget), relay);
    CHECK(supplier->supply()->rendezvousActive);
    CHECK(supplier->supply()->rendezvousAvoidedThreat);
    CHECK(std::abs(supplier->supply()->rendezvousThreat) < 1e-5f);
    CHECK(std::hypot(supplier->supply()->rendezvousPoint.x - 18.0f, supplier->supply()->rendezvousPoint.y - 10.0f) >
          3.9f);

    weapon->release();
    hostile->release();
    relay->release();
    supplier->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.supplyNavigationUsesCanonicalThreatCostOverlay") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue      = Faction::createFaction(subject("00000000-0000-7000-8000-0000000003c1"));
    Faction*         red       = Faction::createFaction(subject("00000000-0000-7000-8000-0000000003c2"));
    Unit*            supplier  = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003c3"));
    Unit*            recipient = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003c4"));
    Unit*            hostile   = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003c5"));
    auto             bind      = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    bind(*supplier, *blue);
    bind(*recipient, *blue);
    bind(*hostile, *red);
    supplier->motion()->x                = 0.0f;
    supplier->motion()->y                = 2.0f;
    recipient->motion()->x               = 8.0f;
    recipient->motion()->y               = 2.0f;
    hostile->motion()->x                 = 4.0f;
    hostile->motion()->y                 = 2.0f;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id               = "route-threat";
    definition.damage           = 30.0f;
    definition.cooldown         = 1.0f;
    definition.range            = 1.5f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def   = weapon->definition()->owned.get();
    auto weaponLink             = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    hostile->weapon()->link = std::move(weaponLink).takeValue();
    CommandSpec mission;
    mission.kind         = OrderKind::Resupply;
    mission.targetEntity = recipient->identity()->self;
    mission.target       = {8.0f, 2.0f};
    REQUIRE(supplier->orders()->values.replace(mission).ok());

    eve::map::Pathfinder pathfinder(9, 5);
    REQUIRE(eve::rts::NavigationSystem::step(pathfinder, {}).ok());
    CHECK(supplier->supply()->routeAvoidedThreat);
    CHECK(supplier->supply()->routeThreat < 1e-4f);
    REQUIRE(!supplier->navigation()->waypoints.empty());
    CHECK(std::any_of(supplier->navigation()->waypoints.begin(), supplier->navigation()->waypoints.end(),
                      [](const WorldPosition& point) { return std::abs(point.y - 2.0f) > 1.9f; }));

    weapon->release();
    hostile->release();
    recipient->release();
    supplier->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.supplyConvoyLeaderWaitsForLaggingVehicle") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction  = Faction::createFaction(subject("00000000-0000-7000-8000-00000000039d"));
    Unit*            leader   = Unit::createUnit(subject("00000000-0000-7000-8000-00000000039e"));
    Unit*            follower = Unit::createUnit(subject("00000000-0000-7000-8000-00000000039f"));
    Unit*            relay    = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003a0"));
    for (Unit* unit : {leader, follower, relay}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        unit->faction()->link = std::move(link).takeValue();
    }
    leader->motion()->x   = 8.0f;
    follower->motion()->x = 0.0f;
    relay->motion()->x    = 10.0f;
    for (Unit* supplier : {leader, follower}) {
        supplier->motion()->speed = 2.0f;
        supplier->supply()->range = 1.0f;
        supplier->supply()->stock = supplier->supply()->capacity = 4.0f;
        CommandSpec order;
        order.kind         = OrderKind::SupplyRelay;
        order.targetEntity = relay->identity()->self;
        order.target       = {10.0f, 0.0f};
        REQUIRE(supplier->orders()->values.replace(order).ok());
    }

    auto paced = eve::rts::SupplyConvoySystem::step();
    REQUIRE(paced.ok());
    CHECK_EQ(ecs::try_get(leader->supply()->convoyLeader), leader);
    CHECK_EQ(ecs::try_get(follower->supply()->convoyLeader), leader);
    CHECK_EQ(leader->supply()->convoyIndex, 0u);
    CHECK_EQ(follower->supply()->convoyIndex, 1u);
    CHECK(leader->supply()->convoyWaiting);
    const eve::SimulationStep movement{eve::SimulationTick{1},
                                       eve::Duration::fromSeconds(1.0).expect("convoy movement dt")};
    REQUIRE(eve::rts::MotionSystem::step(movement).ok());
    CHECK(std::abs(leader->motion()->x - 8.0f) < 1e-5f);
    CHECK(follower->motion()->x > 1.9f);

    follower->motion()->x = 5.0f;
    REQUIRE(eve::rts::SupplyConvoySystem::step().ok());
    CHECK(!leader->supply()->convoyWaiting);
    REQUIRE(eve::rts::MotionSystem::step(movement).ok());
    CHECK(leader->motion()->x > 8.9f);

    relay->release();
    follower->release();
    leader->release();
    faction->release();
}

TEST_CASE("rts.workforcePolicyAssignsNearestBuildersAndRepairersWhileKeepingReserve") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction                      = Faction::createFaction();
    faction->workforce()->autoConstruction        = true;
    faction->workforce()->autoRepair              = true;
    faction->workforce()->maxBuildersPerSite      = 2;
    faction->workforce()->maxRepairersPerBuilding = 2;
    faction->workforce()->reserveWorkers          = 1;
    Building*  site                               = Building::createBuilding();
    const auto siteHandle                         = ecs::handle_of(site);
    Building*  damaged                            = Building::createBuilding();
    site                                          = dynamic_cast<Building*>(ecs::try_get(siteHandle));
    REQUIRE(site != nullptr);
    site->construction()->progress        = 0.0f;
    site->placement()->worldX             = 2.0f;
    damaged->construction()->progress     = 1.0f;
    damaged->placement()->worldX          = 20.0f;
    damaged->integrity()->state.health    = 50.0;
    damaged->integrity()->state.maxHealth = 100.0;
    for (Building* building : {site, damaged}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        building->faction()->link = std::move(link).takeValue();
    }
    std::vector<ecs::EntityHandle> workers;
    for (float x : {0.0f, 1.0f, 18.0f, 30.0f}) {
        Unit* worker = Unit::createUnit();
        auto  link   = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        worker->faction()->link      = std::move(link).takeValue();
        worker->motion()->x          = x;
        worker->worker()->buildRate  = 1.0f;
        worker->worker()->repairRate = 1.0f;
        workers.push_back(ecs::handle_of(worker));
    }
    auto assigned = eve::rts::WorkforceAssignmentSystem::step();
    REQUIRE(assigned.ok());
    CHECK_EQ(assigned.value(), 3u);
    std::size_t builders = 0, repairers = 0, idle = 0;
    for (const auto& handle : workers) {
        auto* worker = dynamic_cast<Unit*>(ecs::try_get(handle));
        REQUIRE(worker != nullptr);
        auto current = worker->orders()->values.current();
        if (!current.ok()) {
            ++idle;
            continue;
        }
        if (current.value().kind == OrderKind::Build) ++builders;
        if (current.value().kind == OrderKind::Repair) ++repairers;
    }
    CHECK_EQ(builders, 2u);
    CHECK_EQ(repairers, 1u);
    CHECK_EQ(idle, 1u);
}

TEST_CASE("rts.completedCanonicalResearchAppliesToExistingAndFutureUnitsOnce") {
    ecs::Table                           world;
    ecs::ScopedTable                     guard(world);
    eve::definitions::DefinitionRegistry registry;
    auto                                 content = eve::rts::RTSContentLoader::load(registry, R"JSON({
      "units":[{"id":"marine"}],
      "buildings":[{"id":"lab"}],
      "upgrades":[{"id":"veteran_training","producer":"lab","targetUnit":"marine",
        "attackMultiplier":1.5,"healthMultiplier":1.25,"speedMultiplier":1.2,"gatherMultiplier":2.0,
        "shieldMultiplier":1.5,"shieldRegenMultiplier":1.25}]
    })JSON");
    REQUIRE(content.ok());
    auto marineId = eve::LogicalId::fromParts("unit", "marine");
    auto labId    = eve::LogicalId::fromParts("building", "lab");
    REQUIRE(marineId.has_value());
    REQUIRE(labId.has_value());
    Faction*  faction     = Faction::createFaction();
    Unit*     marine      = Unit::createUnit({}, *marineId);
    Building* lab         = Building::createBuilding({}, *labId);
    auto      unitFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto      labFaction  = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(unitFaction.ok());
    REQUIRE(labFaction.ok());
    marine->faction()->link               = std::move(unitFaction).takeValue();
    lab->faction()->link                  = std::move(labFaction).takeValue();
    marine->motion()->speed               = 2.0f;
    marine->worker()->gatherRate          = 3.0f;
    marine->durability()->state.health    = 80.0;
    marine->durability()->state.maxHealth = 100.0;
    marine->shield()->value               = 20.0f;
    marine->shield()->capacity            = 40.0f;
    marine->shield()->regenRate           = 4.0f;
    auto duration                         = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    auto upgradeHandle = registry.handle("upgrade", "veteran_training");
    REQUIRE(upgradeHandle.ok());
    eve::production::ProductionRequest researchRequest;
    researchRequest.owner              = "faction";
    researchRequest.kind               = "research";
    researchRequest.product            = "veteran_training";
    researchRequest.duration           = duration.value();
    researchRequest.definition         = upgradeHandle.value();
    researchRequest.settlementRequired = true;
    auto research                      = lab->production()->values.enqueue(std::move(researchRequest));
    REQUIRE(research.ok());
    std::move(research).takeValue();
    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("research dt")};
    auto                      advanced = eve::rts::BuildingProductionSystem::step(tick);
    REQUIRE(advanced.ok());
    auto settled = eve::rts::TechnologySystem::step(registry);
    REQUIRE(settled.ok());
    CHECK_EQ(faction->technology()->unlocked.size(), 1u);
    CHECK(std::abs(marine->combat()->upgradeDamageFactor - 1.5f) < 1e-5f);
    CHECK(std::abs(marine->motion()->speed - 2.4f) < 1e-5f);
    CHECK(std::abs(marine->worker()->gatherRate - 6.0f) < 1e-5f);
    CHECK(std::abs(marine->durability()->state.maxHealth - 125.0) < 1e-6);
    CHECK(std::abs(marine->durability()->state.health - 100.0) < 1e-6);
    CHECK_EQ(marine->shield()->capacity, 60.0f);
    CHECK_EQ(marine->shield()->value, 30.0f);
    CHECK_EQ(marine->shield()->regenRate, 5.0f);
    auto repeated = eve::rts::TechnologySystem::step(registry);
    REQUIRE(repeated.ok());
    CHECK(std::abs(marine->motion()->speed - 2.4f) < 1e-5f);

    auto reloaded = registry.replace("upgrade", "veteran_training", 2, R"JSON({
      "id":"veteran_training","producer":"lab","targetUnit":"marine",
      "attackMultiplier":9.0,"healthMultiplier":9.0,"speedMultiplier":9.0,"gatherMultiplier":9.0,
      "shieldMultiplier":9.0,"shieldRegenMultiplier":9.0})JSON");
    REQUIRE(reloaded.ok());

    Unit* reinforcement        = Unit::createUnit({}, *marineId);
    auto  reinforcementFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(reinforcementFaction.ok());
    reinforcement->faction()->link = std::move(reinforcementFaction).takeValue();
    reinforcement->motion()->speed = 3.0f;
    auto futureApplied             = eve::rts::TechnologySystem::step(registry);
    REQUIRE(futureApplied.ok());
    CHECK(std::abs(reinforcement->motion()->speed - 3.6f) < 1e-5f);

    reinforcement->release();
    lab->release();
    marine->release();
    faction->release();
}

TEST_CASE("rts.matchHeadquartersResourceVictoryAndTeamSurrender") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto             hqId = eve::LogicalId::fromParts("building", "hq");
    REQUIRE(hqId.has_value());
    Faction* alpha                   = Faction::createFaction(subject("00000000-0000-7000-8000-000000000201"));
    Faction* beta                    = Faction::createFaction(subject("00000000-0000-7000-8000-000000000202"));
    Match*   headquarters            = Match::createMatch(subject("00000000-0000-7000-8000-000000000203"));
    headquarters->rules()->rule      = eve::rts::VictoryRule::DestroyHeadquarters;
    headquarters->rules()->archetype = "hq";
    REQUIRE(eve::rts::MatchSystem::addParticipant(*headquarters, *alpha, 10).ok());
    REQUIRE(eve::rts::MatchSystem::addParticipant(*headquarters, *beta, 20).ok());
    Building* alphaHq    = Building::createBuilding({}, *hqId);
    Building* betaHq     = Building::createBuilding({}, *hqId);
    auto      alphaOwner = eve::rts::FactionLink::bind(ecs::handle_of(alpha));
    auto      betaOwner  = eve::rts::FactionLink::bind(ecs::handle_of(beta));
    REQUIRE(alphaOwner.ok());
    REQUIRE(betaOwner.ok());
    alphaHq->faction()->link           = std::move(alphaOwner).takeValue();
    betaHq->faction()->link            = std::move(betaOwner).takeValue();
    alphaHq->integrity()->alive        = true;
    alphaHq->integrity()->state.health = 100.0;
    betaHq->integrity()->alive         = true;
    betaHq->integrity()->state.health  = 100.0;
    REQUIRE(eve::rts::MatchSystem::start(*headquarters).ok());
    auto ongoing = eve::rts::MatchSystem::step(*headquarters);
    REQUIRE(ongoing.ok());
    CHECK_EQ(static_cast<int>(headquarters->state()->phase), static_cast<int>(eve::rts::MatchPhase::Running));
    betaHq->integrity()->alive        = false;
    betaHq->integrity()->state.health = 0.0;
    auto won                          = eve::rts::MatchSystem::step(*headquarters);
    REQUIRE(won.ok());
    CHECK_EQ(static_cast<int>(headquarters->state()->phase), static_cast<int>(eve::rts::MatchPhase::Finished));
    CHECK_EQ(headquarters->state()->winningTeam, 10);

    Match* resource                = Match::createMatch(subject("00000000-0000-7000-8000-000000000204"));
    resource->rules()->rule        = eve::rts::VictoryRule::ResourceTarget;
    resource->rules()->archetype   = "ore";
    resource->rules()->targetValue = 500.0;
    REQUIRE(eve::rts::MatchSystem::addParticipant(*resource, *alpha, 10).ok());
    REQUIRE(eve::rts::MatchSystem::addParticipant(*resource, *beta, 20).ok());
    REQUIRE(eve::rts::MatchSystem::start(*resource).ok());
    auto resourceWin = eve::rts::MatchSystem::step(*resource, [&](Faction& faction, std::string_view kind) {
        CHECK_EQ(kind, "ore");
        return eve::Result<double>::success(&faction == beta ? 500.0 : 100.0);
    });
    REQUIRE(resourceWin.ok());
    CHECK_EQ(resource->state()->winningTeam, 20);

    Match* surrender = Match::createMatch(subject("00000000-0000-7000-8000-000000000205"));
    REQUIRE(eve::rts::MatchSystem::addParticipant(*surrender, *alpha, 1).ok());
    REQUIRE(eve::rts::MatchSystem::addParticipant(*surrender, *beta, 2).ok());
    REQUIRE(eve::rts::MatchSystem::start(*surrender).ok());
    REQUIRE(eve::rts::MatchSystem::surrender(*surrender, *beta).ok());
    CHECK_EQ(surrender->state()->winningTeam, 1);
    CHECK(surrender->participants()->entries[1].surrendered);

    surrender->release();
    resource->release();
    headquarters->release();
    betaHq->release();
    alphaHq->release();
    beta->release();
    alpha->release();
}

TEST_CASE("rts.infrastructureAllocatesPowerByPriorityAndCreditsCanonicalIncome") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction  = Faction::createFaction(subject("00000000-0000-7000-8000-000000000211"));
    Building*        plant    = Building::createBuilding(subject("00000000-0000-7000-8000-000000000212"));
    Building*        refinery = Building::createBuilding(subject("00000000-0000-7000-8000-000000000213"));
    Building*        radar    = Building::createBuilding(subject("00000000-0000-7000-8000-000000000214"));
    for (Building* building : {plant, refinery, radar}) {
        auto owner = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(owner.ok());
        building->faction()->link           = std::move(owner).takeValue();
        building->integrity()->state.health = 100.0;
    }
    plant->infrastructure()->powerProduced     = 5.0f;
    refinery->infrastructure()->powerConsumed  = 4.0f;
    refinery->infrastructure()->powerPriority  = 20;
    refinery->infrastructure()->incomeResource = "ore";
    refinery->infrastructure()->incomeRate     = 2.5f;
    radar->infrastructure()->powerConsumed     = 3.0f;
    radar->infrastructure()->powerPriority     = 10;

    std::int64_t              credited = 0;
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(2.0).expect("income dt")};
    auto                      advanced =
        eve::rts::InfrastructureSystem::step(step, [&](Building& source, const eve::resource::CostSpec& cost) {
            CHECK(&source == refinery);
            REQUIRE_EQ(cost.items().size(), 1u);
            CHECK_EQ(cost.items()[0].resource.value(), "ore");
            credited += cost.items()[0].amount.value();
            return eve::Result<eve::resource::Receipt>::success(eve::resource::Receipt{});
        });
    REQUIRE(advanced.ok());
    CHECK(refinery->infrastructure()->powered);
    CHECK(!radar->infrastructure()->powered);
    CHECK_EQ(credited, 5);
    CHECK(std::abs(refinery->infrastructure()->incomeProgress) < 1e-6f);

    radar->release();
    refinery->release();
    plant->release();
    faction->release();
}

TEST_CASE("rts.buildInfluenceComposesPoweredOwnershipWithCanonicalPlacementProvider") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto             factionHandle = ecs::handle_of(Faction::createFaction());
    auto             hostileHandle = ecs::handle_of(Faction::createFaction());
    auto             sourceHandle  = ecs::handle_of(Building::createBuilding());
    auto*            faction       = dynamic_cast<Faction*>(ecs::try_get(factionHandle));
    auto*            hostile       = dynamic_cast<Faction*>(ecs::try_get(hostileHandle));
    auto*            source        = dynamic_cast<Building*>(ecs::try_get(sourceHandle));
    auto             owner         = eve::rts::FactionLink::bind(factionHandle);
    REQUIRE(owner.ok());
    source->faction()->link                        = std::move(owner).takeValue();
    source->placement()->placed                    = true;
    source->placement()->worldX                    = 4.0f;
    source->placement()->worldY                    = 3.0f;
    source->infrastructure()->buildInfluenceRadius = 5.0f;
    source->infrastructure()->powered              = false;
    auto definition                                = eve::LogicalId::parse("rts:factory");
    REQUIRE(definition.has_value());
    int                           providerCalls = 0;
    eve::rts::PlacementValidation provider      = [&](WorldPosition point, eve::LogicalId id) {
        ++providerCalls;
        CHECK_EQ(id, *definition);
        if (point.x == 7.0f)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "occupied", "placement.occupancy"));
        return eve::Result<void>::success();
    };
    CHECK(!eve::rts::BuildInfluenceSystem::validate(*faction, {5.0f, 3.0f}, *definition, provider).ok());
    CHECK_EQ(providerCalls, 0);
    source->infrastructure()->powered = true;
    CHECK(!eve::rts::BuildInfluenceSystem::validate(*hostile, {5.0f, 3.0f}, *definition, provider).ok());
    CHECK_EQ(providerCalls, 0);
    REQUIRE(eve::rts::BuildInfluenceSystem::validate(*faction, {5.0f, 3.0f}, *definition, provider).ok());
    CHECK_EQ(providerCalls, 1);
    CHECK(!eve::rts::BuildInfluenceSystem::validate(*faction, {7.0f, 3.0f}, *definition, provider).ok());
    CHECK_EQ(providerCalls, 2);
    REQUIRE(eve::rts::BuildInfluenceSystem::validate(*hostile, {20.0f, 20.0f}, *definition, provider, false).ok());
    CHECK_EQ(providerCalls, 3);
    source->release();
    hostile->release();
    faction->release();
}
}  // namespace test_rts_economy

// Contract section: rts economy 2
#include "PrivateTestScope.h"

// rts_economy_2.cpp
namespace test_rts_economy_2 {
TEST_CASE("rts.facadeMigratesMobileSupplyReserveAmmoAndAutoResupplyControls") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             unitCreated     = rts.newUnit(subject("00000000-0000-7000-8000-0000000002d1"));
    auto             buildingCreated = rts.newBuilding(subject("00000000-0000-7000-8000-0000000002d2"));
    REQUIRE(unitCreated.ok());
    REQUIRE(buildingCreated.ok());
    auto* unit                   = unitCreated.value();
    auto* building               = buildingCreated.value();
    unit->supply()->capacity     = 40.0f;
    building->supply()->capacity = 120.0f;

    auto unitSupply     = rts.addUnitAmmoSupply(*unit, 55.0f);
    auto buildingSupply = rts.addBuildingAmmoSupply(*building, 75.0f);
    REQUIRE(unitSupply.ok());
    REQUIRE(buildingSupply.ok());
    CHECK_EQ(unitSupply.value(), 40.0f);
    CHECK_EQ(buildingSupply.value(), 75.0f);
    REQUIRE(rts.addUnitAmmoSupply(*unit, -100.0f).ok());
    CHECK_EQ(unit->supply()->stock, 0.0f);
    CHECK(!rts.addBuildingAmmoSupply(*building, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_EQ(building->supply()->stock, 75.0f);

    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id                     = "supply-test-rifle";
    definition.reserveSize            = 90;
    weapon->definition()->owned       = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def         = weapon->definition()->owned.get();
    weapon->state()->resource.kind    = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.reserve = 20;
    auto weaponLink                   = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    unit->weapon()->link = std::move(weaponLink).takeValue();
    auto reserve         = rts.addUnitReserveAmmo(*unit, 100);
    REQUIRE(reserve.ok());
    CHECK_EQ(reserve.value(), 90);
    reserve = rts.addUnitReserveAmmo(*unit, -200);
    REQUIRE(reserve.ok());
    CHECK_EQ(reserve.value(), 0);

    eve::rts::CommandSpec mission;
    mission.kind = eve::rts::OrderKind::SupplyRelay;
    REQUIRE(unit->orders()->values.replace(mission).ok());
    unit->supply()->autoDispatch     = true;
    unit->supply()->assignedTarget   = ecs::handle_of(building);
    unit->supply()->reservedStock    = 12.0f;
    unit->supply()->returning        = true;
    unit->supply()->rendezvousActive = true;
    unit->navigation()->waypoints.push_back({3.0f, 4.0f});
    unit->navigation()->plannedOrderId = "supply-plan";
    unit->motion()->arrived            = false;
    REQUIRE(rts.setUnitAutoResupply(*unit, false).ok());
    CHECK(!unit->supply()->autoDispatch);
    CHECK(ecs::try_get(unit->supply()->assignedTarget) == nullptr);
    CHECK_EQ(unit->supply()->reservedStock, 0.0f);
    CHECK(!unit->supply()->returning);
    CHECK(!unit->supply()->rendezvousActive);
    CHECK(unit->navigation()->waypoints.empty());
    CHECK(unit->navigation()->plannedOrderId.empty());
    CHECK(unit->motion()->arrived);

    const auto inspected = rts.inspectState().toJson();
    REQUIRE(inspected.ok());
    CHECK(inspected.value().find("\"reserveAmmo\":0") != std::string::npos);
    CHECK(inspected.value().find("\"supplyCapacity\":40") != std::string::npos);
    auto snapshot = rts.snapshotState();
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().units.front().supply.capacity, 40.0f);
    CHECK_EQ(snapshot.value().buildings.front().supply.stock, 75.0f);

    unit->weapon()->link = {};
    weapon->release();
}

TEST_CASE("rts.facadeExposesManualBuildersAndFactionWorkforceAutomation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    const auto       factionSubject = subject("00000000-0000-7000-8000-0000000002e1");
    auto             factionCreated = rts.newFaction(factionSubject);
    REQUIRE(factionCreated.ok());
    auto*      faction     = factionCreated.value();
    const auto siteSubject = subject("00000000-0000-7000-8000-0000000002e2");
    auto       siteCreated = rts.newFactionBuilding(*faction, siteSubject);
    REQUIRE(siteCreated.ok());
    auto* site                     = siteCreated.value();
    site->construction()->progress = 0.25f;
    site->placement()->worldX      = 8.0f;
    site->placement()->worldY      = 2.0f;

    std::vector<eve::rts::Unit*> workers;
    for (int index = 0; index < 3; ++index) {
        const auto workerId      = "00000000-0000-7000-8000-0000000002e" + std::to_string(3 + index);
        auto       workerCreated = rts.newFactionUnit(*faction, subject(workerId.c_str()));
        REQUIRE(workerCreated.ok());
        workerCreated.value()->worker()->buildRate = 1.0f;
        workerCreated.value()->motion()->x         = static_cast<float>(index * 3);
        workers.push_back(workerCreated.value());
    }

    auto manual = rts.assignBuilder(*workers.front(), *site);
    REQUIRE(manual.ok());
    auto current = workers.front()->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(eve::rts::OrderKind::Build));
    CHECK_EQ(current.value().targetEntity.id, site->identity()->self.id);

    REQUIRE(rts.configureWorkforce(*faction, true, 2, true, 3, 1).ok());
    auto assigned = eve::rts::WorkforceAssignmentSystem::step();
    REQUIRE(assigned.ok());
    CHECK_EQ(assigned.value(), 1u);
    std::size_t buildingWorkers = 0;
    std::size_t idleWorkers     = 0;
    for (auto* worker : workers) {
        auto order = worker->orders()->values.current();
        if (!order.ok())
            ++idleWorkers;
        else if (order.value().kind == eve::rts::OrderKind::Build)
            ++buildingWorkers;
    }
    CHECK_EQ(buildingWorkers, 2u);
    CHECK_EQ(idleWorkers, 1u);

    auto rejected = rts.configureWorkforce(*faction, false, 0, false, 2, 0);
    CHECK(!rejected.ok());
    CHECK(faction->workforce()->autoConstruction);
    CHECK_EQ(faction->workforce()->maxBuildersPerSite, 2u);

    auto snapshot = rts.snapshotState();
    REQUIRE(snapshot.ok());
    CHECK(snapshot.value().factions.front().workforce.autoConstruction);
    CHECK_EQ(snapshot.value().factions.front().workforce.reserveWorkers, 1u);
    auto inspected = rts.inspectState().toJson();
    REQUIRE(inspected.ok());
    CHECK(inspected.value().find("\"autoConstruction\":true") != std::string::npos);
    CHECK(inspected.value().find("\"reserveWorkers\":1") != std::string::npos);
}
}  // namespace test_rts_economy_2
