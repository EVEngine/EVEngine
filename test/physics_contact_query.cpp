#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"

#include <memory>

using namespace eve::physics;

TEST_CASE("physics.contacts.ownedCoverageAndLifetime") {
    World3D                  world(0.f, -10.f, 0.f, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0.f, -0.5f, 0.f));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(10.f, 1.f, 10.f));
    std::unique_ptr<Body3D>  ball(world.newBody("dynamic", 0.f, 2.f, 0.f));
    std::unique_ptr<Shape3D> sphere(ball->newSphereShape(0.5f, 1.f, 0.4f, 0.f));
    for (int i = 0; i < 120; ++i) world.update(1.f / 60.f);

    auto full = world.queryBodyContactsOwned(ball->runtimeHandle(), 4096);
    REQUIRE(full.ok());
    REQUIRE_GT(full.value().points.size(), 0u);
    CHECK(full.value().coverage == ContactQueryCoverage3D::Complete);
    auto exact = world.queryBodyContactsOwned(ball->runtimeHandle(), static_cast<int>(full.value().points.size()));
    REQUIRE(exact.ok());
    CHECK(exact.value().coverage == ContactQueryCoverage3D::Complete);
    CHECK_LT(exact.value().points.front().normalY, -0.9f);

    REQUIRE_GT(world.queryBodyContacts(ground->getId(), 4096), 0);
    const int cachedShape = world.getContactPointShapeId(0);
    auto      independent = world.queryBodyContactsOwned(ball->runtimeHandle(), 1);
    REQUIRE(independent.ok());
    CHECK_EQ(world.getContactPointShapeId(0), cachedShape);
    const int ownShape = sphere->getId();
    world.destroy();
    CHECK_EQ(exact.value().points.front().shapeId, ownShape);
    auto stale = world.queryBodyContactsOwned(full.value().body, 1);
    CHECK(!stale.ok());
}

TEST_CASE("physics.contacts.truncationIsExplicit") {
    World3D                  world(0.f, -10.f, 0.f, false);
    std::unique_ptr<Body3D>  ground(world.newBody("static", 0.f, -0.5f, 0.f));
    std::unique_ptr<Shape3D> floor(ground->newBoxShape(10.f, 1.f, 10.f));
    std::unique_ptr<Body3D>  box(world.newBody("dynamic", 0.f, 1.f, 0.f));
    std::unique_ptr<Shape3D> shape(box->newBoxShape(1.f, 1.f, 1.f));
    for (int i = 0; i < 120; ++i) world.update(1.f / 60.f);
    auto full = world.queryBodyContactsOwned(box->runtimeHandle(), 4096);
    REQUIRE(full.ok());
    REQUIRE_GT(full.value().points.size(), 1u);
    auto limited = world.queryBodyContactsOwned(box->runtimeHandle(), 1);
    REQUIRE(limited.ok());
    CHECK_EQ(limited.value().points.size(), 1u);
    CHECK(limited.value().coverage == ContactQueryCoverage3D::Truncated);
    CHECK_EQ(limited.value().points.front().featureId, full.value().points.front().featureId);
}

TEST_CASE("physics.contacts.rejectsForeignStaleAndInvalidInputs") {
    World3D                 first(0.f, 0.f, 0.f, false);
    World3D                 second(0.f, 0.f, 0.f, false);
    std::unique_ptr<Body3D> body(first.newBody("dynamic", 0.f, 0.f, 0.f));
    const auto              handle  = body->runtimeHandle();
    auto                    foreign = second.queryBodyContactsOwned(handle, 1);
    CHECK(!foreign.ok());
    auto empty = first.queryBodyContactsOwned(handle, 1);
    REQUIRE(empty.ok());
    CHECK(empty.value().points.empty());
    CHECK(empty.value().coverage == ContactQueryCoverage3D::Complete);
    auto invalid = first.queryBodyContactsOwned(handle, 0);
    CHECK(!invalid.ok());
    auto excessive = first.queryBodyContactsOwned(handle, 4097);
    CHECK(!excessive.ok());
    body->destroy();
    auto stale = first.queryBodyContactsOwned(handle, 1);
    CHECK(!stale.ok());
}
