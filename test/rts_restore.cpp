#include "RtsCompositionFixtures.h"

TEST_CASE("rts.buildingAmmoProductionPurchasesFiniteStockAndSnapshotsFractionalProgress") {
    eve::rts::RTS module;
    auto          buildingResult = module.newBuilding(subject("00000000-0000-7000-8000-000000000010"));
    REQUIRE(buildingResult.ok());
    Building* building                         = std::move(buildingResult).takeValue();
    building->supply()->capacity               = 3.0f;
    building->supply()->productionResource     = "minerals";
    building->supply()->productionCostPerRound = 2;
    building->supply()->productionRate         = 2.0f;
    std::int64_t                     bank      = 5;
    eve::rts::AmmoProductionPurchase purchase  = [&](Building& producer, std::string_view resource,
                                                    std::int64_t unitCost,
                                                    std::size_t  requested) -> eve::Result<std::size_t> {
        CHECK_EQ(&producer, building);
        CHECK_EQ(resource, "minerals");
        const auto affordable = static_cast<std::size_t>(bank / unitCost);
        const auto bought     = std::min(requested, affordable);
        bank -= static_cast<std::int64_t>(bought) * unitCost;
        return eve::Result<std::size_t>::success(bought);
    };

    auto partial = eve::rts::SupplySystem::step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(0.25).expect("ammo partial")}, purchase);
    REQUIRE(partial.ok());
    CHECK_EQ(building->supply()->stock, 0.0f);
    CHECK_EQ(building->supply()->productionProgress, 0.5f);
    auto snapshot = module.snapshotState();
    REQUIRE(snapshot.ok());
    building->supply()->productionProgress = 0.0f;
    REQUIRE(module.restoreState(snapshot.value()).ok());
    CHECK_EQ(building->supply()->productionProgress, 0.5f);

    std::vector<eve::rts::LifecycleEvent> events;
    auto                                  produced = eve::rts::SupplySystem::step(
        {eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("ammo production")}, purchase, nullptr, {},
        [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick tick) {
            CHECK_EQ(tick.value(), 2u);
            events.push_back(event);
        });
    REQUIRE(produced.ok());
    CHECK_EQ(produced.value(), 2u);
    CHECK_EQ(building->supply()->stock, 2.0f);
    CHECK_EQ(building->supply()->productionProgress, 0.5f);
    CHECK_EQ(bank, 1);
    REQUIRE_EQ(events.size(), 1u);
    CHECK_EQ(static_cast<int>(events[0].kind), static_cast<int>(eve::rts::LifecycleEventKind::AmmoProduced));
    CHECK_EQ(events[0].source, building->identity()->subject);
    CHECK_EQ(events[0].detail, "minerals");
    CHECK_EQ(events[0].value, 2.0);

    auto unaffordable = eve::rts::SupplySystem::step(
        {eve::SimulationTick{3}, eve::Duration::fromSeconds(1.0).expect("ammo insufficient")}, purchase);
    REQUIRE(unaffordable.ok());
    CHECK_EQ(building->supply()->stock, 2.0f);
    CHECK_EQ(building->supply()->productionProgress, 1.0f);
    CHECK_EQ(bank, 1);
}

TEST_CASE("rts.orderAndProductionAdaptersRoundTripCanonicalQueueSnapshots") {
    ecs::Table               world;
    ecs::ScopedTable         guard(world);
    Unit*                    target = Unit::createUnit();
    eve::rts::OrderComponent orders;
    CommandSpec              move;
    move.kind            = OrderKind::SuppressArea;
    move.target          = {7.0f, 9.0f};
    move.secondaryTarget = {11.0f, 13.0f};
    move.radius          = 2.5f;
    move.append          = true;
    move.targetEntity    = ecs::handle_of(target);
    REQUIRE(orders.enqueue(move).ok());
    auto orderState = orders.snapshotState();
    REQUIRE(orderState.ok());
    eve::rts::OrderComponent restoredOrders;
    REQUIRE(restoredOrders.restoreState(orderState.value()).ok());
    auto current = restoredOrders.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK_EQ(current.value().target.x, 7.0f);
    CHECK_EQ(current.value().target.y, 9.0f);
    CHECK_EQ(current.value().secondaryTarget.x, 11.0f);
    CHECK_EQ(current.value().secondaryTarget.y, 13.0f);
    CHECK_EQ(current.value().radius, 2.5f);
    CHECK(current.value().append);
    CHECK_EQ(ecs::try_get(current.value().targetEntity), target);

    eve::rts::ProductionComponent production;
    const auto                    duration = eve::Duration::fromSeconds(2.0).expect("snapshot production duration");
    REQUIRE(production.enqueue("faction", "unit", "marine", duration).ok());
    auto productionJson = production.snapshot();
    REQUIRE(productionJson.ok());
    eve::rts::ProductionComponent restoredProduction;
    REQUIRE(restoredProduction.restore(productionJson.value()).ok());
    CHECK_EQ(restoredProduction.taskCount(), 1u);

    const auto before = restoredProduction.snapshot();
    REQUIRE(before.ok());
    auto rejected = restoredProduction.restore("{not-json");
    CHECK(!rejected.ok());
    auto after = restoredProduction.snapshot();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
    target->release();
}

