#include "PrivateTestScope.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <glm/geometric.hpp>

#include "common/SdfField3D.h"
#include "fluids/FluidMath.h"
#include "fluids/FluidSdf.h"
#include "fluids/FluidSimulation.h"
#include "fluids/FluidSurfaceBinding.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/Fluids.h"
#include "fluids/MeshSdfField.h"
#include "fluids/OceanBoat.h"
#include "fluids/OceanHullMask.h"
#include "fluids/OceanInteractionCodec.h"
#include "fluids/OceanMesh.h"
#include "fluids/OceanSeaTransition.h"
#include "fluids/OceanSpectrum.h"
#include "fluids/OceanSpectrumCodec.h"
#include "fluids/OceanSpray.h"
#include "fluids/OceanSurface.h"
#include "fluids/OceanWake.h"
#include "fluids/OceanWaterView.h"
#include "fluids/ShallowWaterField.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceWetnessField.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidCoupling.h"
#include "fluids/VolumeFluidEmitter.h"
#include "image/ImageData.h"
#include "physics/DistanceField3D.h"


// Responsibility: fluids_emitters_coupling_tests
namespace test_fluids_combined_fluids_emitters_coupling_tests {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_collision_material)

namespace test_volume_collision_contract {
using namespace eve::fluids;

std::unique_ptr<VolumeFluid> contactSolver(VolumeFluidParticle particle) {
    VolumeFluidSettings settings;
    settings.gravity    = {0.0f, 0.0f, 0.0f};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    return solver;
}

TEST_CASE("fluids.VolumeFluid collision material: minimum suppresses adhesion and maximum pulls toward contact") {
    VolumeFluidParticle particle;
    particle.position                   = {0.56f, 0.0f, 0.0f};
    particle.material.stickiness        = 0.0f;
    particle.material.stickDistance     = 0.1f;
    particle.material.stickinessCombine = VolumeFluidMaterialCombineMode::Minimum;
    VolumeFluidCollider sphere;
    sphere.radius            = 0.5f;
    sphere.stickiness        = 10.0f;
    sphere.stickDistance     = 0.1f;
    sphere.stickinessCombine = VolumeFluidMaterialCombineMode::Minimum;
    auto minimum             = contactSolver(particle);
    REQUIRE(minimum->setColliders(std::span(&sphere, 1)).ok());
    REQUIRE(minimum->step(0.01f, 1).ok());
    CHECK(std::fabs(minimum->particles()[0].position.x - 0.56f) < 1e-6f);
    sphere.stickinessCombine = VolumeFluidMaterialCombineMode::Maximum;
    auto maximum             = contactSolver(particle);
    REQUIRE(maximum->setColliders(std::span(&sphere, 1)).ok());
    REQUIRE(maximum->step(0.01f, 1).ok());
    CHECK(maximum->particles()[0].position.x < 0.56f);
    CHECK_EQ(maximum->contacts().size(), 1u);
    CHECK(maximum->contacts()[0].impulse.x > 0.0f);
}

TEST_CASE("fluids.VolumeFluid setColliders/emit: invalid material rejects without publishing partial state") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider valid;
    valid.label = 7;
    REQUIRE(solver->setColliders(std::span(&valid, 1)).ok());
    auto invalid            = valid;
    invalid.label           = 8;
    invalid.frictionCombine = static_cast<VolumeFluidMaterialCombineMode>(99);
    CHECK(!solver->setColliders(std::span(&invalid, 1)).ok());
    REQUIRE_EQ(solver->snapshot().colliders.size(), 1u);
    CHECK_EQ(solver->snapshot().colliders[0].label, 7);
    VolumeFluidParticle particle;
    particle.material.stickDistance = 10.01f;
    CHECK(!solver->emit(std::span(&particle, 1)).ok());
    CHECK_EQ(solver->particleCount(), 0u);
}

TEST_CASE("fluids.VolumeFluidContactTracker advance: contact identity emits enter/stay/exit once") {
    VolumeFluidContactTracker tracker;
    VolumeFluidContact        contact;
    contact.colliderLabel              = 4;
    contact.actorGroup                 = 2;
    contact.particleIndex              = 7;
    contact.distance                   = 0.005f;
    auto duplicate                     = contact;
    duplicate.particleIndex            = 8;
    const VolumeFluidContact initial[] = {contact, duplicate};
    auto                     entered   = tracker.advance(initial, 0.01f);
    REQUIRE(entered.ok());
    REQUIRE_EQ(entered.value().size(), 1u);
    CHECK_EQ(entered.value()[0].type, VolumeFluidContactEventType::Enter);
    // Equal-key contacts retain the first input because deduplication follows a stable sort.
    REQUIRE_EQ(entered.value()[0].contact.particleIndex, 7u);
    auto stayed = tracker.advance(std::span(&contact, 1), 0.01f);
    REQUIRE(stayed.ok());
    REQUIRE_EQ(stayed.value().size(), 1u);
    CHECK_EQ(stayed.value()[0].type, VolumeFluidContactEventType::Stay);
    auto exited = tracker.advance({}, 0.01f);
    REQUIRE(exited.ok());
    REQUIRE_EQ(exited.value().size(), 1u);
    CHECK_EQ(exited.value()[0].type, VolumeFluidContactEventType::Exit);
    CHECK(!tracker.advance({}, std::numeric_limits<float>::quiet_NaN()).ok());
}

TEST_CASE("fluids.VolumeFluid actor filter: updates only matching particles and rejects invalid category") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position        = {0.0f, 1.0f, 0.0f};
    particles[0].actorGroup      = 7;
    particles[0].collisionFilter = 0x00f00001u;
    particles[1]                 = particles[0];
    particles[1].position.x      = 0.2f;
    particles[1].actorGroup      = 8;
    particles[1].collisionFilter = 0xff000004u;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->setActorFilterCategory(7, 3).ok());
    CHECK_EQ(solver->snapshot().particles[0].collisionFilter, 0x00f00008u);
    CHECK_EQ(solver->snapshot().particles[1].collisionFilter, 0xff000004u);
    CHECK(!solver->setActorFilterCategory(7, 16).ok());
    CHECK_EQ(solver->snapshot().particles[0].collisionFilter, 0x00f00008u);
}

TEST_CASE("fluids.VolumeFluidDiffuse advance: advection expires independently of source fluid") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                      fluid = std::move(created).takeValue();
    const VolumeFluidParticle source{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    REQUIRE(fluid->emit(std::span(&source, 1)).ok());
    VolumeFluidDiffuse               pool;
    const VolumeFluidDiffuseParticle particle{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.03f};
    REQUIRE(pool.emit(std::span(&particle, 1)).ok());
    REQUIRE(pool.advance(*fluid, 0.01f, 1).ok());
    REQUIRE_EQ(pool.particleCount(), 1u);
    CHECK(std::fabs(pool.snapshot().particles[0].position.x - 0.01f) < 1e-6f);
    CHECK_EQ(fluid->particles()[0].position.x, 0.0f);
    REQUIRE(pool.advance(*fluid, 0.03f, 1).ok());
    CHECK_EQ(pool.particleCount(), 0u);
}
}  // namespace test_volume_collision_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_coupling)

namespace test_volume_coupling_contract {
TEST_CASE("fluids.VolumeFluid attachment reaction: failed step retains last published impulse") {
    eve::fluids::VolumeFluidSettings settings;
    settings.gravity = {0.0f, -10.0f, 0.0f};
    auto created     = eve::fluids::VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             fluid = std::move(created).takeValue();
    eve::fluids::VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    eve::fluids::VolumeFluidParticle particle;
    particle.position       = {0.0f, 1.0f, 0.0f};
    particle.material.phase = eve::fluids::VolumeFluidPhase::Solid;
    snapshot.particles.push_back(particle);
    eve::fluids::VolumeFluidCollider collider;
    collider.label    = 7;
    collider.solidify = true;
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 7, {0.0f, 0.0f, 0.0f}});
    REQUIRE(fluid->restore(snapshot).ok());
    REQUIRE(fluid->step(1.0f / 120.0f, 1).ok());
    REQUIRE_EQ(fluid->attachmentReactions().size(), 1u);
    const auto previousImpulse = fluid->attachmentReactions().front().impulse;
    CHECK(!fluid->step(1.0f, 1).ok());
    REQUIRE_EQ(fluid->attachmentReactions().size(), 1u);
    CHECK_EQ(fluid->attachmentReactions().front().impulse, previousImpulse);
    fluid->clear();
    CHECK(fluid->attachmentReactions().empty());
}
}  // namespace test_volume_coupling_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_emitter)

namespace test_volume_emitter_contract {
using namespace eve::fluids;
TEST_CASE("fluids.emitVolumeFluidBurst: seed reproduces shape positions and invalid bounds publish nothing") {
    for (auto shape : {VolumeFluidEmissionShape::Edge, VolumeFluidEmissionShape::Disk, VolumeFluidEmissionShape::Sphere,
                       VolumeFluidEmissionShape::Cube}) {
        auto left  = VolumeFluid::create({});
        auto right = VolumeFluid::create({});
        REQUIRE(left.ok());
        REQUIRE(right.ok());
        auto                a = std::move(left).takeValue();
        auto                b = std::move(right).takeValue();
        VolumeFluidEmission emission;
        emission.shape  = shape;
        emission.extent = {0.2f, 0.2f, 0.2f};
        emission.seed   = 42;
        emission.jitter = 0.2f;
        REQUIRE(emitVolumeFluidBurst(*a, emission, 8).ok());
        REQUIRE(emitVolumeFluidBurst(*b, emission, 8).ok());
        REQUIRE_EQ(a->particleCount(), 8u);
        for (std::size_t index = 0; index < 8; ++index)
            CHECK(glm::length(a->particles()[index].position - b->particles()[index].position) == 0.0f);
        emission.origin.y = -10.0f;
        CHECK(!emitVolumeFluidBurst(*a, emission, 1).ok());
        CHECK_EQ(a->particleCount(), 8u);
    }
}

TEST_CASE("fluids.VolumeFluidEmitter advance: work cap bounds emission and pool capacity is recovered") {
    auto created = VolumeFluid::create(VolumeFluidSettings{.capacity = 10});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmitter  stream;
    VolumeFluidEmission emission;
    emission.prototype.life = 0.01f;
    auto first              = stream.advance(*solver, emission, 1.0f / 60.0f, 6000.0f, 3);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), 3u);
    auto noDebt = stream.advance(*solver, emission, 0.0f, 0.0f, 3);
    REQUIRE(noDebt.ok());
    CHECK_EQ(noDebt.value(), 0u);
    for (int index = 0; index < 3; ++index) REQUIRE(stream.advance(*solver, emission, 1.0f / 60.0f, 6000.0f, 3).ok());
    CHECK_EQ(solver->particleCount(), 10u);
    REQUIRE(solver->step(1.0f / 60.0f).ok());
    CHECK_EQ(solver->particleCount(), 0u);
    auto reused = stream.advance(*solver, emission, 1.0f / 60.0f, 6000.0f, 3);
    REQUIRE(reused.ok());
    CHECK_EQ(reused.value(), 3u);
}

TEST_CASE("fluids.VolumeFluidEmitter advanceBurst: one burst per live actor and failed request preserves sequence") {
    VolumeFluidSettings settings;
    settings.capacity = 4;
    settings.gravity  = {0.0f, 0.0f, 0.0f};
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmitter  emitter;
    VolumeFluidEmission emission;
    emission.seed                 = 17;
    emission.prototype.actorGroup = 7;
    emission.prototype.life       = 0.01f;
    auto first                    = emitter.advanceBurst(*solver, emission, 2, 0.0f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), 2u);
    auto waiting = emitter.advanceBurst(*solver, emission, 2, 0.0f);
    REQUIRE(waiting.ok());
    CHECK_EQ(waiting.value(), 0u);
    const auto saved = emitter.snapshot();
    CHECK(!emitter.advanceBurst(*solver, emission, 0, 0.0f).ok());
    CHECK_EQ(emitter.snapshot().sequence, saved.sequence);
    CHECK_EQ(solver->particleCount(), 2u);
    REQUIRE(solver->killParticle(1).ok());
    REQUIRE(solver->killParticle(0).ok());
    auto restarted = emitter.advanceBurst(*solver, emission, 2, 0.0f);
    REQUIRE(restarted.ok());
    CHECK_EQ(restarted.value(), 2u);
}

TEST_CASE("fluids.VolumeFluidJetEmitter advance: temporal layers respect spacing and failure keeps phase") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidJetEmitter jet;
    VolumeFluidEmission   emission;
    emission.direction = {0.0f, 0.0f, 1.0f};
    emission.extent    = {0.1f, 0.1f, 0.1f};
    emission.speed     = 12.0f;
    auto emitted       = jet.advance(*solver, emission, 0.025f, 32, 0.0f);
    REQUIRE(emitted.ok());
    CHECK_EQ(emitted.value(), 15u);
    const auto previousPhase = jet.snapshot().phase;
    auto       invalid       = emission;
    invalid.origin.y         = -10.0f;
    CHECK(!jet.advance(*solver, invalid, 0.02f, 32).ok());
    CHECK_EQ(jet.snapshot().phase, previousPhase);
}

TEST_CASE("fluids.VolumeFluidEmission codec: known version round trips and unknown fields reject") {
    VolumeFluidEmission source;
    source.seed  = 123;
    source.speed = 3.0f;
    auto encoded = encodeVolumeFluidEmission(source);
    auto decoded = decodeVolumeFluidEmission(encoded);
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().seed, 123u);
    CHECK_EQ(decoded.value().speed, 3.0f);
    SUB_CASE("future version") {
        auto invalid = encoded;
        invalid.set("version", 99);
        CHECK(!decodeVolumeFluidEmission(invalid).ok());
    };
    SUB_CASE("unknown field") {
        auto invalid = encoded;
        invalid.set("extra", 1);
        CHECK(!decodeVolumeFluidEmission(invalid).ok());
    };
}
}  // namespace test_volume_emitter_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_emitter_part2)

