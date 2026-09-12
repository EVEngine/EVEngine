#include "RtsCompositionFixtures.h"

TEST_CASE("rts.facadeCommandsContainmentAndCloakUseOwnedStableRoots") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    Faction*         faction   = rts.newFaction(subject("00000000-0000-7000-8000-00000000ee01")).value();
    Unit*            transport = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ee02")).value();
    Unit*            passenger = rts.newFactionUnit(*faction, subject("00000000-0000-7000-8000-00000000ee03")).value();
    Building*        bunker = rts.newFactionBuilding(*faction, subject("00000000-0000-7000-8000-00000000ee04")).value();
    transport->containment()->capacity = 2;
    transport->motion()->x             = 3.0f;
    transport->motion()->y             = 4.0f;
    passenger->motion()->x             = 3.0f;
    passenger->motion()->y             = 4.0f;
    passenger->motion()->arrived       = true;

    CommandSpec board;
    board.kind         = OrderKind::BoardTransport;
    board.target       = {3.0f, 4.0f};
    board.targetEntity = ecs::handle_of(transport);
    const std::array passengerSelection{passenger->identity()->subject};
    REQUIRE(rts.commandUnits(passengerSelection, board).ok());
    REQUIRE(eve::rts::ContainmentSystem::step().ok());
    CHECK(passenger->containment()->container.resolve() == transport);
    auto unloaded = rts.unloadTransport(*transport, {8.0f, 5.0f});
    REQUIRE(unloaded.ok());
    CHECK_EQ(unloaded.value(), std::size_t{1});
    CHECK(!passenger->containment()->container.isBound());

    auto bunkerLink = eve::rts::ContainerLink::bind(ecs::handle_of(bunker));
    REQUIRE(bunkerLink.ok());
    passenger->containment()->container = std::move(bunkerLink).takeValue();
    bunker->garrison()->occupants.push_back(ecs::handle_of(passenger));
    bunker->capture()->blockedByGarrison = true;
    auto evacuated                       = rts.evacuateBuilding(*bunker, {10.0f, 6.0f});
    REQUIRE(evacuated.ok());
    CHECK_EQ(evacuated.value(), std::size_t{1});
    CHECK(!bunker->capture()->blockedByGarrison);

    REQUIRE(rts.setUnitCloaked(*passenger, true).ok());
    CHECK(passenger->vision()->cloaked);
    Unit* foreign = Unit::createUnit(subject("00000000-0000-7000-8000-00000000ee05"));
    CHECK(!rts.setUnitCloaked(*foreign, true).ok());
    CHECK(!rts.unloadTransport(*foreign, {}).ok());
    foreign->release();
}

TEST_CASE("rts.formationPlannerIsDeterministic") {
    const FormationSpec spec{FormationKind::Grid, 10.0f, 2};
    auto                planned = eve::rts::FormationPlanner::plan(3, {100.0f, 200.0f}, spec);
    REQUIRE(planned.ok());
    const auto positions = std::move(planned).takeValue();
    REQUIRE_EQ(positions.size(), 3u);
    CHECK(std::abs(positions[0].x - 95.0f) < 1e-5f);
    CHECK(std::abs(positions[0].y - 195.0f) < 1e-5f);
    CHECK(std::abs(positions[1].x - 105.0f) < 1e-5f);
    CHECK(std::abs(positions[1].y - 195.0f) < 1e-5f);
    CHECK(std::abs(positions[2].x - 95.0f) < 1e-5f);
    CHECK(std::abs(positions[2].y - 205.0f) < 1e-5f);
}

