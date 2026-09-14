#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCoupling.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>

using namespace eve::fluids;

TEST_CASE("fluids.volumeCoupling.attachmentReturnsGravityLoadAndTorque") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto*                                  body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    body->newBoxShape(1.f, 1.f, 1.f, 10.f);
    VolumeFluidSettings settings;
    settings.gravity = {0.f, -10.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                fluid = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle particle;
    particle.position       = {.5f, 1.f, 0.f};
    particle.material.phase = VolumeFluidPhase::Solid;
    snapshot.particles.push_back(particle);
    VolumeFluidCollider collider;
    collider.label    = 17;
    collider.radius   = .1f;
    collider.solidify = true;
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 17, {.5f, 0.f, 0.f}});
    REQUIRE(fluid->restore(snapshot).ok());
    VolumeFluidCoupling bridge;
    REQUIRE(bridge.attach(*body, collider).ok());
    const auto stepped = bridge.step(*world, *fluid, 1.f / 120.f, 1);
    REQUIRE(stepped.ok());
    REQUIRE(stepped.value() == 1u);
    REQUIRE(std::abs(body->getLinearVelocityY() + 1.f / 120.f) < 1e-5f);
    REQUIRE(body->getAngularVelocityZ() < 0.f);
    REQUIRE(fluid->attachmentReactions().size() == 1);
    REQUIRE(fluid->contacts().empty());
}

TEST_CASE("fluids.volumeCoupling.failedStepPreservesLastAttachmentReaction") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, -10.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                fluid = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle particle;
    particle.position       = {0.f, 1.f, 0.f};
    particle.material.phase = VolumeFluidPhase::Solid;
    snapshot.particles.push_back(particle);
    VolumeFluidCollider collider;
    collider.label    = 7;
    collider.solidify = true;
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 7, {0.f, 0.f, 0.f}});
    REQUIRE(fluid->restore(snapshot).ok());
    REQUIRE(fluid->step(1.f / 120.f, 1).ok());
    const auto before = fluid->attachmentReactions().front().impulse;
    REQUIRE(!fluid->step(1.f, 1).ok());
    REQUIRE(fluid->attachmentReactions().size() == 1);
    REQUIRE(fluid->attachmentReactions().front().impulse == before);
    fluid->clear();
    REQUIRE(fluid->attachmentReactions().empty());
}

TEST_CASE("fluids.volumeCoupling.extremeMassRatioUsesObservableVelocityLimits") {
    auto*                                  physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto*                                  body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    body->newBoxShape(.1f, .1f, .1f, .1f);
    body->setAngularVelocity(0.f, 0.f, 10.f);
    VolumeFluidSettings settings;
    settings.gravity = {0.f, -100.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                fluid = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    for (unsigned i = 0; i < 16; ++i) {
        VolumeFluidParticle p;
        p.position       = {-.3f + .04f * i, 1.f, 0.f};
        p.material.phase = VolumeFluidPhase::Solid;
        p.radii          = {.05f, .1f, .05f};
        snapshot.particles.push_back(p);
        snapshot.attachments.push_back({i, 9, p.position - glm::vec3(0, 1, 0)});
    }
    VolumeFluidCollider collider;
    collider.label  = 9;
    collider.radius = .01f;
    snapshot.colliders.push_back(collider);
    REQUIRE(fluid->restore(snapshot).ok());
    VolumeFluidCoupling bridge;
    REQUIRE(bridge.attach(*body, collider).ok());
    REQUIRE(bridge.setImpulseLimits(.5f, .25f).ok());
    REQUIRE(!bridge.setImpulseLimits(0.f, .25f).ok());
    REQUIRE(bridge.step(*world, *fluid, 1.f / 120.f, 1).ok());
    REQUIRE(std::abs(body->getLinearVelocityY()) <= .5001f);
    REQUIRE(std::abs(body->getAngularVelocityZ() - 10.f) <= .2501f);
    REQUIRE(bridge.lastClampedBodyCount() == 1u);
}
