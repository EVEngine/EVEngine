#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/DistanceField3D.h"
#include "physics/Joint3D.h"

#include <box3d/box3d.h>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using namespace eve::physics;

TEST_CASE("box3d.world.createAndGravity") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    REQUIRE(world.get() != nullptr);
    CHECK(std::fabs(world->getGravityX()) < 0.01f);
    CHECK(std::fabs(world->getGravityY() + 9.8f) < 0.01f);
    CHECK(std::fabs(world->getGravityZ()) < 0.01f);
    world->setGravity(1.f, -10.f, 2.f);
    CHECK(std::fabs(world->getGravityX() - 1.f) < 0.01f);
    CHECK(std::fabs(world->getGravityY() + 10.f) < 0.01f);
    CHECK(std::fabs(world->getGravityZ() - 2.f) < 0.01f);
}

TEST_CASE("box3d.body.fallsUnderGravity") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, true));
    Body3D *ground = world->newBody("static", 0.f, -1.f, 0.f);
    ground->newBoxShape(20.f, 2.f, 20.f, 0.f, 0.5f, 0.f);

    Body3D *box = world->newBody("dynamic", 0.f, 4.f, 0.f);
    box->newBoxShape(1.f, 1.f, 1.f, 1.f, 0.3f, 0.1f);
    float y0 = box->getY();

    for (int i = 0; i < 90; ++i)
        world->update(1.f / 60.f);

    CHECK_LT(box->getY(), y0);
    CHECK_GT(box->getY(), 0.f);
}

TEST_CASE("box3d.body.staticDoesNotMove") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, true));
    Body3D *ground = world->newBody("static", 0.f, 0.f, 0.f);
    ground->newBoxShape(4.f, 1.f, 4.f, 0.f);
    float y0 = ground->getY();
    for (int i = 0; i < 30; ++i)
        world->update(1.f / 60.f);
    CHECK(std::fabs(ground->getY() - y0) < 0.01f);
    CHECK_EQ(ground->getType(), std::string("static"));
}

TEST_CASE("box3d.body.sphereAndVelocity") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *ball = world->newBody("dynamic", 0.f, 0.f, 0.f);
    ball->newSphereShape(0.5f, 1.f, 0.2f, 0.8f);
    ball->setLinearVelocity(5.f, 0.f, 0.f);

    for (int i = 0; i < 30; ++i)
        world->update(1.f / 60.f);

    CHECK_GT(ball->getX(), 1.f);
    CHECK(std::fabs(ball->getY()) < 0.1f);
    CHECK(std::fabs(ball->getZ()) < 0.1f);
}

TEST_CASE("box3d.body.dampingGravitySleepMassAndCenters") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D *body = world->newBody("dynamic", 3.f, 4.f, 5.f);
    body->newBoxShape(2.f, 2.f, 2.f, 2.f);

    CHECK(std::fabs(body->getMass() - 16.f) < 0.05f);
    CHECK(std::fabs(body->getLocalCenterX()) < 0.001f);
    CHECK(std::fabs(body->getLocalCenterY()) < 0.001f);
    CHECK(std::fabs(body->getLocalCenterZ()) < 0.001f);
    CHECK(std::fabs(body->getWorldCenterX() - 3.f) < 0.001f);
    CHECK(std::fabs(body->getWorldCenterY() - 4.f) < 0.001f);
    CHECK(std::fabs(body->getWorldCenterZ() - 5.f) < 0.001f);

    body->setLinearDamping(3.f);
    body->setAngularDamping(4.f);
    body->setGravityScale(-0.5f);
    body->setSleepEnabled(false);
    body->setSleepThreshold(0.25f);
    CHECK(std::fabs(body->getLinearDamping() - 3.f) < 0.001f);
    CHECK(std::fabs(body->getAngularDamping() - 4.f) < 0.001f);
    CHECK(std::fabs(body->getGravityScale() + 0.5f) < 0.001f);
    CHECK(!body->isSleepEnabled());
    CHECK(std::fabs(body->getSleepThreshold() - 0.25f) < 0.001f);

    body->setLinearVelocity(6.f, 0.f, 0.f);
    world->updateFull(0.25f, 4);
    CHECK_LT(body->getLinearVelocityX(), 6.f);
    CHECK_GT(body->getLinearVelocityY(), 0.f);

    CHECK_THROWS((body->setLinearDamping(-1.f), false));
    CHECK_THROWS((body->setAngularDamping(std::numeric_limits<float>::infinity()), false));
    CHECK_THROWS((body->setGravityScale(std::numeric_limits<float>::quiet_NaN()), false));
    CHECK_THROWS((body->setSleepThreshold(-0.1f), false));
}

TEST_CASE("box3d.body.customMassCenterAndInertia") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("dynamic", 10.f, 0.f, 0.f);
    body->newBoxShape(2.f, 2.f, 2.f, 2.f);
    const float automaticMass = body->getMass();

    body->setMassProperties(4.f, 0.25f, -0.5f, 0.75f, 2.f, 3.f, 4.f, 0.1f,
                            0.2f, 0.3f);
    CHECK(std::fabs(body->getMass() - 4.f) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterX() - 0.25f) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterY() + 0.5f) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterZ() - 0.75f) < 1e-6f);
    CHECK(std::fabs(body->getWorldCenterX() - 10.25f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaXX() - 2.f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaYY() - 3.f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaZZ() - 4.f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaXY() - 0.1f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaXZ() - 0.2f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaYZ() - 0.3f) < 1e-6f);

    body->applyLinearImpulse(8.f, 0.f, 0.f);
    CHECK(std::fabs(body->getLinearVelocityX() - 2.f) < 1e-6f);

    body->resetMassProperties();
    CHECK(std::fabs(body->getMass() - automaticMass) < 0.05f);
    CHECK(std::fabs(body->getLocalCenterX()) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterY()) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterZ()) < 1e-6f);

    CHECK_THROWS((body->setMassProperties(0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f), false));
    CHECK_THROWS((body->setMassProperties(1.f, 0.f, 0.f, 0.f, 1.f, 1.f, -1.f), false));
    CHECK_THROWS((body->setMassProperties(1.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 2.f),
                  false));
    CHECK_THROWS((body->setMassProperties(
                      1.f, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 1.f, 1.f,
                      1.f),
                  false));

    Body3D *ground = world->newBody("static", 0.f, 0.f, 0.f);
    CHECK_THROWS((ground->setMassProperties(1.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f), false));
}

