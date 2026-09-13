#include "RtsCompositionFixtures.h"

TEST_CASE("rts.buildingTouchesConstructionDropoffAndRally") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Building* building = Building::createBuilding();
    CHECK(std::abs(building->construction()->progress - 1.0f) < 1e-5f);
    CHECK(building->dropoff()->acceptedResources.empty());
    CHECK(!building->rally()->enabled);
    building->release();
}

TEST_CASE("rts.aiRequestsProductionAndLaunchesAttackMove") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue     = Faction::createFaction();
    Faction*         red      = Faction::createFaction();
    Building*        producer = Building::createBuilding();
    Building*        target   = Building::createBuilding();
    const auto       workerId = eve::LogicalId::parse("rts:worker");
    const auto       armyId   = eve::LogicalId::parse("rts:marine");
    const auto       baseId   = eve::LogicalId::parse("rts:command-center");
    REQUIRE(workerId.has_value());
    REQUIRE(armyId.has_value());
    REQUIRE(baseId.has_value());
    Unit* first    = Unit::createUnit({}, *armyId);
    Unit* second   = Unit::createUnit({}, *armyId);
    auto  bindUnit = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    auto bindBuilding = [&](Building& building, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        building.faction()->link = std::move(link).takeValue();
    };
    bindUnit(*first, *blue);
    bindUnit(*second, *blue);
    bindBuilding(*producer, *blue);
    bindBuilding(*target, *red);
    target->definition()->id                   = *baseId;
    target->placement()->worldX                = 20.0f;
    target->placement()->worldY                = 4.0f;
    blue->strategy()->enabled                  = true;
    blue->strategy()->workerDefinition         = *workerId;
    blue->strategy()->armyDefinition           = *armyId;
    blue->strategy()->targetBuildingDefinition = *baseId;
    blue->strategy()->desiredWorkers           = 0;
    blue->strategy()->attackThreshold          = 2;
    blue->strategy()->thinkInterval            = 0.5f;

    int                       productionRequests = 0;
    eve::LogicalId            requestedDefinition;
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("AI dt")};
    auto                      result =
        eve::rts::AISystem::step(step, [&](Faction& faction, Building& building, const eve::LogicalId& definition) {
            CHECK(&faction == blue);
            CHECK(&building == producer);
            ++productionRequests;
            requestedDefinition = definition;
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        });
    REQUIRE(result.ok());
    CHECK_EQ(productionRequests, 1);
    CHECK(requestedDefinition == *armyId);
    CHECK(!first->orders()->values.empty());
    CHECK(!second->orders()->values.empty());
    CHECK_EQ(first->tactics()->combatGroup, second->tactics()->combatGroup);
    CHECK(first->tactics()->combatGroup != 0);

    first->release();
    second->release();
    producer->release();
    target->release();
    blue->release();
    red->release();
}

TEST_CASE("rts.transportBoardingUsesGenerationCheckedContainment") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Faction* faction          = Faction::createFaction();
    Unit*    transport        = Unit::createUnit();
    Unit*    passenger        = Unit::createUnit();
    auto     transportFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto     passengerFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(transportFaction.ok());
    REQUIRE(passengerFaction.ok());
    transport->faction()->link         = std::move(transportFaction).takeValue();
    passenger->faction()->link         = std::move(passengerFaction).takeValue();
    transport->containment()->capacity = 2;
    transport->motion()->x             = 4.0f;
    transport->motion()->y             = 7.0f;
    passenger->motion()->arrived       = true;

    CommandSpec board;
    board.kind         = OrderKind::BoardTransport;
    board.target       = {4.0f, 7.0f};
    board.targetEntity = ecs::handle_of(transport);
    auto queued        = passenger->orders()->values.enqueue(board);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();
    auto boarded = eve::rts::ContainmentSystem::step();
    REQUIRE(boarded.ok());
    CHECK_EQ(boarded.value(), 1u);
    CHECK(passenger->containment()->container.resolve() == transport);
    CHECK_EQ(transport->containment()->occupants.size(), 1u);
    CHECK(passenger->orders()->values.empty());

    transport->motion()->x = 9.0f;
    auto synchronized      = eve::rts::ContainmentSystem::step();
    REQUIRE(synchronized.ok());
    CHECK(std::abs(passenger->motion()->x - 9.0f) < 1e-5f);

    auto unloaded = eve::rts::ContainmentSystem::unload(*transport, {12.0f, 3.0f});
    REQUIRE(unloaded.ok());
    CHECK_EQ(unloaded.value(), 1u);
    CHECK(transport->containment()->occupants.empty());
    CHECK(!passenger->containment()->container.isBound());
    CHECK(passenger->motion()->x > 12.0f);

    Building* bunker        = Building::createBuilding();
    auto      bunkerFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(bunkerFaction.ok());
    bunker->faction()->link = std::move(bunkerFaction).takeValue();
    auto bunkerLink         = eve::rts::ContainerLink::bind(ecs::handle_of(bunker));
    REQUIRE(bunkerLink.ok());
    passenger->containment()->container = std::move(bunkerLink).takeValue();
    bunker->garrison()->occupants.push_back(ecs::handle_of(passenger));
    bunker->capture()->blockedByGarrison = true;
    auto evacuated                       = eve::rts::ContainmentSystem::evacuate(*bunker, {15.0f, 5.0f});
    REQUIRE(evacuated.ok());
    CHECK_EQ(evacuated.value(), 1u);
    CHECK(bunker->garrison()->occupants.empty());
    CHECK(!bunker->capture()->blockedByGarrison);
    CHECK(!passenger->containment()->container.isBound());

    passenger->release();
    transport->release();
    bunker->release();
    faction->release();
}

