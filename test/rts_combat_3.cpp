#include "RtsCompositionFixtures.h"

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
