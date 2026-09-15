#include "RtsCompositionFixtures.h"

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
