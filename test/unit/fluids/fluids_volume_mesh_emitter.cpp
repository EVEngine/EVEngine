#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <string_view>
#include "common/Profile.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidEmitter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// fluids_volume_mesh_emitter.cpp
using namespace eve::fluids;
TEST_CASE("fluids.volumeEmitter.meshInteriorAndScale") {
    const glm::vec3 vertices[] = {{-.3f, -.3f, -.3f}, {.3f, -.3f, -.3f}, {.3f, .3f, -.3f}, {-.3f, .3f, -.3f},
                                  {-.3f, -.3f, .3f},  {.3f, -.3f, .3f},  {.3f, .3f, .3f},  {-.3f, .3f, .3f}};
    const uint32_t  indices[]  = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                  3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    auto            result     = buildVolumeFluidMeshDistribution(vertices, indices, {1, 1, 1}, .1f);
    REQUIRE(result.ok());
    REQUIRE(!result.value().empty());
    bool interior = false;
    for (const auto& p : result.value()) {
        if (glm::length(p.position) < .09f) interior = true;
        REQUIRE(glm::length(p.position) < .7f);
    }
    REQUIRE(interior);
    auto mirrored = buildVolumeFluidMeshDistribution(vertices, indices, {-1, 1, 1}, .1f);
    REQUIRE(mirrored.ok());
    REQUIRE(mirrored.value().size() == result.value().size());
    const uint32_t invalid[] = {0, 1, 99};
    REQUIRE(!buildVolumeFluidMeshDistribution(vertices, invalid, {1, 1, 1}, .1f).ok());
    REQUIRE(!buildVolumeFluidMeshDistribution(vertices, indices, {0, 1, 1}, .1f).ok());
}
TEST_CASE("fluids.volumeEmitter.meshOpenSurface") {
    const glm::vec3 vertices[] = {{-.2f, -.2f, 0}, {.2f, -.2f, 0}, {.2f, .2f, 0}, {-.2f, .2f, 0}};
    const uint32_t  indices[]  = {0, 1, 2, 0, 2, 3};
    auto            result     = buildVolumeFluidMeshDistribution(vertices, indices, {1, 1, 1}, .1f);
    REQUIRE(result.ok());
    REQUIRE(!result.value().empty());
    for (const auto& p : result.value()) REQUIRE(std::abs(p.position.z) < .00001f);
}

TEST_CASE("fluids.volumeEmitter.meshTorusPreservesHole") {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t>  indices;
    constexpr unsigned     rings = 24, sides = 12;
    for (unsigned u = 0; u < rings; ++u)
        for (unsigned v = 0; v < sides; ++v) {
            const float a = 6.28318530718f * float(u) / rings, b = 6.28318530718f * float(v) / sides;
            const float radius = .3f + .12f * std::cos(b);
            vertices.push_back({radius * std::cos(a), .12f * std::sin(b), radius * std::sin(a)});
        }
    for (unsigned u = 0; u < rings; ++u)
        for (unsigned v = 0; v < sides; ++v) {
            const uint32_t a = u * sides + v, b = ((u + 1) % rings) * sides + v,
                           c = ((u + 1) % rings) * sides + (v + 1) % sides, d = u * sides + (v + 1) % sides;
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    auto result = buildVolumeFluidMeshDistribution(vertices, indices, {1, 1, 1}, .04f);
    REQUIRE(result.ok());
    REQUIRE(!result.value().empty());
    bool tubeInterior = false;
    for (const auto& p : result.value()) {
        REQUIRE(std::sqrt(p.position.x * p.position.x + p.position.z * p.position.z) > .12f);
        if (glm::length(p.position - glm::vec3(.3f, 0, 0)) < .05f) tubeInterior = true;
    }
    REQUIRE(tubeInterior);
}

// fluids_volume_part4.cpp
using namespace eve::fluids;

static eve::Value withoutRecentMaterialFields(eve::Value particle) {
    const auto* current = particle.find("material");
    eve::Value  material(eve::Value::Object{});
    for (const auto& key : current->keys())
        if (key != "atmosphericPressure" && key != "smoothing" && key != "rollingContacts" &&
            key != "rollingFriction" && key != "dynamicFriction" && key != "staticFriction" && key != "stickiness" &&
            key != "stickDistance" && key != "frictionCombine" && key != "stickinessCombine")
            material.set(key, *current->find(key));
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : particle.keys())
        if (key != "angularVelocity") legacy.set(key, *particle.find(key));
    legacy.set("material", std::move(material));
    return legacy;
}
static eve::Value withoutSdfColliders(const eve::Value& snapshot) {
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : snapshot.keys()) {
        if (key == "sdfColliders" || key == "heightFieldColliders" || key == "stitches") continue;
        if (key != "colliders") {
            legacy.set(key, *snapshot.find(key));
            continue;
        }
        eve::Value::Array colliders;
        const auto*       source = snapshot.find(key);
        for (size_t i = 0; i < source->arraySize(); ++i) {
            eve::Value collider(eve::Value::Object{});
            for (const auto& field : source->at(i).keys())
                if (field != "isTrigger" && field != "staticFriction" && field != "rollingFriction" &&
                    field != "stickiness" && field != "stickDistance" && field != "frictionCombine" &&
                    field != "stickinessCombine" && field != "rollingContacts")
                    collider.set(field, *source->at(i).find(field));
            colliders.push_back(std::move(collider));
        }
        legacy.set(key, eve::Value(std::move(colliders)));
    }
    return legacy;
}