TEST_CASE("box3d.body.spaceTransformsPointVelocityAndLocalImpulses") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("dynamic", 3.f, 4.f, 5.f);
    body->newSphereShape(0.5f, 1.f);
    const float halfSqrt2 = std::sqrt(0.5f);
    body->setRotation(0.f, 0.f, halfSqrt2, halfSqrt2);  // local +X becomes world +Y

    const std::vector<float> worldPoint = body->localToWorldPoint(1.f, 2.f, 3.f);
    REQUIRE(worldPoint.size() == 3);
    CHECK(std::fabs(worldPoint[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(worldPoint[1] - 5.f) < 1e-5f);
    CHECK(std::fabs(worldPoint[2] - 8.f) < 1e-5f);
    const std::vector<float> localPoint =
        body->worldToLocalPoint(worldPoint[0], worldPoint[1], worldPoint[2]);
    CHECK(std::fabs(localPoint[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(localPoint[1] - 2.f) < 1e-5f);
    CHECK(std::fabs(localPoint[2] - 3.f) < 1e-5f);

    const std::vector<float> worldVector = body->localToWorldVector(2.f, 0.f, 0.f);
    CHECK(std::fabs(worldVector[0]) < 1e-5f);
    CHECK(std::fabs(worldVector[1] - 2.f) < 1e-5f);
    CHECK(std::fabs(worldVector[2]) < 1e-5f);
    const std::vector<float> localVector =
        body->worldToLocalVector(worldVector[0], worldVector[1], worldVector[2]);
    CHECK(std::fabs(localVector[0] - 2.f) < 1e-5f);
    CHECK(std::fabs(localVector[1]) < 1e-5f);

    body->setLinearVelocity(1.f, 2.f, 3.f);
    body->setAngularVelocity(0.f, 0.f, 2.f);
    // Local (2,0,0) is world offset (0,2,0): v + omega x r = (-3,2,3).
    const std::vector<float> localPointVelocity = body->getLocalPointVelocity(2.f, 0.f, 0.f);
    const std::vector<float> worldPointVelocity = body->getWorldPointVelocity(3.f, 6.f, 5.f);
    for (const std::vector<float> *velocity : {&localPointVelocity, &worldPointVelocity}) {
        CHECK(std::fabs((*velocity)[0] + 3.f) < 1e-5f);
        CHECK(std::fabs((*velocity)[1] - 2.f) < 1e-5f);
        CHECK(std::fabs((*velocity)[2] - 3.f) < 1e-5f);
    }

    body->setLinearVelocity(0.f, 0.f, 0.f);
    body->setAngularVelocity(0.f, 0.f, 0.f);
    body->setMassProperties(2.f, 0.f, 0.f, 0.f, 2.f, 2.f, 2.f);
    body->applyLocalLinearImpulseToCenter(4.f, 0.f, 0.f);
    CHECK(std::fabs(body->getLinearVelocityX()) < 1e-5f);
    CHECK(std::fabs(body->getLinearVelocityY() - 2.f) < 1e-5f);
    body->applyLocalAngularImpulse(4.f, 0.f, 0.f);
    CHECK(std::fabs(body->getAngularVelocityX()) < 1e-5f);
    CHECK(std::fabs(body->getAngularVelocityY() - 2.f) < 1e-5f);

    CHECK_THROWS((body->localToWorldPoint(std::numeric_limits<float>::infinity(), 0.f, 0.f),
                  false));
    CHECK_THROWS((body->applyLocalForceToCenter(
                      0.f, std::numeric_limits<float>::quiet_NaN(), 0.f),
                  false));
}

TEST_CASE("box3d.body.perAxisLocksAndAdvancedForces") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    body->newBoxShape(1.f, 1.f, 1.f, 1.f);
    body->setMotionLocks(true, false, true, true, true, false);
    CHECK(body->isLinearXLocked());
    CHECK(!body->isLinearYLocked());
    CHECK(body->isLinearZLocked());
    CHECK(body->isAngularXLocked());
    CHECK(body->isAngularYLocked());
    CHECK(!body->isAngularZLocked());

    body->setLinearVelocity(5.f, 2.f, 4.f);
    body->applyLinearImpulseAt(0.f, 1.f, 0.f, 0.5f, 0.f, 0.f);
    body->applyTorque(0.f, 0.f, 4.f);
    world->updateFull(0.25f, 4);
    CHECK(std::fabs(body->getX()) < 0.001f);
    CHECK_GT(body->getY(), 0.1f);
    CHECK(std::fabs(body->getZ()) < 0.001f);
    CHECK(std::fabs(body->getAngularVelocityX()) < 0.001f);
    CHECK(std::fabs(body->getAngularVelocityY()) < 0.001f);
    CHECK_GT(std::fabs(body->getAngularVelocityZ()), 0.1f);

    body->setFixedRotation(true);
    CHECK(body->isAngularXLocked());
    CHECK(body->isAngularYLocked());
    CHECK(body->isAngularZLocked());
    CHECK(body->isFixedRotation());
}

TEST_CASE("box3d.body.kinematicTargetTransform") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("kinematic", 0.f, 0.f, 0.f);
    body->newBoxShape(1.f, 1.f, 1.f);
    body->setTargetTransform(2.f, 1.f, -1.f, 0.f, 0.f, 0.f, 2.f, 0.5f);
    world->updateFull(0.5f, 4);
    CHECK(std::fabs(body->getX() - 2.f) < 0.05f);
    CHECK(std::fabs(body->getY() - 1.f) < 0.05f);
    CHECK(std::fabs(body->getZ() + 1.f) < 0.05f);
    CHECK(std::fabs(body->getRotW() - 1.f) < 0.001f);
    CHECK_THROWS((body->setTargetTransform(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f),
                  false));
    CHECK_THROWS((body->setTargetTransform(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f),
                  false));
}

TEST_CASE("box3d.joint.distanceMaintainsLengthAndSupportsSpringMotorConfiguration") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D *ground = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *bob = world->newBody("dynamic", 0.f, 1.f, 0.f);
    bob->newSphereShape(0.25f, 2.f);
    Joint3D *joint = world->newDistanceJoint(ground, bob, 0.f, 3.f, 0.f, 0.f, 1.f, 0.f,
                                             2.f, false);
    REQUIRE(joint != nullptr);
    CHECK(joint->isValid());
    CHECK_EQ(joint->getKind(), std::string("distance"));
    CHECK_EQ(joint->getBodyAId(), ground->getId());
    CHECK_EQ(joint->getBodyBId(), bob->getId());
    CHECK(std::fabs(joint->getDistanceLength() - 2.f) < 0.001f);
    CHECK(!joint->getCollideConnected());

    bob->applyLinearImpulse(1.f, 0.f, 0.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK(std::fabs(joint->getDistanceCurrentLength() - 2.f) < 0.03f);
    CHECK_GT(std::fabs(joint->getConstraintForceY()), 0.1f);

    joint->setDistanceSpring(true, 4.f, 0.7f);
    joint->setDistanceLimits(true, 1.5f, 2.5f);
    joint->setDistanceMotor(true, 0.25f, 20.f);
    joint->setDistanceLength(2.1f);
    CHECK(std::fabs(joint->getDistanceLength() - 2.1f) < 0.001f);
    CHECK_THROWS((joint->setDistanceLimits(true, 3.f, 2.f), false));
    CHECK_THROWS((joint->setRevoluteMotor(true, 1.f, 2.f), false));
}

TEST_CASE("box3d.joint.revoluteMotorHonorsLimitAndBodyDestructionInvalidatesWrapper") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *frame = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *door = world->newBody("dynamic", 1.f, 0.f, 0.f);
    door->newBoxShape(2.f, 0.25f, 0.25f, 1.f);
    Joint3D *hinge =
        world->newRevoluteJoint(frame, door, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(hinge != nullptr);
    CHECK_EQ(hinge->getKind(), std::string("revolute"));
    hinge->setRevoluteLimits(true, -0.5f, 0.5f);
    hinge->setRevoluteSpring(false, 0.f, 0.f, 0.f);
    hinge->setRevoluteMotor(true, 3.f, 100.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(hinge->getRevoluteAngle(), 0.35f);
    CHECK_LT(hinge->getRevoluteAngle(), 0.55f);
    CHECK_GT(std::fabs(hinge->getRevoluteMotorTorque()), 0.01f);
    CHECK_THROWS((hinge->setRevoluteLimits(true, 1.f, -1.f), false));
    CHECK_THROWS((hinge->setDistanceLength(1.f), false));

    door->destroy();
    CHECK(!hinge->isValid());
    CHECK_EQ(hinge->getBodyAId(), -1);
    CHECK_EQ(hinge->getBodyBId(), -1);
    hinge->destroy();
}

TEST_CASE("box3d.joint.validatesBodiesAxisAndWorldOwnership") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> worldA(mod->newWorld3D(0.f, 0.f, 0.f, false));
    std::unique_ptr<World3D> worldB(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *a = worldA->newBody("static", 0.f, 0.f, 0.f);
    Body3D *b = worldA->newBody("dynamic", 1.f, 0.f, 0.f);
    b->newSphereShape(0.25f);
    Body3D *foreign = worldB->newBody("dynamic", 0.f, 0.f, 0.f);
    foreign->newSphereShape(0.25f);
    CHECK_THROWS((worldA->newDistanceJoint(a, foreign, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                            1.f, false),
                  false));
    CHECK_THROWS((worldA->newRevoluteJoint(a, b, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false),
                  false));
    CHECK_THROWS((worldA->newDistanceJoint(a, a, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f,
                                            false),
                  false));
}

TEST_CASE("box3d.joint.prismaticMotorMovesOnlyAlongAxisAndHonorsLimit") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *rail = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *carriage = world->newBody("dynamic", 0.f, 0.f, 0.f);
    carriage->newBoxShape(0.5f, 0.5f, 0.5f);
    Joint3D *slider =
        world->newPrismaticJoint(rail, carriage, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, false);
    REQUIRE(slider != nullptr);
    CHECK_EQ(slider->getKind(), std::string("prismatic"));
    slider->setPrismaticLimits(true, 0.f, 1.f);
    slider->setPrismaticSpring(false, 0.f, 0.f, 0.f);
    slider->setPrismaticMotor(true, 3.f, 100.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(slider->getPrismaticTranslation(), 0.85f);
    CHECK_LT(slider->getPrismaticTranslation(), 1.05f);
    CHECK(std::fabs(carriage->getY()) < 0.01f);
    CHECK(std::fabs(carriage->getZ()) < 0.01f);
    CHECK_GT(std::fabs(slider->getPrismaticMotorForce()), 0.01f);
    CHECK_THROWS((slider->setPrismaticLimits(true, 2.f, 1.f), false));
    CHECK_THROWS((slider->setSphericalConeLimit(true, 0.5f), false));
}

TEST_CASE("box3d.joint.sphericalMotorRespectsConeAndKeepsSharedAnchor") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D *socket = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *limb = world->newBody("dynamic", 0.f, -1.f, 0.f);
    limb->newBoxShape(0.25f, 2.f, 0.25f);
    Joint3D *ball =
        world->newSphericalJoint(socket, limb, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(ball != nullptr);
    CHECK_EQ(ball->getKind(), std::string("spherical"));
    ball->setSphericalConeLimit(true, 0.4f);
    ball->setSphericalTwistLimits(true, -0.3f, 0.3f);
    ball->setSphericalMotor(true, 4.f, 0.f, 0.f, 100.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(ball->getSphericalConeAngle(), 0.25f);
    CHECK_LT(ball->getSphericalConeAngle(), 0.45f);
    CHECK_LT(std::fabs(ball->getSphericalTwistAngle()), 0.35f);
    const b3Pos anchor = b3Body_GetWorldPoint(limb->raw(), b3Vec3{0.f, 1.f, 0.f});
    CHECK(std::fabs(anchor.x) < 0.03f);
    CHECK(std::fabs(anchor.y) < 0.03f);
    CHECK(std::fabs(anchor.z) < 0.03f);
    CHECK_THROWS((ball->setSphericalConeLimit(true, 4.f), false));
    CHECK_THROWS((ball->setSphericalTwistLimits(true, 0.5f, -0.5f), false));
}

TEST_CASE("box3d.joint.stressEventsExposeStableIdentityAndSolverFeedback") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D *support = world->newBody("static", 0.f, 2.f, 0.f);
    Body3D *load = world->newBody("dynamic", 0.f, 0.f, 0.f);
    load->newSphereShape(0.5f, 20.f);
    Joint3D *joint = world->newDistanceJoint(support, load, 0.f, 2.f, 0.f, 0.f, 0.f,
                                             0.f, 2.f, false);
    joint->setForceThreshold(0.1f);
    joint->setTorqueThreshold(1000000.f);
    CHECK(std::fabs(joint->getForceThreshold() - 0.1f) < 0.001f);
    CHECK(std::fabs(joint->getTorqueThreshold() - 1000000.f) < 1.f);

    world->updateFull(1.f / 60.f, 4);
    REQUIRE_EQ(world->getJointStressCount(), 1);
    CHECK_EQ(world->getJointStressJointId(0), joint->getId());
    CHECK_EQ(world->getJointStressBodyAId(0), support->getId());
    CHECK_EQ(world->getJointStressBodyBId(0), load->getId());
    CHECK_EQ(world->getJointStressKind(0), 0);
    CHECK_GT(std::fabs(world->getJointStressForceY(0)), 1.f);
    CHECK(std::isfinite(world->getJointStressTorqueX(0)));
    CHECK_THROWS((world->getJointStressJointId(1), false));

    joint->setForceThreshold(1000000.f);
    world->updateFull(1.f / 60.f, 4);
    CHECK_EQ(world->getJointStressCount(), 0);
    CHECK_THROWS((joint->setForceThreshold(-1.f), false));
    CHECK_THROWS((joint->setTorqueThreshold(std::numeric_limits<float>::quiet_NaN()), false));
}

TEST_CASE("box3d.joint.wheelCombinesSuspensionSpinAndSteering") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *chassis = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D *wheelBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    wheelBody->newSphereShape(0.5f, 1.f);
    Joint3D *wheel = world->newWheelJoint(chassis, wheelBody, 0.f, 0.f, 0.f,
                                          0.f, 1.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(wheel != nullptr);
    CHECK_EQ(wheel->getKind(), std::string("wheel"));
    wheel->setWheelSuspension(true, 5.f, 0.7f);
    wheel->setWheelSuspensionLimits(true, -0.1f, 0.1f);
    wheel->setWheelSpinMotor(true, 5.f, 100.f);
    wheel->setWheelSteering(true, 0.3f, 5.f, 0.8f, 100.f);
    wheel->setWheelSteeringLimits(true, -0.4f, 0.4f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(wheel->getWheelSpinSpeed(), 4.f);
    CHECK_GT(std::fabs(wheel->getWheelSpinTorque()), 0.001f);
    CHECK_GT(wheel->getWheelSteeringAngle(), 0.2f);
    CHECK_LT(wheel->getWheelSteeringAngle(), 0.4f);
    CHECK(std::isfinite(wheel->getWheelSteeringTorque()));
    CHECK(std::fabs(wheelBody->getX()) < 0.02f);
    CHECK(std::fabs(wheelBody->getY()) < 0.12f);
    CHECK(std::fabs(wheelBody->getZ()) < 0.02f);
    CHECK_THROWS((wheel->setWheelSuspensionLimits(true, 1.f, -1.f), false));
    CHECK_THROWS((wheel->setWheelSpinMotor(true, 1.f, -1.f), false));

    CHECK_THROWS((world->newWheelJoint(chassis, wheelBody, 0.f, 0.f, 0.f,
                                        0.f, 1.f, 0.f, 0.f, 2.f, 0.f, false),
                  false));
}

TEST_CASE("box3d.shape.sensorAndMaterial") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *b = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *shape = b->newBoxShape(1.f, 1.f, 1.f);
    CHECK(!shape->isSensor());
    shape->setSensor(true);
    CHECK(shape->isSensor());
    shape->setFriction(0.7f);
    CHECK(std::fabs(shape->getFriction() - 0.7f) < 0.001f);
    shape->setRestitution(0.35f);
    shape->setRollingResistance(0.12f);
    shape->setTangentVelocity(1.f, 2.f, 3.f);
    shape->setMaterialId(-559038737); // 0xDEADBEEF preserves all low bits.
    shape->setFrictionCombineMode("multiply");
    shape->setRestitutionCombineMode("minimum");
    shape->setDensity(2.f);
    CHECK(std::fabs(shape->getRestitution() - 0.35f) < 0.001f);
    CHECK(std::fabs(shape->getRollingResistance() - 0.12f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityX() - 1.f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityY() - 2.f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityZ() - 3.f) < 0.001f);
    CHECK_EQ(shape->getMaterialId(), -559038737);
    CHECK_EQ(shape->getFrictionCombineMode(), std::string("multiply"));
    CHECK_EQ(shape->getRestitutionCombineMode(), std::string("minimum"));
    shape->setSensor(false);
    CHECK(std::fabs(shape->getRollingResistance() - 0.12f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityZ() - 3.f) < 0.001f);
    CHECK_EQ(shape->getMaterialId(), -559038737);
    CHECK_EQ(shape->getFrictionCombineMode(), std::string("multiply"));
    CHECK_EQ(shape->getRestitutionCombineMode(), std::string("minimum"));
    shape->setMaterialId(1234);
    CHECK_EQ(shape->getMaterialId(), 1234);
    CHECK_EQ(shape->getFrictionCombineMode(), std::string("multiply"));
    CHECK_THROWS((shape->setFriction(-0.1f), false));
    CHECK_THROWS((shape->setRestitution(std::numeric_limits<float>::quiet_NaN()), false));
    CHECK_THROWS((shape->setRollingResistance(-0.1f), false));
    CHECK_THROWS((shape->setTangentVelocity(
                      0.f, std::numeric_limits<float>::infinity(), 0.f), false));
    CHECK_THROWS((shape->setDensity(-1.f), false));
    CHECK_THROWS((shape->setFrictionCombineMode("median"), false));
    CHECK_THROWS((shape->setRestitutionCombineMode("add"), false));
}

TEST_CASE("box3d.material.tangentVelocityDrivesConveyorContact") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Shape3D *belt =
        world->newBody("static", 0.f, -0.25f, 0.f)->newBoxShape(20.f, 0.5f, 4.f);
    belt->setFriction(1.f);
    belt->setTangentVelocity(3.f, 0.f, 0.f);
    Body3D *cargo = world->newBody("dynamic", 0.f, 0.6f, 0.f);
    cargo->newBoxShape(0.8f, 0.8f, 0.8f, 1.f, 1.f);
    for (int i = 0; i < 180; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(std::fabs(cargo->getX()), 1.f);
    CHECK_LT(std::fabs(cargo->getY() - 0.4f), 0.08f);
}

TEST_CASE("box3d.material.restitutionCombineModeChangesCollisionResponse") {
    auto reboundSpeed = [](const std::string &mode) {
        auto *mod = Physics::create();
        std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
        Shape3D *floor =
            world->newBody("static", 0.f, -0.25f, 0.f)->newBoxShape(10.f, 0.5f, 10.f);
        floor->setRestitution(0.2f);
        floor->setRestitutionCombineMode(mode);
        Body3D *ball = world->newBody("dynamic", 0.f, 2.f, 0.f);
        Shape3D *ballShape = ball->newSphereShape(0.25f);
        ballShape->setRestitution(0.8f);
        float maximumUpwardSpeed = 0.f;
        for (int i = 0; i < 120; ++i) {
            world->updateFull(1.f / 120.f, 2);
            if (i > 30)
                maximumUpwardSpeed = std::max(maximumUpwardSpeed,
                                              ball->getLinearVelocityY());
        }
        return maximumUpwardSpeed;
    };

    const float minimumBounce = reboundSpeed("minimum");
    const float maximumBounce = reboundSpeed("maximum");
    CHECK_GT(minimumBounce, 0.5f);
    CHECK_GT(maximumBounce, minimumBounce + 2.f);
}

TEST_CASE("box3d.world.solverAndContinuousCollisionTuning") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));

    CHECK(world->isContinuousCollisionEnabled());
    world->setContinuousCollisionEnabled(false);
    CHECK(!world->isContinuousCollisionEnabled());
    world->setContinuousCollisionEnabled(true);

    world->setRestitutionThreshold(0.75f);
    CHECK(std::fabs(world->getRestitutionThreshold() - 0.75f) < 0.001f);
    world->setContactTuning(42.f, 0.85f, 2.5f);
    CHECK(std::fabs(world->getContactHertz() - 42.f) < 0.001f);
    CHECK(std::fabs(world->getContactDampingRatio() - 0.85f) < 0.001f);
    CHECK(std::fabs(world->getContactPushOutSpeed() - 2.5f) < 0.001f);
    world->setContactRecycleDistance(0.02f);
    CHECK(std::fabs(world->getContactRecycleDistance() - 0.02f) < 0.001f);
    world->setMaximumLinearSpeed(12.f);
    CHECK(std::fabs(world->getMaximumLinearSpeed() - 12.f) < 0.001f);

    CHECK(world->isWarmStartingEnabled());
    world->setWarmStartingEnabled(false);
    CHECK(!world->isWarmStartingEnabled());
    world->setWarmStartingEnabled(true);

    Body3D *fast = world->newBody("dynamic", 0.f, 0.f, 0.f);
    fast->newSphereShape(0.1f);
    fast->setLinearVelocity(100.f, 0.f, 0.f);
    world->updateFull(1.f / 60.f, 1);
    CHECK_LE(fast->getLinearVelocityX(), 12.01f);

    CHECK_THROWS((world->setRestitutionThreshold(-0.01f), false));
    CHECK_THROWS((world->setContactTuning(0.f, 1.f, 1.f), false));
    CHECK_THROWS((world->setContactTuning(30.f, -0.1f, 1.f), false));
    CHECK_THROWS((world->setContactTuning(30.f, 1.f,
                                           std::numeric_limits<float>::infinity()), false));
    CHECK_THROWS((world->setContactRecycleDistance(-0.01f), false));
    CHECK_THROWS((world->setMaximumLinearSpeed(0.f), false));
}

