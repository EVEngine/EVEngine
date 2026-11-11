#pragma once
#include "RTSSpawnRestoreFixture.h"
#include "action/Action.h"
#include "rts/RTSAction.h"
#include "rts/RTSCrowdCapture.h"
namespace test_rts_spawn {
inline std::shared_ptr<const eve::rts::CommandSpec> crowdMoveCommand() {
    auto command    = std::make_shared<eve::rts::CommandSpec>();
    command->kind   = eve::rts::OrderKind::Move;
    command->target = {20, 12};
    return command;
}
inline void crowdStep(Fixture& source, std::uint64_t tick) {
    ecs::ScopedTable scope(*source.table);
    eve::rts::CrowdMotionSystem::step({eve::SimulationTick(tick), eve::Duration(100000000)}, source.crowd)
        .expect("canonical source Crowd step");
}
struct SharedFlowFixture : Fixture {
    eve::action::ActionRuntime actions;
    eve::rts::ActionAdapter    executor{actions};
    explicit SharedFlowFixture(int work = 3, float origin = 0.f) : Fixture(crowdMoveCommand()) {
        eve::crowd::NavigationSurfaceSnapshot surface;
        surface.stamp = {eve::crowd::NavigationSurfaceId(1), eve::crowd::MovementClassId(1), 1, 1, 1, .35f};
        surface.width = surface.height = 32;
        surface.cellSize               = 1.f;
        surface.originX = surface.originY = origin;
        surface.terrainCosts.assign(1024, 1.f);
        surface.navigationCosts.assign(1024, 1.f);
        crowd.commitNavigationSurface(crowd.prepareNavigationSurface(surface).expect("native movement surface"))
            .expect("publish source movement surface");
        domain->configureCrowdFlowNavigation({work, 1}).expect("real shared-flow quotas");
        sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("shared-flow source population");
        ecs::ScopedTable             scope(*table);
        eve::rts::MovementGroupBatch group;
        group.units        = {request.units[0].subject, request.units[1].subject};
        group.target       = {20.f, 12.f};
        group.formation    = {eve::rts::FormationKind::Column, 2.f, 0};
        group.leadDistance = 1.f;
        domain->submitMovementGroup(group).expect("canonical group command");
        // A waiting member keeps the real group on its constrained shared-field route.
        domain->findUnit(request.units[0].subject)->navigation()->trafficWaiting = true;
        step(1);
    }
    void step(std::uint64_t tick, std::int64_t nanoseconds = 100000000) {
        ecs::ScopedTable scope(*table);
        domain->step({eve::SimulationTick(tick), eve::Duration(nanoseconds)}, executor)
            .expect("actual shared-flow RTS step");
    }
};
struct SavedRTSCrowdScope {
    SavedRTSSpawn                                    session;
    std::shared_ptr<const eve::rts::RTSCrowdCapture> crowd;
    std::weak_ptr<ecs::Table>                        sourceWorld;
};
inline SavedRTSCrowdScope freezeCrowdScope() {
    Fixture source(crowdMoveCommand());
    source.sourceWorld->advance({eve::SimulationTick(1), eve::Duration(0)}).expect("real spawned members");
    crowdStep(source, 1);
    auto epoch   = source.request.source.owner.child("native-crowd");
    auto capture = source.domain->captureCrowd(epoch).expect("native Crowd scope");
    return {freezeSpawn(source, epoch), std::make_shared<const eve::rts::RTSCrowdCapture>(std::move(capture)),
            source.table};
}
}  // namespace test_rts_spawn
