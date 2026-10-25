// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "module": "fluids"
}
*/
// clang-format on

#include "common/CameraObstruction.h"
#include "common/ProcgenWorldQuery.h"
#include "fluids/OceanBoat.h"
#include "fluids/OceanWake.h"
#include "physics/Body3D.h"
#include "physics/PhysicsWorldQueries.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::fluids;
namespace {
eve::Result<void> advanceWake(OceanWake& wake, double dt, double endTime, const OceanSpectrumFrame& frame) {
    auto prepared = wake.prepareStep(dt, endTime, frame);
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    return wake.commitStep(*prepared.value());
}
}  // namespace

namespace {
OceanHullSettings smallHull() {
    OceanHullSettings hull;
    hull.thrust    = 150.0;
    hull.propeller = {0.0, -0.3, -1.2};
    for (double x : {-0.4, 0.4})
        for (double z : {-1.0, 1.0}) hull.cells.push_back({{x, 0.0, z}, 0.5, 0.15});
    return hull;
}
OceanSpectrum calmSea() {
    OceanSpectrumBuilder builder;
    builder.settings.significantWaveHeight = 0.0;
    builder.settings.bands                 = {{8, 32.0, 8.0, 32.0}};
    return builder.build().expect("valid calm sea");
}
}  // namespace

TEST_CASE("fluids.ocean.coreWorldQueriesSurviveSplit") {
    auto  world    = std::make_unique<eve::physics::World3D>(0.0F, 0.0F, 0.0F, false);
    auto* obstacle = world->newBody("static", 0.0F, 0.0F, 0.0F);
    REQUIRE(obstacle != nullptr);
    REQUIRE(obstacle->newBoxShape(2.0F, 2.0F, 2.0F) != nullptr);
    eve::CameraObstructionHit hit;
    CHECK(eve::physics::cameraObstructionQuery().sphereCast(0, 10, 0, 0, -10, 0, 0.1F, ~uint64_t(0), -1, &hit));
    CHECK(hit.hit);
    auto projection = eve::physics::procgenWorldQuery().projectDown(0, 0, 10, -10, ~uint64_t(0));
    REQUIRE(projection.ok());
    CHECK(projection.value().hit);
    world.reset();
    CHECK(!eve::physics::cameraObstructionQuery().sphereCast(0, 10, 0, 0, -10, 0, 0.1F, ~uint64_t(0), -1, &hit));
    auto stale = eve::physics::procgenWorldQuery().projectDown(0, 0, 10, -10, ~uint64_t(0));
    REQUIRE(stale.ok());
    CHECK(!stale.value().hit);
}

TEST_CASE("fluids.ocean.boatFloatsAndPropelsRealRigidBody") {
    eve::physics::World3D world(0.0F, -9.81F, 0.0F, false);
    auto*                 body = world.newBody("dynamic", 0.0F, 0.0F, 0.0F);
    REQUIRE(body != nullptr);
    REQUIRE(body->newBoxShape(1.0F, 1.0F, 3.0F, 102.5F) != nullptr);
    auto boat = OceanBoat::create(*body, smallHull());
    REQUIRE(boat.ok());
    const auto       sea = calmSea();
    constexpr double dt  = 1.0 / 60.0;
    for (int tick = 0; tick < 180; ++tick) {
        auto frame = sea.frameBuilder(tick * dt).build();
        REQUIRE(frame.ok());
        // A rejected control command must not consume the frame or submit any forces.
        auto invalid = boat.value()->apply(frame.value(), dt, 2.0, 0.0);
        CHECK(!invalid.ok());
        auto applied = boat.value()->apply(frame.value(), dt, 0.0, 0.0);
        REQUIRE(applied.ok());
        CHECK(applied.value().contacts.size() == 4);
        world.update(float(dt));
    }
    CHECK(std::abs(body->getY()) < 0.03F);
    CHECK(std::abs(body->getLinearVelocityY()) < 0.03F);
    for (int tick = 180; tick < 360; ++tick) {
        auto frame = sea.frameBuilder(tick * dt).build();
        REQUIRE(frame.ok());
        auto applied = boat.value()->apply(frame.value(), dt, 1.0, 0.0);
        REQUIRE(applied.ok());
        world.update(float(dt));
    }
    CHECK(body->getZ() > 0.2F);
    CHECK(body->getLinearVelocityZ() > 0.1F);
    CHECK(std::abs(body->getY()) < 0.5F);
    const double startingYaw = body->getRotY();
    for (int tick = 360; tick < 540; ++tick) {
        auto frame = sea.frameBuilder(tick * dt).build();
        REQUIRE(frame.ok());
        auto applied = boat.value()->apply(frame.value(), dt, 1.0, 0.7);
        REQUIRE(applied.ok());
        world.update(float(dt));
    }
    CHECK(std::abs(double(body->getRotY()) - startingYaw) > 0.01);
}

