#include "zeroerr/assert.h"
#include "RtsCompositionFixtures.h"
#include "rts/RTSCrowdFlowNavigationInternal.h"
#include "rts/RTSSystemMovementInternal.h"

namespace test_rts_crowd_flow_navigation_composition {
TEST_CASE("rts.crowd.sharedFlowStartsOldestRequestBeforeNewLowerKey") {
    eve::crowd::Crowd                     crowd;
    eve::crowd::NavigationSurfaceSnapshot input;
    input.stamp    = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, 0.25f};
    input.width    = 9;
    input.height   = 3;
    input.cellSize = 1.f;
    input.terrainCosts.assign(27, 1.f);
    input.navigationCosts.assign(27, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    eve::rts::CrowdFlowNavigation navigation({1000, 1});
    const auto                    step = [](std::uint64_t tick) {
        return eve::SimulationStep{eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)};
    };
    REQUIRE(navigation.begin(crowd, step(1)).ok());
    REQUIRE(navigation.request(crowd, {0.5f, 1.5f}, step(1).tick).ok());
    REQUIRE(navigation.request(crowd, {8.5f, 1.5f}, step(1).tick).ok());
    REQUIRE(navigation.pump(crowd, step(1)).ok());
    REQUIRE(navigation.begin(crowd, step(2)).ok());
    REQUIRE(navigation.request(crowd, {1.5f, 1.5f}, step(2).tick).ok());
    REQUIRE(navigation.request(crowd, {8.5f, 1.5f}, step(2).tick).ok());
    REQUIRE(navigation.pump(crowd, step(2)).ok());
    eve::crowd::AgentUpdate update;
    auto                    older = navigation.steer(crowd, {4.5f, 1.5f}, {8.5f, 1.5f}, 0.1f, {}, update);
    REQUIRE(older.ok());
    CHECK(older.value() == eve::rts::CrowdFlowNavigation::DriveState::Ready);
    auto newer = navigation.steer(crowd, {4.5f, 1.5f}, {1.5f, 1.5f}, 0.1f, {}, update);
    REQUIRE(newer.ok());
    CHECK(newer.value() == eve::rts::CrowdFlowNavigation::DriveState::Pending);
    CHECK_EQ(navigation.observation().lastStarts, 1);
    CHECK(navigation.observation().lastWork <= 1000);
    REQUIRE(navigation.pump(crowd, {step(2).tick, eve::Duration{}}).ok());
    CHECK_EQ(navigation.observation().prepared, 2);
}
namespace {
TEST_CASE("rts.crowd.failedPumpAndSameOwnerInputRepairDoNotRefundTickQuota") {
    eve::crowd::Crowd                     crowd;
    eve::crowd::NavigationSurfaceSnapshot input;
    input.stamp    = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, 0.25f};
    input.width    = 5;
    input.height   = 3;
    input.cellSize = 1.f;
    input.terrainCosts.assign(15, 1.f);
    input.navigationCosts.assign(15, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    eve::rts::CrowdFlowNavigation navigation({1000, 2});
    const eve::SimulationStep     first{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(navigation.begin(crowd, first).ok());
    REQUIRE(navigation.request(crowd, {1.5f, 1.5f}, first.tick).ok());
    REQUIRE(navigation.request(crowd, {4.5f, 1.5f}, first.tick).ok());
    // Inject a native preparation-stage failure after one earlier request has prepared.
    input.width                 = 3;
    input.stamp.surfaceRevision = 2;
    input.terrainCosts.assign(9, 1.f);
    input.navigationCosts.assign(9, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    auto failedPump = navigation.pump(crowd, first);
    REQUIRE(!failedPump.ok());
    CHECK(failedPump.code() == eve::StatusCode::Rejected);
    CHECK_EQ(navigation.observation().lastStarts, 1);
    CHECK_EQ(navigation.observation().prepared, 1);
    REQUIRE(navigation.pump(crowd, first).ok());
    CHECK_EQ(navigation.observation().prepared, 1);
    input.width                 = 5;
    input.stamp.surfaceRevision = 3;
    input.terrainCosts.assign(15, 1.f);
    input.navigationCosts.assign(15, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    REQUIRE(navigation.begin(crowd, first).ok());
    REQUIRE(navigation.request(crowd, {1.5f, 1.5f}, first.tick).ok());
    REQUIRE(navigation.pump(crowd, first).ok());
    CHECK_EQ(navigation.observation().prepared, 1);
    CHECK_EQ(navigation.observation().lastStarts, 1);
    eve::crowd::AgentUpdate update;
    auto                    waiting = navigation.steer(crowd, {0.5f, 1.5f}, {1.5f, 1.5f}, 0.1f, {}, update);
    REQUIRE(waiting.ok());
    CHECK(waiting.value() == eve::rts::CrowdFlowNavigation::DriveState::Pending);
    const eve::SimulationStep second{eve::SimulationTick(2), first.delta};
    REQUIRE(navigation.begin(crowd, second).ok());
    REQUIRE(navigation.request(crowd, {1.5f, 1.5f}, second.tick).ok());
    REQUIRE(navigation.pump(crowd, second).ok());
    CHECK_EQ(navigation.observation().prepared, 2);
    auto ready = navigation.steer(crowd, {0.5f, 1.5f}, {1.5f, 1.5f}, 0.1f, {}, update);
    REQUIRE(ready.ok());
    CHECK(ready.value() == eve::rts::CrowdFlowNavigation::DriveState::Ready);
    const auto oldOwner      = crowd.getFieldStatus().owner;
    const auto beforeRestore = navigation.observation();
    auto       saved         = crowd.captureSnapshot();
    REQUIRE(saved.ok());
    auto restored = crowd.prepareRestore(saved.value());
    REQUIRE(restored.ok());
    REQUIRE(crowd.commitRestore(std::move(restored).takeValue()).ok());
    CHECK(crowd.getFieldStatus().owner != oldOwner);
    REQUIRE(navigation.begin(crowd, second).ok());
    REQUIRE(navigation.request(crowd, {1.5f, 1.5f}, second.tick).ok());
    REQUIRE(navigation.pump(crowd, second).ok());
    const auto afterRestore = navigation.observation();
    CHECK(afterRestore.cacheHits + afterRestore.prepared > beforeRestore.cacheHits + beforeRestore.prepared);
}
TEST_CASE("rts.crowd.sharedFlowWorkServesPersistentGoalsWhileLowerKeysChurn") {
    eve::crowd::Crowd                     crowd;
    eve::crowd::NavigationSurfaceSnapshot input;
    input.stamp    = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, 0.25f};
    input.width    = 5;
    input.height   = 3;
    input.cellSize = 1.f;
    input.terrainCosts.assign(15, 1.f);
    input.navigationCosts.assign(15, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    eve::rts::CrowdFlowNavigation navigation({1, 3});
    bool                          firstReady = false, secondReady = false;
    for (std::uint64_t tick = 1; tick <= 160 && (!firstReady || !secondReady); ++tick) {
        const eve::SimulationStep step{eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)};
        REQUIRE(navigation.begin(crowd, step).ok());
        REQUIRE(navigation.request(crowd, {4.5f, 1.5f}, step.tick).ok());
        REQUIRE(navigation.request(crowd, {3.5f, 1.5f}, step.tick).ok());
        if (tick > 1) REQUIRE(navigation.request(crowd, {float(tick % 3) + 0.5f, 1.5f}, step.tick).ok());
        REQUIRE(navigation.pump(crowd, step).ok());
        CHECK(navigation.observation().lastWork <= 1);
        CHECK(navigation.observation().lastStarts <= 3);
        eve::crowd::AgentUpdate update;
        auto                    first = navigation.steer(crowd, {2.5f, 1.5f}, {4.5f, 1.5f}, 0.1f, {}, update);
        REQUIRE(first.ok());
        firstReady  = first.value() == eve::rts::CrowdFlowNavigation::DriveState::Ready;
        auto second = navigation.steer(crowd, {2.5f, 1.5f}, {3.5f, 1.5f}, 0.1f, {}, update);
        REQUIRE(second.ok());
        secondReady = second.value() == eve::rts::CrowdFlowNavigation::DriveState::Ready;
    }
    CHECK(firstReady);
    CHECK(secondReady);
}
void surface(eve::rts::RTS& rts, eve::map::Pathfinder& map, eve::crowd::Crowd& crowd) {
    rts.setNavigationProvider(&map, {1.f, 0.f, 0.f});
    rts.setCrowdProvider(&crowd);
    eve::rts::CrowdSurfaceTracking config;
    config.surface          = eve::crowd::NavigationSurfaceId(1);
    config.movement         = eve::crowd::MovementClassId(1);
    config.movementRevision = config.costModelRevision = 1;
    config.clearanceRadius                             = 0.25f;
    REQUIRE(rts.configureCrowdSurfaceTracking(config).ok());
}
}  // namespace
TEST_CASE("rts.crowd.reconfigureAndClearReenableCannotRefundFailedTickWork") {
    for (const bool clearFirst : {false, true}) {
        ecs::Table           world;
        ecs::ScopedTable     guard(world);
        PendingRTSExecutor   executor;
        eve::map::Pathfinder map(8, 3);
        eve::crowd::Crowd    crowd;
        eve::rts::RTS        rts;
        surface(rts, map, crowd);
        REQUIRE(rts.configureCrowdFlowNavigation({1, 1}).ok());
        auto* unit            = rts.newUnit(subject("00000000-0000-7000-8000-000000007001")).takeValue();
        unit->crowd()->link   = eve::rts::CrowdLink::bind("config-budget/unit").takeValue();
        unit->crowd()->radius = 0.1f;
        unit->motion()->x = unit->motion()->y = 1.f;
        unit->motion()->speed                 = 2.f;
        CommandSpec order;
        order.kind   = OrderKind::Move;
        order.target = {6.f, 1.f};
        REQUIRE(unit->orders()->values.enqueue(order).ok());
        const auto step = [&](std::uint64_t tick) {
            return rts.step({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)}, executor);
        };
        REQUIRE(step(1).ok());
        unit->crowd()->radius = -0.1f;
        // Graph work executes before the atomic agent update rejects this radius.
        REQUIRE(!step(2).ok());
        const auto spent = rts.getCrowdFlowNavigationStatus();
        CHECK_EQ(spent.lastWork, 1);
        CHECK_EQ(spent.prepared, 1);
        if (clearFirst) {
            rts.clearCrowdFlowNavigation();
            CHECK_EQ(rts.getCrowdFlowNavigationStatus().pendingGoals, 0u);
        }
        REQUIRE(rts.configureCrowdFlowNavigation({1000, 2}).ok());
        auto unchanged = rts.configureCrowdFlowNavigation({1000, 2});
        REQUIRE(unchanged.ok());
        CHECK(unchanged.code() == eve::StatusCode::NoOp);
        unit->crowd()->radius = 0.1f;
        REQUIRE(step(2).ok());
        const auto retried = rts.getCrowdFlowNavigationStatus();
        CHECK_EQ(retried.lastWork, spent.lastWork);
        CHECK_EQ(retried.lastStarts, spent.lastStarts);
        CHECK_EQ(retried.prepared, spent.prepared);
        CHECK_EQ(retried.readyGoals, 0u);
        REQUIRE(step(3).ok());
        CHECK_EQ(rts.getCrowdFlowNavigationStatus().readyGoals, 1u);
        CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, clearFirst ? 2 : 1);
    }
}