namespace test_volume_emitter_runtime_contract {
using namespace eve::fluids;
TEST_CASE("fluids.VolumeFluidJetEmitter advanceMoving: invalid pose leaves phase and solver unchanged") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidJetEmitter jet;
    VolumeFluidEmission   emission;
    emission.shape  = VolumeFluidEmissionShape::Edge;
    emission.extent = {0.0f, 0.0f, 0.0f};
    emission.speed  = 12.0f;
    VolumeFluidNozzlePose begin{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    VolumeFluidNozzlePose end{{0.3f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    auto                  invalid = end;
    invalid.rotation              = {0.0f, 0.0f, 0.0f, 0.0f};
    const auto saved              = jet.snapshot();
    CHECK(!jet.advanceMoving(*solver, emission, begin, invalid, 0.025f, 16, 0.0f, 1.0f).ok());
    CHECK_EQ(jet.snapshot().phase, saved.phase);
    CHECK_EQ(solver->particleCount(), 0u);
    auto emitted = jet.advanceMoving(*solver, emission, begin, end, 0.025f, 16, 0.0f, 1.0f);
    REQUIRE(emitted.ok());
    CHECK(emitted.value() > 0);
    for (const auto& particle : solver->particles()) {
        CHECK(std::fabs(particle.position.x - 0.3f) < 1e-5f);
        CHECK(std::fabs(particle.velocity.x - 12.0f) < 1e-5f);
    }
}

TEST_CASE("fluids.VolumeFluid particle events: bounded ring records emission and explicit kill") {
    VolumeFluidSettings settings;
    settings.gravity  = {0.0f, 0.0f, 0.0f};
    settings.capacity = 8;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->configureParticleEvents(2).ok());
    VolumeFluidParticle first;
    first.position    = {0.0f, 1.0f, 0.0f};
    first.actorGroup  = 7;
    auto second       = first;
    second.position.x = 0.2f;
    REQUIRE(solver->emit(std::array{first, second}).ok());
    auto emitted = solver->drainParticleEvents();
    REQUIRE_EQ(emitted.events.size(), 2u);
    CHECK_EQ(emitted.dropped, 0u);
    CHECK_EQ(emitted.events[0].type, VolumeFluidParticleEventType::Emitted);
    REQUIRE(solver->killParticle(1).ok());
    auto killed = solver->drainParticleEvents();
    REQUIRE_EQ(killed.events.size(), 1u);
    CHECK_EQ(killed.events[0].type, VolumeFluidParticleEventType::Killed);
    CHECK_EQ(killed.events[0].actorGroup, 7u);
    CHECK(!solver->configureParticleEvents(65537).ok());
}

TEST_CASE("fluids.VolumeFluidEmitter checkpoint: invalid controller does not partially restore") {
    VolumeFluidSettings settings;
    settings.gravity  = {0.0f, 0.0f, 0.0f};
    settings.capacity = 8;
    auto sourceResult = VolumeFluid::create(settings);
    auto targetResult = VolumeFluid::create(settings);
    REQUIRE(sourceResult.ok());
    REQUIRE(targetResult.ok());
    auto                source = std::move(sourceResult).takeValue();
    auto                target = std::move(targetResult).takeValue();
    VolumeFluidEmission emission;
    emission.shape                = VolumeFluidEmissionShape::Edge;
    emission.extent               = {0.0f, 0.0f, 0.0f};
    emission.speed                = 3.0f;
    emission.seed                 = 47;
    emission.prototype.actorGroup = 5;
    VolumeFluidEmitter writer, reader;
    auto               first = writer.advance(*source, emission, 0.02f, 120.0f, 2, 0.0f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), 2u);
    auto encoded = encodeVolumeFluidEmitterCheckpoint(captureVolumeFluidEmitterCheckpoint(*source, emission, writer));
    auto decoded = decodeVolumeFluidEmitterCheckpoint(encoded);
    REQUIRE(decoded.ok());
    auto invalid            = decoded.value();
    invalid.controller.kind = 1;
    CHECK(!restoreVolumeFluidEmitterCheckpoint(*target, reader, invalid).ok());
    CHECK_EQ(target->particleCount(), 0u);
    REQUIRE(restoreVolumeFluidEmitterCheckpoint(*target, reader, decoded.value()).ok());
    CHECK_EQ(target->particleCount(), source->particleCount());
    CHECK_EQ(reader.snapshot().sequence, writer.snapshot().sequence);
    encoded.set("extra", 1);
    CHECK(!decodeVolumeFluidEmitterCheckpoint(encoded).ok());
}

TEST_CASE("fluids.VolumeFluidFoam restore/advance: credit and capacity remain unchanged on invalid state") {
    VolumeFluidFoam foam;
    auto            state             = foam.snapshot();
    state.settings.rate               = 1.0f;
    state.credit                      = "0.99";
    state.settings.vorticityThreshold = 0.0f;
    state.settings.densityThreshold   = 1e9f;
    REQUIRE(foam.restore(state).ok());
    auto saved   = foam.snapshot();
    state.credit = "1";
    CHECK(!foam.restore(state).ok());
    CHECK_EQ(foam.snapshot().credit, saved.credit);
    auto encoded = encodeVolumeFluidFoam(saved);
    auto decoded = decodeVolumeFluidFoam(encoded);
    REQUIRE(decoded.ok());
    encoded.set("unknown", 3);
    CHECK(!decodeVolumeFluidFoam(encoded).ok());
}
}  // namespace test_volume_emitter_runtime_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_mesh_emitter)

namespace test_volume_mesh_contract {
using namespace eve::fluids;
TEST_CASE("fluids.buildVolumeFluidMeshDistribution: closed cube fills interior and invalid topology rejects") {
    const glm::vec3     vertices[] = {{-0.3f, -0.3f, -0.3f}, {0.3f, -0.3f, -0.3f}, {0.3f, 0.3f, -0.3f},
                                      {-0.3f, 0.3f, -0.3f},  {-0.3f, -0.3f, 0.3f}, {0.3f, -0.3f, 0.3f},
                                      {0.3f, 0.3f, 0.3f},    {-0.3f, 0.3f, 0.3f}};
    const std::uint32_t indices[]  = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                      3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    auto                generated  = buildVolumeFluidMeshDistribution(vertices, indices, {1, 1, 1}, 0.1f);
    REQUIRE(generated.ok());
    REQUIRE(!generated.value().empty());
    bool interior = false;
    for (const auto& point : generated.value())
        if (glm::length(point.position) < 0.09f) interior = true;
    CHECK(interior);
    const std::uint32_t invalid[] = {0, 1, 99};
    CHECK(!buildVolumeFluidMeshDistribution(vertices, invalid, {1, 1, 1}, 0.1f).ok());
    CHECK(!buildVolumeFluidMeshDistribution(vertices, indices, {0, 1, 1}, 0.1f).ok());
}

TEST_CASE("fluids.VolumeFluid sampleField: opposite velocities cancel locally and distant query is empty") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle left, right;
    left.position                    = {-0.05f, 1.0f, 0.0f};
    right.position                   = {0.05f, 1.0f, 0.0f};
    left.velocity                    = {0.0f, -1.0f, 0.0f};
    right.velocity                   = {0.0f, 1.0f, 0.0f};
    const VolumeFluidParticle pair[] = {left, right};
    REQUIRE(solver->emit(pair).ok());
    const glm::vec3 queries[] = {{0.0f, 1.0f, 0.0f}, {100.0f, 100.0f, 100.0f}};
    auto            sampled   = solver->sampleField(queries);
    REQUIRE(sampled.ok());
    REQUIRE_EQ(sampled.value().size(), 2u);
    CHECK_EQ(sampled.value()[0].neighborCount, 2u);
    CHECK(glm::length(sampled.value()[0].velocity) < 1e-6f);
    CHECK_EQ(sampled.value()[1].neighborCount, 0u);
    CHECK_EQ(sampled.value()[1].density, 0.0f);
}

TEST_CASE("fluids.VolumeFluid static attachment: bound particle follows collider and invalid index leaves binding") {
    VolumeFluidSettings settings;
    settings.gravity = {0.0f, -10.0f, 0.0f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {-0.2f, 1.0f, 0.0f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider target;
    target.label  = 71;
    target.center = {0.0f, 1.0f, 0.0f};
    target.radius = 0.1f;
    REQUIRE(solver->setColliders(std::span(&target, 1)).ok());
    const unsigned index = 0;
    auto           bound = solver->bindStaticParticles(std::span(&index, 1), 71, false);
    REQUIRE(bound.ok());
    CHECK_EQ(bound.value(), 1u);
    target.center.x += 0.25f;
    REQUIRE(solver->setColliders(std::span(&target, 1)).ok());
    REQUIRE(solver->step(1.0f / 120.0f, 1).ok());
    CHECK(std::fabs(solver->particles()[0].position.x - 0.05f) < 1e-5f);
    const unsigned invalid = 9;
    CHECK(!solver->bindStaticParticles(std::span(&invalid, 1), 71, false).ok());
    CHECK_EQ(solver->snapshot().attachments.size(), 1u);
}
}  // namespace test_volume_mesh_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_triggers)

namespace test_volume_trigger_contract {
TEST_CASE("fluids.VolumeFluid trigger collider: contact reports without changing position or impulse") {
    eve::fluids::VolumeFluidSettings settings;
    settings.gravity = {0.0f, 0.0f, 0.0f};
    auto created     = eve::fluids::VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    eve::fluids::VolumeFluidParticle particle;
    particle.position = {0.2f, 0.5f, 0.0f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    eve::fluids::VolumeFluidCollider trigger;
    trigger.label     = 10;
    trigger.center    = {0.0f, 0.5f, 0.0f};
    trigger.radius    = 0.5f;
    trigger.isTrigger = true;
    REQUIRE(solver->setColliders(std::span(&trigger, 1)).ok());
    REQUIRE(solver->step(1.0f / 120.0f, 1).ok());
    CHECK(glm::length(solver->particles()[0].position - particle.position) < 1e-6f);
    REQUIRE_EQ(solver->contacts().size(), 1u);
    CHECK_EQ(solver->contacts()[0].colliderLabel, 10u);
    CHECK_EQ(glm::length(solver->contacts()[0].impulse), 0.0f);
    trigger.solidify = true;
    CHECK(!solver->setColliders(std::span(&trigger, 1)).ok());
    CHECK_EQ(solver->snapshot().colliders[0].solidify, false);
}
}  // namespace test_volume_trigger_contract
#endif
}  // namespace test_fluids_combined_fluids_emitters_coupling_tests

// Responsibility: fluids_shore_tests
namespace test_fluids_combined_fluids_shore_tests {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_shore_tests)
using namespace eve::fluids;

namespace {
std::unique_ptr<ShallowWaterField> slopedLake() {
    ShallowWaterSettings settings;
    settings.resolutionX = 64;
    settings.resolutionZ = 8;
    settings.cellSize    = 0.1f;
    settings.preferGpu   = false;
    auto created         = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto               field = std::move(created).takeValue();
    std::vector<float> bed(static_cast<std::size_t>(field->cellCount()));
    for (int z = 0; z < field->resolutionZ(); ++z)
        for (int x = 0; x < field->resolutionX(); ++x)
            bed[static_cast<std::size_t>(z * field->resolutionX() + x)] =
                std::max(0.f, ((float(x) + .5f) * .1f - 2.f) * .3f);
    REQUIRE(field->setBed(bed).ok());
    REQUIRE(field->fillSurface(.4f).ok());
    return field;
}

std::vector<float> copy(std::span<const float> values) { return {values.begin(), values.end()}; }
}  // namespace

TEST_CASE("Shore fill and validation contract") {
    SUB_CASE("shore contract: surface fill produces wet cells at one elevation and dry cells above it") {
        auto field = slopedLake();
        REQUIRE_EQ(field->cellCount(), 512);
        bool wet = false;
        bool dry = false;
        for (std::size_t i = 0; i < field->depths().size(); ++i) {
            const float expected = std::max(0.f, .4f - field->beds()[i]);
            REQUIRE(std::abs(field->depths()[i] - expected) < 1e-6f);
            wet |= expected > 0.f;
            dry |= expected == 0.f;
            REQUIRE_EQ(field->momentumX()[i], 0.f);
            REQUIRE_EQ(field->momentumZ()[i], 0.f);
        }
        REQUIRE(wet);
        REQUIRE(dry);
    };

    SUB_CASE("shore contract: invalid bed and elevation leave all authoritative fields unchanged") {
        auto       field  = slopedLake();
        const auto depths = copy(field->depths());
        const auto beds   = copy(field->beds());
        REQUIRE(!field->setBed(std::span<const float>(beds.data(), beds.size() - 1)).ok());
        auto invalid = beds;
        invalid[20]  = std::numeric_limits<float>::infinity();
        REQUIRE(!field->setBed(invalid).ok());
        REQUIRE(!field->fillSurface(std::numeric_limits<float>::quiet_NaN()).ok());
        REQUIRE(!field->fillSurface(20.f).ok());
        REQUIRE(std::equal(beds.begin(), beds.end(), field->beds().begin()));
        REQUIRE(std::equal(depths.begin(), depths.end(), field->depths().begin()));
    };
}

TEST_CASE("Shore still water and wave evolution contract") {
    SUB_CASE("shore contract: still wet-dry lake remains at rest over hundreds of steps") {
        auto       field  = slopedLake();
        const auto before = copy(field->depths());
        for (int step = 0; step < 240; ++step) REQUIRE(field->step(1.f / 60.f).ok());
        for (std::size_t i = 0; i < before.size(); ++i) {
            REQUIRE(std::abs(field->depths()[i] - before[i]) < 1e-5f);
            REQUIRE(std::abs(field->momentumX()[i]) < 1e-5f);
            REQUIRE(std::abs(field->momentumZ()[i]) < 1e-5f);
        }
    };

    SUB_CASE("shore contract: wave reaches initially dry cell then retreats without losing mass") {
        auto field = slopedLake();
        REQUIRE_EQ(field->depthAt(3.35f, .4f), 0.f);
        REQUIRE(field->disturbAt(2.9f, .4f, .4f, .12f, .4f).ok());
        const double initialMass       = std::accumulate(field->depths().begin(), field->depths().end(), 0.);
        float        highestRunup      = 0.f;
        float        highestNearDepth  = 0.f;
        float        returningVelocity = 0.f;
        for (int tick = 0; tick < 480; ++tick) {
            REQUIRE(field->step(1.f / 120.f).ok());
            highestRunup     = std::max(highestRunup, field->depthAt(3.35f, .4f));
            highestNearDepth = std::max(highestNearDepth, field->depthAt(3.05f, .4f));
            if (tick > 30) returningVelocity = std::min(returningVelocity, field->velocityAt(3.05f, .4f).x);
            for (float depth : field->depths()) {
                REQUIRE(std::isfinite(depth));
                REQUIRE(depth >= 0.f);
            }
        }
        const double finalMass = std::accumulate(field->depths().begin(), field->depths().end(), 0.);
        REQUIRE(highestRunup > .001f);
        REQUIRE(returningVelocity < -.005f);
        REQUIRE(field->depthAt(3.05f, .4f) < highestNearDepth);
        REQUIRE(std::abs(finalMass - initialMass) < initialMass * .02);
    };
}

TEST_CASE("Shore rendering and collider independence contract") {
    SUB_CASE("shore contract: display gain leaves the level rest surface at physical elevation") {
        auto                       field = slopedLake();
        std::vector<float>         positions, normals, uvs;
        std::vector<std::uint32_t> indices;
        REQUIRE(field->buildSurfaceMesh(positions, normals, uvs, indices, 2.5f).ok());
        REQUIRE_EQ(positions.size(), field->depths().size() * 3u);
        REQUIRE_EQ(normals.size(), positions.size());
        REQUIRE_EQ(uvs.size(), field->depths().size() * 2u);
        REQUIRE(!indices.empty());
        for (std::size_t i = 0; i < field->depths().size(); ++i)
            REQUIRE(std::abs(positions[i * 3 + 1] - std::max(.4f, field->beds()[i])) < 1e-5f);
    };

    SUB_CASE("shore contract: colliders outside water do not create depth") {
        auto                 field = slopedLake();
        ShallowWaterCollider air;
        air.label    = 1;
        air.center   = {1.f, 3.f, .4f};
        air.velocity = {1.f, -1.f, 0.f};
        ShallowWaterCollider dryLand;
        dryLand.label    = 2;
        dryLand.center   = {5.f, .95f, .4f};
        dryLand.velocity = {1.f, -1.f, 0.f};
        const std::vector<ShallowWaterCollider> colliders{air, dryLand};
        REQUIRE(field->setColliders(colliders).ok());
        REQUIRE_EQ(field->colliders().size(), 2u);
        const auto before = copy(field->depths());
        for (int tick = 0; tick < 60; ++tick) REQUIRE(field->step(1.f / 60.f).ok());
        for (std::size_t i = 0; i < before.size(); ++i) REQUIRE(std::abs(before[i] - field->depths()[i]) < 1e-5f);
    };
}
#endif
}  // namespace test_fluids_combined_fluids_shore_tests

// Responsibility: fluids_solver_tests
namespace test_fluids_combined_fluids_solver_tests {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_shallow_water)

namespace test_shallow_water_contract {
using namespace eve::fluids;

TEST_CASE("Shallow water admission validates grid geometry and exposes initial surface") {
    ShallowWaterSettings settings;
    settings.resolutionX = 2;
    CHECK(!ShallowWaterField::create(settings).ok());
    settings.resolutionX  = 32;
    settings.resolutionZ  = 32;
    settings.cellSize     = .5f;
    settings.initialDepth = .4f;
    settings.bedHeight    = .1f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    CHECK_EQ(field->resolutionX(), 32);
    CHECK(!field->usingGpu());
    const float x = settings.origin.x + 8.f, z = settings.origin.z + 8.f;
    CHECK(std::fabs(field->surfaceHeightAt(x, z) - .5f) < .02f);
    CHECK(std::fabs(field->depthAt(x, z) - .4f) < .02f);
}

TEST_CASE("Shallow water impulse transports a bounded wave away from its source") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 48;
    settings.resolutionZ  = 48;
    settings.cellSize     = .25f;
    settings.initialDepth = .5f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->addImpulse(6, 6, 1.4f, .45f).ok());
    const float before = field->surfaceHeightAt(8.5f, 6.f);
    double      mass0  = 0;
    for (float depth : field->depths()) mass0 += depth;
    for (int frame = 0; frame < 90; ++frame) REQUIRE(field->step(1.f / 60.f).ok());
    const float after = field->surfaceHeightAt(8.5f, 6.f);
    CHECK(std::fabs(after - before) > .001f);
    double mass1 = 0;
    for (float depth : field->depths()) mass1 += depth;
    CHECK(mass1 > mass0 * .5);
    CHECK(mass1 < mass0 * 1.5);
}