TEST_CASE("rts.rootSnapshotRestoresStableRelationshipsAndRejectsTopologyMismatch") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    auto             factionResult  = module.newFaction(subject("00000000-0000-7000-8000-000000000081"));
    auto             unitResult     = module.newUnit(subject("00000000-0000-7000-8000-000000000082"));
    auto             buildingResult = module.newBuilding(subject("00000000-0000-7000-8000-000000000083"));
    auto             nodeResult =
        module.newResourceNode(subject("00000000-0000-7000-8000-000000000084"), "minerals", 500.0f, {8.0f, 6.0f}, 2);
    auto playerResult = module.newPlayer(subject("00000000-0000-7000-8000-000000000085"));
    auto matchResult  = module.newMatch(subject("00000000-0000-7000-8000-000000000086"));
    REQUIRE(factionResult.ok());
    REQUIRE(unitResult.ok());
    REQUIRE(buildingResult.ok());
    REQUIRE(nodeResult.ok());
    REQUIRE(playerResult.ok());
    REQUIRE(matchResult.ok());
    Faction*      faction         = factionResult.value();
    Unit*         unit            = unitResult.value();
    Building*     building        = buildingResult.value();
    ResourceNode* node            = nodeResult.value();
    Player*       player          = playerResult.value();
    Match*        match           = matchResult.value();
    auto          unitFaction     = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto          buildingFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto          nodeLink        = eve::rts::ResourceNodeLink::bind(ecs::handle_of(node));
    auto          dropoffLink     = eve::rts::BuildingLink::bind(ecs::handle_of(building));
    REQUIRE(unitFaction.ok());
    REQUIRE(buildingFaction.ok());
    REQUIRE(nodeLink.ok());
    REQUIRE(dropoffLink.ok());
    unit->faction()->link                      = std::move(unitFaction).takeValue();
    building->faction()->link                  = std::move(buildingFaction).takeValue();
    unit->worker()->resourceNode               = std::move(nodeLink).takeValue();
    unit->worker()->dropoff                    = std::move(dropoffLink).takeValue();
    unit->worker()->cargo                      = 17.0f;
    unit->motion()->x                          = 3.0f;
    unit->motion()->y                          = 4.0f;
    unit->tactics()->coordinatedVolleyInterval = 0.75f;
    unit->tactics()->volleyReleaseRemaining    = 0.25f;
    unit->tactics()->volleyHolding             = true;
    unit->artillery()->observedFireSpotter     = ecs::handle_of(unit);
    unit->artillery()->usingObservedFire       = true;
    REQUIRE(module.setUnitAttribute(*unit, "armor", 7.0).ok());
    REQUIRE(unit->tags()->values.add("unit.worker").ok());
    REQUIRE(building->tags()->values.add("building.dropoff").ok());
    eve::rts::RTSEffectDefinition effect;
    effect.id       = "snapshot-morale";
    effect.source   = "test";
    effect.duration = 5.0;
    REQUIRE(module.applyEffect(*unit, effect).ok());
    building->construction()->builders.push_back(ecs::handle_of(unit));
    building->rally()->enabled                 = true;
    building->rally()->productionSpawnBlocked  = true;
    building->rally()->blockedProductionTask   = "task-7";
    building->combat()->airDefenseNetworkRange = 9.0f;
    building->combat()->airDefenseNetworkRoot  = ecs::handle_of(building);
    building->combat()->airDefenseNetworkSize  = 3;
    building->rally()->command.kind            = OrderKind::Escort;
    building->rally()->command.targetEntity    = ecs::handle_of(unit);
    node->harvest()->workers.push_back(ecs::handle_of(unit));
    faction->members()->units.push_back(ecs::handle_of(unit));
    faction->members()->buildings.push_back(ecs::handle_of(building));
    faction->workforce()->autoConstruction = true;
    faction->intel()->enabled              = true;
    player->selection()->units.push_back(ecs::handle_of(unit));
    player->selection()->buildings.push_back(ecs::handle_of(building));
    Match::Participants::Entry participant;
    auto                       participantLink = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(participantLink.ok());
    participant.faction = std::move(participantLink).takeValue();
    participant.team    = 3;
    match->participants()->entries.push_back(std::move(participant));
    CommandSpec attack;
    attack.kind         = OrderKind::Attack;
    attack.target       = {8.0f, 6.0f};
    attack.targetEntity = ecs::handle_of(building);
    REQUIRE(unit->orders()->values.enqueue(attack).ok());
    auto duration = eve::Duration::fromSeconds(3.0).expect("snapshot queue duration");
    REQUIRE(building->production()->values.enqueue("faction", "unit", "marine", duration).ok());

    auto snapshot = module.snapshotState();
    REQUIRE(snapshot.ok());
    unit->motion()->x                          = 99.0f;
    unit->worker()->cargo                      = 0.0f;
    unit->tactics()->coordinatedVolleyInterval = 0.0f;
    unit->tactics()->volleyReleaseRemaining    = 0.0f;
    unit->tactics()->volleyHolding             = false;
    unit->artillery()->observedFireSpotter     = {};
    unit->artillery()->usingObservedFire       = false;
    REQUIRE(module.setUnitAttribute(*unit, "armor", 1.0).ok());
    REQUIRE(unit->tags()->values.remove("unit.worker").ok());
    REQUIRE(building->tags()->values.remove("building.dropoff").ok());
    building->construction()->builders.clear();
    node->stock()->remaining                  = 1.0f;
    building->rally()->command.targetEntity   = {};
    building->rally()->productionSpawnBlocked = false;
    building->rally()->blockedProductionTask.clear();
    building->combat()->airDefenseNetworkRange = 0.0f;
    building->combat()->airDefenseNetworkRoot  = {};
    building->combat()->airDefenseNetworkSize  = 0;
    faction->members()->units.clear();
    faction->workforce()->autoConstruction = false;
    faction->intel()->enabled              = false;
    player->selection()->units.clear();
    match->participants()->entries.clear();
    REQUIRE(module.restoreState(snapshot.value()).ok());
    CHECK_EQ(unit->motion()->x, 3.0f);
    CHECK_EQ(unit->worker()->cargo, 17.0f);
    CHECK_EQ(unit->tactics()->coordinatedVolleyInterval, 0.75f);
    CHECK_EQ(unit->tactics()->volleyReleaseRemaining, 0.25f);
    CHECK(unit->tactics()->volleyHolding);
    CHECK(faction->intel()->enabled);
    CHECK_EQ(ecs::try_get(unit->artillery()->observedFireSpotter), unit);
    CHECK(unit->artillery()->usingObservedFire);
    auto armor = module.readUnitAttribute(*unit, "armor");
    REQUIRE(armor.ok());
    CHECK_EQ(armor.value(), 7.0);
    CHECK(unit->tags()->values.contains("unit.worker"));
    CHECK(building->tags()->values.contains("building.dropoff"));
    CHECK_EQ(unit->effects()->values.count(), 1u);
    CHECK_EQ(unit->worker()->resourceNode.resolve(), node);
    CHECK_EQ(building->construction()->builders.size(), 1u);
    CHECK_EQ(ecs::try_get(building->rally()->command.targetEntity), unit);
    CHECK(building->rally()->productionSpawnBlocked);
    CHECK_EQ(building->rally()->blockedProductionTask, "task-7");
    CHECK_EQ(building->combat()->airDefenseNetworkRange, 9.0f);
    CHECK_EQ(ecs::try_get(building->combat()->airDefenseNetworkRoot), building);
    CHECK_EQ(building->combat()->airDefenseNetworkSize, 3u);
    CHECK_EQ(node->stock()->remaining, 500.0f);
    auto restoredOrder = unit->orders()->values.current();
    REQUIRE(restoredOrder.ok());
    CHECK_EQ(ecs::try_get(restoredOrder.value().targetEntity), building);
    CHECK_EQ(building->production()->values.taskCount(), 1u);
    CHECK_EQ(faction->members()->units.size(), 1u);
    CHECK(faction->workforce()->autoConstruction);
    CHECK_EQ(player->selection()->units.size(), 1u);
    REQUIRE_EQ(match->participants()->entries.size(), 1u);
    CHECK_EQ(match->participants()->entries.front().faction.resolve(), faction);
    CHECK_EQ(match->participants()->entries.front().team, 3);

    auto invalid = snapshot.value();
    invalid.resourceNodes.clear();
    unit->motion()->x = 42.0f;
    auto rejected     = module.restoreState(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(unit->motion()->x, 42.0f);

    invalid                                   = snapshot.value();
    invalid.units.front().worker.resourceNode = building->identity()->subject;
    rejected                                  = module.restoreState(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(unit->motion()->x, 42.0f);
}

TEST_CASE("rts.ballisticProjectileUsesAbsoluteHeightsAndSnapshotsThreeDimensionalFlight") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction     = Faction::createFaction(subject("00000000-0000-7000-8000-00000000e701"));
    Unit*            source      = Unit::createUnit(subject("00000000-0000-7000-8000-00000000e702"));
    auto             factionLink = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(factionLink.ok());
    source->faction()->link = std::move(factionLink).takeValue();

    eve::weapon::WeaponDefinition definition;
    definition.id                 = "height-mortar";
    definition.damage             = 1.0f;
    definition.range              = 10.0f;
    definition.projectile.speed   = 10.0f;
    definition.projectile.gravity = 4.0f;
    definition.blockedByObstacles = true;
    eve::rts::RTSProjectileSystem projectiles;
    REQUIRE(projectiles
                .launch(source->identity()->subject, source->faction()->link.handle(), {}, {}, {6.0f, 0.0f}, definition,
                        1.0, {}, 1.0f, 1.0f)
                .ok());
    auto launched = projectiles.snapshot();
    REQUIRE(!launched.runtime.slots.empty());
    REQUIRE(launched.runtime.slots[0].state.has_value());
    CHECK_EQ(launched.runtime.slots[0].state->position.y, 1.0);
    CHECK(launched.runtime.slots[0].state->velocity.y > 0.0);

    eve::combat::DamageRuntime damage;
    bool                       sawThreeDimensionalSweep = false;
    auto                       advanced                 = projectiles.step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(0.2).expect("ballistic height dt")}, damage,
        [&](eve::rts::WorldPosition, float fromHeight, eve::rts::WorldPosition, float toHeight, eve::SubjectRef,
            ecs::EntityHandle) {
            sawThreeDimensionalSweep = true;
            CHECK(fromHeight >= 1.0f);
            CHECK(toHeight > fromHeight);
            return eve::Result<std::optional<eve::rts::ProjectileCollision>>::success(std::nullopt);
        });
    REQUIRE(advanced.ok());
    CHECK(sawThreeDimensionalSweep);
    const auto inFlight = projectiles.snapshot();
    REQUIRE(inFlight.runtime.slots[0].state.has_value());
    CHECK(inFlight.runtime.slots[0].state->position.y > 1.0);

    eve::rts::RTSProjectileSystem restored;
    REQUIRE(restored
                .restore(inFlight,
                         [&](eve::SubjectRef stable) -> ecs::Entity* {
                             if (stable == source->identity()->subject) return source;
                             if (stable == faction->identity()->subject) return faction;
                             return nullptr;
                         })
                .ok());
    CHECK(restored.snapshot().runtime.slots[0].state->position == inFlight.runtime.slots[0].state->position);
    auto completed =
        restored.step({eve::SimulationTick{2}, eve::Duration::fromSeconds(0.5).expect("ballistic impact dt")}, damage);
    REQUIRE(completed.ok());
    CHECK_EQ(completed.value(), std::size_t{1});
    CHECK_EQ(restored.activeCount(), std::size_t{0});
    source->release();
    faction->release();
}

