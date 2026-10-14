#include <cmath>
#include <memory>
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if EVENGINE_TEST_HAS_JOLT
using namespace eve::physics;

TEST_CASE("physics.jolt.solvedImpulseMomentumParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
        std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, 1, 10));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, .6f, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        body->setLinearDamping(0);
        body->setAngularDamping(0);
        body->setLinearVelocity(0, -4, 0);
        sphere->setTag(72);
        for (int frame = 0; frame < 4; ++frame) {
            const float before = body->getLinearVelocityY();
            world->updateFull(.025f, 1);
            auto queried = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
            REQUIRE(queried.ok());
            float impulse = 0;
            for (const auto& point : queried.value().points) {
                CHECK(point.shapeTag == 72);
                CHECK(point.otherBodyId == ground->getId());
                CHECK(point.normalY < -.99f);
                CHECK(point.normalImpulse >= 0);
                impulse += point.totalNormalImpulse;
            }
            const float momentum = body->getMass() * (body->getLinearVelocityY() - before);
            CHECK(std::abs(impulse - momentum) < .03f);
        }
        CHECK(body->getLinearVelocityY() > -1);
    }
}

TEST_CASE("physics.jolt.solvedContactCoverageLifetimeParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, -10, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
        std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, 1, 10));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 1, 0));
        std::unique_ptr<Shape3D> box(body->newBoxShape(1, 1, 1));
        for (int i = 0; i < 100; ++i) world->updateFull(.02f, 4);
        auto all = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
        REQUIRE(all.ok());
        REQUIRE(all.value().points.size() > 1);
        float impulse = 0;
        for (const auto& point : all.value().points) {
            impulse += point.totalNormalImpulse;
            CHECK(point.persisted);
        }
        CHECK(std::abs(impulse - body->getMass() * 10 * .02f) < .04f);
        auto limited = world->queryBodyContactsOwned(body->runtimeHandle(), 1);
        REQUIRE(limited.ok());
        CHECK(limited.value().coverage == ContactQueryCoverage3D::Truncated);
        CHECK(limited.value().points.size() == 1);
        auto opposite = world->queryBodyContactsOwned(ground->runtimeHandle(), 4096);
        REQUIRE(opposite.ok());
        CHECK(opposite.value().points.front().normalY > .99f);
        CHECK(!world->queryBodyContactsOwned(body->runtimeHandle(), 0).ok());
        body->setActive(false);
        auto inactive = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
        REQUIRE(inactive.ok());
        CHECK(inactive.value().points.empty());
        body->setActive(true);
        const auto handle  = body->runtimeHandle();
        const auto shapeId = box->getId();
        box.reset();
        auto removed = world->queryBodyContactsOwned(handle, 4096);
        REQUIRE(removed.ok());
        CHECK(removed.value().points.empty());
        world->destroy();
        CHECK(all.value().points.front().shapeId == shapeId);
        CHECK(!world->queryBodyContactsOwned(handle, 1).ok());
    }
}

TEST_CASE("physics.jolt.hitThresholdAndOptInParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt})
        for (int policy = 0; policy < 3; ++policy) {
            auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
            REQUIRE(made.ok());
            auto world = std::move(made).takeValue();
            world->setHitEventThreshold(policy == 2 ? 100.f : 1.f);
            CHECK(world->getHitEventThreshold() == (policy == 2 ? 100.f : 1.f));
            std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
            std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, 1, 10));
            std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 1, 0));
            std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
            sphere->setHitEventsEnabled(policy != 0);
            sphere->setTag(73);
            body->setLinearDamping(0);
            body->setLinearVelocity(0, -5, 0);
            int hits = 0;
            for (int i = 0; i < 20; ++i) {
                world->updateFull(.01f, 2);
                hits += world->getHitCount();
                for (int j = 0; j < world->getHitCount(); ++j) {
                    CHECK(world->getHitApproachSpeed(j) > 1);
                    CHECK(world->getHitNormalImpulse(j) > 0);
                    CHECK(world->getHitShapeAId(j) < world->getHitShapeBId(j));
                    CHECK(world->getHitShapeBTag(j) == 73);
                }
            }
            CHECK((hits > 0) == (policy == 1));
            CHECK(world->getHitCount() == 0);
        }
}

TEST_CASE("physics.jolt.sensorHasNoSolvedImpulseOrHitParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> sensor(ground->newBoxShape(10, 1, 10));
        sensor->setSensor(true);
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 1, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        sphere->setHitEventsEnabled(true);
        body->setLinearVelocity(0, -5, 0);
        for (int i = 0; i < 30; ++i) {
            world->update(.01f);
            CHECK(world->getHitCount() == 0);
            auto queried = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
            REQUIRE(queried.ok());
            CHECK(queried.value().points.empty());
        }
    }
}