TEST_CASE("rts.buildingProductionAndModuleFactoryCompose") {
    eve::rts::RTS module;
    auto          unitResult = module.newUnit(subject("00000000-0000-7000-8000-000000000011"));
    REQUIRE(unitResult.ok());
    Unit* unit = std::move(unitResult).takeValue();
    CHECK_EQ(module.unitCount(), 1u);

    auto buildingResult = module.newBuilding(subject("00000000-0000-7000-8000-000000000012"));
    REQUIRE(buildingResult.ok());
    Building* building = std::move(buildingResult).takeValue();
    CHECK_EQ(module.buildingCount(), 1u);

    auto duration = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    auto task = building->production()->values.enqueue("faction", "train", "worker", std::move(duration).takeValue());
    REQUIRE(task.ok());
    std::move(task).takeValue();

    eve::action::ActionRuntime runtime;
    eve::rts::ActionAdapter    adapter(runtime);
    const eve::SimulationStep  step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(500000000)};
    auto                       processed = module.step(step, adapter);
    REQUIRE(processed.ok());
    CHECK(processed.value() >= 2u);
    CHECK_EQ(building->production()->values.taskCount(), 1u);

    (void)unit;
}

TEST_CASE("rts.completedProductionBoardsDispatchesAndUnloadsTransportReinforcements") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction          = Faction::createFaction();
    Building*        factory          = Building::createBuilding();
    Unit*            transport        = Unit::createUnit();
    auto             factoryFaction   = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto             transportFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(factoryFaction.ok());
    REQUIRE(transportFaction.ok());
    factory->faction()->link               = std::move(factoryFaction).takeValue();
    transport->faction()->link             = std::move(transportFaction).takeValue();
    transport->containment()->capacity     = 2;
    factory->rally()->enabled              = true;
    factory->rally()->command.kind         = OrderKind::AttackMove;
    factory->rally()->command.target       = {20.0f, 4.0f};
    factory->rally()->combatGroup          = 77;
    factory->rally()->transport            = ecs::handle_of(transport);
    const auto transportHandle             = ecs::handle_of(transport);
    factory->rally()->minimumTransportLoad = 2;
    const auto duration                    = eve::Duration::fromSeconds(1.0).expect("reinforcement duration");
    REQUIRE(factory->production()->values.enqueue("faction", "unit", "marine", duration).ok());
    REQUIRE(factory->production()->values.enqueue("faction", "unit", "marine", duration).ok());

    std::vector<ecs::EntityHandle> spawned;
    eve::rts::ProductionSpawn      spawn = [&](Building&, const eve::production::ProductionTask&) {
        Unit* unit = Unit::createUnit();
        auto  link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        if (!link) return eve::Result<Unit*>::failure(link.status());
        unit->faction()->link = std::move(link).takeValue();
        spawned.push_back(ecs::handle_of(unit));
        return eve::Result<Unit*>::success(unit);
    };
    auto first = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{1}, duration}, spawn);
    REQUIRE(first.ok());
    auto second = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{2}, duration}, spawn);
    REQUIRE(second.ok());
    REQUIRE_EQ(spawned.size(), 2u);
    transport = dynamic_cast<Unit*>(ecs::try_get(transportHandle));
    REQUIRE(transport != nullptr);
    CHECK_EQ(transport->containment()->occupants.size(), 2u);

    auto dispatched = eve::rts::ReinforcementSystem::step();
    REQUIRE(dispatched.ok());
    CHECK(factory->rally()->transportActive);
    transport->motion()->arrived = false;
    auto travelling              = eve::rts::ReinforcementSystem::step();
    REQUIRE(travelling.ok());
    CHECK_EQ(transport->containment()->occupants.size(), 2u);
    transport->motion()->x       = 20.0f;
    transport->motion()->y       = 4.0f;
    transport->motion()->arrived = true;
    auto arrived                 = eve::rts::ReinforcementSystem::step();
    REQUIRE(arrived.ok());
    CHECK(!factory->rally()->transportActive);
    CHECK(transport->containment()->occupants.empty());
    for (const auto& handle : spawned) {
        Unit* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        REQUIRE(unit != nullptr);
        CHECK(!unit->containment()->container.isBound());
        CHECK_EQ(unit->tactics()->combatGroup, 77u);
        CHECK_EQ(unit->motion()->x, 20.0f);
        auto order = unit->orders()->values.current();
        REQUIRE(order.ok());
        CHECK_EQ(static_cast<int>(order.value().kind), static_cast<int>(OrderKind::AttackMove));
    }
}

