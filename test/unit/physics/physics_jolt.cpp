#include <cmath>
#include <memory>
#include "common/Snapshot.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
#if EVENGINE_TEST_HAS_JOLT
TEST_CASE("physics.jolt.nativeDropContactRayAndLifetime") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -0.5f, 0));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(20, 1, 20));
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 4, 0));
    std::unique_ptr<Shape3D> ball(body->newSphereShape(0.5f));
    bool                     contact = false;
    for (int i = 0; i < 200; ++i) {
        world->update(0.01f);
        contact = contact || world->getBeginContactCount() > 0;
    }
    CHECK(contact);
    CHECK(std::abs(body->getY() - 0.5f) < 0.06f);
    auto ray = world->rayCastOwned(0, 5, 0, 0, -2, 0, {});
    REQUIRE(ray.ok());
    CHECK(ray.value().hit);
    CHECK(ray.value().body == body->runtimeHandle());
    auto saved = ray.value();
    world->destroy();
    CHECK(!body->isValid());
    CHECK(!ball->isValid());
    CHECK(saved.hit);
}
TEST_CASE("physics.jolt.compoundShapeRemovalAndHitEvents") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> first(body->newBoxShape(2, 2, 2));
    std::unique_ptr<Shape3D> second(body->newSphereShape(2));
    CHECK(world->getShapeCount() == 2);
    second.reset();
    CHECK(world->getShapeCount() == 1);
    first->setHitEventsEnabled(true);
    CHECK(first->areHitEventsEnabled());
    first->setHitEventsEnabled(false);
    CHECK(!first->areHitEventsEnabled());
    auto ray = world->rayCastOwned(0, 3, 0, 0, -3, 0, {});
    REQUIRE(ray.ok());
    CHECK(ray.value().hit);
    body.reset();
    CHECK(!first->isValid());
    CHECK(world->getBodyCount() == 0);
}