TEST_CASE("box3d.contact.runtimeBodyAndShapePairOverridesRefreshImmediately") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *wallBody = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *nearWall = wallBody->newBoxShape(0.2f, 4.f, 4.f);
    Shape3D *farWall = wallBody->newBoxShape(0.2f, 4.f, 4.f);
    farWall->setLocalPosition(2.f, 0.f, 0.f);
    Body3D *mover = world->newBody("dynamic", -2.f, 0.f, 0.f);
    Shape3D *ball = mover->newSphereShape(0.25f);
    mover->setBullet(true);

    CHECK(world->isBodyPairCollisionEnabled(wallBody, mover));
    world->setBodyPairCollisionEnabled(wallBody, mover, false);
    CHECK(!world->isBodyPairCollisionEnabled(mover, wallBody));
    mover->setLinearVelocity(5.f, 0.f, 0.f);
    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 2);
    CHECK_GT(mover->getX(), 2.5f);

    mover->setPosition(-2.f, 0.f, 0.f);
    mover->setLinearVelocity(5.f, 0.f, 0.f);
    world->setBodyPairCollisionEnabled(wallBody, mover, true);
    CHECK(world->isBodyPairCollisionEnabled(wallBody, mover));
    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 2);
    CHECK_LT(mover->getX(), -0.25f);

    mover->setPosition(-2.f, 0.f, 0.f);
    mover->setLinearVelocity(5.f, 0.f, 0.f);
    world->setShapePairCollisionEnabled(nearWall, ball, false);
    CHECK(!world->isShapePairCollisionEnabled(ball, nearWall));
    CHECK(world->isShapePairCollisionEnabled(ball, farWall));
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 2);
    CHECK_GT(mover->getX(), 0.5f);
    CHECK_LT(mover->getX(), 1.75f);

    world->setShapePairCollisionEnabled(nearWall, ball, true);
    CHECK(world->isShapePairCollisionEnabled(nearWall, ball));
    CHECK_THROWS((world->setBodyPairCollisionEnabled(mover, mover, false), false));
    CHECK_THROWS((world->setShapePairCollisionEnabled(ball, ball, false), false));
}

TEST_CASE("box3d.world.explosionUsesGeometryFalloffMaskAndReportsVelocityDelta") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *nearBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    nearBody->newSphereShape(0.25f)->setCategoryBits(1);
    Body3D *farBody = world->newBody("dynamic", 3.5f, 0.f, 0.f);
    farBody->newSphereShape(0.25f)->setCategoryBits(1);
    Body3D *filteredBody = world->newBody("dynamic", 1.f, 0.f, 2.f);
    filteredBody->newSphereShape(0.25f)->setCategoryBits(2);
    Body3D *staticBody = world->newBody("static", 1.f, 0.f, -2.f);
    staticBody->newSphereShape(0.25f)->setCategoryBits(1);

    CHECK_EQ(world->explode(0.f, 0.f, 0.f, 1.f, 4.f, 10.f, 1), 2);
    CHECK_EQ(world->getExplosionResultCount(), 2);
    CHECK_EQ(world->getExplosionResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getExplosionResultBodyId(1), farBody->getId());
    CHECK_GT(nearBody->getLinearVelocityX(), farBody->getLinearVelocityX());
    CHECK_GT(farBody->getLinearVelocityX(), 0.f);
    CHECK(std::fabs(filteredBody->getLinearVelocityX()) < 1e-6f);
    CHECK(std::fabs(world->getExplosionResultDeltaVelocityX(0) -
                    nearBody->getLinearVelocityX()) < 1e-5f);
    CHECK(std::isfinite(world->getExplosionResultDeltaAngularVelocityZ(0)));

    nearBody->setLinearVelocity(0.f, 0.f, 0.f);
    farBody->setLinearVelocity(0.f, 0.f, 0.f);
    CHECK_EQ(world->explode(0.f, 0.f, 0.f, 1.f, 4.f, -10.f, 1), 2);
    CHECK_LT(nearBody->getLinearVelocityX(), 0.f);
    CHECK_EQ(world->explode(0.f, 0.f, 0.f, 1.f, 4.f, 0.f, 1), 0);
    CHECK_EQ(world->getExplosionResultCount(), 0);
    CHECK_THROWS((world->explode(0.f, 0.f, 0.f, -1.f, 0.f, 1.f), false));
    CHECK_THROWS((world->explode(0.f, 0.f, 0.f, 1.f, -1.f, 1.f), false));
    CHECK_THROWS((world->explode(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f,
                                 1.f, 0.f, 1.f), false));
    CHECK_THROWS((world->getExplosionResultBodyId(0), false));
}

TEST_CASE("box3d.world.diagnosticsExposeBoundsCountersAndStepProfile") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, true));
    Body3D *floor = world->newBody("static", 0.f, -0.5f, 0.f);
    floor->newBoxShape(10.f, 1.f, 10.f);
    Body3D *a = world->newBody("dynamic", -0.5f, 2.f, 0.f);
    a->newSphereShape(0.25f);
    Body3D *b = world->newBody("dynamic", 0.5f, 2.f, 0.f);
    b->newSphereShape(0.25f);
    world->newDistanceJoint(a, b, -0.5f, 2.f, 0.f, 0.5f, 2.f, 0.f, 1.f);

    CHECK_EQ(world->getBodyCount(), 3);
    CHECK_EQ(world->getShapeCount(), 3);
    CHECK_EQ(world->getJointCount(), 1);
    CHECK_LE(world->getBoundsMinX(), -5.f);
    CHECK_GE(world->getBoundsMaxX(), 5.f);
    CHECK_LE(world->getBoundsMinY(), -1.f);
    CHECK_GE(world->getBoundsMaxY(), 2.25f);
    CHECK_GE(world->getStaticTreeHeight(), 0);
    CHECK_GE(world->getDynamicTreeHeight(), 0);
    CHECK_GT(world->getMemoryByteCount(), 0);

    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(world->getContactCount(), 0);
    CHECK_GE(world->getIslandCount(), 1);
    CHECK_GE(world->getAwakeBodyCount(), 0);
    CHECK_GE(world->getAwakeContactCount(), 0);
    CHECK_GE(world->getRecycledContactCount(), 0);
    CHECK_GE(world->getProfileStepMs(), 0.f);
    CHECK_GE(world->getProfilePairsMs(), 0.f);
    CHECK_GE(world->getProfileCollideMs(), 0.f);
    CHECK_GE(world->getProfileSolveMs(), 0.f);
    CHECK_GE(world->getProfileBulletsMs(), 0.f);
    CHECK_GE(world->getProfileSensorsMs(), 0.f);
}

TEST_CASE("box3d.shape.explosionScaleDisablesAmplifiesAndSurvivesRecreation") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *normal = world->newBody("dynamic", 2.f, 0.f, 0.f);
    Shape3D *normalShape = normal->newSphereShape(0.25f);
    Body3D *amplified = world->newBody("dynamic", 0.f, 0.f, 2.f);
    Shape3D *amplifiedShape = amplified->newSphereShape(0.25f);
    Body3D *immune = world->newBody("dynamic", -2.f, 0.f, 0.f);
    Shape3D *immuneShape = immune->newSphereShape(0.25f);

    const int stableId = amplifiedShape->getId();
    amplifiedShape->setExplosionScale(2.f);
    immuneShape->setExplosionScale(0.f);
    amplifiedShape->setSensor(true);
    amplifiedShape->setSensor(false);
    CHECK_EQ(amplifiedShape->getId(), stableId);
    CHECK(std::fabs(amplifiedShape->getExplosionScale() - 2.f) < 1e-6f);
    CHECK(std::fabs(normalShape->getExplosionScale() - 1.f) < 1e-6f);

    CHECK_EQ(world->explode(0.f, 0.f, 0.f, 3.f, 0.f, 10.f), 2);
    const float normalSpeed = std::sqrt(
        normal->getLinearVelocityX() * normal->getLinearVelocityX() +
        normal->getLinearVelocityY() * normal->getLinearVelocityY() +
        normal->getLinearVelocityZ() * normal->getLinearVelocityZ());
    const float amplifiedSpeed = std::sqrt(
        amplified->getLinearVelocityX() * amplified->getLinearVelocityX() +
        amplified->getLinearVelocityY() * amplified->getLinearVelocityY() +
        amplified->getLinearVelocityZ() * amplified->getLinearVelocityZ());
    CHECK_GT(normalSpeed, 0.f);
    CHECK(std::fabs(amplifiedSpeed / normalSpeed - 2.f) < 0.02f);
    CHECK(std::fabs(immune->getLinearVelocityX()) < 1e-6f);
    CHECK(std::fabs(immune->getLinearVelocityZ()) < 1e-6f);
    CHECK_THROWS((immuneShape->setExplosionScale(-0.1f), false));
    CHECK_THROWS((immuneShape->setExplosionScale(
                      std::numeric_limits<float>::infinity()), false));
}

TEST_CASE("box3d.oneway.configurationAndSensorRecreation") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Shape3D *shape = world->newBody("static", 0.f, 0.f, 0.f)->newBoxShape(4.f, 0.5f, 4.f);

    CHECK(!shape->isOneWay());
    CHECK_THROWS((shape->setOneWay(0.f, 0.f, 0.f, 0.f, 0.05f, 0.1f), false));
    CHECK_THROWS((shape->setOneWay(0.f, 1.f, 0.f, 0.f, -0.01f, 0.1f), false));
    CHECK_THROWS((shape->setOneWay(0.f, 1.f, 0.f, 0.f, 0.05f, 1.01f), false));

    shape->setOneWay(0.f, 2.f, 0.f, 0.25f, 0.05f, 0.2f);
    CHECK(shape->isOneWay());
    CHECK(b3Shape_ArePreSolveEventsEnabled(shape->raw()));
    shape->setSensor(true);
    CHECK(shape->isOneWay());
    CHECK(!b3Shape_ArePreSolveEventsEnabled(shape->raw()));
    shape->setSensor(false);
    CHECK(shape->isOneWay());
    CHECK(b3Shape_ArePreSolveEventsEnabled(shape->raw()));
    shape->disableOneWay();
    CHECK(!shape->isOneWay());
    CHECK(!b3Shape_ArePreSolveEventsEnabled(shape->raw()));
}

TEST_CASE("box3d.oneway.platformPassesFromBelowAndBlocksFromAbove") {
    auto run = [](float startY, float velocityY, bool oneWay) {
        auto *mod = Physics::create();
        std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
        Shape3D *platform =
            world->newBody("static", 0.f, 0.f, 0.f)->newBoxShape(10.f, 0.5f, 10.f);
        if (oneWay) platform->setOneWay(0.f, 1.f, 0.f, 0.25f, 0.06f, 0.2f);
        Body3D *ball = world->newBody("dynamic", 0.f, startY, 0.f);
        ball->newSphereShape(0.25f);
        ball->setLinearVelocity(0.f, velocityY, 0.f);
        for (int i = 0; i < 45; ++i) world->updateFull(1.f / 60.f, 4);
        return ball->getY();
    };

    CHECK_GT(run(-2.f, 5.f, true), 1.f);
    const float fromAbove = run(2.f, -5.f, true);
    CHECK_GT(fromAbove, 0.45f);
    CHECK_LT(fromAbove, 0.55f);
    CHECK_LT(run(-2.f, 5.f, false), -0.45f);
}

TEST_CASE("box3d.shape.filterSurvivesSensorRecreation") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *shape = body->newSphereShape(0.5f);
    shape->setCategoryBits(8);
    shape->setMaskBits(3);
    shape->setGroupIndex(-7);
    shape->setTag(42);
    CHECK_EQ(shape->getCategoryBits(), 8);
    CHECK_EQ(shape->getMaskBits(), 3);
    CHECK_EQ(shape->getGroupIndex(), -7);
    CHECK_EQ(shape->getTag(), 42);

    const int stableShapeId = shape->getId();
    CHECK_GT(stableShapeId, 0);
    shape->setSensor(true);
    CHECK_EQ(shape->getId(), stableShapeId);
    CHECK_EQ(shape->getTag(), 42);
    CHECK_EQ(shape->getCategoryBits(), 8);
    CHECK_EQ(shape->getMaskBits(), 3);
    CHECK_EQ(shape->getGroupIndex(), -7);
    shape->setSphereRadius(0.75f);
    CHECK(shape->isSensor());
    CHECK_EQ(shape->getId(), stableShapeId);
    CHECK_EQ(shape->getTag(), 42);
    CHECK(std::fabs(shape->getRadius() - 0.75f) < 1e-5f);
}

