#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <memory>

namespace {

struct World {
    World() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("static", 0.f, 0.5f, 1.f);
        shape = body->newBoxShape(2.f, 1.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

eve::climbing::ClimbingActionDefinition action(float apex = 0.7f) {
    eve::climbing::ClimbingActionDefinition result;
    result.id             = "climb:mantle";
    result.minHeight      = 0.4f;
    result.maxHeight      = 1.2f;
    result.duration       = eve::Duration::fromNanoseconds(600000000);
    result.landingForward = 0.6f;
    result.apexHeight     = apex;
    return result;
}

void begin(World& fixture, eve::climbing::ClimbingRuntime& runtime) {
    REQUIRE(runtime.upsertAction(action()).ok());
    auto begun = runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(10));
    REQUIRE(begun.ok());
}

}  // namespace

TEST_CASE("climbing.snapshot.roundTripRestoresPinnedExecution") {
    World                          fixture;
    eve::climbing::ClimbingRuntime source;
    begin(fixture, source);
    auto snapshot = source.snapshot();
    REQUIRE(snapshot.ok());

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    CHECK_EQ(static_cast<int>(restored.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Requested));
    CHECK(restored.executionId() == source.executionId());
    auto roundTrip = restored.snapshot();
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value() == snapshot.value());
}

TEST_CASE("climbing.snapshot.supportsNMinusOneAndPinsActiveDefinitionAcrossReload") {
    World                          fixture;
    eve::climbing::ClimbingRuntime runtime;
    begin(fixture, runtime);
    REQUIRE(runtime.upsertAction(action(4.0f)).ok());

    auto snapshot = runtime.snapshot();
    REQUIRE(snapshot.ok());
    snapshot.value().set("schemaVersion",
                         eve::Value(std::int64_t(eve::climbing::ClimbingRuntime::SnapshotSchemaVersion - 1)));

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    auto migrated = restored.snapshot();
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value().find("schemaVersion")->asInt(), eve::climbing::ClimbingRuntime::SnapshotSchemaVersion);
    const auto* restoredExecution = migrated.value().find("execution")->getIf<eve::Value::Object>();
    REQUIRE(restoredExecution != nullptr);
    const auto* pinnedAction = restoredExecution->at("action").getIf<eve::Value::Object>();
    REQUIRE(pinnedAction != nullptr);
    CHECK(pinnedAction->at("apexHeight").asDouble() < 1.0);
}

TEST_CASE("climbing.snapshot.futureVersionAndStaleLinkFailuresAreAtomic") {
    World                          fixture;
    eve::climbing::ClimbingRuntime runtime;
    begin(fixture, runtime);
    auto before = runtime.snapshot();
    REQUIRE(before.ok());

    eve::Value future = before.value();
    future.set("schemaVersion", eve::Value(std::int64_t(eve::climbing::ClimbingRuntime::SnapshotSchemaVersion + 1)));
    auto unknown = runtime.restore(future, *fixture.world);
    CHECK(!unknown.ok());
    CHECK_EQ(static_cast<int>(unknown.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
    auto afterUnknown = runtime.snapshot();
    REQUIRE(afterUnknown.ok());
    CHECK(afterUnknown.value() == before.value());

    fixture.world->destroyBody(fixture.body);
    fixture.body  = nullptr;
    fixture.shape = nullptr;
    auto stale    = runtime.restore(before.value(), *fixture.world);
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(stale.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
    auto afterStale = runtime.snapshot();
    REQUIRE(afterStale.ok());
    CHECK(afterStale.value() == before.value());
}