TEST_CASE("rts.crowd.disabledRetainedServiceDoesNotRequireCrowdProvider") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(4, 3);
    eve::rts::RTS        rts;
    rts.setNavigationProvider(&map, {1.f, 0.f, 0.f});
    REQUIRE(rts.configureCrowdFlowNavigation({1, 1}).ok());
    rts.clearCrowdFlowNavigation();
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 0);
}
TEST_CASE("rts.crowd.sharedStaticGoalsSpendOneQuotaAndReuseOneField") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(8, 5);
    eve::crowd::Crowd    crowd;
    eve::rts::RTS        rts;
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(1.f);
    surface(rts, map, crowd);
    REQUIRE(rts.configureCrowdFlowNavigation({1, 1}).ok());
    std::array<Unit*, 2> units{};
    for (size_t i = 0; i < units.size(); ++i) {
        units[i] = rts.newUnit(subject(i == 0 ? "00000000-0000-7000-8000-000000001111"
                                              : "00000000-0000-7000-8000-000000001112"))
                       .takeValue();
        auto* unit                    = units[i];
        unit->crowd()->link           = eve::rts::CrowdLink::bind("shared/" + std::to_string(i)).takeValue();
        unit->crowd()->radius         = 0.1f;
        unit->motion()->x             = 1.5f;
        unit->motion()->y             = 1.5f + float(i);
        unit->motion()->speed         = 2.f;
        unit->motion()->arrivalRadius = 0.1f;
        CommandSpec order;
        order.kind   = OrderKind::Move;
        order.target = {6.5f, 2.5f};
        REQUIRE(unit->orders()->values.enqueue(order).ok());
    }
    auto step = [&](std::uint64_t tick) {
        return rts.step({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)}, executor);
    };
    REQUIRE(step(1).ok());
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().lastWork, 1);
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    CHECK_EQ(units[0]->motion()->x, 1.5f);
    CHECK(!units[0]->motion()->arrived);
    CHECK(units[0]->navigation()->waypoints.empty());
    std::uint64_t tick = 2;
    while (rts.getCrowdFlowNavigationStatus().readyGoals == 0) {
        REQUIRE(tick < 300);
        REQUIRE(step(tick++).ok());
    }
    const auto a = crowd.readAgent({units[0]->crowd()->runtimeAgent, units[0]->crowd()->runtimeOwner}).takeValue();
    const auto b = crowd.readAgent({units[1]->crowd()->runtimeAgent, units[1]->crowd()->runtimeOwner}).takeValue();
    CHECK(a.flowGeneration != 0);
    CHECK_EQ(a.flowGeneration, b.flowGeneration);
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    CHECK(units[0]->motion()->x > 1.5f);
    rts.clearCrowdFlowNavigation();
    REQUIRE(step(tick).ok());
    CHECK_EQ(crowd.readAgent({units[0]->crowd()->runtimeAgent, units[0]->crowd()->runtimeOwner}).value().flowGeneration,
             0);
}
TEST_CASE("rts.crowd.sharedFlowUnreachableRetainsOrderAndNeverArrives") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(5, 3);
    map.setBlocked(4, 1, true);
    eve::crowd::Crowd crowd;
    eve::rts::RTS     rts;
    surface(rts, map, crowd);
    REQUIRE(rts.configureCrowdFlowNavigation({1000, 1}).ok());
    int reported = 0;
    rts.setNavigationProvider(&map, {1.f, 0.f, 0.f}, [&](Unit& notified, const eve::rts::OrderRecord& order) {
        CHECK(!notified.motion()->arrived);
        CHECK(order.kind == OrderKind::Move);
        ++reported;
    });
    auto* unit            = rts.newUnit(subject("00000000-0000-7000-8000-000000001113")).takeValue();
    unit->crowd()->link   = eve::rts::CrowdLink::bind("blocked/shared").takeValue();
    unit->crowd()->radius = 0.1f;
    unit->motion()->x = unit->motion()->y = 1.f;
    CommandSpec order;
    order.kind   = OrderKind::Move;
    order.target = {4.f, 1.f};
    REQUIRE(unit->orders()->values.enqueue(order).ok());
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK(unit->navigation()->unreachable);
    CHECK(!unit->motion()->arrived);
    REQUIRE(unit->orders()->values.current().ok());
    CHECK_EQ(unit->motion()->x, 1.f);
    CHECK_EQ(reported, 1);
    REQUIRE(rts.step({eve::SimulationTick(2), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK_EQ(reported, 1);
    map.setBlocked(4, 1, false);
    REQUIRE(rts.step({eve::SimulationTick(3), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK(!unit->navigation()->unreachable);
    CHECK(unit->motion()->x > 1.f);
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 2);
}
TEST_CASE("rts.crowd.scriptSharedFlowConfigurationHasExactQuotas") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local sim=eve.RTS();assert(sim.configureScriptWorld(5,5,1.0,0.0,0.0).ok);
        assert(sim.configureCrowdSurfaceTracking({surfaceId=1,movementId=1,movementRevision=1,costModelRevision=1,
               clearanceRadius=0.25,maxInflationChecks=100000}).ok);
        assert(!sim.configureCrowdFlowNavigation({maxWorkPerTick=2.0,maxStartsPerTick=1}).ok);
        assert(sim.configureCrowdFlowNavigation({maxWorkPerTick=2,maxStartsPerTick=1}).ok);
        assert(sim.stepScript(0.01).ok);
        assert(sim.getCrowdFlowNavigationStatus().value.lastWork==0);
        sim.clearCrowdFlowNavigation();assert(sim.stepScript(0.01).ok);
    )"));
}

TEST_CASE("rts.crowd.sharedFlowMissingProjectionRejectsWithoutConsumingTick") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    PendingRTSExecutor executor;
    eve::crowd::Crowd  crowd;
    eve::rts::RTS      rts;
    rts.setCrowdProvider(&crowd);
    REQUIRE(rts.configureCrowdFlowNavigation({100, 1}).ok());
    const eve::SimulationStep first{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(!rts.step(first, executor).ok());
    eve::map::Pathfinder map(3, 3);
    surface(rts, map, crowd);
    REQUIRE(rts.step(first, executor).ok());
    rts.setNavigationProvider(nullptr);
}

TEST_CASE("rts.crowd.sharedFlowCheckpointRenewsOwnerAndRebindsCachedField") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rts::RTS    rts;
    REQUIRE(rts.configureScriptWorld(8, 5, 1.f, 0.f, 0.f).ok());
    eve::rts::CrowdSurfaceTracking config;
    config.surface          = eve::crowd::NavigationSurfaceId(1);
    config.movement         = eve::crowd::MovementClassId(1);
    config.movementRevision = config.costModelRevision = 1;
    config.clearanceRadius                             = 0.25f;
    REQUIRE(rts.configureCrowdSurfaceTracking(config).ok());
    REQUIRE(rts.configureCrowdFlowNavigation({1000, 1}).ok());
    auto* unit            = rts.newUnit(subject("00000000-0000-7000-8000-000000001121")).takeValue();
    unit->crowd()->link   = eve::rts::CrowdLink::bind("restore/shared").takeValue();
    unit->crowd()->radius = 0.1f;
    unit->motion()->x = unit->motion()->y = 1.5f;
    unit->motion()->speed                 = 2.f;
    CommandSpec order;
    order.kind   = OrderKind::Move;
    order.target = {6.5f, 1.5f};
    REQUIRE(unit->orders()->values.enqueue(order).ok());
    REQUIRE(rts.stepScript(0.01).ok());
    const auto  owner  = unit->crowd()->runtimeOwner;
    const float savedX = unit->motion()->x;
    REQUIRE(rts.captureScriptCheckpoint("shared").ok());
    REQUIRE(rts.stepScript(0.01).ok());
    CHECK(unit->motion()->x > savedX);
    REQUIRE(rts.restoreScriptCheckpoint("shared").ok());
    CHECK_EQ(unit->motion()->x, savedX);
    REQUIRE(rts.stepScript(0.01).ok());
    CHECK(unit->crowd()->runtimeOwner != owner);
    CHECK(unit->motion()->x > savedX);
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    CHECK(rts.getCrowdFlowNavigationStatus().cacheHits > 0);
}
TEST_CASE("rts.crowd.sharedFlowCallbacksCanDestroyRecipientsWithoutStaleAccess") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(5, 3);
    map.setBlocked(4, 1, true);
    eve::crowd::Crowd crowd;
    eve::rts::RTS     rts;
    surface(rts, map, crowd);
    REQUIRE(rts.configureCrowdFlowNavigation({1000, 1}).ok());
    const std::array ids{subject("00000000-0000-7000-8000-000000001122"),
                         subject("00000000-0000-7000-8000-000000001123")};
    for (size_t i = 0; i < ids.size(); ++i) {
        auto* unit            = rts.newUnit(ids[i]).takeValue();
        unit->crowd()->link   = eve::rts::CrowdLink::bind("notice/" + std::to_string(i)).takeValue();
        unit->crowd()->radius = 0.1f;
        unit->motion()->x     = 1.f;
        unit->motion()->y     = float(i);
        CommandSpec order;
        order.kind   = OrderKind::Move;
        order.target = {4.f, 1.f};
        REQUIRE(unit->orders()->values.enqueue(order).ok());
    }
    int notices = 0;
    rts.setNavigationProvider(&map, {1.f, 0.f, 0.f}, [&](Unit&, const eve::rts::OrderRecord&) {
        ++notices;
        REQUIRE(rts.remove(ids[0]).ok());
        REQUIRE(rts.remove(ids[1]).ok());
    });
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK_EQ(notices, 1);
    CHECK_EQ(rts.unitCount(), 0u);
    CHECK_EQ(crowd.getAgentCount(), 0);
}
TEST_CASE("rts.crowd.sharedFlowCachePressurePreservesPreparedWorkAndOrder") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(5, 3);
    eve::crowd::Crowd    crowd;
    eve::rts::RTS        rts;
    surface(rts, map, crowd);
    REQUIRE(crowd.configureFlowCache(0).ok());
    REQUIRE(rts.configureCrowdFlowNavigation({1000, 1}).ok());
    auto* unit            = rts.newUnit(subject("00000000-0000-7000-8000-000000001124")).takeValue();
    unit->crowd()->link   = eve::rts::CrowdLink::bind("pressure/shared").takeValue();
    unit->crowd()->radius = 0.1f;
    unit->motion()->x = unit->motion()->y = 1.f;
    unit->motion()->speed                 = 2.f;
    CommandSpec order;
    order.kind   = OrderKind::Move;
    order.target = {4.f, 1.f};
    REQUIRE(unit->orders()->values.enqueue(order).ok());
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK(rts.getCrowdFlowNavigationStatus().cachePressure);
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    CHECK(!unit->motion()->arrived);
    CHECK_EQ(unit->motion()->x, 1.f);
    REQUIRE(unit->orders()->values.current().ok());
    REQUIRE(crowd.configureFlowCache(1024 * 1024).ok());
    REQUIRE(rts.step({eve::SimulationTick(2), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    CHECK(!rts.getCrowdFlowNavigationStatus().cachePressure);
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().lastWork, 0);
    CHECK(unit->motion()->x > 1.f);
}

TEST_CASE("rts.crowd.sharedFlowProviderReconstructionUsesFreshOwnerAndCachedState") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(5, 3);
    auto                 provider = std::make_unique<eve::crowd::Crowd>();
    eve::rts::RTS        rts;
    surface(rts, map, *provider);
    REQUIRE(rts.configureCrowdFlowNavigation({1000, 1}).ok());
    auto* unit            = rts.newUnit(subject("00000000-0000-7000-8000-000000001125")).takeValue();
    unit->crowd()->link   = eve::rts::CrowdLink::bind("reload/shared").takeValue();
    unit->crowd()->radius = 0.1f;
    unit->motion()->x = unit->motion()->y = 1.f;
    unit->motion()->speed                 = 2.f;
    CommandSpec order;
    order.kind   = OrderKind::Move;
    order.target = {4.f, 1.f};
    REQUIRE(unit->orders()->values.enqueue(order).ok());
    REQUIRE(rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)}, executor).ok());
    const auto  oldOwner = unit->crowd()->runtimeOwner;
    const float before   = unit->motion()->x;
    auto        saved    = provider->captureSnapshot().takeValue();
    provider.reset();
    const eve::SimulationStep next{eve::SimulationTick(2), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(!rts.step(next, executor).ok());
    CHECK_EQ(unit->motion()->x, before);
    provider = std::make_unique<eve::crowd::Crowd>();
    REQUIRE(provider->commitRestore(provider->prepareRestore(saved).takeValue()).ok());
    rts.setCrowdProvider(provider.get());
    REQUIRE(rts.step(next, executor).ok());
    CHECK(unit->crowd()->runtimeOwner != oldOwner);
    CHECK(unit->motion()->x > before);
    CHECK(rts.getCrowdFlowNavigationStatus().cacheHits > 0);
}