TEST_CASE("box3d.shape.localTransformBuildsCompoundCollider") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);

    Shape3D *box = body->newBoxShape(4.f, 1.f, 1.f);
    box->setTag(2101);
    box->setCategoryBits(8);
    box->setMaskBits(3);
    box->setGroupIndex(-4);
    box->setFriction(0.65f);
    box->setRestitution(0.25f);
    box->setDensity(2.5f);
    box->setHitEventsEnabled(true);
    const int stableBoxId = box->getId();
    const float halfRoot = std::sqrt(0.5f);
    box->setLocalTransform(3.f, 0.f, 0.f, 0.f, 0.f, halfRoot, halfRoot);

    CHECK_EQ(box->getId(), stableBoxId);
    CHECK_EQ(box->getTag(), 2101);
    CHECK_EQ(box->getCategoryBits(), 8);
    CHECK_EQ(box->getMaskBits(), 3);
    CHECK_EQ(box->getGroupIndex(), -4);
    CHECK(std::fabs(box->getFriction() - 0.65f) < 1e-5f);
    CHECK(std::fabs(box->getRestitution() - 0.25f) < 1e-5f);
    CHECK(std::fabs(box->getDensity() - 2.5f) < 1e-5f);
    CHECK(box->areHitEventsEnabled());
    CHECK(std::fabs(box->getLocalX() - 3.f) < 1e-5f);
    CHECK(std::fabs(box->getLocalRotZ() - halfRoot) < 1e-5f);
    CHECK(box->testPoint(3.f, 1.5f, 0.f));
    CHECK(!box->testPoint(4.5f, 0.f, 0.f));

    Shape3D *sphere = body->newSphereShape(0.75f);
    sphere->setLocalPosition(-3.f, 0.f, 0.f);
    CHECK(sphere->testPoint(-3.f, 0.f, 0.f));
    CHECK(!sphere->testPoint(0.f, 0.f, 0.f));

    Shape3D *capsule = body->newCapsuleShape(4.f, 0.5f);
    capsule->setLocalTransform(0.f, 0.f, 3.f, 0.f, 0.f, halfRoot, halfRoot);
    CHECK(capsule->testPoint(1.5f, 0.f, 3.f));
    CHECK(!capsule->testPoint(0.f, 1.5f, 3.f));

    CHECK_THROWS((box->setLocalRotation(0.f, 0.f, 0.f, 0.f), false));
}

TEST_CASE("box3d.shape.localTransformParticipatesInWorldQueries") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *shape = body->newSphereShape(1.f);
    shape->setTag(2201);
    shape->setLocalPosition(5.f, 0.f, 0.f);

    CHECK_EQ(world->rayCast(5.f, -4.f, 0.f, 5.f, 4.f, 0.f), body->getId());
    CHECK_EQ(world->getRayHitShapeId(), shape->getId());
    CHECK_EQ(world->getRayHitShapeTag(), 2201);
    CHECK_EQ(world->querySphere(5.f, 0.f, 0.f, 0.25f), 1);
    CHECK_EQ(world->getQueryShapeId(0), shape->getId());
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.25f), 0);
}

TEST_CASE("box3d.shape.runtimePrimitiveResizePreservesIdentityAndUpdatesMass") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *box = body->newBoxShape(1.f, 1.f, 1.f, 2.f, 0.6f, 0.2f);
    box->setTag(2301);
    box->setCategoryBits(16);
    box->setMaskBits(7);
    box->setGroupIndex(-3);
    box->setHitEventsEnabled(true);
    box->setLocalPosition(1.f, 0.f, 0.f);
    const int stableId = box->getId();
    const float initialMass = b3Body_GetMass(body->raw());

    CHECK_EQ(box->getKind(), std::string("box"));
    box->setBoxSize(2.f, 2.f, 2.f);
    CHECK_EQ(box->getId(), stableId);
    CHECK_EQ(box->getTag(), 2301);
    CHECK_EQ(box->getCategoryBits(), 16);
    CHECK_EQ(box->getMaskBits(), 7);
    CHECK_EQ(box->getGroupIndex(), -3);
    CHECK(box->areHitEventsEnabled());
    CHECK(std::fabs(box->getFriction() - 0.6f) < 1e-5f);
    CHECK(std::fabs(box->getRestitution() - 0.2f) < 1e-5f);
    CHECK(std::fabs(box->getDensity() - 2.f) < 1e-5f);
    CHECK(std::fabs(box->getBoxWidth() - 2.f) < 1e-5f);
    CHECK(std::fabs(box->getBoxHeight() - 2.f) < 1e-5f);
    CHECK(std::fabs(box->getBoxDepth() - 2.f) < 1e-5f);
    CHECK(box->testPoint(1.f, 0.9f, 0.f));
    CHECK_GT(b3Body_GetMass(body->raw()), initialMass * 7.5f);
    CHECK_THROWS((box->setSphereRadius(1.f), false));
    CHECK_THROWS((box->setBoxSize(0.f, 1.f, 1.f), false));
    CHECK(std::fabs(box->getBoxWidth() - 2.f) < 1e-5f);
}

TEST_CASE("box3d.shape.runtimeSphereAndCapsuleResizeAffectsQueries") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *sphere = body->newSphereShape(0.5f);
    sphere->setLocalPosition(-3.f, 0.f, 0.f);
    sphere->setSphereRadius(1.5f);
    CHECK_EQ(sphere->getKind(), std::string("sphere"));
    CHECK(std::fabs(sphere->getRadius() - 1.5f) < 1e-5f);
    CHECK(sphere->testPoint(-1.75f, 0.f, 0.f));

    Shape3D *capsule = body->newCapsuleShape(4.f, 0.5f);
    CHECK_EQ(capsule->getKind(), std::string("capsule"));
    CHECK(capsule->testPoint(0.f, 2.f, 0.f));
    capsule->setCapsuleSize(1.f, 0.25f);
    CHECK(std::fabs(capsule->getCapsuleHeight() - 1.f) < 1e-5f);
    CHECK(std::fabs(capsule->getRadius() - 0.25f) < 1e-5f);
    CHECK(!capsule->testPoint(0.f, 2.f, 0.f));
    CHECK(capsule->testPoint(0.f, 0.6f, 0.f));
    CHECK_THROWS((capsule->setCapsuleSize(-1.f, 0.25f), false));
    CHECK_THROWS((sphere->setCapsuleSize(1.f, 0.25f), false));
}

TEST_CASE("box3d.shape.convexHullCreatesQueriesAndRebuildsAtomically") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);
    const std::vector<float> smallCube{
        -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
         0.5f,  0.5f, -0.5f, -0.5f, -0.5f, 0.5f,  0.5f, -0.5f, 0.5f,
        -0.5f,  0.5f,  0.5f, 0.5f, 0.5f, 0.5f};
    Shape3D *hull = body->newConvexHullShape(smallCube, 16);
    REQUIRE(hull != nullptr);
    CHECK_EQ(hull->getKind(), std::string("convexHull"));
    CHECK_EQ(hull->getConvexHullPointCount(), 8);
    CHECK_EQ(hull->getConvexHullMaxVertices(), 16);
    CHECK(hull->testPoint(0.f, 0.f, 0.f));
    CHECK(!hull->testPoint(0.75f, 0.f, 0.f));
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.1f), 1);

    hull->setTag(77);
    hull->setCategoryBits(8);
    hull->setSensor(true);
    const int stableId = hull->getId();
    const std::vector<float> largeCube{
        -1.f, -1.f, -1.f, 1.f, -1.f, -1.f, -1.f, 1.f, -1.f, 1.f, 1.f, -1.f,
        -1.f, -1.f,  1.f, 1.f, -1.f,  1.f, -1.f, 1.f,  1.f, 1.f, 1.f,  1.f};
    hull->setConvexHullVertices(largeCube, 32);
    CHECK_EQ(hull->getId(), stableId);
    CHECK_EQ(hull->getTag(), 77);
    CHECK_EQ(hull->getCategoryBits(), 8);
    CHECK(hull->isSensor());
    CHECK_EQ(hull->getConvexHullMaxVertices(), 32);
    CHECK(hull->testPoint(0.75f, 0.f, 0.f));

    const std::vector<float> coplanar{0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
                                      0.f, 1.f, 0.f, 1.f, 1.f, 0.f};
    CHECK_THROWS((hull->setConvexHullVertices(coplanar, 16), false));
    CHECK_THROWS((hull->setConvexHullVertices(largeCube, 3), false));
    CHECK_THROWS((body->newConvexHullShape({0.f, 0.f, 0.f}, 16), false));
    CHECK_EQ(hull->getConvexHullPointCount(), 8);
    CHECK_EQ(hull->getConvexHullMaxVertices(), 32);
    CHECK(hull->testPoint(0.75f, 0.f, 0.f));
}

TEST_CASE("box3d.shape.convexHullSupportsLocalTransformAndRigidCollision") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    const std::vector<float> wedge{
        -2.f, -0.5f, -2.f, 2.f, -0.5f, -2.f, -2.f, -0.5f, 2.f, 2.f, -0.5f, 2.f,
        -2.f,  0.5f, -2.f, 2.f,  0.5f, -2.f};
    Shape3D *ground = world->newBody("static", 0.f, 0.f, 0.f)->newConvexHullShape(wedge, 16);
    ground->setLocalPosition(1.f, 0.f, 0.f);
    CHECK(ground->testPoint(1.f, 0.f, -1.5f));
    CHECK_EQ(world->querySphere(1.f, 0.f, -1.5f, 0.1f), 1);

    Body3D *ball = world->newBody("dynamic", 1.f, 3.f, -1.5f);
    ball->newSphereShape(0.25f);
    for (int i = 0; i < 120; ++i) world->update(1.f / 60.f);
    CHECK_GT(ball->getY(), 0.65f);
}

TEST_CASE("box3d.shape.triangleMeshValidatesRebuildsAndStaysStatic") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    const std::vector<float> vertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f,
                                       1.f, 0.f,  1.f, -1.f, 0.f, 1.f};
    const std::vector<int32_t> indices{0, 2, 1, 0, 3, 2};
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *mesh = body->newTriangleMeshShape(vertices, indices);
    REQUIRE(mesh != nullptr);
    CHECK_EQ(mesh->getKind(), std::string("triangleMesh"));
    CHECK_EQ(mesh->getTriangleMeshVertexCount(), 4);
    CHECK_EQ(mesh->getTriangleMeshTriangleCount(), 2);
    CHECK_EQ(world->rayCast(0.f, 2.f, 0.f, 0.f, -2.f, 0.f), body->getId());

    mesh->setTag(91);
    mesh->setCategoryBits(16);
    mesh->setSensor(true);
    mesh->setLocalPosition(0.f, 1.f, 0.f);
    const int stableId = mesh->getId();
    CHECK_EQ(world->rayCast(0.f, 2.f, 0.f, 0.f, 0.f, 0.f), body->getId());

    const std::vector<float> larger{-2.f, 0.f, -2.f, 2.f, 0.f, -2.f,
                                     2.f, 0.f,  2.f, -2.f, 0.f, 2.f};
    mesh->setTriangleMeshData(larger, indices, false, 0.f, true, true);
    CHECK_EQ(mesh->getId(), stableId);
    CHECK_EQ(mesh->getTag(), 91);
    CHECK_EQ(mesh->getCategoryBits(), 16);
    CHECK(mesh->isSensor());
    CHECK_EQ(mesh->getTriangleMeshVertexCount(), 4);
    CHECK_EQ(world->rayCast(1.5f, 2.f, 0.f, 1.5f, 0.f, 0.f), body->getId());

    CHECK_THROWS((mesh->setTriangleMeshData(larger, {0, 1, 8}), false));
    CHECK_THROWS((mesh->setTriangleMeshData(larger, {0, 0, 1}), false));
    CHECK_THROWS((mesh->setTriangleMeshData(larger, indices, true, -0.1f), false));
    CHECK_EQ(mesh->getTriangleMeshTriangleCount(), 2);
    CHECK_EQ(world->rayCast(1.5f, 2.f, 0.f, 1.5f, 0.f, 0.f), body->getId());
    CHECK_THROWS((body->setType("dynamic"), false));
    CHECK_EQ(body->getType(), std::string("static"));

    Body3D *dynamicBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    CHECK_THROWS((dynamicBody->newTriangleMeshShape(vertices, indices), false));
}

TEST_CASE("box3d.shape.concaveTriangleMeshPreservesHoleAndSupportsRigidContact") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    std::vector<float> vertices;
    std::vector<int32_t> indices;
    auto addQuad = [&](float minX, float maxX, float minZ, float maxZ) {
        const int32_t base = static_cast<int32_t>(vertices.size() / 3);
        vertices.insert(vertices.end(), {minX, 0.f, minZ, maxX, 0.f, minZ,
                                         maxX, 0.f, maxZ, minX, 0.f, maxZ});
        indices.insert(indices.end(), {base, base + 2, base + 1,
                                       base, base + 3, base + 2});
    };
    addQuad(-3.f, -1.f, -3.f, 3.f);
    addQuad(1.f, 3.f, -3.f, 3.f);
    addQuad(-1.f, 1.f, -3.f, -1.f);
    addQuad(-1.f, 1.f, 1.f, 3.f);
    Body3D *groundBody = world->newBody("static", 0.f, 0.f, 0.f);
    groundBody->newTriangleMeshShape(vertices, indices, true, 0.001f, true, false);

    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.2f), 0);
    CHECK_EQ(world->querySphere(2.f, 0.f, 0.f, 0.2f), 1);
    CHECK_EQ(world->rayCast(0.f, 2.f, 0.f, 0.f, -2.f, 0.f), -1);
    CHECK_EQ(world->rayCast(2.f, 2.f, 0.f, 2.f, -2.f, 0.f), groundBody->getId());

    Body3D *ball = world->newBody("dynamic", 2.f, 3.f, 0.f);
    ball->newSphereShape(0.25f);
    for (int i = 0; i < 120; ++i) world->update(1.f / 60.f);
    CHECK_GT(ball->getY(), 0.2f);
    CHECK_LT(ball->getY(), 0.35f);
}