TEST_CASE("rts.commandFanOutUsesGenericOrdersAndFormation") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    std::vector<Unit*>             units;
    std::vector<ecs::EntityHandle> handles;
    for (int index = 0; index < 3; ++index) {
        Unit* unit            = Unit::createUnit();
        unit->motion()->speed = 100.0f;
        units.push_back(unit);
        handles.push_back(ecs::handle_of(unit));
    }

    const CommandSpec   command{OrderKind::Move, {100.0f, 200.0f}, {}, 0, 0.0};
    const FormationSpec formation{FormationKind::Grid, 10.0f, 2};
    auto                receiptResult = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
    REQUIRE(receiptResult.ok());
    const auto receipt = std::move(receiptResult).takeValue();
    CHECK_EQ(receipt.requested, 3u);
    CHECK_EQ(receipt.accepted, 3u);
    CHECK_EQ(receipt.orderIds.size(), 3u);

    for (std::size_t index = 0; index < units.size(); ++index) {
        auto current = units[index]->orders()->values.current();
        REQUIRE(current.ok());
        const auto order = std::move(current).takeValue();
        CHECK_EQ(static_cast<int>(order.kind), static_cast<int>(OrderKind::Move));
        CHECK_EQ(order.formationSlot, static_cast<int>(index));
        CHECK_EQ(order.id, receipt.orderIds[index]);
    }

    const std::size_t beforeRejectedFanOut = units[0]->orders()->values.orderCount();
    units[2]->release();
    auto staleResult = eve::rts::CommandFanOutSystem::fanOut(handles, command, formation);
    CHECK(!staleResult.ok());
    staleResult.ignore("expected stale selection in fan-out test");
    CHECK_EQ(units[0]->orders()->values.orderCount(), beforeRejectedFanOut);

    units[0]->release();
    units[1]->release();
}

TEST_CASE("rts.commandFanOutReplacesDirectCommandsAndAppendsQueuedWaypoints") {
    ecs::Table                       world;
    ecs::ScopedTable                 guard(world);
    Unit*                            unit = Unit::createUnit();
    std::array<ecs::EntityHandle, 1> selection{ecs::handle_of(unit)};
    FormationSpec                    formation{FormationKind::Line, 1.0f, 0};

    CommandSpec oldMove;
    oldMove.kind   = OrderKind::Move;
    oldMove.target = {1.0f, 0.0f};
    REQUIRE(unit->orders()->values.enqueue(oldMove).ok());
    CommandSpec direct = oldMove;
    direct.target      = {5.0f, 0.0f};
    auto replaced      = eve::rts::CommandFanOutSystem::fanOut(selection, direct, formation);
    REQUIRE(replaced.ok());
    CHECK_EQ(unit->orders()->values.orderCount(), 1u);
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK(std::abs(current.value().target.x - 5.0f) < 1e-5f);

    CommandSpec queued = direct;
    queued.target      = {9.0f, 0.0f};
    queued.append      = true;
    auto appended      = eve::rts::CommandFanOutSystem::fanOut(selection, queued, formation);
    REQUIRE(appended.ok());
    CHECK_EQ(unit->orders()->values.orderCount(), 2u);
    current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK(std::abs(current.value().target.x - 5.0f) < 1e-5f);
    unit->release();
}

TEST_CASE("rts.extendedOrdersPreserveEntityAndAreaPayload") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Unit*       unit   = Unit::createUnit();
    Unit*       target = Unit::createUnit();
    CommandSpec suppress;
    suppress.kind            = OrderKind::SuppressArea;
    suppress.target          = {12.0f, 8.0f};
    suppress.secondaryTarget = {20.0f, 14.0f};
    suppress.targetEntity    = ecs::handle_of(target);
    suppress.radius          = 6.0f;
    suppress.append          = true;

    auto queued = unit->orders()->values.enqueue(suppress);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    const auto order = std::move(current).takeValue();
    CHECK_EQ(static_cast<int>(order.kind), static_cast<int>(OrderKind::SuppressArea));
    CHECK(std::abs(order.secondaryTarget.x - 20.0f) < 1e-5f);
    CHECK(std::abs(order.radius - 6.0f) < 1e-5f);
    CHECK(order.append);
    CHECK(order.targetEntity.id == ecs::handle_of(target).id);
    CHECK(std::abs(unit->worker()->cargo) < 1e-5f);
    CHECK(!unit->combat()->holdPosition);

    unit->release();
    target->release();
}