TEST_CASE("rts.crowd.dynamicAttackGoalsReuseWithinCellAndRebuildAcrossCells") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(10, 5);
    eve::crowd::Crowd    crowd;
    eve::rts::RTS        rts;
    surface(rts, map, crowd);
    REQUIRE(rts.configureCrowdFlowNavigation({1000, 2}).ok());
    auto* target        = rts.newUnit(subject("00000000-0000-7000-8000-000000001131")).takeValue();
    target->motion()->x = 6.5f;
    target->motion()->y = 2.5f;
    std::array<Unit*, 2> attackers{};
    for (size_t i = 0; i < attackers.size(); ++i) {
        auto* unit = rts.newUnit(subject(i == 0 ? "00000000-0000-7000-8000-000000001132"
                                                : "00000000-0000-7000-8000-000000001133"))
                         .takeValue();
        attackers[i]          = unit;
        unit->crowd()->link   = eve::rts::CrowdLink::bind("attack/shared/" + std::to_string(i)).takeValue();
        unit->crowd()->radius = 0.1f;
        unit->motion()->x     = 1.5f;
        unit->motion()->y     = 1.5f + float(i);
        unit->motion()->speed = 2.f;
        CommandSpec order;
        order.kind         = OrderKind::Attack;
        order.targetEntity = target->identity()->self;
        REQUIRE(unit->orders()->values.enqueue(order).ok());
    }
    auto step = [&](std::uint64_t tick) {
        return rts.step({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)}, executor);
    };
    REQUIRE(step(1).ok());
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    const auto old =
        crowd.readAgent({attackers[0]->crowd()->runtimeAgent, attackers[0]->crowd()->runtimeOwner}).takeValue();
    CHECK(old.flowGeneration != 0);
    CHECK(attackers[0]->navigation()->waypoints.empty());
    target->motion()->x = 6.7f;
    REQUIRE(step(2).ok());
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 1);
    CHECK_EQ(crowd.readAgent({attackers[0]->crowd()->runtimeAgent, attackers[0]->crowd()->runtimeOwner})
                 .value()
                 .flowGeneration,
             old.flowGeneration);
    target->motion()->x = 7.5f;
    REQUIRE(step(3).ok());
    CHECK_EQ(rts.getCrowdFlowNavigationStatus().prepared, 2);
    const auto a =
        crowd.readAgent({attackers[0]->crowd()->runtimeAgent, attackers[0]->crowd()->runtimeOwner}).takeValue();
    const auto b =
        crowd.readAgent({attackers[1]->crowd()->runtimeAgent, attackers[1]->crowd()->runtimeOwner}).takeValue();
    CHECK(a.flowGeneration != old.flowGeneration);
    CHECK_EQ(a.flowGeneration, b.flowGeneration);
}

