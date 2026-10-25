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

#include <Box2D/Box2D.h>
#include <cmath>
#include <memory>
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/World.h"
#include "vehicle/VehicleDefinitionRuntime.h"
#include "vehicle/VehicleMobility.h"
#include "vehicle/VehiclePhysics.h"
#include "vehicle/VehicleSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::vehicle;
namespace {
struct Rig {
    physics::World                    world;
    ecs::Table                        table;
    VehicleDefinition                 definition;
    VehicleEntity*                    vehicle;
    std::unique_ptr<physics::Body>    body;
    std::unique_ptr<physics::Fixture> fixture;
    explicit Rig(float meter = 30) : world(0, 0, false, meter) {
        definition.mobility        = "wheel";
        definition.radius          = .6f * meter;
        definition.maxSpeed        = 4 * meter;
        definition.accel           = (100.f / 30) * meter;
        vehicle                    = VehicleEntity::create(table);
        vehicle->definition()->def = &definition;
        REQUIRE(VehiclePhysics::attach2D(vehicle, &world) == VehiclePhysicsStatus::Applied);
        body.reset(world.findBody(physics::PhysicsBodyHandle(1u, 1u)));
        REQUIRE(body != nullptr);
        fixture.reset(static_cast<physics::Fixture*>(body->raw()->GetFixtureList()->GetUserData()));
        body->raw()->SetLinearDamping(0);
        body->raw()->SetAngularDamping(0);
    }
    ~Rig() {
        auto detached = VehiclePhysics::detach(vehicle);
        (void)detached;
        ecs::DestroyEntity(vehicle);
    }
    void step(std::uint64_t tick, std::int64_t ns = 10000000) {
        SimulationStep step{SimulationTick{tick}, Duration::fromNanoseconds(ns)};
        auto           submitted = VehiclePhysics::submitWheelStep(*vehicle, step);
        REQUIRE(submitted.ok());
        REQUIRE(submitted.value() == VehiclePhysicsStatus::Pending);
        REQUIRE(world.step(step).ok());
        auto synchronized = VehiclePhysics::synchronizeWheelState(*vehicle);
        REQUIRE(synchronized.ok());
        REQUIRE(synchronized.value() == VehiclePhysicsStatus::Applied);
    }
};
}  // namespace
TEST_CASE("physics.vehicleTires2D.forcesDriveAndSteerWithoutVelocitySetters") {
    Rig r;
    r.vehicle->input()->throttle = 1;
    auto queued = VehiclePhysics::submitWheelStep(*r.vehicle, {SimulationTick{1}, Duration::fromNanoseconds(10000000)});
    REQUIRE(queued.ok());
    CHECK(r.vehicle->suspension()->wheels.empty());
    CHECK_EQ(r.body->getLinearVelocityX(), 0);
    CHECK_EQ(r.body->getAngle(), 0);
    // The world consumes forces; only explicit synchronization publishes wheel state.
    REQUIRE(r.world.step({SimulationTick{1}, Duration::fromNanoseconds(10000000)}).ok());
    CHECK(r.vehicle->suspension()->wheels.empty());
    REQUIRE(VehiclePhysics::synchronizeWheelState(*r.vehicle).ok());
    REQUIRE_EQ(r.vehicle->suspension()->wheels.size(), 2u);
    CHECK(r.vehicle->suspension()->wheels[0].angularVelocity > 0);
    for (std::uint64_t i = 2; i <= 100; ++i) r.step(i);
    CHECK(r.body->getLinearVelocityX() > 60);
    CHECK(r.body->getX() > 30);
    r.vehicle->input()->steer = 1;
    const float angle         = r.body->getAngle();
    for (std::uint64_t i = 101; i <= 200; ++i) r.step(i);
    CHECK(r.body->getAngle() > angle + .1f);
    CHECK(r.body->getY() > 1);
}
TEST_CASE("physics.vehicleTires2D.pendingRetryCannotPublishOrDoubleApply") {
    Rig r, reference;
    r.vehicle->input()->throttle = reference.vehicle->input()->throttle = 1;
    const SimulationStep step{SimulationTick{1}, Duration::fromNanoseconds(10000000)};
    auto                 first = VehiclePhysics::submitWheelStep(*r.vehicle, step);
    REQUIRE(first.ok());
    for (int i = 0; i < 4; ++i) {
        auto duplicate = VehiclePhysics::submitWheelStep(*r.vehicle, step);
        REQUIRE(duplicate.ok());
        CHECK(duplicate.value() == VehiclePhysicsStatus::Pending);
        REQUIRE(VehiclePhysics::synchronizeWheelState(*r.vehicle).ok());
        CHECK(r.vehicle->suspension()->wheels.empty());
    }
    CHECK(!VehiclePhysics::submitWheelStep(*r.vehicle, {SimulationTick{2}, step.delta}).ok());
    CHECK(!r.world.step(step, {0, 3, 4}).ok());
    CHECK(r.vehicle->suspension()->wheels.empty());
    REQUIRE(r.world.step(step).ok());
    REQUIRE(VehiclePhysics::synchronizeWheelState(*r.vehicle).ok());
    reference.step(1);
    CHECK(std::abs(r.body->getLinearVelocityX() - reference.body->getLinearVelocityX()) < 1e-6);
    CHECK(std::abs(r.vehicle->suspension()->wheels[0].angularVelocity -
                   reference.vehicle->suspension()->wheels[0].angularVelocity) < 1e-9);
    auto completed = VehiclePhysics::submitWheelStep(*r.vehicle, step);
    REQUIRE(completed.ok());
    CHECK(completed.value() == VehiclePhysicsStatus::Applied);
    REQUIRE(r.world.step({SimulationTick{2}, step.delta}).ok());
    CHECK(std::abs(r.body->getLinearVelocityX() - reference.body->getLinearVelocityX()) < 1e-6);
}
TEST_CASE("physics.vehicleTires2D.brakeAndAirborneRespectContactCapacity") {
    Rig r;
    r.vehicle->input()->throttle = 1;
    for (std::uint64_t i = 1; i <= 100; ++i) r.step(i);
    const float moving           = r.body->getLinearVelocityX();
    r.vehicle->input()->throttle = 0;
    r.vehicle->input()->brake    = 1;
    for (std::uint64_t i = 101; i <= 140; ++i) {
        r.step(i);
        for (const auto& wheel : r.vehicle->suspension()->wheels) {
            CHECK(std::hypot(wheel.longitudinalForce, wheel.lateralForce) <=
                  wheel.normalLoad * r.definition.suspension.tireFriction + 1e-6);
            CHECK(std::abs(wheel.angularVelocity) < 1e-6);
        }
    }
    CHECK(std::abs(r.body->getLinearVelocityX()) < moving * .2);
    r.definition.suspension.supportAcceleration2D = 0;
    r.vehicle->input()->throttle                  = 1;
    r.vehicle->input()->brake                     = 0;
    const float velocity                          = r.body->getLinearVelocityX();
    for (std::uint64_t i = 141; i <= 160; ++i) r.step(i);
    CHECK(std::abs(r.body->getLinearVelocityX() - velocity) < 1e-6);
    CHECK(r.vehicle->suspension()->wheels[0].angularVelocity > 1);
    CHECK(!r.vehicle->suspension()->wheels[0].grounded);
}
TEST_CASE("physics.vehicleTires2D.invalidTailAndDestroyedBodyKeepCompletedState") {
    Rig r;
    r.vehicle->input()->throttle = 1;
    r.step(1);
    const double    angular = r.vehicle->suspension()->wheels[0].angularVelocity;
    SuspensionWheel a, b;
    a.x                            = .4f;
    b.x                            = -.4f;
    b.radius                       = -1;
    r.definition.suspension.wheels = {a, b};
    r.body->raw()->SetAwake(false);
    CHECK(!VehiclePhysics::submitWheelStep(*r.vehicle, {SimulationTick{2}, Duration::fromNanoseconds(10000000)}).ok());
    CHECK(!r.body->raw()->IsAwake());
    CHECK_EQ(r.vehicle->suspension()->wheels[0].angularVelocity, angular);
    r.definition.suspension.wheels.clear();
    REQUIRE(VehiclePhysics::submitWheelStep(*r.vehicle, {SimulationTick{2}, Duration::fromNanoseconds(10000000)}).ok());
    r.body->destroy();
    REQUIRE(r.world.step({SimulationTick{2}, Duration::fromNanoseconds(10000000)}).ok());
    auto cancelled = VehiclePhysics::synchronizeWheelState(*r.vehicle);
    REQUIRE(cancelled.ok());
    CHECK(cancelled.value() == VehiclePhysicsStatus::Cancelled);
    CHECK_EQ(r.vehicle->suspension()->wheels[0].angularVelocity, angular);
}
TEST_CASE("physics.vehicleTires2D.offCenterCombinedSlipDissipatesAtMultipleScales") {
    for (float meter : {10.f, 100.f}) {
        Rig r(meter);
        r.definition.suspension.longitudinalStiffness = 1e8;
        r.definition.suspension.corneringStiffness    = 1e8;
        r.definition.suspension.rollingResistance     = 0;
        SuspensionWheel a, b;
        a.x                            = .4f;
        a.y                            = .2f;
        b.x                            = -.4f;
        b.y                            = -.2f;
        r.definition.suspension.wheels = {a, b};
        r.vehicle->suspension()->wheels.resize(2);
        r.vehicle->suspension()->wheels[0].angularVelocity = .1;
        r.vehicle->suspension()->wheels[1].angularVelocity = -.05;
        r.vehicle->input()->steer                          = .7f;
        r.body->setLinearVelocity(.01f * meter, .02f * meter);
        r.body->setAngularVelocity(.03f);
        auto energy = [&] {
            const auto v     = r.body->raw()->GetLinearVelocity();
            double     total = r.body->getMass() * b2Dot(v, v) +
                           r.body->raw()->GetInertia() * std::pow(r.body->getAngularVelocity(), 2);
            for (const auto& wheel : r.vehicle->suspension()->wheels)
                total += r.definition.suspension.wheelInertia * wheel.angularVelocity * wheel.angularVelocity;
            return total;
        };
        double previous = energy();
        for (std::uint64_t i = 1; i <= 50; ++i) {
            r.step(i, 20000000);
            CHECK(energy() <= previous + 1e-9);
            previous = energy();
        }
    }
}
TEST_CASE("physics.vehicleTires2D.compatibilityMobilityQueuesInsteadOfTeleporting") {
    Rig r;
    r.vehicle->input()->throttle = 1;
    auto* mobility               = VehicleSystem::findMobility("wheel");
    REQUIRE(mobility != nullptr);
    mobility->update(*r.vehicle, .01f);
    mobility->update(*r.vehicle, .01f);
    CHECK_EQ(r.vehicle->motion()->x, 0);
    CHECK_EQ(r.vehicle->motion()->y, 0);
    CHECK_EQ(r.body->getLinearVelocityX(), 0);
    r.world.update(.01f);
    CHECK(r.body->getLinearVelocityX() > 0);
    CHECK(VehiclePhysics::tryWheelMove(*r.vehicle, .01f) == VehiclePhysicsStatus::Pending);
    CHECK(r.vehicle->suspension()->wheels[0].angularVelocity > 0);
    CHECK_EQ(r.vehicle->motion()->x, r.body->getX());
}

