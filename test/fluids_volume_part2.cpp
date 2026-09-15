#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

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

TEST_CASE("fluids.volume.multiphaseViscosityConservesMomentum") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position   = {-0.14f, 1.f, 0.f};
    particles[1].position   = {0.f, 1.f, 0.f};
    particles[2].position   = {0.1f, 1.f, 0.f};
    const float density[]   = {500.f, 2000.f, 1000.f};
    const float viscosity[] = {0.f, 30.f, 100.f};
    const float shear[]     = {0.04f, -0.02f, 0.02f};
    glm::vec3   initialMomentum(0.f);
    float       initialEnergy = 0.f;
    for (int i = 0; i < 3; ++i) {
        particles[i].material.density   = density[i];
        particles[i].material.viscosity = viscosity[i];
        particles[i].material.yield     = float(i) * 5.f;
        particles[i].material.cohesion  = 0.f;
        particles[i].velocity           = {0.f, shear[i], 0.1f};
        const float mass                = density[i] * 0.001f;
        initialMomentum += mass * particles[i].velocity;
        initialEnergy += 0.5f * mass * glm::dot(particles[i].velocity, particles[i].velocity);
    }
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    glm::vec3 momentum(0.f);
    float     energy = 0.f;
    for (const auto& particle : solver->particles()) {
        const float mass = particle.material.density * 0.001f;
        momentum += mass * particle.velocity;
        energy += 0.5f * mass * glm::dot(particle.velocity, particle.velocity);
    }
    REQUIRE(glm::length(momentum - initialMomentum) < 1e-5f);
    REQUIRE(energy < initialEnergy);
}

TEST_CASE("fluids.volume.orientedBoxQuery") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {0.2f, 1.f, 0.f};
    particles[1].position = {0.f, 1.2f, 0.f};
    particles[2].position = {0.25f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    auto aligned = solver->overlapBox({0.f, 1.f, 0.f}, {0.25f, 0.05f, 0.05f}, {0.f, 0.f, 0.f, 1.f});
    REQUIRE(aligned.ok());
    REQUIRE(aligned.value().size() == 2);
    REQUIRE(aligned.value()[1].position.x == 0.25f);
    auto rotated = solver->overlapBox({0.f, 1.f, 0.f}, {0.25f, 0.05f, 0.05f}, {0.f, 0.f, 0.70710678f, 0.70710678f});
    REQUIRE(rotated.ok());
    REQUIRE(rotated.value().size() == 1);
    REQUIRE(rotated.value()[0].position.y == 1.2f);
    auto point = solver->overlapBox(particles[0].position, glm::vec3(0.f), {0.f, 0.f, 0.f, 1.f});
    REQUIRE(point.ok());
    REQUIRE(point.value().size() == 1);
    REQUIRE(!solver->overlapBox({}, {-1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 1.f}).ok());
    REQUIRE(!solver->overlapBox({}, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 2.f}).ok());
    REQUIRE(
        !solver->overlapBox({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f, 1.f})
             .ok());
    REQUIRE(solver->particleCount() == 3);
}

TEST_CASE("fluids.volume.diffusionConservesConcentration") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-0.14f, 1.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[2].position = {0.1f, 1.f, 0.f};
    glm::vec4 initialColor(0.f), initialData(0.f);
    for (int i = 0; i < 3; ++i) {
        particles[i].material.diffusion = float(i) * 50.f;
        particles[i].material.cohesion  = 0.f;
        particles[i].color              = glm::vec4(0.f, 0.f, 0.f, 1.f);
        particles[i].color[i]           = 1.f;
        particles[i].data               = glm::vec4(float(i), float(i * i), -float(i), 0.5f);
        initialColor += particles[i].color;
        initialData += particles[i].data;
    }
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    glm::vec4 color(0.f), data(0.f);
    for (const auto& particle : solver->particles()) {
        color += particle.color;
        data += particle.data;
        REQUIRE(glm::all(glm::greaterThanEqual(particle.color, glm::vec4(0.f))));
        REQUIRE(glm::all(glm::lessThanEqual(particle.color, glm::vec4(1.f))));
    }
    REQUIRE(glm::length(color - initialColor) < 1e-6f);
    REQUIRE(glm::length(data - initialData) < 1e-6f);
    REQUIRE(solver->particles()[1].color.g < 1.f);
}