TEST_CASE("physics.jolt.sharedSensorAndFilterContract") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  fixed(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(fixed->newBoxShape(4, 4, 4));
        sensor->setSensor(true);
        std::unique_ptr<Body3D>  moving(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> ball(moving->newSphereShape(0.5f));
        ball->setMaskBits(0);
        world->update(0.01f);
        CHECK(world->getBeginTriggerCount() == 0);
        ball->setMaskBits(UINT64_MAX);
        world->update(0.01f);
        CHECK(world->getBeginTriggerCount() > 0);
        CHECK(std::abs(moving->getY()) < 0.001f);
    }
}
TEST_CASE("physics.jolt.snapshotRestoreKeepsNativeStepDriver") {
    auto hash = [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        uint64_t              value = 14695981039346656037ull;
        for (unsigned char byte : input) value = (value ^ byte) * 1099511628211ull;
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(value >> ((i % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto  world = std::move(created).takeValue();
    auto* body  = world->newBody("dynamic", 0, 10, 0);
    body->newSphereShape(0.5f);
    const auto id        = body->getId();
    const auto oldHandle = body->runtimeHandle();
    auto       snapshot  = world->snapshot(hash);
    REQUIRE(snapshot.ok());
    auto restored = world->restore(snapshot.value(), hash);
    REQUIRE(restored.ok());
    CHECK(world->findBody(oldHandle) == nullptr);
    auto* current = world->findBodyById(id);
    REQUIRE(current != nullptr);
    world->update(0.02f);
    CHECK(current->getY() < 10.f);
    CHECK(world->rigidBackendKind() == RigidBackend3D::Jolt);
}
TEST_CASE("physics.jolt.rotationLockSurvivesMotionTypeChange") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  body(world->newBody("static", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 1, 1));
    body->setFixedRotation(true);
    body->setType("dynamic");
    body->applyAngularImpulse(0, 1, 0);
    CHECK(std::abs(body->getAngularVelocityY()) < 0.001f);
    body->setFixedRotation(false);
    body->applyAngularImpulse(0, 1, 0);
    CHECK(body->getAngularVelocityY() > 0.1f);
    const float oldW     = body->getRotW();
    bool        rejected = false;
    try {
        body->setRotation(1.e38f, 1.e38f, 1.e38f, 1.e38f);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(body->getRotW() == oldW);
}

TEST_CASE("physics.jolt.meshMaterialsAndAtomicReplacement") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, -9.8f, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::vector<float>       vertices{-5, 0, -5, -5, 0, 5, 5, 0, -5, 5, 0, 5};
        std::unique_ptr<Shape3D> mesh(ground->newTriangleMeshShape(vertices, {0, 1, 2, 3, 2, 1}));
        mesh->setTriangleMeshMaterialIndices({0, 1});
        mesh->setTriangleMeshMaterial(1, .5f, 0, 0, 0, 0, 0, 42, "default", "default");
        auto ray = world->rayCastOwned(3, 3, 3, 3, -3, 3, {});
        REQUIRE(ray.ok());
        CHECK(ray.value().hit);
        CHECK(ray.value().triangleIndex == 1);
        CHECK(ray.value().materialId == 42);
        bool rejected = false;
        try {
            mesh->setTriangleMeshData(vertices, {0, 0, 0});
        } catch (const std::exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(mesh->getTriangleMeshTriangleCount() == 2);
        rejected = false;
        try {
            ground->setType("dynamic");
        } catch (const std::exception&) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(ground->getType() == "static");
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 3, 0));
        std::unique_ptr<Shape3D> ball(body->newSphereShape(.5f));
        for (int i = 0; i < 200; ++i) world->update(.01f);
        CHECK(std::abs(body->getY() - .5f) < .08f);
    }
}
TEST_CASE("physics.jolt.rectangularHeightFieldAndMover") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> terrain(ground->newHeightFieldShape(3, 2, 2, 3, {0, 0, 0, 0, 0, 0}, -1, 1));
        auto                     ray = world->rayCastOwned(3, 2, 2, 3, -2, 2, {});
        REQUIRE(ray.ok());
        CHECK(ray.value().hit);
        CHECK(std::abs(ray.value().y) < .001f);
        auto moved = world->moveCapsuleOwned(1, 2, 1, 1, 3, 1, .4f, 1, -5, 0, {}, {});
        REQUIRE(moved.ok());
        CHECK(moved.value().constrained);
        CHECK(moved.value().grounded);
        CHECK(moved.value().deltaY > -1.65f);
        CHECK(moved.value().deltaX > .8f);
        QueryFilter3D ignored;
        ignored.ignoredShapeId = terrain->getId();
        auto free              = world->moveCapsuleOwned(1, 2, 1, 1, 3, 1, .4f, 1, -5, 0, ignored, {});
        REQUIRE(free.ok());
        CHECK(!free.value().constrained);
        CHECK(std::abs(free.value().deltaY + 5) < .001f);
        auto invalid = world->moveCapsuleOwned(1, 2, 1, 1, 3, 1, .4f, 1, -5, 0, {}, {0, 0, 0, .5f});
        CHECK(!invalid.ok());
    }
}
TEST_CASE("physics.jolt.moverWallSlideAndInitialPenetration") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  wall(world->newBody("static", 2, 0, 0));
        std::unique_ptr<Shape3D> box(wall->newBoxShape(.2f, 20, 20));
        auto                     moved = world->moveCapsuleOwned(0, 0, 0, 0, 1, 0, .5f, 20, 0, 3, {}, {});
        REQUIRE(moved.ok());
        CHECK(moved.value().constrained);
        CHECK(moved.value().deltaX < 1.41f);
        CHECK(moved.value().deltaZ > 2.9f);
        auto overlap = world->moveCapsuleOwned(1.8f, 0, 0, 1.8f, 1, 0, .5f, 0, 0, 0, {}, {});
        REQUIRE(overlap.ok());
        CHECK(overlap.value().constrained);
        CHECK(overlap.value().deltaX < -.35f);
    }
}
TEST_CASE("physics.jolt.distanceJointAndBodyFirstLifetime") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, -2, 0));
    std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
    std::unique_ptr<Joint3D> joint(world->newDistanceJoint(a.get(), b.get(), 0, 0, 0, 0, -2, 0, 2));
    joint->setForceThreshold(.01f);
    bool stress = false;
    for (int i = 0; i < 100; ++i) {
        world->update(.01f);
        stress = stress || world->getJointStressCount() > 0;
    }
    CHECK(std::abs(joint->getDistanceCurrentLength() - 2) < .03f);
    CHECK(stress);
    CHECK(world->getJointCount() == 1);
    a.reset();
    CHECK(!joint->isValid());
    CHECK(world->getJointCount() == 0);
    world->update(.01f);
}
TEST_CASE("physics.jolt.prismaticMotorAndLimits") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, -9.8f, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
        std::unique_ptr<Joint3D> joint(world->newPrismaticJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0));
        joint->setPrismaticLimits(true, -1, 1);
        joint->setPrismaticMotor(true, 2, 100);
        for (int i = 0; i < 150; ++i) world->update(.01f);
        CHECK(b->getX() > .8f);
        CHECK(b->getX() < 1.04f);
        CHECK(std::abs(b->getY()) < .02f);
        bool rejected = false;
        try {
            joint->setPrismaticLimits(true, 2, -2);
        } catch (const std::exception&) {
            rejected = true;
        }
        CHECK(rejected);
        world->destroy();
        CHECK(!joint->isValid());
    }
}
TEST_CASE("physics.jolt.revoluteMotorAndLimits") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 2, 1));
        std::unique_ptr<Joint3D> joint(world->newRevoluteJoint(a.get(), b.get(), 0, 0, 0, 0, 0, 1));
        joint->setRevoluteLimits(true, -.5f, .5f);
        joint->setRevoluteMotor(true, 2, 100);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(joint->getRevoluteAngle() > .4f);
        CHECK(joint->getRevoluteAngle() < .55f);
    }
}
TEST_CASE("physics.jolt.remainingJointKindsAndShapeComChange") {
    for (int kind = 0; kind < 5; ++kind) {
        auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, -1, 0));
        std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
        std::unique_ptr<Joint3D> joint;
        if (kind == 0) joint.reset(world->newWeldJoint(a.get(), b.get(), 0, 0, 0));
        if (kind == 1) joint.reset(world->newSphericalJoint(a.get(), b.get(), 0, 0, 0, 0, 1, 0));
        if (kind == 2) {
            joint.reset(world->newWheelJoint(a.get(), b.get(), 0, 0, 0, 0, 1, 0, 0, 0, 1));
            joint->setWheelSuspension(true, 5, 1);
            joint->setWheelSuspensionLimits(true, -.2f, .2f);
            joint->setWheelSpinMotor(true, 3, 100);
        }
        if (kind == 3) {
            joint.reset(world->newMotorJoint(a.get(), b.get()));
            joint->setMotorVelocityLimits(100, 100);
            joint->setMotorLinearVelocity(1, 0, 0);
        }
        if (kind == 4) joint.reset(world->newParallelJoint(a.get(), b.get(), 0, 1, 0));
        auto frames = joint->localFrames();
        REQUIRE(frames.ok());
        shape->setLocalPosition(.2f, 0, 0);
        for (int i = 0; i < 80; ++i) world->update(.01f);
        CHECK(joint->isValid());
        if (kind < 3) CHECK(b->getY() > -1.3f);
        if (kind == 2) CHECK(joint->getWheelSpinSpeed() > 2.f);
        if (kind == 3) CHECK(b->getX() > .6f);
    }
}
TEST_CASE("physics.jolt.filterJointReferenceCounting") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(1)), sb(b->newSphereShape(1));
    std::unique_ptr<Joint3D> j1(world->newFilterJoint(a.get(), b.get())), j2(world->newFilterJoint(a.get(), b.get()));
    world->update(.01f);
    CHECK(world->getBeginContactCount() == 0);
    j1.reset();
    world->update(.01f);
    CHECK(world->getBeginContactCount() == 0);
    j2.reset();
    world->update(.01f);
    CHECK(world->getBeginContactCount() > 0);
}

