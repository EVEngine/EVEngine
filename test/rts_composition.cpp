#include "RtsCompositionFixtures.h"

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

TEST_CASE("rts.largeExactlyOverlappingCrowdUnlocksDeterministically") {
    auto simulate = [&] {
        ecs::Table world;
        ecs::ScopedTable guard(world);
        eve::crowd::Crowd crowd;
        crowd.setClampToField(false);
        crowd.setResolveOverlaps(true);
        crowd.setSeparationRadius(2.0f);
        crowd.setSeparationWeight(1.0f);
        std::vector<Unit*> units;
        units.reserve(96);
        for (int index = 0; index < 96; ++index) {
            Unit* unit = Unit::createUnit();
            auto link = eve::rts::CrowdLink::bind("rts/overlap/" + std::to_string(index));
            REQUIRE(link.ok());
            unit->crowd()->link = std::move(link).takeValue();
            unit->crowd()->radius = 0.45f;
            unit->motion()->speed = 4.0f;
            CommandSpec move;
            move.kind = OrderKind::Move;
            move.target = {20.0f, 10.0f};
            REQUIRE(unit->orders()->values.enqueue(move).ok());
            units.push_back(unit);
        }
        for (std::uint64_t tick = 1; tick <= 120; ++tick) {
            const eve::SimulationStep step{eve::SimulationTick{tick},
                eve::Duration::fromSeconds(0.05).expect("large crowd dt")};
            REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
        }
        std::vector<WorldPosition> positions;
        positions.reserve(units.size());
        for (Unit* unit : units) positions.push_back({unit->motion()->x, unit->motion()->y});
        for (Unit* unit : units) unit->release();
        return positions;
    };

    const auto first = simulate();
    const auto second = simulate();
    REQUIRE_EQ(first.size(), second.size());
    int progressed = 0;
    for (std::size_t index = 0; index < first.size(); ++index) {
        progressed += first[index].x > 5.0f;
        CHECK(std::abs(first[index].x - second[index].x) < 1e-5f);
        CHECK(std::abs(first[index].y - second[index].y) < 1e-5f);
    }
    CHECK(progressed > 80);
}

TEST_CASE("rts.linkedAirDefensesFormStableNetworksAndDistributeAircraft") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction* blue = Faction::createFaction(subject("00000000-0000-7000-8000-000000000061"));
    Faction* red = Faction::createFaction(subject("00000000-0000-7000-8000-000000000062"));
    Building* first = Building::createBuilding(subject("00000000-0000-7000-8000-000000000063"));
    Building* second = Building::createBuilding(subject("00000000-0000-7000-8000-000000000064"));
    Unit* left = Unit::createUnit(subject("00000000-0000-7000-8000-000000000065"));
    Unit* right = Unit::createUnit(subject("00000000-0000-7000-8000-000000000066"));
    auto bindBuilding = [&](Building& building) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(blue));
        REQUIRE(link.ok());
        building.faction()->link = std::move(link).takeValue();
    };
    auto bindAircraft = [&](Unit& unit) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(red));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
        unit.motion()->airborne = true;
        unit.durability()->state.health = unit.durability()->state.maxHealth = 100.0;
    };
    bindBuilding(*first); bindBuilding(*second); bindAircraft(*left); bindAircraft(*right);
    first->placement()->worldX = 0.0f;
    second->placement()->worldX = 6.0f;
    left->motion()->x = 3.0f; left->motion()->y = 4.0f;
    right->motion()->x = 7.0f; right->motion()->y = 4.0f;
    std::vector<eve::weapon::WeaponEntity*> weapons;
    for (Building* defense : {first, second}) {
        auto* weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id = "network-sam";
        definition.damage = 10.0f;
        definition.range = 20.0f;
        definition.targetsGround = false;
        definition.targetsAir = true;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def = weapon->definition()->owned.get();
        weapon->state()->stages = &weapon->definition()->def->stages;
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
        REQUIRE(link.ok());
        defense->weapon()->link = std::move(link).takeValue();
        defense->combat()->acquisitionRange = 20.0f;
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
    first->release(); second->release(); left->release(); right->release(); blue->release(); red->release();
}