TEST_CASE("fluids.volume.materialChannelsMapAtomically") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-0.1f, 1.f, 0.f};
    particles[1].position = {0.1f, 1.f, 0.f};
    particles[0].data     = {30.f, 1.5f, -3.f, 7.f};
    particles[1].data     = {-1.f, 0.5f, 0.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(!solver->applyMaterialChannels().ok());
    REQUIRE(solver->particles()[0].material.viscosity == particles[0].material.viscosity);
    solver->clear();
    particles[1].data.x = 100.f;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->applyMaterialChannels().ok());
    const auto state = solver->particles();
    REQUIRE(state[0].material.viscosity == 30.f);
    REQUIRE(state[0].material.cohesion == 1.5f);
    REQUIRE(state[1].material.viscosity == 100.f);
    REQUIRE(state[1].material.cohesion == 0.5f);
    REQUIRE(state[0].data == particles[0].data);
    REQUIRE(state[0].position == particles[0].position);
}

TEST_CASE("fluids.volume.thermalContactIsBoundedAndIterationIndependent") {
    const auto run = [&](unsigned iterations, float rate) {
        VolumeFluidSettings settings;
        settings.iterations = iterations;
        auto result         = VolumeFluid::create(settings);
        REQUIRE(result.ok());
        auto                solver = std::move(result).takeValue();
        VolumeFluidCollider collider;
        collider.label = 17;
        REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
        VolumeFluidParticle particles[2];
        particles[0].position = {0.f, 1.25f, 0.f};
        particles[1].position = {0.8f, 1.25f, 0.f};
        particles[0].data = particles[1].data = {5.f, 1.f, 7.f, -2.f};
        REQUIRE(solver->emit(particles).ok());
        VolumeFluidThermalRule rule;
        rule.colliderLabel = 17;
        rule.rate          = rate;
        REQUIRE(solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
        REQUIRE(!solver->contacts().empty());
        const auto state = solver->particles();
        REQUIRE(state[1].data == particles[1].data);
        REQUIRE(state[0].data.z == 7.f);
        REQUIRE(state[0].data.w == -2.f);
        REQUIRE(state[0].material.viscosity == particles[0].material.viscosity);
        return state[0].data;
    };
    const auto hot = run(1, -6.f);
    REQUIRE(glm::length(hot - glm::vec4(4.9f, 0.9f, 7.f, -2.f)) < 1e-6f);
    REQUIRE(glm::length(run(10, -6.f) - hot) < 1e-6f);
    REQUIRE(glm::length(run(5, 6.f) - glm::vec4(5.1f, 1.1f, 7.f, -2.f)) < 1e-6f);
    REQUIRE(glm::length(run(5, -1000000.f) - glm::vec4(0.05f, 0.5f, 7.f, -2.f)) < 1e-6f);
    REQUIRE(glm::length(run(5, 1000000.f) - glm::vec4(10.f, 2.f, 7.f, -2.f)) < 1e-6f);
}

TEST_CASE("fluids.volume.thermalRuleRejectsBeforeStepping") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidThermalRule rule;
    rule.colliderLabel = 42;
    REQUIRE(!solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
    REQUIRE(solver->particles()[0].position == particle.position);
    VolumeFluidCollider collider;
    collider.label = 42;
    REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
    rule.minimumViscosity = 20.f;
    REQUIRE(!solver->stepWithThermalContacts(1.f / 60.f, 4, std::span(&rule, 1)).ok());
    REQUIRE(solver->particles()[0].position == particle.position);
    rule.minimumViscosity                     = 0.05f;
    const VolumeFluidThermalRule duplicates[] = {rule, rule};
    REQUIRE(!solver->stepWithThermalContacts(1.f / 60.f, 4, duplicates).ok());
    REQUIRE(solver->particles()[0].position == particle.position);
}

TEST_CASE("fluids.volume.snapshotStrictAndAtomic") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle p;
    p.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    auto original = solver->snapshot();
    auto encoded  = encodeVolumeFluid(original);
    auto json     = encoded.toJson();
    REQUIRE(json.ok());
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    auto decoded = decodeVolumeFluid(parsed.value());
    REQUIRE(decoded.ok());
    solver->clear();
    REQUIRE(solver->restore(decoded.value()).ok());
    REQUIRE(solver->particles().size() == 1);
    auto bad    = original;
    bad.version = 99;
    REQUIRE(!solver->restore(bad).ok());
    REQUIRE(solver->particles()[0].position.y == 1.f);
    encoded.set("unexpected", 3);
    REQUIRE(!decodeVolumeFluid(encoded).ok());
    bad                               = original;
    bad.particles[0].material.density = -1.f;
    REQUIRE(!solver->restore(bad).ok());
    REQUIRE(solver->particles()[0].position.y == 1.f);
}