TEST_CASE("physics.jolt.snapshotRestoresAllJointKindsAndTerrain") {
    auto hash = [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        uint64_t              value = 14695981039346656037ull;
        for (unsigned char byte : input) value = (value ^ byte) * 1099511628211ull;
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(value >> ((i % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };

    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto  world = std::move(created).takeValue();
    auto* floor = world->newBody("static", 0, 0, 0);
    floor->newHeightFieldShape(3, 2, 10, 10, {0, 0, 0, 0, 0, 0}, -1, 1);
    auto* anchor = world->newBody("static", 0, 3, 0);
    for (int i = 0; i < 9; ++i) {
        auto* b = world->newBody("dynamic", float(i) * 2, 3, 0);
        b->newSphereShape(.2f);
        switch (i) {
            case 0: world->newDistanceJoint(anchor, b, 0, 3, 0, 0, 3, 0, 0); break;
            case 1: world->newRevoluteJoint(anchor, b, 2, 3, 0, 0, 0, 1); break;
            case 2: world->newPrismaticJoint(anchor, b, 4, 3, 0, 1, 0, 0); break;
            case 3: world->newSphericalJoint(anchor, b, 6, 3, 0, 0, 1, 0); break;
            case 4: world->newWheelJoint(anchor, b, 8, 3, 0, 0, 1, 0, 0, 0, 1); break;
            case 5: world->newWeldJoint(anchor, b, 10, 3, 0); break;
            case 6: world->newMotorJoint(anchor, b); break;
            case 7: world->newParallelJoint(anchor, b, 0, 1, 0); break;
            case 8: world->newFilterJoint(anchor, b); break;
        }
    }
    const auto oldHandle = anchor->runtimeHandle();
    auto       snapshot  = world->snapshot(hash);
    REQUIRE(snapshot.ok());
    auto restored = world->restore(snapshot.value(), hash);
    REQUIRE(restored.ok());
    CHECK(world->findBody(oldHandle) == nullptr);
    CHECK(world->getJointCount() == 9);
    for (int i = 0; i < 10; ++i) world->update(.01f);
    auto ray = world->rayCastOwned(1, 1, 1, 1, -1, 1, {});
    REQUIRE(ray.ok());
    CHECK(ray.value().hit);
}


TEST_CASE("physics.jolt.distanceSpringLimitsAndMotorShareSolver") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 1, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
        std::unique_ptr<Joint3D> joint(world->newDistanceJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0, 1));
        joint->setDistanceSpring(true, 0, 0);
        joint->setDistanceLimits(true, .5f, 2);
        joint->setDistanceMotor(true, 1, 100);
        for (int i = 0; i < 200; ++i) world->update(.01f);
        CHECK(joint->getDistanceCurrentLength() > 1.8f);
        CHECK(joint->getDistanceCurrentLength() < 2.05f);
        joint->setDistanceMotor(false, 0, 0);
        joint->setDistanceSpring(true, 4, 1);
        for (int i = 0; i < 200; ++i) world->update(.01f);
        CHECK(std::abs(joint->getDistanceCurrentLength() - 1) < .08f);
    }
}
TEST_CASE("physics.jolt.volumeQueriesAndCameraEndpoints") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 10, 0, 0)), b(world->newBody("static", 14, 0, 0));
        std::unique_ptr<Shape3D> sa(a->newBoxShape(1, 1, 1)), sb(b->newSphereShape(.5f));
        CHECK(sa->testPoint(10, 0, 0));
        CHECK(!sa->testPoint(12, 0, 0));
        CHECK(world->querySphere(10, 0, 0, .25f) == 1);
        auto overlap = world->queryCapsuleOwned(10, 0, 0, 10, 1, 0, .25f, {});
        REQUIRE(overlap.ok());
        CHECK(overlap.value().bodyCount == 1);
        auto aabb = world->queryAabbBroadPhaseOwned(9, -1, -1, 15, 1, 1, {});
        REQUIRE(aabb.ok());
        CHECK(aabb.value().count == 2);
        CHECK(world->rayCastAll(8, 0, 0, 16, 0, 0, 8) >= 2);
        CHECK(world->castSphereAll(8, 0, 0, .25f, 8, 0, 0, 8) >= 2);
        CHECK(world->getShapeCastBodyId() == a->getId());
        CHECK(std::abs(world->getShapeCastFraction() - .15625f) < .01f);
        CameraSphereHit3D camera;
        CHECK(world->sphereCast(8, 0, 0, 16, 0, 0, .25f, UINT64_MAX, -1, &camera));
        CHECK(camera.bodyId == a->getId());
        CHECK(std::abs(camera.fraction - .15625f) < .01f);
        CHECK(world->sphereCast(8, 0, 0, 16, 0, 0, 0, UINT64_MAX, -1, &camera));
        CHECK(std::abs(camera.fraction - .1875f) < .01f);
        sa->setCategoryBits(uint64_t(1) << 48);
        CHECK(world->rayCastFiltered(8, 0, 0, 16, 0, 0, uint64_t(1) << 48) == a->getId());
        sa->setCategoryBits(1);
        QueryFilter3D ignored;
        ignored.ignoredBodyId = a->getId();
        auto filtered         = world->queryCapsuleOwned(10, 0, 0, 10, 1, 0, .25f, ignored);
        REQUIRE(filtered.ok());
        CHECK(filtered.value().bodyCount == 0);
    }
}
TEST_CASE("physics.jolt.localImpulsesLocksAndKinematicTarget") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 1, 1));
    body->setRotation(0, 0, .70710678f, .70710678f);
    body->applyLocalLinearImpulseToCenter(1, 0, 0);
    CHECK(body->getLinearVelocityY() > .5f);
    body->setMotionLocks(true, false, false, true, true, true);
    CHECK(body->isLinearXLocked());
    CHECK(body->isAngularZLocked());
    body->applyLinearImpulse(10, 0, 0);
    CHECK(std::abs(body->getLinearVelocityX()) < .001f);
    body->setMotionLocks(true, true, true, false, false, false);
    bool rejected = false;
    try {
        body->setFixedRotation(true);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(!body->isFixedRotation());
    body->setMotionLocks(false, false, false, false, false, false);
    body->setType("kinematic");
    body->setTargetTransform(0, 2, 0, 0, 0, 0, 1, 1);
    for (int i = 0; i < 100; ++i) world->update(.01f);
    CHECK(std::abs(body->getY() - 2) < .02f);
}
TEST_CASE("physics.jolt.moverArbitraryUpAndThinWall") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  floor(world->newBody("static", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(floor->newBoxShape(.02f, 20, 20));
    auto                     moved = world->moveCapsuleOwned(2, 0, 0, 3, 0, 0, .5f, -50, 0, 2, {}, {1, 0, 0, .8f});
    REQUIRE(moved.ok());
    CHECK(moved.value().grounded);
    CHECK(moved.value().deltaX > -1.51f);
    CHECK(moved.value().deltaZ > 1.9f);
}


TEST_CASE("physics.jolt.motorWakesBodiesAndKeepsWorldVelocity") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, true);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
    std::unique_ptr<Shape3D> shape(b->newSphereShape(.25f));
    std::unique_ptr<Joint3D> joint(world->newMotorJoint(a.get(), b.get()));
    b->setAwake(false);
    CHECK(!b->isAwake());
    joint->setMotorVelocityLimits(100, 100);
    joint->setMotorLinearVelocity(2, 0, 0);
    CHECK(b->isAwake());
    a->setRotation(0, 0, .70710678f, .70710678f);
    for (int i = 0; i < 50; ++i) world->update(.01f);
    CHECK(b->getX() > .8f);
    CHECK(std::abs(b->getY()) < .02f);
    CHECK(joint->getMotorLinearVelocityX() == 2);
}


