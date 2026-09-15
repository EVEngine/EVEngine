#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

static std::unique_ptr<VolumeFluid> triggerSolver(VolumeFluidParticle particle) {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    return solver;
}

static void requireTriggerContact(VolumeFluid& solver, glm::vec3 original, unsigned label) {
    REQUIRE(solver.step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(solver.particles()[0].position - original) < 1e-6f);
    const auto contacts = solver.contacts();
    REQUIRE(contacts.size() == 1);
    REQUIRE(contacts[0].colliderLabel == label);
    REQUIRE(glm::length(contacts[0].impulse) == 0.f);
}

TEST_CASE("fluids.volume.colliderTriggersReportWithoutEnforcement") {
    VolumeFluidParticle particle;
    particle.position            = {.2f, .5f, 0.f};
    auto                analytic = triggerSolver(particle);
    VolumeFluidCollider sphere;
    sphere.label     = 10;
    sphere.center    = {0.f, .5f, 0.f};
    sphere.radius    = .5f;
    sphere.isTrigger = true;
    REQUIRE(analytic->setColliders(std::span(&sphere, 1)).ok());
    requireTriggerContact(*analytic, particle.position, sphere.label);

    auto                   sdfSolver = triggerSolver(particle);
    VolumeFluidSdfCollider sdf;
    sdf.label     = 11;
    sdf.sdf       = MeshSdf::makeSphere(glm::vec3(0.f), .5f, {16, 16, 16});
    sdf.position  = {0.f, .5f, 0.f};
    sdf.isTrigger = true;
    REQUIRE(sdfSolver->setSdfColliders(std::span(&sdf, 1)).ok());
    requireTriggerContact(*sdfSolver, particle.position, sdf.label);

    particle.position                            = {.5f, .02f, .5f};
    auto                           terrainSolver = triggerSolver(particle);
    VolumeFluidHeightFieldCollider terrain;
    terrain.label     = 12;
    terrain.heights   = {0.f, 0.f, 0.f, 0.f};
    terrain.isTrigger = true;
    REQUIRE(terrainSolver->setHeightFieldColliders(std::span(&terrain, 1)).ok());
    requireTriggerContact(*terrainSolver, particle.position, terrain.label);
}

TEST_CASE("fluids.volume.triggerValidationAndV15MigrationAreStrict") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider collider;
    collider.isTrigger = true;
    collider.solidify  = true;
    REQUIRE(!solver->setColliders(std::span(&collider, 1)).ok());

    VolumeFluidSnapshot snapshot;
    auto                current = encodeVolumeFluid(snapshot);
    eve::Value          legacy(eve::Value::Object{});
    for (const auto& key : current.keys()) {
        if (key == "version" || key == "stitches") continue;
        const auto* source = current.find(key);
        if (key != "colliders" && key != "sdfColliders" && key != "heightFieldColliders") {
            legacy.set(key, *source);
            continue;
        }
        eve::Value::Array oldItems;
        for (size_t i = 0; i < source->arraySize(); ++i) {
            eve::Value oldItem(eve::Value::Object{});
            for (const auto& itemKey : source->at(i).keys())
                if (itemKey != "isTrigger" && itemKey != "staticFriction" && itemKey != "rollingFriction" &&
                    itemKey != "stickiness" && itemKey != "stickDistance" && itemKey != "frictionCombine" &&
                    itemKey != "stickinessCombine" && itemKey != "rollingContacts")
                    oldItem.set(itemKey, *source->at(i).find(itemKey));
            oldItems.push_back(std::move(oldItem));
        }
        legacy.set(key, eve::Value(std::move(oldItems)));
    }
    legacy.set("version", 15);
    auto migrated = decodeVolumeFluid(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
}