TEST_CASE("fluids.volume.densityLayerInversion") {
    VolumeFluidSettings settings;
    settings.minimum = {-0.3f, 0.f, -0.3f};
    settings.maximum = {0.3f, 1.4f, 0.3f};
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> batch;
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 5; ++x) {
                VolumeFluidParticle p;
                // A resolved perturbation seeds the unstable interface; an exactly flat
                // interface is an equilibrium even though it is physically unstable.
                p.position = {-0.2f + x * 0.1f, 0.071f + y * 0.1f + 0.015f * std::sin(float(x + z)), -0.2f + z * 0.1f};
                p.position.x += 0.015f * std::sin(float(y + z));
                p.position.z += 0.015f * std::cos(float(x + y));
                p.material.density   = y < 5 ? 700.f : 1300.f;
                p.material.cohesion  = 0.f;
                p.material.viscosity = 0.05f;
                batch.push_back(p);
            }
    REQUIRE(solver->emit(batch).ok());
    for (int i = 0; i < 360; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    float light = 0.f, heavy = 0.f;
    for (const auto& p : solver->particles()) {
        if (p.material.density < 1000.f)
            light += p.position.y;
        else
            heavy += p.position.y;
    }
    REQUIRE(heavy < light);
}

TEST_CASE("fluids.volume.coincidentLiquidSamplesSeparate") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle p;
    p.position                       = {0, 1, 0};
    p.material.cohesion              = 0;
    p.material.viscosity             = 0;
    const VolumeFluidParticle pair[] = {p, p};
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto particles = solver->particles();
    REQUIRE(glm::length(particles[0].position - particles[1].position) >= .04999f);
    REQUIRE(glm::length((particles[0].position + particles[1].position) * .5f - p.position) < 1e-6f);
}

TEST_CASE("fluids.volume.unequalMassSeparationPreservesCenter") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle a, b;
    a.position = b.position = {0, 1, 0};
    a.material.density      = 700.f;
    b.material.density      = 1300.f;
    a.material.cohesion = b.material.cohesion = 0;
    a.material.viscosity = b.material.viscosity = 0;
    const VolumeFluidParticle pair[]            = {a, b};
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto p      = solver->particles();
    const auto center = (p[0].position * 700.f + p[1].position * 1300.f) / 2000.f;
    REQUIRE(glm::length(center - a.position) < 1e-6f);
    REQUIRE(glm::length(p[0].position - p[1].position) >= .0499f);
}

TEST_CASE("fluids.volume.stableDensityLayersRemainOrdered") {
    VolumeFluidSettings settings;
    settings.minimum = {-.3f, 0, -.3f};
    settings.maximum = {.3f, 1.4f, .3f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> batch;
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 5; ++x) {
                VolumeFluidParticle p;
                p.position           = {-.2f + x * .1f + .015f * std::sin(float(y + z)),
                                        .071f + y * .1f + .015f * std::sin(float(x + z)),
                                        -.2f + z * .1f + .015f * std::cos(float(x + y))};
                p.material.density   = y < 5 ? 1300.f : 700.f;
                p.material.cohesion  = 0;
                p.material.viscosity = .05f;
                batch.push_back(p);
            }
    REQUIRE(solver->emit(batch).ok());
    for (int i = 0; i < 360; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    float heavy = 0, light = 0;
    for (const auto& p : solver->particles()) (p.material.density > 1000 ? heavy : light) += p.position.y;
    REQUIRE(heavy < light);
}

TEST_CASE("fluids.volume.rotatedBoxAndAngularSurfaceVelocity") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider box;
    box.shape      = VolumeFluidColliderShape::Box;
    box.center     = {0, 1, 0};
    box.halfExtent = {.4f, .05f, .1f};
    box.rotation   = {0, 0, .70710678118f, .70710678118f};
    box.friction   = 0;
    REQUIRE(solver->setColliders(std::span(&box, 1)).ok());
    VolumeFluidParticle p;
    p.position           = {.06f, 1.2f, 0};
    p.material.cohesion  = 0;
    p.material.viscosity = 0;
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.x >= .09999f);
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].normal.x > .99f);
    solver->clear();
    box.shape           = VolumeFluidColliderShape::Sphere;
    box.radius          = .2f;
    box.angularVelocity = {0, 0, 4};
    box.friction        = 1;
    REQUIRE(solver->setColliders(std::span(&box, 1)).ok());
    p.position = {.245f, 1, 0};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.y > 0.f);
    float oppositeImpulse = 0;
    for (const auto& contact : solver->contacts()) oppositeImpulse += contact.impulse.y;
    REQUIRE(oppositeImpulse < 0.f);
}