TEST_CASE("Shallow water collider displaces local depth and duplicate labels reject atomically") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 40;
    settings.resolutionZ  = 40;
    settings.cellSize     = .2f;
    settings.initialDepth = .55f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto                 field  = std::move(created).takeValue();
    const float          before = field->depthAt(4, 4);
    ShallowWaterCollider leg;
    leg.label             = 7;
    leg.shape             = ShallowWaterColliderShape::Capsule;
    leg.center            = {4, .35f, 4};
    leg.halfExtent        = {.12f, .45f, .12f};
    leg.radius            = .12f;
    leg.velocity          = {.8f, 0, 0};
    leg.displacementScale = 2.5f;
    leg.dragScale         = 1.8f;
    REQUIRE(field->setColliders(std::span(&leg, 1)).ok());
    auto pair      = std::array{leg, leg};
    pair[1].center = {1, 1, 1};
    CHECK(!field->setColliders(pair).ok());
    CHECK_EQ(field->colliders().size(), 1u);
    CHECK_EQ(field->colliders()[0].label, 7);
    for (int frame = 0; frame < 45; ++frame) REQUIRE(field->step(1.f / 60.f).ok());
    CHECK(field->depthAt(4, 4) < before);
    const auto velocity = field->velocityAt(4 + settings.cellSize, 4);
    CHECK(std::fabs(velocity.x) + std::fabs(velocity.y) > .01f);
}

TEST_CASE("Shallow water exports a complete mesh and RGBA field while rejecting wrong image size") {
    ShallowWaterSettings settings;
    settings.resolutionX  = 16;
    settings.resolutionZ  = 16;
    settings.cellSize     = .25f;
    settings.initialDepth = .5f;
    settings.maxDepth     = 2.5f;
    settings.preferGpu    = false;
    auto created          = ShallowWaterField::create(settings);
    REQUIRE(created.ok());
    auto field = std::move(created).takeValue();
    REQUIRE(field->addImpulse(2, 2, 1.f, .2f).ok());
    for (int i = 0; i < 8; ++i) REQUIRE(field->step(1.f / 60.f).ok());
    std::vector<float>         positions, normals, uv, colors;
    std::vector<std::uint32_t> indices;
    REQUIRE(field->buildSurfaceMesh(positions, normals, uv, indices, 4.f, &colors).ok());
    CHECK_EQ(positions.size(), std::size_t(16 * 16 * 3));
    CHECK_EQ(normals.size(), positions.size());
    CHECK_EQ(uv.size(), std::size_t(16 * 16 * 2));
    CHECK_EQ(colors.size(), std::size_t(16 * 16 * 4));
    CHECK_EQ(indices.size(), std::size_t(15 * 15 * 6));
    eve::image::ImageData image(16, 16, "RGBA8");
    REQUIRE(field->copyFieldToImage(image, 2.f).ok());
    const auto* pixels    = static_cast<const std::uint8_t*>(image.getData());
    bool        variation = false;
    for (int i = 1; i < 16 * 16; ++i) variation |= pixels[4 * i] != pixels[0];
    CHECK(variation);
    eve::image::ImageData wrong(8, 8, "RGBA8");
    CHECK(!field->copyFieldToImage(wrong, 2.f).ok());
}
}  // namespace test_shallow_water_contract
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_part3)

namespace test_volume_query_contract {
using namespace eve::fluids;

TEST_CASE("Volume fluid owns SDF collider samples and applies pose updates without changing geometry") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    settings.minimum    = {-1.f, .2f, -.8f};
    settings.maximum    = {1.4f, 1.8f, .8f};
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                   solver = std::move(created).takeValue();
    VolumeFluidSdfCollider obstacle;
    obstacle.label    = 41;
    obstacle.sdf      = MeshSdf::makeSphere({0, 1, 0}, .5f, {24, 24, 24});
    obstacle.position = {.2f, 0, 0};
    obstacle.scale    = 1.5f;
    REQUIRE(solver->setSdfColliders(std::span(&obstacle, 1)).ok());
    VolumeFluidParticle particle;
    particle.position           = {.9f, 1.5f, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    CHECK(solver->particles()[0].position.x > .98f);
    const auto saved   = solver->snapshot();
    const auto decoded = decodeVolumeFluid(encodeVolumeFluid(saved));
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().sdfColliders.size(), 1u);
    CHECK(decoded.value().sdfColliders[0].sdf.distances == obstacle.sdf.distances);
    VolumeFluidSdfPose pose;
    pose.label    = 41;
    pose.position = {.35f, .1f, 0};
    pose.scale    = 1.25f;
    REQUIRE(solver->updateSdfColliderPoses(std::span(&pose, 1)).ok());
    CHECK(solver->snapshot().sdfColliders[0].position == pose.position);
    CHECK(solver->snapshot().sdfColliders[0].sdf.distances == saved.sdfColliders[0].sdf.distances);
    pose.label = 999;
    CHECK(!solver->updateSdfColliderPoses(std::span(&pose, 1)).ok());
    CHECK_EQ(solver->snapshot().sdfColliders[0].label, 41);
}

TEST_CASE("Volume fluid raycast returns nearest hits in distance order with a bounded phase mask") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(4);
    for (std::size_t i = 0; i < particles.size(); ++i) {
        particles[i].position       = {float(i) * .5f, 1, 0};
        particles[i].material.phase = VolumeFluidPhase(i);
    }
    REQUIRE(solver->emit(particles).ok());
    const auto all = solver->raycast({-1, 1, 0}, {5, 0, 0}, 2.1f, 4, 0x0f);
    REQUIRE(all.ok());
    REQUIRE_EQ(all.value().size(), 3u);
    CHECK_EQ(all.value()[0].particleIndex, 0);
    CHECK_EQ(all.value()[1].particleIndex, 1);
    CHECK(all.value()[0].distance < all.value()[1].distance);
    const auto two = solver->raycast({-1, 1, 0}, {1, 0, 0}, 10, 2, 0x0f);
    REQUIRE(two.ok());
    CHECK_EQ(two.value().size(), 2u);
    const auto granular = solver->raycast({-1, 1, 0}, {1, 0, 0}, 10, 4, 1u << 2);
    REQUIRE(granular.ok());
    REQUIRE_EQ(granular.value().size(), 1u);
    CHECK_EQ(granular.value()[0].particleIndex, 2);
    CHECK(!solver->raycast({}, {}, 1.f).ok());
    CHECK(!solver->raycast({}, {0, 0, 1}, -1.f).ok());
    CHECK_EQ(solver->particleCount(), 4);
}

TEST_CASE("Volume fluid signed sphere query filters phases and returns independent hit copies") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position       = {0, 1, 0};
    particles[1].position       = {.2f, 1, 0};
    particles[1].material.phase = VolumeFluidPhase::Gas;
    particles[2].position       = {.4f, 1, 0};
    particles[2].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(particles).ok());
    auto hits = solver->querySphere({0, 1, 0}, .1f, .02f, .1f, 4, 0x0f);
    REQUIRE(hits.ok());
    REQUIRE_EQ(hits.value().size(), 2u);
    CHECK_EQ(hits.value()[0].particleIndex, 0);
    CHECK(std::fabs(hits.value()[0].distance + .17f) < 1e-6f);
    const auto gas = solver->querySphere({0, 1, 0}, .5f, 0, 0, 1, 1u << 1);
    REQUIRE(gas.ok());
    REQUIRE_EQ(gas.value().size(), 1u);
    CHECK(gas.value()[0].particle.material.phase == VolumeFluidPhase::Gas);
    hits.value()[0].particle.position.x = 9;
    CHECK_EQ(solver->particles()[0].position.x, 0.f);
    CHECK(!solver->querySphere({}, -1.f, 0, 1).ok());
    CHECK(!solver->querySphere({}, 1.f, 0, 1, 0, 15).ok());
}
}  // namespace test_volume_query_contract
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_part2)

namespace test_volume_material_contract {
using namespace eve::fluids;

TEST_CASE("Volume fluid viscosity diffuses relative motion while conserving total momentum") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    const float         density[] = {500, 2000, 1000};
    const float         shear[]   = {.04f, -.02f, .02f};
    glm::vec3           beforeMomentum(0);
    float               beforeEnergy = 0;
    for (int i = 0; i < 3; ++i) {
        particles[i].position           = {i == 0 ? -.14f : (i == 1 ? 0.f : .1f), 1, 0};
        particles[i].material.density   = density[i];
        particles[i].material.viscosity = 30.f * i;
        particles[i].material.cohesion  = 0.f;
        particles[i].velocity           = {0, shear[i], .1f};
        const float mass                = density[i] * .001f;
        beforeMomentum += mass * particles[i].velocity;
        beforeEnergy += .5f * mass * glm::dot(particles[i].velocity, particles[i].velocity);
    }
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    glm::vec3 afterMomentum(0);
    float     afterEnergy = 0;
    for (const auto& particle : solver->particles()) {
        const float mass = particle.material.density * .001f;
        afterMomentum += mass * particle.velocity;
        afterEnergy += .5f * mass * glm::dot(particle.velocity, particle.velocity);
    }
    CHECK(glm::length(afterMomentum - beforeMomentum) < 1e-5f);
    CHECK(afterEnergy < beforeEnergy);
}

TEST_CASE("Volume fluid oriented box overlap distinguishes rotation and validates query parameters") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {.2f, 1, 0};
    particles[1].position = {0, 1.2f, 0};
    particles[2].position = {.25f, 1, 0};
    REQUIRE(solver->emit(particles).ok());
    const auto aligned = solver->overlapBox({0, 1, 0}, {.25f, .05f, .05f}, {0, 0, 0, 1});
    REQUIRE(aligned.ok());
    CHECK_EQ(aligned.value().size(), 2u);
    const auto rotated = solver->overlapBox({0, 1, 0}, {.25f, .05f, .05f}, {0, 0, .70710678f, .70710678f});
    REQUIRE(rotated.ok());
    REQUIRE_EQ(rotated.value().size(), 1u);
    CHECK_EQ(rotated.value()[0].position.y, 1.2f);
    CHECK(!solver->overlapBox({}, {-1, 1, 1}, {0, 0, 0, 1}).ok());
    CHECK(!solver->overlapBox({}, {1, 1, 1}, {0, 0, 0, 2}).ok());
    CHECK_EQ(solver->particleCount(), 3);
}

TEST_CASE("Volume fluid snapshot codec round trips owning state and rejects malformed restores atomically") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0, 1, 0};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    const auto original = solver->snapshot();
    auto       encoded  = encodeVolumeFluid(original);
    const auto json     = encoded.toJson();
    REQUIRE(json.ok());
    const auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    const auto decoded = decodeVolumeFluid(parsed.value());
    REQUIRE(decoded.ok());
    solver->clear();
    REQUIRE(solver->restore(decoded.value()).ok());
    CHECK_EQ(solver->particles().size(), 1u);
    auto bad    = original;
    bad.version = 99;
    CHECK(!solver->restore(bad).ok());
    CHECK_EQ(solver->particles()[0].position.y, 1.f);
    encoded.set("unexpected", 3);
    CHECK(!decodeVolumeFluid(encoded).ok());
}

TEST_CASE("Volume fluid material channels either update every particle or leave all materials unchanged") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-.1f, 1, 0};
    particles[0].data     = {30, 1.5f, -3, 7};
    particles[1].position = {.1f, 1, 0};
    particles[1].data     = {-1, .5f, 0, 0};
    REQUIRE(solver->emit(particles).ok());
    CHECK(!solver->applyMaterialChannels().ok());
    CHECK_EQ(solver->particles()[0].material.viscosity, particles[0].material.viscosity);
    solver->clear();
    particles[1].data.x = 100;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->applyMaterialChannels().ok());
    CHECK_EQ(solver->particles()[0].material.viscosity, 30.f);
    CHECK_EQ(solver->particles()[0].material.cohesion, 1.5f);
    CHECK_EQ(solver->particles()[1].material.viscosity, 100.f);
    CHECK(solver->particles()[0].data == particles[0].data);
}

TEST_CASE("Volume fluid thermal contacts validate rule ownership before mutating particles") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0, 1, 0};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidThermalRule rule;
    rule.colliderLabel = 42;
    CHECK(!solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
    CHECK(solver->particles()[0].position == particle.position);
    VolumeFluidCollider collider;
    collider.label = 42;
    REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
    rule.minimumViscosity = 20.f;
    CHECK(!solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
    CHECK(solver->particles()[0].position == particle.position);
    rule.minimumViscosity                     = .05f;
    const VolumeFluidThermalRule duplicates[] = {rule, rule};
    CHECK(!solver->stepWithThermalContacts(1.f / 60.f, 4, duplicates).ok());
    CHECK(solver->particles()[0].position == particle.position);
}
}  // namespace test_volume_material_contract
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume)

namespace test_volume_fluid_contract {
using namespace eve::fluids;

TEST_CASE("Volume fluid emits owning particles and exposes independent render copies") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle{{0, 1, 0}, {0, 0, 0}};
    particle.radii       = {.04f, .08f, .12f};
    particle.orientation = {0, 0, .70710677f, .70710677f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    CHECK_EQ(solver->particleCount(), 1);
    std::vector<glm::vec3> positions, radii;
    std::vector<glm::vec4> colors, orientations;
    CHECK_EQ(solver->copySurfaceRenderData(positions, colors, radii, orientations), .1f);
    CHECK_EQ(positions.size(), 1u);
    CHECK_EQ(positions[0].y, 1.f);
    CHECK(radii[0] == particle.radii);
    CHECK(glm::length(orientations[0] - particle.orientation) < 1e-6f);
    REQUIRE(solver->step(1.f / 60.f).ok());
    CHECK_EQ(positions[0].y, 1.f);
    CHECK_EQ(solver->copyRenderData(positions, colors), .1f);
    CHECK(positions[0].y < 1.f);
    solver->clear();
    CHECK_EQ(solver->particleCount(), 0);
    CHECK_EQ(solver->copyRenderData(positions, colors), .1f);
    CHECK(positions.empty());
}

TEST_CASE("Volume fluid free fall follows configured gravity and rejects invalid gravity atomically") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0, 1, 0};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->setGravity({0, 9.81f, 0}).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    CHECK(solver->particleView()[0].velocity.y > 0);
    const auto before = solver->snapshot();
    CHECK(!solver->setGravity({std::numeric_limits<float>::quiet_NaN(), 0, 0}).ok());
    CHECK(!solver->setGravity({1001.f, 0, 0}).ok());
    CHECK(solver->gravity() == before.settings.gravity);
}

TEST_CASE("Volume fluid interpolation follows survivors after expiry and remains unchanged on invalid alpha") {
    VolumeFluidSettings settings;
    settings.gravity  = {0, 0, 0};
    settings.capacity = 4;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position = {-1, 1, 0};
    particles[0].velocity = {1, 0, 0};
    particles[1].position = {0, 1, 0};
    particles[1].life     = .001f;
    particles[2].position = {1, 1, 0};
    particles[2].velocity = {3, 0, 0};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    std::vector<glm::vec3> positions;
    REQUIRE(solver->copyInterpolatedPositions(0, positions).ok());
    REQUIRE_EQ(positions.size(), 2u);
    CHECK(positions[0] == glm::vec3(-1, 1, 0));
    CHECK(positions[1] == glm::vec3(1, 1, 0));
    REQUIRE(solver->copyInterpolatedPositions(.5f, positions).ok());
    CHECK(std::fabs(positions[0].x + 1.f - .5f / 120.f) < 1e-5f);
    const auto preserved = positions;
    CHECK(!solver->copyInterpolatedPositions(1.01f, positions).ok());
    CHECK(positions == preserved);
    REQUIRE(solver->killParticle(0).ok());
    REQUIRE(solver->copyInterpolatedPositions(0, positions).ok());
    CHECK_EQ(positions[0].x, 1.f);
}

TEST_CASE("Volume fluid particle deletion remaps owned attachments and simplex indexes atomically") {
    VolumeFluidSnapshot state;
    state.settings.gravity  = {0, 0, 0};
    state.settings.capacity = 8;
    state.particles.resize(3);
    for (unsigned i = 0; i < 3; ++i) state.particles[i].position = {-.3f + .3f * float(i), 1, 0};
    state.particles[2].material.phase = VolumeFluidPhase::Solid;
    VolumeFluidCollider collider;
    collider.label = 7;
    state.colliders.push_back(collider);
    state.attachments.push_back({2, 7, {0, 1, 0}, {0, 0, 0, 1}});
    state.simplexes = {{{0, 2, 0}, 2}, {{1, 2, 0}, 2}};
    auto created    = VolumeFluid::create(state.settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->restore(state).ok());
    REQUIRE(solver->killParticle(1).ok());
    const auto after = solver->snapshot();
    CHECK_EQ(after.particles.size(), 2u);
    REQUIRE_EQ(after.attachments.size(), 1u);
    CHECK_EQ(after.attachments[0].particleIndex, 1);
    REQUIRE_EQ(after.simplexes.size(), 1u);
    CHECK_EQ(after.simplexes[0].particleIndices[1], 1);
    CHECK_EQ(solver->availableCapacity(), 6);
    CHECK(!solver->killParticle(2).ok());
    CHECK_EQ(solver->snapshot().particles.size(), 2u);
}

