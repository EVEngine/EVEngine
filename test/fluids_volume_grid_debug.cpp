#include <glm/geometric.hpp>

#include "fluids/VolumeFluid.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

TEST_CASE("fluids.volume.gridDebuggerReturnsOnlyStableOccupiedCells") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-1.90f, .10f, -.90f};
    particles[1].position = {-1.85f, .15f, -.85f};
    particles[2].position = {-1.50f, .10f, -.90f};
    REQUIRE(solver->emit(particles).ok());

    auto cells = solver->debugParticleGrid(2);
    REQUIRE(cells.ok());
    REQUIRE(cells.value().size() == 2);
    REQUIRE(cells.value()[0].particleCount == 2);
    REQUIRE(cells.value()[1].particleCount == 1);
    REQUIRE(glm::length(cells.value()[0].center - glm::vec3(-1.9f, .1f, -.9f)) < 1e-5f);
    REQUIRE(glm::length(cells.value()[0].size - glm::vec3(.2f)) < 1e-6f);
    REQUIRE(cells.value()[0].center.x < cells.value()[1].center.x);
}

TEST_CASE("fluids.volume.gridDebuggerBudgetFailureDoesNotMutateSolver") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-1.9f, .1f, -.9f};
    particles[1].position = {1.9f, 2.9f, .9f};
    REQUIRE(solver->emit(particles).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->debugParticleGrid(1).ok());
    REQUIRE(!solver->debugParticleGrid(0).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == before.particles.size());
    REQUIRE(after.particles[0].position == before.particles[0].position);
    REQUIRE(after.particles[1].position == before.particles[1].position);
}

TEST_CASE("fluids.volume.particleFrameDebuggerReturnsActorLocalAxesOnDemand") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position    = {.5f, 1.f, .5f};
    particles[0].actorGroup  = 7;
    particles[0].orientation = {0.f, 0.f, .70710678f, .70710678f};
    particles[1].position    = {-.5f, 1.f, 0.f};
    particles[1].actorGroup  = 8;
    REQUIRE(solver->emit(particles).ok());
    auto frames = solver->debugParticleFrames(7, 2.f, 1);
    REQUIRE(frames.ok());
    REQUIRE(frames.value().size() == 1);
    REQUIRE(frames.value()[0].particleIndex == 0);
    REQUIRE(glm::length(frames.value()[0].x - glm::vec3(.5f, 3.f, .5f)) < 1e-5f);
    REQUIRE(glm::length(frames.value()[0].y - glm::vec3(-1.5f, 1.f, .5f)) < 1e-5f);
    REQUIRE(glm::length(frames.value()[0].z - glm::vec3(.5f, 1.f, 2.5f)) < 1e-5f);
}

TEST_CASE("fluids.volume.particleFrameDebuggerBudgetAndValidationAreAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].actorGroup = particles[1].actorGroup = 3;
    particles[0].position                             = {-.1f, 1.f, 0.f};
    particles[1].position                             = {.1f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->debugParticleFrames(3, 1.f, 1).ok());
    REQUIRE(!solver->debugParticleFrames(3, 0.f, 2).ok());
    REQUIRE(!solver->debugParticleFrames(4, 1.f, 2).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == before.particles.size());
    REQUIRE(after.particles[0].position == before.particles[0].position);
    REQUIRE(after.particles[1].position == before.particles[1].position);
}

TEST_CASE("fluids.volume.particleInstancesFilterScaleColorAndInterpolate") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position    = {0.f, 1.f, 0.f};
    particles[0].velocity    = {1.f, 0.f, 0.f};
    particles[0].radii       = {.1f, .2f, .3f};
    particles[0].orientation = {0.f, 0.f, .70710678f, .70710678f};
    particles[0].color       = {.2f, .3f, .4f, .5f};
    particles[0].actorGroup  = 7;
    particles[1].position    = {.5f, 1.f, 0.f};
    particles[1].actorGroup  = 8;
    particles[2].position    = {1.f, 1.f, 0.f};
    particles[2].actorGroup  = 7;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());

    auto instances = solver->particleInstances(7, {2.f, 3.f, 4.f}, .5f, 2);
    REQUIRE(instances.ok());
    REQUIRE(instances.value().size() == 2);
    REQUIRE(instances.value()[0].particleIndex == 0);
    REQUIRE(instances.value()[1].particleIndex == 2);
    CHECK(std::abs(instances.value()[0].position.x - 1.f / 240.f) < 1e-5f);
    CHECK(glm::length(instances.value()[0].scale - glm::vec3(.2f, .6f, 1.2f)) < 1e-5f);
    CHECK(instances.value()[0].orientation == particles[0].orientation);
    CHECK(instances.value()[0].color == particles[0].color);
}

TEST_CASE("fluids.volume.particleInstancesBudgetAndValidationAreAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position   = {0.f, 1.f, 0.f};
    particles[1].position   = {.2f, 1.f, 0.f};
    particles[0].actorGroup = particles[1].actorGroup = 3;
    REQUIRE(solver->emit(particles).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->particleInstances(3, glm::vec3(1.f), 1.f, 1).ok());
    REQUIRE(!solver->particleInstances(4, glm::vec3(1.f), 1.f, 2).ok());
    REQUIRE(!solver->particleInstances(3, {0.f, 1.f, 1.f}, 1.f, 2).ok());
    REQUIRE(!solver->particleInstances(3, glm::vec3(1.f), 1.1f, 2).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == before.particles.size());
    REQUIRE(after.particles[0].position == before.particles[0].position);
    REQUIRE(after.particles[1].position == before.particles[1].position);
}

