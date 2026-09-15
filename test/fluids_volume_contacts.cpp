#include <limits>
#include "fluids/VolumeFluid.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids;

TEST_CASE("fluids.volume.contacts.enterStayExitDeduplicatedAndAtomic") {
    VolumeFluidContactTracker tracker;
    VolumeFluidContact        first;
    first.colliderLabel                = 4;
    first.actorGroup                   = 2;
    first.particleIndex                = 7;
    first.distance                     = .005f;
    auto duplicate                     = first;
    duplicate.particleIndex            = 8;
    auto second                        = first;
    second.actorGroup                  = 3;
    second.particleIndex               = 9;
    const VolumeFluidContact initial[] = {second, duplicate, first};

    auto entered = tracker.advance(initial, .01f);
    REQUIRE(entered.ok());
    REQUIRE(entered.value().size() == 2);
    CHECK(entered.value()[0].type == VolumeFluidContactEventType::Enter);
    CHECK(entered.value()[0].contact.actorGroup == 2);
    CHECK(entered.value()[0].contact.particleIndex == 8);
    CHECK(entered.value()[1].contact.actorGroup == 3);

    auto replacement                   = first;
    replacement.actorGroup             = 5;
    const VolumeFluidContact current[] = {first, replacement};
    auto                     events    = tracker.advance(current, .01f);
    REQUIRE(events.ok());
    REQUIRE(events.value().size() == 3);
    CHECK(events.value()[0].type == VolumeFluidContactEventType::Stay);
    CHECK(events.value()[1].type == VolumeFluidContactEventType::Exit);
    CHECK(events.value()[1].contact.actorGroup == 3);
    CHECK(events.value()[2].type == VolumeFluidContactEventType::Enter);
    CHECK(events.value()[2].contact.actorGroup == 5);

    CHECK(!tracker.advance(current, std::numeric_limits<float>::quiet_NaN()).ok());
    auto exited = tracker.advance({}, .01f);
    REQUIRE(exited.ok());
    REQUIRE(exited.value().size() == 2);
    CHECK(exited.value()[0].type == VolumeFluidContactEventType::Exit);
    CHECK(exited.value()[1].type == VolumeFluidContactEventType::Exit);

    tracker.reset();
    auto empty = tracker.advance({}, .01f);
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());
}

TEST_CASE("fluids.volume.actorFilterCategoryPreservesMaskAtomically") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position        = {0.f, 1.f, 0.f};
    particles[0].actorGroup      = 7;
    particles[0].collisionFilter = 0x00f00001u;
    particles[1].position        = {.2f, 1.f, 0.f};
    particles[1].actorGroup      = 7;
    particles[1].collisionFilter = 0x0f000002u;
    particles[2].position        = {.4f, 1.f, 0.f};
    particles[2].actorGroup      = 8;
    particles[2].collisionFilter = 0xff000004u;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->setActorFilterCategory(7, 3).ok());
    const auto changed = solver->snapshot();
    CHECK(changed.particles[0].collisionFilter == 0x00f00008u);
    CHECK(changed.particles[1].collisionFilter == 0x0f000008u);
    CHECK(changed.particles[2].collisionFilter == 0xff000004u);
    CHECK(!solver->setActorFilterCategory(7, 16).ok());
    CHECK(!solver->setActorFilterCategory(9, 1).ok());
    CHECK(solver->snapshot().particles[0].collisionFilter == 0x00f00008u);
}