TEST_CASE("rts.rebuildSnapshotMaterializesTopologyAndRollsBackInvalidRelationships") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    source;
    auto             factionResult = source.newFaction(subject("00000000-0000-7000-8000-000000000261"));
    auto             unitResult    = source.newUnit(subject("00000000-0000-7000-8000-000000000262"));
    auto             nodeResult =
        source.newResourceNode(subject("00000000-0000-7000-8000-000000000263"), "ore", 80.0f, {4.0f, 5.0f}, 2);
    REQUIRE(factionResult.ok());
    REQUIRE(unitResult.ok());
    REQUIRE(nodeResult.ok());
    auto* faction                 = factionResult.value();
    auto* unit                    = unitResult.value();
    auto* node                    = nodeResult.value();
    unit->identity()->displayName = "Harvester";
    unit->motion()->x             = 2.0f;
    auto factionLink              = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto nodeLink                 = eve::rts::ResourceNodeLink::bind(ecs::handle_of(node));
    REQUIRE(factionLink.ok());
    REQUIRE(nodeLink.ok());
    unit->faction()->link        = std::move(factionLink).takeValue();
    unit->worker()->resourceNode = std::move(nodeLink).takeValue();
    faction->members()->units.push_back(ecs::handle_of(unit));
    node->harvest()->workers.push_back(ecs::handle_of(unit));

    auto captured = source.snapshotState();
    REQUIRE(captured.ok());
    eve::rts::RTS rebuilt;
    REQUIRE(rebuilt.rebuildState(captured.value()).ok());
    CHECK_EQ(rebuilt.unitCount(), 1u);
    CHECK_EQ(rebuilt.factionCount(), 1u);
    CHECK_EQ(rebuilt.resourceNodeCount(), 1u);
    auto* restoredUnit = rebuilt.findUnit(unit->identity()->subject);
    REQUIRE(restoredUnit != nullptr);
    CHECK_EQ(restoredUnit->identity()->displayName, "Harvester");
    CHECK(std::abs(restoredUnit->motion()->x - 2.0f) < 1e-5f);
    CHECK(restoredUnit->faction()->link.resolve() != nullptr);
    CHECK(restoredUnit->worker()->resourceNode.resolve() != nullptr);

    eve::rts::RTS invalidTarget;
    auto          invalid         = captured.value();
    invalid.units.front().faction = subject("00000000-0000-7000-8000-000000000269");
    auto rejected                 = invalidTarget.rebuildState(invalid);
    CHECK(!rejected.ok());
    CHECK_EQ(invalidTarget.unitCount(), 0u);
    CHECK_EQ(invalidTarget.factionCount(), 0u);
    CHECK_EQ(invalidTarget.resourceNodeCount(), 0u);
}

