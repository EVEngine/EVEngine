#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/forces/ForceBatch.h"
#include "physics/forces/ForceModels.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>

using namespace eve::physics;
TEST_CASE("physics.forces.application2DIsMeterIndependent") {
    for (float meter : {10.f, 30.f, 100.f}) {
        World                    world(0, 0, false, meter);
        std::unique_ptr<Body>    body(world.newBody("dynamic", 0, 0));
        std::unique_ptr<Fixture> fixture(body->newCircleFixture(meter));
        const double             mass  = body->getMass();
        auto                     force = ForceModels::directional({{}, {2, 0, 0}}, {}, mass);
        REQUIRE(force.ok());
        REQUIRE(applyForceSI(*body, force.value(), {}).ok());
        REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
        CHECK(std::abs(body->getLinearVelocityX() / meter - 0.02) < 1e-5);
        CHECK(!applyForceSI(*body, {1, 0, 1}, {}).ok());
        body->destroy();
        CHECK(!applyForceSI(*body, {1, 0, 0}, {}).ok());
    }
}
TEST_CASE("physics.forces.application3DForceAndOffCenterTorque") {
    World3D                  world(0, 0, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
    body->setLinearDamping(0);
    body->setAngularDamping(0);
    auto force = ForceModels::directional({{}, {2, 0, 0}}, {}, body->getMass());
    REQUIRE(force.ok());
    REQUIRE(applyForceSI(*body, force.value(), {0, 1, 0}).ok());
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(std::abs(body->getLinearVelocityX() - 0.02) < 1e-5);
    CHECK_LT(body->getAngularVelocityZ(), 0);
    body->destroy();
    CHECK(!applyForceSI(*body, {1, 0, 0}, {}).ok());
}
TEST_CASE("physics.forces.applicationRejectsStaticWithoutMutation") {
    World                 world(0, 0, false, 30);
    std::unique_ptr<Body> body(world.newBody("static", 0, 0));
    CHECK(!applyForceSI(*body, {1, 0, 0}, {}).ok());
    CHECK_EQ(body->getLinearVelocityX(), 0);
}

TEST_CASE("physics.forces.batch2DAtomicityAndCoupleAreMeterIndependent") {
    for (float meter : {10.f, 100.f}) {
        World                    world(0, 0, false, meter);
        std::unique_ptr<Body>    body(world.newBody("dynamic", 0, 0));
        std::unique_ptr<Fixture> fixture(body->newCircleFixture(meter));
        auto                     link = PhysicsLink::fromBody(*body);
        REQUIRE(link.ok());
        ForceLoad loads[]  = {{link.value(), {10, 0, 0}, {0, 1, 0}, {}}, {link.value(), {-10, 0, 0}, {0, -1, 0}, {}}};
        auto      prepared = ForceBatch::prepare(world, loads);
        REQUIRE(prepared.ok());
        CHECK_EQ(body->getAngularVelocity(), 0);
        REQUIRE(
            prepared.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
        CHECK(!prepared.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
        REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
        CHECK(std::abs(body->getLinearVelocityX()) < 1e-6);
        CHECK(body->getAngularVelocity() < 0);
        const double expected = -20 * .01 / body->raw()->GetInertia();
        CHECK(std::abs(body->getAngularVelocity() - expected) < 1e-5);
    }
}
TEST_CASE("physics.forces.batchRejectsInvalidTailBeforeWaking") {
    World3D                  world(0, 0, 0, true);
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Body3D>  b(world.newBody("dynamic", 3, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(1)), sb(b->newSphereShape(1));
    auto                     la = PhysicsLink::fromBody(*a);
    REQUIRE(la.ok());
    auto lb = PhysicsLink::fromBody(*b);
    REQUIRE(lb.ok());
    b3Body_SetAwake(eve::physics::box3d::Access::bodyId(*a), false);
    ForceLoad loads[] = {{la.value(), {10, 0, 0}, {}, {}}, {lb.value(), {0, 1e30, 0}, {1e30, 0, 0}, {}}};
    CHECK(!ForceBatch::prepare(world, loads).ok());
    CHECK(!b3Body_IsAwake(eve::physics::box3d::Access::bodyId(*a)));
    loads[1].force = {1, 0, 0};
    loads[1].point = {};
    auto prepared  = ForceBatch::prepare(world, loads);
    REQUIRE(prepared.ok());
    b->destroy();
    CHECK(!prepared.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(!b3Body_IsAwake(eve::physics::box3d::Access::bodyId(*a)));
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK_EQ(a->getLinearVelocityX(), 0);
}
TEST_CASE("physics.forces.batchMoveAndForeignWorldCannotDuplicateSubmission") {
    World3D                  world(0, 0, 0, false), foreign(0, 0, 0, false);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
    auto                     link = PhysicsLink::fromBody(*body);
    REQUIRE(link.ok());
    ForceLoad loads[]  = {{link.value(), {body->getMass() * 2, 0, 0}, {}, {0, 0, 1}}};
    auto      prepared = ForceBatch::prepare(world, loads);
    REQUIRE(prepared.ok());
    auto moved = std::move(prepared.value());
    CHECK(!prepared.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(!moved.commit(foreign, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    REQUIRE(moved.commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(std::abs(body->getLinearVelocityX() - .02) < 1e-5);
    CHECK(body->getAngularVelocityZ() > 0);
}

TEST_CASE("physics.forces.batch2DRevalidatesWholeBatchAtCommit") {
    World                    world(0, 0, true, 30);
    std::unique_ptr<Body>    a(world.newBody("dynamic", 0, 0)), b(world.newBody("dynamic", 90, 0));
    std::unique_ptr<Fixture> fa(a->newCircleFixture(30)), fb(b->newCircleFixture(30));
    auto                     la = PhysicsLink::fromBody(*a);
    REQUIRE(la.ok());
    auto lb = PhysicsLink::fromBody(*b);
    REQUIRE(lb.ok());
    ForceLoad loads[] = {{la.value(), {10, 0, 0}, {}, {}}, {lb.value(), {10, 0, 0}, {}, {}}};
    auto      batch   = ForceBatch::prepare(world, loads);
    REQUIRE(batch.ok());
    a->raw()->SetAwake(false);
    b->destroy();
    CHECK(!batch.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(!a->raw()->IsAwake());
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK_EQ(a->getLinearVelocityX(), 0);
    loads[0].force = {2e38, 0, 0};
    loads[1]       = loads[0];
    CHECK(!ForceBatch::prepare(world, loads).ok());
    CHECK(!a->raw()->IsAwake());
}

TEST_CASE("physics.forces.angularDampingBatchUsesActualInertia") {
    World                    planar(0, 0, false, 30);
    std::unique_ptr<Body>    b2(planar.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> f2(b2->newCircleFixture(30));
    b2->setAngularVelocity(4);
    const double i2 = b2->raw()->GetInertia();
    auto         t2 = ForceModels::angularDamping2D({10, 2}, 4, i2, .01);
    REQUIRE(t2.ok());
    auto l2 = PhysicsLink::fromBody(*b2);
    REQUIRE(l2.ok());
    ForceLoad load2{l2.value(), {}, {}, {0, 0, t2.value()}};
    auto      batch2 = ForceBatch::prepare(planar, std::span(&load2, 1));
    REQUIRE(batch2.ok());
    REQUIRE(batch2.value().commit(planar, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    REQUIRE(planar.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(std::abs(b2->getAngularVelocity() - (4 + t2.value() * .01 / i2)) < 1e-5);
    World3D                  spatial(0, 0, 0, false);
    std::unique_ptr<Body3D>  b3(spatial.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> s3(b3->newSphereShape(1));
    b3->setAngularDamping(0);
    b3->setAngularVelocity(1, -2, 3);
    const auto  tensor = b3Body_GetLocalRotationalInertia(eve::physics::box3d::Access::bodyId(*b3));
    ForceVector inertia{tensor.cx.x, tensor.cy.y, tensor.cz.z};
    auto        t3 = ForceModels::angularDamping3D({10, 2}, {1, -2, 3}, inertia, .01);
    REQUIRE(t3.ok());
    auto l3 = PhysicsLink::fromBody(*b3);
    REQUIRE(l3.ok());
    ForceLoad load3{l3.value(), {}, {}, t3.value()};
    auto      batch3 = ForceBatch::prepare(spatial, std::span(&load3, 1));
    REQUIRE(batch3.ok());
    REQUIRE(batch3.value().commit(spatial, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    REQUIRE(spatial.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(std::abs(b3->getAngularVelocityX() - (1 + t3.value().x * .01 / inertia.x)) < 1e-5);
    CHECK(std::abs(b3->getAngularVelocityY() - (-2 + t3.value().y * .01 / inertia.y)) < 1e-5);
    CHECK(std::abs(b3->getAngularVelocityZ() - (3 + t3.value().z * .01 / inertia.z)) < 1e-5);
}

TEST_CASE("physics.forces.tireImplicitImpulseMatchesRealChassis") {
    auto run = [&](auto& world, auto& body, double meter) {
        TireModel model;
        model.longitudinalStiffness = 1e8;
        model.rollingResistance     = 0;
        TireInput input;
        input.angularVelocity         = .1;
        input.normalLoad              = 1000;
        input.longitudinalInverseMass = 1 / body.getMass();

        double       energy   = .5 * model.inertia * input.angularVelocity * input.angularVelocity;
        const double momentum = model.inertia * input.angularVelocity / model.radius;
        for (int i = 0; i < 32; ++i) {
            input.longitudinalVelocity = body.getLinearVelocityX() / meter;
            auto sample                = ForceModels::tire(model, input, .01);
            REQUIRE(sample.ok());
            REQUIRE(applyForceSI(body, {sample.value().longitudinalForce, 0, 0},
                                 {body.getWorldCenterX() / meter, body.getWorldCenterY() / meter, 0})
                        .ok());
            REQUIRE(world
                        .step({eve::SimulationTick{static_cast<std::uint64_t>(i + 1)},
                               eve::Duration::fromNanoseconds(10000000)})
                        .ok());
            input.angularVelocity   = sample.value().angularVelocity;
            const double velocity   = body.getLinearVelocityX() / meter;
            const double nextEnergy = .5 * body.getMass() * velocity * velocity +
                                      .5 * model.inertia * input.angularVelocity * input.angularVelocity;
            CHECK(nextEnergy <= energy + 1e-8);
            energy = nextEnergy;
            CHECK(std::abs(body.getMass() * velocity + model.inertia * input.angularVelocity / model.radius -
                           momentum) < 1e-6);
        }
    };
    World                    w2(0, 0, false, 30);
    std::unique_ptr<Body>    b2(w2.newBody("dynamic", 0, 0));
    std::unique_ptr<Fixture> f2(b2->newCircleFixture(30));
    b2->raw()->SetLinearDamping(0);
    b2->raw()->SetAngularDamping(0);
    run(w2, *b2, 30);
    World3D                  w3(0, 0, 0, false);
    std::unique_ptr<Body3D>  b3(w3.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> s3(b3->newSphereShape(1));
    b3->setLinearDamping(0);
    b3->setAngularDamping(0);
    run(w3, *b3, 1);
}