TEST_CASE("rts.crowd.sharedGoalResolverPreservesPatrolEscortAndThreatOverlayPolicy") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            unit = Unit::createUnit();
    unit->crowd()->link   = eve::rts::CrowdLink::bind("resolver").takeValue();
    CommandSpec command;
    command.kind   = OrderKind::Patrol;
    command.target = {4.5f, 2.5f};
    REQUIRE(unit->orders()->values.enqueue(command).ok());
    auto order                             = unit->orders()->values.current().takeValue();
    unit->navigation()->patrolInitialized  = true;
    unit->navigation()->patrolTowardTarget = false;
    unit->navigation()->patrolOrigin       = {1.5f, 1.5f};
    auto goal                              = eve::rts::systems_internal::movementGoal(*unit, order);
    CHECK_EQ(goal.x, 1.5f);
    CHECK(eve::rts::systems_internal::isSharedFlowOrder(*unit, order));
    order.kind                = OrderKind::Escort;
    unit->tactics()->guardSet = true;
    unit->tactics()->guardX   = 3.5f;
    unit->tactics()->guardY   = 1.5f;
    goal                      = eve::rts::systems_internal::movementGoal(*unit, order);
    CHECK_EQ(goal.x, 3.5f);
    CHECK(eve::rts::systems_internal::isSharedFlowOrder(*unit, order));
    order.kind                       = OrderKind::SupplyRelay;
    unit->supply()->rendezvousActive = true;
    unit->supply()->rendezvousPoint  = {2.5f, 3.5f};
    goal                             = eve::rts::systems_internal::movementGoal(*unit, order);
    CHECK_EQ(goal.x, 2.5f);
    CHECK(!eve::rts::systems_internal::isSharedFlowOrder(*unit, order));
    order.kind                                = OrderKind::Move;
    unit->navigation()->trafficRecoveryTarget = eve::rts::WorldPosition{1.5f, 2.5f};
    CHECK(!eve::rts::systems_internal::isSharedFlowOrder(*unit, order));
    unit->release();
}

