#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

eve::climbing::ClimbingActionDefinition action(std::string id, eve::climbing::ClimbingActionKind kind) {
    eve::climbing::ClimbingActionDefinition result;
    result.id         = std::move(id);
    result.kind       = kind;
    result.minHeight  = 0.f;
    result.maxHeight  = 3.f;
    result.duration   = seconds(0.1);
    result.apexHeight = kind == eve::climbing::ClimbingActionKind::LedgeJump ? 0.35f : 0.f;
    return result;
}

eve::climbing::ClimbingAnchorNodeDefinition node(std::string id, eve::climbing::Vec3 position,
                                                 eve::climbing::ClimbingAnchorKind kind) {
    eve::climbing::ClimbingAnchorNodeDefinition result;
    result.id              = std::move(id);
    result.kind            = kind;
    result.localPosition   = position;
    result.localNormal     = {0.f, 0.f, -1.f};
    result.localTangent    = {1.f, 0.f, 0.f};
    result.leftHandSocket  = {position.x - 0.2f, position.y, position.z};
    result.rightHandSocket = {position.x + 0.2f, position.y, position.z};
    result.feetSocket      = {position.x, position.y - 1.f, position.z - 0.2f};
    result.tags            = {"route"};
    return result;
}

eve::climbing::ClimbingAnchorEdgeDefinition edge(std::string from, std::string to,
                                                 eve::climbing::ClimbingAnchorEdgeKind kind) {
    eve::climbing::ClimbingAnchorEdgeDefinition result;
    result.from = std::move(from);
    result.to   = std::move(to);
    result.kind = kind;
    return result;
}