TEST_CASE("rts.factionResourceFloorsProtectHighPriorityProductionAcrossFactories") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    auto             factionResult  = rts.newFaction(subject("00000000-0000-7000-8000-0000000004a1"));
    auto             barracksResult = rts.newBuilding(subject("00000000-0000-7000-8000-0000000004a2"));
    auto             factoryResult  = rts.newBuilding(subject("00000000-0000-7000-8000-0000000004a3"));
    REQUIRE(factionResult.ok());
    REQUIRE(barracksResult.ok());
    REQUIRE(factoryResult.ok());
    Faction*  faction  = std::move(factionResult).takeValue();
    Building* barracks = std::move(barracksResult).takeValue();
    Building* factory  = std::move(factoryResult).takeValue();
    for (Building* building : {barracks, factory}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        building->faction()->link = std::move(link).takeValue();
    }
    REQUIRE(rts.setProductionResourceReserve(*faction, "minerals", 100, 10).ok());
    REQUIRE(rts.setProductionResourceReserve(*faction, "gas", 25, 10).ok());
    eve::economy::EconomyLedger ledger;
    REQUIRE_EQ(ledger.credit("minerals", 150), 150);
    REQUIRE_EQ(ledger.credit("gas", 25), 25);
    eve::rts::RTSEconomyAdapter economy(ledger);
    eve::action::ActionRuntime  action;
    const auto                  duration = eve::Duration::fromSeconds(1.0).expect("resource floor production duration");

    auto marineCost = eve::resource::CostSpec::single("minerals", 50);
    REQUIRE(marineCost.ok());
    auto firstMarine = rts.build(*barracks, action, economy.account(), std::move(marineCost).takeValue(), "marine",
                                 duration, "unit", 1);
    REQUIRE(firstMarine.ok());
    CHECK_EQ(ledger.get("minerals"), 100);
    marineCost = eve::resource::CostSpec::single("minerals", 50);
    REQUIRE(marineCost.ok());
    auto blockedMarine = rts.build(*barracks, action, economy.account(), std::move(marineCost).takeValue(), "marine",
                                   duration, "unit", 1);
    CHECK(!blockedMarine.ok());
    CHECK_EQ(ledger.get("minerals"), 100);

    auto tankCost = eve::resource::CostSpec::from({{"minerals", 100}, {"gas", 25}});
    REQUIRE(tankCost.ok());
    auto tank =
        rts.build(*factory, action, economy.account(), std::move(tankCost).takeValue(), "tank", duration, "unit", 10);
    REQUIRE(tank.ok());
    CHECK_EQ(ledger.get("minerals"), 0);
    CHECK_EQ(ledger.get("gas"), 0);
    auto snapshot = rts.snapshotState();
    REQUIRE(snapshot.ok());
    REQUIRE_EQ(snapshot.value().factions.size(), 1u);
    CHECK_EQ(snapshot.value().factions[0].productionPolicy.resourceReserves.at("minerals").amount, 100);
    auto canonical = rts.canonicalStateJson();
    REQUIRE(canonical.ok());
    CHECK(canonical.value().find("productionPolicy") != std::string::npos);

    REQUIRE(rts.setProductionResourceReserve(*faction, "minerals", 0, 10).ok());
    CHECK(!faction->productionPolicy()->resourceReserves.contains("minerals"));
}