TEST_CASE("fluids.volume.colliderPoseRejectsAtomically") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider box;
    box.label = 17;
    REQUIRE(solver->setColliders(std::span(&box, 1)).ok());
    box.rotation = {0, 0, 0, 0};
    REQUIRE(!solver->setColliders(std::span(&box, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].rotation.w == 1.f);
}

TEST_CASE("fluids.volume.colliderVersionOneMigration") {
    VolumeFluidSnapshot state;
    state.colliders.push_back(VolumeFluidCollider{});
    auto        encoded = encodeVolumeFluid(state);
    const auto& current = encoded.find("colliders")->at(0);
    eve::Value  oldCollider(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "rotation" && key != "angularVelocity" && key != "collisionFilter" && key != "isTrigger" &&
            key != "staticFriction" && key != "rollingFriction" && key != "stickiness" && key != "stickDistance" &&
            key != "frictionCombine" && key != "stickinessCombine" && key != "rollingContacts")
            oldCollider.set(key, *current.find(key));
    eve::Value::Array colliders;
    colliders.push_back(oldCollider);
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : encoded.keys())
        if (key != "attachments" && key != "simplexes") legacy.set(key, *encoded.find(key));
    encoded = std::move(legacy);
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 1);
    encoded.set("colliders", eve::Value(std::move(colliders)));
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().attachments.empty());
    REQUIRE(migrated.value().colliders[0].rotation.w == 1.f);
    REQUIRE(glm::length(migrated.value().colliders[0].angularVelocity) == 0.f);
}

TEST_CASE("fluids.volume.failedCoupledStepRestoresSamples") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider original;
    original.label = 7;
    REQUIRE(solver->setColliders(std::span(&original, 1)).ok());
    VolumeFluidCollider replacement;
    replacement.label = 9;
    REQUIRE(!solver->stepWithColliders(-1.f, 4, std::span(&replacement, 1)).ok());
    REQUIRE(solver->snapshot().colliders.size() == 1);
    REQUIRE(solver->snapshot().colliders[0].label == 7);
}

TEST_CASE("fluids.volume.thermalCoupledStepRestoresSamples") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider original;
    original.label = 7;
    REQUIRE(solver->setColliders(std::span(&original, 1)).ok());
    VolumeFluidParticle particle;
    particle.position = {0, 1.25f, 0};
    particle.data     = {5, 1, 7, -2};
    particle.life     = 1;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider replacement;
    replacement.label = 9;
    VolumeFluidThermalRule rule;
    rule.colliderLabel = 7;
    rule.rate          = -6;
    REQUIRE(!solver->stepWithColliders(1.f / 60.f, 4, std::span(&replacement, 1), std::span(&rule, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].label == 7);
    REQUIRE(solver->particles()[0].position.y == particle.position.y);
    REQUIRE(solver->particles()[0].data.x == 5.f);
    REQUIRE(solver->particles()[0].life == 1.f);
    rule.colliderLabel = 9;
    REQUIRE(solver->stepWithColliders(1.f / 60.f, 4, std::span(&replacement, 1), std::span(&rule, 1)).ok());
    REQUIRE(std::abs(solver->particles()[0].data.x - 4.9f) < 1e-5f);
    REQUIRE(std::abs(solver->particles()[0].life - (1.f - 1.f / 60.f)) < 1e-6f);
    REQUIRE(solver->snapshot().colliders[0].label == 9);
}

