#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Physics.h"
#include "physics/Body.h"
#include "physics/Joint2D.h"
#include "physics/Mechanism2D.h"
#include "physics/World.h"

#include <algorithm>
#include <cmath>
#include <memory>

using eve::physics::Body;
using eve::physics::Joint2D;
using eve::physics::Mechanism2D;
using eve::physics::Physics;
using eve::physics::World;

TEST_CASE("physics.joint2d.distanceRevoluteWeldMotorAndGearAreUsable") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);

    Body *anchor = world->newBody("static", 0.f, 0.f);
    Body *a = world->newBody("dynamic", 60.f, 0.f);
    a->newRectangleFixture(30.f, 30.f, 1.f, 0.3f, 0.1f);
    Body *b = world->newBody("dynamic", 0.f, 60.f);
    b->newRectangleFixture(24.f, 24.f, 1.f, 0.3f, 0.1f);
    Body *c = world->newBody("dynamic", 120.f, 0.f);
    c->newRectangleFixture(20.f, 20.f, 1.f, 0.3f, 0.1f);

    Joint2D *distance =
        world->newDistanceJoint(anchor, a, 0.f, 0.f, 60.f, 0.f, 60.f, false);
    REQUIRE(distance != nullptr);
    CHECK_EQ(distance->getKind(), std::string("distance"));
    distance->setDistanceSpring(4.f, 0.7f);
    CHECK(std::fabs(distance->getDistanceFrequency() - 4.f) < 0.001f);

    Joint2D *revolute = world->newRevoluteJoint(anchor, b, 0.f, 0.f, false);
    REQUIRE(revolute != nullptr);
    CHECK_EQ(revolute->getKind(), std::string("revolute"));
    revolute->setRevoluteMotor(true, 2.f, 50.f);

    Joint2D *weld = world->newWeldJoint(a, c, 90.f, 0.f, false);
    REQUIRE(weld != nullptr);
    CHECK_EQ(weld->getKind(), std::string("weld"));
    weld->setWeldSpring(0.f, 1.f);

    Joint2D *motor = world->newMotorJoint(anchor, c, false);
    REQUIRE(motor != nullptr);
    CHECK_EQ(motor->getKind(), std::string("motor"));
    motor->setMotorAngularOffset(0.5f);
    motor->setMotorLimits(1000.f, 50.f);
    CHECK(std::fabs(motor->getMotorAngularOffset() - 0.5f) < 0.001f);

    Joint2D *prismatic =
        world->newPrismaticJoint(anchor, c, 120.f, 0.f, 1.f, 0.f, false);
    REQUIRE(prismatic != nullptr);
    Joint2D *gear = world->newGearJoint(revolute, prismatic, 1.f);
    REQUIRE(gear != nullptr);
    CHECK_EQ(gear->getKind(), std::string("gear"));
    gear->setGearRatio(2.f);
    CHECK(std::fabs(gear->getGearRatio() - 2.f) < 0.001f);

    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 8, 3);
    CHECK_GT(std::fabs(revolute->getRevoluteAngle()), 0.2f);
    CHECK_THROWS((world->newPrismaticJoint(anchor, a, 0.f, 0.f, 0.f, 0.f, false), false));
    CHECK_THROWS((distance->setRevoluteMotor(true, 1.f, 1.f), false));
}

TEST_CASE("physics.mechanism2d.shaftDriveSpinsRotor") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);
    Body *support = world->newBody("static", 0.f, 0.f);
    Body *rotor = world->newBody("dynamic", 0.f, 0.f);
    rotor->newRectangleFixture(60.f, 12.f, 1.f, 0.3f, 0.1f);
    Mechanism2D *shaft = world->newShaft(support, rotor, 0.f, 0.f, false);
    REQUIRE(shaft != nullptr);
    CHECK_EQ(shaft->getKind(), std::string("shaft"));
    shaft->setDrive(4.f, 80.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 8, 3);
    CHECK_GT(std::fabs(shaft->getDriveAngle()), 1.f);
    CHECK_GT(std::fabs(shaft->getSpinSpeed()), 1.f);
    shaft->clearDrive();
    CHECK(!shaft->isDriveEnabled());
}

TEST_CASE("physics.mechanism2d.ratchetBlocksReverseSpin") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);
    Body *frame = world->newBody("static", 0.f, 0.f);
    Body *wheel = world->newBody("dynamic", 0.f, 0.f);
    wheel->newRectangleFixture(60.f, 12.f, 1.f, 0.3f, 0.1f);
    Mechanism2D *ratchet = world->newRatchet(frame, wheel, 0.f, 0.f, 1, 200.f, false);
    REQUIRE(ratchet != nullptr);
    CHECK_EQ(ratchet->getKind(), std::string("ratchet"));
    CHECK_EQ(ratchet->getRatchetDirection(), 1);

    ratchet->setDrive(3.f, 80.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 8, 3);
    const float forwardAngle = ratchet->getDriveAngle();
    CHECK_GT(forwardAngle, 0.5f);

    ratchet->clearDrive();
    ratchet->setRatchetEngagementTorque(500.f);
    wheel->setAngularVelocity(-20.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 8, 3);
    CHECK_GT(ratchet->getDriveAngle(), forwardAngle - 0.35f);
    CHECK_THROWS((world->newRatchet(frame, wheel, 0.f, 0.f, 0, 10.f, false), false));
}

TEST_CASE("physics.mechanism2d.crankSliderConvertsRotationToStroke") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, false));
    world->setMeter(30.f);

    Body *frame = world->newBody("static", 0.f, 0.f);
    Body *crank = world->newBody("dynamic", 15.f, 0.f);
    crank->newRectangleFixture(30.f, 6.f, 1.f, 0.3f, 0.1f);
    Body *rod = world->newBody("dynamic", 50.f, 0.f);
    rod->newRectangleFixture(40.f, 5.f, 0.5f, 0.3f, 0.1f);
    Body *slider = world->newBody("dynamic", 85.f, 0.f);
    slider->newRectangleFixture(18.f, 18.f, 1.f, 0.3f, 0.1f);

    Mechanism2D *cs = world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 30.f, 0.f, 70.f,
                                            0.f, 1.f, 0.f, false);
    REQUIRE(cs != nullptr);
    CHECK_EQ(cs->getKind(), std::string("crankSlider"));
    REQUIRE(cs->getSliderJoint() != nullptr);
    cs->setDrive(5.f, 120.f);

    float minT = 1e9f, maxT = -1e9f;
    for (int i = 0; i < 240; ++i) {
        world->updateFull(1.f / 60.f, 8, 3);
        const float t = cs->getSliderTranslation();
        minT = std::min(minT, t);
        maxT = std::max(maxT, t);
    }
    CHECK_GT(maxT - minT, 10.f);
    CHECK(std::fabs(slider->getY()) < 8.f);
    CHECK_THROWS((world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 30.f, 0.f, 70.f, 0.f,
                                        0.f, 0.f, false),
                  false));
}
