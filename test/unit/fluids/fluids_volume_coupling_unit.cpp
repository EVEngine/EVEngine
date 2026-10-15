#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCoupling.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>

using namespace eve::fluids;


TEST_CASE("fluids.volumeCoupling.failedStepPreservesLastAttachmentReaction") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, -10.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                fluid = std::move(created).takeValue();
    VolumeFluidSnapshot snapshot;
    snapshot.settings = settings;
    VolumeFluidParticle particle;
    particle.position       = {0.f, 1.f, 0.f};
    particle.material.phase = VolumeFluidPhase::Solid;
    snapshot.particles.push_back(particle);
    VolumeFluidCollider collider;
    collider.label    = 7;
    collider.solidify = true;
    snapshot.colliders.push_back(collider);
    snapshot.attachments.push_back({0, 7, {0.f, 0.f, 0.f}});
    REQUIRE(fluid->restore(snapshot).ok());
    REQUIRE(fluid->step(1.f / 120.f, 1).ok());
    const auto before = fluid->attachmentReactions().front().impulse;
    REQUIRE(!fluid->step(1.f, 1).ok());
    REQUIRE(fluid->attachmentReactions().size() == 1);
    REQUIRE(fluid->attachmentReactions().front().impulse == before);
    fluid->clear();
    REQUIRE(fluid->attachmentReactions().empty());
}
