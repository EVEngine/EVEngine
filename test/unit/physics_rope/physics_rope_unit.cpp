#include "physics/DistanceField3D.h"
#include "physics/rope/Rope.h"
#include "physics/rope/Rope3D.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

using namespace eve::physics;

namespace {
bool near(float actual, float expected, float tolerance = 1e-5f) { return std::fabs(actual - expected) < tolerance; }

eve::SimulationStep tick(std::uint64_t index) {
    return {eve::SimulationTick{index}, eve::Duration::fromNanoseconds(16666667)};
}
}  // namespace

TEST_CASE("physics.rope.spec.simulation") {
    SUB_CASE("pinned endpoint stays fixed while gravity changes the free center") {
        Rope3D rope(9, 0.f, 3.f, 0.f, 4.f, 3.f, 0.f);
        REQUIRE(rope.getParticleCount() == 9);
        REQUIRE(rope.getElementCount() == 8);
        REQUIRE(near(rope.getRestLength(), 4.f));
        REQUIRE(rope.pin(0).ok());
        REQUIRE(rope.attach(8, 4.f, 3.f, 0.f).ok());
        rope.setGravity(0.f, -9.81f, 0.f);
        for (std::uint64_t i = 1; i <= 20; ++i) REQUIRE(rope.step(tick(i), SimulationSettings{8, 6, 4}).ok());
        REQUIRE(rope.observation().stepCount == 20);
        REQUIRE(near(rope.getParticleX(0), 0.f));
        REQUIRE(near(rope.getParticleY(0), 3.f));
        REQUIRE(rope.getParticleY(4) < 3.f);
        REQUIRE(rope.calculateLength() >= rope.getRestLength() * .99f);
    };
    SUB_CASE("automatic tearing respects per-step cap and local resistance") {
        Rope3D rope(5, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f);
        REQUIRE(rope.attach(0, 0.f, 0.f, 0.f).ok());
        REQUIRE(rope.attach(4, 8.f, 0.f, 0.f).ok());
        REQUIRE(rope.setElementTearResistance(0, 1000000.f).ok());
        REQUIRE(!rope.setElementTearResistance(99, 1.f).ok());
        rope.setTearing(.01f, 1);
        REQUIRE(rope.step(tick(1), SimulationSettings{8, 8, 1}).ok());
        REQUIRE(rope.isElementActive(0));
        REQUIRE(rope.getLastTornElementCount() == 1);
        REQUIRE(rope.getLastTornElement(0) > 0);
        REQUIRE(rope.getTopologyRevision() == 1);
    };
    SUB_CASE("plasticity retains a persistent bend") {
        Rope3D rope(3, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f);
        rope.setGravity(0.f, 0.f, 0.f);
        rope.setPlasticity(.1f, 1.f);
        REQUIRE(rope.attach(0, 0.f, 0.f, 0.f).ok());
        REQUIRE(rope.attach(2, 1.f, 0.f, 0.f).ok());
        REQUIRE(rope.step(tick(1), SimulationSettings{1, 4, 1}).ok());
        REQUIRE(rope.getBendPlasticity(0) > .015f);
    };
}

TEST_CASE("physics.rope.spec.topology") {
    SUB_CASE("rest length, cut, repeated cut, and repair report observable state") {
        Rope3D rope(6, 0.f, 2.f, 0.f, 0.f, -3.f, 0.f);
        REQUIRE(rope.setRestLength(3.f).ok());
        REQUIRE(near(rope.getRestLength(), 3.f));
        REQUIRE(rope.cut(2).ok());
        REQUIRE(!rope.isElementActive(2));
        REQUIRE(near(rope.getRestLength(), 2.4f, 1e-4f));
        auto repeated = rope.cut(2);
        REQUIRE(repeated.ok());
        REQUIRE(repeated.value() == RopeTopologyChange::Unchanged);
        REQUIRE(rope.repair(2).ok());
        REQUIRE(rope.isElementActive(2));
        REQUIRE(rope.getElementForce(2) == 0.f);
        auto invalid = rope.attach(99, 0.f, 0.f, 0.f);
        REQUIRE(!invalid.ok());
        REQUIRE(invalid.code() == eve::StatusCode::Rejected);
    };
    SUB_CASE("cursor resize transfers terminal attachment and rejects a cut rope") {
        Rope3D rope(5, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f);
        REQUIRE(rope.attach(4, 4.f, 1.f, 0.f).ok());
        auto revision = rope.getTopologyRevision();
        REQUIRE(rope.changeLength(6.f, .5f, true).ok());
        REQUIRE(rope.getParticleCount() == 9);
        REQUIRE(near(rope.getRestLength(), 6.f));
        REQUIRE(rope.getTopologyRevision() > revision);
        REQUIRE(rope.changeLength(2.5f, .5f, true).ok());
        REQUIRE(rope.isAttached(rope.getParticleCount() - 1));
        REQUIRE(rope.cut(0).ok());
        auto rejected = rope.changeLength(3.f, .5f, true);
        REQUIRE(!rejected.ok());
        REQUIRE(rejected.code() == eve::StatusCode::Conflict);
    };
    SUB_CASE("nonfinite compatibility inputs throw without corrupting positions") {
        Rope3D rope(3, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f);
        rope.update(0.f);
        CHECK_THROWS((rope.update(-.01f), false));
        CHECK_THROWS((rope.update(std::numeric_limits<float>::quiet_NaN()), false));
        CHECK_THROWS((rope.setGravity(0.f, std::numeric_limits<float>::infinity(), 0.f), false));
        CHECK_THROWS((rope.applyForce(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f), false));
        REQUIRE(std::isfinite(rope.getParticleX(1)));
    };
}

