#include "RtsCompositionFixtures.h"

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
