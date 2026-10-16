#include "zeroerr/assert.h"
#include "RtsCompositionFixtures.h"
#include "rts/RTSCrowdFlowNavigationInternal.h"
#include "rts/RTSSystemMovementInternal.h"

namespace {
void groupSurface(eve::crowd::Crowd& crowd, bool disconnected = false) {
    eve::crowd::NavigationSurfaceSnapshot input;
    input.stamp    = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, 0.25f};
    input.width    = 7;
    input.height   = 5;
    input.cellSize = 1.f;
    input.terrainCosts.assign(35, 1.f);
    input.navigationCosts.assign(35, 1.f);
    for (int y = 0; y < (disconnected ? 5 : 4); ++y)
        input.terrainCosts[y * 7 + 3] = input.navigationCosts[y * 7 + 3] = 0.f;
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
}
}  // namespace

TEST_CASE("rts.crowd.groupWrongSlotStartsBoundedTerminalRouteAndKeepsItAcrossRefresh") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    auto*             unit = Unit::createUnit();
    eve::crowd::Crowd crowd;
    groupSurface(crowd);
    eve::rts::CrowdFlowNavigation             navigation({1000, 1});
    eve::rts::CrowdFlowNavigation::GroupInput group;
    group.members = {{ecs::handle_of(unit), "slot-order"}};
    group.targets = {{0.5f, 1.5f}, {6.5f, 1.5f}};
    REQUIRE(navigation.replaceGroups(crowd, {group}).ok());
    auto step = [](std::uint64_t tick) {
        return eve::SimulationStep{eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)};
    };
    REQUIRE(navigation.begin(crowd, step(1)).ok());
    auto shared =
        navigation.requestRoute(crowd, ecs::handle_of(unit), "slot-order", {1.5f, 1.5f}, {6.5f, 1.5f}, step(1).tick);
    REQUIRE(shared.ok());
    REQUIRE(navigation.pump(crowd, step(1)).ok());
    eve::crowd::AgentUpdate update;
    REQUIRE(navigation.steerRoute(crowd, {1.5f, 1.5f}, shared.value(), 0.1f, {}, update).ok());
    REQUIRE(update.flow.has_value());
    const auto sharedGeneration = update.flow->generation();
    CHECK(crowd.sampleFlowField(*update.flow, 1.5f, 1.5f).value().direction.x < 0.f);
    REQUIRE(navigation.begin(crowd, step(2)).ok());
    auto terminal =
        navigation.requestRoute(crowd, ecs::handle_of(unit), "slot-order", {0.5f, 1.5f}, {6.5f, 1.5f}, step(2).tick);
    REQUIRE(terminal.ok());
    REQUIRE(navigation.pump(crowd, step(2)).ok());
    REQUIRE(navigation.steerRoute(crowd, {0.5f, 1.5f}, terminal.value(), 0.1f, {}, update).ok());
    CHECK(update.flow->generation() != sharedGeneration);
    CHECK(update.intent->action == eve::crowd::AgentAction::Flow);
    const auto terminalGeneration = update.flow->generation();
    CHECK_EQ(navigation.observation().lastStarts, 1);
    CHECK(navigation.observation().lastWork <= 1000);
    REQUIRE(navigation.replaceGroups(crowd, {group}).ok());
    REQUIRE(navigation.begin(crowd, step(3)).ok());
    auto continued =
        navigation.requestRoute(crowd, ecs::handle_of(unit), "slot-order", {1.5f, 2.5f}, {6.5f, 1.5f}, step(3).tick);
    REQUIRE(continued.ok());
    REQUIRE(navigation.pump(crowd, step(3)).ok());
    REQUIRE(navigation.steerRoute(crowd, {1.5f, 2.5f}, continued.value(), 0.1f, {}, update).ok());
    CHECK_EQ(update.flow->generation(), terminalGeneration);
    CHECK_EQ(navigation.observation().prepared, 2);
    CHECK_EQ(navigation.observation().lastStarts, 0);
    unit->release();
}