TEST_CASE("rts.crowdProviderOwnsLinkedMovementAndOverlapResolution") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setResolveOverlaps(true);
    crowd.setSeparationRadius(3.0f);
    crowd.setSeparationWeight(2.0f);

    Unit* first      = Unit::createUnit();
    Unit* second     = Unit::createUnit();
    auto  firstLink  = eve::rts::CrowdLink::bind("rts/unit/first");
    auto  secondLink = eve::rts::CrowdLink::bind("rts/unit/second");
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());
    first->crowd()->link    = std::move(firstLink).takeValue();
    second->crowd()->link   = std::move(secondLink).takeValue();
    first->crowd()->radius  = 0.75f;
    second->crowd()->radius = 0.75f;
    first->motion()->speed  = 4.0f;
    second->motion()->speed = 4.0f;
    CommandSpec move;
    move.kind        = OrderKind::Move;
    move.target      = {10.0f, 0.0f};
    auto firstOrder  = first->orders()->values.enqueue(move);
    auto secondOrder = second->orders()->values.enqueue(move);
    REQUIRE(firstOrder.ok());
    REQUIRE(secondOrder.ok());
    std::move(firstOrder).takeValue();
    std::move(secondOrder).takeValue();

    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.25).expect("crowd dt")};
    auto                      moved = eve::rts::CrowdMotionSystem::step(step, crowd);
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value(), 2u);
    CHECK(crowd.hasNamedAgent("rts/unit/first"));
    CHECK(crowd.hasNamedAgent("rts/unit/second"));
    const float dx = first->motion()->x - second->motion()->x;
    const float dy = first->motion()->y - second->motion()->y;
    CHECK(std::hypot(dx, dy) > 0.0f);

    first->release();
    second->release();
}

TEST_CASE("rts.crowdProjectsMovementPriorityAndUnlocksExactOverlap") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    crowd.setClampToField(false);
    crowd.setResolveOverlaps(true);
    crowd.setSeparationRadius(3.0f);
    crowd.setSeparationWeight(0.0f);
    Unit* high     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000401"));
    Unit* low      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000402"));
    auto  highLink = eve::rts::CrowdLink::bind("rts/priority/high");
    auto  lowLink  = eve::rts::CrowdLink::bind("rts/priority/low");
    REQUIRE(highLink.ok());
    REQUIRE(lowLink.ok());
    high->crowd()->link   = std::move(highLink).takeValue();
    low->crowd()->link    = std::move(lowLink).takeValue();
    high->crowd()->radius = low->crowd()->radius = 1.0f;
    high->navigation()->movementPriority         = 10;
    low->navigation()->movementPriority          = -10;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {10.0f, 0.0f};
    REQUIRE(high->orders()->values.enqueue(move).ok());
    REQUIRE(low->orders()->values.enqueue(move).ok());
    const eve::SimulationStep step{eve::SimulationTick{1},
                                   eve::Duration::fromSeconds(0.1).expect("priority avoidance dt")};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const int highAgent = crowd.getNamedAgentIndex("rts/priority/high");
    const int lowAgent  = crowd.getNamedAgentIndex("rts/priority/low");
    CHECK_EQ(crowd.getAgentAvoidancePriority(highAgent), 10);
    CHECK_EQ(crowd.getAgentAvoidancePriority(lowAgent), -10);
    CHECK(std::hypot(low->motion()->x, low->motion()->y) > std::hypot(high->motion()->x, high->motion()->y));
    high->release();
    low->release();
}

