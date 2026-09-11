#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "physics/Body3D.h"
#include "physics/DistanceField3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/rope/Rope.h"
#include "physics/rope/Rope3D.h"
#include "schema/SchemaRegistry.h"

#include <cmath>
#include <limits>
#include <memory>

using namespace eve::physics;

TEST_CASE("physics.rope.fixedStepAttachmentsAndStretch") {
    Rope3D rope(9, 0.f, 3.f, 0.f, 4.f, 3.f, 0.f);
    CHECK_EQ(rope.getParticleCount(), 9);
    CHECK_EQ(rope.getElementCount(), 8);
    CHECK(std::abs(rope.getRestLength() - 4.f) < 1e-5f);

    auto pinned = rope.pin(0);
    REQUIRE(pinned.ok());
    auto attached = rope.attach(8, 4.f, 3.f, 0.f);
    REQUIRE(attached.ok());
    rope.setGravity(0.f, -9.81f, 0.f);

    for (std::uint64_t tick = 1; tick <= 20; ++tick) {
        auto stepped = rope.step({eve::SimulationTick{tick}, eve::Duration::fromNanoseconds(16666667)},
                                 SimulationSettings{8, 6, 4});
        REQUIRE(stepped.ok());
    }
    CHECK_EQ(rope.observation().stepCount, std::uint64_t(20));
    CHECK(std::abs(rope.getParticleX(0)) < 1e-6f);
    CHECK(std::abs(rope.getParticleY(0) - 3.f) < 1e-6f);
    CHECK(rope.getParticleY(4) < 3.f);
    CHECK(rope.calculateLength() >= rope.getRestLength() * 0.99f);
}

TEST_CASE("physics.rope.reelCutRepairAndValidation") {
    Rope3D rope(6, 0.f, 2.f, 0.f, 0.f, -3.f, 0.f);
    auto   resized = rope.setRestLength(3.f);
    REQUIRE(resized.ok());
    CHECK(std::abs(rope.getRestLength() - 3.f) < 1e-5f);

    auto cut = rope.cut(2);
    REQUIRE(cut.ok());
    CHECK(!rope.isElementActive(2));
    CHECK(std::abs(rope.getRestLength() - 2.4f) < 1e-4f);
    auto cutAgain = rope.cut(2);
    REQUIRE(cutAgain.ok());
    CHECK_EQ(cutAgain.value(), RopeTopologyChange::Unchanged);

    auto repaired = rope.repair(2);
    REQUIRE(repaired.ok());
    CHECK(rope.isElementActive(2));
    CHECK(rope.getElementForce(2) == 0.f);

    auto invalid = rope.attach(99, 0.f, 0.f, 0.f);
    CHECK(!invalid.ok());
    CHECK_EQ(invalid.code(), eve::StatusCode::Rejected);
}

TEST_CASE("physics.rope.automaticTearingIsBounded") {
    Rope3D rope(5, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f);
    REQUIRE(rope.attach(0, 0.f, 0.f, 0.f).ok());
    REQUIRE(rope.attach(4, 8.f, 0.f, 0.f).ok());
    rope.setTearing(0.01f, 1);
    auto stepped =
        rope.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)}, SimulationSettings{8, 8, 1});
    REQUIRE(stepped.ok());
    int inactive = 0;
    for (int i = 0; i < rope.getElementCount(); ++i) inactive += rope.isElementActive(i) ? 0 : 1;
    CHECK_EQ(inactive, 1);
    CHECK_EQ(rope.getLastTornElementCount(), 1);
    CHECK(rope.getLastTornElement(0) >= 0);
    CHECK_EQ(rope.getTopologyRevision(), std::uint64_t(1));
}

TEST_CASE("physics.rope.cursorLengthChangesTopologyAndTransfersAttachment") {
    Rope3D rope(5, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f);
    REQUIRE(rope.attach(4, 4.f, 1.f, 0.f).ok());
    const auto initialRevision = rope.getTopologyRevision();
    REQUIRE(rope.changeLength(6.f, 0.5f, true).ok());
    CHECK_EQ(rope.getParticleCount(), 9);
    CHECK(std::abs(rope.getRestLength() - 6.f) < 1e-5f);
    CHECK(rope.getTopologyRevision() > initialRevision);

    REQUIRE(rope.changeLength(2.5f, 0.5f, true).ok());
    CHECK(std::abs(rope.getRestLength() - 2.5f) < 1e-5f);
    CHECK(rope.isAttached(rope.getParticleCount() - 1));
    REQUIRE(rope.cut(0).ok());
    auto rejected = rope.changeLength(3.f, 0.5f, true);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
}

