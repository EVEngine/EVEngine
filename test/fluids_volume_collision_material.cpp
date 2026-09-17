#include <glm/geometric.hpp>

#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

namespace {
std::unique_ptr<VolumeFluid> makeContactSolver(VolumeFluidParticle particle) {
    VolumeFluidSettings settings;
    settings.gravity    = {0.f, 0.f, 0.f};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    return solver;
}

eve::Value withoutV17MaterialFields(const eve::Value& current) {
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : current.keys()) {
        if (key == "version" || key == "stitches") continue;
        const auto* source = current.find(key);
        if (key == "particles") {
            eve::Value::Array particles;
            for (size_t i = 0; i < source->arraySize(); ++i) {
                auto        particle       = source->at(i);
                const auto* materialSource = particle.find("material");
                eve::Value  material(eve::Value::Object{});
                for (const auto& field : materialSource->keys())
                    if (field != "stickiness" && field != "stickDistance" && field != "frictionCombine" &&
                        field != "stickinessCombine")
                        material.set(field, *materialSource->find(field));
                particle.set("material", std::move(material));
                particles.push_back(std::move(particle));
            }
            legacy.set(key, eve::Value(std::move(particles)));
            continue;
        }
        if (key == "colliders" || key == "sdfColliders" || key == "heightFieldColliders") {
            eve::Value::Array colliders;
            for (size_t i = 0; i < source->arraySize(); ++i) {
                eve::Value collider(eve::Value::Object{});
                for (const auto& field : source->at(i).keys())
                    if (field != "staticFriction" && field != "rollingFriction" && field != "stickiness" &&
                        field != "stickDistance" && field != "frictionCombine" && field != "stickinessCombine" &&
                        field != "rollingContacts")
                        collider.set(field, *source->at(i).find(field));
                colliders.push_back(std::move(collider));
            }
            legacy.set(key, eve::Value(std::move(colliders)));
            continue;
        }
        legacy.set(key, *source);
    }
    legacy.set("version", 16);
    return legacy;
}
}  // namespace

TEST_CASE("fluids.volume.collisionMaterialAdhesionUsesFluid3DCombinePriority") {
    VolumeFluidParticle particle;
    particle.position                   = {.56f, 0.f, 0.f};
    particle.material.stickiness        = 0.f;
    particle.material.stickDistance     = .1f;
    particle.material.stickinessCombine = VolumeFluidMaterialCombineMode::Minimum;

    VolumeFluidCollider sphere;
    sphere.center            = {0.f, 0.f, 0.f};
    sphere.radius            = .5f;
    sphere.stickiness        = 10.f;
    sphere.stickDistance     = .1f;
    sphere.stickinessCombine = VolumeFluidMaterialCombineMode::Minimum;

    auto minimum = makeContactSolver(particle);
    REQUIRE(minimum->setColliders(std::span(&sphere, 1)).ok());
    REQUIRE(minimum->step(.01f, 1).ok());
    REQUIRE(std::abs(minimum->particles()[0].position.x - .56f) < 1e-6f);

    sphere.stickinessCombine = VolumeFluidMaterialCombineMode::Maximum;
    auto maximum             = makeContactSolver(particle);
    REQUIRE(maximum->setColliders(std::span(&sphere, 1)).ok());
    REQUIRE(maximum->step(.01f, 1).ok());
    REQUIRE(maximum->particles()[0].position.x < .56f);
    REQUIRE(maximum->particles()[0].position.x >= .46f - 1e-6f);
    REQUIRE(maximum->contacts().size() == 1);
    REQUIRE(maximum->contacts()[0].impulse.x > 0.f);
}

TEST_CASE("fluids.volume.collisionMaterialValidationIsAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider valid;
    valid.label = 7;
    REQUIRE(solver->setColliders(std::span(&valid, 1)).ok());

    auto invalid            = valid;
    invalid.label           = 8;
    invalid.frictionCombine = static_cast<VolumeFluidMaterialCombineMode>(99);
    REQUIRE(!solver->setColliders(std::span(&invalid, 1)).ok());
    REQUIRE(solver->snapshot().colliders.size() == 1);
    REQUIRE(solver->snapshot().colliders[0].label == 7);

    VolumeFluidParticle particle;
    particle.material.stickDistance = 10.01f;
    REQUIRE(!solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->particleCount() == 0);
}

TEST_CASE("fluids.volume.collisionMaterialV16MigrationIsStrict") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidCollider analytic;
    analytic.label = 1;
    REQUIRE(solver->setColliders(std::span(&analytic, 1)).ok());
    VolumeFluidSdfCollider sdf;
    sdf.label = 2;
    sdf.sdf   = MeshSdf::makeSphere(glm::vec3(0.f), .5f, {8, 8, 8});
    REQUIRE(solver->setSdfColliders(std::span(&sdf, 1)).ok());
    VolumeFluidHeightFieldCollider terrain;
    terrain.label   = 3;
    terrain.heights = {0.f, 0.f, 0.f, 0.f};
    REQUIRE(solver->setHeightFieldColliders(std::span(&terrain, 1)).ok());

    auto legacy   = withoutV17MaterialFields(encodeVolumeFluid(solver->snapshot()));
    auto migrated = decodeVolumeFluid(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().particles[0].material.stickiness == 0.f);
    REQUIRE(migrated.value().colliders[0].staticFriction == migrated.value().colliders[0].friction);
    REQUIRE(!migrated.value().sdfColliders[0].rollingContacts);
    REQUIRE(migrated.value().heightFieldColliders[0].stickDistance == 0.f);

    legacy.find("particles")->at(0).find("material")->set("stickiness", 1.f);
    REQUIRE(!decodeVolumeFluid(legacy).ok());
}