TEST_CASE("rts.crowd.sharedSlotGoalSetsCanonicalizeAndKeepExactUnitDestinations") {
    eve::crowd::Crowd                     crowd;
    eve::crowd::NavigationSurfaceSnapshot input;
    input.stamp    = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, 0.25f};
    input.width    = 5;
    input.height   = 3;
    input.cellSize = 1.f;
    input.terrainCosts.assign(15, 1.f);
    input.navigationCosts.assign(15, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    eve::rts::CrowdFlowNavigation navigation({1000, 1});
    const eve::SimulationStep     step{eve::SimulationTick(1), eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(navigation.begin(crowd, step).ok());
    std::vector<eve::rts::WorldPosition> slots{{4.5f, 1.5f}, {0.5f, 1.5f}, {4.5f, 1.5f}};
    REQUIRE(navigation.request(crowd, slots, step.tick).ok());
    std::vector<eve::rts::WorldPosition> reverse{{0.5f, 1.5f}, {4.5f, 1.5f}};
    REQUIRE(navigation.request(crowd, reverse, step.tick).ok());
    REQUIRE(navigation.pump(crowd, step).ok());
    CHECK_EQ(navigation.observation().prepared, 1);
    CHECK_EQ(navigation.observation().readyGoals, 1u);
    eve::crowd::AgentUpdate left, right;
    REQUIRE(navigation.steer(crowd, {0.6f, 1.5f}, slots, {0.5f, 1.5f}, 0.1f, {}, left).ok());
    REQUIRE(navigation.steer(crowd, {4.4f, 1.5f}, reverse, {4.5f, 1.5f}, 0.1f, {}, right).ok());
    REQUIRE(left.flow.has_value());
    REQUIRE(right.flow.has_value());
    CHECK_EQ(left.flow->generation(), right.flow->generation());
    CHECK(left.intent->action == eve::crowd::AgentAction::Seek);
    CHECK_EQ(left.intent->targetX, 0.5f);
    CHECK(right.intent->action == eve::crowd::AgentAction::Seek);
    CHECK_EQ(right.intent->targetX, 4.5f);
    REQUIRE(!navigation.steer(crowd, {4.4f, 1.5f}, reverse, {2.5f, 1.5f}, 0.1f, {}, right).ok());
    CHECK_EQ(right.intent->targetX, 4.5f);
    CHECK_EQ(right.flow->generation(), left.flow->generation());
    std::vector<eve::rts::WorldPosition> bad{{2.5f, 1.5f}, {99.f, 1.5f}};
    REQUIRE(!navigation.request(crowd, bad, step.tick).ok());
    CHECK_EQ(navigation.observation().readyGoals, 1u);
}


TEST_CASE("rts.crowd.sharedFlowQueuesMoreThan64GoalsWithoutRejectingTick") {
    eve::crowd::Crowd                     crowd;
    eve::crowd::NavigationSurfaceSnapshot input;
    input.stamp = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, 0.25f};
    input.width = input.height = 9;
    input.cellSize             = 1.f;
    input.terrainCosts.assign(81, 1.f);
    input.navigationCosts.assign(81, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    eve::rts::CrowdFlowNavigation navigation({10000, 16});
    for (std::uint64_t tick = 1; tick <= 8; ++tick) {
        eve::SimulationStep step{eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)};
        REQUIRE(navigation.begin(crowd, step).ok());
        for (int goal = 0; goal < 70; ++goal)
            REQUIRE(navigation.request(crowd, {float(goal % 9) + 0.5f, float(goal / 9) + 0.5f}, step.tick).ok());
        REQUIRE(navigation.pump(crowd, step).ok());
        CHECK(navigation.observation().lastStarts <= 16);
        CHECK(navigation.observation().lastWork <= 10000);
    }
    CHECK_EQ(navigation.observation().readyGoals, 70u);
    CHECK_EQ(navigation.observation().pendingGoals, 0u);
    eve::crowd::AgentUpdate update;
    auto                    drive = navigation.steer(crowd, {0.5f, 0.5f}, {6.5f, 7.5f}, 0.1f, {}, update);
    REQUIRE(drive.ok());
    CHECK(drive.value() == eve::rts::CrowdFlowNavigation::DriveState::Ready);
}


