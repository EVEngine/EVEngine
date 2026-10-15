#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <memory>

namespace {

struct EventWorld {
    EventWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("static", 0.f, 0.5f, 1.f);
        shape = body->newBoxShape(2.f, 1.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

eve::climbing::ClimbingActionDefinition action() {
    eve::climbing::ClimbingActionDefinition value;
    value.id             = "climb:event_mantle";
    value.minHeight      = 0.4f;
    value.maxHeight      = 1.2f;
    value.duration       = eve::Duration::fromNanoseconds(600000000);
    value.landingForward = 0.6f;
    value.apexHeight     = 0.7f;
    value.eventMetadata  = {"event:parkour", "surface:stone"};
    return value;
}

eve::climbing::ClimbingPose pose() { return {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}; }

}  // namespace

TEST_CASE("climbing.events.detachBeforeReentrantConsumerMutation") {
    EventWorld                     fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(action()).ok());
    auto first = runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(10));
    REQUIRE(first.ok());

    auto dispatched = runtime.drainEvents();
    REQUIRE(dispatched.ok());
    REQUIRE_EQ(dispatched.value().size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(dispatched.value().front().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    REQUIRE_EQ(dispatched.value().front().metadata.size(), std::size_t(2));
    CHECK_EQ(dispatched.value().front().metadata.front(), std::string("event:parkour"));

    // This models a post-simulation callback: the batch is already detached, so
    // reentrant mutations append to a fresh queue rather than invalidating iteration.
    for (const auto& event : dispatched.value()) {
        REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, event.tick).ok());
        auto second = runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(event.tick.value() + 1));
        REQUIRE(second.ok());
        CHECK_EQ(second.value().definitionGeneration, first.value().definitionGeneration);
    }
    CHECK_EQ(runtime.executionId().value(), std::uint64_t(2));
    CHECK_EQ(runtime.pendingEventCount(), std::size_t(2));

    auto reentrant = runtime.drainEvents();
    REQUIRE(reentrant.ok());
    REQUIRE_EQ(reentrant.value().size(), std::size_t(2));
    CHECK_EQ(static_cast<int>(reentrant.value()[0].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Cancelled));
    CHECK_EQ(static_cast<int>(reentrant.value()[1].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(reentrant.value()[0].executionId.value(), std::uint64_t(1));
    CHECK_EQ(reentrant.value()[1].executionId.value(), std::uint64_t(2));
}

TEST_CASE("climbing.events.snapshotPreservesUndeliveredOrder") {
    EventWorld                     fixture;
    eve::climbing::ClimbingRuntime source;
    REQUIRE(source.upsertAction(action()).ok());
    REQUIRE(source.tryBegin(*fixture.world, pose(), eve::SimulationTick(20)).ok());
    for (std::uint64_t tick = 21; tick <= 26; ++tick) {
        auto advanced =
            source.advance(*fixture.world, {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(100000000)});
        REQUIRE(advanced.ok());
    }
    CHECK_EQ(static_cast<int>(source.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Completed));

    auto snapshot = source.snapshot();
    REQUIRE(snapshot.ok());
    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    auto events = restored.drainEvents();
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t(3));
    CHECK_EQ(static_cast<int>(events.value()[0].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(static_cast<int>(events.value()[1].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Landed));
    CHECK_EQ(static_cast<int>(events.value()[2].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Completed));
    CHECK_EQ(events.value()[0].metadata, events.value()[2].metadata);
    CHECK_EQ(events.value()[1].metadata.front(), std::string("event:parkour"));
}

TEST_CASE("climbing.events.capacityBackpressureIsAtomic") {
    EventWorld                     fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(action()).ok());
    std::uint64_t tick = 30;
    for (std::size_t index = 0; index < eve::climbing::ClimbingRuntime::PendingEventCapacity / 2; ++index) {
        REQUIRE(runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(tick)).ok());
        REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(tick)).ok());
        ++tick;
    }
    CHECK_EQ(runtime.pendingEventCount(), eve::climbing::ClimbingRuntime::PendingEventCapacity);
    const auto phaseBefore = runtime.phase();
    auto       rejected    = runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(tick));
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(phaseBefore));
    CHECK(runtime.executionId().isZero());

    auto drained = runtime.drainEvents();
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value().size(), eve::climbing::ClimbingRuntime::PendingEventCapacity);
    REQUIRE(runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(tick)).ok());
}
