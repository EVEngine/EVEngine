// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "script-binding": [
      "rts.crowd.scriptRelocationUsesCheckedOwnerPlacement"
    ]
  },
  "module": "rts",
  "origins": [
    "rts_crowd_registry.cpp"
  ]
}
*/
// clang-format on

#include "RtsCompositionFixtures.h"
#include "rts/RTSCrowdSurfaceInternal.h"
#include "zeroerr/assert.h"

namespace test_rts_crowd_registry_composition {
TEST_CASE("rts.crowd.providerDestructionIsObservableAndSafeInBothOrders") {
    ecs::Table         world;
    ecs::ScopedTable   guard(world);
    PendingRTSExecutor executor;
    {
        eve::rts::RTS rts;
        auto          provider = std::make_unique<eve::crowd::Crowd>();
        rts.setCrowdProvider(provider.get());
        auto* unit          = rts.newUnit(subject("00000000-0000-7000-8000-000000000972")).takeValue();
        unit->crowd()->link = eve::rts::CrowdLink::bind("lifetime").takeValue();
        REQUIRE(rts.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}, executor).ok());
        CHECK(unit->crowd()->runtimeOwner != 0);
        provider.reset();
        REQUIRE(!rts.step({eve::SimulationTick{2}, eve::Duration::fromNanoseconds(10000000)}, executor).ok());
        CHECK_EQ(unit->crowd()->runtimeOwner, 0u);
        REQUIRE(!rts.relocateUnits({{{unit->identity()->subject, {3.f, 4.f}}}, 1000}).ok());
        rts.setCrowdProvider(nullptr);
    }
    {
        eve::crowd::Crowd provider;
        auto              rts = std::make_unique<eve::rts::RTS>();
        rts->setCrowdProvider(&provider);
        auto* unit          = rts->newUnit(subject("00000000-0000-7000-8000-000000000973")).takeValue();
        unit->crowd()->link = eve::rts::CrowdLink::bind("lifetime-other-order").takeValue();
        REQUIRE(rts->step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}, executor).ok());
        rts.reset();
        CHECK_EQ(provider.getAgentCount(), 0);
    }
}

TEST_CASE("rts.crowd.invalidClockRejectsBeforeBindingOrIntentMutation") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    auto*             unit = Unit::createUnit();
    unit->crowd()->link    = eve::rts::CrowdLink::bind("clock-unit").takeValue();
    unit->motion()->speed  = 0.f;
    eve::SimulationStep step{eve::SimulationTick{2}, eve::Duration::fromNanoseconds(10000000)};
    REQUIRE(!eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    CHECK_EQ(unit->crowd()->runtimeOwner, 0u);
    step.tick = eve::SimulationTick{1};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const eve::crowd::AgentRef ref{unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner};
    const auto                 before = crowd.readAgent(ref).takeValue();
    unit->motion()->speed             = 10.f;
    unit->crowd()->link               = eve::rts::CrowdLink::bind("clock-rebound").takeValue();
    REQUIRE(!eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(crowd.isAgentAlive(ref));
    CHECK(!crowd.hasNamedAgent("clock-rebound"));
    CHECK_EQ(crowd.readAgent(ref).value().x, before.x);
    step.tick = eve::SimulationTick{2};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(!crowd.isAgentAlive(ref));
    unit->release();
}

TEST_CASE("rts.crowd.scriptRelocationUsesCheckedOwnerPlacement") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.addFunc("createRelocationUnit", [](eve::rts::RTS* rts) {
        auto created = rts->newUnit(subject("00000000-0000-7000-8000-000000000971"));
        if (!created) return false;
        created.value()->motion()->x     = 2.f;
        created.value()->motion()->y     = 2.f;
        created.value()->motion()->speed = 0.f;
        return true;
    });
    vm.addFunc("relocationX", [](eve::rts::RTS* rts) {
        return rts->findUnit(subject("00000000-0000-7000-8000-000000000971"))->motion()->x;
    });
    vm.run(vm.compileSource(R"(
        local sim = eve.RTS();
        assert(sim.configureScriptWorld(32,32,1.0,0.0,0.0).ok);
        assert(createRelocationUnit(sim));
        assert(sim.stepScript(0.01).ok);
        local id = "00000000-0000-7000-8000-000000000971";
        assert(sim.relocateUnits([{subject=id,x=8.0,y=8.0}],10000).ok);
        assert(relocationX(sim) == 8.0);
        assert(!sim.relocateUnits([{subject=id,x=9.0,y=8.0},{subject=id,x=10.0,y=8.0}],10000).ok);
        assert(!sim.relocateUnits([{subject="invalid",x=9.0,y=8.0}],10000).ok);
        assert(relocationX(sim) == 8.0);
    )"));
}

TEST_CASE("rts.crowd.cachedProjectionUsesCanonicalStateAfterSwapPop") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    const int         unrelated = crowd.addNamedAgent("unrelated", 100.f, 0.f, 0.f, 1.f);
    auto*             unit      = Unit::createUnit();
    unit->crowd()->link         = eve::rts::CrowdLink::bind("cached").takeValue();
    unit->motion()->speed       = 0.f;
    const eve::SimulationStep step{eve::SimulationTick{0}, eve::Duration{}};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const eve::crowd::AgentRef reference{unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner};
    REQUIRE(crowd.removeAgent(unrelated));
    REQUIRE(crowd.setAgentPosition(crowd.getNamedAgentIndex("cached"), 7.f, 3.f));
    unit->motion()->x = 99.f;  // An old ECS projection must not overwrite the movement owner.
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(unit->crowd()->runtimeAgent, reference.handle);
    CHECK_EQ(unit->motion()->x, 7.f);
    CHECK_EQ(unit->motion()->y, 3.f);
    unit->release();
}

