#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

TEST_CASE("fluids.volume.heightFieldUsesLocalFluid3DTrianglesAndStableContacts") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();

    VolumeFluidHeightFieldCollider terrain;
    terrain.label      = 41;
    terrain.resolution = {2, 2};
    terrain.size       = {1.f, 1.f, 1.f};
    terrain.heights    = {0.f, .5f, 0.f, .5f};
    REQUIRE(solver->setHeightFieldColliders(std::span(&terrain, 1)).ok());

    VolumeFluidParticle particle;
    particle.position = {.5f, .1f, .5f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto particles = solver->particles();
    REQUIRE(particles[0].position.y > .25f);
    const auto contacts = solver->contacts();
    REQUIRE(contacts.size() == 1);
    REQUIRE(contacts[0].colliderLabel == 41);
    REQUIRE(contacts[0].normal.y > .8f);
    REQUIRE(contacts[0].normal.x < -.4f);
}

TEST_CASE("fluids.volume.heightFieldValidationIsAtomicAndCodecMigratesV14") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                           solver = std::move(created).takeValue();
    VolumeFluidHeightFieldCollider terrain;
    terrain.label   = 7;
    terrain.heights = {0.f, .1f, .2f, .3f};
    REQUIRE(solver->setHeightFieldColliders(std::span(&terrain, 1)).ok());
    const auto before = solver->snapshot();
    terrain.heights.pop_back();
    REQUIRE(!solver->setHeightFieldColliders(std::span(&terrain, 1)).ok());
    REQUIRE(solver->snapshot().heightFieldColliders[0].heights == before.heightFieldColliders[0].heights);

    auto decoded = decodeVolumeFluid(encodeVolumeFluid(before));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().version == 20);
    REQUIRE(decoded.value().heightFieldColliders[0].heights.size() == 4);

    auto       current = encodeVolumeFluid(before);
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "heightFieldColliders" && key != "version" && key != "stitches") legacy.set(key, *current.find(key));
    legacy.set("version", 14);
    auto migrated = decodeVolumeFluid(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().heightFieldColliders.empty());
}
