#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <cmath>
#include <memory>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Shape3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/forces/FieldSet.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;

TEST_CASE("physics.forces.fieldSetFiltersAndSuperposes2D") {
    for (float meter : {10.f, 100.f}) {
        World                    world(0, 0, true, meter);
        std::unique_ptr<Body>    a(world.newBody("dynamic", 0, 0)), b(world.newBody("dynamic", 10 * meter, 0));
        std::unique_ptr<Fixture> fa(a->newCircleFixture(meter)), fb(b->newCircleFixture(meter));
        auto                     la = PhysicsLink::fromBody(*a);
        REQUIRE(la.ok());
        auto lb = PhysicsLink::fromBody(*b);
        REQUIRE(lb.ok());
        const auto      high     = UINT64_C(1) << 63;
        FieldDefinition fields[] = {{DirectionalField{{}, {2, 0, 0}}, {high, 0}},
                                    {DirectionalForceField{{}, {a->getMass() * 3, 0, 0}}, {high, 2}},
                                    {DirectionalField{{}, {100, 0, 0}}, {high, high}}};
        auto            set      = FieldSet::build(fields);
        REQUIRE(set.ok());
        FieldTarget targets[] = {{la.value(), high}, {lb.value(), 2}};
        a->raw()->SetAwake(false);
        b->raw()->SetAwake(false);
        auto batch = set.value().prepare(world, targets);
        REQUIRE(batch.ok());
        CHECK(!a->raw()->IsAwake());
        REQUIRE(batch.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
        CHECK(a->raw()->IsAwake());
        CHECK(!b->raw()->IsAwake());
        REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
        CHECK(std::abs(a->getLinearVelocityX() / meter - .05) < 1e-5);
        CHECK_EQ(b->getLinearVelocityX(), 0);
    }
}
TEST_CASE("physics.forces.fieldSet3DOrderAndAtomicFailures") {
    World3D                  world(0, 0, 0, true);
    std::unique_ptr<Body3D>  a(world.newBody("dynamic", 0, 0, 0)), b(world.newBody("dynamic", 10, 0, 0));
    std::unique_ptr<Shape3D> sa(a->newSphereShape(1)), sb(b->newSphereShape(1));
    auto                     la = PhysicsLink::fromBody(*a);
    REQUIRE(la.ok());
    auto lb = PhysicsLink::fromBody(*b);
    REQUIRE(lb.ok());
    FieldDefinition fields[] = {{DirectionalForceField{{}, {1e20, 0, 0}}, {}},
                                {DirectionalForceField{{}, {-1e20, 0, 0}}, {}},
                                {DirectionalForceField{{}, {4, 0, 0}}, {}}};
    auto            set      = FieldSet::build(fields);
    REQUIRE(set.ok());
    FieldTarget targets[] = {{la.value(), 1}, {lb.value(), 1}};
    auto        batch     = set.value().prepare(world, targets);
    REQUIRE(batch.ok());
    b3Body_SetAwake(eve::physics::box3d::Access::bodyId(*a), false);
    b->destroy();
    CHECK(!batch.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(!b3Body_IsAwake(eve::physics::box3d::Access::bodyId(*a)));
    CHECK(!set.value().prepare(world, targets).ok());
    targets[1] = targets[0];
    CHECK(!set.value().prepare(world, targets).ok());
    auto one = set.value().prepare(world, std::span(targets, 1));
    REQUIRE(one.ok());
    REQUIRE(one.value().commit(world, {eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    REQUIRE(world.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(10000000)}).ok());
    CHECK(std::abs(a->getLinearVelocityX() - 4 * .01 / a->getMass()) < 1e-6);
    fields[2] = {RadialField{{}, 1, -1}, {0, 0}};
    CHECK(!FieldSet::build(fields).ok());
}