TEST_CASE("rts.commandLogRoundTripsStableSubjectsAndAppliesAtExactTick") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    const auto       firstSubject  = subject("00000000-0000-7000-8000-000000000271");
    const auto       secondSubject = subject("00000000-0000-7000-8000-000000000272");
    const auto       targetSubject = subject("00000000-0000-7000-8000-000000000273");
    eve::rts::RTS    source;
    REQUIRE(source.newUnit(firstSubject).ok());
    REQUIRE(source.newUnit(secondSubject).ok());
    REQUIRE(source.newBuilding(targetSubject).ok());

    eve::rts::RTSReplayCommand command;
    command.tick                 = eve::SimulationTick{7};
    command.units                = {secondSubject, firstSubject, secondSubject};
    command.command.kind         = OrderKind::Attack;
    command.command.target       = {9.0f, 4.0f};
    command.command.targetEntity = ecs::handle_of(source.findBuilding(targetSubject));
    command.targetEntity         = targetSubject;
    command.formation            = {eve::rts::FormationKind::Line, 2.0f, 0};
    eve::rts::RTSCommandLog recorded;
    REQUIRE(recorded.queue(command, eve::SimulationTick{3}).ok());
    CHECK_EQ(recorded.size(), 1u);
    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 1\n"));

    eve::rts::RTS replay;
    REQUIRE(replay.newUnit(firstSubject).ok());
    REQUIRE(replay.newUnit(secondSubject).ok());
    REQUIRE(replay.newBuilding(targetSubject).ok());
    eve::rts::RTSCommandLog imported;
    REQUIRE(imported.importText(text, eve::SimulationTick{3}).ok());
    CHECK_EQ(imported.exportText(), text);
    auto early = imported.apply(eve::SimulationTick{6}, replay);
    REQUIRE(early.ok());
    CHECK_EQ(early.value(), 0u);
    auto applied = imported.apply(eve::SimulationTick{7}, replay);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), 2u);
    for (eve::SubjectRef unitSubject : {firstSubject, secondSubject}) {
        auto current = replay.findUnit(unitSubject)->orders()->values.current();
        REQUIRE(current.ok());
        CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::Attack));
        CHECK(ecs::try_get(current.value().targetEntity) == replay.findBuilding(targetSubject));
    }
    CHECK(!imported.importText("EVERTS_COMMANDS 99\n", eve::SimulationTick{}).ok());
    CHECK(!imported.importText(text, eve::SimulationTick{8}).ok());
    CHECK_EQ(imported.size(), 1u);
}