eve::climbing::ClimbingAnchorGraphDefinition ledgeGraph() {
    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "test:ledge-route";
    graph.sourceGeometryContentId = "test:geometry-v1";
    graph.buildSettingsHash       = "test:settings-v1";
    graph.nodes                   = {
        node("ledge:left", {-0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Ledge),
        node("ledge:right", {0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Ledge),
        node("corner:outer", {1.f, 1.f, 0.25f}, eve::climbing::ClimbingAnchorKind::CornerOuter),
    };
    graph.edges = {
        edge("ledge:left", "ledge:right", eve::climbing::ClimbingAnchorEdgeKind::Shimmy),
        edge("ledge:right", "corner:outer", eve::climbing::ClimbingAnchorEdgeKind::Corner),
    };
    return graph;
}

eve::climbing::ClimbingAnchorGraphDefinition ladderGraph() {
    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "test:ladder-route";
    graph.sourceGeometryContentId = "test:geometry-v1";
    graph.buildSettingsHash       = "test:settings-v1";
    graph.nodes                   = {
        node("ladder:bottom", {0.f, 0.f, -0.25f}, eve::climbing::ClimbingAnchorKind::LadderRung),
        node("ladder:top", {0.f, 1.25f, -0.25f}, eve::climbing::ClimbingAnchorKind::LadderRung),
        node("ladder:exit", {0.6f, 0.2f, -0.25f}, eve::climbing::ClimbingAnchorKind::Ledge),
    };
    graph.edges = {
        edge("ladder:bottom", "ladder:top", eve::climbing::ClimbingAnchorEdgeKind::Climb),
        edge("ladder:top", "ladder:exit", eve::climbing::ClimbingAnchorEdgeKind::Dismount),
    };
    return graph;
}

eve::climbing::ClimbingAnchorGraphDefinition parkourGraph() {
    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "test:parkour-route";
    graph.sourceGeometryContentId = "test:parkour-geometry-v1";
    graph.buildSettingsHash       = "test:parkour-settings-v1";
    graph.nodes                   = {
        node("beam:a", {-0.5f, 2.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Beam),
        node("beam:b", {0.5f, 2.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Beam),
        node("pole:a", {-0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Pole),
        node("bar:a", {0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Bar),
    };
    graph.edges = {
        edge("beam:a", "beam:b", eve::climbing::ClimbingAnchorEdgeKind::Balance),
        edge("pole:a", "bar:a", eve::climbing::ClimbingAnchorEdgeKind::Swing),
    };
    return graph;
}

struct AnchorWorld {
    AnchorWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("kinematic", 0.f, 1.f, 1.f);
        shape = body->newBoxShape(4.f, 4.f, 0.5f);
    }

    eve::climbing::ClimbingPose poseFor(const eve::climbing::ClimbingActionDefinition& start,
                                        eve::climbing::Vec3                            localNode) const {
        const eve::climbing::Vec3 worldNode{localNode.x, localNode.y + 1.f, localNode.z + 1.f};
        return {
            {worldNode.x, worldNode.y - start.hangFeetBelowLedge, worldNode.z - (0.3f + 0.03f + start.hangBodyOffset)},
            {0.f, 0.f, 1.f},
            0.f,
            body->getId(),
            0.f,
            false};
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

struct GraphLease {
    eve::climbing::ClimbingAnchorGraphHandleRef handle;
    ~GraphLease() {
        if (handle.isValid()) {
            auto released = eve::climbing::Climbing::releaseAnchorGraph(handle);
            released.ignore("test graph lease cleanup");
        }
    }
};

eve::climbing::ClimbingAnchorNodeRef nodeRef(eve::climbing::ClimbingAnchorGraphHandleRef handle, std::string_view id) {
    auto graph = eve::climbing::Climbing::resolveAnchorGraph(handle);
    REQUIRE(graph.isBound());
    auto reference = graph->nodeRef(id);
    REQUIRE(reference.ok());
    return std::move(reference).takeValue();
}

void advanceToEnd(eve::climbing::ClimbingRuntime& runtime, eve::physics::World3D& world, std::uint64_t tick) {
    auto advanced = runtime.advance(world, {eve::SimulationTick(tick), seconds(0.1)});
    REQUIRE(advanced.ok());
}

}  // namespace

TEST_CASE("climbing.anchorExecution.shimmyAndCornerKeepOneAtomicReservation") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ledgeGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());

    auto grab   = action("climbing:ledge_grab", eve::climbing::ClimbingActionKind::LedgeGrab);
    auto shimmy = action("climbing:shimmy", eve::climbing::ClimbingActionKind::Shimmy);
    auto corner = action("climbing:corner_outer", eve::climbing::ClimbingActionKind::CornerOuter);
    eve::climbing::ClimbingProfile profile;
    profile.maxObstacleHeight      = 3.f;
    profile.pathValidationSegments = 4;
    profile.actions                = {grab, shimmy, corner};

    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile).ok());
    const auto left = nodeRef(lease.handle, "ledge:left");
    REQUIRE(runtime
                .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(1001), grab.id,
                                fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(1))
                .ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    advanceToEnd(runtime, *fixture.world, 2);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Hanging));

    const auto right = nodeRef(lease.handle, "ledge:right");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, right, eve::climbing::ClimbingAnchorEdgeKind::Shimmy, shimmy.id,
                                  eve::SimulationTick(3))
                .ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    advanceToEnd(runtime, *fixture.world, 4);
    auto current = runtime.currentAnchor();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().nodeId, std::string("ledge:right"));

    const auto cornerNode = nodeRef(lease.handle, "corner:outer");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, cornerNode, eve::climbing::ClimbingAnchorEdgeKind::Corner, corner.id,
                                  eve::SimulationTick(5))
                .ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    advanceToEnd(runtime, *fixture.world, 6);
    current = runtime.currentAnchor();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().nodeId, std::string("corner:outer"));
    CHECK_EQ(graph->reservationCount(), std::size_t(1));

    REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(6)).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t(7));
    CHECK_EQ(static_cast<int>(events.value()[0].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(static_cast<int>(events.value()[1].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Hanging));
    CHECK_EQ(static_cast<int>(events.value()[2].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::AnchorTransitionStarted));
    CHECK_EQ(static_cast<int>(events.value()[3].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::AnchorReached));
    CHECK_EQ(static_cast<int>(events.value()[6].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Cancelled));
}

TEST_CASE("climbing.anchorExecution.beamPoleAndBarUseExplicitStablePhases") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(parkourGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};

    auto beam = action("parkour:beam_balance", eve::climbing::ClimbingActionKind::BeamBalance);
    auto pole = action("parkour:pole_swing", eve::climbing::ClimbingActionKind::PoleSwing);
    auto bar  = action("parkour:bar_swing", eve::climbing::ClimbingActionKind::BarSwing);
    eve::climbing::ClimbingProfile profile;
    profile.maxObstacleHeight      = 4.f;
    profile.pathValidationSegments = 4;
    profile.actions                = {beam, pole, bar};

    eve::climbing::ClimbingRuntime beamRuntime;
    REQUIRE(beamRuntime.setProfile(profile).ok());
    auto                        beamStart = nodeRef(lease.handle, "beam:a");
    eve::climbing::ClimbingPose beamPose{{-0.5f, 3.f, 0.75f}, {1.f, 0.f, 0.f}, 0.f, fixture.body->getId(), 0.f, true};
    REQUIRE(beamRuntime
                .tryBeginAnchor(*fixture.world, lease.handle, beamStart, eve::climbing::ClimbingAnchorAgentId(2001),
                                beam.id, beamPose, eve::SimulationTick(1))
                .ok());
    advanceToEnd(beamRuntime, *fixture.world, 2);
    CHECK_EQ(static_cast<int>(beamRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Balanced));
    auto beamTarget = nodeRef(lease.handle, "beam:b");
    REQUIRE(beamRuntime
                .transitionAnchor(*fixture.world, beamTarget, eve::climbing::ClimbingAnchorEdgeKind::Balance, beam.id,
                                  eve::SimulationTick(3))
                .ok());
    advanceToEnd(beamRuntime, *fixture.world, 4);
    CHECK_EQ(static_cast<int>(beamRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Balanced));
    REQUIRE(beamRuntime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(5)).ok());

    eve::climbing::ClimbingRuntime swingRuntime;
    REQUIRE(swingRuntime.setProfile(profile).ok());
    auto poleStart = nodeRef(lease.handle, "pole:a");
    REQUIRE(swingRuntime
                .tryBeginAnchor(*fixture.world, lease.handle, poleStart, eve::climbing::ClimbingAnchorAgentId(2002),
                                pole.id, fixture.poseFor(pole, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(10))
                .ok());
    advanceToEnd(swingRuntime, *fixture.world, 11);
    CHECK_EQ(static_cast<int>(swingRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Swinging));
    auto barTarget = nodeRef(lease.handle, "bar:a");
    REQUIRE(swingRuntime
                .transitionAnchor(*fixture.world, barTarget, eve::climbing::ClimbingAnchorEdgeKind::Swing, bar.id,
                                  eve::SimulationTick(12))
                .ok());
    advanceToEnd(swingRuntime, *fixture.world, 13);
    CHECK_EQ(static_cast<int>(swingRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Swinging));
}