TEST_CASE("rts.crowd.groupProjectionRejectsDuplicatesAtomicallyAndOldRouteEpoch") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    auto*             unit = Unit::createUnit();
    eve::crowd::Crowd crowd;
    groupSurface(crowd);
    eve::rts::CrowdFlowNavigation             navigation({1000, 1});
    eve::rts::CrowdFlowNavigation::GroupInput group;
    group.members = {{ecs::handle_of(unit), "slot-order"}};
    group.targets = {{0.5f, 1.5f}, {6.5f, 1.5f}};
    REQUIRE(navigation.replaceGroups(crowd, {group}).ok());
    REQUIRE(!navigation.replaceGroups(crowd, {group, group}).ok());
    const eve::SimulationStep step{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(navigation.begin(crowd, step).ok());
    auto route =
        navigation.requestRoute(crowd, ecs::handle_of(unit), "slot-order", {1.5f, 1.5f}, {6.5f, 1.5f}, step.tick);
    REQUIRE(route.ok());
    REQUIRE(navigation.pump(crowd, step).ok());
    eve::crowd::AgentUpdate update;
    REQUIRE(navigation.steerRoute(crowd, {1.5f, 1.5f}, route.value(), 0.1f, {}, update).ok());
    CHECK(crowd.sampleFlowField(*update.flow, 1.5f, 1.5f).value().direction.x < 0.f);
    const auto generation = update.flow->generation();
    auto       checkpoint = crowd.captureSnapshot();
    REQUIRE(checkpoint.ok());
    auto restored = crowd.prepareRestore(checkpoint.value());
    REQUIRE(restored.ok());
    REQUIRE(crowd.commitRestore(std::move(restored).takeValue()).ok());
    REQUIRE(!navigation.steerRoute(crowd, {1.5f, 1.5f}, route.value(), 0.1f, {}, update).ok());
    CHECK_EQ(update.flow->generation(), generation);
    unit->release();
}

TEST_CASE("rts.crowd.movementGroupSharesBarrierRouteAndArrivesAtDistinctSlots") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(10, 7);
    for (int y = 0; y < 5; ++y) map.setBlocked(4, y, true);
    map.setBlocked(4, 6, true);  // One traversable cell at (4, 5), rather than a two-cell opening.
    eve::crowd::Crowd crowd;
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(0.3f);
    eve::rts::RTS rts;
    rts.setNavigationProvider(&map, {1.f, 0.f, 0.f});
    rts.setCrowdProvider(&crowd);
    eve::rts::CrowdSurfaceTracking tracking;
    tracking.surface          = eve::crowd::NavigationSurfaceId(1);
    tracking.movement         = eve::crowd::MovementClassId(1);
    tracking.movementRevision = tracking.costModelRevision = 1;
    tracking.clearanceRadius                               = 0.25f;
    REQUIRE(rts.configureCrowdSurfaceTracking(tracking).ok());
    REQUIRE(rts.configureCrowdFlowNavigation({1000, 1}).ok());
    auto  a      = subject("00000000-0000-7000-8000-000000009001");
    auto  b      = subject("00000000-0000-7000-8000-000000009002");
    auto* first  = rts.newUnit(a).takeValue();
    auto* second = rts.newUnit(b).takeValue();
    for (auto* unit : {first, second}) {
        unit->crowd()->link =
            eve::rts::CrowdLink::bind(unit == first ? "group-flow/first" : "group-flow/second").takeValue();
        unit->crowd()->radius         = 0.1f;
        unit->motion()->x             = 1.f;
        unit->motion()->y             = unit == first ? 1.f : 3.f;
        unit->motion()->speed         = 2.f;
        unit->motion()->arrivalRadius = 0.12f;
    }
    eve::rts::MovementGroupBatch group;
    group.units        = {a, b};
    group.target       = {7.f, 2.f};
    group.formation    = {FormationKind::Column, 2.f, 0};
    group.leadDistance = 1.f;
    REQUIRE(rts.submitMovementGroup(group).ok());
    const auto goalA = first->orders()->values.current().value().target;
    const auto goalB = second->orders()->values.current().value().target;
    auto       step  = [&](std::uint64_t tick) {
        return rts.step({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(50000000)}, executor);
    };
    REQUIRE(step(1).ok());
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    const auto stateA = crowd.readAgent({first->crowd()->runtimeAgent, first->crowd()->runtimeOwner}).value();
    const auto stateB = crowd.readAgent({second->crowd()->runtimeAgent, second->crowd()->runtimeOwner}).value();
    CHECK(stateA.flowGeneration != 0);
    CHECK_EQ(stateA.flowGeneration, stateB.flowGeneration);
    CHECK_EQ(first->navigation()->formationSpeedFactor, 1.f);
    CHECK(first->navigation()->waypoints.empty());
    for (std::uint64_t tick = 2; tick <= 600 && (!first->orders()->values.empty() || !second->orders()->values.empty());
         ++tick)
        REQUIRE(step(tick).ok());
    CHECK(first->orders()->values.empty());
    CHECK(second->orders()->values.empty());
    CHECK(std::hypot(first->motion()->x - goalA.x, first->motion()->y - goalA.y) <= 0.13f);
    CHECK(std::hypot(second->motion()->x - goalB.x, second->motion()->y - goalB.y) <= 0.13f);
}