TEST_CASE("rts.sharedReinforcementCapReservesLastSlotByTypePriority") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction         = Faction::createFaction();
    Building*        infantryFactory = Building::createBuilding(subject("00000000-0000-7000-8000-0000000003f1"));
    Building*        armorFactory    = Building::createBuilding(subject("00000000-0000-7000-8000-0000000003f2"));
    for (Building* factory : {infantryFactory, armorFactory}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        factory->faction()->link                                    = std::move(link).takeValue();
        factory->rally()->enabled                                   = true;
        factory->rally()->combatGroup                               = 91;
        factory->rally()->reinforcementLimit                        = 1;
        factory->rally()->reinforcementTypePriorities["rts:marine"] = 1;
        factory->rally()->reinforcementTypePriorities["rts:tank"]   = 10;
    }
    const auto duration   = eve::Duration::fromSeconds(5.0).expect("reinforcement policy duration");
    auto       marineTask = infantryFactory->production()->values.enqueue("faction", "unit", "rts:marine", duration);
    auto       tankTask   = armorFactory->production()->values.enqueue("faction", "unit", "rts:tank", duration);
    REQUIRE(marineTask.ok());
    REQUIRE(tankTask.ok());
    const std::string marineId = marineTask.value();
    const std::string tankId   = tankTask.value();

    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step().ok());
    REQUIRE(infantryFactory->production()->values.find(marineId));
    REQUIRE(armorFactory->production()->values.find(tankId));
    CHECK_EQ(static_cast<int>(infantryFactory->production()->values.find(marineId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));
    CHECK_NE(static_cast<int>(armorFactory->production()->values.find(tankId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));
    CHECK(infantryFactory->rally()->reinforcementCapped);

    const auto tankDefinition = eve::LogicalId::parse("rts:tank");
    REQUIRE(tankDefinition.has_value());
    Unit* existing        = Unit::createUnit({}, *tankDefinition);
    auto  existingFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(existingFaction.ok());
    existing->faction()->link        = std::move(existingFaction).takeValue();
    existing->tactics()->combatGroup = 91;
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step().ok());
    CHECK_EQ(static_cast<int>(armorFactory->production()->values.find(tankId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));

    existing->durability()->alive = false;
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step().ok());
    CHECK_NE(static_cast<int>(armorFactory->production()->values.find(tankId)->get().state),
             static_cast<int>(eve::production::TaskState::Paused));
    CHECK(armorFactory->rally()->reinforcementPolicyPausedTask.empty());

    existing->release();
    armorFactory->release();
    infantryFactory->release();
    faction->release();
}

TEST_CASE("rts.reinforcementRequestUsesDeterministicFallbackChain") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Building*        factory                             = Building::createBuilding();
    factory->rally()->reinforcementFallbacks["rts:tank"] = "rts:marine";
    std::vector<std::string>       attempts;
    eve::rts::ReinforcementEnqueue enqueue = [&](Building& producer, std::string_view product) {
        CHECK_EQ(&producer, factory);
        attempts.emplace_back(product);
        if (product == "rts:tank")
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "tank unavailable", "product"));
        return producer.production()->values.enqueue("faction", "unit", product,
                                                     eve::Duration::fromSeconds(1.0).expect("fallback duration"));
    };
    auto requested = eve::rts::ReinforcementProductionPolicySystem::request(*factory, "rts:tank", enqueue);
    REQUIRE(requested.ok());
    CHECK_EQ(requested.value().requestedProduct, "rts:tank");
    CHECK_EQ(requested.value().queuedProduct, "rts:marine");
    REQUIRE_EQ(attempts.size(), 2u);
    CHECK_EQ(attempts[0], "rts:tank");
    CHECK_EQ(attempts[1], "rts:marine");

    factory->rally()->reinforcementFallbacks["rts:marine"] = "rts:tank";
    auto cyclic =
        eve::rts::ReinforcementProductionPolicySystem::request(*factory, "rts:tank", [&](Building&, std::string_view) {
            return eve::Result<std::string>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "unavailable", "product"));
        });
    CHECK(!cyclic.ok());
    CHECK_EQ(cyclic.code(), eve::StatusCode::Rejected);
    factory->release();
}