TEST_CASE("rts.navigationUsesCanonicalMapPathAndReportsUnreachableOnce") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::map::Pathfinder pathfinder(5, 3);
    pathfinder.setDiagonal(false);
    pathfinder.setBlocked(1, 1, true);

    Unit* unit            = Unit::createUnit();
    unit->motion()->x     = 0.0f;
    unit->motion()->y     = 1.0f;
    unit->motion()->speed = 1.0f;
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {4.0f, 1.0f};
    auto queued = unit->orders()->values.enqueue(move);
    REQUIRE(queued.ok());
    std::move(queued).takeValue();

    int  unreachableCount = 0;
    auto planned = eve::rts::NavigationSystem::step(pathfinder, {}, [&](Unit& reported, const eve::rts::OrderRecord&) {
        CHECK(&reported == unit);
        ++unreachableCount;
    });
    REQUIRE(planned.ok());
    CHECK(!unit->navigation()->unreachable);
    REQUIRE(!unit->navigation()->waypoints.empty());
    CHECK(std::abs(unit->navigation()->waypoints.front().y - 1.0f) > 0.5f);

    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("navigation dt")};
    auto                      moved = eve::rts::MotionSystem::step(tick);
    REQUIRE(moved.ok());
    CHECK(std::abs(unit->motion()->y - 1.0f) > 0.5f);

    pathfinder.setBlocked(3, 1, true);
    pathfinder.setBlocked(4, 0, true);
    pathfinder.setBlocked(4, 2, true);
    auto blockedQueue = unit->orders()->values.replace(move);
    REQUIRE(blockedQueue.ok());
    std::move(blockedQueue).takeValue();
    auto blocked = eve::rts::NavigationSystem::step(pathfinder, {},
                                                    [&](Unit&, const eve::rts::OrderRecord&) { ++unreachableCount; });
    REQUIRE(blocked.ok());
    CHECK(unit->navigation()->unreachable);
    CHECK_EQ(unreachableCount, 1);
    auto repeated = eve::rts::NavigationSystem::step(pathfinder, {},
                                                     [&](Unit&, const eve::rts::OrderRecord&) { ++unreachableCount; });
    REQUIRE(repeated.ok());
    CHECK_EQ(unreachableCount, 1);

    unit->release();
}

TEST_CASE("rts.trafficReservationsHonorPriorityAndRecoverAtNarrowCells") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    eve::map::Pathfinder pathfinder(3, 1);
    pathfinder.setDiagonal(false);

    Unit* low                            = Unit::createUnit(subject("00000000-0000-7000-8000-000000000202"));
    Unit* high                           = Unit::createUnit(subject("00000000-0000-7000-8000-000000000201"));
    low->motion()->x                     = 0.0f;
    high->motion()->x                    = 2.0f;
    low->navigation()->movementPriority  = 1;
    high->navigation()->movementPriority = 9;
    CommandSpec lowMove;
    lowMove.kind         = OrderKind::Move;
    lowMove.target       = {2.0f, 0.0f};
    CommandSpec highMove = lowMove;
    highMove.target      = {0.0f, 0.0f};
    REQUIRE(low->orders()->values.enqueue(lowMove).ok());
    REQUIRE(high->orders()->values.enqueue(highMove).ok());
    REQUIRE(eve::rts::NavigationSystem::step(pathfinder, {}).ok());

    auto reserved = eve::rts::TrafficReservationSystem::step(pathfinder, {});
    REQUIRE(reserved.ok());
    CHECK(low->navigation()->trafficWaiting);
    CHECK(!high->navigation()->trafficWaiting);

    high->motion()->x = 1.0f;
    REQUIRE(eve::rts::NavigationSystem::step(pathfinder, {}).ok());
    REQUIRE(eve::rts::TrafficReservationSystem::step(pathfinder, {}).ok());
    CHECK(!low->navigation()->trafficWaiting);

    low->release();
    high->release();
}

TEST_CASE("rts.arrivedMovementOrdersAdvanceTheCanonicalMixedQueue") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            unit = Unit::createUnit();
    unit->motion()->speed = 2.0f;

    CommandSpec first;
    first.kind   = OrderKind::Move;
    first.target = {1.0f, 0.0f};
    CommandSpec second;
    second.kind   = OrderKind::AttackMove;
    second.target = {3.0f, 0.0f};
    CommandSpec third;
    third.kind   = OrderKind::Move;
    third.target = {4.0f, 0.0f};
    REQUIRE(unit->orders()->values.enqueue(first).ok());
    REQUIRE(unit->orders()->values.enqueue(second).ok());
    REQUIRE(unit->orders()->values.enqueue(third).ok());

    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("movement queue dt")};
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(unit->motion()->arrived);
    auto settled = eve::rts::MovementOrderSystem::step();
    REQUIRE(settled.ok());
    CHECK_EQ(settled.value(), 1u);
    auto current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::AttackMove));

    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(!unit->motion()->arrived);
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(unit->motion()->arrived);
    REQUIRE(eve::rts::MovementOrderSystem::step().ok());
    current = unit->orders()->values.current();
    REQUIRE(current.ok());
    CHECK_EQ(static_cast<int>(current.value().kind), static_cast<int>(OrderKind::Move));
    unit->release();
}