TEST_CASE("climbing.anchorExecution.ladderMountClimbAndDismountShareOneExecution") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ladderGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());

    auto mount    = action("climbing:ladder_mount", eve::climbing::ClimbingActionKind::LadderMount);
    auto climb    = action("climbing:ladder_climb", eve::climbing::ClimbingActionKind::LadderClimb);
    auto dismount = action("climbing:ladder_dismount", eve::climbing::ClimbingActionKind::LadderDismount);
    eve::climbing::ClimbingProfile profile;
    profile.maxObstacleHeight      = 3.f;
    profile.pathValidationSegments = 4;
    profile.actions                = {mount, climb, dismount};

    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile).ok());
    const auto bottom = nodeRef(lease.handle, "ladder:bottom");
    REQUIRE(runtime
                .tryBeginAnchor(*fixture.world, lease.handle, bottom, eve::climbing::ClimbingAnchorAgentId(2001),
                                mount.id, fixture.poseFor(mount, {0.f, 0.f, -0.25f}), eve::SimulationTick(10))
                .ok());
    const auto executionId = runtime.executionId();
    advanceToEnd(runtime, *fixture.world, 11);

    const auto top = nodeRef(lease.handle, "ladder:top");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, top, eve::climbing::ClimbingAnchorEdgeKind::Climb, climb.id,
                                  eve::SimulationTick(12))
                .ok());
    advanceToEnd(runtime, *fixture.world, 13);
    CHECK(runtime.executionId() == executionId);
    CHECK_EQ(graph->reservationCount(), std::size_t(1));

    const auto exit = nodeRef(lease.handle, "ladder:exit");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, exit, eve::climbing::ClimbingAnchorEdgeKind::Dismount, dismount.id,
                                  eve::SimulationTick(14))
                .ok());
    advanceToEnd(runtime, *fixture.world, 15);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Completed));
    CHECK(runtime.executionId().isZero());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
}

