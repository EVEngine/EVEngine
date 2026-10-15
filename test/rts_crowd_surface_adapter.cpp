#include "RtsCompositionFixtures.h"
#include "rts/RTSCrowdSurfaceInternal.h"
TEST_CASE("rts.crowd.mapAdapterPreservesAdjacencyAndDestinationEntryCosts") {
    eve::map::Pathfinder map(2, 2);
    eve::crowd::Crowd    crowd;
    auto                 publish = [&] {
        auto                               captured = map.captureNavigationGrid().takeValue();
        eve::crowd::NavigationSurfaceStamp stamp{
            eve::crowd::NavigationSurfaceId(12), eve::crowd::MovementClassId(1), captured.revision(), 1, 1, 0.f};
        auto input = eve::rts::projectCrowdSurface(captured, stamp, {}, 0).takeValue();
        auto plan  = crowd.prepareNavigationSurface(input).takeValue();
        REQUIRE(crowd.commitNavigationSurface(std::move(plan)).ok());
        crowd.buildFlowField(1, 1);
    };
    publish();
    CHECK_EQ(crowd.costAtWorld(0.f, 0.f), 2.f);
    map.setTopology("ortho8");
    publish();
    CHECK(std::abs(crowd.costAtWorld(0.f, 0.f) - std::sqrt(2.f)) < 1e-5f);
    map.setTopology("ortho4");
    map.setCellCost(1, 1, 9.f);
    publish();
    CHECK_EQ(crowd.costAtWorld(0.f, 0.f), 10.f);
}
TEST_CASE("rts.crowd.mapAdapterInflatesNavigationAndPublishesRawPhysicalGeometry") {
    eve::map::Pathfinder map(5, 5);
    map.setBlocked(2, 2, true);
    auto                               captured = map.captureNavigationGrid().takeValue();
    eve::crowd::NavigationSurfaceStamp stamp{
        eve::crowd::NavigationSurfaceId(11), eve::crowd::MovementClassId(1), captured.revision(), 1, 1, 0.75f};
    auto projected = eve::rts::projectCrowdSurface(captured, stamp, {}, 10000);
    REQUIRE(projected.ok());
    auto input = std::move(projected).takeValue();
    CHECK_EQ(input.originX, -0.5f);
    CHECK_EQ(input.originY, -0.5f);
    CHECK(!input.diagonal);
    CHECK(input.destinationEntryCost);
    CHECK_EQ(input.terrainCosts[11], 1.f);
    CHECK_EQ(input.navigationCosts[11], 0.f);
    eve::crowd::Crowd crowd;
    auto              plan = crowd.prepareNavigationSurface(input).takeValue();
    REQUIRE(crowd.commitNavigationSurface(std::move(plan)).ok());
    crowd.setDefaultSpeed(0.f);
    crowd.setSeparationWeight(0.f);
    const int agent = crowd.addAgent(1.f, 2.f, 0.f, 0.1f);
    REQUIRE(crowd.advance(0.01f).ok());
    CHECK_EQ(crowd.getAgentState(agent).x, 1.f);
    crowd.buildFlowField(4, 2);
    CHECK_EQ(crowd.flowAtCell(1, 2).x, 0.f);
    REQUIRE(!eve::rts::projectCrowdSurface(captured, stamp, {}, 0).ok());
    map.setTopology("hex");
    REQUIRE(!eve::rts::projectCrowdSurface(map.captureNavigationGrid().takeValue(), stamp, {}, 10000).ok());
}

#include "RtsCompositionFixtures.h"