TEST_CASE("physics.vehicleTires.definitionCodecAdmitsAndRejectsTireParameters") {
    definitions::Definition source;
    source.id   = "tires";
    source.json = R"({"suspension":{"wheelInertia":0.2,"tireFriction":0.7,"longitudinalStiffness":4000,
        "corneringStiffness":3000,"rollingResistance":0.02,"brakeTorque":80,"maxSteerAngle":0.5,"supportAcceleration2D":8}})";
    auto parsed = parseVehicleDefinition(source);
    REQUIRE(parsed.ok());
    const auto& config = parsed.value().suspension;
    CHECK(std::abs(config.wheelInertia - .2) < 1e-6);
    CHECK(std::abs(config.tireFriction - .7) < 1e-6);
    CHECK_EQ(config.longitudinalStiffness, 4000);
    CHECK_EQ(config.corneringStiffness, 3000);
    CHECK(std::abs(config.rollingResistance - .02) < 1e-6);
    CHECK_EQ(config.brakeTorque, 80);
    CHECK_EQ(config.maxSteerAngle, .5f);
    CHECK_EQ(config.supportAcceleration2D, 8);
    for (const char* invalid : {R"({"suspension":{"wheelInertia":0}})", R"({"suspension":{"tireFriction":-1}})",
                                R"({"suspension":{"maxSteerAngle":2}})", R"({"suspension":{"brakeTorque":"heavy"}})"}) {
        source.json = invalid;
        CHECK(!parseVehicleDefinition(source).ok());
    }
    source.json   = "{}";
    auto defaults = parseVehicleDefinition(source);
    REQUIRE(defaults.ok());
    CHECK(defaults.value().suspension.wheelInertia > 0);
}