TEST_CASE("fluids.volume.viscosityGradientOrderAndAtomicity") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(5);
    const float                      viscosity[] = {0, 2, 4, 6, 100};
    for (size_t i = 0; i < particles.size(); ++i) {
        particles[i].position           = {0, 1, 0};
        particles[i].material.viscosity = viscosity[i];
        particles[i].data               = {8, 1, 7, -2};
    }
    REQUIRE(solver->emit(particles).ok());
    const VolumeFluidViscosityColorKey keys[] = {{2, {1, 0, 0, .2f}}, {6, {0, 0, 1, .8f}}, {10, {0, 1, 0, 1}}};
    REQUIRE(solver->applyMaterialChannels(keys).ok());
    auto mapped = solver->particles();
    REQUIRE(mapped[0].color.x == 1.f);
    REQUIRE(mapped[1].color.x == 1.f);
    REQUIRE(std::abs(mapped[2].color.x - .5f) < 1e-6f);
    REQUIRE(std::abs(mapped[2].color.w - .5f) < 1e-6f);
    REQUIRE(mapped[3].color.z == 1.f);
    REQUIRE(mapped[4].color.y == 1.f);
    for (const auto& p : mapped) {
        REQUIRE(p.material.viscosity == 8.f);
        REQUIRE(p.data.z == 7.f);
    }
    auto invalidKeys         = std::vector<VolumeFluidViscosityColorKey>(std::begin(keys), std::end(keys));
    invalidKeys[1].viscosity = 2;
    REQUIRE(!solver->applyMaterialChannels(invalidKeys).ok());
    REQUIRE(solver->particles()[0].color.x == 1.f);
    invalidKeys[1]         = keys[1];
    invalidKeys[2].color.w = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!solver->applyMaterialChannels(invalidKeys).ok());
    REQUIRE(solver->particles()[0].color.x == 1.f);
    auto snapshot                    = solver->snapshot();
    snapshot.particles.back().data.y = -1;
    REQUIRE(solver->restore(snapshot).ok());
    REQUIRE(!solver->applyMaterialChannels(keys).ok());
    REQUIRE(solver->particles()[0].color.x == 1.f);
    REQUIRE(solver->particles()[0].material.viscosity == 8.f);
}

TEST_CASE("fluids.volume.viscosityGradientStrictCodec") {
    eve::Value key(eve::Value::Object{});
    key.set("viscosity", 2.f);
    key.set("color", eve::Value(eve::Value::Array{1.f, 0.f, 0.f, 1.f}));
    auto value = eve::Value(eve::Value::Array{key, key});
    auto valid = decodeVolumeFluidViscosityColors(value);
    REQUIRE(valid.ok());
    REQUIRE(valid.value().size() == 2);
    key.set("extra", 1);
    REQUIRE(!decodeVolumeFluidViscosityColors(eve::Value(eve::Value::Array{key})).ok());
    REQUIRE(!decodeVolumeFluidViscosityColors(eve::Value(eve::Value::Array(33, key))).ok());
}

TEST_CASE("fluids.volume.solidAttachmentMovesRotatesAndDetaches") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle p;
    p.position = {0, 1.25f, 0};
    VolumeFluidCollider c;
    c.label    = 17;
    c.solidify = true;
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    auto frozen = solver->snapshot();
    REQUIRE(frozen.attachments.size() == 1);
    REQUIRE(frozen.attachments[0].particleIndex == 0);
    REQUIRE(frozen.attachments[0].colliderLabel == 17);
    c.center.x = .2f;
    c.rotation = {0, 0, .70710678118f, .70710678118f};
    REQUIRE(solver->stepWithColliders(1.f / 60.f, 4, std::span(&c, 1)).ok());
    const auto      local    = frozen.attachments[0].localPosition;
    const glm::vec3 expected = c.center + glm::vec3(-local.y, local.x, local.z);
    REQUIRE(glm::length(solver->particles()[0].position - expected) < 1e-5f);
    REQUIRE(glm::length(solver->particles()[0].velocity - (expected - frozen.particles[0].position) * 60.f) < 1e-4f);
    REQUIRE(std::abs(solver->particles()[0].orientation.z - .70710678f) < 1e-5f);
    REQUIRE(std::abs(solver->particles()[0].orientation.w - .70710678f) < 1e-5f);
    auto decoded = decodeVolumeFluid(encodeVolumeFluid(solver->snapshot()));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().attachments.size() == 1);
    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->snapshot().attachments.empty());
    c.center.x = .4f;
    REQUIRE(solver->stepWithColliders(1.f / 60.f, 4, std::span(&c, 1)).ok());
    REQUIRE(glm::length(solver->particles()[0].position - expected) < 1e-5f);
    REQUIRE(glm::length(solver->particles()[0].velocity) == 0.f);
    REQUIRE(solver->restore(decoded.value()).ok());
    c.solidify = false;
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->paint(expected, .001f, VolumeFluidMaterial{}).ok());
    REQUIRE(solver->snapshot().attachments.empty());
}

