#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <memory>

TEST_CASE("climbing.physics.ownedMoverSlopePolicyIsPerCall") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    world->newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(10.f, 1.f, 10.f);

    eve::physics::CapsuleMovePolicy3D strict;
    strict.upY             = 1.f;
    strict.upZ             = 1.f;
    strict.maxSlopeRadians = 0.69813170f;
    auto notGround         = world->moveCapsuleOwned(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f, {}, strict);
    REQUIRE(notGround.ok());
    CHECK(!notGround.value().grounded);

    auto permissive            = strict;
    permissive.maxSlopeRadians = 0.87266463f;
    auto ground = world->moveCapsuleOwned(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f, {}, permissive);
    REQUIRE(ground.ok());
    CHECK(ground.value().grounded);

    auto defaultPolicy = world->moveCapsuleOwned(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f);
    REQUIRE(defaultPolicy.ok());
    CHECK(defaultPolicy.value().grounded);
}

TEST_CASE("climbing.physics.broadPhaseOwnsStableFilteredShapeEvidence") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto*                                  ignored = world->newBody("static", -1.f, 0.f, 0.f);
    ignored->newBoxShape(0.5f, 0.5f, 0.5f);
    auto* kept      = world->newBody("static", 1.f, 0.f, 0.f);
    auto* keptShape = kept->newBoxShape(0.5f, 0.5f, 0.5f);

    eve::physics::QueryFilter3D filter;
    filter.ignoredBodyId = ignored->getId();
    auto result          = world->queryAabbBroadPhaseOwned(-2.f, -1.f, -1.f, 2.f, 1.f, 1.f, filter);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().count, std::size_t(1));
    CHECK(!result.value().truncated);
    CHECK(result.value().hits[0].body == kept->runtimeHandle());
    CHECK(result.value().hits[0].shape == keptShape->runtimeHandle());
}