TEST_CASE("fluids.ocean.boatRejectsRepeatedStepAndStaleOwners") {
    auto  world = std::make_unique<eve::physics::World3D>(0.0F, -9.81F, 0.0F, false);
    auto* body  = world->newBody("dynamic", 0.0F, 0.0F, 0.0F);
    REQUIRE(body != nullptr);
    REQUIRE(body->newBoxShape(1.0F, 1.0F, 3.0F, 102.5F) != nullptr);
    auto boat = OceanBoat::create(*body, smallHull());
    REQUIRE(boat.ok());
    const auto sea   = calmSea();
    auto       frame = sea.frameBuilder(0.0).build();
    REQUIRE(frame.ok());
    auto once = boat.value()->apply(frame.value(), 1.0 / 60.0, 0.0, 0.0);
    REQUIRE(once.ok());
    auto twice = boat.value()->apply(frame.value(), 1.0 / 60.0, 0.0, 0.0);
    CHECK(!twice.ok());
    auto next = sea.frameBuilder(1.0 / 60.0).build();
    REQUIRE(next.ok());
    world->destroyBody(body);
    auto staleBody = boat.value()->apply(next.value(), 1.0 / 60.0, 0.0, 0.0);
    CHECK(!staleBody.ok());
    world.reset();
    auto staleWorld = boat.value()->apply(next.value(), 1.0 / 60.0, 0.0, 0.0);
    CHECK(!staleWorld.ok());
}

TEST_CASE("fluids.ocean.dryPropellerAndAdapterFirstDestruction") {
    eve::physics::World3D world(0.0F, 0.0F, 0.0F, false);
    auto*                 body = world.newBody("dynamic", 0.0F, 10.0F, 0.0F);
    REQUIRE(body != nullptr);
    REQUIRE(body->newBoxShape(1.0F, 1.0F, 3.0F, 102.5F) != nullptr);
    auto boat = OceanBoat::create(*body, smallHull());
    REQUIRE(boat.ok());
    const auto sea   = calmSea();
    auto       frame = sea.frameBuilder(0.0).build();
    REQUIRE(frame.ok());
    auto dry = boat.value()->apply(frame.value(), 1.0 / 60.0, 1.0, 1.0);
    REQUIRE(dry.ok());
    CHECK(glm::length(dry.value().totalForce) == 0.0);
    boat.value().reset();
    CHECK(body->isValid());
    world.update(1.0F / 60.0F);
    CHECK(body->getLinearVelocityZ() == 0.0F);
}