TEST_CASE("rts.genericReplayCoversQueuedMovementGroundFireAndControlOrdersAtExactTicks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    const auto       unitSubject      = subject("00000000-0000-7000-8000-00000000d191");
    const auto       buildingSubject  = subject("00000000-0000-7000-8000-00000000d192");
    const auto       transportSubject = subject("00000000-0000-7000-8000-00000000d193");
    auto*            unit             = module.newUnit(unitSubject).value();
    module.newBuilding(buildingSubject).value();
    module.newUnit(transportSubject).value();
    eve::rts::RTSCommandLog recorded;
    const auto queue = [&](std::uint64_t tick, OrderKind kind, eve::rts::WorldPosition point = {}, bool append = false,
                           eve::SubjectRef target = {}, float spacing = 0.0f) {
        eve::rts::RTSReplayCommand replay;
        replay.tick           = eve::SimulationTick{tick};
        replay.units          = {unitSubject};
        replay.command.kind   = kind;
        replay.command.target = point;
        replay.command.append = append;
        replay.targetEntity   = target;
        if (spacing > 0.0f) {
            replay.formation.kind    = FormationKind::Grid;
            replay.formation.spacing = spacing;
        }
        return recorded.queue(std::move(replay));
    };
    REQUIRE(queue(2, OrderKind::Move, {3.0f, 4.0f}, false, {}, 1.25f).ok());
    REQUIRE(queue(2, OrderKind::AttackMove, {6.0f, 7.0f}, true, {}, 1.5f).ok());
    REQUIRE(queue(3, OrderKind::AttackGround, {8.0f, 9.0f}, true).ok());
    REQUIRE(queue(4, OrderKind::Stop).ok());
    REQUIRE(queue(5, OrderKind::HoldPosition).ok());
    REQUIRE(queue(6, OrderKind::Patrol, {2.0f, 5.0f}).ok());
    REQUIRE(queue(7, OrderKind::Repair, {}, false, buildingSubject).ok());
    REQUIRE(queue(8, OrderKind::Capture, {}, false, buildingSubject).ok());
    REQUIRE(queue(9, OrderKind::Garrison, {}, false, buildingSubject).ok());
    REQUIRE(queue(10, OrderKind::BoardTransport, {}, false, transportSubject).ok());
    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 1\n"));
    eve::rts::RTSCommandLog replay;
    REQUIRE(replay.importText(text).ok());
    CHECK_EQ(replay.exportText(), text);
    auto first = replay.apply(eve::SimulationTick{2}, module);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), std::size_t{2});
    CHECK_EQ(unit->orders()->values.orderCount(), std::size_t{2});
    auto ground = replay.apply(eve::SimulationTick{3}, module);
    REQUIRE(ground.ok());
    CHECK_EQ(unit->orders()->values.orderCount(), std::size_t{3});
    for (std::uint64_t tick = 4; tick <= 10; ++tick) {
        auto applied = replay.apply(eve::SimulationTick{tick}, module);
        REQUIRE(applied.ok());
        CHECK_EQ(applied.value(), std::size_t{1});
        CHECK_EQ(unit->orders()->values.orderCount(), std::size_t{1});
    }
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::BoardTransport));
    CHECK(ecs::try_get(current.value().targetEntity) == module.findUnit(transportSubject));
}

