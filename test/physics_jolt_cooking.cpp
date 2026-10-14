#include <cmath>
#include <limits>
#include <memory>
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DWorldFactory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#if EVENGINE_TEST_HAS_JOLT
#include "physics/jolt/JoltWorldFactory.h"
using namespace eve::physics;

TEST_CASE("physics.jolt.deferredDensityParity") {
    for (auto kind : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        World3D owner(0, 0, 0, false);
        auto    made = kind == RigidBackend3D::Box3D ? box3d::makeWorld(owner, 0, 0, 0, false)
                                                     : jolt::makeWorld(owner, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto world      = std::move(made).takeValue();
        auto bodyResult = world->createBody("dynamic", 0, 0, 0);
        REQUIRE(bodyResult.ok());
        auto            body = std::move(bodyResult).takeValue();
        ShapeGeometry3D geometry;
        geometry.a = geometry.b = geometry.c = .5f;
        geometry.localX                      = -1;
        auto leftResult                      = body->createShape(geometry, 1, .2f, 0);
        REQUIRE(leftResult.ok());
        auto left        = std::move(leftResult).takeValue();
        geometry.localX  = 1;
        auto rightResult = body->createShape(geometry, 1, .2f, 0);
        REQUIRE(rightResult.ok());
        auto       right   = std::move(rightResult).takeValue();
        const auto mass    = body->getMass().value();
        const auto inertia = body->getInertiaYY().value();
        const auto center  = body->getLocalCenterX().value();
        REQUIRE(right->setDensity(3, false).ok());
        CHECK(right->density().value() == 3);
        CHECK(body->getMass().value() == mass);
        CHECK(body->getInertiaYY().value() == inertia);
        CHECK(body->getLocalCenterX().value() == center);
        CHECK(right->testPoint(1, 0, 0).value());
        // Setting the same density does not flush pending mass in Box3D.
        REQUIRE(right->setDensity(3, true).ok());
        CHECK(body->getMass().value() == mass);
        REQUIRE(body->resetMassProperties().ok());
        CHECK(std::abs(body->getMass().value() - 4) < .001f);
        CHECK(std::abs(body->getLocalCenterX().value() - .5f) < .001f);
        CHECK(body->getInertiaYY().value() > inertia);
        CHECK(body->getX().value() == 0);
        REQUIRE(left->setDensity(2, false).ok());
        REQUIRE(right->setDensity(4, true).ok());
        CHECK(std::abs(body->getMass().value() - 6) < .001f);
        CHECK(std::abs(body->getLocalCenterX().value() - 1.f / 3.f) < .001f);
        if (kind == RigidBackend3D::Jolt) {
            auto invalid = right->setDensity(std::numeric_limits<float>::quiet_NaN(), false);
            CHECK(!invalid.ok());
            CHECK(right->density().value() == 4);
            CHECK(std::abs(body->getMass().value() - 6) < .001f);
        }
        REQUIRE(left->setDensity(4, false).ok());
        REQUIRE(body->setType("dynamic").ok());
        CHECK(std::abs(body->getMass().value() - 6) < .001f);
        REQUIRE(body->setType("kinematic").ok());
        REQUIRE(body->setType("dynamic").ok());
        CHECK(std::abs(body->getMass().value() - 8) < .001f);
        CHECK(std::abs(body->getLocalCenterX().value()) < .001f);
    }
}

TEST_CASE("physics.jolt.hullBudgetAndAtomicRebuildParity") {
    std::vector<float> points;
    for (int x : {-1, 1})
        for (int y : {-1, 1})
            for (int z : {-1, 1}) points.insert(points.end(), {float(x), float(y), float(z)});
    // Interior and repeated source points must not consume the hull's vertex budget.
    for (int i = 0; i < 32; ++i) points.insert(points.end(), {0, 0, 0});
    for (auto kind : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(kind, 0, 0, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 0, 0));
        std::unique_ptr<Shape3D> hull(body->newConvexHullShape(points, 8));
        CHECK(std::abs(body->getMass() - 8) < .02f);
        CHECK(hull->testPoint(.9f, .9f, .9f));
        const auto id = hull->getId();
        hull->setConvexHullVertices(points, 4);
        CHECK(hull->getId() == id);
        CHECK(hull->getConvexHullMaxVertices() == 4);
        CHECK(body->getMass() > .01f);
        CHECK(body->getMass() < 7.9f);
        const auto               mass = body->getMass();
        const std::vector<float> flat{0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
        CHECK_THROWS((hull->setConvexHullVertices(flat, 4), false));
        CHECK_THROWS((hull->setConvexHullVertices(points, 255), false));
        CHECK(hull->getConvexHullMaxVertices() == 4);
        CHECK(body->getMass() == mass);
        CHECK(world->getShapeCount() == 1);
        hull->setConvexHullVertices(points, 8);
        CHECK(std::abs(body->getMass() - 8) < .02f);
    }
}
TEST_CASE("physics.jolt.simplifiedHullCollisionParity") {
    std::vector<float> points;
    for (int x : {-1, 1})
        for (int y : {-1, 1})
            for (int z : {-1, 1}) points.insert(points.end(), {float(x), float(y), float(z)});
    for (auto kind : {RigidBackend3D::Box3D, RigidBackend3D::Jolt}) {
        auto made = World3D::createWithBackend(kind, 0, -10, 0, false);
        REQUIRE(made.ok());
        auto                     world = std::move(made).takeValue();
        std::unique_ptr<Body3D>  ground(world->newBody("static", 0, -.5f, 0));
        std::unique_ptr<Shape3D> floor(ground->newBoxShape(20, 1, 20));
        std::unique_ptr<Body3D>  body(world->newBody("dynamic", 0, 4, 0));
        std::unique_ptr<Shape3D> hull(body->newConvexHullShape(points, 4));
        bool                     contacted = false;
        for (int i = 0; i < 300; ++i) {
            world->update(.01f);
            contacted = contacted || world->getBeginContactCount() > 0;
        }
        CHECK(contacted);
        CHECK(std::isfinite(body->getY()));
        CHECK(body->getY() > -2);
        CHECK(body->getY() < 2);
        CHECK(std::abs(body->getLinearVelocityY()) < .1f);
        auto ray = world->rayCastOwned(body->getWorldCenterX(), 5, body->getWorldCenterZ(), body->getWorldCenterX(), -3,
                                       body->getWorldCenterZ(), {});
        REQUIRE(ray.ok());
        CHECK(ray.value().hit);
        CHECK(ray.value().body == body->runtimeHandle());
    }
}
#endif