TEST_CASE("fluids.volume.simplexTopologyIsAtomicMigratedAndRemapped") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-.2f, 1, 0};
    particles[0].life     = .001f;
    particles[1].position = {.2f, 1, 0};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidSimplex topology[2] = {{{0, 1, 0}, 2}, {{1, 0, 0}, 1}};
    REQUIRE(solver->setSimplexes(topology).ok());
    auto               before  = solver->snapshot();
    VolumeFluidSimplex invalid = {{0, 0, 0}, 2};
    REQUIRE(!solver->setSimplexes(std::span(&invalid, 1)).ok());
    REQUIRE(solver->snapshot().simplexes.size() == before.simplexes.size());
    REQUIRE(solver->step(.01f, 2).ok());
    auto remapped = solver->snapshot();
    REQUIRE(remapped.particles.size() == 1);
    REQUIRE(remapped.simplexes.size() == 1);
    REQUIRE(remapped.simplexes[0].particleIndices[0] == 0);
    auto        encoded         = encodeVolumeFluid(remapped);
    const auto& currentParticle = encoded.find("particles")->at(0);
    eve::Value  oldParticle(eve::Value::Object{});
    for (const auto& key : currentParticle.keys())
        if (key != "actorGroup" && key != "selfCollide") oldParticle.set(key, *currentParticle.find(key));
    encoded.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(oldParticle))}));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 6);
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : encoded.keys())
        if (key != "simplexes") legacy.set(key, *encoded.find(key));
    auto migrated = decodeVolumeFluid(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().simplexes.empty());
}

TEST_CASE("fluids.volume.propagationUsesStableLabelAndCanBeDisabled") {
    VolumeFluidSnapshot input;
    input.settings.gravity = {0, 0, 0};
    VolumeFluidCollider a, b;
    a.label  = 17;
    b.label  = 3;
    a.radius = b.radius = .01f;
    a.solidify = b.solidify = true;
    a.center                = {-.1f, 1, 0};
    b.center                = {.1f, 1, 0};
    input.colliders         = {a, b};
    VolumeFluidParticle left, right, center;
    left.position       = a.center;
    right.position      = b.center;
    center.position     = {0, 1, 0};
    left.material.phase = right.material.phase = VolumeFluidPhase::Solid;
    center.material.cohesion                   = 0;
    center.material.viscosity                  = 0;
    input.particles                            = {left, right, center};
    input.attachments                          = {{0, 17, {0, 0, 0}}, {1, 3, {0, 0, 0}}};
    auto created                               = VolumeFluid::create(input.settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->restore(input).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->snapshot().attachments.size() == 3);
    REQUIRE(solver->snapshot().attachments.back().colliderLabel == 3);
    std::swap(input.colliders[0], input.colliders[1]);
    std::swap(input.attachments[0], input.attachments[1]);
    REQUIRE(solver->restore(input).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->snapshot().attachments.back().colliderLabel == 3);
    for (auto& c : input.colliders) c.solidify = false;
    REQUIRE(solver->restore(input).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->snapshot().attachments.size() == 2);
    REQUIRE(solver->particles()[2].material.phase == VolumeFluidPhase::Liquid);
    input.colliders[0].solidify = input.colliders[1].solidify = true;
    input.attachments.clear();
    REQUIRE(solver->restore(input).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->snapshot().attachments.empty());
    REQUIRE(solver->particles()[2].material.phase == VolumeFluidPhase::Liquid);
}