TEST_CASE("fluids.volume.particleImpostorsApplyRadiusTintAndInterpolation") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();

    VolumeFluidParticle particle;
    particle.position    = {0.f, 1.f, 0.f};
    particle.velocity    = {2.f, 0.f, 0.f};
    particle.radii       = {.1f, .2f, .3f};
    particle.orientation = {0.f, 0.f, .70710677f, .70710677f};
    particle.color       = {.8f, .5f, .25f, .75f};
    particle.actorGroup  = 9;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());

    auto instances = solver->particleImpostors(9, 2.f, {.5f, .25f, 1.f, .5f}, .5f, 1);
    REQUIRE(instances.ok());
    REQUIRE(instances.value().size() == 1);
    CHECK(glm::length(instances.value()[0].position - glm::vec3(1.f / 120.f, 1.f, 0.f)) < 1e-5f);
    CHECK(glm::length(instances.value()[0].scale - glm::vec3(.2f, .4f, .6f)) < 1e-6f);
    CHECK(glm::length(instances.value()[0].color - glm::vec4(.4f, .125f, .25f, .375f)) < 1e-6f);
    CHECK(glm::length(instances.value()[0].orientation - particle.orientation) < 1e-6f);
    REQUIRE(!solver->particleImpostors(9, 0.f, glm::vec4(1.f), 1.f, 1).ok());
    REQUIRE(!solver->particleImpostors(9, 1.f, {1.1f, 1.f, 1.f, 1.f}, 1.f, 1).ok());
    REQUIRE(!solver->particleImpostors(9, 1.f, glm::vec4(1.f), 1.f, 0).ok());

    std::vector<VolumeFluidParticleInstance> reused;
    reused.reserve(2);
    REQUIRE(solver->copyParticleImpostors(9, 1.f, glm::vec4(1.f), 1.f, 1, reused).ok());
    const auto* storage = reused.data();
    REQUIRE(solver->copyParticleImpostors(9, 3.f, glm::vec4(.5f), 0.f, 1, reused).ok());
    CHECK(reused.data() == storage);
    CHECK(glm::length(reused[0].scale - glm::vec3(.3f, .6f, .9f)) < 1e-6f);
    const auto preserved = reused[0];
    REQUIRE(!solver->copyParticleImpostors(9, -1.f, glm::vec4(1.f), 1.f, 1, reused).ok());
    REQUIRE(reused.size() == 1);
    CHECK(reused[0].position == preserved.position);
    CHECK(reused[0].scale == preserved.scale);
    CHECK(reused[0].color == preserved.color);
}

TEST_CASE("fluids.volume.sdfSliceDebuggerSamplesAndTransformsOnDemand") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                   solver = std::move(created).takeValue();
    VolumeFluidSdfCollider collider;
    collider.label    = 42;
    collider.sdf      = MeshSdf::makeSphere({0.f, 0.f, 0.f}, .5f, {5, 7, 9});
    collider.position = {1.f, 2.f, 3.f};
    collider.rotation = {0.f, 0.f, .70710678f, .70710678f};
    collider.scale    = 2.f;
    REQUIRE(solver->setSdfColliders(std::span(&collider, 1)).ok());

    auto slice = solver->debugSdfSlice(42, VolumeFluidSdfSliceAxis::Z, .5f, .5f, 35);
    REQUIRE(slice.ok());
    REQUIRE(slice.value().width == 5);
    REQUIRE(slice.value().height == 7);
    REQUIRE(slice.value().values.size() == 35);
    CHECK(glm::length(slice.value().stepX - glm::vec3(0.f, collider.sdf.cellSize * 2.f, 0.f)) < 1e-5f);
    CHECK(glm::length(slice.value().stepY - glm::vec3(-collider.sdf.cellSize * 2.f, 0.f, 0.f)) < 1e-5f);
    const auto center = slice.value().values[size_t(3) * 5 + 2];
    CHECK(center < .1f);
}

TEST_CASE("fluids.volume.sdfSliceDebuggerRejectsWithoutPartialState") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                   solver = std::move(created).takeValue();
    VolumeFluidSdfCollider collider;
    collider.label = 7;
    collider.sdf   = MeshSdf::makeSphere(glm::vec3(0.f), .5f, {5, 5, 5});
    REQUIRE(solver->setSdfColliders(std::span(&collider, 1)).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->debugSdfSlice(8, VolumeFluidSdfSliceAxis::X, .5f, .5f, 25).ok());
    REQUIRE(!solver->debugSdfSlice(7, VolumeFluidSdfSliceAxis::X, .5f, .5f, 24).ok());
    REQUIRE(!solver->debugSdfSlice(7, VolumeFluidSdfSliceAxis(9), .5f, .5f, 25).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.sdfColliders.size() == before.sdfColliders.size());
    REQUIRE(after.sdfColliders[0].sdf.distances == before.sdfColliders[0].sdf.distances);
}