TEST_CASE("Volume fluid external forces scale by inverse particle mass and clear after a successful step") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position         = {-.2f, 1, 0};
    particles[1].position         = {.2f, 1, 0};
    particles[0].material.density = 1000;
    particles[1].material.density = 2000;
    REQUIRE(solver->emit(particles).ok());
    const glm::vec3 forces[2] = {{8, 0, 0}, {8, 0, 0}};
    REQUIRE(solver->setParticleExternalForces(forces).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto first = solver->particles();
    CHECK(std::fabs(first[0].velocity.x - 2 * first[1].velocity.x) < 1e-5f);
    CHECK(first[0].velocity.x > 0);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    CHECK(std::fabs(solver->particles()[0].velocity.x - first[0].velocity.x) < 1e-5f);
}
}  // namespace test_volume_fluid_contract
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids)

namespace test_fluid_surface_contract {
using namespace eve::fluids;

TEST_CASE("Surface binding preserves barycentric material coordinates across rigid and deformed poses") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2}, {{0, 0}, {1, 0}, {0, 1}}));
    const SurfaceLocation location{0, {.25f, .25f, .5f}};
    const auto            initial = binding.evaluate(location, .5f);
    CHECK(glm::distance(initial.position, glm::vec3(.25f, .5f, 0)) < 1e-5f);
    CHECK(glm::distance(initial.uv, glm::vec2(.25f, .5f)) < 1e-5f);
    SUB_CASE("rigid translation updates position and velocity") {
        glm::mat4 pose(1.f);
        pose[3] = {2, -1, 3, 1};
        binding.setTransform(pose);
        const auto moved = binding.evaluate(location, .5f);
        CHECK(glm::distance(moved.position, initial.position + glm::vec3(2, -1, 3)) < 1e-5f);
        CHECK(glm::distance(moved.velocity, glm::vec3(4, -2, 6)) < 1e-5f);
        CHECK(glm::distance(moved.uv, initial.uv) < 1e-5f);
    };
    SUB_CASE("deformation follows weighted vertex motion") {
        REQUIRE(binding.setDeformedPositions({{0, 0, 0}, {1, 0, 0}, {0, 1, 2}}));
        const auto moved = binding.evaluate(location, .25f);
        CHECK(glm::distance(moved.position, glm::vec3(.25f, .5f, 1)) < 1e-5f);
        CHECK(glm::distance(moved.velocity, glm::vec3(0, 0, 4)) < 1e-5f);
    };
}

TEST_CASE("Surface walker crosses shared edges and reports unused motion at open boundaries") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 1, 2, 2, 1, 3}));
    SurfaceLocation start;
    REQUIRE(binding.project({.25f, .25f, .1f}, .2f, start));
    SUB_CASE("shared edge continues into neighbor") {
        const auto walk = binding.walkAcrossSurface(start, {.5f, .5f, 0});
        REQUIRE(walk.valid);
        CHECK(!walk.reachedBoundary);
        CHECK_EQ(walk.location.triangle, 1u);
        CHECK(glm::distance(binding.evaluate(walk.location, 0).position, glm::vec3(.75f, .75f, 0)) < 1e-4f);
    };
    SUB_CASE("open edge stops and returns remaining displacement") {
        const auto walk = binding.walkAcrossSurface(start, {-1, -1, 0});
        REQUIRE(walk.valid);
        CHECK(walk.reachedBoundary);
        CHECK(glm::length(walk.remainingDisplacement) > .1f);
        const auto edge = binding.evaluate(walk.location, 0).position;
        CHECK(edge.x >= -1e-5f);
        CHECK(edge.y >= -1e-5f);
    };
}

TEST_CASE("Surface droplets move tangent to gravity and detach at an open edge") {
    SUB_CASE("gravity moves an attached drop down a vertical triangle") {
        FluidSurfaceBinding binding;
        REQUIRE(binding.build({{-2, -2, 0}, {2, -2, 0}, {-2, 2, 0}}, {0, 1, 2}));
        SurfaceDropletParams params;
        params.friction = 0;
        SurfaceDropletSimulation droplets(&binding, params);
        const SurfaceLocation    start{0, {.25f, .25f, .5f}};
        REQUIRE(droplets.addDroplet(start));
        const float before = binding.evaluate(start, 0).position.y;
        droplets.step(.05f);
        REQUIRE_EQ(droplets.droplets().size(), 1u);
        const auto& drop   = droplets.droplets().front();
        const auto  sample = binding.evaluate(drop.location, 0);
        CHECK(sample.position.y < before);
        CHECK(drop.relativeVelocity.y < 0);
        CHECK(std::fabs(glm::dot(drop.relativeVelocity, sample.normal)) < 1e-5f);
    };
    SUB_CASE("open boundary transfers an attached drop to the detached list") {
        FluidSurfaceBinding binding;
        REQUIRE(binding.build({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2}));
        SurfaceDropletParams params;
        params.gravity  = {0, 0, 0};
        params.friction = 0;
        SurfaceDropletSimulation droplets(&binding, params);
        REQUIRE(droplets.addDroplet({0, {.8f, .1f, .1f}}, 2.f, {-5, -5, 0}));
        droplets.step(.05f);
        CHECK(droplets.droplets().empty());
        REQUIRE_EQ(droplets.detachedDroplets().size(), 1u);
        CHECK(std::fabs(droplets.detachedDroplets().front().volume - 2.f) < 1e-6f);
    };
}

TEST_CASE("Surface droplet merging conserves volume and wetness diffusion conserves film without evaporation") {
    FluidSurfaceBinding binding;
    REQUIRE(binding.build({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 1, 2, 2, 1, 3}));
    SUB_CASE("nearby caps merge into one drop") {
        SurfaceDropletParams params;
        params.gravity = {0, 0, 0};
        SurfaceDropletSimulation droplets(&binding, params);
        REQUIRE(droplets.addDroplet({0, {.25f, .25f, .5f}}, .001f));
        REQUIRE(droplets.addDroplet({0, {.24f, .26f, .5f}}, .002f));
        droplets.step(.01f);
        REQUIRE_EQ(droplets.droplets().size(), 1u);
        CHECK(std::fabs(droplets.droplets().front().volume - .003f) < 1e-6f);
    };
    SUB_CASE("wetness spreads without losing total film") {
        SurfaceWetnessField wetness;
        REQUIRE(wetness.build(binding));
        wetness.deposit({0, {1, 0, 0}}, .8f);
        const float          before = std::accumulate(wetness.values().begin(), wetness.values().end(), 0.f);
        SurfaceWetnessParams params;
        params.diffusion   = 1;
        params.evaporation = 0;
        params.maxWetness  = 10;
        wetness.step(.1f, params);
        const float after = std::accumulate(wetness.values().begin(), wetness.values().end(), 0.f);
        CHECK(wetness.values()[1] > 0);
        CHECK(std::fabs(after - before) < 1e-6f);
    };
}

TEST_CASE("Fluid kernels have compact support and signed gradients toward nearby particles") {
    const float support = .2f;
    CHECK(fluidPoly6(.001f, support) > fluidPoly6(.01f, support));
    CHECK(std::fabs(fluidPoly6(support * support, support)) < 1e-9f);
    CHECK(std::fabs(fluidPoly6(.3f * .3f, support)) < 1e-12f);
    const auto gradient = fluidSpikyGrad(glm::vec3(.1f, 0, 0), support);
    CHECK(gradient.x < 0);
    CHECK(std::fabs(gradient.y) < 1e-9f);
    CHECK(glm::length(fluidSpikyGrad(glm::vec3(support, 0, 0), support)) < 1e-6f);
    CHECK(fluidViscLaplacian(.05f, support) > 0);
    REQUIRE(std::fabs(fluidViscLaplacian(support, support)) < 1e-6f);
}

TEST_CASE("Fluid SDF sphere exposes signed distance gradient and an owning distance-field conversion") {
    const auto sdf = MeshSdf::makeSphere(glm::vec3(0, 1, 0), 1.f, glm::ivec3(32));
    CHECK_EQ(sdf.voxelCount(), 32 * 32 * 32);
    CHECK(std::fabs(sdf.sample({0, 1, 0}) + 1.f) < .2f);
    CHECK(std::fabs(sdf.sample({1, 1, 0})) < .2f);
    CHECK(sdf.sample({2, 1, 0}) > .5f);
    CHECK(glm::dot(sdf.gradient({0, 2, 0}), glm::vec3(0, 1, 0)) > 0);
    MeshSdfField adapter;
    adapter.bind(sdf);
    CHECK_EQ(adapter.revision(), 1u);
    float nx = 0, ny = 0, nz = 0;
    CHECK(adapter.gradient(0, 1.6f, 0, nx, ny, nz) == eve::SdfField3DGradientStatus::Defined);
    CHECK(ny > .5f);
    const auto converted = distanceFieldFromMeshSdf(sdf, 1000.f);
    REQUIRE(converted.ok());
    const auto back = meshSdfFromDistanceField(converted.value());
    REQUIRE(back.ok());
    CHECK(back.value().distances == sdf.distances);
}

TEST_CASE("Fluid surface solver projects particles onto a boundary and gravity carries them downhill") {
    FluidParams params;
    params.gravity   = {0, -9.8f, 0};
    params.viscosity = .02f;
    FluidSimulation sim(2048, params);
    sim.setSdf(MeshSdf::makeSphere(glm::vec3(0), 1.f, glm::ivec3(32)));
    REQUIRE_EQ(sim.spawnDrop({0, 1.5f, 0}, .25f, 200), 200);
    float before = 0;
    for (int i = 0; i < sim.particleCount(); ++i) {
        const auto point = sim.particles()[std::size_t(i)].pos;
        CHECK(glm::length(point) >= .98f);
        before += point.y;
    }
    before /= sim.particleCount();
    for (int frame = 0; frame < 150; ++frame) sim.step(1.f / 60.f);
    float after = 0;
    for (int i = 0; i < sim.particleCount(); ++i) {
        const auto point = sim.particles()[std::size_t(i)].pos;
        CHECK(glm::length(point) >= .97f);
        after += point.y;
    }
    after /= sim.particleCount();
    CHECK(after < before - .1f);
}

TEST_CASE("Fluid surface renderer derives depth thickness and forward normal from one particle") {
    FluidSurfaceParams params;
    params.width            = 65;
    params.height           = 65;
    params.eye              = {0, 0, -3};
    params.particleRadius   = .08f;
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, false);
    renderer.render(std::vector<glm::vec3>{{0, 0, 0}}, .08f);
    const std::size_t center = 32 * 65 + 32;
    CHECK(std::fabs(renderer.depth()[center] - 2.92f) < 1e-5f);
    CHECK(renderer.normals()[center].z > .99f);
    CHECK(std::fabs(renderer.thickness()[center] - .16f) < 1e-5f);
    REQUIRE(renderer.color()[center * 4 + 3] > 0);
}

TEST_CASE("Fluids module creates an owned simulator and CPU renderer with configured dimensions") {
    auto* module = Fluids::create();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("Fluids"));
    auto* simulation = module->newSimulator(1024);
    REQUIRE(simulation != nullptr);
    CHECK_EQ(simulation->getMaxParticles(), 1024);
    CHECK_EQ(module->getSimulatorCount(), 1);
    auto* renderer = module->newSurfaceRenderer(32, 32);
    REQUIRE(renderer != nullptr);
    renderer->render(*simulation);
    CHECK_EQ(renderer->getWidth(), 32);
    CHECK_EQ(renderer->getHeight(), 32);
}
}  // namespace test_fluid_surface_contract
#endif
}  // namespace test_fluids_combined_fluids_solver_tests

// Responsibility: ocean_codec
namespace test_fluids_combined_ocean_codec {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_codec)
using namespace eve;
using namespace eve::fluids;

namespace {

OceanSpectrumSettings spectrumExample() {
    OceanSpectrumSettings settings;
    settings.seed          = std::numeric_limits<std::uint64_t>::max();
    settings.bands         = {{16, 64.0, 8.0, 64.0}};
    settings.current       = {0.8, -0.2};
    settings.windDirection = {0.3, 0.7};
    settings.seaLevel      = 12.25;
    return settings;
}

void removeField(Value& value, const char* name) { std::get<Value::Object>(value.storage()).erase(name); }

}  // namespace

TEST_CASE("fluids.ocean.codec.spectrum: serialized definitions preserve data and reject schema violations") {
    SUB_CASE("a valid definition round-trips through JSON with its full-width unsigned seed") {
        const auto input   = spectrumExample();
        const auto encoded = encodeOceanSpectrumSettings(input);
        CHECK_EQ(encoded.find("schema")->asString(), std::string("eve.ocean.spectrum"));
        CHECK_EQ(encoded.find("version")->asInt(), std::int64_t{1});
        CHECK_EQ(encoded.find("seed")->asString(), std::string("18446744073709551615"));

        auto json = encoded.toJson();
        REQUIRE(json.ok());
        auto parsed = Value::fromJson(json.value());
        REQUIRE(parsed.ok());
        auto decoded = decodeOceanSpectrumSettings(parsed.value());
        REQUIRE(decoded.ok());
        CHECK_EQ(decoded.value().seed, input.seed);
        CHECK_EQ(decoded.value().seaLevel, input.seaLevel);
        CHECK_EQ(decoded.value().current.x, input.current.x);
        CHECK_EQ(decoded.value().current.y, input.current.y);
        CHECK_EQ(decoded.value().bands.size(), std::size_t{1});
        CHECK_EQ(decoded.value().bands[0].resolution, 16u);
        CHECK_EQ(decoded.value().bands[0].maximumWavelength, 64.0);
        CHECK_EQ(encodeOceanSpectrumSettings(decoded.value()).toJson().value(), json.value());
    };

    SUB_CASE("schema/version drift, missing fields, and unknown keys are rejected") {
        const auto canonical = encodeOceanSpectrumSettings(spectrumExample());
        for (const auto& version : {Value(0), Value(2), Value(1.0), Value("1")}) {
            auto value = canonical;
            value.set("version", version);
            CHECK(!decodeOceanSpectrumSettings(value).ok());
        }
        auto value = canonical;
        value.set("schema", Value("eve.ocean.other"));
        CHECK(!decodeOceanSpectrumSettings(value).ok());
        value = canonical;
        removeField(value, "gravity");
        CHECK(!decodeOceanSpectrumSettings(value).ok());
        value = canonical;
        value.set("typo", Value(true));
        CHECK(!decodeOceanSpectrumSettings(value).ok());
        value = canonical;
        value.find("bands")->at(0).set("typo", Value(true));
        CHECK(!decodeOceanSpectrumSettings(value).ok());
    };

    SUB_CASE("seed text must be a canonical unsigned 64-bit integer") {
        const auto canonical = encodeOceanSpectrumSettings(spectrumExample());
        for (const auto* text : {"", "01", "+1", "-1", "1.0", "1 ", "18446744073709551616"}) {
            auto value = canonical;
            value.set("seed", Value(text));
            CHECK(!decodeOceanSpectrumSettings(value).ok());
        }
        for (const auto& seed : {Value(1), Value(1.0), Value(true)}) {
            auto value = canonical;
            value.set("seed", seed);
            CHECK(!decodeOceanSpectrumSettings(value).ok());
        }
        auto zero = canonical;
        zero.set("seed", Value("0"));
        auto decoded = decodeOceanSpectrumSettings(zero);
        REQUIRE(decoded.ok());
        CHECK_EQ(decoded.value().seed, std::uint64_t{0});
    };

    SUB_CASE("structural decoding is distinct from domain validation by the builder") {
        const auto canonical = encodeOceanSpectrumSettings(spectrumExample());
        auto       invalid   = canonical;
        invalid.set("peakPeriod", Value(std::numeric_limits<double>::infinity()));
        CHECK(!decodeOceanSpectrumSettings(invalid).ok());
        invalid = canonical;
        invalid.find("bands")->at(0).set("resolution", Value(16.5));
        CHECK(!decodeOceanSpectrumSettings(invalid).ok());
        invalid = canonical;
        invalid.set("bands", Value::Array{});
        CHECK(!decodeOceanSpectrumSettings(invalid).ok());
        invalid = canonical;
        invalid.set("bands", Value::Array(5, canonical.find("bands")->at(0)));
        CHECK(!decodeOceanSpectrumSettings(invalid).ok());
        invalid = canonical;
        invalid.set("windDirection", Value::Array{Value(1.0)});
        CHECK(!decodeOceanSpectrumSettings(invalid).ok());

        invalid = canonical;
        invalid.set("peakPeriod", Value(-1.0));
        auto decoded = decodeOceanSpectrumSettings(invalid);
        REQUIRE(decoded.ok());
        OceanSpectrumBuilder builder;
        builder.settings = decoded.value();
        CHECK(!builder.build().ok());
    };
}