TEST_CASE("box3d.shape.heightFieldUpdatesRegionAndPreservesConfiguration") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *body = world->newBody("static", -1.f, 0.f, -1.f);
    std::vector<float> heights(9, 0.f);
    Shape3D *terrain = body->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -4.f, 4.f);
    REQUIRE(terrain != nullptr);
    CHECK_EQ(terrain->getKind(), std::string("heightField"));
    CHECK_EQ(terrain->getHeightFieldCountX(), 3);
    CHECK_EQ(terrain->getHeightFieldCountZ(), 3);
    CHECK(std::fabs(terrain->getHeightFieldCellSizeX() - 1.f) < 1e-6f);
    CHECK(std::fabs(terrain->getHeightFieldCellSizeZ() - 1.f) < 1e-6f);
    CHECK_EQ(world->rayCast(0.f, 3.f, 0.f, 0.f, -3.f, 0.f), body->getId());
    CHECK(std::fabs(world->getRayHitY()) < 0.001f);

    terrain->setTag(123);
    terrain->setCategoryBits(32);
    terrain->setSensor(true);
    const int stableId = terrain->getId();
    terrain->setHeightFieldRegion(1, 1, 1, 1, {2.f});
    CHECK_EQ(terrain->getId(), stableId);
    CHECK_EQ(terrain->getTag(), 123);
    CHECK_EQ(terrain->getCategoryBits(), 32);
    CHECK(terrain->isSensor());
    CHECK(std::fabs(terrain->getHeightFieldHeight(1, 1) - 2.f) < 1e-6f);
    CHECK_EQ(world->rayCast(0.f, 3.f, 0.f, 0.f, -3.f, 0.f), body->getId());
    CHECK(std::fabs(world->getRayHitY() - 2.f) < 0.001f);

    CHECK_THROWS((terrain->setHeightFieldRegion(3, 0, 1, 1, {0.f}), false));
    CHECK_THROWS((terrain->setHeightFieldRegion(0, 0, 2, 2, {0.f}), false));
    CHECK_THROWS((terrain->setHeightFieldHeights(std::vector<float>(9, 5.f)), false));
    CHECK_THROWS((terrain->getHeightFieldHeight(3, 0), false));
    CHECK_THROWS((terrain->setLocalPosition(1.f, 0.f, 0.f), false));
    CHECK(std::fabs(terrain->getHeightFieldHeight(1, 1) - 2.f) < 1e-6f);

    body->setPosition(4.f, 1.f, 5.f);
    CHECK_EQ(world->rayCast(5.f, 5.f, 6.f, 5.f, -2.f, 6.f), body->getId());
    CHECK(std::fabs(world->getRayHitY() - 3.f) < 0.001f);
    CHECK_THROWS((body->setType("kinematic"), false));
}

TEST_CASE("box3d.shape.heightFieldSupportsTerrainContactAndStaticValidation") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D *ground = world->newBody("static", -1.f, 0.f, -1.f);
    const std::vector<float> heights(9, 0.f);
    ground->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -1.f, 1.f);
    Body3D *ball = world->newBody("dynamic", 0.f, 3.f, 0.f);
    ball->newSphereShape(0.25f);
    for (int i = 0; i < 120; ++i) world->update(1.f / 60.f);
    CHECK_GT(ball->getY(), 0.2f);
    CHECK_LT(ball->getY(), 0.35f);

    Body3D *dynamicBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    CHECK_THROWS((dynamicBody->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -1.f, 1.f),
                  false));
    CHECK_THROWS((ground->newHeightFieldShape(1, 3, 1.f, 1.f, heights, -1.f, 1.f), false));
    CHECK_THROWS((ground->newHeightFieldShape(3, 3, 0.f, 1.f, heights, -1.f, 1.f), false));
    CHECK_THROWS((ground->newHeightFieldShape(3, 3, 1.f, 1.f, heights, 2.f, 1.f), false));
}

TEST_CASE("box3d.query.rayAndShapeCastExposeMeshAndHeightFieldTriangleIndex") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    const std::vector<float> meshVertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f,
                                           1.f, 0.f,  1.f, -1.f, 0.f, 1.f};
    const std::vector<int32_t> meshIndices{0, 2, 1, 0, 3, 2};
    Body3D *meshBody = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *meshShape = meshBody->newTriangleMeshShape(meshVertices, meshIndices);
    meshShape->setMaterialId(0x12345678);

    CHECK_EQ(world->rayCast(-0.5f, 2.f, -0.5f, -0.5f, -2.f, -0.5f), meshBody->getId());
    CHECK_GE(world->getRayHitTriangleIndex(), 0);
    CHECK_LT(world->getRayHitTriangleIndex(), 2);
    CHECK_EQ(world->getRayResultTriangleIndex(0), world->getRayHitTriangleIndex());
    CHECK_EQ(world->getRayHitMaterialId(), 0x12345678);
    CHECK_EQ(world->getRayResultMaterialId(0), 0x12345678);
    CHECK_EQ(world->castSphere(-0.5f, 2.f, -0.5f, 0.1f, 0.f, -4.f, 0.f),
             meshBody->getId());
    CHECK_GE(world->getShapeCastTriangleIndex(), 0);
    CHECK_LT(world->getShapeCastTriangleIndex(), 2);
    CHECK_EQ(world->getShapeCastResultTriangleIndex(0),
             world->getShapeCastTriangleIndex());
    CHECK_EQ(world->getShapeCastMaterialId(), 0x12345678);
    CHECK_EQ(world->getShapeCastResultMaterialId(0), 0x12345678);

    Body3D *terrainBody = world->newBody("static", 10.f, 0.f, 0.f);
    terrainBody->newHeightFieldShape(3, 3, 1.f, 1.f, std::vector<float>(9, 0.f), -1.f,
                                     1.f);
    CHECK_EQ(world->rayCast(10.25f, 2.f, 0.25f, 10.25f, -2.f, 0.25f),
             terrainBody->getId());
    CHECK_GE(world->getRayHitTriangleIndex(), 0);
    CHECK_LT(world->getRayHitTriangleIndex(), 8);
    CHECK_EQ(world->castSphere(10.25f, 2.f, 0.25f, 0.1f, 0.f, -4.f, 0.f),
             terrainBody->getId());
    CHECK_GE(world->getShapeCastTriangleIndex(), 0);
    CHECK_LT(world->getShapeCastTriangleIndex(), 8);

    Body3D *boxBody = world->newBody("static", 20.f, 0.f, 0.f);
    boxBody->newBoxShape(2.f, 0.5f, 2.f);
    CHECK_EQ(world->rayCast(20.f, 2.f, 0.f, 20.f, -2.f, 0.f), boxBody->getId());
    CHECK_EQ(world->getRayHitTriangleIndex(), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 0);
    CHECK_EQ(world->castSphere(20.f, 2.f, 0.f, 0.1f, 0.f, -4.f, 0.f), boxBody->getId());
    CHECK_EQ(world->getShapeCastTriangleIndex(), -1);
    CHECK_EQ(world->getShapeCastMaterialId(), 0);

    CHECK_EQ(world->rayCast(30.f, 2.f, 0.f, 30.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitTriangleIndex(), -1);
    CHECK_EQ(world->castSphere(30.f, 2.f, 0.f, 0.1f, 0.f, -4.f, 0.f), -1);
    CHECK_EQ(world->getShapeCastTriangleIndex(), -1);
    CHECK_THROWS((world->getRayResultTriangleIndex(0), false));
    CHECK_THROWS((world->getShapeCastResultTriangleIndex(0), false));
}

TEST_CASE("box3d.shape.triangleMeshPerTriangleMaterialsDriveQueriesAndSurviveRebuild") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    const std::vector<float> vertices{
        -4.f, 0.f, -1.f, -2.f, 0.f, -1.f, -3.f, 0.f, 1.f,
         2.f, 0.f, -1.f,  4.f, 0.f, -1.f,  3.f, 0.f, 1.f};
    const std::vector<int32_t> indices{0, 2, 1, 3, 5, 4};
    Shape3D *mesh = world->newBody("static", 0.f, 0.f, 0.f)
                            ->newTriangleMeshShape(vertices, indices, false);
    mesh->setTriangleMeshMaterialIndices({0, 1});
    CHECK_EQ(mesh->getTriangleMeshMaterialCount(), 2);
    CHECK_EQ(mesh->getTriangleMeshMaterialIndex(0), 0);
    CHECK_EQ(mesh->getTriangleMeshMaterialIndex(1), 1);
    mesh->setTriangleMeshMaterial(0, 0.1f, 0.2f, 0.01f, 0.f, 0.f, 0.f, 101,
                                  "minimum", "average");
    mesh->setTriangleMeshMaterial(1, 0.9f, 0.8f, 0.03f, 1.f, 0.f, 0.f, 202,
                                  "maximum", "multiply");
    CHECK(std::fabs(mesh->getTriangleMeshMaterialFriction(1) - 0.9f) < 0.001f);
    CHECK(std::fabs(mesh->getTriangleMeshMaterialRestitution(0) - 0.2f) < 0.001f);
    CHECK(std::fabs(mesh->getTriangleMeshMaterialRollingResistance(1) - 0.03f) < 0.001f);
    CHECK_EQ(mesh->getTriangleMeshMaterialId(0), 101);
    CHECK_EQ(mesh->getTriangleMeshMaterialId(1), 202);

    CHECK_NE(world->rayCast(-3.f, 2.f, 0.f, -3.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 101);
    CHECK_NE(world->rayCast(3.f, 2.f, 0.f, 3.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 202);
    CHECK_NE(world->castSphere(3.f, 2.f, 0.f, 0.1f, 0.f, -4.f, 0.f), -1);
    CHECK_EQ(world->getShapeCastMaterialId(), 202);

    mesh->setSensor(true);
    mesh->setSensor(false);
    CHECK_EQ(mesh->getTriangleMeshMaterialCount(), 2);
    CHECK_EQ(mesh->getTriangleMeshMaterialId(1), 202);
    CHECK_NE(world->rayCast(3.f, 2.f, 0.f, 3.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 202);

    CHECK_THROWS((mesh->setTriangleMeshMaterialIndices({0}), false));
    CHECK_THROWS((mesh->setTriangleMeshMaterialIndices({0, 255}), false));
    CHECK_THROWS((mesh->setTriangleMeshMaterial(2, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                0, "default", "default"), false));
    CHECK_THROWS((mesh->getTriangleMeshMaterialIndex(2), false));
}

TEST_CASE("box3d.query.ignoreBodyAndShapeAppliesAcrossQueryTypes") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *self = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *selfFront = self->newSphereShape(1.f);
    Shape3D *selfRear = self->newSphereShape(0.75f);
    selfRear->setLocalPosition(2.f, 0.f, 0.f);
    Body3D *target = world->newBody("static", 6.f, 0.f, 0.f);
    Shape3D *targetShape = target->newSphereShape(1.f);

    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), self->getId());
    CHECK_EQ(world->getRayHitShapeId(), selfFront->getId());

    world->setQueryIgnoredShapeId(selfFront->getId());
    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), self->getId());
    CHECK_EQ(world->getRayHitShapeId(), selfRear->getId());

    world->setQueryIgnoredBodyId(self->getId());
    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), target->getId());
    CHECK_EQ(world->getRayHitShapeId(), targetShape->getId());
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 3.f), 0);
    CHECK_EQ(world->castSphere(-3.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f), target->getId());
    CHECK_EQ(world->closestPoint(0.f, 0.f, 0.f, 10.f), target->getId());

    CHECK(world->moveCapsule(-3.f, -0.5f, 0.f, -3.f, 0.5f, 0.f, 0.25f, 12.f, 0.f,
                             0.f));
    CHECK_GT(world->getMoverDeltaX(), 1.f);
    CHECK_LT(world->getMoverDeltaX(), 12.f);

    CHECK_EQ(world->getQueryIgnoredBodyId(), self->getId());
    CHECK_EQ(world->getQueryIgnoredShapeId(), selfFront->getId());
    world->clearQueryIgnores();
    CHECK_EQ(world->getQueryIgnoredBodyId(), -1);
    CHECK_EQ(world->getQueryIgnoredShapeId(), -1);
    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), self->getId());
    CHECK_THROWS((world->setQueryIgnoredBodyId(0), false));
    CHECK_THROWS((world->setQueryIgnoredShapeId(-2), false));
}

TEST_CASE("box3d.events.contactCarriesStableShapeIdentity") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *wall = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *wallShape = wall->newBoxShape(2.f, 2.f, 2.f);
    wallShape->setTag(3101);
    Body3D *actor = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *actorShape = actor->newSphereShape(0.5f);
    actorShape->setTag(3102);

    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getBeginContactCount(), 1);
    CHECK_EQ(world->getBeginContactShapeAId(0), wallShape->getId());
    CHECK_EQ(world->getBeginContactShapeBId(0), actorShape->getId());
    CHECK_EQ(world->getBeginContactBodyAId(0), wall->getId());
    CHECK_EQ(world->getBeginContactBodyBId(0), actor->getId());
    CHECK_EQ(world->getBeginContactShapeATag(0), 3101);
    CHECK_EQ(world->getBeginContactShapeBTag(0), 3102);

    actor->setPosition(10.f, 0.f, 0.f);
    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getEndContactCount(), 1);
    CHECK_EQ(world->getEndContactShapeAId(0), wallShape->getId());
    CHECK_EQ(world->getEndContactShapeBId(0), actorShape->getId());
    CHECK_EQ(world->getBeginContactCount(), 0);
    world->clearContactEvents();
    CHECK_EQ(world->getEndContactCount(), 0);
}