TEST_CASE("rts.patrolPersistsAndAlternatesBetweenCommandOriginAndTarget") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Unit*            unit = Unit::createUnit();
    unit->motion()->x     = 1.0f;
    unit->motion()->speed = 2.0f;
    CommandSpec patrol;
    patrol.kind   = OrderKind::Patrol;
    patrol.target = {3.0f, 0.0f};
    REQUIRE(unit->orders()->values.enqueue(patrol).ok());
    REQUIRE(eve::rts::PatrolSystem::step().ok());
    CHECK(unit->navigation()->patrolInitialized);
    CHECK(unit->navigation()->patrolTowardTarget);
    CHECK(std::abs(unit->navigation()->patrolOrigin.x - 1.0f) < 1e-5f);

    const eve::SimulationStep tick{eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("patrol dt")};
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(std::abs(unit->motion()->x - 3.0f) < 1e-5f);
    REQUIRE(eve::rts::PatrolSystem::step().ok());
    CHECK(!unit->navigation()->patrolTowardTarget);
    REQUIRE(eve::rts::MotionSystem::step(tick).ok());
    CHECK(std::abs(unit->motion()->x - 1.0f) < 1e-5f);
    CHECK(!unit->orders()->values.empty());
    unit->release();
}

TEST_CASE("rts.radarCreatesQuantizedUntargetableContactsAndJammingStopsRefresh") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue       = Faction::createFaction(subject("00000000-0000-7000-8000-000000000106"));
    Faction*         red        = Faction::createFaction(subject("00000000-0000-7000-8000-000000000107"));
    Unit*            radar      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000108"));
    Unit*            target     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000109"));
    Unit*            jammer     = Unit::createUnit(subject("00000000-0000-7000-8000-000000000110"));
    auto             blueLink   = eve::rts::FactionLink::bind(ecs::handle_of(blue));
    auto             targetLink = eve::rts::FactionLink::bind(ecs::handle_of(red));
    auto             jammerLink = eve::rts::FactionLink::bind(ecs::handle_of(red));
    REQUIRE(blueLink.ok());
    REQUIRE(targetLink.ok());
    REQUIRE(jammerLink.ok());
    radar->faction()->link           = std::move(blueLink).takeValue();
    target->faction()->link          = std::move(targetLink).takeValue();
    jammer->faction()->link          = std::move(jammerLink).takeValue();
    radar->vision()->radarRange      = 20.0f;
    radar->vision()->radarResolution = 2.0f;
    target->motion()->x              = 5.2f;
    target->motion()->y              = 3.1f;
    jammer->motion()->x              = 30.0f;

    eve::map::Fov                   blueFov(40, 40), redFov(40, 40);
    eve::rts::FogOfWarSystem::State fog;
    const auto                      provider = [&](Faction& faction) { return &faction == blue ? &blueFov : &redFov; };
    const eve::SimulationStep       first{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.5).expect("radar dt")};
    REQUIRE(eve::rts::FogOfWarSystem::step(first, {}, fog, provider).ok());
    const auto* contact = eve::rts::FogOfWarSystem::contact(*blue, target->identity()->subject);
    REQUIRE(contact != nullptr);
    CHECK_EQ(contact->kind, "radar_unit");
    CHECK(!contact->visible);
    CHECK(!contact->detected);
    CHECK(std::abs(contact->position.x - 5.0f) < 1e-5f);
    CHECK(std::abs(contact->position.y - 3.0f) < 1e-5f);

    jammer->motion()->x            = 5.2f;
    jammer->motion()->y            = 3.1f;
    jammer->vision()->jammingRange = 3.0f;
    target->motion()->x            = 9.0f;
    const eve::SimulationStep second{eve::SimulationTick{2}, eve::Duration::fromSeconds(1.0).expect("jam dt")};
    REQUIRE(eve::rts::FogOfWarSystem::step(second, {}, fog, provider).ok());
    contact = eve::rts::FogOfWarSystem::contact(*blue, target->identity()->subject);
    REQUIRE(contact != nullptr);
    CHECK(std::abs(contact->position.x - 5.0f) < 1e-5f);
    CHECK(std::abs(contact->ageSeconds - 1.0) < 1e-6);

    eve::rts::FogOfWarSystem::clear(fog);
    jammer->release();
    target->release();
    radar->release();
    red->release();
    blue->release();
}