TEST_CASE("fluids.volume.solidColorSelectionAndAtomicity") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[4];
    for (unsigned i = 0; i < 4; ++i) {
        particles[i].position       = {float(i) * .2f, 1, 0};
        particles[i].velocity       = {1, 2, 3};
        particles[i].material.phase = VolumeFluidPhase(i);
        particles[i].data           = {4, 1, 7, -2};
        particles[i].life           = 2;
    }
    REQUIRE(solver->emit(particles).ok());
    const glm::vec4 gold(.95f, .65f, .2f, .8f);
    REQUIRE(solver->applySolidColor(gold).ok());
    auto output = solver->particles();
    for (unsigned i = 0; i < 4; ++i) {
        REQUIRE(glm::length(output[i].color - (i == 3 ? gold : particles[i].color)) == 0.f);
        REQUIRE(glm::length(output[i].position - particles[i].position) == 0.f);
        REQUIRE(glm::length(output[i].velocity - particles[i].velocity) == 0.f);
        REQUIRE(glm::length(output[i].data - particles[i].data) == 0.f);
        REQUIRE(output[i].life == 2.f);
    }
    REQUIRE(!solver->applySolidColor({0, 1, 0, -.1f}).ok());
    REQUIRE(!solver->applySolidColor({std::numeric_limits<float>::quiet_NaN(), 0, 0, 1}).ok());
    REQUIRE(glm::length(solver->particles()[3].color - gold) == 0.f);
    auto decoded = decodeVolumeFluid(encodeVolumeFluid(solver->snapshot()));
    REQUIRE(decoded.ok());
    REQUIRE(glm::length(decoded.value().particles[3].color - gold) == 0.f);
    VolumeFluidMaterial liquid;
    REQUIRE(solver->paint(output[3].position, .01f, liquid).ok());
    REQUIRE(solver->applySolidColor({0, 0, 0, 1}).ok());
    REQUIRE(glm::length(solver->particles()[3].color - gold) == 0.f);
    solver->clear();
    REQUIRE(solver->applySolidColor(gold).ok());
}

TEST_CASE("fluids.volume.fieldSamplingVelocityDensityAndCurl") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle a, b;
    a.position                       = {-.05f, 1, 0};
    b.position                       = {.05f, 1, 0};
    a.velocity                       = {0, -1, 0};
    b.velocity                       = {0, 1, 0};
    const VolumeFluidParticle pair[] = {a, b};
    REQUIRE(solver->emit(pair).ok());
    const glm::vec3 queries[] = {{0, 1, 0}, {-.0001f, 1, 0}, {.0001f, 1, 0}, {100, 100, 100}};
    auto            samples   = solver->sampleField(queries);
    REQUIRE(samples.ok());
    const auto& s = samples.value();
    REQUIRE(s.size() == 4);
    REQUIRE(s[0].neighborCount == 2);
    REQUIRE(glm::length(s[0].velocity) < 1e-6f);
    REQUIRE(std::abs(s[0].vorticity.z - 8.f) < 1e-4f);
    const float derivative = (s[2].velocity.y - s[1].velocity.y) / .0002f;
    REQUIRE(std::abs(s[0].vorticity.z - derivative) < .002f);
    const float expected = 2.f * 1000.f * .001f * 315.f / (64.f * 3.14159265359f * .008f) * std::pow(.9375f, 3.f);
    REQUIRE(std::abs(s[0].density - expected) < .001f);
    REQUIRE(s[3].neighborCount == 0);
    REQUIRE(s[3].density == 0.f);
    REQUIRE(glm::length(s[3].velocity) == 0.f);
    REQUIRE(glm::length(s[3].vorticity) == 0.f);
    auto shifted = solver->snapshot();
    for (auto& p : shifted.particles) p.velocity += glm::vec3(4, 5, 6);
    REQUIRE(solver->restore(shifted).ok());
    auto moved = solver->sampleField(queries);
    REQUIRE(moved.ok());
    REQUIRE(glm::length(moved.value()[0].velocity - glm::vec3(4, 5, 6)) < 1e-6f);
    REQUIRE(glm::length(moved.value()[0].vorticity - s[0].vorticity) < 1e-6f);
}