TEST_CASE("box3d.events.sensorUsesDedicatedTriggerBuffer") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *zone = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *sensor = zone->newBoxShape(4.f, 4.f, 4.f);
    sensor->setTag(3201);
    sensor->setSensor(true);
    Body3D *actor = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *visitor = actor->newSphereShape(0.5f);
    visitor->setTag(3202);

    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    CHECK_EQ(world->getBeginContactCount(), 0);
    CHECK_EQ(world->getBeginTriggerSensorBodyId(0), zone->getId());
    CHECK_EQ(world->getBeginTriggerVisitorBodyId(0), actor->getId());
    CHECK_EQ(world->getBeginTriggerSensorShapeId(0), sensor->getId());
    CHECK_EQ(world->getBeginTriggerVisitorShapeId(0), visitor->getId());
    CHECK_EQ(world->getBeginTriggerSensorShapeTag(0), 3201);
    CHECK_EQ(world->getBeginTriggerVisitorShapeTag(0), 3202);

    actor->setPosition(10.f, 0.f, 0.f);
    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    CHECK_EQ(world->getEndTriggerSensorShapeId(0), sensor->getId());
    CHECK_EQ(world->getEndTriggerVisitorShapeId(0), visitor->getId());
    CHECK_EQ(world->getBeginTriggerCount(), 0);
}

TEST_CASE("box3d.events.endTriggerSurvivesSensorBackendRecreation") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *zone = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *sensor = zone->newBoxShape(4.f, 4.f, 4.f);
    sensor->setTag(3301);
    sensor->setSensor(true);
    Body3D *actor = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *visitor = actor->newSphereShape(0.5f);
    visitor->setTag(3302);

    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    const int stableSensorId = sensor->getId();
    sensor->setSensor(false);
    world->update(1.f / 60.f);

    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    CHECK_EQ(world->getEndTriggerSensorShapeId(0), stableSensorId);
    CHECK_EQ(world->getEndTriggerVisitorShapeId(0), visitor->getId());
    CHECK_EQ(world->getEndTriggerSensorShapeTag(0), 3301);
    CHECK_EQ(world->getEndTriggerVisitorShapeTag(0), 3302);
}

TEST_CASE("box3d.events.hitReportsPointNormalSpeedAndImpulse") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    world->setHitEventThreshold(1.f);
    CHECK(std::fabs(world->getHitEventThreshold() - 1.f) < 1e-5f);

    Body3D *ground = world->newBody("static", 0.f, -0.5f, 0.f);
    Shape3D *groundShape = ground->newBoxShape(10.f, 1.f, 10.f);
    groundShape->setTag(3401);
    Body3D *ball = world->newBody("dynamic", 0.f, 2.f, 0.f);
    Shape3D *ballShape = ball->newSphereShape(0.5f, 1.f, 0.2f, 0.f);
    ballShape->setTag(3402);
    ballShape->setHitEventsEnabled(true);
    CHECK(ballShape->areHitEventsEnabled());
    ball->setLinearVelocity(0.f, -30.f, 0.f);

    bool captured = false;
    for (int step = 0; step < 30 && !captured; ++step) {
        world->update(1.f / 60.f);
        captured = world->getHitCount() > 0;
    }
    REQUIRE(captured);
    CHECK_EQ(world->getHitShapeAId(0), groundShape->getId());
    CHECK_EQ(world->getHitShapeBId(0), ballShape->getId());
    CHECK_EQ(world->getHitShapeATag(0), 3401);
    CHECK_EQ(world->getHitShapeBTag(0), 3402);
    CHECK_EQ(world->getHitBodyAId(0), ground->getId());
    CHECK_EQ(world->getHitBodyBId(0), ball->getId());
    CHECK(std::fabs(world->getHitPointX(0)) < 0.1f);
    CHECK(std::fabs(world->getHitPointY(0)) < 0.2f);
    CHECK(std::fabs(world->getHitPointZ(0)) < 0.1f);
    CHECK_GT(world->getHitNormalY(0), 0.9f);
    CHECK_GT(world->getHitApproachSpeed(0), 1.f);
    CHECK_GT(world->getHitNormalImpulse(0), 0.f);
}

TEST_CASE("box3d.events.hitPreferenceSurvivesSensorToggle") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *shape = body->newSphereShape(0.5f);
    shape->setHitEventsEnabled(true);
    shape->setSensor(true);
    CHECK(shape->areHitEventsEnabled());
    shape->setSensor(false);
    CHECK(shape->areHitEventsEnabled());
    CHECK(b3Shape_AreHitEventsEnabled(shape->raw()));
}

TEST_CASE("box3d.contacts.bodyManifoldIsPersistentAndBodyRelative") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D *ground = world->newBody("static", 0.f, -0.5f, 0.f);
    Shape3D *groundShape = ground->newBoxShape(10.f, 1.f, 10.f);
    groundShape->setTag(3501);
    Body3D *ball = world->newBody("dynamic", 0.f, 2.f, 0.f);
    Shape3D *ballShape = ball->newSphereShape(0.5f, 1.f, 0.4f, 0.f);
    ballShape->setTag(3502);

    for (int step = 0; step < 120; ++step) world->update(1.f / 60.f);

    REQUIRE_GT(world->queryBodyContacts(ball->getId(), 16), 0);
    CHECK_EQ(world->getContactPointCount(), world->queryBodyContacts(ball->getId(), 16));
    CHECK_EQ(world->getContactPointShapeId(0), ballShape->getId());
    CHECK_EQ(world->getContactPointShapeTag(0), 3502);
    CHECK_EQ(world->getContactPointOtherBodyId(0), ground->getId());
    CHECK_EQ(world->getContactPointOtherShapeId(0), groundShape->getId());
    CHECK_EQ(world->getContactPointOtherShapeTag(0), 3501);
    CHECK_LT(world->getContactPointNormalY(0), -0.9f);
    CHECK(std::fabs(world->getContactPointNormalX(0)) < 0.1f);
    CHECK(std::fabs(world->getContactPointZ(0)) < 0.1f);
    CHECK(std::fabs(world->getContactPointY(0)) < 0.1f);
    CHECK_LT(world->getContactPointSeparation(0), 0.05f);
    CHECK_GE(world->getContactPointNormalImpulse(0), 0.f);
    CHECK_GT(world->getContactPointTotalNormalImpulse(0), 0.f);
    CHECK(world->isContactPointPersisted(0));

    REQUIRE_GT(world->queryBodyContacts(ground->getId(), 1), 0);
    CHECK_EQ(world->getContactPointCount(), 1);
    CHECK_EQ(world->getContactPointShapeId(0), groundShape->getId());
    CHECK_EQ(world->getContactPointOtherShapeId(0), ballShape->getId());
    CHECK_GT(world->getContactPointNormalY(0), 0.9f);

    ball->setPosition(0.f, 5.f, 0.f);
    world->update(1.f / 60.f);
    CHECK_EQ(world->queryBodyContacts(ball->getId(), 16), 0);
    CHECK_EQ(world->getContactPointCount(), 0);
    CHECK_THROWS((world->queryBodyContacts(ball->getId(), 0), false));
    CHECK_THROWS((world->getContactPointShapeId(0), false));
}

TEST_CASE("box3d.body.destroy") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *b = world->newBody("dynamic", 1.f, 2.f, 3.f);
    b->newBoxShape(0.5f, 0.5f, 0.5f);
    int id = b->getId();
    CHECK_GT(id, 0);
    b->destroy();
    CHECK(std::fabs(b->getX()) < 0.001f);
    delete b;
}

TEST_CASE("box3d.query.rayCastAndAABB") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *a = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *sa = a->newBoxShape(2.f, 2.f, 2.f);
    sa->setTag(101);
    Body3D *b = world->newBody("static", 5.f, 0.f, 0.f);
    b->newSphereShape(1.f);

    CHECK(sa->testPoint(0.f, 0.f, 0.f));
    CHECK(!sa->testPoint(10.f, 10.f, 10.f));

    int hit = world->rayCast(-5.f, 0.f, 0.f, 10.f, 0.f, 0.f);
    CHECK_EQ(hit, a->getId());
    CHECK(world->hasRayHit());
    CHECK_EQ(world->getRayHitShapeId(), sa->getId());
    CHECK_EQ(world->getRayHitShapeTag(), 101);
    CHECK(std::fabs(world->getRayHitY()) < 0.5f);

    int count = world->queryAABB(-2.f, -2.f, -2.f, 2.f, 2.f, 2.f);
    CHECK_GE(count, 1);
    bool foundA = false;
    for (int i = 0; i < world->getQueryCount(); ++i) {
        if (world->getQueryBodyId(i) == a->getId()) foundA = true;
    }
    CHECK(foundA);
    CHECK_EQ(world->getQueryShapeCount(), 1);
    CHECK_EQ(world->getQueryShapeId(0), sa->getId());
}

TEST_CASE("box3d.query.rayCastAllIsSortedBoundedAndCompatible") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *nearBody = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *nearShape = nearBody->newSphereShape(0.5f);
    nearShape->setTag(4101);
    Body3D *middleBody = world->newBody("static", 3.f, 0.f, 0.f);
    Shape3D *middleShape = middleBody->newSphereShape(0.5f);
    middleShape->setTag(4102);
    Body3D *farBody = world->newBody("static", 6.f, 0.f, 0.f);
    Shape3D *farShape = farBody->newSphereShape(0.5f);
    farShape->setTag(4103);

    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f, 8), 3);
    CHECK_EQ(world->getRayResultCount(), 3);
    CHECK_EQ(world->getRayResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getRayResultBodyId(1), middleBody->getId());
    CHECK_EQ(world->getRayResultBodyId(2), farBody->getId());
    CHECK_EQ(world->getRayResultShapeId(0), nearShape->getId());
    CHECK_EQ(world->getRayResultShapeTag(1), 4102);
    CHECK_LT(world->getRayResultFraction(0), world->getRayResultFraction(1));
    CHECK_LT(world->getRayResultFraction(1), world->getRayResultFraction(2));
    CHECK_LT(world->getRayResultNormalX(0), -0.9f);
    CHECK(std::fabs(world->getRayResultY(0)) < 1e-5f);

    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f, 2), 2);
    CHECK_EQ(world->getRayResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getRayResultBodyId(1), middleBody->getId());
    CHECK_EQ(world->getRayHitBodyId(), nearBody->getId());

    world->setQueryIgnoredBodyId(nearBody->getId());
    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f, 8), 2);
    CHECK_EQ(world->getRayResultBodyId(0), middleBody->getId());
    CHECK_EQ(world->getRayResultShapeId(1), farShape->getId());
    world->clearQueryIgnores();

    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f), nearBody->getId());
    CHECK_EQ(world->getRayResultCount(), 1);
    CHECK_EQ(world->getRayResultShapeId(0), nearShape->getId());
    CHECK_THROWS((world->rayCastAll(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0), false));
    CHECK_EQ(world->getRayResultCount(), 0);
    CHECK_THROWS((world->getRayResultBodyId(0), false));
}

TEST_CASE("box3d.query.rayCastAllTieUsesStableShapeId") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *first = body->newSphereShape(1.f);
    Shape3D *second = body->newSphereShape(1.f);
    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 3.f, 0.f, 0.f, 2), 2);
    CHECK_LT(first->getId(), second->getId());
    CHECK_EQ(world->getRayResultShapeId(0), first->getId());
    CHECK_EQ(world->getRayResultShapeId(1), second->getId());
    CHECK(std::fabs(world->getRayResultFraction(0) - world->getRayResultFraction(1)) < 1e-6f);
}

TEST_CASE("box3d.query.reportsEveryShapeOnOneBody") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *body = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *sphere = body->newSphereShape(0.5f);
    Shape3D *box = body->newBoxShape(2.f, 2.f, 2.f);
    sphere->setTag(11);
    box->setTag(22);

    CHECK_NE(sphere->getId(), box->getId());
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.1f), 1);  // Unique bodies.
    CHECK_EQ(world->getQueryCount(), 1);
    CHECK_EQ(world->getQueryShapeCount(), 2);
    CHECK_EQ(world->getQueryShapeId(0), std::min(sphere->getId(), box->getId()));
    CHECK_EQ(world->getQueryShapeId(1), std::max(sphere->getId(), box->getId()));
    const int firstTag = sphere->getId() < box->getId() ? 11 : 22;
    CHECK_EQ(world->getQueryShapeTag(0), firstTag);
    CHECK_EQ(world->getQueryShapeTag(1), firstTag == 11 ? 22 : 11);
    CHECK_THROWS((world->getQueryShapeId(2), false));
}

TEST_CASE("box3d.body.capsuleAndRotation") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *cap = world->newBody("dynamic", 0.f, 1.f, 0.f);
    cap->newCapsuleShape(1.f, 0.25f, 1.f);
    cap->setRotation(0.f, 0.f, 0.f, 1.f);
    CHECK(std::fabs(cap->getRotW() - 1.f) < 0.001f);
    cap->setFixedRotation(true);
    CHECK(cap->isFixedRotation());
}

TEST_CASE("box3d.query.sphereAndCapsuleOverlap") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *left = world->newBody("static", -2.f, 0.f, 0.f);
    left->newSphereShape(0.5f);
    Body3D *middle = world->newBody("static", 0.f, 0.f, 0.f);
    middle->newBoxShape(0.5f, 0.5f, 0.5f);
    Body3D *right = world->newBody("static", 2.f, 0.f, 0.f);
    right->newCapsuleShape(1.f, 0.25f);

    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.1f), 1);
    CHECK_EQ(world->getQueryBodyId(0), middle->getId());

    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 3);
    CHECK_EQ(world->getQueryBodyId(0), left->getId());
    CHECK_EQ(world->getQueryBodyId(1), middle->getId());
    CHECK_EQ(world->getQueryBodyId(2), right->getId());

    CHECK_EQ(world->queryCapsule(-2.f, 3.f, 0.f, 2.f, 3.f, 0.f, 0.1f), 0);
}