TEST_CASE("physics.jolt.massDensityAndConstraintComAreAtomic") {
    auto created = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, false);
    REQUIRE(created.ok());
    auto                     world = std::move(created).takeValue();
    std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, -1, 0));
    std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
    std::unique_ptr<Joint3D> joint(world->newWeldJoint(a.get(), b.get(), 0, 0, 0));
    shape->setDensity(2);
    CHECK(std::abs(b->getMass() - 2) < .001f);
    b->setMassProperties(3, .3f, 0, 0, 2, 3, 4, .1f, .2f, .1f);
    CHECK(std::abs(b->getMass() - 3) < .001f);
    CHECK(std::abs(b->getLocalCenterX() - .3f) < .001f);
    CHECK(std::abs(b->getInertiaXY() - .1f) < .001f);
    bool rejected = false;
    try {
        b->setMassProperties(5, 2, 0, 0, 1, -1, 1);
    } catch (const std::exception&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK(std::abs(b->getLocalCenterX() - .3f) < .001f);
    CHECK(std::abs(b->getMass() - 3) < .001f);
    for (int i = 0; i < 100; ++i) world->update(.01f);
    CHECK(std::abs(b->getY() + 1) < .02f);
    CHECK(std::abs(b->getX()) < .02f);
    b->resetMassProperties();
    CHECK(std::abs(b->getLocalCenterX()) < .001f);
    CHECK(std::abs(b->getMass() - 2) < .001f);
    shape->setDensity(0);
    CHECK(std::isfinite(b->getMass()));
}