TEST_CASE("fluids.ocean.codec.wake round-trips owning values and rejects malformed arguments") {
    OceanWakeSettings input;
    input.resolution      = 16;
    input.length          = 32.0;
    input.absorptionWidth = 8.0;
    input.center          = {3.0, -4.0};
    auto encoded          = encodeOceanWakeSettings(input);
    auto decoded          = decodeOceanWakeSettings(encoded);
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().resolution, 16u);
    CHECK_EQ(decoded.value().center.x, 3.0);
    CHECK_EQ(decoded.value().center.y, -4.0);
    auto wake = OceanWake::create(decoded.value());
    REQUIRE(wake.ok());
    CHECK_EQ(wake.value()->settings().center.x, 3.0);

    encoded.set("center", Value::Array{Value(1.0)});
    CHECK(!decodeOceanWakeSettings(encoded).ok());
    CHECK_EQ(decoded.value().center.x, 3.0);
    encoded = encodeOceanWakeSettings(input);
    encoded.set("resolution", Value(16.5));
    CHECK(!decodeOceanWakeSettings(encoded).ok());
    encoded = encodeOceanWakeSettings(input);
    encoded.set("resolution", Value(513));
    CHECK(!decodeOceanWakeSettings(encoded).ok());
    encoded = encodeOceanWakeSettings(input);
    encoded.set("unknown", Value(true));
    CHECK(!decodeOceanWakeSettings(encoded).ok());
}

TEST_CASE("fluids.ocean.codec.hull round-trips cell geometry and bounds structural input") {
    OceanHullSettings input;
    input.cells  = {{{1.0, -0.25, 2.0}, 0.5, 0.2}};
    auto encoded = encodeOceanHullSettings(input);
    auto decoded = decodeOceanHullSettings(encoded);
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().cells.size(), std::size_t{1});
    CHECK_EQ(decoded.value().cells[0].center.z, 2.0);
    CHECK_EQ(decoded.value().cells[0].volume, 0.2);

    encoded.find("cells")->at(0).set("unknown", Value(0));
    CHECK(!decodeOceanHullSettings(encoded).ok());
    encoded = encodeOceanHullSettings(input);
    encoded.set("cells", Value::Array(65, encoded.find("cells")->at(0)));
    CHECK(!decodeOceanHullSettings(encoded).ok());
    encoded = encodeOceanHullSettings(input);
    encoded.set("drag", Value::Array{Value(1.0), Value(2.0), Value(std::numeric_limits<double>::infinity())});
    CHECK(!decodeOceanHullSettings(encoded).ok());
    encoded = encodeOceanHullSettings(input);
    removeField(encoded, "waterDensity");
    CHECK(!decodeOceanHullSettings(encoded).ok());
}
#endif
}  // namespace test_fluids_combined_ocean_codec

// Responsibility: ocean_hull_mask
namespace test_fluids_combined_ocean_hull_mask {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_hull_mask)
using namespace eve::fluids;

namespace {
OceanHullVolume vessel() {
    OceanHullVolume volume;
    volume.center     = {1e8 + 3, 4, -1e8 + 2};
    volume.rotation   = glm::angleAxis(0.5, glm::normalize(glm::dvec3(1, 2, 3)));
    volume.halfExtent = {2, 1, 4};
    volume.bevels     = {{1, 0, 1, 4}};
    return volume;
}
}  // namespace

TEST_CASE("fluids.ocean.hull.spec.classification") {
    SUB_CASE("unit box: boundaries are inside; exterior and nonfinite points are outside") {
        std::array<OceanHullVolume, 1> volumes{};
        auto                           frame = OceanHullMaskBuilder(volumes).build();
        REQUIRE(frame.ok());
        REQUIRE(frame.value().size() == 1);
        REQUIRE(frame.value().rows().size() == 17);
        for (const auto& point : {glm::dvec3{0, 0, 0}, glm::dvec3{1, 1, 1}, glm::dvec3{-1, -1, -1}})
            REQUIRE(frame.value().contains(point));
        for (const auto& point : {glm::dvec3{1.01, 0, 0}, glm::dvec3{0, -1.01, 0}, glm::dvec3{1e308, 0, 0},
                                  glm::dvec3{std::numeric_limits<double>::quiet_NaN(), 0, 0}})
            REQUIRE(!frame.value().contains(point));
    };

    SUB_CASE("rotated beveled union: each member contributes, and the frame owns its pose") {
        auto first  = vessel();
        auto second = first;
        second.center += glm::dvec3(20, 0, 0);
        std::array<OceanHullVolume, 2> volumes{first, second};
        OceanHullMaskBuilder           builder(volumes);
        builder.origin = {1e8, 0, -1e8};
        auto frame     = builder.build();
        REQUIRE(frame.ok());
        REQUIRE(frame.value().size() == 2);
        REQUIRE(frame.value().rows().size() == 34);
        REQUIRE(frame.value().origin().x == builder.origin.x);
        auto world = [&](glm::dvec3 local) { return first.center + first.rotation * local; };
        REQUIRE(frame.value().contains(world({0, 0, 0})));
        REQUIRE(frame.value().contains(world({1, 0, 2})));
        REQUIRE(!frame.value().contains(world({1.5, 0, 3})));
        REQUIRE(!frame.value().contains(world({0, 1.1, 0})));
        REQUIRE(frame.value().contains(second.center));
        REQUIRE(!frame.value().contains(first.center + glm::dvec3(10, 0, 0)));
        volumes[0].center = {0, 0, 0};
        REQUIRE(frame.value().contains(first.center));
    };

    SUB_CASE("empty and maximum-count unions") {
        std::vector<OceanHullVolume> volumes;
        auto                         empty = OceanHullMaskBuilder(volumes).build();
        REQUIRE(empty.ok());
        REQUIRE(empty.value().size() == 0);
        REQUIRE(empty.value().rows().empty());
        REQUIRE(!empty.value().contains({0, 0, 0}));
        volumes.resize(32);
        auto maximum = OceanHullMaskBuilder(volumes).build();
        REQUIRE(maximum.ok());
        REQUIRE(maximum.value().size() == 32);
        REQUIRE(maximum.value().rows().size() == 32 * 17);
        volumes.emplace_back();
        REQUIRE(!OceanHullMaskBuilder(volumes).build().ok());
    };
}

TEST_CASE("fluids.ocean.hull.spec.inputValidation") {
    SUB_CASE("one invalid volume rejects the whole batch") {
        std::array<OceanHullVolume, 2> volumes{};
        auto                           rejected = [&]() { return !OceanHullMaskBuilder(volumes).build().ok(); };
        REQUIRE(!rejected());
        volumes[1].rotation = {0, 0, 0, 0};
        REQUIRE(rejected());
        volumes[1]              = {};
        volumes[1].halfExtent.x = 0;
        REQUIRE(rejected());
        volumes[1].halfExtent.x = 10000.01;
        REQUIRE(rejected());
        volumes[1]          = {};
        volumes[1].center.x = std::numeric_limits<double>::infinity();
        REQUIRE(rejected());
        volumes[1]        = {};
        volumes[1].bevels = {{0, 0, 0, 1}};
        REQUIRE(rejected());
        volumes[1].bevels.resize(11, {1, 0, 0, 1});
        REQUIRE(rejected());
    };

    SUB_CASE("bevel plane normalization and render-origin limits") {
        std::array<OceanHullVolume, 1> volumes{};
        volumes[0].bevels = {{2, 0, 0, 1}};
        auto normalized   = OceanHullMaskBuilder(volumes).build();
        REQUIRE(normalized.ok());
        REQUIRE(normalized.value().contains({.4, 0, 0}));
        REQUIRE(!normalized.value().contains({.6, 0, 0}));
        std::vector<OceanHullVolume> none;
        OceanHullMaskBuilder         builder(none);
        builder.origin = {1e9, 0, -1e9};
        REQUIRE(builder.build().ok());
        builder.origin.x = 1e9 + 1;
        REQUIRE(!builder.build().ok());
        builder.origin.x = std::numeric_limits<double>::quiet_NaN();
        REQUIRE(!builder.build().ok());
    };
}
#endif
}  // namespace test_fluids_combined_ocean_hull_mask

// Responsibility: ocean_mesh
namespace test_fluids_combined_ocean_mesh {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_mesh)
namespace {
using namespace eve::fluids;

// Shape contract: a center plus N vertices per ring, one center fan and two
// triangles per outer quad, with strictly growing radii and exact outer extent.
TEST_CASE("fluids.ocean.mesh.spec_counts_and_radial_grading") {
    OceanMeshBuilder builder;
    builder.settings = {32, 16, 0.5, 2000.0};
    auto result      = builder.build();
    REQUIRE(result.ok());
    const auto& mesh = result.value();
    CHECK_EQ(mesh.parameterOffsets.size(), std::size_t{513});
    CHECK_EQ(mesh.indices.size(), std::size_t{3 * 32 * 31});
    CHECK_EQ(mesh.radii.size(), std::size_t{16});
    CHECK_EQ(mesh.radii.front(), 0.5);
    CHECK_EQ(mesh.radii.back(), 2000.0);
    CHECK_EQ(mesh.parameterOffsets.front().x, 0.0);
    CHECK_EQ(mesh.parameterOffsets.front().y, 0.0);
    for (std::size_t i = 1; i < mesh.radii.size(); ++i) CHECK(mesh.radii[i] > mesh.radii[i - 1]);
}

// Topology contract: every triangle faces upward; interior edges occur twice
// with opposite direction. Only the outermost ring is an open boundary.
TEST_CASE("fluids.ocean.mesh.spec_oriented_disk_without_interior_seams") {
    OceanMeshBuilder builder;
    builder.settings = {32, 16, 0.5, 2000.0};
    auto result      = builder.build();
    REQUIRE(result.ok());
    const auto&                                                       mesh = result.value();
    std::map<std::pair<unsigned, unsigned>, std::pair<unsigned, int>> edges;
    double                                                            area = 0.0;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        REQUIRE(a < mesh.parameterOffsets.size());
        REQUIRE(b < mesh.parameterOffsets.size());
        REQUIRE(c < mesh.parameterOffsets.size());
        const auto   ab        = mesh.parameterOffsets[b] - mesh.parameterOffsets[a];
        const auto   ac        = mesh.parameterOffsets[c] - mesh.parameterOffsets[a];
        const double twiceArea = ab.y * ac.x - ab.x * ac.y;
        CHECK(twiceArea > 0.0);
        area += twiceArea * 0.5;
        for (const auto [from, to] : {std::pair(a, b), std::pair(b, c), std::pair(c, a)}) {
            auto& edge = edges[std::minmax(from, to)];
            ++edge.first;
            edge.second += from < to ? 1 : -1;
        }
    }
    unsigned boundary = 0;
    for (const auto& [vertices, edge] : edges) {
        if (edge.first == 1) {
            ++boundary;
            CHECK(vertices.first >= 1 + 15 * 32);
        } else {
            CHECK_EQ(edge.first, 2u);
            CHECK_EQ(edge.second, 0);
        }
    }
    CHECK_EQ(boundary, 32u);
    CHECK(std::abs(area - 16000000.0) < 1e-6);
    CHECK_EQ(static_cast<long>(mesh.parameterOffsets.size()) - static_cast<long>(edges.size()) +
                 static_cast<long>(mesh.indices.size() / 3),
             1L);
}

// Input contract: tessellation and extents are checked before an owning mesh
// is published; boundary-valid minimum settings still produce a mesh.
TEST_CASE("fluids.ocean.mesh.spec_settings_validation") {
    OceanMeshBuilder builder;
    builder.settings = {16, 2, 1.0, 2.0};
    REQUIRE(builder.build().ok());
    builder.settings.segments = 15;
    CHECK(!builder.build().ok());
    builder.settings.segments = 17;
    CHECK(!builder.build().ok());
    builder.settings.segments = 16;
    builder.settings.rings    = 1;
    CHECK(!builder.build().ok());
    builder.settings.rings       = 2;
    builder.settings.firstRadius = 0.001;
    CHECK(!builder.build().ok());
    builder.settings.firstRadius = 1.0;
    builder.settings.outerRadius = std::numeric_limits<double>::infinity();
    CHECK(!builder.build().ok());
    builder.settings.outerRadius = 1.9;
    CHECK(!builder.build().ok());
}

// Floating-origin contract: rendered local coordinates plus the origin agree
// with the water query. Shifting the render origin changes coordinates only.
TEST_CASE("fluids.ocean.mesh.spec_frame_coordinates_match_water") {
    OceanMeshBuilder topology;
    topology.settings = {16, 8, 0.5, 64.0};
    auto mesh         = topology.build();
    REQUIRE(mesh.ok());
    OceanSpectrumBuilder sea;
    sea.settings.bands                 = {{16, 64.0, 8.0, 64.0}};
    sea.settings.significantWaveHeight = 0.3;
    auto spectrum                      = sea.build();
    REQUIRE(spectrum.ok());
    auto frame = spectrum.value().frameBuilder(7.25).build();
    REQUIRE(frame.ok());
    OceanWaterView        water(frame.value());
    OceanMeshFrameBuilder renderer(mesh.value(), water);
    renderer.center = {100000000.25, -100000000.5};
    renderer.origin = {100000000.0, 0.0, -100000000.0};
    auto projected  = renderer.build();
    REQUIRE(projected.ok());
    CHECK_EQ(projected.value().time, 7.25);
    CHECK_EQ(projected.value().positions.size(), mesh.value().parameterOffsets.size() * 3);
    for (std::size_t i = 0; i < mesh.value().parameterOffsets.size(); ++i) {
        glm::dvec3 world(projected.value().positions[3 * i], projected.value().positions[3 * i + 1],
                         projected.value().positions[3 * i + 2]);
        world += renderer.origin;
        auto physics = water.sample({world.x, world.z});
        REQUIRE(physics.ok());
        CHECK(std::abs(world.y - physics.value().height) < 2e-5);
        glm::dvec3 normal(projected.value().normals[3 * i], projected.value().normals[3 * i + 1],
                          projected.value().normals[3 * i + 2]);
        CHECK(glm::length(normal - physics.value().normal) < 2e-5);
    }
    renderer.origin += glm::dvec3(32.0, 4.0, -16.0);
    auto shifted = renderer.build();
    REQUIRE(shifted.ok());
    for (std::size_t i = 0; i < projected.value().positions.size(); i += 3) {
        CHECK(std::abs(shifted.value().positions[i] + 32.0 - projected.value().positions[i]) < 1e-5);
        CHECK(std::abs(shifted.value().positions[i + 1] + 4.0 - projected.value().positions[i + 1]) < 1e-5);
        CHECK(std::abs(shifted.value().positions[i + 2] - 16.0 - projected.value().positions[i + 2]) < 1e-5);
    }
    renderer.center.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(!renderer.build().ok());
}
}  // namespace
#endif
}  // namespace test_fluids_combined_ocean_mesh