TEST_CASE("rts.cappedReinforcementUsesInjectedAtomicCancelRefundBoundary") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         faction         = Faction::createFaction();
    Building*        factory         = Building::createBuilding();
    Unit*            existing        = Unit::createUnit();
    auto             buildingFaction = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    auto             unitFaction     = eve::rts::FactionLink::bind(ecs::handle_of(faction));
    REQUIRE(buildingFaction.ok());
    REQUIRE(unitFaction.ok());
    factory->faction()->link                       = std::move(buildingFaction).takeValue();
    existing->faction()->link                      = std::move(unitFaction).takeValue();
    factory->rally()->enabled                      = true;
    factory->rally()->combatGroup                  = 92;
    factory->rally()->reinforcementLimit           = 1;
    factory->rally()->reinforcementAutoCancelDelay = 1.0f;
    existing->tactics()->combatGroup               = 92;
    auto task = factory->production()->values.enqueue("faction", "unit", "rts:marine",
                                                      eve::Duration::fromSeconds(5.0).expect("auto cancel duration"));
    REQUIRE(task.ok());
    const std::string             taskId   = task.value();
    bool                          refunded = false;
    eve::rts::ReinforcementCancel cancel   = [&](Building& producer, std::string_view id) {
        CHECK_EQ(&producer, factory);
        CHECK_EQ(id, taskId);
        auto cancelled = producer.production()->values.cancel(id, "reinforcement capped");
        if (cancelled) refunded = true;
        return cancelled;
    };
    const eve::SimulationStep half{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("half capped delay")};
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step(half, cancel).ok());
    CHECK(!refunded);
    const eve::SimulationStep secondHalf{eve::SimulationTick{2}, half.delta};
    REQUIRE(eve::rts::ReinforcementProductionPolicySystem::step(secondHalf, cancel).ok());
    CHECK(refunded);
    REQUIRE(factory->production()->values.find(taskId));
    CHECK_EQ(static_cast<int>(factory->production()->values.find(taskId)->get().state),
             static_cast<int>(eve::production::TaskState::Cancelled));
    CHECK(!factory->rally()->reinforcementCapped);

    existing->release();
    factory->release();
    faction->release();
}

TEST_CASE("rts.productionBlockedExitWaitsAndSpawnsAtFirstAvailablePosition") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Building*        factory     = Building::createBuilding();
    factory->placement()->worldX = 4.0f;
    factory->placement()->worldY = 5.0f;
    const auto duration          = eve::Duration::fromSeconds(1.0).expect("blocked exit duration");
    auto       queued            = factory->production()->values.enqueue("faction", "unit", "marine", duration);
    REQUIRE(queued.ok());
    const std::string                 taskId   = std::move(queued).takeValue();
    int                               probes   = 0;
    eve::rts::ProductionSpawnPosition position = [&](Building&, const eve::production::ProductionTask&) {
        ++probes;
        if (probes == 1) return eve::Result<std::optional<eve::rts::WorldPosition>>::success(std::nullopt);
        return eve::Result<std::optional<eve::rts::WorldPosition>>::success(eve::rts::WorldPosition{8.0f, 9.0f});
    };
    std::vector<ecs::EntityHandle> spawned;
    eve::rts::ProductionSpawn      spawn = [&](Building&, const eve::production::ProductionTask&) {
        Unit* unit = Unit::createUnit();
        spawned.push_back(ecs::handle_of(unit));
        return eve::Result<Unit*>::success(unit);
    };

    std::vector<eve::rts::LifecycleEvent> productionEvents;
    auto collectProduction = [&](const eve::rts::LifecycleEvent& event, eve::SimulationTick) {
        productionEvents.push_back(event);
    };
    auto blocked = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{1}, duration}, spawn, position,
                                                            collectProduction);
    REQUIRE(blocked.ok());
    CHECK(spawned.empty());
    CHECK(factory->rally()->productionSpawnBlocked);
    CHECK_EQ(factory->rally()->blockedProductionTask, taskId);
    CHECK(factory->rally()->settledProductionTasks.empty());
    REQUIRE_EQ(productionEvents.size(), 1u);
    CHECK_EQ(static_cast<int>(productionEvents[0].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::ProductionSpawnBlocked));

    auto cleared = eve::rts::BuildingProductionSystem::step({eve::SimulationTick{2}, duration}, spawn, position,
                                                            collectProduction);
    REQUIRE(cleared.ok());
    REQUIRE_EQ(spawned.size(), 1u);
    auto* unit = dynamic_cast<Unit*>(ecs::try_get(spawned.front()));
    REQUIRE(unit != nullptr);
    CHECK_EQ(unit->motion()->x, 8.0f);
    CHECK_EQ(unit->motion()->y, 9.0f);
    CHECK(!factory->rally()->productionSpawnBlocked);
    CHECK(factory->rally()->blockedProductionTask.empty());
    CHECK_EQ(factory->rally()->settledProductionTasks.size(), 1u);
    REQUIRE_EQ(productionEvents.size(), 3u);
    CHECK_EQ(static_cast<int>(productionEvents[1].kind),
             static_cast<int>(eve::rts::LifecycleEventKind::ProductionSpawnCleared));
    CHECK_EQ(static_cast<int>(productionEvents[2].kind), static_cast<int>(eve::rts::LifecycleEventKind::UnitProduced));
}