TEST_CASE("climbing.anchorExecution.reloadAndDestructionCannotLeakOccupancy") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ledgeGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());
    auto       grab = action("climbing:ledge_grab", eve::climbing::ClimbingActionKind::LedgeGrab);
    const auto left = nodeRef(lease.handle, "ledge:left");

    {
        eve::climbing::ClimbingRuntime runtime;
        REQUIRE(runtime.upsertAction(grab).ok());
        REQUIRE(runtime
                    .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(3001),
                                    grab.id, fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(20))
                    .ok());
        CHECK_EQ(graph->reservationCount(), std::size_t(1));
    }
    CHECK_EQ(graph->reservationCount(), std::size_t(0));

    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(grab).ok());
    REQUIRE(runtime
                .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(3002), grab.id,
                                fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(30))
                .ok());
    auto reload = graph->reload(ledgeGraph());
    REQUIRE(reload.ok());
    REQUIRE_EQ(reload.value().invalidatedOccupants.size(), std::size_t(1));
    CHECK_EQ(reload.value().invalidatedOccupants.front().agentId.value(), std::uint64_t(3002));
    auto stale = runtime.advance(*fixture.world, {eve::SimulationTick(31), seconds(0.1)});
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
    CHECK_EQ(runtime.inspect().terminalCode, std::string("climbing.anchor.stale"));
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
}

TEST_CASE("climbing.anchorExecution.snapshotTransfersOrReacquiresOneExclusiveClaim") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ledgeGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());
    auto       grab = action("climbing:ledge_grab", eve::climbing::ClimbingActionKind::LedgeGrab);
    const auto left = nodeRef(lease.handle, "ledge:left");

    eve::climbing::ClimbingRuntime source;
    REQUIRE(source.upsertAction(grab).ok());
    REQUIRE(source
                .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(4001), grab.id,
                                fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(40))
                .ok());
    advanceToEnd(source, *fixture.world, 41);
    auto snapshot = source.snapshot();
    REQUIRE(snapshot.ok());

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    CHECK_EQ(static_cast<int>(restored.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Hanging));
    auto sourceStale = source.advance(*fixture.world, {eve::SimulationTick(42), seconds(0.1)});
    CHECK(!sourceStale.ok());
    CHECK_EQ(static_cast<int>(source.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
    CHECK_EQ(graph->reservationCount(), std::size_t(1));

    eve::climbing::ClimbingRuntime replay;
    auto                           duplicate = replay.restore(snapshot.value(), *fixture.world);
    CHECK(!duplicate.ok());
    CHECK_EQ(static_cast<int>(duplicate.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    REQUIRE(restored.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(42)).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));

    eve::Value detachedSnapshot;
    {
        eve::climbing::ClimbingRuntime detachedSource;
        REQUIRE(detachedSource.upsertAction(grab).ok());
        REQUIRE(detachedSource
                    .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(4002),
                                    grab.id, fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(50))
                    .ok());
        auto captured = detachedSource.snapshot();
        REQUIRE(captured.ok());
        detachedSnapshot = std::move(captured).takeValue();
        CHECK_EQ(graph->reservationCount(), std::size_t(1));
    }
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
    eve::climbing::ClimbingRuntime reacquired;
    REQUIRE(reacquired.restore(detachedSnapshot, *fixture.world).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    REQUIRE(reacquired.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(50)).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
}