// Responsibility: ocean_spectrum
namespace test_fluids_combined_ocean_spectrum {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_spectrum)
namespace {
using eve::fluids::OceanSpectrumBuilder;

// Energy contract: significant wave height fixes ensemble variance; the
// generated height field is zero mean and carries that variance spatially.
TEST_CASE("fluids.ocean.spectrum.spec_height_energy") {
    OceanSpectrumBuilder builder;
    builder.settings.bands      = {{16, 64.0, 8.0, 64.0}};
    builder.settings.choppiness = 0.2;
    auto spectrum               = builder.build();
    REQUIRE(spectrum.ok());
    CHECK(std::abs(spectrum.value().variance() - 0.25) < 1e-12);
    auto frame = spectrum.value().frameBuilder(1.25).build();
    REQUIRE(frame.ok());
    const auto& grid   = frame.value().grids()[0];
    double      mean   = 0.0;
    double      energy = 0.0;
    for (const auto& displacement : grid.displacement) {
        mean += displacement.y;
        energy += displacement.y * displacement.y;
    }
    mean /= grid.displacement.size();
    energy /= grid.displacement.size();
    CHECK(std::abs(mean) < 1e-12);
    CHECK(std::abs(energy - spectrum.value().variance()) < 1e-10);
}

// Seed contract: rebuilding with the same settings and timestamp reproduces
// positions; changing the phase seed changes the generated wave field.
TEST_CASE("fluids.ocean.spectrum.spec_seed_reproducibility") {
    OceanSpectrumBuilder builder;
    builder.settings.bands = {{16, 64.0, 8.0, 64.0}};
    auto firstSea          = builder.build();
    auto secondSea         = builder.build();
    REQUIRE(firstSea.ok());
    REQUIRE(secondSea.ok());
    auto first  = firstSea.value().frameBuilder(1.25).build();
    auto second = secondSea.value().frameBuilder(1.25).build();
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(glm::length(first.value().grids()[0].displacement[13] - second.value().grids()[0].displacement[13]) < 1e-12);
    builder.settings.seed = 9;
    auto changedSea       = builder.build();
    REQUIRE(changedSea.ok());
    auto changed = changedSea.value().frameBuilder(1.25).build();
    REQUIRE(changed.ok());
    CHECK(glm::length(first.value().grids()[0].displacement[13] - changed.value().grids()[0].displacement[13]) > 1e-3);
}

// Spatial contract: the tile is periodic in both horizontal axes; inverting
// displaced world XZ recovers the same surface height.
TEST_CASE("fluids.ocean.spectrum.spec_periodic_position_and_world_query") {
    OceanSpectrumBuilder builder;
    builder.settings.bands      = {{16, 64.0, 8.0, 64.0}};
    builder.settings.choppiness = 0.2;
    auto spectrum               = builder.build();
    REQUIRE(spectrum.ok());
    auto frame = spectrum.value().frameBuilder(1.25).build();
    REQUIRE(frame.ok());
    for (glm::dvec2 parameter : {glm::dvec2(1.3, -2.7), glm::dvec2(-1e-20, 0.0), glm::dvec2(100000.1, 30.0)}) {
        auto point   = frame.value().position(parameter);
        auto shifted = frame.value().position(parameter + glm::dvec2(64.0, -64.0));
        REQUIRE(point.ok());
        REQUIRE(shifted.ok());
        CHECK(glm::length(shifted.value() - point.value() - glm::dvec3(64.0, 0.0, -64.0)) < 1e-9);
        auto water = frame.value().sample({point.value().x, point.value().z});
        REQUIRE(water.ok());
        CHECK(std::abs(water.value().height - point.value().y) < 1e-5);
    }
}

// Temporal contract: FFT propagation matches an independent discrete Fourier
// reconstruction of the t=0 field at each grid cell.
TEST_CASE("fluids.ocean.spectrum.spec_fft_matches_discrete_oracle") {
    OceanSpectrumBuilder builder;
    builder.settings.bands      = {{8, 32.0, 8.0, 32.0}};
    builder.settings.choppiness = 0.0;
    auto spectrum               = builder.build();
    REQUIRE(spectrum.ok());
    auto initial = spectrum.value().frameBuilder(0.0).build();
    auto later   = spectrum.value().frameBuilder(0.7).build();
    REQUIRE(initial.ok());
    REQUIRE(later.ok());
    constexpr unsigned                n   = 8;
    constexpr double                  tau = 2.0 * std::numbers::pi;
    std::vector<std::complex<double>> coefficients(n * n);
    const auto&                       start = initial.value().grids()[0];
    for (unsigned kz = 0; kz < n; ++kz)
        for (unsigned kx = 0; kx < n; ++kx)
            for (unsigned z = 0; z < n; ++z)
                for (unsigned x = 0; x < n; ++x)
                    coefficients[kz * n + kx] +=
                        start.displacement[z * n + x].y * std::polar(1.0, -tau * (kx * x + kz * z) / n) / double(n * n);
    for (unsigned z = 0; z < n; ++z) {
        for (unsigned x = 0; x < n; ++x) {
            std::complex<double> expected{};
            for (unsigned kz = 0; kz < n; ++kz) {
                for (unsigned kx = 0; kx < n; ++kx) {
                    const int    ix         = kx < n / 2 ? int(kx) : int(kx) - int(n);
                    const int    iz         = kz < n / 2 ? int(kz) : int(kz) - int(n);
                    const double waveNumber = std::hypot(double(ix), double(iz)) * tau / 32.0;
                    const double frequency  = std::sqrt(9.81 * waveNumber);
                    const double direction  = ix >= 0 ? -1.0 : 1.0;
                    expected += coefficients[kz * n + kx] *
                                std::polar(1.0, tau * (kx * x + kz * z) / n + direction * frequency * 0.7);
                }
            }
            CHECK(std::abs(expected.imag()) < 1e-10);
            CHECK(std::abs(expected.real() - later.value().grids()[0].displacement[z * n + x].y) < 1e-10);
        }
    }
}

// Velocity contract: the published velocity field equals the centered time
// derivative of displacement at every grid cell.
TEST_CASE("fluids.ocean.spectrum.spec_velocity_is_time_derivative") {
    OceanSpectrumBuilder builder;
    builder.settings.bands = {{16, 64.0, 8.0, 64.0}};
    auto spectrum          = builder.build();
    REQUIRE(spectrum.ok());
    auto before = spectrum.value().frameBuilder(1.0 - 1e-4).build();
    auto now    = spectrum.value().frameBuilder(1.0).build();
    auto after  = spectrum.value().frameBuilder(1.0 + 1e-4).build();
    REQUIRE(before.ok());
    REQUIRE(now.ok());
    REQUIRE(after.ok());
    for (unsigned i = 0; i < 256; ++i) {
        const auto derivative =
            (after.value().grids()[0].displacement[i] - before.value().grids()[0].displacement[i]) / 2e-4;
        CHECK(glm::length(derivative - now.value().grids()[0].velocity[i]) < 1e-6);
    }
}

// Builder contract: invalid time, overlapping bands and non-radix-two grids
// fail; an already-built spectrum remains usable after settings change.
TEST_CASE("fluids.ocean.spectrum.spec_input_validation_and_lifetime") {
    OceanSpectrumBuilder builder;
    builder.settings.bands = {{16, 64.0, 8.0, 64.0}};
    auto spectrum          = builder.build();
    REQUIRE(spectrum.ok());
    CHECK(!spectrum.value().frameBuilder(std::numeric_limits<double>::infinity()).build().ok());
    builder.settings.bands.push_back(builder.settings.bands.front());
    CHECK(!builder.build().ok());
    builder.settings.bands.resize(1);
    builder.settings.bands[0].resolution = 15;
    CHECK(!builder.build().ok());
    auto retained = spectrum.value().frameBuilder(1.0).build();
    REQUIRE(retained.ok());
    CHECK_EQ(retained.value().grids().size(), std::size_t{1});
}

// Ownership and current contract: a frame builder keeps an immutable spectrum
// lease even after the source builder and spectrum are destroyed. Current
// translates world position by velocity times simulation time.
TEST_CASE("fluids.ocean.spectrum.spec_builder_lease_and_current") {
    auto movingBuilder = [] {
        OceanSpectrumBuilder author;
        author.settings.bands   = {{16, 64.0, 8.0, 64.0}};
        author.settings.current = {4.0, 0.0};
        auto spectrum           = author.build().expect("valid authored sea");
        return spectrum.frameBuilder(1.0);
    }();
    auto moving = movingBuilder.build();
    REQUIRE(moving.ok());
    OceanSpectrumBuilder stillAuthor;
    stillAuthor.settings.bands = {{16, 64.0, 8.0, 64.0}};
    auto stillSpectrum         = stillAuthor.build();
    REQUIRE(stillSpectrum.ok());
    auto still = stillSpectrum.value().frameBuilder(1.0).build();
    REQUIRE(still.ok());
    auto a = still.value().position({8.0, 12.0});
    auto b = moving.value().position({12.0, 12.0});
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(glm::length(b.value() - a.value() - glm::dvec3(4.0, 0.0, 0.0)) < 1e-10);
    stillAuthor.settings.significantWaveHeight = 0.0;
    auto calm                                  = stillAuthor.build();
    REQUIRE(calm.ok());
    auto flat = calm.value().frameBuilder(3.0).build();
    REQUIRE(flat.ok());
    for (const auto& displacement : flat.value().grids()[0].displacement) CHECK_EQ(glm::length(displacement), 0.0);
}
}  // namespace
#endif
}  // namespace test_fluids_combined_ocean_spectrum

// Responsibility: ocean_spray
namespace test_fluids_combined_ocean_spray {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_spray)
using namespace eve::fluids;

namespace {

OceanSpectrumFrame stillWater(double time) {
    OceanSpectrumBuilder builder;
    builder.settings.significantWaveHeight = 0;
    builder.settings.bands                 = {{8, 32.0, 8.0, 32.0}};
    auto sea                               = builder.build();
    REQUIRE(sea.ok());
    auto frame = sea.value().frameBuilder(time).build();
    REQUIRE(frame.ok());
    return std::move(frame).takeValue();
}

OceanBoatReceipt oneContact(double speed = 4.0) {
    OceanBoatReceipt receipt;
    receipt.contacts.push_back({{0, 0, 0}, {speed, 0, 0}, 1});
    return receipt;
}

}  // namespace

TEST_CASE("fluids.ocean.spray.create checks each settings domain and starts empty") {
    OceanSpraySettings valid;
    auto               frame = OceanSprayFrame::create(valid);
    REQUIRE(frame.ok());
    CHECK(frame.value().particles().empty());
    CHECK_EQ(frame.value().time(), 0.0);
    CHECK_EQ(frame.value().report().emitted, 0u);

    valid.capacity = 0;
    CHECK(!OceanSprayFrame::create(valid).ok());
    valid.capacity = 16385;
    CHECK(!OceanSprayFrame::create(valid).ok());
    valid.capacity = 128;
    valid.gravity  = 0;
    CHECK(!OceanSprayFrame::create(valid).ok());
    valid.gravity  = 9.81;
    valid.lifetime = 0.04;
    CHECK(!OceanSprayFrame::create(valid).ok());
    valid.lifetime = 1;
    valid.radius   = 0.0009;
    CHECK(!OceanSprayFrame::create(valid).ok());
    valid.radius                 = 0.04;
    valid.emissionPerVolumeSpeed = std::numeric_limits<double>::infinity();
    CHECK(!OceanSprayFrame::create(valid).ok());
}

TEST_CASE("fluids.ocean.spray.step is repeatable across source traversal order") {
    OceanSpraySettings settings;
    settings.capacity = 128;
    auto initial      = OceanSprayFrame::create(settings);
    REQUIRE(initial.ok());
    auto a                   = oneContact();
    auto b                   = oneContact(5.0);
    b.contacts[0].position.x = 3.0;
    const std::array<OceanSpraySource, 2> ab{{{1, a}, {2, b}}};
    const std::array<OceanSpraySource, 2> ba{{{2, b}, {1, a}}};
    auto                                  waterFrame = stillWater(0);
    OceanWaterView                        water(waterFrame);
    auto forward = OceanSprayStepBuilder(initial.value(), water, ab, 1.0 / 30.0).build();
    auto reverse = OceanSprayStepBuilder(initial.value(), water, ba, 1.0 / 30.0).build();
    REQUIRE(forward.ok());
    REQUIRE(reverse.ok());
    REQUIRE(!forward.value().particles().empty());
    CHECK(initial.value().particles().empty());
    CHECK_EQ(initial.value().time(), 0.0);
    REQUIRE_EQ(forward.value().particles().size(), reverse.value().particles().size());
    for (std::size_t i = 0; i < forward.value().particles().size(); ++i) {
        const auto& p = forward.value().particles()[i];
        const auto& q = reverse.value().particles()[i];
        CHECK_EQ(p.id, q.id);
        CHECK(glm::length(p.position - q.position) == 0.0);
        CHECK(glm::length(p.velocity - q.velocity) == 0.0);
    }

    settings.seed      = 2;
    auto differentSeed = OceanSprayFrame::create(settings);
    REQUIRE(differentSeed.ok());
    auto changed = OceanSprayStepBuilder(differentSeed.value(), water, ab, 1.0 / 30.0).build();
    REQUIRE(changed.ok());
    CHECK(glm::length(changed.value().particles()[0].velocity - forward.value().particles()[0].velocity) > 1e-4);
}

TEST_CASE("fluids.ocean.spray.step integrates gravity and expires particles") {
    OceanSpraySettings settings;
    settings.capacity               = 2;
    settings.lifetime               = 0.1;
    settings.emissionPerVolumeSpeed = 1000;
    auto initial                    = OceanSprayFrame::create(settings);
    REQUIRE(initial.ok());
    auto                                  contact = oneContact();
    const std::array<OceanSpraySource, 1> sources{{{1, contact}}};
    auto                                  waterFrame = stillWater(0);
    OceanWaterView                        water(waterFrame);
    auto emitted = OceanSprayStepBuilder(initial.value(), water, sources, 1.0 / 30.0).build();
    REQUIRE(emitted.ok());
    REQUIRE_EQ(emitted.value().particles().size(), std::size_t{2});
    CHECK_EQ(emitted.value().report().emitted, 2u);
    CHECK_EQ(emitted.value().report().dropped, 98u);

    const auto     firstParticle  = emitted.value().particles()[0];
    auto           nextWaterFrame = stillWater(emitted.value().time());
    OceanWaterView nextWater(nextWaterFrame);
    auto           next = OceanSprayStepBuilder(emitted.value(), nextWater, {}, 1.0 / 30.0).build();
    REQUIRE(next.ok());
    REQUIRE_EQ(next.value().particles().size(), std::size_t{2});
    const auto& moved = next.value().particles()[0];
    CHECK(glm::length(moved.position - (firstParticle.position + firstParticle.velocity / 30.0 +
                                        glm::dvec3(0, -0.5 * 9.81 / 900.0, 0))) < 1e-12);
    CHECK(std::abs(moved.velocity.y - (firstParticle.velocity.y - 9.81 / 30.0)) < 1e-12);

    auto     current = std::move(next).takeValue();
    unsigned expired = 0;
    for (int i = 0; i < 8; ++i) {
        auto           frame = stillWater(current.time());
        OceanWaterView view(frame);
        auto           advanced = OceanSprayStepBuilder(current, view, {}, 1.0 / 30.0).build();
        REQUIRE(advanced.ok());
        expired += advanced.value().report().expired;
        current = std::move(advanced).takeValue();
    }
    CHECK(current.particles().empty());
    CHECK_EQ(expired, 2u);
}

TEST_CASE("fluids.ocean.spray.step rejects a whole invalid batch without changing its input") {
    auto initial = OceanSprayFrame::create({});
    REQUIRE(initial.ok());
    auto good                  = oneContact();
    auto bad                   = good;
    bad.contacts[0].position.x = std::numeric_limits<double>::quiet_NaN();
    const std::array<OceanSpraySource, 2> mixed{{{1, good}, {2, bad}}};
    const std::array<OceanSpraySource, 2> duplicate{{{1, good}, {1, good}}};
    auto                                  waterFrame = stillWater(0);
    OceanWaterView                        water(waterFrame);
    CHECK(!OceanSprayStepBuilder(initial.value(), water, mixed, 1.0 / 30.0).build().ok());
    CHECK(!OceanSprayStepBuilder(initial.value(), water, duplicate, 1.0 / 30.0).build().ok());
    CHECK(!OceanSprayStepBuilder(initial.value(), water, {}, 0.0).build().ok());
    CHECK(!OceanSprayStepBuilder(initial.value(), water, {}, 1.0).build().ok());
    auto           staleFrame = stillWater(1);
    OceanWaterView stale(staleFrame);
    CHECK(!OceanSprayStepBuilder(initial.value(), stale, {}, 1.0 / 30.0).build().ok());
    CHECK_EQ(initial.value().time(), 0.0);
    CHECK(initial.value().particles().empty());
}
#endif
}  // namespace test_fluids_combined_ocean_spray