TEST_CASE("fluids.volume.addRandomVelocityUsesOneDeterministicActorImpulse") {
    auto makeSolver = [&] {
        auto created = VolumeFluid::create({});
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position = {-.2f, 1.f, 0.f};
        particles[0].velocity = {1.f, 2.f, 3.f};
        particles[1].position = {.2f, 1.f, 0.f};
        particles[1].velocity = {-2.f, 1.f, .5f};
        REQUIRE(solver->emit(particles).ok());
        return solver;
    };
    auto       first = makeSolver(), replay = makeSolver();
    const auto before = first->particles();
    REQUIRE(first->addRandomVelocity(5.f, 73).ok());
    REQUIRE(replay->addRandomVelocity(5.f, 73).ok());
    const auto      after  = first->particles();
    const glm::vec3 delta0 = after[0].velocity - before[0].velocity;
    const glm::vec3 delta1 = after[1].velocity - before[1].velocity;
    CHECK(glm::length(delta0 - delta1) < 1e-6f);
    CHECK(std::abs(glm::length(delta0) - 5.f) < 1e-5f);
    CHECK(after[0].velocity == replay->particles()[0].velocity);
    const auto stable = first->snapshot();
    REQUIRE(!first->addRandomVelocity(-1.f, 73).ok());
    CHECK(first->snapshot().particles[0].velocity == stable.particles[0].velocity);

    auto overflow               = makeSolver();
    auto state                  = overflow->snapshot();
    state.particles[1].velocity = glm::normalize(delta0) * 99.f;
    REQUIRE(overflow->restore(state).ok());
    REQUIRE(!overflow->addRandomVelocity(2.f, 73).ok());
    CHECK(overflow->particles()[0].velocity == state.particles[0].velocity);
    CHECK(overflow->particles()[1].velocity == state.particles[1].velocity);
}

TEST_CASE("fluids.volume.teleportActorTransformsPoseZerosMotionAndRejectsAtomically") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position        = {-.2f, 1.f, 0.f};
    particles[0].velocity        = {1.f, 2.f, 3.f};
    particles[0].angularVelocity = {.5f, 0.f, 0.f};
    particles[1].position        = {.2f, 1.f, 0.f};
    particles[1].velocity        = {-1.f, 0.f, 2.f};
    REQUIRE(solver->emit(particles).ok());
    const float half = std::sqrt(.5f);
    REQUIRE(solver->teleportActor({0.f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f}, {0.f, 0.f, half, half}).ok());
    const auto transformed = solver->particles();
    CHECK(glm::length(transformed[0].position - glm::vec3(1.f, .8f, 0.f)) < 1e-5f);
    CHECK(glm::length(transformed[1].position - glm::vec3(1.f, 1.2f, 0.f)) < 1e-5f);
    CHECK(glm::length(transformed[0].velocity) == 0.f);
    CHECK(glm::length(transformed[0].angularVelocity) == 0.f);
    CHECK(std::abs(transformed[0].orientation.z - half) < 1e-5f);
    CHECK(std::abs(transformed[0].orientation.w - half) < 1e-5f);

    const auto stable = solver->snapshot();
    REQUIRE(
        !solver->teleportActor({1.f, 1.f, 0.f}, {0.f, 0.f, half, half}, {3.f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}).ok());
    CHECK(solver->snapshot().particles[0].position == stable.particles[0].position);
    REQUIRE(!solver->teleportActor({}, {0.f, 0.f, 0.f, 0.f}, {}, {0.f, 0.f, 0.f, 1.f}).ok());
    CHECK(solver->snapshot().particles[1].position == stable.particles[1].position);
}

TEST_CASE("fluids.volume.referenceVelocityAndActorGroupColorizers") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position   = {0, 1, 0};
    particles[0].velocity   = {-.2f, 0, .2f};
    particles[0].actorGroup = 0;
    particles[1].position   = {.2f, 1, 0};
    particles[1].velocity   = {-.4f, .4f, 0};
    particles[1].actorGroup = 1;
    particles[2].position   = {.4f, 1, 0};
    particles[2].velocity   = {0, 0, 0};
    particles[2].actorGroup = 26;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->applyVelocityColors(.2f).ok());
    const auto velocityColors = solver->particles();
    REQUIRE(glm::length(velocityColors[0].color - glm::vec4(0, .5f, 1, 1)) < 1e-6f);
    REQUIRE(glm::length(velocityColors[1].color - glm::vec4(0, 1, .5f, 1)) < 1e-6f);
    REQUIRE(!solver->applyVelocityColors(0).ok());
    REQUIRE(solver->particles()[0].color == velocityColors[0].color);
    REQUIRE(solver->applyActorGroupColors().ok());
    const auto groupColors = solver->particles();
    REQUIRE(glm::length(groupColors[0].color - glm::vec4(240 / 255.f, 163 / 255.f, 1, 1)) < 1e-6f);
    REQUIRE(glm::length(groupColors[1].color - glm::vec4(0, 117 / 255.f, 220 / 255.f, 1)) < 1e-6f);
    REQUIRE(groupColors[2].color == groupColors[0].color);
    REQUIRE(groupColors[0].velocity == particles[0].velocity);
}

