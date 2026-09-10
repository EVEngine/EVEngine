#include "RtsCompositionFixtures.h"

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
    auto repaired = eve::rts::RepairSystem::step(oneSecond, debit);
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
    auto research = lab->production()->values.enqueue("faction", "research", "veteran_training", duration.value());
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
