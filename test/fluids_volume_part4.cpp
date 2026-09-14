#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