TEST_CASE("fluids.volume.actorCollisionFilterReplacesPackedValueAtomically") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position        = {0.f, 1.f, 0.f};
    particles[0].actorGroup      = 7;
    particles[0].collisionFilter = 0x00f00001u;
    particles[1].position        = {.2f, 1.f, 0.f};
    particles[1].actorGroup      = 7;
    particles[1].collisionFilter = 0x0f000002u;
    particles[2].position        = {.4f, 1.f, 0.f};
    particles[2].actorGroup      = 8;
    particles[2].collisionFilter = 0xff000004u;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->setActorCollisionFilter(7, 0x00aa0020u).ok());
    auto changed = solver->snapshot();
    CHECK(changed.particles[0].collisionFilter == 0x00aa0020u);
    CHECK(changed.particles[1].collisionFilter == 0x00aa0020u);
    CHECK(changed.particles[2].collisionFilter == 0xff000004u);
    CHECK(!solver->setActorCollisionFilter(7, 0x00aa0000u).ok());
    CHECK(!solver->setActorCollisionFilter(9, 0x00aa0020u).ok());
    changed = solver->snapshot();
    CHECK(changed.particles[0].collisionFilter == 0x00aa0020u);
    CHECK(changed.particles[1].collisionFilter == 0x00aa0020u);
}

TEST_CASE("fluids.volume.actorMaterialRefreshMatchesEmitterBlueprintAtomically") {
    VolumeFluidSettings settings;
    settings.spacing = .2f;
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position   = {0.f, 1.f, 0.f};
    particles[0].actorGroup = 7;
    particles[0].color      = {1, 0, 0, 1};
    particles[0].life       = 3.f;
    particles[1]            = particles[0];
    particles[1].position.x = .3f;
    particles[2]            = particles[0];
    particles[2].position.x = .6f;
    particles[2].actorGroup = 8;
    REQUIRE(solver->emit(particles).ok());

    VolumeFluidParticle prototype;
    prototype.material.viscosity = 9.f;
    prototype.material.cohesion  = .7f;
    prototype.data               = {1, 2, 3, 4};
    prototype.radii              = {0, 0, 0};
    prototype.collisionFilter    = 0x00aa0020u;
    prototype.selfCollide        = false;
    REQUIRE(solver->updateActorMaterial(7, prototype).ok());
    const auto changed = solver->snapshot();
    for (size_t i = 0; i < 2; ++i) {
        CHECK(changed.particles[i].material.viscosity == 9.f);
        CHECK(changed.particles[i].material.cohesion == .7f);
        CHECK(changed.particles[i].data == glm::vec4(1, 2, 3, 4));
        CHECK(changed.particles[i].radii == glm::vec3(.1f));
        CHECK(changed.particles[i].collisionFilter == 0x00aa0020u);
        CHECK(!changed.particles[i].selfCollide);
        CHECK(changed.particles[i].color == glm::vec4(1, 0, 0, 1));
        CHECK(changed.particles[i].life == 3.f);
    }
    CHECK(changed.particles[2].material.viscosity != 9.f);
    prototype.material.viscosity = -1.f;
    CHECK(!solver->updateActorMaterial(7, prototype).ok());
    CHECK(solver->snapshot().particles[0].material.viscosity == 9.f);
    CHECK(!solver->updateActorMaterial(9, VolumeFluidParticle{}).ok());
}

TEST_CASE("fluids.volume.actorSelfCollisionsToggleEveryLiveParticle") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position   = {0.f, 1.f, 0.f};
    particles[0].actorGroup = 7;
    particles[1]            = particles[0];
    particles[1].position.x = .2f;
    particles[2]            = particles[0];
    particles[2].position.x = .4f;
    particles[2].actorGroup = 8;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->setActorSelfCollisions(7, false).ok());
    auto changed = solver->snapshot();
    CHECK(!changed.particles[0].selfCollide);
    CHECK(!changed.particles[1].selfCollide);
    CHECK(changed.particles[2].selfCollide);
    REQUIRE(solver->setActorSelfCollisions(7, true).ok());
    changed = solver->snapshot();
    CHECK(changed.particles[0].selfCollide);
    CHECK(changed.particles[1].selfCollide);
    CHECK(!solver->setActorSelfCollisions(9, false).ok());
    CHECK(solver->snapshot().particles[0].selfCollide);
}