TEST_CASE("fluids.volume.fieldSamplingFreshGridAndPhaseFilter") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto            solver = std::move(created).takeValue();
    const glm::vec3 query(0, 1, 0);
    for (auto phase :
         {VolumeFluidPhase::Liquid, VolumeFluidPhase::Gas, VolumeFluidPhase::Granular, VolumeFluidPhase::Solid}) {
        VolumeFluidParticle p;
        p.position       = query;
        p.material.phase = phase;
        p.velocity       = {1, 2, 3};
        REQUIRE(solver->emit(std::span(&p, 1)).ok());
    }
    auto samples = solver->sampleField(std::span(&query, 1));
    REQUIRE(samples.ok());
    REQUIRE(samples.value()[0].neighborCount == 2);
    REQUIRE(glm::length(samples.value()[0].velocity - glm::vec3(1, 2, 3)) < 1e-6f);
    REQUIRE(glm::length(samples.value()[0].vorticity) == 0.f);
    solver->clear();
    auto empty = solver->sampleField(std::span(&query, 1));
    REQUIRE(empty.ok());
    REQUIRE(empty.value()[0].neighborCount == 0);
    VolumeFluidParticle p;
    p.position = query;
    p.life     = .001f;
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    auto expired = solver->sampleField(std::span(&query, 1));
    REQUIRE(expired.ok());
    REQUIRE(expired.value()[0].neighborCount == 0);
    const glm::vec3 bad(std::numeric_limits<float>::quiet_NaN(), 0, 0);
    REQUIRE(!solver->sampleField(std::span(&bad, 1)).ok());
    REQUIRE(!solver->sampleField(std::vector<glm::vec3>(65537, query)).ok());
}

TEST_CASE("fluids.volume.fieldSamplingRejectsCandidateOverload") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle p;
    p.position = {0, 1, 0};
    REQUIRE(solver->emit(std::vector<VolumeFluidParticle>(1000, p)).ok());
    REQUIRE(!solver->sampleField(std::vector<glm::vec3>(4001, p.position)).ok());
    REQUIRE(solver->particleCount() == 1000);
    auto retry = solver->sampleField(std::span(&p.position, 1));
    REQUIRE(retry.ok());
    REQUIRE(retry.value()[0].neighborCount == 1000);
}

// fluids_volume_particle_attachment.cpp
using namespace eve::fluids;

namespace {
std::unique_ptr<VolumeFluid> attachmentSolver() {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, -10.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-.2f, 1.f, 0.f};
    particles[1].position = {.2f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidCollider target;
    target.label  = 71;
    target.center = {0.f, 1.f, 0.f};
    target.radius = .1f;
    REQUIRE(solver->setColliders(std::span(&target, 1)).ok());
    return solver;
}
eve::Value withoutStitches(const eve::Value& snapshot) {
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : snapshot.keys())
        if (key != "stitches") legacy.set(key, *snapshot.find(key));
    return legacy;
}
}  // namespace