TEST_CASE("rts.fireSupportFacadeRoundTripsAndReplaysRequestAndCancellationAtExactTicks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    module;
    const auto       factionSubject   = subject("00000000-0000-7000-8000-00000000d201");
    const auto       requesterSubject = subject("00000000-0000-7000-8000-00000000d202");
    const auto       batterySubject   = subject("00000000-0000-7000-8000-00000000d203");
    auto             faction          = module.newFaction(factionSubject);
    REQUIRE(faction.ok());
    auto requester = module.newFactionUnit(*faction.value(), requesterSubject);
    auto battery   = module.newFactionUnit(*faction.value(), batterySubject);
    REQUIRE(requester.ok());
    REQUIRE(battery.ok());
    requester.value()->motion()->x       = 0.0f;
    battery.value()->motion()->x         = 2.0f;
    auto*                         weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition indirect;
    indirect.id                 = "replay-howitzer";
    indirect.range              = 30.0f;
    indirect.projectile.speed   = 12.0f;
    indirect.projectile.gravity = 1.0f;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(indirect);
    weapon->definition()->def   = weapon->definition()->owned.get();
    auto weaponLink             = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
    REQUIRE(weaponLink.ok());
    battery.value()->weapon()->link = std::move(weaponLink).takeValue();

    eve::rts::RTSReplayCommand request;
    request.tick           = eve::SimulationTick{4};
    request.operation      = eve::rts::RTSReplayOperation::RequestFireSupport;
    request.producer       = requesterSubject;
    request.point          = {12.0f, 1.0f};
    request.command.radius = 2.5f;
    request.priority       = 4;
    request.limit          = 1;
    eve::rts::RTSReplayCommand cancel;
    cancel.tick      = eve::SimulationTick{5};
    cancel.operation = eve::rts::RTSReplayOperation::CancelFireSupport;
    cancel.producer  = requesterSubject;
    eve::rts::RTSCommandLog recorded;
    REQUIRE(recorded.queue(request).ok());
    REQUIRE(recorded.queue(cancel).ok());
    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 3\n"));
    eve::rts::RTSCommandLog replay;
    REQUIRE(replay.importText(text).ok());
    CHECK_EQ(replay.exportText(), text);
    auto early = replay.apply(eve::SimulationTick{3}, module);
    REQUIRE(early.ok());
    CHECK_EQ(early.value(), 0u);
    auto assigned = replay.apply(eve::SimulationTick{4}, module);
    REQUIRE(assigned.ok());
    CHECK_EQ(assigned.value(), 1u);
    auto order = battery.value()->orders()->values.current();
    REQUIRE(order.ok());
    CHECK_EQ(static_cast<int>(order.value().kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK_EQ(battery.value()->artillery()->suppressionShotsRemaining, 4);
    CHECK_EQ(ecs::try_get(battery.value()->artillery()->fireSupportRequester), requester.value());
    auto cancelled = replay.apply(eve::SimulationTick{5}, module);
    REQUIRE(cancelled.ok());
    CHECK_EQ(cancelled.value(), 1u);
    CHECK(ecs::try_get(battery.value()->artillery()->fireSupportRequester) == nullptr);
    weapon->release();
}

TEST_CASE("rts.commandLogReplaysConstructionProductionResearchAndAbilitiesThroughCanonicalFacades") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(16, 16, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[
          {"id":"worker","role":"worker","health":60,"speed":3,"radius":0.3,"buildRate":1},
          {"id":"marine","producer":"barracks","costResource":"minerals","cost":50,
           "buildTime":1,"health":80,"speed":3,"radius":0.3}],
        "buildings":[
          {"id":"barracks","costResource":"minerals","cost":150,"buildTime":2,"health":500}],
        "upgrades":[
          {"id":"weapons_1","producer":"barracks","targetUnit":"marine",
           "costResource":"minerals","cost":100,"researchTime":2,"attackMultiplier":1.2}],
        "abilities":[
          {"id":"grenade","casterUnit":"marine","targetType":"point","range":6,"radius":1,
           "cooldown":2,"damage":10,"castTime":0.5,"resourceType":"minerals","resourceCost":10}]
    })")
                .ok());
    const auto factionSubject  = subject("00000000-0000-7000-8000-000000000291");
    const auto builderSubject  = subject("00000000-0000-7000-8000-000000000292");
    const auto casterSubject   = subject("00000000-0000-7000-8000-000000000293");
    const auto producerSubject = subject("00000000-0000-7000-8000-000000000294");
    const auto buildingSubject = subject("00000000-0000-7000-8000-000000000295");
    const auto producedSubject = subject("00000000-0000-7000-8000-000000000296");
    const auto workerId        = eve::LogicalId::parse("unit:worker");
    const auto marineId        = eve::LogicalId::parse("unit:marine");
    const auto barracksId      = eve::LogicalId::parse("building:barracks");
    REQUIRE(workerId.has_value());
    REQUIRE(marineId.has_value());
    REQUIRE(barracksId.has_value());
    auto* faction = rts.newFaction(factionSubject).value();
    REQUIRE(rts.newFactionUnit(*faction, builderSubject, *workerId).ok());
    auto* caster        = rts.newFactionUnit(*faction, casterSubject, *marineId).value();
    caster->motion()->x = 1.0f;
    caster->motion()->y = 1.0f;
    REQUIRE(rts.newFactionBuilding(*faction, producerSubject, *barracksId).ok());
    REQUIRE(rts.addScriptResource(*faction, "minerals", 500).ok());

    eve::rts::RTSCommandLog    recorded;
    eve::rts::RTSReplayCommand construction;
    construction.tick          = eve::SimulationTick{2};
    construction.operation     = eve::rts::RTSReplayOperation::Construction;
    construction.units         = {builderSubject};
    construction.faction       = factionSubject;
    construction.resultSubject = buildingSubject;
    construction.definition    = *barracksId;
    construction.point         = {4.0f, 5.0f};
    REQUIRE(recorded.queue(construction).ok());

    eve::rts::RTSReplayCommand production;
    production.tick          = eve::SimulationTick{2};
    production.operation     = eve::rts::RTSReplayOperation::Production;
    production.producer      = producerSubject;
    production.resultSubject = producedSubject;
    production.definition    = *marineId;
    production.priority      = 3;
    REQUIRE(recorded.queue(production).ok());

    eve::rts::RTSReplayCommand research;
    research.tick      = eve::SimulationTick{2};
    research.operation = eve::rts::RTSReplayOperation::Research;
    research.producer  = producerSubject;
    research.value     = "weapons_1";
    REQUIRE(recorded.queue(research).ok());

    eve::rts::RTSReplayCommand ability;
    ability.tick      = eve::SimulationTick{2};
    ability.operation = eve::rts::RTSReplayOperation::Ability;
    ability.units     = {casterSubject};
    ability.value     = "grenade";
    ability.point     = {2.0f, 1.0f};
    REQUIRE(recorded.queue(ability).ok());

    eve::rts::RTSReplayCommand cancelProduction;
    cancelProduction.tick      = eve::SimulationTick{3};
    cancelProduction.operation = eve::rts::RTSReplayOperation::CancelProduction;
    cancelProduction.producer  = producerSubject;
    cancelProduction.priority  = -1;
    REQUIRE(recorded.queue(cancelProduction).ok());

    eve::rts::RTSReplayCommand cancelAbility;
    cancelAbility.tick      = eve::SimulationTick{3};
    cancelAbility.operation = eve::rts::RTSReplayOperation::CancelAbility;
    cancelAbility.units     = {casterSubject};
    REQUIRE(recorded.queue(cancelAbility).ok());

    const std::string text = recorded.exportText();
    CHECK(text.starts_with("EVERTS_COMMANDS 2\n"));
    eve::rts::RTSCommandLog imported;
    REQUIRE(imported.importText(text).ok());
    CHECK_EQ(imported.exportText(), text);
    REQUIRE(imported.apply(eve::SimulationTick{1}, rts).ok());
    auto applied = imported.apply(eve::SimulationTick{2}, rts);
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), std::size_t{4});
    CHECK(rts.findBuilding(buildingSubject) != nullptr);
    CHECK_EQ(rts.findBuilding(producerSubject)->production()->values.taskCount(), std::size_t{2});
    CHECK(caster->abilities()->channel.has_value());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 190);
    auto cancelled = imported.apply(eve::SimulationTick{3}, rts);
    REQUIRE(cancelled.ok());
    CHECK_EQ(cancelled.value(), std::size_t{2});
    CHECK(!caster->abilities()->channel.has_value());
    CHECK_EQ(rts.scriptResource(*faction, "minerals").value(), 290);

    std::string replayCanonical;
    {
        ecs::Table       replayTable;
        ecs::ScopedTable replayGuard(replayTable);
        eve::rts::RTS    replayWorld;
        REQUIRE(replayWorld.configureScriptWorld(16, 16, 1.0f).ok());
        REQUIRE(replayWorld
                    .loadScriptContent(R"({
            "units":[
              {"id":"worker","role":"worker","health":60,"speed":3,"radius":0.3,"buildRate":1},
              {"id":"marine","producer":"barracks","costResource":"minerals","cost":50,
               "buildTime":1,"health":80,"speed":3,"radius":0.3}],
            "buildings":[
              {"id":"barracks","costResource":"minerals","cost":150,"buildTime":2,"health":500}],
            "upgrades":[
              {"id":"weapons_1","producer":"barracks","targetUnit":"marine",
               "costResource":"minerals","cost":100,"researchTime":2,"attackMultiplier":1.2}],
            "abilities":[
              {"id":"grenade","casterUnit":"marine","targetType":"point","range":6,"radius":1,
               "cooldown":2,"damage":10,"castTime":0.5,"resourceType":"minerals","resourceCost":10}]
        })")
                    .ok());
        auto* replayFaction = replayWorld.newFaction(factionSubject).value();
        REQUIRE(replayWorld.newFactionUnit(*replayFaction, builderSubject, *workerId).ok());
        auto* replayCaster        = replayWorld.newFactionUnit(*replayFaction, casterSubject, *marineId).value();
        replayCaster->motion()->x = 1.0f;
        replayCaster->motion()->y = 1.0f;
        REQUIRE(replayWorld.newFactionBuilding(*replayFaction, producerSubject, *barracksId).ok());
        REQUIRE(replayWorld.addScriptResource(*replayFaction, "minerals", 500).ok());
        eve::rts::RTSCommandLog replayLog;
        REQUIRE(replayLog.importText(text).ok());
        REQUIRE(replayLog.apply(eve::SimulationTick{2}, replayWorld).ok());
        REQUIRE(replayLog.apply(eve::SimulationTick{3}, replayWorld).ok());
        CHECK_EQ(replayWorld.scriptResource(*replayFaction, "minerals").value(), 290);
        auto canonical = replayWorld.canonicalStateJson();
        REQUIRE(canonical.ok());
        replayCanonical = canonical.value();
    }
    auto sourceCanonical = rts.canonicalStateJson();
    REQUIRE(sourceCanonical.ok());
    CHECK_EQ(replayCanonical, sourceCanonical.value());
}

