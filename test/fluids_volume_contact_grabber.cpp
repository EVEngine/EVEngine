#include <glm/geometric.hpp>

#include "fluids/VolumeFluid.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

namespace {
std::unique_ptr<VolumeFluid> contactGrabberSolver(std::span<const VolumeFluidParticle> particles,
                                                  std::span<const VolumeFluidCollider> colliders) {
    VolumeFluidSettings settings;
    settings.gravity    = {0.f, -10.f, 0.f};
    settings.iterations = 2;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->setColliders(colliders).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    return solver;
}
}  // namespace

TEST_CASE("fluids.volume.contactGrabberCapturesMovesFreezesAndReleases") {
    VolumeFluidParticle particle;
    particle.position = {.2f, .5f, 0.f};
    VolumeFluidCollider sphere;
    sphere.label  = 41;
    sphere.center = {0.f, .5f, 0.f};
    sphere.radius = .5f;
    auto solver   = contactGrabberSolver(std::span(&particle, 1), std::span(&sphere, 1));

    REQUIRE(solver->grabContactParticles(41, sphere.center, {0.f, 0.f, 0.f, 1.f}, .01f).value() == 1);
    const auto captured = solver->particles()[0].position;
    REQUIRE(
        solver->updateGrabbedParticles(41, sphere.center + glm::vec3(.3f, .2f, 0.f), {0.f, 0.f, 0.f, 1.f}).value() ==
        1);
    const auto moved = solver->particles()[0].position;
    REQUIRE(glm::length(moved - captured - glm::vec3(.3f, .2f, 0.f)) < 1e-6f);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(solver->particles()[0].position - moved) < 1e-6f);
    REQUIRE(glm::length(solver->particles()[0].velocity) == 0.f);

    REQUIRE(solver->releaseGrabbedParticles(41).value() == 1);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.y < moved.y);
    REQUIRE(solver->releaseGrabbedParticles(41).value() == 0);
}

TEST_CASE("fluids.volume.contactGrabberRemapsDeathAndReleasesMissingCollider") {
    VolumeFluidParticle particles[2];
    particles[0].position = {.15f, .5f, 0.f};
    particles[1].position = {.25f, .5f, 0.f};
    VolumeFluidCollider sphere;
    sphere.label  = 42;
    sphere.center = {0.f, .5f, 0.f};
    sphere.radius = .5f;
    auto solver   = contactGrabberSolver(particles, std::span(&sphere, 1));
    REQUIRE(solver->grabContactParticles(42, sphere.center, {0.f, 0.f, 0.f, 1.f}, .01f).value() == 2);

    REQUIRE(solver->killParticle(0).ok());
    const auto before = solver->particles()[0].position;
    REQUIRE(
        solver->updateGrabbedParticles(42, sphere.center + glm::vec3(.2f, 0.f, 0.f), {0.f, 0.f, 0.f, 1.f}).value() ==
        1);
    REQUIRE(std::abs(solver->particles()[0].position.x - before.x - .2f) < 1e-6f);

    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->releaseGrabbedParticles(42).value() == 0);
    const float y = solver->particles()[0].position.y;
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.y < y);
}

TEST_CASE("fluids.volume.contactGrabberValidationAndOwnershipAreAtomic") {
    VolumeFluidParticle particle;
    particle.position = {.2f, .5f, 0.f};
    VolumeFluidCollider colliders[2];
    colliders[0].label     = 43;
    colliders[0].center    = {0.f, .5f, 0.f};
    colliders[0].radius    = .5f;
    colliders[0].isTrigger = true;
    colliders[1]           = colliders[0];
    colliders[1].label     = 44;
    auto solver            = contactGrabberSolver(std::span(&particle, 1), colliders);
    REQUIRE(solver->grabContactParticles(43, colliders[0].center, {0.f, 0.f, 0.f, 1.f}, .01f).value() == 1);
    const auto before = solver->particles()[0].position;
    REQUIRE(!solver->grabContactParticles(44, colliders[1].center, {0.f, 0.f, 0.f, 1.f}, .01f).ok());
    REQUIRE(!solver->updateGrabbedParticles(43, {100.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}).ok());
    REQUIRE(glm::length(solver->particles()[0].position - before) < 1e-6f);
    const auto persistent = solver->snapshot();
    REQUIRE(solver->restore(persistent).ok());
    REQUIRE(solver->releaseGrabbedParticles(43).value() == 0);
}
