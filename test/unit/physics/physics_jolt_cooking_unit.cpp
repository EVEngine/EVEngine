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
