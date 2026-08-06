#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Physics.h"
#include "physics/World.h"
#include "physics/Body.h"
#include "physics/Fixture.h"

#include <cmath>
#include <memory>

using namespace eve::physics;

TEST_CASE("box2d.module.meter") {
    auto *mod = Physics::create();
    CHECK_EQ(mod->getMeter(), 30.f);
    mod->setMeter(50.f);
    CHECK_EQ(mod->getMeter(), 50.f);
    mod->setMeter(30.f);
}

TEST_CASE("box2d.world.createAndGravity") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    REQUIRE(world.get() != nullptr);
    CHECK(std::fabs(world->getGravityX()) < 0.01f);
    CHECK(std::fabs(world->getGravityY() - 900.f) < 0.01f);
    world->setGravity(10.f, 20.f);
    CHECK(std::fabs(world->getGravityX() - 10.f) < 0.01f);
    CHECK(std::fabs(world->getGravityY() - 20.f) < 0.01f);
}

TEST_CASE("box2d.body.fallsUnderGravity") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    Body *ground = world->newBody("static", 400.f, 500.f);
    ground->newRectangleFixture(800.f, 40.f, 0.f, 0.5f, 0.f);

    Body *box = world->newBody("dynamic", 400.f, 100.f);
    box->newRectangleFixture(40.f, 40.f, 1.f, 0.3f, 0.1f);
    float y0 = box->getY();

    for (int i = 0; i < 60; ++i)
        world->update(1.f / 60.f);

    CHECK_GT(box->getY(), y0);
    CHECK_LT(box->getY(), 500.f);
}

TEST_CASE("box2d.body.staticDoesNotMove") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    Body *ground = world->newBody("static", 200.f, 300.f);
    ground->newRectangleFixture(100.f, 20.f, 0.f);
    float y0 = ground->getY();
    for (int i = 0; i < 30; ++i)
        world->update(1.f / 60.f);
    CHECK(std::fabs(ground->getY() - y0) < 0.01f);
    CHECK_EQ(ground->getType(), std::string("static"));
}

TEST_CASE("box2d.body.circleAndVelocity") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *ball = world->newBody("dynamic", 0.f, 0.f);
    ball->newCircleFixture(16.f, 1.f, 0.2f, 0.8f);
    ball->setLinearVelocity(300.f, 0.f);

    for (int i = 0; i < 30; ++i)
        world->update(1.f / 60.f);

    CHECK_GT(ball->getX(), 100.f);
    CHECK(std::fabs(ball->getY()) < 5.f);
}

TEST_CASE("box2d.fixture.sensor") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *b = world->newBody("dynamic", 0.f, 0.f);
    Fixture *fixture = b->newRectangleFixture(10.f, 10.f);
    CHECK(!fixture->isSensor());
    fixture->setSensor(true);
    CHECK(fixture->isSensor());
    fixture->setFriction(0.7f);
    CHECK(std::fabs(fixture->getFriction() - 0.7f) < 0.001f);
}

TEST_CASE("box2d.body.destroy") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *b = world->newBody("dynamic", 10.f, 20.f);
    b->newRectangleFixture(8.f, 8.f);
    int id = b->getId();
    CHECK_GT(id, 0);
    b->destroy();
    CHECK(std::fabs(b->getX()) < 0.001f);
    delete b;
}

TEST_CASE("box2d.world.destroyInvalidates") {
    auto *mod = Physics::create();
    World *world = mod->newWorld(0.f, 100.f, true);
    Body *b = world->newBody("dynamic", 50.f, 50.f);
    b->newCircleFixture(5.f);
    world->destroy();
    CHECK(std::fabs(b->getX()) < 0.001f);
    delete b;
    delete world;
}
