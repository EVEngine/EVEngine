#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Physics.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Mechanism3D.h"
#include "physics/World3D.h"

#include <cmath>
#include <memory>

using eve::physics::Body3D;
using eve::physics::Joint3D;
using eve::physics::Mechanism3D;
using eve::physics::Physics;
using eve::physics::World3D;

TEST_CASE("physics.joint.weldMotorParallelAndFilterAreUsable") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *a = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *b = world->newBody("dynamic", 0.5f, 0.f, 0.f);
    b->newBoxShape(0.5f, 0.5f, 0.5f, 1.f);
    Body3D *c = world->newBody("dynamic", 0.f, 1.f, 0.f);
    c->newBoxShape(0.4f, 0.4f, 0.4f, 1.f);

    Joint3D *weld = world->newWeldJoint(a, b, 0.25f, 0.f, 0.f, false);
    REQUIRE(weld != nullptr);
    CHECK_EQ(weld->getKind(), std::string("weld"));
    weld->setWeldLinearSpring(0.f, 1.f);
    weld->setWeldAngularSpring(0.f, 1.f);
    CHECK(std::fabs(weld->getWeldLinearHertz()) < 0.001f);

    Joint3D *motor = world->newMotorJoint(a, c, false);
    REQUIRE(motor != nullptr);
    CHECK_EQ(motor->getKind(), std::string("motor"));
    motor->setMotorAngularVelocity(0.f, 0.f, 2.f);
    motor->setMotorVelocityLimits(0.f, 50.f);
    CHECK(std::fabs(motor->getMotorAngularVelocityZ() - 2.f) < 0.001f);

    Joint3D *parallel = world->newParallelJoint(a, c, 0.f, 1.f, 0.f, false);
    REQUIRE(parallel != nullptr);
    CHECK_EQ(parallel->getKind(), std::string("parallel"));
    parallel->setParallelSpring(8.f, 0.7f, 100.f);
    CHECK(std::fabs(parallel->getParallelHertz() - 8.f) < 0.001f);

    Joint3D *filter = world->newFilterJoint(b, c);
    REQUIRE(filter != nullptr);
    CHECK_EQ(filter->getKind(), std::string("filter"));

    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK(std::fabs(b->getY()) < 0.2f);
    CHECK_THROWS((world->newParallelJoint(a, c, 0.f, 0.f, 0.f, false), false));
    CHECK_THROWS((weld->setMotorLinearVelocity(1.f, 0.f, 0.f), false));
}

TEST_CASE("physics.mechanism.shaftDriveSpinsRotor") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *support = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *rotor = world->newBody("dynamic", 0.f, 0.f, 0.f);
    rotor->newBoxShape(1.f, 0.2f, 0.2f, 1.f);
    Mechanism3D *shaft = world->newShaft(support, rotor, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(shaft != nullptr);
    CHECK_EQ(shaft->getKind(), std::string("shaft"));
    shaft->setDrive(4.f, 80.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(std::fabs(shaft->getDriveAngle()), 1.f);
    CHECK_GT(std::fabs(shaft->getSpinSpeed()), 1.f);
    shaft->clearDrive();
    CHECK(!shaft->isDriveEnabled());
}

TEST_CASE("physics.mechanism.ratchetBlocksReverseSpin") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *frame = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *wheel = world->newBody("dynamic", 0.f, 0.f, 0.f);
    wheel->newBoxShape(1.f, 0.2f, 0.2f, 1.f);
    Mechanism3D *ratchet =
        world->newRatchet(frame, wheel, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1, 200.f, false);
    REQUIRE(ratchet != nullptr);
    CHECK_EQ(ratchet->getKind(), std::string("ratchet"));
    CHECK_EQ(ratchet->getRatchetDirection(), 1);

    // Freewheel forward under drive.
    ratchet->setDrive(3.f, 80.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 4);
    const float forwardAngle = ratchet->getDriveAngle();
    CHECK_GT(forwardAngle, 0.5f);

    // Attempt reverse: engagement should hold near the freewheel peak.
    ratchet->clearDrive();
    ratchet->setRatchetEngagementTorque(500.f);
    wheel->setAngularVelocity(0.f, 0.f, -20.f);
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(ratchet->getDriveAngle(), forwardAngle - 0.35f);
    CHECK_THROWS((world->newRatchet(frame, wheel, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0, 10.f, false),
                  false));
}

TEST_CASE("physics.mechanism.crankSliderConvertsRotationToStroke") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *frame = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *crank = world->newBody("dynamic", 0.25f, 0.f, 0.f);
    crank->newBoxShape(0.5f, 0.1f, 0.1f, 1.f);
    Body3D *rod = world->newBody("dynamic", 0.85f, 0.f, 0.f);
    rod->newBoxShape(0.7f, 0.08f, 0.08f, 0.5f);
    Body3D *slider = world->newBody("dynamic", 1.4f, 0.f, 0.f);
    slider->newBoxShape(0.3f, 0.3f, 0.3f, 1.f);

    Mechanism3D *cs = world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f,
                                            0.5f, 0.f, 0.f, 1.2f, 0.f, 0.f, 1.f, 0.f, 0.f, false);
    REQUIRE(cs != nullptr);
    CHECK_EQ(cs->getKind(), std::string("crankSlider"));
    REQUIRE(cs->getSliderJoint() != nullptr);
    cs->setDrive(5.f, 120.f);

    float minT = 1e9f, maxT = -1e9f;
    for (int i = 0; i < 240; ++i) {
        world->updateFull(1.f / 60.f, 4);
        const float t = cs->getSliderTranslation();
        minT = std::min(minT, t);
        maxT = std::max(maxT, t);
    }
    CHECK_GT(maxT - minT, 0.35f);
    CHECK(std::fabs(slider->getY()) < 0.15f);
    CHECK(std::fabs(slider->getZ()) < 0.15f);
    CHECK_THROWS((world->newCrankSlider(frame, crank, rod, slider, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
                                        0.5f, 0.f, 0.f, 1.2f, 0.f, 0.f, 1.f, 0.f, 0.f, false),
                  false));
}