TEST_CASE("rts.convoyFlanksAtomicallyHandoffCrossingThreats") {
    ecs::Table world;
    ecs::ScopedTable guard(world);
    Faction* blue = Faction::createFaction();
    Faction* red = Faction::createFaction();
    Unit* leader = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f1"));
    Unit* tail = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f2"));
    Unit* left = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f3"));
    Unit* right = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f4"));
    Unit* upper = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f5"));
    Unit* lower = Unit::createUnit(subject("00000000-0000-7000-8000-0000000003f6"));
    auto bind = [&](Unit& unit, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        unit.faction()->link = std::move(link).takeValue();
    };
    for (Unit* unit : {leader, tail, left, right}) bind(*unit, *blue);
    bind(*upper, *red); bind(*lower, *red);
    leader->motion()->x = 4.0f;
    tail->motion()->x = 0.0f;
    leader->supply()->convoyLeader = leader->identity()->self;
    tail->supply()->convoyLeader = leader->identity()->self;
    CommandSpec mission;
    mission.kind = OrderKind::SupplyRelay;
    mission.targetEntity = tail->identity()->self;
    mission.target = {20.0f, 0.0f};
    REQUIRE(leader->orders()->values.replace(mission).ok());
    left->tactics()->escortOffsetY = 3.0f;
    right->tactics()->escortOffsetY = -3.0f;
    for (Unit* escort : {left, right}) {
        escort->tactics()->protectionRange = 20.0f;
        CommandSpec order;
        order.kind = OrderKind::Escort;
        order.targetEntity = leader->identity()->self;
        REQUIRE(escort->orders()->values.replace(order).ok());
    }
    upper->motion()->x = lower->motion()->x = 2.0f;
    upper->motion()->y = 7.0f;
    lower->motion()->y = -7.0f;
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

    lower->release(); upper->release(); right->release(); left->release();
    tail->release(); leader->release(); red->release(); blue->release();
}