TEST_CASE("physics.rope.constraintFamiliesAndElementTearMaterials") {
    Rope3D rope(5, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f);
    rope.setDistanceConstraintsEnabled(false);
    rope.setBendConstraintsEnabled(false);
    rope.setMaxBending(0.2f);
    CHECK(!rope.getDistanceConstraintsEnabled());
    CHECK(!rope.getBendConstraintsEnabled());
    CHECK(std::abs(rope.getMaxBending() - 0.2f) < 1e-6f);
    REQUIRE(rope.setElementTearResistance(0, 1000000.f).ok());
    CHECK(!rope.setElementTearResistance(99, 1.f).ok());

    rope.setDistanceConstraintsEnabled(true);
    rope.setBendConstraintsEnabled(true);
    REQUIRE(rope.attach(0, 0.f, 0.f, 0.f).ok());
    REQUIRE(rope.attach(4, 8.f, 0.f, 0.f).ok());
    rope.setTearing(0.01f, 4);
    REQUIRE(rope.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)}, SimulationSettings{8, 8, 1})
                .ok());
    CHECK(rope.isElementActive(0));
    int torn = 0;
    for (int i = 1; i < rope.getElementCount(); ++i) torn += rope.isElementActive(i) ? 0 : 1;
    CHECK(torn > 0);
}

TEST_CASE("physics.rope.rejectsNonFiniteCompatibilityInputs") {
    Rope3D rope(3, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f);
    rope.update(0.f);
    CHECK_THROWS((rope.update(-0.01f), false));
    CHECK_THROWS((rope.update(std::numeric_limits<float>::quiet_NaN()), false));
    CHECK_THROWS((rope.setGravity(0.f, std::numeric_limits<float>::infinity(), 0.f), false));
    CHECK_THROWS((rope.applyForce(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f), false));
    CHECK(std::isfinite(rope.getParticleX(1)));
}

TEST_CASE("physics.rope.plasticityAbsorbsPersistentLocalBend") {
    Rope3D rope(3, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f);
    rope.setGravity(0.f, 0.f, 0.f);
    rope.setPlasticity(0.1f, 1.f);
    REQUIRE(rope.attach(0, 0.f, 0.f, 0.f).ok());
    REQUIRE(rope.attach(2, 1.f, 0.f, 0.f).ok());
    REQUIRE(rope.step({eve::SimulationTick{1}, eve::Duration::fromNanoseconds(16666667)}, SimulationSettings{1, 4, 1})
                .ok());
    CHECK(rope.getBendPlasticity(0) > 0.015f);
}

TEST_CASE("physics.rope.analyticCollidersUseStableIdsAndResolveContacts") {
    Rope3D rope(3, -1.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    rope.setGravity(0.f, 0.f, 0.f);
    rope.setRadius(0.1f);
    rope.setCollisionFriction(0.5f);
    auto plane = rope.addPlaneCollider(0.f, 0.f, 0.f, 0.f, 2.f, 0.f);
    REQUIRE(plane.ok());
    rope.update(1.f / 60.f);
    CHECK(rope.getParticleY(1) >= 0.099f);

    auto sphere = rope.addSphereCollider(0.f, 0.1f, 0.f, 0.5f);
    REQUIRE(sphere.ok());
    REQUIRE(rope.moveSphereCollider(sphere.value(), 0.f, 0.2f, 0.f).ok());
    REQUIRE(rope.removeCollider(sphere.value()).ok());
    auto stale = rope.removeCollider(sphere.value());
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::NotFound);
}

TEST_CASE("physics.rope.materialSamplingProvidesRendererNeutralFrames") {
    Rope3D rope(5, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f);
    CHECK(std::abs(rope.getSampleX(0.25f) - 1.f) < 1e-5f);
    CHECK(std::abs(rope.getSampleY(0.25f)) < 1e-5f);
    CHECK(std::abs(rope.getSampleTangentX(0.75f) - 1.f) < 1e-5f);
    CHECK(std::abs(rope.getSampleTangentY(0.75f)) < 1e-5f);
    CHECK(std::isfinite(rope.getSampleZ(std::numeric_limits<float>::quiet_NaN())));

    REQUIRE(rope.cut(1).ok());
    CHECK(std::isfinite(rope.getSampleX(0.5f)));
    CHECK(std::isfinite(rope.getSampleTangentZ(0.5f)));
}