TEST_CASE("rts.crowd.rebindingAndUnlinkInvalidateOldProjection") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    auto*             unit = Unit::createUnit();
    unit->crowd()->link    = eve::rts::CrowdLink::bind("old").takeValue();
    const eve::SimulationStep step{eve::SimulationTick{0}, eve::Duration{}};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    const eve::crowd::AgentRef old{unit->crowd()->runtimeAgent, unit->crowd()->runtimeOwner};
    unit->crowd()->link = eve::rts::CrowdLink::bind("new").takeValue();
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK(!crowd.isAgentAlive(old));
    CHECK(!crowd.hasNamedAgent("old"));
    CHECK(crowd.hasNamedAgent("new"));
    unit->crowd()->link = {};
    REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    CHECK_EQ(unit->crowd()->runtimeOwner, 0u);
    unit->release();
}

TEST_CASE("rts.crowd.duplicateBindingNamesRejectBeforeCreation") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    auto*             a = Unit::createUnit();
    auto*             b = Unit::createUnit();
    a->crowd()->link    = eve::rts::CrowdLink::bind("duplicate").takeValue();
    b->crowd()->link    = eve::rts::CrowdLink::bind("duplicate").takeValue();
    const eve::SimulationStep step{eve::SimulationTick{0}, eve::Duration{}};
    REQUIRE(!eve::rts::CrowdMotionSystem::step(step, crowd).ok());
    CHECK_EQ(crowd.getAgentCount(), 0);
    a->release();
    b->release();
}
}  // namespace test_rts_crowd_registry_composition

namespace test_rts_crowd_surface_adapter_composition {
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
}  // namespace test_rts_crowd_surface_adapter_composition

namespace test_rts_crowd_zero_delta_composition {
TEST_CASE("rts.crowd.zeroDeltaReconcilesOnlyReachedGoals") {
    ecs::Table        world;
    ecs::ScopedTable  guard(world);
    eve::crowd::Crowd crowd;
    auto*             reached = Unit::createUnit();
    auto*             moving  = Unit::createUnit();
    reached->crowd()->link    = eve::rts::CrowdLink::bind("zero/reached").takeValue();
    moving->crowd()->link     = eve::rts::CrowdLink::bind("zero/moving").takeValue();
    moving->motion()->x       = 5.f;
    reached->motion()->speed = moving->motion()->speed = 0.f;
    CommandSpec reachedOrder;
    reachedOrder.kind   = OrderKind::Move;
    reachedOrder.target = {0.f, 0.f};
    CommandSpec movingOrder;
    movingOrder.kind   = OrderKind::Move;
    movingOrder.target = {10.f, 0.f};
    REQUIRE(reached->orders()->values.enqueue(reachedOrder).ok());
    REQUIRE(moving->orders()->values.enqueue(movingOrder).ok());
    const eve::SimulationTick tick{1};
    REQUIRE(eve::rts::CrowdMotionSystem::step({tick, eve::Duration::fromSeconds(0.01).expect("test dt")}, crowd).ok());
    REQUIRE(eve::rts::CrowdMotionSystem::step({tick, eve::Duration{}}, crowd).ok());
    CHECK(reached->motion()->arrived);
    CHECK(!moving->motion()->arrived);
    const auto reachedState = crowd.getAgentState(crowd.getNamedAgentIndex("zero/reached"));
    const auto movingState  = crowd.getAgentState(crowd.getNamedAgentIndex("zero/moving"));
    CHECK_EQ(reachedState.action, static_cast<int>(eve::crowd::AgentAction::Idle));
    CHECK_EQ(movingState.action, static_cast<int>(eve::crowd::AgentAction::Seek));
    reached->release();
    moving->release();
}
}  // namespace test_rts_crowd_zero_delta_composition
