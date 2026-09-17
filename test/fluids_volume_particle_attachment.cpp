#include <glm/geometric.hpp>

#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