TEST_CASE("box3d.query.collisionLayerFilter") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Shape3D *layer1 = world->newBody("static", -1.f, 0.f, 0.f)->newSphereShape(0.5f);
    Shape3D *layer2 = world->newBody("static", 1.f, 0.f, 0.f)->newSphereShape(0.5f);
    layer1->setCategoryBits(1);
    layer2->setCategoryBits(2);

    world->setQueryFilter(4, 1);
    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 1);
    CHECK_EQ(world->getQueryMaskBits(), 1);

    world->setQueryFilter(4, 2);
    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 1);

    world->resetQueryFilter();
    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 2);
}

TEST_CASE("box3d.query.sphereAndCapsuleCast") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *wall = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *wallShape = wall->newBoxShape(1.f, 4.f, 4.f);
    wallShape->setTag(77);

    CHECK_EQ(world->castSphere(-5.f, 0.f, 0.f, 0.5f, 10.f, 0.f, 0.f), wall->getId());
    CHECK(world->hasShapeCastHit());
    CHECK_EQ(world->getShapeCastShapeId(), wallShape->getId());
    CHECK_EQ(world->getShapeCastShapeTag(), 77);
    CHECK(world->getShapeCastFraction() > 0.35f);
    CHECK(world->getShapeCastFraction() < 0.45f);
    CHECK(world->getShapeCastNormalX() < -0.9f);

    CHECK_EQ(world->castCapsule(-5.f, -1.f, 0.f, -5.f, 1.f, 0.f, 0.25f, 10.f, 0.f, 0.f),
             wall->getId());
    CHECK(world->getShapeCastFraction() > 0.35f);
    CHECK(world->getShapeCastFraction() < 0.45f);

    CHECK_EQ(world->castSphere(-5.f, 5.f, 0.f, 0.5f, 10.f, 0.f, 0.f), -1);
    CHECK(!world->hasShapeCastHit());
    CHECK_EQ(world->getShapeCastShapeId(), -1);
    CHECK_EQ(world->getShapeCastShapeTag(), 0);
}

TEST_CASE("box3d.query.shapeCastAllIsSortedBoundedAndSharedByPrimitives") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D *nearBody = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D *nearShape = nearBody->newBoxShape(0.5f, 4.f, 4.f);
    nearShape->setTag(4201);
    Body3D *middleBody = world->newBody("static", 3.f, 0.f, 0.f);
    Shape3D *middleShape = middleBody->newBoxShape(0.5f, 4.f, 4.f);
    middleShape->setTag(4202);
    Body3D *farBody = world->newBody("static", 6.f, 0.f, 0.f);
    Shape3D *farShape = farBody->newBoxShape(0.5f, 4.f, 4.f);
    farShape->setTag(4203);

    REQUIRE_EQ(world->castSphereAll(-4.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f, 8), 3);
    CHECK_EQ(world->getShapeCastResultCount(), 3);
    CHECK_EQ(world->getShapeCastResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getShapeCastResultBodyId(1), middleBody->getId());
    CHECK_EQ(world->getShapeCastResultBodyId(2), farBody->getId());
    CHECK_EQ(world->getShapeCastResultShapeId(0), nearShape->getId());
    CHECK_EQ(world->getShapeCastResultShapeTag(1), 4202);
    CHECK_LT(world->getShapeCastResultFraction(0), world->getShapeCastResultFraction(1));
    CHECK_LT(world->getShapeCastResultFraction(1), world->getShapeCastResultFraction(2));
    CHECK_LT(world->getShapeCastResultNormalX(0), -0.9f);
    CHECK(std::fabs(world->getShapeCastResultY(0)) < 1e-5f);
    CHECK_EQ(world->getShapeCastBodyId(), nearBody->getId());

    REQUIRE_EQ(world->castCapsuleAll(-4.f, -0.5f, 0.f, -4.f, 0.5f, 0.f, 0.2f, 12.f,
                                     0.f, 0.f, 2),
               2);
    CHECK_EQ(world->getShapeCastResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getShapeCastResultBodyId(1), middleBody->getId());

    world->setQueryIgnoredBodyId(nearBody->getId());
    REQUIRE_EQ(world->castBoxAll(-4.f, 0.f, 0.f, 0.4f, 0.4f, 0.4f, 0.f, 0.f, 0.f, 1.f,
                                 12.f, 0.f, 0.f, 8),
               2);
    CHECK_EQ(world->getShapeCastResultBodyId(0), middleBody->getId());
    CHECK_EQ(world->getShapeCastResultShapeId(1), farShape->getId());
    world->clearQueryIgnores();

    CHECK_EQ(world->castSphere(-4.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f), nearBody->getId());
    CHECK_EQ(world->getShapeCastResultCount(), 1);
    CHECK_EQ(world->getShapeCastResultShapeId(0), nearShape->getId());
    CHECK_THROWS((world->castSphereAll(-4.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f, 0), false));
    CHECK_EQ(world->getShapeCastResultCount(), 0);
    CHECK_THROWS((world->getShapeCastResultBodyId(0), false));
}

TEST_CASE("box3d.query.orientedBoxOverlap") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *target = world->newBody("static", 1.2f, 0.f, 0.f);
    target->newSphereShape(0.1f);

    CHECK_EQ(world->queryBox(0.f, 0.f, 0.f, 3.f, 0.4f, 0.4f, 0.f, 0.f, 0.f, 1.f), 1);
    CHECK_EQ(world->getQueryBodyId(0), target->getId());

    constexpr float sin45 = 0.70710678f;
    // A 90-degree yaw turns the long local X axis onto world Z, so it no longer reaches x=1.2.
    CHECK_EQ(world->queryBox(0.f, 0.f, 0.f, 3.f, 0.4f, 0.4f, 0.f, sin45, 0.f, sin45),
             0);
    CHECK_THROWS((world->queryBox(0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f),
                  false));
    CHECK_THROWS((world->queryBox(0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1.f),
                  false));
}

TEST_CASE("box3d.query.orientedBoxCast") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *wall = world->newBody("static", 2.f, 0.f, 0.f);
    wall->newBoxShape(0.5f, 4.f, 4.f);

    CHECK_EQ(world->castBox(-1.f, 0.f, 0.f, 2.f, 0.4f, 0.4f, 0.f, 0.f, 0.f, 1.f, 4.f,
                            0.f, 0.f),
             wall->getId());
    const float identityFraction = world->getShapeCastFraction();
    CHECK(std::fabs(identityFraction - 0.4375f) < 0.01f);
    CHECK(world->getShapeCastNormalX() < -0.99f);

    constexpr float sin45 = 0.70710678f;
    CHECK_EQ(world->castBox(-1.f, 0.f, 0.f, 2.f, 0.4f, 0.4f, 0.f, sin45, 0.f, sin45,
                            4.f, 0.f, 0.f),
             wall->getId());
    CHECK(world->getShapeCastFraction() > identityFraction + 0.15f);
    CHECK(world->getShapeCastFraction() < 0.65f);
}

TEST_CASE("box3d.query.closestPointAndLayerFilter") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *sphere = world->newBody("static", 2.f, 0.f, 0.f);
    Shape3D *sphereShape = sphere->newSphereShape(0.5f);
    sphereShape->setCategoryBits(2);
    sphereShape->setTag(201);
    Body3D *box = world->newBody("static", -3.f, 0.f, 0.f);
    Shape3D *boxShape = box->newBoxShape(1.f, 2.f, 2.f);
    boxShape->setCategoryBits(4);
    boxShape->setTag(202);

    CHECK_EQ(world->closestPoint(0.f, 0.f, 0.f, 10.f), sphere->getId());
    CHECK(world->hasClosestPoint());
    CHECK_EQ(world->getClosestBodyId(), sphere->getId());
    CHECK_EQ(world->getClosestShapeId(), sphereShape->getId());
    CHECK_EQ(world->getClosestShapeTag(), 201);
    CHECK(std::fabs(world->getClosestX() - 1.5f) < 1e-5f);
    CHECK(std::fabs(world->getClosestDistance() - 1.5f) < 1e-5f);
    CHECK(world->getClosestNormalX() < -0.999f);
    CHECK(std::fabs(world->getClosestNormalY()) < 1e-5f);

    world->setQueryFilter(1, 4);
    CHECK_EQ(world->closestPoint(0.f, 0.f, 0.f, 10.f), box->getId());
    CHECK_EQ(world->getClosestShapeId(), boxShape->getId());
    CHECK_EQ(world->getClosestShapeTag(), 202);
    CHECK(std::fabs(world->getClosestX() + 2.5f) < 1e-5f);
    CHECK(std::fabs(world->getClosestDistance() - 2.5f) < 1e-5f);
    CHECK(world->getClosestNormalX() > 0.999f);

    world->resetQueryFilter();
    CHECK_EQ(world->closestPoint(2.f, 0.f, 0.f, 1.f), sphere->getId());
    CHECK(std::fabs(world->getClosestDistance()) < 1e-6f);
    CHECK(std::fabs(world->getClosestNormalX()) < 1e-6f);
    CHECK_EQ(world->closestPoint(0.f, 5.f, 0.f, 1.f), -1);
    CHECK(!world->hasClosestPoint());
    CHECK_EQ(world->getClosestBodyId(), -1);
    CHECK_EQ(world->getClosestShapeId(), -1);
    CHECK_EQ(world->getClosestShapeTag(), 0);
    CHECK_THROWS((world->closestPoint(0.f, 0.f, 0.f, -1.f), false));
}

TEST_CASE("box3d.mover.capsuleSlidesAlongWall") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D *wall = world->newBody("static", 0.f, 0.f, 0.f);
    wall->newBoxShape(1.f, 6.f, 10.f);

    CHECK(world->moveCapsule(-2.f, -1.f, 0.f, -2.f, 1.f, 0.f, 0.25f, 4.f, 0.f, 2.f));
    CHECK(world->getMoverDeltaX() > 1.15f);
    CHECK(world->getMoverDeltaX() < 1.3f);
    CHECK(world->getMoverDeltaZ() > 1.9f);
    CHECK(world->getMoverNormalX() < -0.9f);
    CHECK_GT(world->getMoverPlaneCount(), 0);
    CHECK_GE(world->getMoverIterations(), 2);
}

TEST_CASE("box3d.mover.capsuleStopsAtCornerAndMovesFreely") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    world->newBody("static", 0.f, 0.f, -2.f)->newBoxShape(1.f, 6.f, 6.f);
    world->newBody("static", -2.f, 0.f, 0.f)->newBoxShape(6.f, 6.f, 1.f);

    CHECK(world->moveCapsule(-2.f, -1.f, -2.f, -2.f, 1.f, -2.f, 0.25f, 4.f, 0.f, 4.f));
    CHECK(world->getMoverDeltaX() > 1.15f);
    CHECK(world->getMoverDeltaX() < 1.3f);
    CHECK(world->getMoverDeltaZ() > 1.15f);
    CHECK(world->getMoverDeltaZ() < 1.3f);

    CHECK(!world->moveCapsule(-5.f, -1.f, -5.f, -5.f, 1.f, -5.f, 0.25f, -1.f, 0.f,
                              -2.f));
    CHECK(std::fabs(world->getMoverDeltaX() + 1.f) < 1e-5f);
    CHECK(std::fabs(world->getMoverDeltaZ() + 2.f) < 1e-5f);
}

TEST_CASE("box3d.mover.recoversInitialPenetration") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    world->newBody("static", 0.f, 0.f, 0.f)->newBoxShape(1.f, 6.f, 6.f);

    // The wall face is x=-0.5 and the capsule reaches to x=-0.35: 0.15m penetration.
    CHECK(world->moveCapsule(-0.6f, -1.f, 0.f, -0.6f, 1.f, 0.f, 0.25f, 0.f, 0.f, 0.f));
    CHECK(world->getMoverDeltaX() < -0.14f);
    CHECK(world->getMoverDeltaX() > -0.17f);
    CHECK(world->getMoverNormalX() < -0.9f);
    CHECK_GT(world->getMoverPlaneCount(), 0);
}

TEST_CASE("box3d.mover.groundAndSlopeClassification") {
    auto *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    world->newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(10.f, 1.f, 10.f);

    world->setMoverUp(0.f, 1.f, 1.f); // normalized internally; floor dot is sqrt(1/2)
    world->setMoverSlopeLimit(40.f);
    CHECK(world->moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(!world->isMoverGrounded());
    CHECK(world->getMoverGroundDot() > 0.70f);
    CHECK(world->getMoverGroundDot() < 0.72f);

    world->setMoverSlopeLimit(50.f);
    CHECK(world->moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(world->isMoverGrounded());
}

TEST_CASE("box3d.sdf.interpolationNormalAndSphere") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    // Exact SDF for the plane y=0. Trilinear interpolation reproduces it exactly.
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 2));

    CHECK(std::fabs(field.sample(0.25f, 0.5f, -0.25f) - 0.5f) < 1e-5f);
    field.sampleNormal(0.f, 0.f, 0.f);
    CHECK(std::fabs(field.getNormalX()) < 1e-5f);
    CHECK(std::fabs(field.getNormalY() - 1.f) < 1e-5f);
    CHECK(std::fabs(field.getNormalZ()) < 1e-5f);

    CHECK(field.checkSphere(0.f, 0.4f, 0.f, 0.5f));
    CHECK_LT(field.getCollisionDistance(), 0.f);
    CHECK(std::fabs(field.getPenetrationDepth() - 0.1f) < 1e-5f);
    CHECK(std::fabs(field.getSurfaceY()) < 1e-5f);
    CHECK(std::fabs(field.getShapeContactY() + 0.1f) < 1e-5f);
    CHECK(!field.checkSphere(0.f, 1.f, 0.f, 0.25f));
}