TEST_CASE("rts.crowd.surfaceTrackingPublishesOnlyOnChangedInputsAndRejectsBeforeSimulation") {
    ecs::Table           world;
    ecs::ScopedTable     guard(world);
    PendingRTSExecutor   executor;
    eve::map::Pathfinder map(5, 5);
    eve::crowd::Crowd    crowd;
    eve::rts::RTS        rts;
    rts.setNavigationProvider(&map, {1.f, 0.f, 0.f});
    rts.setCrowdProvider(&crowd);
    eve::rts::CrowdSurfaceTracking settings;
    settings.surface          = eve::crowd::NavigationSurfaceId(9);
    settings.movement         = eve::crowd::MovementClassId(2);
    settings.movementRevision = settings.costModelRevision = 1;
    REQUIRE(rts.configureCrowdSurfaceTracking(settings).ok());
    const auto step = [&](std::uint64_t tick) {
        return rts.step({eve::SimulationTick(tick), eve::Duration::fromNanoseconds(10000000)}, executor);
    };
    REQUIRE(step(1).ok());
    const auto initial = crowd.getFieldStatus();
    CHECK_EQ(initial.surface.surface.value(), 9);
    REQUIRE(step(2).ok());
    CHECK_EQ(crowd.getFieldStatus().revision, initial.revision);
    map.setCellCost(0, 0, 1.f);
    REQUIRE(step(3).ok());
    CHECK_EQ(crowd.getFieldStatus().revision, initial.revision);
    map.setBlocked(2, 2, true);
    REQUIRE(step(4).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.surfaceRevision, 2);
    CHECK_EQ(crowd.getCellCost(2, 2), 0.f);
    const auto current = crowd.getFieldStatus();
    map.setTopology("hex");
    REQUIRE(!step(5).ok());
    CHECK_EQ(crowd.getFieldStatus().revision, current.revision);
    map.setTopology("ortho4");
    REQUIRE(step(5).ok());
    rts.setNavigationProvider(&map, {2.f, 0.f, 0.f});
    REQUIRE(step(6).ok());
    CHECK(crowd.getFieldStatus().surface.surfaceRevision > current.surface.surfaceRevision);
    settings.clearanceRadius    = 0.25f;
    settings.movementRevision   = 2;
    settings.maxInflationChecks = 0;
    REQUIRE(rts.configureCrowdSurfaceTracking(settings).ok());
    const auto before = crowd.getFieldStatus();
    REQUIRE(!step(7).ok());
    CHECK_EQ(crowd.getFieldStatus().revision, before.revision);
    settings.maxInflationChecks = 100000;
    REQUIRE(rts.configureCrowdSurfaceTracking(settings).ok());
    REQUIRE(step(7).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.movementRevision, 2);
    rts.clearCrowdSurfaceTracking();
    map.setTopology("hex");
    REQUIRE(step(8).ok());
}
TEST_CASE("rts.crowd.surfaceTrackingRequiresProvidersAndRetainsSettingsAfterRejectedConfiguration") {
    ecs::Table                     world;
    ecs::ScopedTable               guard(world);
    PendingRTSExecutor             executor;
    eve::rts::RTS                  rts;
    eve::rts::CrowdSurfaceTracking settings;
    settings.surface          = eve::crowd::NavigationSurfaceId(1);
    settings.movement         = eve::crowd::MovementClassId(1);
    settings.movementRevision = settings.costModelRevision = 1;
    REQUIRE(rts.configureCrowdSurfaceTracking(settings).ok());
    REQUIRE(!rts.step({eve::SimulationTick(1), eve::Duration::fromNanoseconds(0)}, executor).ok());
    auto bad    = settings;
    bad.surface = {};
    REQUIRE(!rts.configureCrowdSurfaceTracking(bad).ok());
    eve::map::Pathfinder map(2, 2);
    eve::crowd::Crowd    crowd;
    rts.setNavigationProvider(&map);
    rts.setCrowdProvider(&crowd);
    REQUIRE(rts.step({eve::SimulationTick(0), eve::Duration::fromNanoseconds(0)}, executor).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.surface.value(), 1);
    const auto revision = crowd.getFieldStatus().surface.surfaceRevision;
    rts.setNavigationProvider(&map);
    REQUIRE(rts.step({eve::SimulationTick(0), eve::Duration::fromNanoseconds(0)}, executor).ok());
    CHECK_EQ(crowd.getFieldStatus().surface.surfaceRevision, revision + 1);
    rts.setCrowdProvider(nullptr);
}

TEST_CASE("rts.crowd.scriptTrackingSupportsWorldSetupAndCheckedConfiguration") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local sim=eve.RTS();
        assert(sim.configureScriptWorld(5,5,1.0,0.0,0.0).ok);
        local settings={surfaceId=3,movementId=2,movementRevision=1,costModelRevision=1,
                        clearanceRadius=0.25,maxInflationChecks=100000};
        assert(sim.configureCrowdSurfaceTracking(settings).ok);
        assert(sim.stepScript(0.01).ok);
        local bad=clone settings; bad.surfaceId=3.0;
        assert(!sim.configureCrowdSurfaceTracking(bad).ok);
        bad=clone settings; delete bad.movementRevision;
        assert(!sim.configureCrowdSurfaceTracking(bad).ok);
        bad=clone settings; bad.maxInflationChecks=-1;
        assert(!sim.configureCrowdSurfaceTracking(bad).ok);
        assert(sim.stepScript(0.01).ok);
        sim.clearCrowdSurfaceTracking();
        assert(sim.stepScript(0.01).ok);
    )"));
}
