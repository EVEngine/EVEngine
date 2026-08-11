#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Physics.h"
#include "physics/World3D.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"

#include <cmath>
#include <memory>

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
    Body3D *b = world->newBody("static", 5.f, 0.f, 0.f);
    b->newSphereShape(1.f);

    CHECK(sa->testPoint(0.f, 0.f, 0.f));
    CHECK(!sa->testPoint(10.f, 10.f, 10.f));

    int hit = world->rayCast(-5.f, 0.f, 0.f, 10.f, 0.f, 0.f);
    CHECK_EQ(hit, a->getId());
    CHECK(world->hasRayHit());
    CHECK(std::fabs(world->getRayHitY()) < 0.5f);

    int count = world->queryAABB(-2.f, -2.f, -2.f, 2.f, 2.f, 2.f);
    CHECK_GE(count, 1);
    bool foundA = false;
    for (int i = 0; i < world->getQueryCount(); ++i) {
        if (world->getQueryBodyId(i) == a->getId()) foundA = true;
    }
    CHECK(foundA);
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