TEST_CASE("rts.rallyFacadeConfiguresLinkedReinforcementPolicyAndExclusiveTransport") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(16, 16, 1.0f).ok());
    REQUIRE(rts.loadScriptContent(R"({
        "units":[{"id":"marine","health":80,"speed":3,"radius":0.3},
                 {"id":"medic","health":60,"speed":3,"radius":0.3},
                 {"id":"apc","health":200,"speed":4,"radius":0.8}],
        "buildings":[{"id":"barracks","health":500,"buildTime":2}]
    })")
                .ok());
    auto*      faction    = rts.newFaction(subject("00000000-0000-7000-8000-0000000002a1")).value();
    const auto barracksId = eve::LogicalId::parse("building:barracks");
    const auto apcId      = eve::LogicalId::parse("unit:apc");
    REQUIRE(barracksId.has_value());
    REQUIRE(apcId.has_value());
    auto* first =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-0000000002a2"), *barracksId).value();
    auto* second =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-0000000002a3"), *barracksId).value();
    auto* third =
        rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-0000000002a4"), *barracksId).value();
    auto* transport = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-0000000002a5"), *apcId).value();
    transport->containment()->capacity = 4;

    CommandSpec rally;
    rally.kind   = OrderKind::AttackMove;
    rally.target = {12.0f, 6.0f};
    REQUIRE(rts.setBuildingRally(*first, rally, true).ok());
    REQUIRE(rts.linkBuildingRally(*second, *first).ok());
    CHECK(first->rally()->combatGroup != 0);
    CHECK_EQ(second->rally()->combatGroup, first->rally()->combatGroup);
    REQUIRE(rts.setReinforcementLimit(*first, 8).ok());
    REQUIRE(rts.setReinforcementTypeLimit(*first, "marine", 5).ok());
    REQUIRE(rts.setReinforcementTypePriority(*second, "medic", 3).ok());
    REQUIRE(rts.setReinforcementFallback(*first, "medic", "marine").ok());
    CHECK(!rts.setReinforcementFallback(*first, "marine", "medic").ok());
    REQUIRE(rts.setReinforcementAutoCancel(*first, 2.5f).ok());
    for (auto* building : {first, second}) {
        CHECK_EQ(building->rally()->reinforcementLimit, std::size_t{8});
        CHECK_EQ(building->rally()->reinforcementTypeLimits.at("marine"), std::size_t{5});
        CHECK_EQ(building->rally()->reinforcementTypePriorities.at("medic"), 3);
        CHECK_EQ(building->rally()->reinforcementFallbacks.at("medic"), "marine");
        CHECK_EQ(building->rally()->reinforcementAutoCancelDelay, 2.5f);
    }
    REQUIRE(rts.setReinforcementTransport(*first, transport, 2).ok());
    CHECK(!rts.setReinforcementTransport(*third, transport, 2).ok());
    REQUIRE(rts.setReinforcementTransport(*first, nullptr).ok());
    REQUIRE(rts.setReinforcementTransport(*third, transport, 2).ok());
    REQUIRE(rts.clearBuildingRally(*second).ok());
    CHECK(!second->rally()->enabled);
    CHECK_EQ(second->rally()->combatGroup, std::uint64_t{0});
}
