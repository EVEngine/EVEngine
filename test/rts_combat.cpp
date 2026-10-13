#include "RtsCompositionFixtures.h"

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