TEST_CASE("fluids.volume.referenceDataAndSeededRandomColorizers") {
    auto ar = VolumeFluid::create({}), br = VolumeFluid::create({});
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidParticle particles[3];
    for (int i = 0; i < 3; ++i) {
        particles[i].position = {float(i) * .2f, 1, 0};
        particles[i].data.x   = float(i) * .5f;
    }
    REQUIRE(a->emit(particles).ok());
    REQUIRE(b->emit(particles).ok());
    const VolumeFluidColorKey gradient[] = {{0, {1, 0, 0, 1}}, {1, {0, 0, 1, 1}}};
    REQUIRE(a->applyDataColors(0, gradient).ok());
    auto colors = a->particles();
    REQUIRE(colors[0].color == glm::vec4(1, 0, 0, 1));
    REQUIRE(colors[1].color == glm::vec4(.5f, 0, .5f, 1));
    REQUIRE(colors[2].color == glm::vec4(0, 0, 1, 1));
    auto invalidGradient = std::array{gradient[1], gradient[0]};
    REQUIRE(!a->applyDataColors(0, invalidGradient).ok());
    REQUIRE(a->particles()[1].color == colors[1].color);
    REQUIRE(!a->applyDataColors(4, gradient).ok());
    REQUIRE(a->applyRandomColors(gradient, 91).ok());
    REQUIRE(b->applyRandomColors(gradient, 91).ok());
    for (size_t i = 0; i < 3; ++i) REQUIRE(a->particles()[i].color == b->particles()[i].color);
}

TEST_CASE("fluids.volume.rotatedCapsuleAndAngularSurfaceVelocity") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    settings.minimum = {-1, -1, -1};
    settings.maximum = {1, 1, 1};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider capsule;
    capsule.label           = 81;
    capsule.shape           = VolumeFluidColliderShape::Capsule;
    capsule.center          = {0, 0, 0};
    capsule.radius          = .1f;
    capsule.halfExtent      = {.1f, .3f, .1f};
    capsule.rotation        = {0, 0, .70710678118f, .70710678118f};
    capsule.angularVelocity = {0, 0, 4};
    capsule.friction        = 1;
    REQUIRE(solver->setColliders(std::span(&capsule, 1)).ok());
    VolumeFluidParticle particle;
    particle.position           = {.15f, .11f, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].colliderLabel == 81);
    REQUIRE(solver->contacts()[0].normal.y > .99f);
    REQUIRE(solver->particles()[0].position.y >= .1499f);
    REQUIRE(solver->particles()[0].velocity.y > 0.f);
    auto decoded = decodeVolumeFluid(encodeVolumeFluid(solver->snapshot()));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().colliders[0].shape == VolumeFluidColliderShape::Capsule);
    auto invalid  = decoded.value().colliders[0];
    invalid.shape = static_cast<VolumeFluidColliderShape>(99);
    REQUIRE(!solver->setColliders(std::span(&invalid, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].shape == VolumeFluidColliderShape::Capsule);
}