// Responsibility: ocean_surface
namespace test_fluids_combined_ocean_surface {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_surface)
namespace {
using eve::fluids::evaluateOceanCellForce;
using eve::fluids::OceanBuoyancyCell;
using eve::fluids::OceanSurface;
using eve::fluids::OceanSurfaceSample;
using eve::fluids::OceanSurfaceSettings;
using eve::fluids::OceanWave;

constexpr double tolerance = 1e-8;

OceanSurfaceSettings twoWaveSea() {
    OceanSurfaceSettings settings;
    settings.seaLevel = 2.0;
    settings.current  = {0.7, -0.3};
    settings.waves    = {{{1.0, 0.3}, 0.8, 18.0, 0.4, 0.7}, {{-0.4, 1.0}, 0.2, 7.0, 1.2, 0.4}};
    return settings;
}

}  // namespace

// create: accepted settings are copied and wave directions are normalized.
// Mutating the caller's wave vector after construction must not alter output.
TEST_CASE("fluids.ocean.createCopiesAndNormalizesWaves") {
    OceanSurfaceSettings settings;
    settings.waves  = {{{2.0, 0.0}, 0.2, 12.0, 0.0, 0.2}};
    auto normalized = OceanSurface::create(settings);
    REQUIRE(normalized.ok());
    auto expectedSettings               = settings;
    expectedSettings.waves[0].direction = {1.0, 0.0};
    auto expected                       = OceanSurface::create(expectedSettings);
    REQUIRE(expected.ok());
    const auto before = normalized.value().position({3.0, 4.0}, 1.0);
    const auto equal  = expected.value().position({3.0, 4.0}, 1.0);
    REQUIRE(before.ok());
    REQUIRE(equal.ok());
    CHECK(glm::length(before.value() - equal.value()) < tolerance);
    settings.waves.clear();
    const auto after = normalized.value().position({3.0, 4.0}, 1.0);
    REQUIRE(after.ok());
    CHECK(glm::length(before.value() - after.value()) < tolerance);
}

// create: each invalid scalar, component count, or folded steepness fails.
TEST_CASE("fluids.ocean.createRejectsInvalidSettings") {
    OceanSurfaceSettings settings;
    settings.gravity = 0.0;
    CHECK(!OceanSurface::create(settings).ok());
    settings.gravity = 101.0;
    CHECK(!OceanSurface::create(settings).ok());
    settings.gravity  = 9.81;
    settings.seaLevel = std::numeric_limits<double>::infinity();
    CHECK(!OceanSurface::create(settings).ok());
    settings.seaLevel = 0.0;
    settings.current  = {101.0, 0.0};
    CHECK(!OceanSurface::create(settings).ok());
    settings.current = {0.0, 0.0};
    settings.waves.resize(65);
    CHECK(!OceanSurface::create(settings).ok());
    settings.waves = {{{1.0, 0.0}, 1.0, 1.0, 0.0, 1.0}};
    CHECK(!OceanSurface::create(settings).ok());
    settings.waves = {{{0.0, 0.0}, 0.2, 12.0, 0.0, 0.2}};
    CHECK(!OceanSurface::create(settings).ok());
    settings.waves = {{{1.0, 0.0}, 0.2, 0.0, 0.0, 0.2}};
    REQUIRE(!OceanSurface::create(settings).ok());
}

// position/sample: a flat sea has exact height, up normal, and uniform current.
TEST_CASE("fluids.ocean.flatSeaHasExactWorldSample") {
    OceanSurfaceSettings settings;
    settings.seaLevel = 3.0;
    settings.current  = {0.5, -0.25};
    auto sea          = OceanSurface::create(settings);
    REQUIRE(sea.ok());
    const auto position = sea.value().position({4.0, -2.0}, 7.0);
    const auto sample   = sea.value().sample({4.0, -2.0}, 7.0);
    REQUIRE(position.ok());
    REQUIRE(sample.ok());
    CHECK_EQ(position.value().x, 4.0);
    CHECK_EQ(position.value().y, 3.0);
    CHECK_EQ(position.value().z, -2.0);
    CHECK_EQ(sample.value().height, 3.0);
    CHECK_EQ(sample.value().normal.y, 1.0);
    CHECK_EQ(sample.value().velocity.x, 0.5);
    CHECK_EQ(sample.value().velocity.z, -0.25);
    CHECK_EQ(sample.value().parameterPosition.x, 4.0);
    CHECK_EQ(sample.value().parameterPosition.y, -2.0);
    CHECK_EQ(sample.value().horizontalJacobian, 1.0);
    CHECK_EQ(sample.value().time, 7.0);
}

// position/sample: invalid coordinates and time are rejected at both query APIs.
TEST_CASE("fluids.ocean.queriesRejectNonfiniteAndOutOfRangeInputs") {
    auto sea = OceanSurface::create({});
    REQUIRE(sea.ok());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(!sea.value().position({nan, 0.0}, 0.0).ok());
    CHECK(!sea.value().sample({0.0, nan}, 0.0).ok());
    CHECK(!sea.value().position({1e9 + 1.0, 0.0}, 0.0).ok());
    CHECK(!sea.value().sample({0.0, 0.0}, 1e9 + 1.0).ok());
    CHECK(sea.value().sample({1e9, -1e9}, -1e9).ok());
}

// position/sample: the inverse query must recover displaced geometry, with
// normal and velocity agreeing with independent finite differences.
TEST_CASE("fluids.ocean.worldQueriesInvertAnimatedSurface") {
    const auto settings = twoWaveSea();
    auto       sea      = OceanSurface::create(settings);
    REQUIRE(sea.ok());
    constexpr double step = 1e-4;
    for (double time : {0.0, 1.5, 17.0}) {
        for (double x : {-40.0, 0.0, 13.0, 100000.0}) {
            const glm::dvec2 parameter{x, 2.7};
            const auto       p = sea.value().position(parameter, time);
            REQUIRE(p.ok());
            const auto sample = sea.value().sample({p.value().x, p.value().z}, time);
            REQUIRE(sample.ok());
            CHECK(std::abs(sample.value().height - p.value().y) < 1e-6);
            CHECK(glm::length(sample.value().parameterPosition - parameter) < 1e-6);
            CHECK(sample.value().horizontalJacobian > 0.0);
            const auto px   = sea.value().position(parameter + glm::dvec2(step, 0.0), time);
            const auto pz   = sea.value().position(parameter + glm::dvec2(0.0, step), time);
            const auto next = sea.value().position(parameter + settings.current * step, time + step);
            REQUIRE(px.ok());
            REQUIRE(pz.ok());
            REQUIRE(next.ok());
            const auto normal = glm::normalize(glm::cross(pz.value() - p.value(), px.value() - p.value()));
            CHECK(glm::length(normal - sample.value().normal) < 1e-4);
            CHECK(glm::length((next.value() - p.value()) / step - sample.value().velocity) < 1e-3);
        }
    }
}

// evaluateOceanCellForce: dry, half-submerged, and full-submerged cells have
// zero, half, and full assigned displacement volume respectively.
TEST_CASE("fluids.ocean.buoyancyTracksSubmergedVolume") {
    OceanBuoyancyCell cell;
    cell.radius = 1.0;
    cell.volume = 2.0;
    OceanSurfaceSample water;
    for (const auto& example : std::array<std::pair<double, double>, 3>{{{2.0, 0.0}, {0.0, 1.0}, {-2.0, 2.0}}}) {
        cell.center.y     = example.first;
        const auto result = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
        REQUIRE(result.ok());
        CHECK(std::abs(result.value().submergedVolume - example.second) < 1e-12);
        CHECK(std::abs(result.value().force.y - 1000.0 * 9.81 * example.second) < 1e-8);
    }
}

// evaluateOceanCellForce: drag opposes relative velocity; equal water and
// particle velocities remove drag without removing buoyancy.
TEST_CASE("fluids.ocean.dragUsesRelativeVelocity") {
    OceanBuoyancyCell cell;
    cell.center.y        = -2.0;
    cell.radius          = 1.0;
    cell.volume          = 2.0;
    cell.pointVelocity.x = 3.0;
    OceanSurfaceSample water;
    water.velocity.x  = 1.0;
    const auto moving = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
    REQUIRE(moving.ok());
    CHECK(moving.value().force.x < 0.0);
    cell.pointVelocity = water.velocity;
    const auto matched = evaluateOceanCellForce(cell, water, 1000.0, 9.81);
    REQUIRE(matched.ok());
    CHECK_EQ(matched.value().force.x, 0.0);
    CHECK_EQ(matched.value().force.y, moving.value().force.y);
}

// evaluateOceanCellForce: invalid cell dimensions and environmental scalars
// produce a failed result rather than a partially meaningful force.
TEST_CASE("fluids.ocean.forceRejectsInvalidInputs") {
    OceanBuoyancyCell  cell;
    OceanSurfaceSample water;
    cell.radius = 0.0;
    CHECK(!evaluateOceanCellForce(cell, water, 1000.0, 9.81).ok());
    cell.radius = 1.0;
    cell.volume = -1.0;
    CHECK(!evaluateOceanCellForce(cell, water, 1000.0, 9.81).ok());
    cell.volume = 2.0;
    CHECK(!evaluateOceanCellForce(cell, water, 0.0, 9.81).ok());
    CHECK(!evaluateOceanCellForce(cell, water, 1000.0, 0.0).ok());
    water.velocity.x = std::numeric_limits<double>::infinity();
    REQUIRE(!evaluateOceanCellForce(cell, water, 1000.0, 9.81).ok());
}
#endif
}  // namespace test_fluids_combined_ocean_surface

// Responsibility: ocean_transition
namespace test_fluids_combined_ocean_transition {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_transition)
namespace {
using eve::fluids::OceanSeaTransitionBuilder;
using eve::fluids::OceanSpectrum;
using eve::fluids::OceanSpectrumBuilder;

OceanSpectrumBuilder seaBuilder() {
    OceanSpectrumBuilder builder;
    builder.settings.bands                 = {{16, 64.0, 8.0, 64.0}};
    builder.settings.current               = {0.4, -0.2};
    builder.settings.significantWaveHeight = 0.3;
    return builder;
}

double blend(double time, double start, double duration) {
    const double u = (time - start) / duration;
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u));
}

}  // namespace

// Builder input contract: finite start, duration >= 1/30 s, and matching
// current, gravity, tile resolution, and tile length are required. A failure
// does not invalidate the endpoint spectra for a later valid build.
TEST_CASE("fluids.ocean.transitionBuilderValidatesTimeAndEndpointCompatibility") {
    auto sourceBuilder = seaBuilder();
    auto source        = sourceBuilder.build();
    REQUIRE(source.ok());
    auto targetBuilder                           = sourceBuilder;
    targetBuilder.settings.significantWaveHeight = 0.8;
    auto target                                  = targetBuilder.build();
    REQUIRE(target.ok());
    OceanSeaTransitionBuilder transition(source.value(), target.value());

    transition.duration = 0.0;
    CHECK(!transition.build().ok());
    transition.duration = 1.0 / 60.0;
    CHECK(!transition.build().ok());
    transition.duration = 1.0 / 30.0;
    CHECK(transition.build().ok());
    transition.duration = 2.0;
    transition.start    = std::numeric_limits<double>::quiet_NaN();
    CHECK(!transition.build().ok());
    transition.start = 1e9;
    CHECK(!transition.build().ok());
    transition.start = 0.0;
    CHECK(transition.build().ok());

    targetBuilder.settings.current.x += 1.0;
    auto differentCurrent = targetBuilder.build();
    REQUIRE(differentCurrent.ok());
    CHECK(!OceanSeaTransitionBuilder(source.value(), differentCurrent.value()).build().ok());
    targetBuilder.settings.current = sourceBuilder.settings.current;
    targetBuilder.settings.gravity += 1.0;
    auto differentGravity = targetBuilder.build();
    REQUIRE(differentGravity.ok());
    CHECK(!OceanSeaTransitionBuilder(source.value(), differentGravity.value()).build().ok());
    targetBuilder.settings.gravity             = sourceBuilder.settings.gravity;
    targetBuilder.settings.bands[0].resolution = 32;
    auto differentLayout                       = targetBuilder.build();
    REQUIRE(differentLayout.ok());
    CHECK(!OceanSeaTransitionBuilder(source.value(), differentLayout.value()).build().ok());
}

// Lifetime contract: the frame builder owns endpoint leases after the original
// builders and transition leave scope.
TEST_CASE("fluids.ocean.frameBuilderSurvivesItsTransition") {
    auto frameBuilder = [&] {
        auto builder = seaBuilder();
        auto source  = builder.build();
        REQUIRE(source.ok());
        builder.settings.significantWaveHeight = 0.8;
        auto target                            = builder.build();
        REQUIRE(target.ok());
        auto transition = OceanSeaTransitionBuilder(source.value(), target.value()).build();
        REQUIRE(transition.ok());
        return transition.value().frameBuilder(5.0);
    }();
    auto frame = frameBuilder.build();
    REQUIRE(frame.ok());
    CHECK(!frame.value().grids().empty());
}

// Time contract: before/at the start and at/after the end the projection is
// exactly its endpoint; the midpoint is the equal blend of both surfaces.
TEST_CASE("fluids.ocean.transitionEndpointsAndMidpointMatchSpectra") {
    auto builder = seaBuilder();
    auto source  = builder.build();
    REQUIRE(source.ok());
    builder.settings.significantWaveHeight = 0.8;
    builder.settings.seaLevel              = 0.7;
    builder.settings.seed                  = 12;
    auto target                            = builder.build();
    REQUIRE(target.ok());
    OceanSeaTransitionBuilder transitionBuilder(source.value(), target.value());
    transitionBuilder.start    = 10.0;
    transitionBuilder.duration = 8.0;
    auto transition            = transitionBuilder.build();
    REQUIRE(transition.ok());
    const glm::dvec2 parameter{3.25, 5.75};

    for (double time : {9.0, 10.0, 18.0, 19.0}) {
        const auto actual   = transition.value().frameBuilder(time).build();
        const auto expected = (time <= 10.0 ? source.value() : target.value()).frameBuilder(time).build();
        REQUIRE(actual.ok());
        REQUIRE(expected.ok());
        const auto a = actual.value().position(parameter);
        const auto b = expected.value().position(parameter);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        CHECK_EQ(glm::length(a.value() - b.value()), 0.0);
    }
    const auto middle = transition.value().frameBuilder(14.0).build();
    const auto first  = source.value().frameBuilder(14.0).build();
    const auto second = target.value().frameBuilder(14.0).build();
    REQUIRE(middle.ok());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const auto m = middle.value().position(parameter);
    const auto a = first.value().position(parameter);
    const auto b = second.value().position(parameter);
    REQUIRE(m.ok());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(glm::length(m.value() - (a.value() + b.value()) * 0.5) < 1e-12);
}

// Velocity contract: every cell includes both the interpolated endpoint
// velocity and the derivative of displacement blending.
TEST_CASE("fluids.ocean.transitionVelocityIsDerivativeOfBlendedDisplacement") {
    auto builder = seaBuilder();
    auto source  = builder.build();
    REQUIRE(source.ok());
    builder.settings.significantWaveHeight = 0.8;
    builder.settings.windDirection         = {0.3, 0.8};
    auto target                            = builder.build();
    REQUIRE(target.ok());
    OceanSeaTransitionBuilder transitionBuilder(source.value(), target.value());
    transitionBuilder.start    = 2.0;
    transitionBuilder.duration = 6.0;
    auto transition            = transitionBuilder.build();
    REQUIRE(transition.ok());
    constexpr double time   = 4.0;
    constexpr double step   = 1e-4;
    const double     weight = blend(time, 2.0, 6.0);
    const double     rate   = (blend(time + step, 2.0, 6.0) - blend(time - step, 2.0, 6.0)) / (2.0 * step);
    const auto       first  = source.value().frameBuilder(time).build();
    const auto       second = target.value().frameBuilder(time).build();
    const auto       actual = transition.value().frameBuilder(time).build();
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(actual.ok());
    const auto& a      = first.value().grids()[0];
    const auto& b      = second.value().grids()[0];
    const auto& result = actual.value().grids()[0];
    REQUIRE_EQ(result.velocity.size(), a.velocity.size());
    for (std::size_t index = 0; index < result.velocity.size(); ++index) {
        const auto expected = (1.0 - weight) * a.velocity[index] + weight * b.velocity[index] +
                              rate * (b.displacement[index] - a.displacement[index]);
        CHECK(glm::length(result.velocity[index] - expected) < 1e-8);
    }
}