TEST_CASE("rts.commandNetworkUsesRelaysCapacityPriorityAndHostileJamming") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    Faction*         blue        = Faction::createFaction(subject("00000000-0000-7000-8000-000000000221"));
    Faction*         red         = Faction::createFaction(subject("00000000-0000-7000-8000-000000000222"));
    Building*        hq          = Building::createBuilding(subject("00000000-0000-7000-8000-000000000223"));
    Unit*            relay       = Unit::createUnit(subject("00000000-0000-7000-8000-000000000224"));
    Unit*            high        = Unit::createUnit(subject("00000000-0000-7000-8000-000000000225"));
    Unit*            low         = Unit::createUnit(subject("00000000-0000-7000-8000-000000000226"));
    Unit*            jammer      = Unit::createUnit(subject("00000000-0000-7000-8000-000000000227"));
    auto             bindFaction = [&](auto& entity, Faction& faction) {
        auto link = eve::rts::FactionLink::bind(ecs::handle_of(&faction));
        REQUIRE(link.ok());
        entity.faction()->link = std::move(link).takeValue();
    };
    bindFaction(*hq, *blue);
    bindFaction(*relay, *blue);
    bindFaction(*high, *blue);
    bindFaction(*low, *blue);
    bindFaction(*jammer, *red);
    hq->integrity()->state.health = hq->integrity()->state.maxHealth = 100.0;
    hq->command()->range                                             = 10.0f;
    hq->command()->capacity                                          = 1;
    relay->motion()->x                                               = 8.0f;
    relay->command()->range                                          = 12.0f;
    relay->command()->capacity                                       = 1;
    relay->command()->cost                                           = 1;
    relay->command()->priority                                       = 100;
    relay->command()->relayRequiresUplink                            = true;
    relay->command()->requiresCommand                                = true;
    high->motion()->x                                                = 16.0f;
    high->command()->requiresCommand                                 = true;
    high->command()->priority                                        = 10;
    low->motion()->x                                                 = 17.0f;
    low->motion()->speed                                             = 2.0f;
    low->command()->requiresCommand                                  = true;
    low->command()->priority                                         = 1;
    low->command()->outOfCommandSpeedFactor                          = 0.5f;
    jammer->motion()->x                                              = 40.0f;
    jammer->command()->jammingRange                                  = 5.0f;

    auto connected = eve::rts::CommandNetworkSystem::step();
    REQUIRE(connected.ok());
    CHECK(relay->command()->relayActive);
    CHECK(high->command()->inCommand);
    CHECK(!low->command()->inCommand);
    CHECK_EQ(relay->command()->load, 1);

    jammer->motion()->x = 8.0f;
    auto jammed         = eve::rts::CommandNetworkSystem::step();
    REQUIRE(jammed.ok());
    CHECK(relay->command()->jammed);
    CHECK(!relay->command()->relayActive);
    CHECK(!high->command()->inCommand);
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {27.0f, 0.0f};
    REQUIRE(low->orders()->values.replace(move).ok());
    auto moved = eve::rts::MotionSystem::step(
        {eve::SimulationTick{1}, eve::Duration::fromSeconds(1.0).expect("command motion dt")});
    REQUIRE(moved.ok());
    CHECK(std::abs(low->motion()->x - 18.0f) < 1e-5f);

    jammer->release();
    low->release();
    high->release();
    relay->release();
    hq->release();
    red->release();
    blue->release();
}