TEST_CASE("rts.coordinatedVolleyHoldsAndReleasesGroupWhileExplicitAttackBypassesSchedule") {
    ecs::Table world;
    ecs::ScopedTable guard(world);
    Faction*         blue   = Faction::createFaction(subject("00000000-0000-7000-8000-000000000321"));
    Faction*         red    = Faction::createFaction(subject("00000000-0000-7000-8000-000000000322"));
    Unit*            first  = Unit::createUnit(subject("00000000-0000-7000-8000-000000000323"));
    Unit*            second = Unit::createUnit(subject("00000000-0000-7000-8000-000000000324"));
    Unit*            target = Unit::createUnit(subject("00000000-0000-7000-8000-000000000325"));
    auto bind = [&](Unit& unit, Faction& faction) {
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
        auto* weapon = eve::weapon::WeaponEntity::createWeapon();
        eve::weapon::WeaponDefinition definition;
        definition.id               = "volley-shell";
        definition.damage = 10.0f;
        definition.range            = 12.0f;
        weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
        weapon->definition()->def = weapon->definition()->owned.get();
        weapon->state()->stages     = &weapon->definition()->def->stages;
        auto link = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
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
    ecs::Table world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS module;
    auto faction = module.newFaction(subject("00000000-0000-7000-8000-00000000d111"));
    REQUIRE(faction.ok());
    auto building = module.newFactionBuilding(*faction.value(),
        subject("00000000-0000-7000-8000-00000000d112"));
    auto occupant = module.newFactionUnit(*faction.value(),
        subject("00000000-0000-7000-8000-00000000d113"));
    REQUIRE(building.ok()); REQUIRE(occupant.ok());
    auto container = eve::rts::ContainerLink::bind(ecs::handle_of(building.value()));
    REQUIRE(container.ok());
    occupant.value()->containment()->container = std::move(container).takeValue();
    building.value()->garrison()->occupants.push_back(ecs::handle_of(occupant.value()));
    building.value()->capture()->blockedByGarrison = true;
    building.value()->integrity()->alive = false;
    building.value()->integrity()->state.health = 0.0;

    eve::action::ActionRuntime runtime;
    eve::rts::ActionAdapter adapter(runtime);
    const eve::SimulationStep simulationStep{
        eve::SimulationTick{1}, eve::Duration::fromSeconds(0.1).expect("cleanup dt")};
    auto stepped = module.step(simulationStep, adapter);
    REQUIRE(stepped.ok());
    CHECK(module.findBuilding(subject("00000000-0000-7000-8000-00000000d112")) == nullptr);
    CHECK(module.findUnit(subject("00000000-0000-7000-8000-00000000d113")) == nullptr);
    CHECK_EQ(module.buildingCount(), 0u);
    CHECK_EQ(module.unitCount(), 0u);
}

TEST_CASE("rts.mobileSupplierTracksTheRecipientsLivePosition") {
    ecs::Table world;
    ecs::ScopedTable guard(world);
    Faction* faction = Faction::createFaction(subject("00000000-0000-7000-8000-000000000391"));
    Unit* supplier = Unit::createUnit(subject("00000000-0000-7000-8000-000000000392"));
    Unit* recipient = Unit::createUnit(subject("00000000-0000-7000-8000-000000000393"));
    for (Unit* unit : {supplier, recipient}) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(faction));
        REQUIRE(link.ok());
        unit->faction()->link = std::move(link).takeValue();
    }
    supplier->motion()->speed = 5.0f;
    supplier->supply()->stock = supplier->supply()->capacity = 4.0f;
    supplier->supply()->range = 1.0f;
    supplier->supply()->transferRate = 2.0f;
    supplier->supply()->autoDispatch = true;
    recipient->motion()->x = 10.0f;

    auto* weapon = eve::weapon::WeaponEntity::createWeapon();
    eve::weapon::WeaponDefinition definition;
    definition.id = "moving-recipient-rifle";
    definition.magSize = 4;
    weapon->definition()->owned = std::make_shared<const eve::weapon::WeaponDefinition>(definition);
    weapon->definition()->def = weapon->definition()->owned.get();
    weapon->state()->resource.kind = eve::weapon::ResourceKind::Ammo;
    weapon->state()->resource.value = 0.0f;
    weapon->state()->resource.max = 4.0f;
    weapon->state()->resource.reserve = -1;
    auto weaponLink = eve::rts::WeaponLink::bind(ecs::handle_of(weapon));
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

    weapon->release(); supplier->release(); recipient->release(); faction->release();
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
    move.kind = OrderKind::Move;
    move.target = {4.0f, 7.0f};
    const std::array selected{stable};
    auto commanded = module.commandUnits(selected, move, {eve::rts::FormationKind::Grid, 1.0f, 0});
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
    auto rejected = module.commandUnits(missing, move);
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
        eve::Result<eve::rts::ActionExecutionResult> execute(
            Unit&, const eve::rts::OrderRecord&, const eve::SimulationStep&) override {
            return eve::Result<eve::rts::ActionExecutionResult>::success(
                {eve::rts::ActionDisposition::Pending});
        }
    } executor;

    eve::rts::RTS module;
    auto created = module.newUnit(subject("00000000-0000-7000-8000-00000000ec01"));
    REQUIRE(created.ok());
    Unit* unit = std::move(created).takeValue();
    unit->shield()->capacity = 10.0f;
    unit->shield()->value = 9.0f;
    unit->shield()->regenRate = 2.0f;
    REQUIRE(module.step({eve::SimulationTick{7},
        eve::Duration::fromSeconds(1.0).expect("lifecycle projection dt")}, executor).ok());
    const auto frameEvents = module.inspectFrameEvents();
    const auto* events = frameEvents.getIf<eve::Value::Array>();
    REQUIRE(events != nullptr);
    REQUIRE_EQ(events->size(), 1u);
    const auto* projected = events->front().getIf<eve::Value::Object>();
    REQUIRE(projected != nullptr);
    CHECK_EQ(*projected->at("type").getIf<std::string>(), "shield_recharged");
    CHECK_EQ(*projected->at("source").getIf<std::string>(), unit->identity()->subject.format());
    CHECK_EQ(*projected->at("tick").getIf<std::int64_t>(), std::int64_t{7});

    REQUIRE(module.step({eve::SimulationTick{8},
        eve::Duration::fromSeconds(0.1).expect("lifecycle clear dt")}, executor).ok());
    const auto clearedEvents = module.inspectFrameEvents();
    const auto* cleared = clearedEvents.getIf<eve::Value::Array>();
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