// Query boundary: non-finite or out-of-domain simulation time fails without
// poisoning subsequent valid frame evaluation.
TEST_CASE("fluids.ocean.transitionRejectsInvalidFrameTime") {
    auto builder = seaBuilder();
    auto source  = builder.build();
    REQUIRE(source.ok());
    auto transition = OceanSeaTransitionBuilder(source.value(), source.value()).build();
    REQUIRE(transition.ok());
    CHECK(!transition.value().frameBuilder(std::numeric_limits<double>::quiet_NaN()).build().ok());
    CHECK(!transition.value().frameBuilder(1e9 + 1.0).build().ok());
    CHECK(transition.value().frameBuilder(0.0).build().ok());
}
#endif
}  // namespace test_fluids_combined_ocean_transition

// Responsibility: ocean_wake
namespace test_fluids_combined_ocean_wake {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ocean_wake)
using namespace eve::fluids;

namespace {
OceanWakeSettings settings(unsigned resolution = 32, double length = 64.0) {
    OceanWakeSettings value;
    value.resolution      = resolution;
    value.length          = length;
    value.absorptionWidth = length / 8.0;
    return value;
}

OceanSpectrumFrame stillSea(double time) {
    OceanSpectrumBuilder builder;
    builder.settings.significantWaveHeight = 0.0;
    builder.settings.bands                 = {{8, 32.0, 8.0, 32.0}};
    auto sea                               = builder.build();
    REQUIRE(sea.ok());
    auto frame = sea.value().frameBuilder(time).build();
    REQUIRE(frame.ok());
    return std::move(frame).takeValue();
}

eve::Result<void> step(OceanWake& wake, double dt, double endTime, const OceanSpectrumFrame& frame) {
    auto prepared = wake.prepareStep(dt, endTime, frame);
    if (!prepared) return eve::Result<void>::failure(prepared.status());
    return wake.commitStep(*prepared.value());
}
}  // namespace

TEST_CASE("fluids.ocean.wake.spec.impulseAndPropagation") {
    SUB_CASE("invalid member rejects the full impulse batch without changing velocity") {
        auto wake = OceanWake::create(settings());
        REQUIRE(wake.ok());
        auto* wakeView = wake.value().get();
        std::array<OceanWakeImpulse, 2> impulses{{{{0, 0}, 2, 1, {0, 0}, .5}, {{0, 0}, 0, 1, {0, 0}, .5}}};
        REQUIRE(!wakeView->inject(impulses).ok());
        auto untouched = wakeView->sample({0, 0});
        REQUIRE(untouched.ok());
        REQUIRE(untouched.value().velocity == 0);
        REQUIRE(wakeView->inject(std::span<const OceanWakeImpulse>(impulses.data(), 1)).ok());
        auto seeded = wakeView->sample({0, 0});
        REQUIRE(seeded.ok());
        REQUIRE(seeded.value().velocity == 1);
        REQUIRE(seeded.value().foam == .5);
    };
    SUB_CASE("height propagates to a neighbor, foam decays, and invalid dt is atomic") {
        auto wake = OceanWake::create(settings());
        REQUIRE(wake.ok());
        auto* wakeView = wake.value().get();
        const OceanWakeImpulse impulse{{0, 0}, 2, 1, {0, 0}, .5};
        REQUIRE(wakeView->inject(std::span(&impulse, 1)).ok());
        double neighborPeak = 0;
        for (int i = 1; i <= 120; ++i) {
            REQUIRE(step(*wakeView, 1.0 / 60.0, i / 60.0, stillSea(wakeView->time())).ok());
            auto neighbor = wakeView->sample({6, 0});
            REQUIRE(neighbor.ok());
            neighborPeak = std::max(neighborPeak, std::fabs(neighbor.value().height));
        }
        REQUIRE(neighborPeak > .01);
        auto before = wakeView->sample({0, 0});
        REQUIRE(before.ok());
        REQUIRE(before.value().foam < .2);
        REQUIRE(!step(*wakeView, -1, 3, stillSea(wakeView->time())).ok());
        auto after = wakeView->sample({0, 0});
        REQUIRE(after.ok());
        REQUIRE(after.value().height == before.value().height);
        auto outside = wakeView->sample({-32, 0});
        REQUIRE(outside.ok());
        REQUIRE(outside.value().height == 0);
        REQUIRE(glm::length(outside.value().gradient) == 0);
    };
    SUB_CASE("steep crest creates bounded white water while calm field remains clear") {
        auto wake = OceanWake::create(settings());
        REQUIRE(wake.ok());
        auto* wakeView = wake.value().get();
        auto calm = OceanWake::create(settings());
        REQUIRE(calm.ok());
        const OceanWakeImpulse crest{{0, 0}, 2, 5, {0, 0}, 0};
        REQUIRE(wakeView->inject(std::span(&crest, 1)).ok());
        double maximumFoam = 0;
        for (int i = 1; i <= 60; ++i) {
            auto frame = stillSea((i - 1) / 60.0);
            REQUIRE(step(*wakeView, 1.0 / 60.0, i / 60.0, frame).ok());
            REQUIRE(step(*calm.value(), 1.0 / 60.0, i / 60.0, frame).ok());
            for (double value : wakeView->gridView().foam) {
                REQUIRE(value >= 0);
                REQUIRE(value <= 1);
                maximumFoam = std::max(maximumFoam, value);
            }
        }
        REQUIRE(maximumFoam > .1);
        for (double value : calm.value()->gridView().foam) REQUIRE(value == 0);
    };
}

TEST_CASE("fluids.ocean.wake.spec.transactionAndView") {
    SUB_CASE("prepared step is owning, single-use, and tied to its wake revision") {
        auto wake  = OceanWake::create(settings(16, 32));
        auto other = OceanWake::create(settings(16, 32));
        REQUIRE(wake.ok());
        REQUIRE(other.ok());
        auto prepared = wake.value()->prepareStep(1.0 / 60.0, 1.0 / 60.0, stillSea(0));
        REQUIRE(prepared.ok());
        REQUIRE(wake.value()->time() == 0);
        REQUIRE(!other.value()->commitStep(*prepared.value()).ok());
        REQUIRE(wake.value()->commitStep(*prepared.value()).ok());
        REQUIRE(wake.value()->time() == 1.0 / 60.0);
        REQUIRE(!wake.value()->commitStep(*prepared.value()).ok());
        auto stale = wake.value()->prepareStep(1.0 / 60.0, 2.0 / 60.0, stillSea(1.0 / 60.0));
        REQUIRE(stale.ok());
        REQUIRE(wake.value()->recenter({2, 0}).ok());
        REQUIRE(!wake.value()->commitStep(*stale.value()).ok());
        auto old = wake.value()->prepareStep(1.0 / 60.0, 2.0 / 60.0, stillSea(1.0 / 60.0));
        REQUIRE(old.ok());
        const OceanWakeImpulse impulse{{0, 0}, 2, 1, {0, 0}, .5};
        REQUIRE(wake.value()->inject(std::span(&impulse, 1)).ok());
        REQUIRE(!wake.value()->commitStep(*old.value()).ok());
    };
    SUB_CASE("recenter preserves overlap, clears distant history, and invalidates old water view") {
        auto wake = OceanWake::create(settings());
        REQUIRE(wake.ok());
        OceanBoatReceipt boat;
        boat.contacts.push_back({{0, 0, 0}, {4, -1, 0}, 2});
        REQUIRE(injectOceanBoatWake(*wake.value(), boat, 1.0 / 60.0).ok());
        REQUIRE(step(*wake.value(), 1.0 / 60.0, 1.0 / 60.0, stillSea(0)).ok());
        auto before = wake.value()->sample({2, 0});
        REQUIRE(before.ok());
        REQUIRE(std::fabs(before.value().height) > 1e-5);
        REQUIRE(wake.value()->recenter({4, -2}).ok());
        auto overlap = wake.value()->sample({2, 0});
        REQUIRE(overlap.ok());
        REQUIRE(std::fabs(overlap.value().height - before.value().height) < 1e-12);
        OceanSpectrumBuilder builder;
        builder.settings.bands = {{16, 64, 8, 64}};
        auto sea               = builder.build();
        REQUIRE(sea.ok());
        auto frame = sea.value().frameBuilder(wake.value()->time()).build();
        REQUIRE(frame.ok());
        OceanWaterView view(frame.value(), *wake.value());
        auto           position = view.position({2.31, .77});
        REQUIRE(position.ok());
        auto sample = view.sample({position.value().x, position.value().z});
        REQUIRE(sample.ok());
        REQUIRE(std::fabs(position.value().y - sample.value().height) < 1e-5);
        auto geometry = view.geometry({2.31, .77});
        REQUIRE(geometry.ok());
        REQUIRE(glm::length(geometry.value().position - position.value()) < 1e-12);
        REQUIRE(!injectOceanBoatWake(*wake.value(), boat, 1.0 / 60.0).ok());
        REQUIRE(step(*wake.value(), 1.0 / 60.0, 2.0 / 60.0, frame.value()).ok());
        REQUIRE(!view.sample({0, 0}).ok());
        REQUIRE(wake.value()->recenter({1000, 1000}).ok());
        auto cleared = wake.value()->sample({1000, 1000});
        REQUIRE(cleared.ok());
        REQUIRE(cleared.value().height == 0);
    };
    SUB_CASE("many hull contacts share a bounded forcing budget and invalid contact is atomic") {
        auto wake = OceanWake::create(settings());
        REQUIRE(wake.ok());
        OceanBoatReceipt hull;
        for (int i = 0; i < 16; ++i) hull.contacts.push_back({{0, 0, 0}, {0, 0, 12}, 500});
        REQUIRE(injectOceanBoatWake(*wake.value(), hull, 1.0 / 60.0).ok());
        double maximumVelocity = 0, maximumFoam = 0;
        for (int z = -24; z <= 24; z += 2) {
            auto point = wake.value()->sample({0, double(z)});
            REQUIRE(point.ok());
            maximumVelocity = std::max(maximumVelocity, std::fabs(point.value().velocity));
            maximumFoam     = std::max(maximumFoam, point.value().foam);
        }
        REQUIRE(maximumVelocity > .1);
        REQUIRE(maximumVelocity < .25);
        REQUIRE(maximumFoam <= .020001);
        auto before = wake.value()->sample({0, 8});
        REQUIRE(before.ok());
        hull.contacts.back().position.x = std::numeric_limits<double>::quiet_NaN();
        REQUIRE(!injectOceanBoatWake(*wake.value(), hull, 1.0 / 60.0).ok());
        auto after = wake.value()->sample({0, 8});
        REQUIRE(after.ok());
        REQUIRE(after.value().velocity == before.value().velocity);
        REQUIRE(after.value().foam == before.value().foam);
    };
}

TEST_CASE("fluids.ocean.wake.spec.dispersionAndFoamTransport") {
    SUB_CASE("one dispersion step agrees with independent discrete Fourier solution") {
        auto config    = settings(16, 32);
        config.depth   = 3;
        config.damping = 0;
        auto wake      = OceanWake::create(config);
        REQUIRE(wake.ok());
        const OceanWakeImpulse impulse{{0, 0}, 2, 1, {0, 0}, 0};
        REQUIRE(wake.value()->inject(std::span(&impulse, 1)).ok());
        constexpr unsigned        n   = 16;
        constexpr double          tau = 2.0 * std::numbers::pi;
        std::array<double, n * n> velocity{};
        for (unsigned z = 0; z < n; ++z)
            for (unsigned x = 0; x < n; ++x) {
                auto sample = wake.value()->sample({double(x) * 2 - 16, double(z) * 2 - 16});
                REQUIRE(sample.ok());
                velocity[z * n + x] = sample.value().velocity;
            }
        std::complex<double> expected{};
        constexpr double     dt = 1.0 / 60.0;
        for (unsigned kz = 0; kz < n; ++kz)
            for (unsigned kx = 0; kx < n; ++kx) {
                int    ix = kx < n / 2 ? int(kx) : int(kx) - int(n);
                int    iz = kz < n / 2 ? int(kz) : int(kz) - int(n);
                double k  = std::hypot(double(ix), double(iz)) * tau / 32.0;
                if (k == 0) continue;
                std::complex<double> coefficient{};
                for (unsigned z = 0; z < n; ++z)
                    for (unsigned x = 0; x < n; ++x)
                        coefficient +=
                            velocity[z * n + x] * std::polar(1.0, -tau * (kx * x + kz * z) / n) / double(n * n);
                double omega = std::sqrt(9.81 * k * std::tanh(k * config.depth));
                expected += coefficient * (std::sin(omega * dt) / omega) * std::polar(1.0, tau * (kx + kz) * .5);
            }
        REQUIRE(step(*wake.value(), dt, dt, stillSea(0)).ok());
        auto actual = wake.value()->sample({0, 0});
        REQUIRE(actual.ok());
        REQUIRE(std::fabs(expected.imag()) < 1e-10);
        REQUIRE(std::fabs(actual.value().height - expected.real()) < 1e-10);
    };
    SUB_CASE("current advects foam one cell then decay and edge sponge remove it") {
        auto config      = settings(32, 32);
        config.foamDecay = .7;
        auto wake        = OceanWake::create(config);
        REQUIRE(wake.ok());
        const OceanWakeImpulse impulse{{-3, 2}, 1, 0, {0, 0}, .8};
        REQUIRE(wake.value()->inject(std::span(&impulse, 1)).ok());
        auto                      initial = wake.value()->gridView();
        const std::vector<double> old(initial.foam.begin(), initial.foam.end());
        OceanSpectrumBuilder      builder;
        builder.settings.significantWaveHeight = 0;
        builder.settings.current               = {30, -30};
        builder.settings.bands                 = {{8, 32, 8, 32}};
        auto sea                               = builder.build();
        REQUIRE(sea.ok());
        auto frame = sea.value().frameBuilder(0).build();
        REQUIRE(frame.ok());
        constexpr double dt = 1.0 / 30.0;
        REQUIRE(step(*wake.value(), dt, dt, frame.value()).ok());
        auto moved = wake.value()->gridView();
        for (unsigned z = 0; z < 32; ++z)
            for (unsigned x = 0; x < 32; ++x) {
                double edge   = double(std::min({x, z, 31 - x, 31 - z}));
                double sponge = std::clamp(1.0 - edge / 4.0, 0.0, 1.0);
                double expected =
                    x > 0 && z < 31 ? old[(z + 1) * 32 + x - 1] * std::exp(-(.7 + 8.0 * sponge * sponge) * dt) : 0;
                REQUIRE(std::fabs(moved.foam[z * 32 + x] - expected) < 1e-12);
            }
        for (int i = 2; i <= 24; ++i) {
            auto next = sea.value().frameBuilder((i - 1) * dt).build();
            REQUIRE(next.ok());
            REQUIRE(step(*wake.value(), dt, i * dt, next.value()).ok());
        }
        for (double foam : wake.value()->gridView().foam) {
            REQUIRE(foam >= 0);
            REQUIRE(foam < 1e-8);
        }
    };
    SUB_CASE("late transport failure preserves all grid streams and simulation time") {
        auto config   = settings(32, 32);
        config.center = {1e9 - 16, 0};
        auto wake     = OceanWake::create(config);
        REQUIRE(wake.ok());
        const OceanWakeImpulse impulse{config.center, 1, 1, {0, 0}, .8};
        REQUIRE(wake.value()->inject(std::span(&impulse, 1)).ok());
        auto                      view = wake.value()->gridView();
        const std::vector<double> foam(view.foam.begin(), view.foam.end());
        const std::vector<double> height(view.height.begin(), view.height.end());
        OceanSpectrumBuilder      builder;
        builder.settings.significantWaveHeight = 0;
        builder.settings.current               = {-100, 0};
        builder.settings.bands                 = {{8, 32, 8, 32}};
        auto sea                               = builder.build();
        REQUIRE(sea.ok());
        auto stale = sea.value().frameBuilder(1).build();
        REQUIRE(stale.ok());
        REQUIRE(!step(*wake.value(), 1.0 / 30.0, 1.0 / 30.0, stale.value()).ok());
        auto current = sea.value().frameBuilder(0).build();
        REQUIRE(current.ok());
        REQUIRE(!step(*wake.value(), 1.0 / 30.0, 1.0 / 30.0, current.value()).ok());
        REQUIRE(wake.value()->time() == 0);
        auto after = wake.value()->gridView();
        REQUIRE(std::equal(foam.begin(), foam.end(), after.foam.begin()));
        REQUIRE(std::equal(height.begin(), height.end(), after.height.begin()));
    };
}
#endif
}  // namespace test_fluids_combined_ocean_wake