TEST_CASE("physics.jolt.worldControlsAndBoundsParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(body->newSphereShape(.5f));
        body->setBullet(true);
        world->setContinuousCollisionEnabled(false);
        CHECK(body->isBullet());
        CHECK(!world->isContinuousCollisionEnabled());
        world->setContinuousCollisionEnabled(true);
        CHECK(body->isBullet());
        world->setMaximumLinearSpeed(2);
        CHECK(world->getMaximumLinearSpeed() == 2);
        body->setLinearVelocity(20, 0, 0);
        world->update(.1f);
        CHECK(body->getLinearVelocityX() <= 2.01f);
        CHECK(world->getBoundsMinX() <= body->getX() - .49f);
        CHECK(world->getBoundsMaxX() >= body->getX() + .49f);
        CHECK(world->getProfileStepMs() >= 0);
        world->setContactRecycleDistance(.05f);
        CHECK(std::abs(world->getContactRecycleDistance() - .05f) < .00001f);
        world->setContactRecycleDistance(0);
        CHECK(world->getContactRecycleDistance() == 0);
        if (backend == RigidBackend3D::Jolt) {
            bool failed = false;
            try {
                world->setMaximumLinearSpeed(-1);
            } catch (const std::exception&) {
                failed = true;
            }
            CHECK(failed);
            CHECK(world->getMaximumLinearSpeed() == 2);
        }
    }
}
TEST_CASE("physics.jolt.closestAndExplosionParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 3, 0, 0));
        std::unique_ptr<Shape3D> shape(body->newSphereShape(1));
        CHECK(world->closestPoint(0, 0, 0, 5) == body->getId());
        CHECK(std::abs(world->getClosestX() - 2) < .002f);
        CHECK(std::abs(world->getClosestDistance() - 2) < .002f);
        CHECK(world->closestPoint(3, 0, 0, 0) == body->getId());
        CHECK(world->getClosestDistance() < .001f);
        shape->setExplosionScale(0);
        CHECK(world->explode(0, 0, 0, 5, 0, 1, -1) == 0);
        shape->setExplosionScale(2);
        CHECK(world->explode(0, 0, 0, 5, 0, 1, -1) == 1);
        CHECK(std::abs(body->getLinearVelocityX() - 1.5f) < .03f);
        CHECK(std::abs(body->getAngularVelocityZ()) < .001f);
        body->setLinearVelocity(0, 0, 0);
        CHECK(world->explode(0, 0, 0, 1, 2, 1, -1) == 1);
        CHECK(std::abs(body->getLinearVelocityX() - .75f) < .03f);
    }
}
TEST_CASE("physics.jolt.oneWayAndPairOverridesParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, -10, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  floor(world->newBody("static", 0, 0, 0)), body(world->newBody("dynamic", 0, -2, 0));
        std::unique_ptr<Shape3D> platform(floor->newBoxShape(10, .2f, 10)), ball(body->newSphereShape(.2f));
        platform->setOneWay(0, 1, 0, .1f, .05f, .5f);
        body->setLinearVelocity(0, 8, 0);
        for (int i = 0; i < 180; ++i) world->update(.01f);
        CHECK(body->getY() > .25f);
        CHECK(body->getY() < .35f);
        world->setBodyPairCollisionEnabled(floor.get(), body.get(), false);
        for (int i = 0; i < 60; ++i) world->update(.01f);
        CHECK(body->getY() < -.5f);
        world->setBodyPairCollisionEnabled(floor.get(), body.get(), true);
        body->setPosition(0, 2, 0);
        body->setLinearVelocity(0, 0, 0);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(body->getY() > .25f);
        world->setShapePairCollisionEnabled(platform.get(), ball.get(), false);
        for (int i = 0; i < 60; ++i) world->update(.01f);
        CHECK(body->getY() < -.5f);
    }
}
TEST_CASE("physics.jolt.concurrentSpringAndVelocityMotorParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
        std::unique_ptr<Joint3D> joint(world->newPrismaticJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0));
        joint->setPrismaticSpring(true, 0, 2, 1);
        joint->setPrismaticMotor(true, 2, 2);
        joint->setPrismaticLimits(true, -.5f, .5f);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(b->getX() > 0);
        CHECK(b->getX() < .51f);
        CHECK(std::abs(joint->getPrismaticMotorForce()) > 0);
        joint->setPrismaticMotor(false, 0, 0);
        for (int i = 0; i < 150; ++i) world->update(.01f);
        CHECK(std::abs(b->getX()) < .015f);
        joint.reset();
        joint.reset(world->newMotorJoint(a.get(), b.get()));
        joint->setMotorLinearSpring(2, 1, 100);
        joint->setMotorVelocityLimits(2, 2);
        joint->setMotorLinearVelocity(1, 0, 0);
        joint->setMotorAngularSpring(2, 1, 100);
        joint->setMotorAngularVelocity(0, 0, 1);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(std::isfinite(b->getX()));
        CHECK(std::abs(b->getX()) < .2f);
    }
}