TEST_CASE("rts.crowd.groupDisconnectedAssignedSlotStopsAfterNearestSlotInsteadOfCompleting") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    auto*             unit = Unit::createUnit();
    eve::crowd::Crowd crowd;
    groupSurface(crowd, true);
    eve::rts::CrowdFlowNavigation             navigation({1000, 1});
    eve::rts::CrowdFlowNavigation::GroupInput group;
    group.members = {{ecs::handle_of(unit), "slot-order"}};
    group.targets = {{0.5f, 1.5f}, {6.5f, 1.5f}};
    REQUIRE(navigation.replaceGroups(crowd, {group}).ok());
    const eve::SimulationStep step{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(navigation.begin(crowd, step).ok());
    auto route =
        navigation.requestRoute(crowd, ecs::handle_of(unit), "slot-order", {0.5f, 1.5f}, {6.5f, 1.5f}, step.tick);
    REQUIRE(route.ok());
    REQUIRE(navigation.pump(crowd, step).ok());
    eve::crowd::AgentUpdate update;
    update.maxSpeed = 2.f;
    auto state      = navigation.steerRoute(crowd, {0.5f, 1.5f}, route.value(), 0.1f, {}, update);
    REQUIRE(state.ok());
    CHECK(state.value() == eve::rts::CrowdFlowNavigation::DriveState::Unreachable);
    CHECK_EQ(*update.maxSpeed, 0.f);
    CHECK(update.intent->action == eve::crowd::AgentAction::Idle);
    unit->release();
}

TEST_CASE("rts.crowd.groupOldOrderAndReusedEntitySlotCannotReuseMemberProjection") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    auto*             unit = Unit::createUnit();
    eve::crowd::Crowd crowd;
    groupSurface(crowd);
    eve::rts::CrowdFlowNavigation             navigation({1000, 2});
    const auto                                oldHandle = ecs::handle_of(unit);
    eve::rts::CrowdFlowNavigation::GroupInput group;
    group.members = {{oldHandle, "slot-order"}};
    group.targets = {{0.5f, 1.5f}, {6.5f, 1.5f}};
    REQUIRE(navigation.replaceGroups(crowd, {group}).ok());
    const eve::SimulationStep first{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(navigation.begin(crowd, first).ok());
    auto old = navigation.requestRoute(crowd, oldHandle, "slot-order", {1.5f, 1.5f}, {6.5f, 1.5f}, first.tick);
    REQUIRE(old.ok());
    auto replacedOrder = navigation.requestRoute(crowd, oldHandle, "new-order", {1.5f, 1.5f}, {6.5f, 1.5f}, first.tick);
    REQUIRE(replacedOrder.ok());
    REQUIRE(navigation.pump(crowd, first).ok());
    eve::crowd::AgentUpdate oldUpdate, replacedUpdate;
    REQUIRE(navigation.steerRoute(crowd, {1.5f, 1.5f}, old.value(), 0.1f, {}, oldUpdate).ok());
    REQUIRE(navigation.steerRoute(crowd, {1.5f, 1.5f}, replacedOrder.value(), 0.1f, {}, replacedUpdate).ok());
    CHECK(oldUpdate.flow->generation() != replacedUpdate.flow->generation());
    const auto exactGeneration = replacedUpdate.flow->generation();
    unit->release();
    unit                 = Unit::createUnit();
    const auto newHandle = ecs::handle_of(unit);
    CHECK_EQ(oldHandle.id, newHandle.id);
    CHECK(oldHandle.generation != newHandle.generation);
    CHECK(!eve::rts::systems_internal::isSameHandle(oldHandle, newHandle));
    const eve::SimulationStep second{eve::SimulationTick(2), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(navigation.begin(crowd, second).ok());
    auto newMember = navigation.requestRoute(crowd, newHandle, "slot-order", {1.5f, 1.5f}, {6.5f, 1.5f}, second.tick);
    REQUIRE(newMember.ok());
    REQUIRE(navigation.pump(crowd, second).ok());
    REQUIRE(navigation.steerRoute(crowd, {1.5f, 1.5f}, newMember.value(), 0.1f, {}, replacedUpdate).ok());
    CHECK_EQ(replacedUpdate.flow->generation(), exactGeneration);
    CHECK_EQ(navigation.observation().prepared, 2);
    unit->release();
}