TEST_CASE("box3d.sdf.boundaryNormalsUseOneSidedGradient") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 2));

    field.sampleNormal(0.f, -2.f, 0.f);
    CHECK(std::fabs(field.getNormalX()) < 1e-5f);
    CHECK(field.getNormalY() > 0.999f);
    CHECK(std::fabs(field.getNormalZ()) < 1e-5f);
    field.sampleNormal(0.f, 2.f, 0.f);
    CHECK(field.getNormalY() > 0.999f);

    // Outside points have no meaningful field gradient and use the documented safe fallback.
    field.sampleNormal(0.f, 2.01f, 0.f);
    CHECK(std::fabs(field.getNormalX()) < 1e-5f);
    CHECK(field.getNormalY() > 0.999f);
    CHECK(std::fabs(field.getNormalZ()) < 1e-5f);
}

TEST_CASE("box3d.sdf.bulkDataMetadataAndAtomicValidation") {
    DistanceField3D field(3, 2, 2, 0.5f, -1.f, 2.f, 4.f, 99.f);
    CHECK_EQ(field.getRevision(), 0u);
    CHECK_EQ(field.getWidth(), 3);
    CHECK_EQ(field.getHeight(), 2);
    CHECK_EQ(field.getDepth(), 2);
    CHECK_EQ(field.getSampleCount(), 12);
    CHECK(std::fabs(field.getCellSize() - 0.5f) < 1e-6f);
    CHECK(std::fabs(field.getOriginX() + 1.f) < 1e-6f);
    CHECK(std::fabs(field.getOriginY() - 2.f) < 1e-6f);
    CHECK(std::fabs(field.getOriginZ() - 4.f) < 1e-6f);
    CHECK(std::fabs(field.getOutsideDistance() - 99.f) < 1e-6f);

    std::vector<float> samples(12);
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<float>(i);
    field.setDistances(samples);
    CHECK_EQ(field.getRevision(), 1u);
    CHECK(std::fabs(field.getDistance(2, 0, 0) - 2.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 1, 0) - 3.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 0, 1) - 6.f) < 1e-6f);

    auto invalid = samples;
    invalid[4] = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS((field.setDistances(invalid), false));
    CHECK_EQ(field.getRevision(), 1u);
    CHECK(std::fabs(field.getDistance(1, 1, 0) - 4.f) < 1e-6f);
    CHECK_THROWS((field.setDistances(std::vector<float>(11, 0.f)), false));

    field.fill(-2.f);
    CHECK_EQ(field.getRevision(), 2u);
    CHECK(std::fabs(field.getDistance(2, 1, 1) + 2.f) < 1e-6f);
    CHECK_THROWS((field.fill(std::numeric_limits<float>::infinity()), false));
    CHECK(std::fabs(field.getDistance(2, 1, 1) + 2.f) < 1e-6f);

    const std::vector<float> region{10.f, 11.f, 12.f, 13.f, 14.f, 15.f, 16.f, 17.f};
    field.setDistanceRegion(1, 0, 0, 2, 2, 2, region);
    CHECK_EQ(field.getRevision(), 3u);
    CHECK(std::fabs(field.getDistance(1, 0, 0) - 10.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(2, 0, 0) - 11.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(1, 1, 0) - 12.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(1, 0, 1) - 14.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(2, 1, 1) - 17.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 0, 0) + 2.f) < 1e-6f);

    auto invalidRegion = region;
    invalidRegion[2] = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS((field.setDistanceRegion(1, 0, 0, 2, 2, 2, invalidRegion), false));
    CHECK_THROWS((field.setDistanceRegion(2, 0, 0, 2, 2, 2, region), false));
    CHECK_EQ(field.getRevision(), 3u);
    CHECK(std::fabs(field.getDistance(1, 1, 0) - 12.f) < 1e-6f);

    field.fillRegion(0, 1, 0, 1, 1, 2, 7.f);
    CHECK_EQ(field.getRevision(), 4u);
    CHECK(std::fabs(field.getDistance(0, 1, 0) - 7.f) < 1e-6f);
    CHECK(std::fabs(field.getDistance(0, 1, 1) - 7.f) < 1e-6f);
    CHECK_THROWS((field.fillRegion(0, 0, 0, 0, 1, 1, 3.f), false));
    CHECK_EQ(field.getRevision(), 4u);
}

TEST_CASE("box3d.sdf.capsuleFindsInteriorCollision") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    // Sphere SDF centered at the origin.
    for (int z = 0; z < 7; ++z) {
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 7; ++x) {
                float px = static_cast<float>(x - 3);
                float py = static_cast<float>(y - 3);
                float pz = static_cast<float>(z - 3);
                field.setDistance(x, y, z, std::sqrt(px * px + py * py + pz * pz) - 1.f);
            }
        }
    }
    // Endpoints are outside, but the capsule segment crosses the solid center.
    CHECK(field.checkCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f));
    CHECK(std::fabs(field.getCollisionX()) < 0.51f);
    CHECK_LT(field.getCollisionDistance(), 0.f);
    CHECK(!field.checkCapsule(-2.f, 2.f, 0.f, 2.f, 2.f, 0.f, 0.1f));
}

TEST_CASE("box3d.sdf.sphereCastPreventsHighSpeedTunneling") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 2));

    CHECK(field.castSphere(0.f, 1.75f, 0.f, 0.25f, 0.f, -3.5f, 0.f));
    CHECK(!field.didCastStartInside());
    CHECK(std::fabs(field.getCastFraction() - (1.5f / 3.5f)) < 1e-4f);
    CHECK(std::fabs(field.getCastDistance() - 1.5f) < 1e-4f);
    CHECK(std::fabs(field.getCollisionY() - 0.25f) < 1e-4f);
    CHECK(field.getNormalY() > 0.999f);

    CHECK(!field.castSphere(0.f, 1.75f, 0.f, 0.25f, 3.f, 0.f, 0.f));
    CHECK(std::fabs(field.getCastFraction() - 1.f) < 1e-6f);
    CHECK(std::fabs(field.getCastDistance() - 3.f) < 1e-6f);
}

TEST_CASE("box3d.sdf.capsuleCastAndInitialOverlap") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 3));

    CHECK(field.castCapsule(-1.f, 2.f, 0.f, 1.f, 2.f, 0.f, 0.5f, 0.f, -4.f, 0.f));
    CHECK(!field.didCastStartInside());
    CHECK(std::fabs(field.getCastFraction() - 0.375f) < 1e-4f);
    CHECK(std::fabs(field.getCollisionY() - 0.5f) < 1e-4f);
    CHECK(field.getNormalY() > 0.999f);

    CHECK(field.castCapsule(-1.f, 0.25f, 0.f, 1.f, 0.25f, 0.f, 0.5f, 0.f, 2.f, 0.f));
    CHECK(field.didCastStartInside());
    CHECK(std::fabs(field.getCastFraction()) < 1e-6f);
    CHECK_LT(field.getCollisionDistance(), 0.f);
}

TEST_CASE("box3d.sdf.moverSlidesAlongWall") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(x - 3));

    CHECK(field.moveCapsule(1.5f, -1.f, 0.f, 1.5f, 1.f, 0.f, 0.25f, -2.f, 0.f, 1.f));
    CHECK(field.getMoverDeltaX() < -1.24f);
    CHECK(field.getMoverDeltaX() > -1.26f);
    CHECK(field.getMoverDeltaZ() > 0.99f);
    CHECK(field.getMoverNormalX() > 0.999f);
    CHECK_GT(field.getMoverIterations(), 1);

    CHECK(!field.moveCapsule(1.5f, -1.f, 0.f, 1.5f, 1.f, 0.f, 0.25f, 0.f, 0.f, 1.f));
    CHECK(std::fabs(field.getMoverDeltaZ() - 1.f) < 1e-5f);
}

TEST_CASE("box3d.sdf.moverRecoversInitialPenetration") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(x - 3));

    CHECK(field.moveCapsule(0.1f, -1.f, 0.f, 0.1f, 1.f, 0.f, 0.25f, 0.f, 0.f, 0.f));
    CHECK(field.getMoverDeltaX() > 0.15f);
    CHECK(field.getMoverDeltaX() < 0.152f);
    CHECK(field.getMoverNormalX() > 0.999f);
}

TEST_CASE("box3d.sdf.moverStopsAtTwoPlaneCorner") {
    DistanceField3D field(9, 7, 9, 0.5f, -2.f, -1.5f, -2.f, 100.f);
    for (int z = 0; z < 9; ++z) {
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 9; ++x) {
                const float worldX = -2.f + 0.5f * static_cast<float>(x);
                const float worldZ = -2.f + 0.5f * static_cast<float>(z);
                // Union of x<=0 and z<=0.5 half spaces.
                field.setDistance(x, y, z, std::min(worldX, worldZ - 0.5f));
            }
        }
    }

    CHECK(field.moveCapsule(1.5f, -0.5f, 2.f, 1.5f, 0.5f, 2.f, 0.25f, -2.f, 0.f,
                            -3.f));
    CHECK(field.getMoverDeltaX() < -1.24f);
    CHECK(field.getMoverDeltaX() > -1.26f);
    CHECK(field.getMoverDeltaZ() < -1.24f);
    CHECK(field.getMoverDeltaZ() > -1.26f);
    CHECK_GT(field.getMoverIterations(), 2);
}

TEST_CASE("box3d.sdf.moverGroundSlopeAndSkinConfiguration") {
    DistanceField3D field(7, 7, 7, 1.f, -3.f, -3.f, -3.f, 100.f);
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x)
                field.setDistance(x, y, z, static_cast<float>(y - 3));

    field.setMoverSkinWidth(0.02f);
    CHECK(std::fabs(field.getMoverSkinWidth() - 0.02f) < 1e-6f);
    CHECK_THROWS((field.setMoverSkinWidth(-0.01f), false));
    CHECK_THROWS((field.setMoverUp(0.f, 0.f, 0.f), false));

    field.setMoverUp(0.f, 1.f, 1.f);  // Normalized internally; floor dot is sqrt(1/2).
    field.setMoverSlopeLimit(40.f);
    CHECK(field.moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(!field.isMoverGrounded());
    CHECK(field.getMoverGroundDot() > 0.70f);
    CHECK(field.getMoverGroundDot() < 0.72f);
    CHECK(field.getMoverDeltaY() > -0.24f);
    CHECK(field.getMoverDeltaY() < -0.22f);  // Stops at radius + configured skin.

    field.setMoverSlopeLimit(50.f);
    CHECK(field.moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(field.isMoverGrounded());
}

TEST_CASE("box3d.sdf.moverGroundSnapAndLedge") {
    DistanceField3D field(9, 9, 9, 0.5f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < 9; ++z) {
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 9; ++x) {
                const float worldX = -2.f + 0.5f * static_cast<float>(x);
                const float worldY = -2.f + 0.5f * static_cast<float>(y);
                // Ground exists only on x<=0, leaving a sharp ledge to the right.
                field.setDistance(x, y, z, worldX <= 0.f ? worldY : 100.f);
            }
        }
    }

    field.setMoverSkinWidth(0.02f);
    field.setMoverGroundSnap(0.5f);
    CHECK(std::fabs(field.getMoverGroundSnap() - 0.5f) < 1e-6f);
    CHECK_THROWS((field.setMoverGroundSnap(-0.1f), false));

    // Starts 0.25m above the configured skin and is pulled down onto walkable ground.
    CHECK(field.moveCapsule(-1.f, 0.5f, 0.f, -1.f, 1.5f, 0.f, 0.25f, 0.2f, 0.f, 0.f));
    CHECK(field.isMoverGrounded());
    CHECK(field.getMoverDeltaY() < -0.22f);
    CHECK(field.getMoverDeltaY() > -0.24f);
    CHECK(std::fabs(field.getMoverDeltaX() - 0.2f) < 1e-5f);

    // Over the empty side of the ledge, snap must not invent a ground contact.
    CHECK(!field.moveCapsule(1.f, 0.5f, 0.f, 1.f, 1.5f, 0.f, 0.25f, 0.2f, 0.f, 0.f));
    CHECK(!field.isMoverGrounded());
    CHECK(std::fabs(field.getMoverDeltaY()) < 1e-6f);
}

TEST_CASE("box3d.sdf.moverStepOffsetClimbsOnlyLowLedges") {
    DistanceField3D field(41, 31, 5, 0.1f, -2.f, -1.f, -0.2f, 100.f);
    for (int z = 0; z < 5; ++z) {
        for (int y = 0; y < 31; ++y) {
            for (int x = 0; x < 41; ++x) {
                const float worldX = -2.f + 0.1f * static_cast<float>(x);
                const float worldY = -1.f + 0.1f * static_cast<float>(y);
                // Ground union a 0.4m-high semi-infinite step at x>=0.
                const float step = std::max(-worldX, worldY - 0.4f);
                field.setDistance(x, y, z, std::min(worldY, step));
            }
        }
    }

    field.setMoverSkinWidth(0.01f);
    field.setMoverGroundSnap(0.1f);
    field.setMoverStepHeight(0.5f);
    CHECK(std::fabs(field.getMoverStepHeight() - 0.5f) < 1e-6f);
    CHECK_THROWS((field.setMoverStepHeight(-0.1f), false));

    CHECK(field.moveCapsule(-1.f, 0.26f, 0.f, -1.f, 1.26f, 0.f, 0.25f, 1.75f, 0.f,
                            0.f));
    CHECK(field.isMoverGrounded());
    CHECK(field.getMoverDeltaX() > 1.74f);
    CHECK(field.getMoverDeltaY() > 0.39f);
    CHECK(field.getMoverDeltaY() < 0.41f);
    CHECK(!field.didMoverHitWall());

    // A step allowance lower than the obstacle must fall back to the direct wall result.
    field.setMoverStepHeight(0.3f);
    CHECK(field.moveCapsule(-1.f, 0.26f, 0.f, -1.f, 1.26f, 0.f, 0.25f, 1.75f, 0.f,
                            0.f));
    CHECK(field.didMoverHitWall());
    CHECK(field.getMoverDeltaX() < 1.f);
    CHECK(field.getMoverDeltaY() < 0.02f);
}