TEST_CASE("fluids.volume.movingColliderPosesAreInterpolatedAcrossSubsteps") {
    VolumeFluidSettings settings;
    settings.gravity   = glm::vec3(0.f);
    settings.minimum   = {-1, -1, -1};
    settings.maximum   = {1, 1, 1};
    auto sphereCreated = VolumeFluid::create(settings);
    REQUIRE(sphereCreated.ok());
    auto                sphereSolver = std::move(sphereCreated).takeValue();
    VolumeFluidParticle particle;
    particle.position = glm::vec3(0.f);
    REQUIRE(sphereSolver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider sphere;
    sphere.label    = 71;
    sphere.shape    = VolumeFluidColliderShape::Sphere;
    sphere.radius   = .05f;
    sphere.center   = {.16f, 0, 0};
    sphere.velocity = {9.6f, 0, 0};
    REQUIRE(sphereSolver->setColliders(std::span(&sphere, 1)).ok());
    REQUIRE(sphereSolver->step(1.f / 60.f, 2).ok());
    REQUIRE(!sphereSolver->contacts().empty());
    REQUIRE(sphereSolver->contacts().front().colliderLabel == 71);

    auto boxCreated = VolumeFluid::create(settings);
    REQUIRE(boxCreated.ok());
    auto boxSolver    = std::move(boxCreated).takeValue();
    particle.position = {.1f, .1f, 0};
    REQUIRE(boxSolver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider box;
    box.label             = 72;
    box.shape             = VolumeFluidColliderShape::Box;
    box.center            = glm::vec3(0.f);
    box.halfExtent        = {.2f, .02f, .1f};
    const float halfAngle = .25f * 3.14159265359f;
    box.rotation          = {0, 0, std::sin(halfAngle), std::cos(halfAngle)};
    box.angularVelocity   = {0, 0, .5f * 3.14159265359f / (1.f / 60.f)};
    REQUIRE(boxSolver->setColliders(std::span(&box, 1)).ok());
    REQUIRE(boxSolver->step(1.f / 60.f, 2).ok());
    REQUIRE(!boxSolver->contacts().empty());
    REQUIRE(boxSolver->contacts().front().colliderLabel == 72);

    auto sdfCreated = VolumeFluid::create(settings);
    REQUIRE(sdfCreated.ok());
    auto sdfSolver    = std::move(sdfCreated).takeValue();
    particle.position = glm::vec3(0.f);
    REQUIRE(sdfSolver->emit(std::span(&particle, 1)).ok());
    VolumeFluidSdfCollider sdf;
    sdf.label    = 73;
    sdf.sdf      = MeshSdf::makeSphere(glm::vec3(0.f), .1f, {16, 16, 16});
    sdf.position = {.16f, 0, 0};
    sdf.velocity = {9.6f, 0, 0};
    sdf.friction = 0.f;
    REQUIRE(sdfSolver->setSdfColliders(std::span(&sdf, 1)).ok());
    const auto samples = sdfSolver->snapshot().sdfColliders[0].sdf.distances;
    REQUIRE(sdfSolver->step(1.f / 60.f, 2).ok());
    REQUIRE(!sdfSolver->contacts().empty());
    REQUIRE(sdfSolver->contacts().front().colliderLabel == 73);
    const auto after = sdfSolver->snapshot().sdfColliders[0];
    REQUIRE(after.position == sdf.position);
    REQUIRE(after.sdf.distances == samples);
}

TEST_CASE("fluids.volume.analyticColliderCandidateBudgetIsAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidCollider> colliders(1024);
    for (size_t i = 0; i < colliders.size(); ++i) colliders[i].label = unsigned(i + 1);
    REQUIRE(solver->setColliders(colliders).ok());
    std::vector<VolumeFluidParticle> particles(3907);
    for (auto& particle : particles) particle.position = {0, 1, 0};
    REQUIRE(!solver->emit(particles).ok());
    REQUIRE(solver->particleCount() == 0);
}

TEST_CASE("fluids.volume.attachedSolidReturnsNeighborConstraintMomentum") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 4;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle solid, liquid;
    solid.position          = {0, 1, 0};
    liquid.position         = {.08f, 1, 0};
    solid.material.phase    = VolumeFluidPhase::Solid;
    solid.material.cohesion = liquid.material.cohesion = 0.f;
    solid.material.viscosity = liquid.material.viscosity = 0.f;
    snapshot.particles                                   = {solid, liquid};
    VolumeFluidCollider collider;
    collider.label  = 17;
    collider.radius = .01f;
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 17, {0, 0, 0}});
    REQUIRE(solver->restore(snapshot).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->contacts().empty());
    REQUIRE(solver->attachmentReactions().size() == 1);
    const auto  state    = solver->particles();
    const float mass     = settings.spacing * settings.spacing * settings.spacing * 1000.f;
    const auto  momentum = state[1].velocity * mass + solver->attachmentReactions()[0].impulse;
    REQUIRE(glm::length(momentum) < 1e-5f);
    REQUIRE(solver->attachmentReactions()[0].impulse.x < 0.f);
}

TEST_CASE("fluids.volume.attachedEllipsoidReturnsAngularInertia") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle solid;
    solid.position       = {0, 1, 0};
    solid.material.phase = VolumeFluidPhase::Solid;
    solid.radii          = {.1f, .2f, .3f};
    snapshot.particles.push_back(solid);
    VolumeFluidCollider collider;
    collider.label           = 17;
    collider.radius          = .01f;
    collider.angularVelocity = {0, 0, 2};
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 17, {0, 0, 0}});
    REQUIRE(solver->restore(snapshot).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->attachmentReactions().size() == 1);
    const auto& reaction = solver->attachmentReactions()[0];
    REQUIRE(glm::length(reaction.impulse) < 1e-7f);
    REQUIRE(std::abs(reaction.angularImpulse.z + .02f) < 1e-6f);
    REQUIRE(std::abs(solver->particles()[0].angularVelocity.z - 2.f) < 1e-6f);
}