TEST_CASE("fluids.volume.staticParticleAttachmentMovesWithoutChangingFluidPhase") {
    auto           solver = attachmentSolver();
    const unsigned index  = 0;
    REQUIRE(solver->bindStaticParticles(std::span(&index, 1), 71, false).value() == 1);
    auto state = solver->snapshot();
    REQUIRE(state.attachments.size() == 1);
    REQUIRE_NOT(state.attachments[0].constrainOrientation);
    REQUIRE(state.particles[0].material.phase == VolumeFluidPhase::Liquid);

    auto target = state.colliders[0];
    target.center += glm::vec3(.25f, .2f, 0.f);
    REQUIRE(solver->setColliders(std::span(&target, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(solver->particles()[0].position - glm::vec3(.05f, 1.2f, 0.f)) < 1e-5f);
    REQUIRE(solver->particles()[0].material.phase == VolumeFluidPhase::Liquid);
    REQUIRE(solver->particles()[1].position.y < 1.f);

    REQUIRE(solver->unbindStaticParticles(std::span(&index, 1)).value() == 1);
    const float releasedY = solver->particles()[0].position.y;
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.y < releasedY);
}

TEST_CASE("fluids.volume.staticParticleAttachmentOrientationAndValidationAreAtomic") {
    auto           solver     = attachmentSolver();
    const unsigned indices[2] = {0, 1};
    REQUIRE(solver->bindStaticParticles(std::span(indices, 1), 71, false).value() == 1);
    REQUIRE(solver->bindStaticParticles(std::span(indices + 1, 1), 71, true).value() == 1);
    auto target     = solver->snapshot().colliders[0];
    target.rotation = {0.f, 0.f, .70710678f, .70710678f};
    REQUIRE(solver->setColliders(std::span(&target, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(std::abs(solver->particles()[0].orientation.z) < 1e-5f);
    REQUIRE(std::abs(solver->particles()[1].orientation.z - .70710678f) < 1e-5f);

    const auto     before       = solver->snapshot();
    const unsigned duplicate[2] = {0, 0};
    REQUIRE(!solver->bindStaticParticles(duplicate, 71, false).ok());
    REQUIRE(!solver->unbindStaticParticles(duplicate).ok());
    REQUIRE(!solver->bindStaticParticles(std::span(indices, 1), 999, false).ok());
    REQUIRE(solver->snapshot().attachments.size() == before.attachments.size());
    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->snapshot().attachments.empty());
}

TEST_CASE("fluids.volume.staticParticleAttachmentV17MigrationPreservesOrientationConstraint") {
    auto           solver = attachmentSolver();
    const unsigned index  = 0;
    REQUIRE(solver->bindStaticParticles(std::span(&index, 1), 71, true).ok());
    auto       encoded       = encodeVolumeFluid(solver->snapshot());
    auto       oldAttachment = encoded.find("attachments")->at(0);
    eve::Value legacyAttachment(eve::Value::Object{});
    for (const auto& key : oldAttachment.keys())
        if (key != "constrainOrientation" && key != "dynamic" && key != "compliance" && key != "breakThreshold")
            legacyAttachment.set(key, *oldAttachment.find(key));
    encoded.set("attachments", eve::Value(eve::Value::Array{legacyAttachment}));
    encoded = withoutStitches(encoded);
    encoded.set("version", 17);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().attachments.size() == 1);
    REQUIRE(migrated.value().attachments[0].constrainOrientation);
    legacyAttachment.set("constrainOrientation", false);
    encoded.set("attachments", eve::Value(eve::Value::Array{legacyAttachment}));
    REQUIRE(!decodeVolumeFluid(encoded).ok());
}

TEST_CASE("fluids.volume.dynamicParticleAttachmentSupportsComplianceAndReaction") {
    auto           rigid = attachmentSolver();
    auto           soft  = attachmentSolver();
    const unsigned index = 0;
    REQUIRE(rigid->bindDynamicParticles(std::span(&index, 1), 71, 0.f, 1e12f, false).value() == 1);
    REQUIRE(soft->bindDynamicParticles(std::span(&index, 1), 71, .01f, 1e12f, false).value() == 1);
    auto rigidTarget = rigid->snapshot().colliders[0];
    auto softTarget  = soft->snapshot().colliders[0];
    rigidTarget.center.x += .25f;
    softTarget.center.x += .25f;
    REQUIRE(rigid->setColliders(std::span(&rigidTarget, 1)).ok());
    REQUIRE(soft->setColliders(std::span(&softTarget, 1)).ok());
    REQUIRE(rigid->step(1.f / 120.f, 1).ok());
    REQUIRE(soft->step(1.f / 120.f, 1).ok());
    REQUIRE(std::abs(rigid->particles()[0].position.x - .05f) < 1e-5f);
    REQUIRE(soft->particles()[0].position.x < rigid->particles()[0].position.x - .05f);
    REQUIRE(rigid->particles()[0].material.phase == VolumeFluidPhase::Liquid);
    REQUIRE(!rigid->attachmentReactions().empty());
    REQUIRE(glm::length(rigid->attachmentReactions()[0].impulse) > 0.f);
}

TEST_CASE("fluids.volume.dynamicParticleAttachmentBreaksWithoutPartialMutation") {
    auto           solver = attachmentSolver();
    const unsigned index  = 0;
    REQUIRE(!solver->bindDynamicParticles(std::span(&index, 1), 71, -1.f, 10.f, false).ok());
    REQUIRE(!solver->bindDynamicParticles(std::span(&index, 1), 71, 0.f, 0.f, false).ok());
    REQUIRE(solver->snapshot().attachments.empty());
    REQUIRE(solver->bindDynamicParticles(std::span(&index, 1), 71, 0.f, .001f, false).ok());
    REQUIRE(!solver->bindDynamicParticles(std::span(&index, 1), 71, 0.f, 10.f, false).ok());
    auto target = solver->snapshot().colliders[0];
    target.center.x += .25f;
    REQUIRE(solver->setColliders(std::span(&target, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->snapshot().attachments.empty());
    const float releasedY = solver->particles()[0].position.y;
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.y < releasedY);
}

TEST_CASE("fluids.volume.dynamicParticleAttachmentV18MigrationUsesStaticDefaults") {
    auto           solver = attachmentSolver();
    const unsigned index  = 0;
    REQUIRE(solver->bindStaticParticles(std::span(&index, 1), 71, false).ok());
    auto       encoded           = encodeVolumeFluid(solver->snapshot());
    const auto currentAttachment = encoded.find("attachments")->at(0);
    eve::Value legacyAttachment(eve::Value::Object{});
    for (const auto& key : currentAttachment.keys())
        if (key != "dynamic" && key != "compliance" && key != "breakThreshold")
            legacyAttachment.set(key, *currentAttachment.find(key));
    encoded.set("attachments", eve::Value(eve::Value::Array{legacyAttachment}));
    encoded = withoutStitches(encoded);
    encoded.set("version", 18);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE_NOT(migrated.value().attachments[0].dynamic);
    REQUIRE(migrated.value().attachments[0].compliance == 0.f);
    REQUIRE(migrated.value().attachments[0].breakThreshold == 1e12f);
    legacyAttachment.set("dynamic", false);
    encoded.set("attachments", eve::Value(eve::Value::Array{legacyAttachment}));
    REQUIRE(!decodeVolumeFluid(encoded).ok());
}

TEST_CASE("fluids.volume.stitchesCollapsePairsWithMassWeighting") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position         = {-.2f, 1.f, 0.f};
    particles[1].position         = {.2f, 1.f, 0.f};
    particles[0].material.density = 1000.f;
    particles[1].material.density = 2000.f;
    particles[0].selfCollide = particles[1].selfCollide = false;
    REQUIRE(solver->emit(particles).ok());
    const VolumeFluidStitch stitch{0, 1, 0.f};
    REQUIRE(solver->setStitches(std::span(&stitch, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(solver->particles()[0].position - solver->particles()[1].position) < 1e-5f);
    REQUIRE(std::abs(solver->particles()[0].position.x - .06666667f) < 1e-5f);
}

TEST_CASE("fluids.volume.stitchesValidatePersistAndRemapAtomically") {
    auto                    solver = attachmentSolver();
    const VolumeFluidStitch stitch{0, 1, .01f};
    REQUIRE(solver->setStitches(std::span(&stitch, 1)).ok());
    auto saved = solver->snapshot();
    REQUIRE(saved.version == 20);
    REQUIRE(saved.stitches.size() == 1);
    const VolumeFluidStitch duplicate[2] = {{0, 1, 0.f}, {1, 0, 0.f}};
    REQUIRE(!solver->setStitches(duplicate).ok());
    REQUIRE(solver->snapshot().stitches.size() == 1);
    REQUIRE(solver->restore(saved).ok());
    REQUIRE(solver->killParticle(0).ok());
    REQUIRE(solver->snapshot().stitches.empty());
}

TEST_CASE("fluids.volume.stitchesMigrateV19AndRejectFutureField") {
    auto       solver  = attachmentSolver();
    auto       encoded = encodeVolumeFluid(solver->snapshot());
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : encoded.keys())
        if (key != "stitches") legacy.set(key, *encoded.find(key));
    legacy.set("version", 19);
    auto migrated = decodeVolumeFluid(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().stitches.empty());
    legacy.set("stitches", eve::Value(eve::Value::Array{}));
    REQUIRE(!decodeVolumeFluid(legacy).ok());
}

// fluids_volume_profiler.cpp
using namespace eve::fluids;

namespace {
bool hasFluidZone(std::string_view name) {
    const auto& samples = eve::prof::Profiler::lastFrame();
    return std::any_of(samples.begin(), samples.end(), [&](const auto& sample) {
        return sample.module == "fluids" && sample.name == name && sample.count == 1 && sample.totalMs >= 0.0;
    });
}
}  // namespace

TEST_CASE("fluids.volume.profilerReportsSolverAndSurfaceWork") {
    eve::prof::Profiler::reset();
    eve::prof::Profiler::setEnabled(true);

    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());

    FluidSurfaceParams surface;
    surface.width  = 16;
    surface.height = 16;
    FluidSurfaceRenderer renderer(surface, false);
    renderer.renderVolume(*solver);

    eve::prof::Profiler::frameMark();
    CHECK(hasFluidZone("VolumeFluid::step"));
    CHECK(hasFluidZone("FluidSurfaceRenderer::renderVolume"));
    CHECK(hasFluidZone("FluidSurfaceRenderer::reconstruct"));

    eve::prof::Profiler::setEnabled(false);
    eve::prof::Profiler::reset();
}