TEST_CASE("rts.lockstepAppliesCommandsBeforeExactFixedSimulationTicks") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    const auto       unitSubject = subject("00000000-0000-7000-8000-000000000281");
    eve::rts::RTS    simulation;
    REQUIRE(simulation.newUnit(unitSubject).ok());
    eve::rts::RTSReplayCommand command;
    command.tick              = eve::SimulationTick{2};
    command.units             = {unitSubject};
    command.command.kind      = OrderKind::Move;
    command.command.target    = {6.0f, 3.0f};
    command.formation.spacing = 1.0f;
    eve::rts::RTSLockstep lockstep;
    auto                  tenth = eve::Duration::fromSeconds(0.1);
    REQUIRE(tenth.ok());
    REQUIRE(lockstep.setFixedStep(tenth.value()).ok());
    REQUIRE(lockstep.queue(command).ok());
    PendingRTSExecutor executor;
    auto               first = lockstep.step(simulation, executor);
    REQUIRE(first.ok());
    CHECK_EQ(lockstep.currentTick().value(), 1u);
    CHECK(!simulation.findUnit(unitSubject)->orders()->values.current().ok());
    auto second = lockstep.step(simulation, executor);
    REQUIRE(second.ok());
    CHECK_EQ(lockstep.currentTick().value(), 2u);
    auto current = simulation.findUnit(unitSubject)->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::Move));
    CHECK(std::abs(current.value().target.x - 6.0f) < 1e-5f);
    CHECK(std::abs(current.value().target.y - 3.0f) < 1e-5f);
    CHECK(!lockstep.setFixedStep(eve::Duration::zero()).ok());
    lockstep.reset(eve::SimulationTick{9});
    CHECK_EQ(lockstep.currentTick().value(), 9u);
    CHECK_EQ(lockstep.commands().size(), 0u);
}

