// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "module": "physics"
}
*/
// clang-format on

#include <box3d/box3d.h>
#include <cmath>
#include <memory>
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehiclePhysics.h"
#include "vehicle/VehicleSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::vehicle;
namespace {
struct Rig {
    physics::World3D                  world;
    ecs::Table                        table;
    VehicleDefinition                 definition;
    VehicleEntity*                    vehicle;
    std::unique_ptr<physics::Body3D>  ground, body;
    std::unique_ptr<physics::Shape3D> floor, chassis;
    explicit Rig(float gravity = 0, bool dynamicGround = false, float height = .435f) : world(0, gravity, 0, false) {
        ground.reset(world.newBody(dynamicGround ? "dynamic" : "static", 0, -.1f, 0));
        floor.reset(ground->newBoxShape(40, .2f, 40, 1, .8f, 0));
        definition.mobility                     = "suspension";
        definition.radius                       = 1;
        definition.maxSpeed                     = 16;
        definition.accel                        = 8;
        definition.suspension.rollingResistance = 0;
        for (int x : {-1, 1})
            for (int z : {-1, 1}) {
                SuspensionWheel wheel;
                wheel.x         = float(x) * .4f;
                wheel.y         = -.175f;
                wheel.z         = float(z) * .6f;
                wheel.stiffness = 1000;
                wheel.damping   = 20;
                wheel.steer     = z > 0;
                definition.suspension.wheels.push_back(wheel);
            }
        vehicle                    = VehicleEntity::create(table);
        vehicle->definition()->def = &definition;
        REQUIRE(VehiclePhysics::attach3D(vehicle, &world, height) == VehiclePhysicsStatus::Applied);
        body.reset(world.findBodyById(2));
        REQUIRE(body != nullptr);
        b3ShapeId shape{};
        REQUIRE_EQ(b3Body_GetShapes(eve::physics::box3d::Access::bodyId(*body), &shape, 1), 1);
        chassis.reset(static_cast<physics::Shape3D*>(b3Shape_GetUserData(shape)));
        body->setLinearDamping(0);
        body->setAngularDamping(0);
        ground->setLinearDamping(0);
        ground->setAngularDamping(0);
    }
    ~Rig() {
        auto detached = VehiclePhysics::detach(vehicle);
        (void)detached;
        ecs::DestroyEntity(vehicle);
    }
    void step(std::uint64_t tick, std::int64_t ns = 10000000) {
        SimulationStep step{SimulationTick{tick}, Duration::fromNanoseconds(ns)};
        auto           submitted = VehiclePhysics::submitWheelStep(*vehicle, step);
        REQUIRE(submitted.ok(), "{}", submitted.status().describe());
        REQUIRE(submitted.value() == VehiclePhysicsStatus::Pending);
        REQUIRE(world.step(step).ok());
        auto synchronized = VehiclePhysics::synchronizeWheelState(*vehicle);
        REQUIRE(synchronized.ok());
        REQUIRE(synchronized.value() == VehiclePhysicsStatus::Applied);
    }
};
}  // namespace
TEST_CASE("physics.vehicleTires3D.suspensionMobilityDrivesAndSteersByForces") {
    Rig   r(-9.81f, false, 1.5f);
    auto* mobility = VehicleSystem::findMobility("suspension");
    REQUIRE(mobility != nullptr);
    r.vehicle->input()->throttle = .3f;
    mobility->update(*r.vehicle, .01f);
    mobility->update(*r.vehicle, .01f);
    CHECK_EQ(r.body->getLinearVelocityZ(), 0);
    CHECK_EQ(r.vehicle->motion()->y, 0);
    r.world.update(.01f);
    for (int i = 1; i < 200; ++i) {
        mobility->update(*r.vehicle, .01f);
        r.world.update(.01f);
    }
    REQUIRE(VehiclePhysics::synchronizeWheelState(*r.vehicle).ok());
    CHECK(r.body->getZ() > 1);
    CHECK(r.body->getY() > .2f);
    CHECK(r.body->getY() < .7f);
    int grounded = 0;
    for (const auto& wheel : r.vehicle->suspension()->wheels) {
        grounded += wheel.grounded ? 1 : 0;
        CHECK(std::hypot(wheel.longitudinalForce, wheel.lateralForce) <= .8 * wheel.normalLoad + 1e-5);
    }
    CHECK(grounded >= 2);
    r.vehicle->input()->steer = .25f;
    for (std::uint64_t i = 201; i <= 250; ++i) r.step(i);
    CHECK(r.body->getRotY() > .01f);
    CHECK(r.body->getX() > 0);
}
TEST_CASE("physics.vehicleTires3D.dynamicGroundReceivesOppositeImpulse") {
    Rig r(0, true);
    r.vehicle->input()->throttle = 1;
    const double m = r.body->getMass(), g = r.ground->getMass();
    r.step(1, 5000000);
    CHECK(r.body->getLinearVelocityZ() > 0);
    CHECK(r.ground->getLinearVelocityZ() < 0);
    CHECK(r.body->getLinearVelocityY() > 0);
    CHECK(r.ground->getLinearVelocityY() < 0);
    CHECK(std::abs(m * r.body->getLinearVelocityX() + g * r.ground->getLinearVelocityX()) < 1e-5);
    CHECK(std::abs(m * r.body->getLinearVelocityY() + g * r.ground->getLinearVelocityY()) < 1e-5);
    CHECK(std::abs(m * r.body->getLinearVelocityZ() + g * r.ground->getLinearVelocityZ()) < 1e-5);
}
TEST_CASE("physics.vehicleTires3D.relativePointVelocityIncludesMovingGroundSpin") {
    Rig r(0, true);
    // Match one rigid velocity field at the actual centers of mass, not two unrelated COM velocities.
    r.body->setLinearVelocity(r.body->getWorldCenterZ(), 0, -r.body->getWorldCenterX());
    r.ground->setLinearVelocity(r.ground->getWorldCenterZ(), 0, -r.ground->getWorldCenterX());
    r.body->setAngularVelocity(0, 1, 0);
    r.ground->setAngularVelocity(0, 1, 0);
    r.step(1);
    for (const auto& wheel : r.vehicle->suspension()->wheels) {
        CHECK(std::abs(wheel.longitudinalForce) < 1e-6);
        CHECK(std::abs(wheel.lateralForce) < 1e-6);
        CHECK(std::abs(wheel.angularVelocity) < 1e-9);
    }
}
TEST_CASE("physics.vehicleTires3D.fullAttitudeAndContactPlaneRotateCovariantly") {
    Rig         flat, slope;
    const float angle = .35f, c = std::cos(angle), s = std::sin(angle);
    slope.body->setPosition(-.435f * s, .435f * c, 0);
    slope.ground->setPosition(.1f * s, -.1f * c, 0);
    slope.body->setRotation(0, 0, std::sin(angle / 2), std::cos(angle / 2));
    slope.ground->setRotation(0, 0, std::sin(angle / 2), std::cos(angle / 2));
    flat.vehicle->input()->throttle = slope.vehicle->input()->throttle = .5f;
    flat.step(1, 5000000);
    slope.step(1, 5000000);
    CHECK(slope.body->getLinearVelocityX() < 0);
    CHECK(std::abs(slope.body->getLinearVelocityX() -
                   (c * flat.body->getLinearVelocityX() - s * flat.body->getLinearVelocityY())) < 1e-5);
    CHECK(std::abs(slope.body->getLinearVelocityY() -
                   (s * flat.body->getLinearVelocityX() + c * flat.body->getLinearVelocityY())) < 1e-5);
    CHECK(std::abs(slope.body->getLinearVelocityZ() - flat.body->getLinearVelocityZ()) < 1e-5);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(std::abs(slope.vehicle->suspension()->wheels[i].prevCompression -
                       flat.vehicle->suspension()->wheels[i].prevCompression) < 1e-6);
        CHECK(std::abs(slope.vehicle->suspension()->wheels[i].angularVelocity -
                       flat.vehicle->suspension()->wheels[i].angularVelocity) < 1e-5);
    }
}
TEST_CASE("physics.vehicleTires3D.sensorFilteringDoesNotMutateWorldQueryState") {
    Rig                               r, reference;
    std::unique_ptr<physics::Body3D>  sensor(r.world.newBody("static", 0, .13f, 0));
    std::unique_ptr<physics::Shape3D> shape(sensor->newBoxShape(40, .01f, 40));
    shape->setSensor(true);
    r.world.setQueryFilter(7, 8);
    r.world.setQueryIgnoredBodyId(99);
    r.step(1);
    reference.step(1);
    CHECK_EQ(r.world.getQueryCategoryBits(), 7);
    CHECK_EQ(r.world.getQueryMaskBits(), 8);
    CHECK_EQ(r.world.getQueryIgnoredBodyId(), 99);
    CHECK(std::abs(r.body->getLinearVelocityY() - reference.body->getLinearVelocityY()) < 1e-6);
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(std::abs(r.vehicle->suspension()->wheels[i].prevCompression -
                       reference.vehicle->suspension()->wheels[i].prevCompression) < 1e-6);
}
TEST_CASE("physics.vehicleTires3D.airborneAndZeroFrictionCannotDriveChassis") {
    Rig air(0, false, 3), ice;
    air.vehicle->input()->throttle = ice.vehicle->input()->throttle = 1;
    ice.floor->setFriction(0);
    air.step(1);
    ice.step(1);
    CHECK_EQ(air.body->getLinearVelocityZ(), 0);
    CHECK_EQ(ice.body->getLinearVelocityZ(), 0);
    for (const auto& wheel : air.vehicle->suspension()->wheels) {
        CHECK(!wheel.grounded);
        CHECK_EQ(wheel.normalLoad, 0);
        CHECK(wheel.angularVelocity > 0);
    }
    for (const auto& wheel : ice.vehicle->suspension()->wheels) {
        CHECK(wheel.grounded);
        CHECK_EQ(wheel.longitudinalForce, 0);
        CHECK(wheel.angularVelocity > 0);
    }
}
TEST_CASE("physics.vehicleTires3D.rejectedStepAndInvalidWheelAreAtomic") {
    Rig r, reference;
    r.vehicle->input()->throttle = reference.vehicle->input()->throttle = 1;
    const SimulationStep step{SimulationTick{1}, Duration::fromNanoseconds(10000000)};
    r.definition.suspension.wheels.back().damping = -1;
    r.body->setAwake(false);
    CHECK(!VehiclePhysics::submitWheelStep(*r.vehicle, step).ok());
    CHECK(!r.body->isAwake());
    CHECK_EQ(r.vehicle->suspension()->wheels[0].angularVelocity, 0);
    r.definition.suspension.wheels.back().damping = 20;
    REQUIRE(VehiclePhysics::submitWheelStep(*r.vehicle, step).ok());
    REQUIRE(VehiclePhysics::submitWheelStep(*r.vehicle, step).ok());
    CHECK(!r.world.step(step, {8, 3, 0}).ok());
    auto pending = VehiclePhysics::synchronizeWheelState(*r.vehicle);
    REQUIRE(pending.ok());
    CHECK(pending.value() == VehiclePhysicsStatus::Pending);
    CHECK_EQ(r.vehicle->suspension()->wheels[0].angularVelocity, 0);
    REQUIRE(r.world.step(step).ok());
    REQUIRE(VehiclePhysics::synchronizeWheelState(*r.vehicle).ok());
    reference.step(1);
    CHECK(std::abs(r.body->getLinearVelocityZ() - reference.body->getLinearVelocityZ()) < 1e-6);
    CHECK(std::abs(r.vehicle->suspension()->wheels[0].angularVelocity -
                   reference.vehicle->suspension()->wheels[0].angularVelocity) < 1e-8);
}
TEST_CASE("physics.vehicleTires3D.brakesHoldOnSlopeAndStopForwardMotion") {
    Rig moving(-9.81f, false, .475f);
    moving.vehicle->input()->throttle = .3f;
    for (std::uint64_t i = 1; i <= 150; ++i) moving.step(i);
    const float before = moving.body->getLinearVelocityZ();
    REQUIRE(before > 1);
    moving.vehicle->input()->throttle = 0;
    moving.vehicle->input()->brake    = 1;
    for (std::uint64_t i = 151; i <= 250; ++i) moving.step(i);
    CHECK(std::abs(moving.body->getLinearVelocityZ()) < before * .1);
    for (const auto& wheel : moving.vehicle->suspension()->wheels) CHECK_EQ(wheel.angularVelocity, 0);
    Rig         slope(-9.81f, false, .474f);
    const float angle = .25f, c = std::cos(angle), s = std::sin(angle);
    slope.body->setPosition(-.474f * s, .474f * c, 0);
    slope.ground->setPosition(.1f * s, -.1f * c, 0);
    slope.body->setRotation(0, 0, std::sin(angle / 2), std::cos(angle / 2));
    slope.ground->setRotation(0, 0, std::sin(angle / 2), std::cos(angle / 2));
    slope.vehicle->input()->brake = 1;
    const float initial           = slope.body->getX();
    for (std::uint64_t i = 1; i <= 200; ++i) slope.step(i);
    CHECK(std::abs(slope.body->getX() - initial) < .08f);
    CHECK(std::abs(slope.body->getLinearVelocityX()) < .03f);
}
TEST_CASE("physics.vehicleTires3D.conveyorVelocityUsesShapeAndBodyRotation") {
    Rig         r;
    const float halfAngle = .3f;
    r.ground->setRotation(0, std::sin(halfAngle), 0, std::cos(halfAngle));
    r.floor->setLocalRotation(0, std::sin(halfAngle), 0, std::cos(halfAngle));
    r.floor->setTangentVelocity(0, 0, 2);
    r.step(1);
    CHECK(r.body->getLinearVelocityX() > 0);
    CHECK(r.body->getLinearVelocityZ() > 0);
    for (const auto& wheel : r.vehicle->suspension()->wheels) CHECK(wheel.normalLoad > 0);
}
TEST_CASE("physics.vehicleTires3D.meshTriangleFrictionOverridesBaseMaterial") {
    Rig r;
    r.floor.reset();
    // Upward winding; source coordinates offset body Y=-0.1 to put the surface at zero.
    r.floor.reset(
        r.ground->newTriangleMeshShape({-10, .1f, -10, -10, .1f, 10, 10, .1f, 10, 10, .1f, -10}, {0, 1, 2, 0, 2, 3}));
    r.floor->setFriction(1);
    r.floor->setTriangleMeshMaterialIndices({0, 0});
    r.floor->setTriangleMeshMaterial(0, 0, 0, 0, 0, 0, 0, 0, "average", "average");
    r.vehicle->input()->throttle = 1;
    r.step(1);
    CHECK_EQ(r.body->getLinearVelocityZ(), 0);
    for (const auto& wheel : r.vehicle->suspension()->wheels) {
        CHECK(wheel.grounded);
        CHECK_EQ(wheel.longitudinalForce, 0);
        CHECK(wheel.angularVelocity > 0);
    }
}