TEST_CASE("fluids.ocean.boatRespondsToSpectralWaves") {
    eve::physics::World3D world(0.0F, -9.81F, 0.0F, false);
    auto*                 body = world.newBody("dynamic", 0.0F, 0.0F, 0.0F);
    REQUIRE(body != nullptr);
    REQUIRE(body->newBoxShape(1.0F, 1.0F, 3.0F, 102.5F) != nullptr);
    auto boat = OceanBoat::create(*body, smallHull());
    REQUIRE(boat.ok());
    OceanSpectrumBuilder builder;
    builder.settings.bands                 = {{16, 64.0, 8.0, 64.0}};
    builder.settings.significantWaveHeight = 0.6;
    builder.settings.windDirection         = {0.3, 1.0};
    auto sea                               = builder.build();
    REQUIRE(sea.ok());
    double maximumHeight = 0.0, maximumTilt = 0.0;
    for (int tick = 0; tick < 300; ++tick) {
        auto frame = sea.value().frameBuilder(tick / 60.0).build();
        REQUIRE(frame.ok());
        auto force = boat.value()->apply(frame.value(), 1.0 / 60.0, 0.0, 0.0);
        REQUIRE(force.ok());
        world.update(1.0F / 60.0F);
        maximumHeight = std::max(maximumHeight, std::abs(double(body->getY())));
        maximumTilt   = std::max(maximumTilt, std::hypot(double(body->getRotX()), double(body->getRotZ())));
        REQUIRE(std::isfinite(body->getY()));
    }
    CHECK(maximumHeight > 0.03);
    CHECK(maximumHeight < 2.0);
    CHECK(maximumTilt > 0.005);
    CHECK(maximumTilt < 0.8);
}

TEST_CASE("fluids.ocean.boatWakeChangesSecondRigidBody") {
    eve::physics::World3D world(0.0F, -9.81F, 0.0F, false);
    eve::physics::World3D controlWorld(0.0F, -9.81F, 0.0F, false);
    auto*                 source   = world.newBody("dynamic", 0.0F, 0.0F, -2.0F);
    auto*                 follower = world.newBody("dynamic", 2.5F, 0.0F, 0.0F);
    auto*                 control  = controlWorld.newBody("dynamic", 2.5F, 0.0F, 0.0F);
    REQUIRE(source != nullptr);
    REQUIRE(follower != nullptr);
    REQUIRE(control != nullptr);
    for (auto* body : {source, follower, control}) REQUIRE(body->newBoxShape(1.0F, 1.0F, 3.0F, 102.5F) != nullptr);
    source->setLinearVelocity(0.0F, 0.0F, 2.0F);
    auto sourceBoat   = OceanBoat::create(*source, smallHull());
    auto followerBoat = OceanBoat::create(*follower, smallHull());
    auto controlBoat  = OceanBoat::create(*control, smallHull());
    REQUIRE(sourceBoat.ok());
    REQUIRE(followerBoat.ok());
    REQUIRE(controlBoat.ok());
    OceanWakeSettings settings;
    settings.resolution      = 32;
    settings.length          = 32.0;
    settings.absorptionWidth = 4.0;
    auto wake                = OceanWake::create(settings);
    REQUIRE(wake.ok());
    const auto sea                    = calmSea();
    double     maximumResponse        = 0.0;
    double     maximumForceDifference = 0.0;
    for (int tick = 0; tick < 180; ++tick) {
        auto frame = sea.frameBuilder(tick / 60.0).build();
        REQUIRE(frame.ok());
        OceanWaterView water(frame.value(), *wake.value());
        auto           sourceReceipt = sourceBoat.value()->apply(water, 1.0 / 60.0, 1.0, 0.0);
        REQUIRE(sourceReceipt.ok());
        auto followerReceipt = followerBoat.value()->apply(water, 1.0 / 60.0, 0.0, 0.0);
        REQUIRE(followerReceipt.ok());
        auto controlReceipt = controlBoat.value()->apply(frame.value(), 1.0 / 60.0, 0.0, 0.0);
        REQUIRE(controlReceipt.ok());
        maximumForceDifference = std::max(maximumForceDifference, std::abs(followerReceipt.value().totalForce.y -
                                                                           controlReceipt.value().totalForce.y));
        auto injection         = injectOceanBoatWake(*wake.value(), sourceReceipt.value(), 1.0 / 60.0);
        REQUIRE(injection.ok());
        world.update(1.0F / 60.0F);
        controlWorld.update(1.0F / 60.0F);
        auto advanced = advanceWake(*wake.value(), 1.0 / 60.0, (tick + 1) / 60.0, frame.value());
        REQUIRE(advanced.ok());
        maximumResponse = std::max(maximumResponse, std::abs(double(follower->getY()) - control->getY()));
    }
    CHECK(maximumForceDifference > 0.1);
    CHECK(maximumResponse > 1e-4);
    CHECK(maximumResponse < 0.5);
}