TEST_CASE("rts.canonicalStateAndInjectedHashIgnoreCreationOrderAndDetectMutation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    const auto       first  = subject("00000000-0000-7000-8000-000000000291");
    const auto       second = subject("00000000-0000-7000-8000-000000000292");
    const auto       base   = subject("00000000-0000-7000-8000-000000000293");
    eve::rts::RTS    left;
    eve::rts::RTS    right;
    REQUIRE(left.newUnit(first).ok());
    REQUIRE(left.newUnit(second).ok());
    REQUIRE(left.newBuilding(base).ok());
    REQUIRE(right.newBuilding(base).ok());
    REQUIRE(right.newUnit(second).ok());
    REQUIRE(right.newUnit(first).ok());
    left.findBuilding(base)->infrastructure()->powerProduced  = 5.0f;
    right.findBuilding(base)->infrastructure()->powerProduced = 5.0f;
    left.findUnit(first)->motion()->x                         = 3.0f;
    right.findUnit(first)->motion()->x                        = 3.0f;
    REQUIRE(left.findUnit(first)->tags()->values.add("unit.scout").ok());
    REQUIRE(right.findUnit(first)->tags()->values.add("unit.scout").ok());
    auto leftJson  = left.canonicalStateJson();
    auto rightJson = right.canonicalStateJson();
    REQUIRE(leftJson.ok());
    REQUIRE(rightJson.ok());
    CHECK_EQ(leftJson.value(), rightJson.value());
    CHECK(eve::Value::fromJson(leftJson.value()).ok());

    std::string hashedInput;
    auto        digest = eve::ContentId::parse("00000000-0000-7000-8000-000000000299");
    REQUIRE(digest.has_value());
    eve::SnapshotHashProvider provider = [&](std::string_view input) {
        hashedInput = input;
        return eve::Result<eve::ContentId>::success(*digest);
    };
    auto hash = left.stateHash(provider);
    REQUIRE(hash.ok());
    CHECK_EQ(hash.value(), *digest);
    CHECK_EQ(hashedInput, leftJson.value());
    right.findUnit(first)->worker()->cargo                    = 1.0f;
    right.findBuilding(base)->infrastructure()->powerProduced = 6.0f;
    auto changed                                              = right.canonicalStateJson();
    REQUIRE(changed.ok());
    CHECK_NE(changed.value(), leftJson.value());
    CHECK(!left.stateHash({}).ok());
}

TEST_CASE("rts.facadeMigratesCombatStanceAndMovementPriorityIntoSnapshots") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    const auto       firstSubject  = subject("00000000-0000-7000-8000-0000000002c1");
    const auto       secondSubject = subject("00000000-0000-7000-8000-0000000002c2");
    auto             firstCreated  = rts.newUnit(firstSubject);
    auto             secondCreated = rts.newUnit(secondSubject);
    REQUIRE(firstCreated.ok());
    REQUIRE(secondCreated.ok());
    auto* first         = firstCreated.value();
    auto* second        = secondCreated.value();
    first->motion()->x  = 3.0f;
    first->motion()->y  = 4.0f;
    second->motion()->x = 7.0f;
    second->motion()->y = 8.0f;
    const std::vector<eve::SubjectRef> selection{firstSubject, secondSubject};

    REQUIRE(rts.setUnitStance(selection, eve::rts::CombatStance::Passive, 6.0f).ok());
    REQUIRE(rts.setUnitMovementPriority(selection, 250).ok());
    CHECK_EQ(static_cast<int>(first->combat()->stance), static_cast<int>(eve::rts::CombatStance::Passive));
    CHECK_EQ(first->combat()->leashRange, 6.0f);
    CHECK_EQ(first->combat()->guardX, 3.0f);
    CHECK_EQ(first->combat()->guardY, 4.0f);
    CHECK_EQ(second->navigation()->movementPriority, 100);

    const std::vector<eve::SubjectRef> invalid{firstSubject, subject("00000000-0000-7000-8000-0000000002cf")};
    auto                               rejected = rts.setUnitMovementPriority(invalid, -50);
    CHECK(!rejected.ok());
    CHECK_EQ(first->navigation()->movementPriority, 100);

    auto snapshot = rts.snapshotState();
    REQUIRE(snapshot.ok());
    CHECK_EQ(static_cast<int>(snapshot.value().units.front().combat.stance),
             static_cast<int>(eve::rts::CombatStance::Passive));
    auto canonical = rts.canonicalStateJson();
    REQUIRE(canonical.ok());
    CHECK(canonical.value().find("\"stance\":\"passive\"") != std::string::npos);
}