TEST_CASE("physics.rope.continuousCollisionStopsAtTriangleMesh") {
    Physics                         physics;
    std::unique_ptr<World3D>        world(physics.newWorld3D(0.f, 0.f, 0.f, false));
    const std::vector<float>        vertices{-4.f, 0.f, -4.f, 4.f, 0.f, -4.f, 4.f, 0.f, 4.f, -4.f, 0.f, 4.f};
    const std::vector<std::int32_t> indices{0, 2, 1, 0, 3, 2};
    REQUIRE(world->newBody("static", 0.f, 0.f, 0.f)->newTriangleMeshShape(vertices, indices) != nullptr);

    Rope3D rope(3, -1.f, 1.f, 0.f, 1.f, 1.f, 0.f);
    rope.setCollideWorld(world.get());
    rope.setRadius(0.1f);
    rope.setGravity(0.f, -3000.f, 0.f);
    rope.update(0.05f);
    CHECK(rope.getParticleY(1) >= 0.099f);
}

TEST_CASE("physics.rope.sdfContinuousCollisionStopsAtImplicitSurface") {
    DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
    for (int z = 0; z < field.getDepth(); ++z)
        for (int y = 0; y < field.getHeight(); ++y)
            for (int x = 0; x < field.getWidth(); ++x) field.setDistance(x, y, z, -2.f + y);

    Rope3D rope(3, -1.f, 1.f, 0.f, 1.f, 1.f, 0.f);
    rope.setCollideSdf(&field);
    rope.setRadius(0.1f);
    rope.setGravity(0.f, -3000.f, 0.f);
    rope.update(0.05f);
    CHECK(rope.getParticleY(1) >= 0.099f);
}

TEST_CASE("physics.rope.dynamicRigidBodyReceivesReactionImpulse") {
    Physics                  physics;
    std::unique_ptr<World3D> world(physics.newWorld3D(0.f, 0.f, 0.f, false));
    Body3D*                  body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    REQUIRE(body->newSphereShape(0.5f, 1.f) != nullptr);

    Rope3D rope(3, -0.5f, 1.f, 0.f, 0.5f, 1.f, 0.f);
    rope.setCollideWorld(world.get());
    rope.setRadius(0.1f);
    rope.setParticleMass(0.25f);
    rope.setGravity(0.f, -1000.f, 0.f);
    for (int i = 0; i < 4; ++i) rope.update(0.05f);
    CHECK(body->getLinearVelocityY() < -0.01f);
}

TEST_CASE("physics.rope.creationSchemaBuildsConfiguredRope") {
    Rope ropeModule;
    REQUIRE(ropeModule.registerRope3DCreateSchema().ok());
    auto contract = eve::schema::SchemaRegistry::generateBindingContract("physics:rope3d-create", 1);
    REQUIRE(contract.ok());
    CHECK(contract.value().find("particleCount") != std::string::npos);
    auto created = ropeModule.newRope3DFromJson(R"({
        "particleCount":7,
        "startX":0,"startY":4,"startZ":0,
        "endX":3,"endY":4,"endZ":0,
        "gravityY":-4.5,
        "distanceConstraintsEnabled":true,
        "bendConstraintsEnabled":true,
        "maxBending":0.15,
        "damping":0.2,
        "particleMass":0.5,
        "radius":0.08,
        "continuousCollision":true,
        "collisionRestitution":0.2,
        "selfCollision":false,
        "pinStart":true,
        "pinEnd":true,
        "tearingEnabled":true,
        "tearResistance":25,
        "maxTearsPerStep":2
    })");
    REQUIRE(created.ok());
    std::unique_ptr<Rope3D> rope(created.value());
    CHECK_EQ(rope->getParticleCount(), 7);
    CHECK(std::abs(rope->getGravityY() + 4.5f) < 1e-6f);
    CHECK(std::abs(rope->getParticleMass() - 0.5f) < 1e-6f);
    CHECK(std::abs(rope->getMaxBending() - 0.15f) < 1e-6f);
    CHECK(rope->getContinuousCollision());
    CHECK(std::abs(rope->getCollisionRestitution() - 0.2f) < 1e-6f);
    CHECK(rope->isAttached(0));
    CHECK(rope->isAttached(6));
    CHECK(!rope->getSelfCollision());
    CHECK(eve::schema::SchemaRegistry::resolve("physics:rope3d-create", 1) != nullptr);
}

TEST_CASE("physics.rope.creationSchemaRejectsInvalidDocuments") {
    Rope ropeModule;
    auto missing = ropeModule.newRope3DFromJson(R"({"particleCount":4})");
    CHECK(!missing.ok());
    auto bounded = ropeModule.newRope3DFromJson(
        R"({"particleCount":1,"startX":0,"startY":0,"startZ":0,"endX":1,"endY":0,"endZ":0})");
    CHECK(!bounded.ok());
    auto unknown = ropeModule.newRope3DFromJson(
        R"({"particleCount":4,"startX":0,"startY":0,"startZ":0,"endX":1,"endY":0,"endZ":0,"typo":1})");
    CHECK(!unknown.ok());
}