TEST_CASE("physics.jolt.rollingResistanceContactOnlyParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt})
        for (int mode = 0; mode < 3; ++mode) {
            auto made = World3D::createWithBackend(backend, 0, mode == 2 ? 0.f : -10.f, 0, false);
            REQUIRE(made.ok());
            auto                     world = std::move(made).takeValue();
            std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
            std::unique_ptr<Shape3D> floor(ground->newBoxShape(20, 1, 20));
            floor->setFriction(0);
            floor->setRollingResistance(mode == 0 ? 0 : .3f);
            std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, mode == 2 ? 5.f : .5f, 0));
            std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
            sphere->setFriction(0);
            body->setAngularDamping(0);
            body->setLinearDamping(0);
            body->setAngularVelocity(0, 10, 0);
            for (int i = 0; i < 100; ++i) world->updateFull(.01f, 2);
            if (mode == 1)
                CHECK(std::abs(body->getAngularVelocityY()) < .2f);
            else
                CHECK(std::abs(body->getAngularVelocityY() - 10) < .01f);
            CHECK_THROWS((sphere->setRollingResistance(-1), false));
            CHECK(sphere->getRollingResistance() == 0);
        }
}

TEST_CASE("physics.jolt.ccdReportsActualHitImpulse") {
    auto made = World3D::createWithBackend(RigidBackend3D::Jolt, 0, 0, 0, false);
    REQUIRE(made.ok());
    auto                     world = std::move(made).takeValue();
    std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.05f, 0));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(10, .1f, 10));
    std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 2, 0));
    std::unique_ptr<Shape3D> sphere(body->newSphereShape(.1f));
    sphere->setHitEventsEnabled(true);
    body->setBullet(true);
    body->setLinearDamping(0);
    body->setLinearVelocity(0, -100, 0);
    world->updateFull(.04f, 1);
    CHECK(body->getY() > -.1f);
    REQUIRE(world->getHitCount() == 1);
    CHECK(world->getHitApproachSpeed(0) > 90);
    CHECK(std::abs(world->getHitNormalImpulse(0) - body->getMass() * 100) < .01f);
    auto queried = world->queryBodyContactsOwned(body->runtimeHandle(), 4096);
    REQUIRE(queried.ok());
    REQUIRE(!queried.value().points.empty());
    CHECK(queried.value().points.front().normalImpulse > 0);
}

TEST_CASE("physics.jolt.meshRollingMaterialAndHitParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, -10, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, 0, 0));
        std::unique_ptr<Shape3D> mesh(
            ground->newTriangleMeshShape({-5, 0, -5, 5, 0, -5, 5, 0, 5, -5, 0, 5}, {0, 2, 1, 0, 3, 2}));
        mesh->setTriangleMeshMaterial(0, 0, 0, .5f, 0, 0, 0, 81, "default", "default");
        CHECK(mesh->getTriangleMeshMaterialRollingResistance(0) == .5f);
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 1, 2, 0));
        std::unique_ptr<Shape3D> sphere(body->newSphereShape(.5f));
        sphere->setHitEventsEnabled(true);
        sphere->setFriction(0);
        body->setAngularDamping(0);
        body->setAngularVelocity(0, 8, 0);
        bool hit = false;
        for (int i = 0; i < 150; ++i) {
            world->update(.01f);
            hit = hit || world->getHitCount() > 0;
        }
        CHECK(hit);
        CHECK(body->getY() > .45f);
        CHECK(std::abs(body->getAngularVelocityY()) < .2f);
    }
}

TEST_CASE("physics.jolt.disabledPairHasNoSolvedHitParity") {
    for (auto backend : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(backend, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  a(world->newBody("static", 0, 0, 0)), b(world->newBody("dynamic", 0, 1, 0));
        std::unique_ptr<Shape3D> floor(a->newBoxShape(10, .1f, 10)), ball(b->newSphereShape(.2f));
        ball->setHitEventsEnabled(true);
        world->setShapePairCollisionEnabled(floor.get(), ball.get(), false);
        b->setLinearVelocity(0, -10, 0);
        for (int i = 0; i < 20; ++i) {
            world->update(.01f);
            CHECK(world->getHitCount() == 0);
            auto contact = world->queryBodyContactsOwned(b->runtimeHandle(), 4096);
            REQUIRE(contact.ok());
            CHECK(contact.value().points.empty());
        }
        CHECK(b->getY() < 0);
    }
}
#endif