TEST_CASE("physics.rope.spec.collisionAndSampling") {
    SUB_CASE("analytic collider contact and stable-id removal") {
        Rope3D rope(3, -1.f, 0.f, 0.f, 1.f, 0.f, 0.f);
        rope.setGravity(0.f, 0.f, 0.f);
        rope.setRadius(.1f);
        auto plane = rope.addPlaneCollider(0.f, 0.f, 0.f, 0.f, 2.f, 0.f);
        REQUIRE(plane.ok());
        rope.update(1.f / 60.f);
        REQUIRE(rope.getParticleY(1) >= .099f);
        auto sphere = rope.addSphereCollider(0.f, .1f, 0.f, .5f);
        REQUIRE(sphere.ok());
        REQUIRE(rope.moveSphereCollider(sphere.value(), 0.f, .2f, 0.f).ok());
        REQUIRE(rope.removeCollider(sphere.value()).ok());
        auto stale = rope.removeCollider(sphere.value());
        REQUIRE(!stale.ok());
        REQUIRE(stale.code() == eve::StatusCode::NotFound);
    };
    SUB_CASE("implicit SDF contact prevents floor penetration") {
        DistanceField3D field(5, 5, 5, 1.f, -2.f, -2.f, -2.f, 100.f);
        for (int z = 0; z < field.getDepth(); ++z)
            for (int y = 0; y < field.getHeight(); ++y)
                for (int x = 0; x < field.getWidth(); ++x) field.setDistance(x, y, z, -2.f + y);
        Rope3D rope(3, -1.f, 1.f, 0.f, 1.f, 1.f, 0.f);
        rope.setCollideSdf(&field);
        rope.setRadius(.1f);
        rope.setGravity(0.f, -3000.f, 0.f);
        rope.update(.05f);
        REQUIRE(rope.getParticleY(1) >= .099f);
    };
    SUB_CASE("material sampling stays finite after a cut") {
        Rope3D rope(5, 0.f, 0.f, 0.f, 4.f, 0.f, 0.f);
        REQUIRE(near(rope.getSampleX(.25f), 1.f));
        REQUIRE(near(rope.getSampleY(.25f), 0.f));
        REQUIRE(near(rope.getSampleTangentX(.75f), 1.f));
        REQUIRE(std::isfinite(rope.getSampleZ(std::numeric_limits<float>::quiet_NaN())));
        REQUIRE(rope.cut(1).ok());
        REQUIRE(std::isfinite(rope.getSampleX(.5f)));
        REQUIRE(std::isfinite(rope.getSampleTangentZ(.5f)));
    };
}

TEST_CASE("physics.rope.spec.creationSchema") {
    Rope  module;
    auto* moduleView = &module;
    REQUIRE(module.registerRope3DCreateSchema().ok());
    auto contract = eve::schema::SchemaRegistry::generateBindingContract("physics:rope3d-create", 1);
    REQUIRE(contract.ok());
    REQUIRE(contract.value().find("particleCount") != std::string::npos);
    SUB_CASE("valid document configures particles, gravity, constraints, and attachments") {
        auto created = moduleView->newRope3DFromJson(R"({
      "particleCount":7,"startX":0,"startY":4,"startZ":0,"endX":3,"endY":4,"endZ":0,
      "gravityY":-4.5,"distanceConstraintsEnabled":true,"bendConstraintsEnabled":true,
      "maxBending":0.15,"damping":0.2,"particleMass":0.5,"radius":0.08,
      "continuousCollision":true,"collisionRestitution":0.2,"selfCollision":false,
      "pinStart":true,"pinEnd":true,"tearingEnabled":true,"tearResistance":25,"maxTearsPerStep":2
    })");
        REQUIRE(created.ok());
        std::unique_ptr<Rope3D> rope(created.value());
        REQUIRE(rope->getParticleCount() == 7);
        REQUIRE(near(rope->getGravityY(), -4.5f));
        REQUIRE(near(rope->getParticleMass(), .5f));
        REQUIRE(rope->getContinuousCollision());
        REQUIRE(rope->isAttached(0));
        REQUIRE(rope->isAttached(6));
        REQUIRE(!rope->getSelfCollision());
    };
    SUB_CASE("missing, out-of-range, and unknown fields fail") {
        REQUIRE(!moduleView->newRope3DFromJson(R"({"particleCount":4})").ok());
        REQUIRE(!moduleView
                     ->newRope3DFromJson(
                         R"({"particleCount":1,"startX":0,"startY":0,"startZ":0,"endX":1,"endY":0,"endZ":0})")
                     .ok());
        REQUIRE(!moduleView
                     ->newRope3DFromJson(
                         R"({"particleCount":4,"startX":0,"startY":0,"startZ":0,"endX":1,"endY":0,"endZ":0,"typo":1})")
                     .ok());
    };
}