TEST_CASE("physics.jolt.nonzeroEqualLimitsParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(b->newBoxShape(1, 1, 1));
        std::unique_ptr<Joint3D> joint(world->newPrismaticJoint(a.get(), b.get(), 0, 0, 0, 1, 0, 0));
        joint->setPrismaticLimits(true, .4f, .4f);
        for (int i = 0; i < 120; ++i) world->update(.01f);
        CHECK(std::abs(joint->getPrismaticTranslation() - .4f) < .015f);
        joint.reset();
        b->setPosition(0, 0, 0);
        b->setLinearVelocity(0, 0, 0);
        joint.reset(world->newRevoluteJoint(a.get(), b.get(), 0, 0, 0, 0, 0, 1));
        joint->setRevoluteLimits(true, .4f, .4f);
        for (int i = 0; i < 120; ++i) world->update(.01f);
        CHECK(std::abs(joint->getRevoluteAngle() - .4f) < .015f);
    }
}

TEST_CASE("physics.jolt.customSleepAndInactivePointParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, true);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> shape(body->newBoxShape(1, 1, 1));
        body->setSleepThreshold(.1f);
        CHECK(body->getSleepThreshold() == .1f);
        body->setLinearVelocity(.01f, 0, 0);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(!body->isAwake());
        body->setSleepEnabled(false);
        body->setAwake(true);
        body->setLinearVelocity(.01f, 0, 0);
        for (int i = 0; i < 100; ++i) world->update(.01f);
        CHECK(body->isAwake());
        body->setActive(false);
        CHECK(shape->testPoint(body->getX(), body->getY(), body->getZ()));
        ClothContact3D contact;
        const auto     status = world->pointProbeFiltered(body->getX() + .6f, 0, 0, .2f, &contact, 1, UINT64_MAX);
        CHECK(status == ClothProbeStatus::Hit);
        CHECK(contact.hit);
        CHECK(std::abs(contact.depth - .1f) < .005f);
        CHECK(contact.nx > .99f);
    }
}

TEST_CASE("physics.jolt.sensorPairExclusionParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto created = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(created.ok());
        auto                     world = std::move(created).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(a->newBoxShape(2, 2, 2)), shape(b->newSphereShape(.2f));
        sensor->setSensor(true);
        world->setShapePairCollisionEnabled(sensor.get(), shape.get(), false);
        world->update(.01f);
        CHECK(world->getBeginTriggerCount() == 0);
        world->setShapePairCollisionEnabled(sensor.get(), shape.get(), true);
        world->update(.01f);
        CHECK(world->getBeginTriggerCount() > 0);
        world->setBodyPairCollisionEnabled(a.get(), b.get(), false);
        world->update(.01f);
        CHECK(world->getEndTriggerCount() > 0);
    }
}

#else
TEST_CASE("physics.jolt.absentProviderRejectsExplicitSelection") {
    auto result = World3D::createWithBackend(RigidBackend3D::Jolt, 0, -9.8f, 0, true);
    REQUIRE(!result.ok());
    CHECK(result.error()->code() == eve::DiagnosticCode::Unsupported);
}
#endif
