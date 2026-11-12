// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"physics-backend","module":"physics"}
*/
// clang-format on
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceBatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace eve::physics;

TEST_CASE("physics.forces.centerSnapshotOwnsSelectedStateAndRejectsDuplicateSources") {
    World3D                               world(0, 0, 0, false);
    std::vector<std::unique_ptr<Body3D>>  bodies;
    std::vector<std::unique_ptr<Shape3D>> shapes;
    std::vector<PhysicsLink>              targets;
    for (unsigned i = 0; i < 10; ++i) {
        bodies.emplace_back(world.newBody("dynamic", 4.f * static_cast<float>(i), 0, 0));
        shapes.emplace_back(bodies.back()->newSphereShape(1));
    }
    for (unsigned i = 9; i-- > 0;) {
        auto link = PhysicsLink::fromBody(*bodies[i]);
        REQUIRE(link.ok());
        targets.push_back(link.value());
    }
    auto snapshot = ForceBatch::captureCenters(world, targets);
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().bodies.size(), std::size_t{9});
    CHECK(snapshot.value().world == world.runtimeHandle());
    CHECK(snapshot.value().tick == world.simulationObservation().lastTick);
    for (std::size_t i = 0; i < snapshot.value().bodies.size(); ++i) {
        CHECK_EQ(snapshot.value().bodies[i].center.x, 4.0 * static_cast<double>(i));
        CHECK(snapshot.value().bodies[i].mass > 0);
    }
    bodies[0]->setPosition(100, 0, 0);
    CHECK_EQ(snapshot.value().bodies[0].center.x, 0.0);
    auto moved = ForceBatch::captureCenters(world, targets);
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().bodies[0].center.x, 100.0);
    targets.back()  = targets.front();
    auto duplicated = ForceBatch::captureCenters(world, targets);
    CHECK(!duplicated.ok());
    bodies[8]->destroy();
    auto stale = ForceBatch::captureCenters(world, targets);
    CHECK(!stale.ok());
}

TEST_CASE("physics.forces.batchLinearMatchingPreservesInputAccumulation") {
    World                                 world(0, 0, false, 1);
    std::vector<std::unique_ptr<Body>>    bodies;
    std::vector<std::unique_ptr<Fixture>> fixtures;
    std::vector<ForceLoad>                loads;
    for (unsigned i = 0; i < 128; ++i) {
        bodies.emplace_back(world.newBody("dynamic", 4.f * static_cast<float>(i), 0));
        fixtures.emplace_back(bodies.back()->newCircleFixture(1));
    }
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (std::size_t i = bodies.size(); i-- > 0;) {
            auto link = PhysicsLink::fromBody(*bodies[i]);
            REQUIRE(link.ok());
            const double force = phase == 0 ? 1e20 : phase == 1 ? -1e20 : 2 * bodies[i]->getMass();
            loads.push_back({link.value(), {force, 0, 0}, {4.0 * static_cast<double>(i), 0, 0}, {}});
        }
    }
    auto prepared = ForceBatch::prepare(world, loads);
    REQUIRE(prepared.ok());
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)};
    auto                      committed = prepared.value().commit(world, step);
    REQUIRE(committed.ok());
    auto stepped = world.step(step);
    REQUIRE(stepped.ok());
    for (const auto& body : bodies) CHECK(std::abs(body->getLinearVelocityX() - 0.02f) < 1e-5f);
}

TEST_CASE("physics.forces.batchLinearMatchingRejectsForeignAndStaleWithoutPartialForce") {
    World                    world(0, 0, false, 1), foreign(0, 0, false, 1);
    std::unique_ptr<Body>    body(world.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> fixture(body->newCircleFixture(1));
    std::unique_ptr<Body>    other(foreign.newBody("dynamic", 0, 0));
    auto                     link = PhysicsLink::fromBody(*body);
    REQUIRE(link.ok());
    auto foreignLink = PhysicsLink::fromBody(*other);
    REQUIRE(foreignLink.ok());
    std::vector<ForceLoad> loads{{link.value(), {10, 0, 0}, {}, {}}, {foreignLink.value(), {10, 0, 0}, {}, {}}};
    auto                   rejected = ForceBatch::prepare(world, loads);
    REQUIRE(!rejected.ok());
    CHECK(rejected.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
    loads[1]      = loads[0];
    auto prepared = ForceBatch::prepare(world, loads);
    REQUIRE(prepared.ok());
    body->destroy();
    auto stale = prepared.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)});
    REQUIRE(!stale.ok());
    CHECK(stale.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
}

TEST_CASE("physics.forces.batchLinearMatchingSpatialDuplicateLoadsPreserveTorque") {
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
    body->setLinearDamping(0);
    body->setAngularDamping(0);
    auto link = PhysicsLink::fromBody(*body);
    REQUIRE(link.ok());
    const ForceLoad loads[]  = {{link.value(), {10, 0, 0}, {0, 1, 0}, {}}, {link.value(), {-10, 0, 0}, {0, -1, 0}, {}}};
    auto            prepared = ForceBatch::prepare(world, loads);
    REQUIRE(prepared.ok());
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)};
    auto                      committed = prepared.value().commit(world, step);
    REQUIRE(committed.ok());
    auto stepped = world.step(step);
    REQUIRE(stepped.ok());
    CHECK(std::abs(body->getLinearVelocityX()) < 1e-6f);
    CHECK(body->getAngularVelocityZ() < 0);
    auto repeated = prepared.value().commit(world, step);
    CHECK(!repeated.ok());
}
