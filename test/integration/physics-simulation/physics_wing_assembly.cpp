#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/forces/WingAssembly.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;

TEST_CASE("physics.forces.wingAssembly2DUsesRotatedPointVelocity") {
    for (float meter : {10.f, 100.f}) {
        World                    world(0, 0, false, meter);
        std::unique_ptr<Body>    body(world.newBody("dynamic", 0, 0));
        std::unique_ptr<Fixture> fixture(body->newCircleFixture(meter));
        body->setAngle(std::numbers::pi_v<float> / 2);
        body->setLinearVelocity(-2 * meter, 20 * meter);
        body->setAngularVelocity(3);
        WingSurface surface;
        surface.center     = {0, 1, 0};
        surface.model.area = .1;
        auto assembly      = WingAssembly::build(std::span(&surface, 1));
        REQUIRE(assembly.ok());
        auto link = PhysicsLink::fromBody(*body);
        REQUIRE(link.ok());
        const double controls[] = {0};
        auto         prepared   = assembly.value().prepare(world, link.value(), controls, {});
        REQUIRE(prepared.ok());
        WingModel expected = surface.model;
        expected.chord     = {0, 1, 0};
        expected.normal    = {-1, 0, 0};
        // Rotated center is (-1,0), so omega cross r adds (0,-3) to center velocity.
        auto sample = ForceModels::wing(expected, {-2, 17, 0});
        REQUIRE(sample.ok());
        const auto   force = sample.value().force;
        const double mass = body->getMass(), inertia = body->raw()->GetInertia();
        REQUIRE(prepared.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(1000000)}).ok());
        REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(1000000)}).ok());
        CHECK(std::abs(body->getLinearVelocityX() / meter - (-2 + force.x * .001 / mass)) < 1e-5);
        CHECK(std::abs(body->getLinearVelocityY() / meter - (20 + force.y * .001 / mass)) < 1e-5);
        CHECK(std::abs(body->getAngularVelocity() - (3 - force.y * .001 / inertia)) < 1e-5);
    }
}
TEST_CASE("physics.forces.wingAssembly3DControlSurfacesAndWind") {
    for (int mode = 0; mode < 3; ++mode) {
        World3D                  world(0, 0, 0, false);
        std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
        body->setLinearDamping(0);
        body->setAngularDamping(0);
        body->setLinearVelocity(20, 0, 0);
        WingSurface wings[2];
        wings[0].center     = {0, 0, -1};
        wings[1].center     = {0, 0, 1};
        wings[0].model.area = .1;
        wings[1].model.area = .1;
        auto assembly       = WingAssembly::build(wings);
        REQUIRE(assembly.ok());
        auto link = PhysicsLink::fromBody(*body);
        REQUIRE(link.ok());
        const double controls[] = {.1, mode == 1 ? -.1 : .1};
        auto         batch =
            assembly.value().prepare(world, link.value(), controls, mode == 2 ? ForceVector{20, 0, 0} : ForceVector{});
        REQUIRE(batch.ok());
        REQUIRE(batch.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(1000000)}).ok());
        REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(1000000)}).ok());
        if (mode == 0) {
            CHECK(body->getLinearVelocityY() > 0);
            CHECK(std::abs(body->getAngularVelocityX()) < 1e-6);
            CHECK(body->getLinearVelocityX() < 20);
        } else if (mode == 1) {
            CHECK(std::abs(body->getLinearVelocityY()) < 1e-6);
            CHECK(body->getAngularVelocityX() > 0);
        } else {
            CHECK_EQ(body->getLinearVelocityX(), 20);
            CHECK_EQ(body->getLinearVelocityY(), 0);
            CHECK_EQ(body->getAngularVelocityX(), 0);
        }
    }
}
TEST_CASE("physics.forces.wingAssemblyFailureIsAtomic") {
    World3D                  world(0, 0, 0, true);
    std::unique_ptr<Body3D>  body(world.newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
    auto                     link = PhysicsLink::fromBody(*body);
    REQUIRE(link.ok());
    WingSurface wings[2];
    wings[1].center = {0, 0, 2};
    auto assembly   = WingAssembly::build(wings);
    REQUIRE(assembly.ok());
    b3Body_SetAwake(eve::physics::box3d::Access::bodyId(*body), false);
    double controls[] = {.1, std::numeric_limits<double>::quiet_NaN()};
    CHECK(!assembly.value().prepare(world, link.value(), controls, {-20, 0, 0}).ok());
    CHECK(!b3Body_IsAwake(eve::physics::box3d::Access::bodyId(*body)));
    controls[1] = 0;
    CHECK(!assembly.value().prepare(world, link.value(), std::span(controls, 1), {}).ok());
    auto valid = assembly.value().prepare(world, link.value(), controls, {-20, 0, 0});
    REQUIRE(valid.ok());
    body->destroy();
    CHECK(!valid.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(1000000)}).ok());
    wings[1].model.chord = {0, 0, 0};
    CHECK(!WingAssembly::build(wings).ok());
}
TEST_CASE("physics.forces.wingReverseFlowAndStallContinuity") {
    WingModel model;
    auto      reverse = ForceModels::wing(model, {-20, 0, 0});
    REQUIRE(reverse.ok());
    CHECK(reverse.value().force.x > 0);
    CHECK_EQ(reverse.value().liftCoefficient, 0);
    auto sample = [&](double angle) {
        return ForceModels::wing(model, {20 * std::cos(angle), -20 * std::sin(angle), 0});
    };
    auto below = sample(model.stallAngle - 1e-8);
    REQUIRE(below.ok());
    auto above = sample(model.stallAngle + 1e-8);
    REQUIRE(above.ok());
    CHECK(std::abs(below.value().force.x - above.value().force.x) < 1e-4);
    CHECK(std::abs(below.value().force.y - above.value().force.y) < 1e-4);
}
