// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"default":"physics-backend","module":"physics_field_coupling"}
*/
// clang-format on
#include <array>
#include <cmath>
#include <memory>
#include "physics/Body3D.h"
#include "physics/PhysicsLink.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/field/GravityField.h"
#include "physics/field_coupling/PointGravityBatch.h"
#include "physics/forces/ForceReceipt.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::physics;

TEST_CASE("physics.fieldCoupling.pointGravityUsesOneIntegratorAndPreservesLocalGravity") {
    World3D                  world(0, -2, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", -5, 0, 0)), b(world.newBody("dynamic", 5, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(1)), sb(b->newSphereShape(1));
    a->setLinearDamping(0);
    b->setLinearDamping(0);
    auto la = PhysicsLink::fromBody(*a);
    REQUIRE(la.ok());
    auto lb = PhysicsLink::fromBody(*b);
    REQUIRE(lb.ok());
    const std::array<PhysicsLink, 2> links{la.value(), lb.value()};
    const eve::SimulationStep        step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)};
    field::GravityPolicy             policy;
    auto                             batch = PointGravityBatch::prepare(world, links, policy, step);
    REQUIRE(batch.ok());
    CHECK_EQ(a->getLinearVelocityX(), 0.f);
    auto receipt = batch.value().commit(world);
    REQUIRE(receipt.ok());
    CHECK(receipt.value().status() == ForceStepStatus::Pending);
    CHECK_EQ(a->getLinearVelocityX(), 0.f);
    const double expected = b->getMass() / 100.0 * 0.01;
    auto         stepped  = world.step(step);
    REQUIRE(stepped.ok());
    CHECK(std::abs(a->getLinearVelocityX() - expected) < 1e-6);
    CHECK(std::abs(b->getLinearVelocityX() + expected) < 1e-6);
    CHECK(std::abs(a->getLinearVelocityY() + 0.02) < 1e-5);
    CHECK(receipt.value().status() == ForceStepStatus::Applied);
    auto repeated = batch.value().commit(world);
    CHECK(!repeated.ok());
}

TEST_CASE("physics.fieldCoupling.changedPositionMassAndTickRejectBeforeApplying") {
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", -5, 0, 0)), b(world.newBody("dynamic", 5, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(1)), sb(b->newSphereShape(1));
    auto                     la = PhysicsLink::fromBody(*a);
    REQUIRE(la.ok());
    auto lb = PhysicsLink::fromBody(*b);
    REQUIRE(lb.ok());
    const std::array<PhysicsLink, 2> links{la.value(), lb.value()};
    const eve::SimulationStep        step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)};
    field::GravityPolicy             policy;
    auto                             moved = PointGravityBatch::prepare(world, links, policy, step);
    REQUIRE(moved.ok());
    b->setPosition(6, 0, 0);
    auto rejectedMove = moved.value().commit(world);
    REQUIRE(!rejectedMove.ok());
    CHECK(rejectedMove.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Conflict);
    auto mass = PointGravityBatch::prepare(world, links, policy, step);
    REQUIRE(mass.ok());
    sb->setDensity(2);
    auto rejectedMass = mass.value().commit(world);
    REQUIRE(!rejectedMass.ok());
    CHECK(rejectedMass.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Conflict);
    auto staleTick = PointGravityBatch::prepare(world, links, policy, step);
    REQUIRE(staleTick.ok());
    auto stepped = world.step(step);
    REQUIRE(stepped.ok());
    CHECK_EQ(a->getLinearVelocityX(), 0.f);
    CHECK_EQ(b->getLinearVelocityX(), 0.f);
    auto rejectedTick = staleTick.value().commit(world);
    REQUIRE(!rejectedTick.ok());
    CHECK(rejectedTick.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Conflict);
}

TEST_CASE("physics.fieldCoupling.budgetMoveForeignWorldAndDestroyedTargetsAreSafe") {
    World3D                  world(0, 0, 0, false), foreign(0, 0, 0, false);
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", -5, 0, 0)), b(world.newBody("dynamic", 5, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(1)), sb(b->newSphereShape(1));
    auto                     la = PhysicsLink::fromBody(*a);
    REQUIRE(la.ok());
    auto lb = PhysicsLink::fromBody(*b);
    REQUIRE(lb.ok());
    const std::array<PhysicsLink, 2> links{la.value(), lb.value()};
    const eve::SimulationStep        step{eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)};
    field::GravityPolicy             policy;
    policy.maxDirectPairs = 0;
    auto denied           = PointGravityBatch::prepare(world, links, policy, step);
    CHECK(!denied.ok());
    policy.maxDirectPairs        = 64;
    policy.gravitationalConstant = 1e-100;
    auto underflow               = PointGravityBatch::prepare(world, links, policy, step);
    REQUIRE(!underflow.ok());
    CHECK(underflow.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Unsupported);
    policy.gravitationalConstant = 1;
    auto prepared                = PointGravityBatch::prepare(world, links, policy, step);
    REQUIRE(prepared.ok());
    auto foreignCommit = prepared.value().commit(foreign);
    REQUIRE(!foreignCommit.ok());
    CHECK(foreignCommit.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
    auto moved    = std::move(prepared.value());
    auto consumed = prepared.value().commit(world);
    CHECK(!consumed.ok());
    b->destroy();
    auto destroyed = moved.commit(world);
    REQUIRE(!destroyed.ok());
    CHECK(destroyed.status().primaryDiagnostic()->code() == eve::DiagnosticCode::StaleHandle);
    auto stepped = world.step(step);
    REQUIRE(stepped.ok());
    CHECK_EQ(a->getLinearVelocityX(), 0.f);
}