TEST_CASE("rts.crowd.sharedFlowWaitingGoalsKeep64BuilderLimit") {
    eve::crowd::Crowd                     crowd;
    eve::crowd::NavigationSurfaceSnapshot input;
    input.stamp = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, 0.25f};
    input.width = input.height = 16;
    input.cellSize             = 1.f;
    input.terrainCosts.assign(256, 1.f);
    input.navigationCosts.assign(256, 1.f);
    REQUIRE(crowd.commitNavigationSurface(crowd.prepareNavigationSurface(input).takeValue()).ok());
    eve::rts::CrowdFlowNavigation navigation({1, 16});
    for (std::uint64_t tick = 1; tick <= 8; ++tick) {
        eve::SimulationStep step{eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)};
        REQUIRE(navigation.begin(crowd, step).ok());
        for (int goal = 0; goal < 140; ++goal)
            REQUIRE(navigation.request(crowd, {float(goal % 16) + 0.5f, float(goal / 16) + 0.5f}, step.tick).ok());
        REQUIRE(navigation.pump(crowd, step).ok());
        CHECK_EQ(navigation.observation().lastWork, 1);
    }
    CHECK_EQ(navigation.observation().prepared, 64u);
    CHECK_EQ(navigation.observation().pendingGoals, 140u);
    CHECK_EQ(navigation.observation().lastStarts, 0);
}
}  // namespace test_rts_crowd_flow_navigation_composition

namespace test_rts_crowd_flow_groups_composition {
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
}  // namespace test_rts_crowd_flow_groups_composition
