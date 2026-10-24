#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_collision_material)
#include "PrivateTestScope.h"
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_collision_material.cpp
namespace test_fluids_volume_collision_material {
// fluids_volume_collision_material.cpp
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

// fluids_volume_contact_grabber.cpp
using namespace eve::fluids;

namespace {
std::unique_ptr<VolumeFluid> contactGrabberSolver(std::span<const VolumeFluidParticle> particles,
                                                  std::span<const VolumeFluidCollider> colliders) {
    VolumeFluidSettings settings;
    settings.gravity    = {0.f, -10.f, 0.f};
    settings.iterations = 2;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->setColliders(colliders).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    return solver;
}
}  // namespace

TEST_CASE("fluids.volume.contactGrabberCapturesMovesFreezesAndReleases") {
    VolumeFluidParticle particle;
    particle.position = {.2f, .5f, 0.f};
    VolumeFluidCollider sphere;
    sphere.label  = 41;
    sphere.center = {0.f, .5f, 0.f};
    sphere.radius = .5f;
    auto solver   = contactGrabberSolver(std::span(&particle, 1), std::span(&sphere, 1));

    REQUIRE(solver->grabContactParticles(41, sphere.center, {0.f, 0.f, 0.f, 1.f}, .01f).value() == 1);
    const auto captured = solver->particles()[0].position;
    REQUIRE(
        solver->updateGrabbedParticles(41, sphere.center + glm::vec3(.3f, .2f, 0.f), {0.f, 0.f, 0.f, 1.f}).value() ==
        1);
    const auto moved = solver->particles()[0].position;
    REQUIRE(glm::length(moved - captured - glm::vec3(.3f, .2f, 0.f)) < 1e-6f);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(solver->particles()[0].position - moved) < 1e-6f);
    REQUIRE(glm::length(solver->particles()[0].velocity) == 0.f);

    REQUIRE(solver->releaseGrabbedParticles(41).value() == 1);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.y < moved.y);
    REQUIRE(solver->releaseGrabbedParticles(41).value() == 0);
}

TEST_CASE("fluids.volume.contactGrabberRemapsDeathAndReleasesMissingCollider") {
    VolumeFluidParticle particles[2];
    particles[0].position = {.15f, .5f, 0.f};
    particles[1].position = {.25f, .5f, 0.f};
    VolumeFluidCollider sphere;
    sphere.label  = 42;
    sphere.center = {0.f, .5f, 0.f};
    sphere.radius = .5f;
    auto solver   = contactGrabberSolver(particles, std::span(&sphere, 1));
    REQUIRE(solver->grabContactParticles(42, sphere.center, {0.f, 0.f, 0.f, 1.f}, .01f).value() == 2);

    REQUIRE(solver->killParticle(0).ok());
    const auto before = solver->particles()[0].position;
    REQUIRE(
        solver->updateGrabbedParticles(42, sphere.center + glm::vec3(.2f, 0.f, 0.f), {0.f, 0.f, 0.f, 1.f}).value() ==
        1);
    REQUIRE(std::abs(solver->particles()[0].position.x - before.x - .2f) < 1e-6f);

    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->releaseGrabbedParticles(42).value() == 0);
    const float y = solver->particles()[0].position.y;
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.y < y);
}

TEST_CASE("fluids.volume.contactGrabberValidationAndOwnershipAreAtomic") {
    VolumeFluidParticle particle;
    particle.position = {.2f, .5f, 0.f};
    VolumeFluidCollider colliders[2];
    colliders[0].label     = 43;
    colliders[0].center    = {0.f, .5f, 0.f};
    colliders[0].radius    = .5f;
    colliders[0].isTrigger = true;
    colliders[1]           = colliders[0];
    colliders[1].label     = 44;
    auto solver            = contactGrabberSolver(std::span(&particle, 1), colliders);
    REQUIRE(solver->grabContactParticles(43, colliders[0].center, {0.f, 0.f, 0.f, 1.f}, .01f).value() == 1);
    const auto before = solver->particles()[0].position;
    REQUIRE(!solver->grabContactParticles(44, colliders[1].center, {0.f, 0.f, 0.f, 1.f}, .01f).ok());
    REQUIRE(!solver->updateGrabbedParticles(43, {100.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 1.f}).ok());
    REQUIRE(glm::length(solver->particles()[0].position - before) < 1e-6f);
    const auto persistent = solver->snapshot();
    REQUIRE(solver->restore(persistent).ok());
    REQUIRE(solver->releaseGrabbedParticles(43).value() == 0);
}

// fluids_volume_contacts.cpp
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

// fluids_volume_diffuse.cpp
using namespace eve::fluids;

TEST_CASE("fluids.volume.diffuse.advectionLifetimeAndOwnership") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                      fluid = std::move(created).takeValue();
    const VolumeFluidParticle source{{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
    REQUIRE(fluid->emit(std::span(&source, 1)).ok());
    VolumeFluidDiffuse               pool;
    const VolumeFluidDiffuseParticle p{{0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}, .03f};
    REQUIRE(pool.emit(std::span(&p, 1)).ok());
    const auto saved = pool.snapshot();
    REQUIRE(pool.advance(*fluid, .01f, 1).ok());
    auto next = pool.snapshot();
    REQUIRE(std::abs(next.particles[0].position.x - .01f) < 1e-6f);
    REQUIRE(std::abs(next.particles[0].life - .02f) < 1e-6f);
    REQUIRE(next.particles[0].velocity.x == 1.f);
    REQUIRE(saved.particles[0].position.x == 0.f);
    REQUIRE(fluid->particles()[0].position.x == 0.f);
    REQUIRE(fluid->particles()[0].velocity.x == 1.f);
    VolumeFluidDiffuse replay;
    REQUIRE(replay.restore(saved).ok());
    REQUIRE(replay.advance(*fluid, .01f, 1).ok());
    REQUIRE(replay.snapshot().particles[0].position.x == next.particles[0].position.x);
    REQUIRE(pool.advance(*fluid, .03f, 1).ok());
    REQUIRE(pool.particleCount() == 0);
    REQUIRE(replay.advance(*fluid, .01f, 2).ok());
    REQUIRE(replay.particleCount() == 0);
    REQUIRE(pool.restore(saved).ok());
    fluid.reset();
    auto replacement = VolumeFluid::create({});
    REQUIRE(replacement.ok());
    REQUIRE(pool.advance(*replacement.value(), .01f, 1).ok());
    REQUIRE(pool.particleCount() == 0);
}

TEST_CASE("fluids.volume.diffuse.atomicAdmissionAndStrictState") {
    VolumeFluidDiffuse pool;
    auto               state = pool.snapshot();
    state.capacity           = 2;
    REQUIRE(pool.restore(state).ok());
    std::vector<VolumeFluidDiffuseParticle> batch(2);
    batch[1].life = 0.f;
    REQUIRE(!pool.emit(batch).ok());
    REQUIRE(pool.particleCount() == 0);
    batch[1].life = 1.f;
    REQUIRE(pool.emit(batch).ok());
    REQUIRE(!pool.emit(batch).ok());
    REQUIRE(pool.particleCount() == 2);
    REQUIRE(pool.availableCapacity() == 0);
    std::vector<glm::vec4> renderData;
    pool.copyRenderData(renderData);
    const auto* renderStorage = renderData.data();
    REQUIRE(renderData.size() == 2);
    REQUIRE(renderData[0].w == 2.f);
    pool.copyRenderData(renderData);
    REQUIRE(renderData.data() == renderStorage);
    state        = pool.snapshot();
    auto encoded = encodeVolumeFluidDiffuse(state);
    auto decoded = decodeVolumeFluidDiffuse(encoded);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().particles.size() == 2);
    REQUIRE(decodeVolumeFluidDiffuseParticles(*encoded.find("particles")).ok());
    encoded.set("unknown", 1);
    REQUIRE(!decodeVolumeFluidDiffuse(encoded).ok());
    state.version = 2;
    REQUIRE(!pool.restore(state).ok());
    state.version                 = 1;
    state.particles[0].velocity.x = std::numeric_limits<float>::infinity();
    REQUIRE(!pool.restore(state).ok());
    REQUIRE(pool.snapshot().particles[0].velocity.x == 0.f);
    auto fluid = VolumeFluid::create({});
    REQUIRE(fluid.ok());
    REQUIRE(!pool.advance(*fluid.value(), 0.f, 1).ok());
    REQUIRE(!pool.advance(*fluid.value(), .01f, 1000001).ok());
    REQUIRE(pool.snapshot().particles[0].life == 2.f);
    pool.clear();
    REQUIRE(pool.availableCapacity() == 2);
}

TEST_CASE("fluids.volume.diffuse.queryBudgetRollbackAndExpiredFastPath") {
    auto fluid = VolumeFluid::create({});
    REQUIRE(fluid.ok());
    std::vector<VolumeFluidParticle> sources(1000);
    for (auto& p : sources) p.position = {0.f, 1.f, 0.f};
    REQUIRE(fluid.value()->emit(sources).ok());
    VolumeFluidDiffuse pool;
    auto               state = pool.snapshot();
    state.capacity           = 4001;
    state.particles.resize(4001);
    for (auto& p : state.particles) p.position = {0.f, 1.f, 0.f};
    REQUIRE(pool.restore(state).ok());
    REQUIRE(!pool.advance(*fluid.value(), .01f, 1).ok());
    REQUIRE(pool.particleCount() == 4001);
    REQUIRE(pool.snapshot().particles.back().life == 2.f);
    REQUIRE(pool.snapshot().particles.back().position.y == 1.f);
    for (auto& p : state.particles) p.life = .005f;
    REQUIRE(pool.restore(state).ok());
    REQUIRE(pool.advance(*fluid.value(), .01f, 1).ok());
    REQUIRE(pool.particleCount() == 0);
    REQUIRE(fluid.value()->particleCount() == 1000);
}
}  // namespace test_fluids_volume_collision_material
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_coupling)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCoupling.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_coupling.cpp
namespace test_fluids_volume_coupling_unit {
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
}  // namespace test_fluids_volume_coupling_unit
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_emitter)
#include "PrivateTestScope.h"
#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidEmitter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_emitter.cpp
namespace test_fluids_volume_emitter {
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

static void removeV12MaterialFields(eve::Value& description) {
    auto*       prototype = description.find("prototype");
    const auto* source    = prototype ? prototype->find("material") : nullptr;
    REQUIRE(source != nullptr);
    eve::Value material(eve::Value::Object{});
    for (const auto& key : source->keys())
        if (key != "stickiness" && key != "stickDistance" && key != "frictionCombine" && key != "stickinessCombine")
            material.set(key, *source->find(key));
    prototype->set("material", std::move(material));
}

TEST_CASE("fluids.volumeEmitter.shapesRepeatableAndAtomic") {
    for (auto shape : {VolumeFluidEmissionShape::Edge, VolumeFluidEmissionShape::Square, VolumeFluidEmissionShape::Disk,
                       VolumeFluidEmissionShape::Sphere, VolumeFluidEmissionShape::Cube}) {
        auto ar = VolumeFluid::create({}), br = VolumeFluid::create({});
        REQUIRE(ar.ok());
        REQUIRE(br.ok());
        auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
        VolumeFluidEmission e;
        e.shape     = shape;
        e.direction = {0.f, 0.f, 1.f};
        e.extent    = {0.2f, 0.2f, 0.2f};
        e.seed      = 42;
        e.jitter    = 0.2f;
        REQUIRE(emitVolumeFluidBurst(*a, e, 100).ok());
        REQUIRE(emitVolumeFluidBurst(*b, e, 100).ok());
        auto pa = a->particles(), pb = b->particles();
        for (size_t i = 0; i < pa.size(); ++i) {
            REQUIRE(glm::length(pa[i].position - pb[i].position) == 0.f);
            REQUIRE(glm::length(pa[i].velocity - pb[i].velocity) == 0.f);
            const auto offset = pa[i].position - e.origin;
            REQUIRE(std::abs(offset.x) <= .20001f);
            REQUIRE(std::abs(offset.y) <= .20001f);
            REQUIRE(std::abs(offset.z) <= .20001f);
            if (shape == VolumeFluidEmissionShape::Disk || shape == VolumeFluidEmissionShape::Sphere)
                REQUIRE(glm::length(offset) <= .20001f);
            if (shape == VolumeFluidEmissionShape::Edge) REQUIRE(offset.y == 0.f);
            if (shape != VolumeFluidEmissionShape::Sphere && shape != VolumeFluidEmissionShape::Cube)
                REQUIRE(offset.z == 0.f);
        }
        e.origin.y = -10.f;
        REQUIRE(!emitVolumeFluidBurst(*a, e, 2).ok());
        REQUIRE(a->particleCount() == 100);
    }
}

TEST_CASE("fluids.volumeEmitter.orderedShapeSetComposition") {
    VolumeFluidEmission base;
    base.origin          = {9.f, 8.f, 7.f};
    base.direction       = {1.f, 0.f, 0.f};
    base.speed           = 2.f;
    base.prototype.color = {.2f, .3f, .4f, .5f};
    VolumeFluidEmission first;
    first.shape        = VolumeFluidEmissionShape::Distribution;
    first.origin       = {1.f, 2.f, 3.f};
    first.direction    = {1.f, 0.f, 0.f};
    first.distribution = {{{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}}};
    VolumeFluidEmission second;
    second.shape     = VolumeFluidEmissionShape::Distribution;
    second.origin    = {-1.f, 0.f, 0.f};
    second.direction = {0.f, 0.f, 1.f};

    const std::vector shapes{first, second};
    auto              result = composeVolumeFluidEmitterShapes(base, shapes);
    REQUIRE(result.ok());
    const auto& combined = result.value();
    REQUIRE(combined.shape == VolumeFluidEmissionShape::Distribution);
    REQUIRE(combined.distribution.size() == 2);
    REQUIRE(glm::length(combined.distribution[0].position - glm::vec3(1.f, 3.f, 3.f)) < .00001f);
    REQUIRE(glm::length(combined.distribution[0].direction - glm::vec3(1.f, 0.f, 0.f)) < .00001f);
    REQUIRE(combined.distribution[0].color == glm::vec4(1.f, 0.f, 0.f, 1.f));
    REQUIRE(combined.distribution[1].position == second.origin);
    REQUIRE(combined.distribution[1].direction == second.direction);
    REQUIRE(combined.speed == base.speed);
    REQUIRE(combined.prototype.color == base.prototype.color);

    auto fallback = composeVolumeFluidEmitterShapes(base, {});
    REQUIRE(fallback.ok());
    REQUIRE(fallback.value().distribution.size() == 1);
    REQUIRE(fallback.value().distribution[0].position == base.origin);
    REQUIRE(fallback.value().distribution[0].direction == glm::vec3(1.f, 0.f, 0.f));

    first.shape = VolumeFluidEmissionShape::Cube;
    const std::vector invalid{first};
    REQUIRE(!composeVolumeFluidEmitterShapes(base, invalid).ok());
    REQUIRE(combined.distribution.size() == 2);

    VolumeFluidEmission dense = second;
    dense.distribution.assign(4096, {});
    const std::vector excessivePoints{dense, second};
    REQUIRE(!composeVolumeFluidEmitterShapes(base, excessivePoints).ok());
    const std::vector excessiveShapes(65, second);
    REQUIRE(!composeVolumeFluidEmitterShapes(base, excessiveShapes).ok());
}

TEST_CASE("fluids.volumeEmitter.granularRadiusRandomness") {
    VolumeFluidSettings settings;
    settings.spacing = .1f;
    auto ar = VolumeFluid::create(settings), br = VolumeFluid::create(settings);
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidEmission emission;
    emission.seed                     = 91;
    emission.granularRadiusRandomness = 100.f;
    emission.prototype.material.phase = VolumeFluidPhase::Granular;
    REQUIRE(emitVolumeFluidBurst(*a, emission, 64).ok());
    REQUIRE(emitVolumeFluidBurst(*b, emission, 64).ok());
    bool varied = false;
    for (size_t i = 0; i < a->particleCount(); ++i) {
        const auto ra = a->particles()[i].radii, rb = b->particles()[i].radii;
        REQUIRE(ra == rb);
        REQUIRE(ra.x == ra.y);
        REQUIRE(ra.y == ra.z);
        REQUIRE(ra.x >= .001f);
        REQUIRE(ra.x <= .051001f);
        varied |= std::abs(ra.x - .05f) > .001f;
    }
    REQUIRE(varied);
    auto liquid                     = emission;
    liquid.prototype.material.phase = VolumeFluidPhase::Liquid;
    liquid.prototype.radii          = glm::vec3(.03f);
    auto lr                         = VolumeFluid::create(settings);
    REQUIRE(lr.ok());
    auto liquidSolver = std::move(lr).takeValue();
    REQUIRE(emitVolumeFluidBurst(*liquidSolver, liquid, 1).ok());
    REQUIRE(liquidSolver->particles()[0].radii == glm::vec3(.03f));
    emission.granularRadiusRandomness = 100.01f;
    REQUIRE(!emitVolumeFluidBurst(*a, emission, 1).ok());
}

TEST_CASE("fluids.volumeEmitter.streamPoolAndBudget") {
    auto created = VolumeFluid::create(VolumeFluidSettings{.capacity = 10});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmitter  stream;
    VolumeFluidEmission e;
    e.prototype.life = .01f;
    auto result      = stream.advance(*solver, e, 1.f / 60.f, 6000.f, 3);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 3);
    // Whole credit beyond the explicit work cap must not become a catch-up burst.
    result = stream.advance(*solver, e, 0.f, 0.f, 3);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 0);
    for (int i = 0; i < 3; ++i) REQUIRE(stream.advance(*solver, e, 1.f / 60.f, 6000.f, 3).ok());
    REQUIRE(solver->particleCount() == 10);
    REQUIRE(stream.advance(*solver, e, 1.f / 60.f, 6000.f, 3).value() == 0);
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(solver->particleCount() == 0);
    REQUIRE(stream.advance(*solver, e, 1.f / 60.f, 6000.f, 3).value() == 3);
}

TEST_CASE("fluids.volumeEmitter.zeroSpeedStopsWithoutCatchupDebt") {
    VolumeFluidSettings settings;
    settings.capacity = 16;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmission emission;
    emission.shape  = VolumeFluidEmissionShape::Edge;
    emission.extent = glm::vec3(0.f);
    VolumeFluidEmitter stream;

    REQUIRE(stream.advance(*solver, emission, .006f, 100.f, 1, 0.f).value() == 0);
    emission.speed = 0.f;
    REQUIRE(stream.advance(*solver, emission, 1.f / 30.f, 100.f, 1, 0.f).value() == 0);
    REQUIRE(solver->particleCount() == 0);
    REQUIRE(stream.snapshot().phase == "0");
    emission.speed = 1.f;
    REQUIRE(stream.advance(*solver, emission, .006f, 100.f, 1, 0.f).value() == 0);
    REQUIRE(stream.advance(*solver, emission, .006f, 100.f, 1, 0.f).value() == 1);

    solver->clear();
    VolumeFluidJetEmitter jet;
    REQUIRE(jet.advance(*solver, emission, .026f, 1, 0.f).value() == 0);
    emission.speed = 0.f;
    REQUIRE(jet.advance(*solver, emission, 1.f / 30.f, 1, 0.f).value() == 0);
    REQUIRE(jet.snapshot().phase == "0");
    emission.speed = 1.f;
    for (int i = 0; i < 3; ++i) REQUIRE(jet.advance(*solver, emission, .026f, 1, 0.f).value() == 0);
    REQUIRE(jet.advance(*solver, emission, .026f, 1, 0.f).value() == 1);
}

TEST_CASE("fluids.volumeEmitter.burstWaitsForActorDeathAndPoolRecovery") {
    VolumeFluidSettings settings;
    settings.capacity = 4;
    settings.gravity  = {0.f, 0.f, 0.f};
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmitter  emitter;
    VolumeFluidEmission emission;
    emission.seed                 = 17;
    emission.jitter               = .25f;
    emission.prototype.actorGroup = 7;
    emission.prototype.life       = .01f;
    auto first                    = emitter.advanceBurst(*solver, emission, 2, 0.f);
    REQUIRE(first.ok());
    REQUIRE(first.value() == 2);
    REQUIRE(solver->particleCount() == 2);
    auto waiting = emitter.advanceBurst(*solver, emission, 2, 0.f);
    REQUIRE(waiting.ok());
    REQUIRE(waiting.value() == 0);
    REQUIRE(solver->particleCount() == 2);

    VolumeFluidEmission other  = emission;
    other.prototype.actorGroup = 8;
    other.prototype.life       = 0.f;
    REQUIRE(emitVolumeFluidBurst(*solver, other, 2).ok());
    REQUIRE(solver->killParticle(1).ok());
    REQUIRE(solver->killParticle(0).ok());
    REQUIRE(solver->particleCount() == 2);
    auto actorPoolIgnoresOtherActorPressure = emitter.advanceBurst(*solver, emission, 2, .5f);
    REQUIRE(actorPoolIgnoresOtherActorPressure.ok());
    REQUIRE(actorPoolIgnoresOtherActorPressure.value() == 2);
    auto actorStillAlive = emitter.advanceBurst(*solver, emission, 2, 0.f);
    REQUIRE(actorStillAlive.ok());
    REQUIRE(actorStillAlive.value() == 0);
    REQUIRE(solver->particleCount() == 4);
}

TEST_CASE("fluids.volumeEmitter.burstFailureIsAtomicAndSnapshotContinuesSequence") {
    VolumeFluidSettings settings;
    settings.capacity = 8;
    settings.gravity  = {0.f, 0.f, 0.f};
    auto ar = VolumeFluid::create(settings), br = VolumeFluid::create(settings);
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidEmitter  source, restored;
    VolumeFluidEmission emission;
    emission.shape                = VolumeFluidEmissionShape::Sphere;
    emission.seed                 = 91;
    emission.jitter               = .2f;
    emission.prototype.actorGroup = 11;
    emission.prototype.life       = .01f;
    REQUIRE(source.advanceBurst(*a, emission, 2, 0.f).ok());
    REQUIRE(a->killParticle(1).ok());
    REQUIRE(a->killParticle(0).ok());
    const auto saved         = source.snapshot();
    const auto particleCount = a->particleCount();
    REQUIRE(!source.advanceBurst(*a, emission, 0, 0.f).ok());
    REQUIRE(!source.advanceBurst(*a, emission, 2, 1.01f).ok());
    REQUIRE(source.snapshot().sequence == saved.sequence);
    REQUIRE(source.snapshot().phase == saved.phase);
    REQUIRE(source.snapshot().emitting == saved.emitting);
    REQUIRE(a->particleCount() == particleCount);
    REQUIRE(restored.restore(saved).ok());
    auto nextA = source.advanceBurst(*a, emission, 2, 0.f);
    auto nextB = restored.advanceBurst(*b, emission, 2, 0.f);
    REQUIRE(nextA.ok());
    REQUIRE(nextB.ok());
    REQUIRE(nextA.value() == 2);
    REQUIRE(nextB.value() == 2);
    for (size_t i = 0; i < 2; ++i) {
        REQUIRE(glm::length(a->particles()[i].position - b->particles()[i].position) == 0.f);
        REQUIRE(glm::length(a->particles()[i].velocity - b->particles()[i].velocity) == 0.f);
    }
}

TEST_CASE("fluids.volumeEmitter.failurePreservesStream") {
    auto ar = VolumeFluid::create({}), br = VolumeFluid::create({});
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidEmitter  sa, sb;
    VolumeFluidEmission e;
    REQUIRE(sa.advance(*a, e, .01f, 50.f).ok());
    REQUIRE(sb.advance(*b, e, .01f, 50.f).ok());
    auto bad     = e;
    bad.origin.y = -20.f;
    REQUIRE(!sa.advance(*a, bad, .02f, 50.f).ok());
    REQUIRE(sa.advance(*a, e, .02f, 50.f).ok());
    REQUIRE(sb.advance(*b, e, .02f, 50.f).ok());
    REQUIRE(a->particleCount() == 1);
    REQUIRE(glm::length(a->particles()[0].position - b->particles()[0].position) == 0.f);
    REQUIRE(!sa.advance(*a, e, std::numeric_limits<float>::quiet_NaN(), 50.f).ok());
}

TEST_CASE("fluids.volumeEmitter.restartThreshold") {
    auto created = VolumeFluid::create(VolumeFluidSettings{.capacity = 10});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> initial(5);
    for (size_t i = 0; i < initial.size(); ++i) {
        initial[i].position = {float(i) * .1f, 1.f, 0.f};
        initial[i].life     = .01f;
    }
    REQUIRE(solver->emit(initial).ok());
    VolumeFluidEmitter  stream;
    VolumeFluidEmission e;
    e.actorCapacity = 10;
    auto stopped    = stream.advance(*solver, e, 1.f / 60.f, 600.f, 2, .5f);
    REQUIRE(stopped.ok());
    REQUIRE(stopped.value() == 0);
    REQUIRE(solver->step(1.f / 60.f).ok());
    auto restarted = stream.advance(*solver, e, 1.f / 60.f, 600.f, 2, .5f);
    REQUIRE(restarted.ok());
    REQUIRE(restarted.value() == 2);
}


TEST_CASE("fluids.volumeEmitter.strictDescriptionCodec") {
    VolumeFluidEmission original;
    original.seed  = 123;
    original.speed = 3.f;
    auto value     = encodeVolumeFluidEmission(original);
    auto decoded   = decodeVolumeFluidEmission(value);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().seed == 123);
    REQUIRE(decoded.value().speed == 3.f);
    value.set("version", 99);
    REQUIRE(!decodeVolumeFluidEmission(value).ok());
    value = encodeVolumeFluidEmission(original);
    value.set("extra", 1);
    REQUIRE(!decodeVolumeFluidEmission(value).ok());
    value            = encodeVolumeFluidEmission(original);
    auto description = *value.find("description");
    description.set("extra", 1);
    value.set("description", description);
    REQUIRE(!decodeVolumeFluidEmission(value).ok());
    REQUIRE(!decodeVolumeFluidEmission(eve::Value(3)).ok());
}

TEST_CASE("fluids.volumeEmitter.batchDecodeIsIndependent") {
    VolumeFluidSnapshot state;
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    state.particles.push_back(particle);
    auto encoded = encodeVolumeFluid(state);
    auto batch   = decodeVolumeFluidParticles(*encoded.find("particles"));
    REQUIRE(batch.ok());
    REQUIRE(batch.value().size() == 1);
    state.particles.clear();
    REQUIRE(batch.value()[0].position.y == 1.f);
    REQUIRE(!decodeVolumeFluidParticles(eve::Value(1)).ok());
    eve::Value::Array malformed;
    malformed.emplace_back(eve::Value::Object{});
    REQUIRE(!decodeVolumeFluidParticles(eve::Value(std::move(malformed))).ok());
}

TEST_CASE("fluids.volumeEmitter.jetSpacingAndTemporalLayers") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidJetEmitter jet;
    VolumeFluidEmission   e;
    e.direction = {0.f, 0.f, 1.f};
    e.extent    = {.1f, .1f, .1f};
    e.speed     = 12.f;
    auto result = jet.advance(*solver, e, .025f, 32, 0.f);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 15);
    const auto particles = solver->particles();
    for (size_t i = 0; i < particles.size(); ++i)
        for (size_t j = i + 1; j < particles.size(); ++j)
            REQUIRE(glm::length(particles[i].position - particles[j].position) >= .09999f);
    for (const auto& p : particles) {
        REQUIRE(p.position.z >= 0.f);
        REQUIRE(p.position.z <= .20001f);
    }
}

TEST_CASE("fluids.volumeEmitter.jetRejectsAndPreservesPhase") {
    auto ar = VolumeFluid::create({}), br = VolumeFluid::create({});
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                  a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidJetEmitter ja, jb;
    VolumeFluidEmission   e;
    e.speed = 3.f;
    REQUIRE(ja.advance(*a, e, .02f, 16).ok());
    REQUIRE(jb.advance(*b, e, .02f, 16).ok());
    auto bad     = e;
    bad.origin.y = -10.f;
    REQUIRE(!ja.advance(*a, bad, .02f, 16).ok());
    REQUIRE(!ja.advance(*a, e, .02f, 1).ok());
    auto ra = ja.advance(*a, e, .02f, 16), rb = jb.advance(*b, e, .02f, 16);
    REQUIRE(ra.ok());
    REQUIRE(rb.ok());
    REQUIRE(ra.value() == 5);
    REQUIRE(glm::length(a->particles()[0].position - b->particles()[0].position) == 0.f);
    bad       = e;
    bad.shape = VolumeFluidEmissionShape::Cube;
    REQUIRE(!ja.advance(*a, bad, .02f, 16).ok());
}

TEST_CASE("fluids.volumeEmitter.directEmittingStateMatchesSnapshotWithoutProjection") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidEmission   emission;
    VolumeFluidEmitter    rate;
    VolumeFluidJetEmitter jet;
    CHECK(!rate.isEmitting());
    CHECK(!jet.isEmitting());
    CHECK(rate.isEmitting() == rate.snapshot().emitting);
    CHECK(jet.isEmitting() == jet.snapshot().emitting);
    REQUIRE(rate.advance(*solver, emission, .02f, 100.f, 1, 0.f).value() == 1);
    CHECK(rate.isEmitting());
    CHECK(rate.isEmitting() == rate.snapshot().emitting);
    solver->clear();
    REQUIRE(jet.advance(*solver, emission, 1.f / 30.f, 8, 0.f).ok());
    CHECK(jet.isEmitting());
    CHECK(jet.isEmitting() == jet.snapshot().emitting);
    auto saved     = jet.snapshot();
    saved.emitting = false;
    REQUIRE(jet.restore(saved).ok());
    CHECK(!jet.isEmitting());
}

TEST_CASE("fluids.volumeEmitter.imageMaskColorAndAdmission") {
    const glm::vec4 pixels[] = {{1, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 1}, {1, 1, 1, 0}};
    auto            built    = buildVolumeFluidImageDistribution(pixels, 2, 2, .1f, 1.f, .05f, .5f);
    REQUIRE(built.ok());
    REQUIRE(!built.value().empty());
    for (const auto& p : built.value()) {
        REQUIRE(p.color.a > .5f);
        REQUIRE(p.position.z == 0.f);
    }
    VolumeFluidEmission e;
    e.shape           = VolumeFluidEmissionShape::Distribution;
    e.direction       = {0, 0, 1};
    e.distribution    = built.value();
    e.prototype.color = {1, 1, 1, 1};
    auto created      = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(emitVolumeFluidBurst(*solver, e, unsigned(e.distribution.size())).ok());
    auto particles = solver->particles();
    for (size_t i = 0; i < particles.size(); ++i) {
        REQUIRE(glm::length(particles[i].position - e.origin - e.distribution[i].position) < 1e-6f);
        REQUIRE(particles[i].color.r == e.distribution[i].color.r);
    }
    auto decoded = decodeVolumeFluidEmission(encodeVolumeFluidEmission(e));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().distribution.size() == e.distribution.size());
    REQUIRE(!buildVolumeFluidImageDistribution(pixels, 3, 2, .1f, 1.f, .05f, .5f).ok());
    REQUIRE(!buildVolumeFluidImageDistribution(pixels, 2, 2, .1f, 1.f, .00001f, .5f).ok());
}

TEST_CASE("fluids.volumeEmitter.imageThresholdAndAspect") {
    std::vector<glm::vec4> pixels(8, glm::vec4(1.f));
    REQUIRE(buildVolumeFluidImageDistribution(pixels, 4, 2, 1.f, 2.f, .5f, 1.f).value().empty());
    auto points = buildVolumeFluidImageDistribution(pixels, 4, 2, 1.f, 2.f, .5f, 0.f);
    REQUIRE(points.ok());
    REQUIRE(points.value().size() == 8);
    REQUIRE(points.value().front().position.x == -1.f);
    REQUIRE(points.value().front().position.y == -.5f);
}

TEST_CASE("fluids.volumeEmitter.descriptionV1Migration") {
    auto        current = encodeVolumeFluidEmission(VolumeFluidEmission{});
    eve::Value  legacyDescription(eve::Value::Object{});
    const auto& description = *current.find("description");
    for (const auto& key : description.keys()) {
        if (key == "distribution" || key == "granularRadiusRandomness" || key == "randomVelocity" ||
            key == "useShapeColor" || key == "actorCapacity")
            continue;
        if (key != "prototype") {
            legacyDescription.set(key, *description.find(key));
            continue;
        }
        eve::Value  oldPrototype(eve::Value::Object{});
        const auto& prototype = *description.find(key);
        for (const auto& field : prototype.keys())
            if (field != "collisionFilter" && field != "radii" && field != "orientation" && field != "actorGroup" &&
                field != "selfCollide")
                oldPrototype.set(field, *prototype.find(field));
        legacyDescription.set(key, withoutRecentMaterialFields(std::move(oldPrototype)));
    }
    current.set("version", 1);
    current.set("description", legacyDescription);
    auto migrated = decodeVolumeFluidEmission(current);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().distribution.empty());
    REQUIRE(migrated.value().prototype.collisionFilter == 0xffff0001u);
    REQUIRE(migrated.value().prototype.radii == glm::vec3(0.f));
    REQUIRE(migrated.value().prototype.orientation.w == 1.f);
    REQUIRE(migrated.value().prototype.actorGroup == 0);
    REQUIRE(migrated.value().prototype.selfCollide);
    REQUIRE(migrated.value().prototype.material.atmosphericPressure == 0.f);
    REQUIRE(migrated.value().prototype.material.smoothing == 2.f);
    REQUIRE(!migrated.value().prototype.material.rollingContacts);
    REQUIRE(migrated.value().prototype.material.rollingFriction == 0.f);
    REQUIRE(migrated.value().prototype.material.dynamicFriction == .2f);
    REQUIRE(migrated.value().prototype.material.staticFriction == .2f);
    REQUIRE(migrated.value().prototype.angularVelocity == glm::vec3(0.f));
    REQUIRE(migrated.value().granularRadiusRandomness == 0.f);
    REQUIRE(migrated.value().randomVelocity == 0.f);
    REQUIRE(migrated.value().useShapeColor);
    REQUIRE(migrated.value().actorCapacity == 1000);
    legacyDescription.set("unrecognized", 1);
    current.set("description", legacyDescription);
    REQUIRE(!decodeVolumeFluidEmission(current).ok());
}

TEST_CASE("fluids.volumeEmitter.distributionStreamVisitsEveryPoint") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmission e;
    e.shape        = VolumeFluidEmissionShape::Distribution;
    e.direction    = {0, 0, 1};
    e.distribution = {{{-.2f, 0, 0}, {1, 0, 0, 1}}, {{0, 0, 0}, {0, 1, 0, 1}}, {{.2f, 0, 0}, {0, 0, 1, 1}}};
    VolumeFluidEmitter emitter;
    for (int i = 0; i < 3; ++i) REQUIRE(emitter.advance(*solver, e, 1.f / 30.f, 30.f, 1, 0.f).ok());
    const auto p = solver->particles();
    REQUIRE(p.size() == 3);
    REQUIRE(p[0].position.x == -.2f);
    REQUIRE(p[1].position.x == 0.f);
    REQUIRE(p[2].position.x == .2f);
}

TEST_CASE("fluids.volumeEmitter.emptyImageDistribution") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmission e;
    e.shape = VolumeFluidEmissionShape::Distribution;
    VolumeFluidEmitter emitter;
    auto               result = emitter.advance(*solver, e, 1.f / 60.f, 100.f, 16);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 0);
    REQUIRE(emitVolumeFluidBurst(*solver, e, 0).ok());
    REQUIRE(!emitVolumeFluidBurst(*solver, e, 1).ok());
}

TEST_CASE("fluids.volumeEmitter.restoreContinuesExactSequence") {
    auto ar = VolumeFluid::create({}), br = VolumeFluid::create({});
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidEmission e;
    e.jitter = .1f;
    e.seed   = 57;
    VolumeFluidEmitter first, restored;
    REQUIRE(first.advance(*a, e, .017f, 73.f, 16).ok());
    auto encoded = encodeVolumeFluidEmitterState(first.snapshot());
    auto decoded = decodeVolumeFluidEmitterState(encoded);
    REQUIRE(decoded.ok());
    REQUIRE(restored.restore(decoded.value()).ok());
    REQUIRE(b->restore(a->snapshot()).ok());
    for (int i = 0; i < 20; ++i) {
        auto ra = first.advance(*a, e, .011f, 73.f, 16), rb = restored.advance(*b, e, .011f, 73.f, 16);
        REQUIRE(ra.ok());
        REQUIRE(rb.ok());
        REQUIRE(ra.value() == rb.value());
    }
    const auto pa = a->particles(), pb = b->particles();
    REQUIRE(pa.size() == pb.size());
    for (size_t i = 0; i < pa.size(); ++i) {
        REQUIRE(glm::length(pa[i].position - pb[i].position) == 0.f);
        REQUIRE(glm::length(pa[i].velocity - pb[i].velocity) == 0.f);
    }
    auto bad         = first.snapshot();
    bad.phase        = "nan";
    const auto saved = first.snapshot();
    REQUIRE(!first.restore(bad).ok());
    REQUIRE(first.snapshot().phase == saved.phase);
    bad      = saved;
    bad.kind = 1;
    REQUIRE(!first.restore(bad).ok());
    encoded.set("unexpected", 1);
    REQUIRE(!decodeVolumeFluidEmitterState(encoded).ok());
}

TEST_CASE("fluids.volumeEmitter.distributionJetEmitsWholeColoredLayers") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmission e;
    e.shape           = VolumeFluidEmissionShape::Distribution;
    e.origin          = {0, 1, 0};
    e.direction       = {0, 0, 1};
    e.speed           = 3.f;
    e.prototype.color = {.5f, .5f, .5f, 1.f};
    e.distribution    = {{{-.2f, 0, 0}, {1, 0, 0, 1}}, {{.2f, 0, 0}, {0, 1, 0, .5f}}};
    VolumeFluidJetEmitter jet;
    auto                  emitted = jet.advance(*solver, e, 1.f / 30.f, 4, 0.f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 2);
    const auto particles = solver->particles();
    REQUIRE(particles.size() == 2);
    REQUIRE(std::abs(particles[0].position.x + .2f) < 1e-6f);
    REQUIRE(std::abs(particles[1].position.x - .2f) < 1e-6f);
    REQUIRE(particles[0].color == glm::vec4(1, 0, 0, 1));
    REQUIRE(particles[1].color == glm::vec4(0, 1, 0, .5f));

    auto before = jet.snapshot();
    REQUIRE(!jet.advance(*solver, e, 1.f / 30.f, 1, 0.f).ok());
    auto after = jet.snapshot();
    REQUIRE(after.phase == before.phase);
    REQUIRE(after.sequence == before.sequence);
}

TEST_CASE("fluids.volumeEmitter.sphereAndCubeDistributionsMatchSurfaceDirections") {
    auto sphereVolume  = buildVolumeFluidSphereDistribution(.2f, .1f, false);
    auto sphereSurface = buildVolumeFluidSphereDistribution(.2f, .1f, true);
    auto cubeVolume    = buildVolumeFluidCubeDistribution({.2f, .3f, .4f}, .1f, false);
    auto cubeSurface   = buildVolumeFluidCubeDistribution({.2f, .3f, .4f}, .1f, true);
    REQUIRE(sphereVolume.ok());
    REQUIRE(sphereSurface.ok());
    REQUIRE(cubeVolume.ok());
    REQUIRE(cubeSurface.ok());
    REQUIRE(!sphereVolume.value().empty());
    REQUIRE(!sphereSurface.value().empty());
    REQUIRE(cubeVolume.value().size() == 60);
    REQUIRE(cubeSurface.value().size() == 54);
    for (const auto& point : sphereVolume.value()) REQUIRE(point.direction == glm::vec3(0, 0, 1));
    for (const auto& point : sphereSurface.value()) {
        REQUIRE(std::abs(glm::length(point.position) - .2f) < 1e-5f);
        REQUIRE(glm::dot(glm::normalize(point.position), point.direction) > .9999f);
    }
    for (const auto& point : cubeSurface.value()) REQUIRE(std::abs(glm::length(point.direction) - 1.f) < 1e-6f);
    REQUIRE(!buildVolumeFluidSphereDistribution(.2f, .0001f, false).ok());
    REQUIRE(!buildVolumeFluidCubeDistribution({1, 1, 1}, .001f, true).ok());
}

TEST_CASE("fluids.volumeEmitter.edgeRadialAndDiskEdgeDistributions") {
    auto edge = buildVolumeFluidEdgeDistribution(.5f, .1f, 30.f);
    REQUIRE(edge.ok());
    REQUIRE(edge.value().size() == 5);
    REQUIRE(std::abs(edge.value().front().position.x + .25f) < 1e-6f);
    REQUIRE(edge.value().front().direction == glm::vec3(0, 0, 1));
    REQUIRE(edge.value()[3].direction.y < -.9999f);
    REQUIRE(std::abs(edge.value()[3].direction.z) < 1e-5f);

    auto diskEdge   = buildVolumeFluidDiskDistribution(.2f, .1f, true);
    auto diskFilled = buildVolumeFluidDiskDistribution(.2f, .1f, false);
    REQUIRE(diskEdge.ok());
    REQUIRE(diskFilled.ok());
    REQUIRE(!diskEdge.value().empty());
    REQUIRE(diskFilled.value().size() > diskEdge.value().size());
    for (const auto& point : diskEdge.value()) {
        REQUIRE(std::abs(glm::length(point.position) - .2f) < 1e-5f);
        REQUIRE(glm::dot(glm::normalize(point.position), point.direction) > .9999f);
    }
    REQUIRE(diskFilled.value().front().position == glm::vec3(0));
    for (const auto& point : diskFilled.value()) REQUIRE(point.direction == glm::vec3(0, 0, 1));
    REQUIRE(!buildVolumeFluidEdgeDistribution(.5f, 0.f, 0.f).ok());
    REQUIRE(!buildVolumeFluidDiskDistribution(.2f, 0.f, false).ok());
    REQUIRE(!buildVolumeFluidEdgeDistribution(10000.f, .001f, 0.f).ok());
}

TEST_CASE("fluids.volumeEmitter.distributionDirectionsDriveBurstStreamAndJet") {
    VolumeFluidSettings settings;
    settings.gravity  = {0, 0, 0};
    settings.capacity = 32;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmission emission;
    emission.shape        = VolumeFluidEmissionShape::Distribution;
    emission.origin       = {0, 1, 0};
    emission.direction    = {0, 0, 1};
    emission.speed        = 6.f;
    emission.distribution = {{{0, 0, 0}, {1, 1, 1, 1}, {1, 0, 0}}, {{.1f, 0, 0}, {1, 1, 1, 1}, {0, 1, 0}}};
    REQUIRE(emitVolumeFluidBurst(*solver, emission, 2).ok());
    auto particles = solver->particles();
    REQUIRE(particles[0].velocity.x > 5.99f);
    REQUIRE(particles[1].velocity.y > 5.99f);
    solver->clear();
    VolumeFluidEmitter stream;
    REQUIRE(stream.advance(*solver, emission, .025f, 100.f, 2, 0.f).value() == 2);
    particles = solver->particles();
    REQUIRE(particles[0].velocity.x > 5.99f);
    REQUIRE(particles[1].velocity.y > 5.99f);
    solver->clear();
    VolumeFluidJetEmitter jet;
    REQUIRE(jet.advance(*solver, emission, 1.f / 30.f, 2, 0.f).value() == 2);
    particles = solver->particles();
    REQUIRE(particles[0].velocity.x > 5.99f);
    REQUIRE(particles[1].velocity.y > 5.99f);
}

TEST_CASE("fluids.volumeEmitter.Fluid3DUseShapeColorCoversBurstStreamAndJet") {
    VolumeFluidSettings settings;
    settings.gravity  = {0, 0, 0};
    settings.capacity = 32;
    VolumeFluidEmission emission;
    emission.shape           = VolumeFluidEmissionShape::Distribution;
    emission.direction       = {0, 0, 1};
    emission.speed           = 6.f;
    emission.prototype.color = {.8f, .6f, .4f, 1.f};
    emission.distribution    = {{{0, 0, 0}, {.5f, .25f, .1f, .5f}, {0, 0, 1}}};

    auto coloredResult = VolumeFluid::create(settings);
    REQUIRE(coloredResult.ok());
    auto colored = std::move(coloredResult).takeValue();
    REQUIRE(emitVolumeFluidBurst(*colored, emission, 1).value() == 1);
    REQUIRE(glm::length(colored->particles()[0].color - emission.distribution[0].color) < 1e-6f);

    auto coloredStreamResult = VolumeFluid::create(settings), coloredJetResult = VolumeFluid::create(settings);
    REQUIRE(coloredStreamResult.ok());
    REQUIRE(coloredJetResult.ok());
    auto               coloredStreamSolver = std::move(coloredStreamResult).takeValue();
    auto               coloredJetSolver    = std::move(coloredJetResult).takeValue();
    VolumeFluidEmitter coloredStream;
    REQUIRE(coloredStream.advance(*coloredStreamSolver, emission, .02f, 100.f, 1, 0.f).value() == 1);
    REQUIRE(glm::length(coloredStreamSolver->particles()[0].color - emission.distribution[0].color) < 1e-6f);
    VolumeFluidJetEmitter coloredJet;
    REQUIRE(coloredJet.advance(*coloredJetSolver, emission, 1.f / 30.f, 4, 0.f).value() > 0);
    for (const auto& particle : coloredJetSolver->particles())
        REQUIRE(glm::length(particle.color - emission.distribution[0].color) < 1e-6f);

    emission.useShapeColor = false;
    auto streamResult = VolumeFluid::create(settings), jetResult = VolumeFluid::create(settings);
    REQUIRE(streamResult.ok());
    REQUIRE(jetResult.ok());
    auto               streamSolver = std::move(streamResult).takeValue(), jetSolver = std::move(jetResult).takeValue();
    VolumeFluidEmitter stream;
    REQUIRE(stream.advance(*streamSolver, emission, .02f, 100.f, 1, 0.f).value() == 1);
    REQUIRE(glm::length(streamSolver->particles()[0].color - emission.prototype.color) < 1e-6f);
    VolumeFluidJetEmitter jet;
    REQUIRE(jet.advance(*jetSolver, emission, 1.f / 30.f, 4, 0.f).value() > 0);
    for (const auto& particle : jetSolver->particles())
        REQUIRE(glm::length(particle.color - emission.prototype.color) < 1e-6f);
}

TEST_CASE("fluids.volumeEmitter.descriptionV12AddsUseShapeColor") {
    auto encoded = encodeVolumeFluidEmission(VolumeFluidEmission{});
    encoded.set("version", 12);
    const auto& current = *encoded.find("description");
    eve::Value  legacy(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "useShapeColor" && key != "actorCapacity") legacy.set(key, *current.find(key));
    encoded.set("description", legacy);
    auto migrated = decodeVolumeFluidEmission(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().useShapeColor);
    legacy.set("useShapeColor", false);
    encoded.set("description", legacy);
    REQUIRE(!decodeVolumeFluidEmission(encoded).ok());
}

TEST_CASE("fluids.volumeEmitter.descriptionV9AddsDistributionDirection") {
    VolumeFluidEmission emission;
    emission.shape        = VolumeFluidEmissionShape::Distribution;
    emission.distribution = {{{.1f, .2f, .3f}, {.4f, .5f, .6f, .7f}, {1, 0, 0}}};
    auto legacy           = encodeVolumeFluidEmission(emission);
    legacy.set("version", 9);
    auto       currentDescription = *legacy.find("description");
    eve::Value description(eve::Value::Object{});
    for (const auto& key : currentDescription.keys())
        if (key != "randomVelocity" && key != "useShapeColor" && key != "actorCapacity")
            description.set(key, *currentDescription.find(key));
    removeV12MaterialFields(description);
    auto       distribution = *description.find("distribution");
    const auto point        = distribution.at(0);
    eve::Value legacyPoint(eve::Value::Object{});
    legacyPoint.set("position", *point.find("position"));
    legacyPoint.set("color", *point.find("color"));
    eve::Value::Array points;
    points.push_back(std::move(legacyPoint));
    description.set("distribution", eve::Value(std::move(points)));
    legacy.set("description", description);
    auto migrated = decodeVolumeFluidEmission(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().distribution[0].direction == glm::vec3(0, 0, 1));
    REQUIRE(migrated.value().randomVelocity == 0.f);
    REQUIRE(migrated.value().useShapeColor);
    REQUIRE(encodeVolumeFluidEmission(emission).find("version")->asInt() == 14);
}

TEST_CASE("fluids.volumeEmitter.Fluid3DRandomVelocityBlendAndV10Migration") {
    VolumeFluidSettings settings;
    settings.gravity  = {0, 0, 0};
    settings.capacity = 8;
    auto ar = VolumeFluid::create(settings), br = VolumeFluid::create(settings);
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidEmission emission;
    emission.direction      = {0, 0, 1};
    emission.speed          = 2;
    emission.seed           = 77;
    emission.randomVelocity = 1;
    REQUIRE(emitVolumeFluidBurst(*a, emission, 1).ok());
    const auto randomized = a->particles()[0].velocity;
    REQUIRE(std::abs(glm::length(randomized) - 2.f) < 1e-5f);
    REQUIRE(glm::length(randomized - glm::vec3(0, 0, 2)) > .01f);
    emission.randomVelocity = 0;
    REQUIRE(emitVolumeFluidBurst(*b, emission, 1).ok());
    REQUIRE(glm::length(b->particles()[0].velocity - glm::vec3(0, 0, 2)) < 1e-6f);
    auto encoded = encodeVolumeFluidEmission(emission);
    encoded.set("version", 10);
    auto       current = *encoded.find("description");
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "randomVelocity" && key != "useShapeColor" && key != "actorCapacity")
            legacy.set(key, *current.find(key));
    removeV12MaterialFields(legacy);
    encoded.set("description", std::move(legacy));
    auto migrated = decodeVolumeFluidEmission(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().randomVelocity == 0.f);
    REQUIRE(migrated.value().useShapeColor);
    emission.randomVelocity = 1.01f;
    REQUIRE(!emitVolumeFluidBurst(*a, emission, 1).ok());
    REQUIRE(a->particleCount() == 1);
}

TEST_CASE("fluids.volumeEmitter.jetRestorePreservesLayerPhase") {
    auto ar = VolumeFluid::create({}), br = VolumeFluid::create({});
    REQUIRE(ar.ok());
    REQUIRE(br.ok());
    auto                  a = std::move(ar).takeValue(), b = std::move(br).takeValue();
    VolumeFluidJetEmitter first, restored;
    VolumeFluidEmission   e;
    e.speed = 3.f;
    REQUIRE(first.advance(*a, e, .017f, 16).ok());
    auto decoded = decodeVolumeFluidEmitterState(encodeVolumeFluidEmitterState(first.snapshot()));
    REQUIRE(decoded.ok());
    REQUIRE(restored.restore(decoded.value()).ok());
    REQUIRE(first.advance(*a, e, .021f, 16).ok());
    REQUIRE(restored.advance(*b, e, .021f, 16).ok());
    REQUIRE(a->particleCount() == b->particleCount());
    REQUIRE(a->particleCount() == 5);
    REQUIRE(glm::length(a->particles()[0].position - b->particles()[0].position) == 0.f);
}

TEST_CASE("fluids.volumeEmitter.movingNozzleSamplesEmissionTime") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidJetEmitter jet;
    VolumeFluidEmission   e;
    e.shape  = VolumeFluidEmissionShape::Edge;
    e.extent = {0, 0, 0};
    e.speed  = 12.f;
    VolumeFluidNozzlePose begin{{0, 1, 0}, {0, 0, 0, 1}}, end{{.3f, 1, 0}, {0, 0, 0, 1}};
    auto                  result = jet.advanceMoving(*solver, e, begin, end, .025f, 16, 0.f, 0.f);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 3);
    const auto particles = solver->particles();
    REQUIRE(std::abs(particles[0].position.x - .3f) < 1e-5f);
    REQUIRE(std::abs(particles[1].position.x - .2f) < 1e-5f);
    REQUIRE(std::abs(particles[2].position.x - .1f) < 1e-5f);
    REQUIRE(std::abs(particles[2].position.z - .2f) < 1e-5f);
}
}  // namespace test_fluids_volume_emitter
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_emitter_part2)
#include "PrivateTestScope.h"
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidEmitter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_emitter_part2.cpp
namespace test_fluids_volume_emitter_part2 {
// fluids_volume_emitter_part2.cpp
using namespace eve::fluids;

TEST_CASE("fluids.volumeEmitter.movingNozzleInheritsVelocityAndRejectsBadPose") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidJetEmitter jet;
    VolumeFluidEmission   e;
    e.shape  = VolumeFluidEmissionShape::Edge;
    e.extent = {0, 0, 0};
    e.speed  = 12.f;
    VolumeFluidNozzlePose begin{{0, 1, 0}, {0, 0, 0, 1}}, end{{.3f, 1, 0}, {0, 0, 0, 1}};
    auto                  bad = end;
    bad.rotation              = {0, 0, 0, 0};
    const auto saved          = jet.snapshot();
    REQUIRE(!jet.advanceMoving(*solver, e, begin, bad, .025f, 16, 0.f, 1.f).ok());
    REQUIRE(jet.snapshot().phase == saved.phase);
    REQUIRE(solver->particleCount() == 0);
    REQUIRE(jet.advanceMoving(*solver, e, begin, end, .025f, 16, 0.f, 1.f).ok());
    for (const auto& p : solver->particles()) {
        REQUIRE(std::abs(p.position.x - .3f) < 1e-5f);
        REQUIRE(std::abs(p.velocity.x - 12.f) < 1e-5f);
    }
}

TEST_CASE("fluids.volumeEmitter.rotatingNozzleInterpolatesOrientation") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidJetEmitter jet;
    VolumeFluidEmission   e;
    e.shape  = VolumeFluidEmissionShape::Edge;
    e.extent = {0, 0, 0};
    e.speed  = 6.f;
    VolumeFluidNozzlePose begin{{0, 1, 0}, {0, 0, 0, 1}}, end{{0, 1, 0}, {0, .70710678118f, 0, .70710678118f}};
    auto                  result = jet.advanceMoving(*solver, e, begin, end, .025f, 16, 0.f, 0.f);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 1);
    const auto p = solver->particles()[0];
    REQUIRE(std::abs(p.velocity.x - 5.1961524f) < .0001f);
    REQUIRE(std::abs(p.velocity.z - 3.f) < .0001f);
    REQUIRE(std::abs(p.position.x - .04330127f) < .0001f);
}

TEST_CASE("fluids.volumeEmitter.rotatingNozzleSurfaceVelocity") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                  solver = std::move(created).takeValue();
    VolumeFluidJetEmitter jet;
    VolumeFluidEmission   e;
    e.shape  = VolumeFluidEmissionShape::Edge;
    e.extent = {.1f, 0, 0};
    e.speed  = 6.f;
    VolumeFluidNozzlePose begin{{0, 1, 0}, {0, 0, 0, 1}}, end{{0, 1, 0}, {0, .70710678118f, 0, .70710678118f}};
    auto                  result = jet.advanceMoving(*solver, e, begin, end, .025f, 16, 0.f, 1.f);
    REQUIRE(result.ok());
    REQUIRE(result.value() == 3);
    const auto particles = solver->particles();
    const auto tangent   = particles[2].velocity - particles[1].velocity;
    REQUIRE(std::abs(glm::length(tangent) - 6.2831853f) < .001f);
    REQUIRE(std::abs(tangent.x + 5.441398f) < .001f);
    REQUIRE(std::abs(tangent.z + 3.1415927f) < .001f);
}

TEST_CASE("fluids.volumeEmitter.nozzleRotatesEllipsoidOrientation") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmission emission;
    emission.shape           = VolumeFluidEmissionShape::Disk;
    emission.origin          = {0, 1, 0};
    emission.direction       = {0, 1, 0};
    emission.extent          = {0, 0, 0};
    emission.prototype.radii = {.2f, .05f, .05f};
    REQUIRE(emitVolumeFluidBurst(*solver, emission, 1).ok());
    auto majorAxis = solver->querySphere({0.f, 1.f, .3f}, 0.f, 0.f, 1.f, 1);
    REQUIRE(majorAxis.ok());
    REQUIRE(majorAxis.value().size() == 1);
    REQUIRE(std::abs(majorAxis.value()[0].distance - .1f) < 1e-5f);
    solver->clear();
    VolumeFluidJetEmitter jet;
    VolumeFluidNozzlePose pose;
    pose.position      = {0, 1, 0};
    pose.rotation      = {0, 0, .70710678f, .70710678f};
    emission.direction = {0, 0, 1};
    emission.speed     = 12.f;
    REQUIRE(jet.advanceMoving(*solver, emission, pose, pose, 1.f / 60.f, 1, 0.f, 0.f).ok());
    REQUIRE(solver->particleCount() == 1);
    const auto particle = solver->particles()[0];
    REQUIRE(std::abs(particle.orientation.z) > .7f);
}

TEST_CASE("fluids.volumeEmitter.boundedLifecycleEventsCoverEmissionExplicitKillAndExpiry") {
    VolumeFluidSettings settings;
    settings.gravity  = {0, 0, 0};
    settings.capacity = 8;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();

    VolumeFluidParticle first, second, third;
    first.actorGroup  = 7;
    first.position    = {0, 1, 0};
    first.color       = {.1f, .2f, .3f, 1.f};
    second            = first;
    second.position.x = .2f;
    second.life       = .01f;
    third             = first;
    third.position.x  = .4f;
    third.actorGroup  = 9;

    REQUIRE(solver->emit(std::array{first}).ok());
    REQUIRE(solver->drainParticleEvents().events.empty());
    REQUIRE(!solver->configureParticleEvents(65537).ok());
    REQUIRE(solver->configureParticleEvents(3).ok());
    REQUIRE(solver->emit(std::array{second, third}).ok());
    auto emitted = solver->drainParticleEvents();
    REQUIRE(emitted.dropped == 0);
    REQUIRE(emitted.events.size() == 2);
    REQUIRE(emitted.events[0].type == VolumeFluidParticleEventType::Emitted);
    REQUIRE(emitted.events[0].particleIndex == 1);
    REQUIRE(emitted.events[0].actorGroup == 7);
    REQUIRE(glm::length(emitted.events[0].particle.color - first.color) < 1e-6f);

    REQUIRE(solver->killParticle(2).ok());
    REQUIRE(solver->step(.01f, 2).ok());
    auto killed = solver->drainParticleEvents();
    REQUIRE(killed.dropped == 0);
    REQUIRE(killed.events.size() == 2);
    REQUIRE(killed.events[0].type == VolumeFluidParticleEventType::Killed);
    REQUIRE(killed.events[0].actorGroup == 9);
    REQUIRE(killed.events[1].type == VolumeFluidParticleEventType::Killed);
    REQUIRE(killed.events[1].actorGroup == 7);

    REQUIRE(solver->emit(std::array{second, second, second, second}).ok());
    auto overflow = solver->drainParticleEvents();
    REQUIRE(overflow.events.size() == 3);
    REQUIRE(overflow.dropped == 1);
    REQUIRE(solver->killActorParticles(7).value() == 5);
    auto killedActor = solver->drainParticleEvents();
    REQUIRE(killedActor.events.size() == 3);
    REQUIRE(killedActor.dropped == 2);
    for (const auto& event : killedActor.events) REQUIRE(event.type == VolumeFluidParticleEventType::Killed);
    REQUIRE(solver->configureParticleEvents(0).ok());
    REQUIRE(solver->emit(std::array{first}).ok());
    REQUIRE(solver->killActorParticles(7).ok());
    REQUIRE(solver->drainParticleEvents().events.empty());
}

TEST_CASE("fluids.volumeEmitter.combinedCheckpointRestoresSolverDescriptionAndControllerAtomically") {
    VolumeFluidSettings settings;
    settings.gravity  = {0, 0, 0};
    settings.capacity = 8;
    auto sourceResult = VolumeFluid::create(settings), targetResult = VolumeFluid::create(settings);
    REQUIRE(sourceResult.ok());
    REQUIRE(targetResult.ok());
    auto                source = std::move(sourceResult).takeValue(), target = std::move(targetResult).takeValue();
    VolumeFluidEmission emission;
    emission.shape                = VolumeFluidEmissionShape::Edge;
    emission.extent               = {0, 0, 0};
    emission.speed                = 3.f;
    emission.seed                 = 47;
    emission.prototype.actorGroup = 5;
    emission.prototype.color      = {.2f, .4f, .6f, 1.f};
    VolumeFluidEmitter sourceController, targetController;
    REQUIRE(sourceController.advance(*source, emission, .02f, 120.f, 2, 0.f).value() == 2);
    auto checkpoint = captureVolumeFluidEmitterCheckpoint(*source, emission, sourceController);
    auto encoded    = encodeVolumeFluidEmitterCheckpoint(checkpoint);
    auto decoded    = decodeVolumeFluidEmitterCheckpoint(encoded);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().emission.seed == 47);

    VolumeFluidEmission other  = emission;
    other.prototype.actorGroup = 9;
    REQUIRE(targetController.advance(*target, other, .02f, 60.f, 1, 0.f).value() == 1);
    const auto beforeParticle   = target->particles()[0];
    const auto beforeController = targetController.snapshot();
    auto       invalid          = decoded.value();
    invalid.controller.kind     = 1;
    REQUIRE(!restoreVolumeFluidEmitterCheckpoint(*target, targetController, invalid).ok());
    REQUIRE(target->particleCount() == 1);
    REQUIRE(target->particles()[0].actorGroup == beforeParticle.actorGroup);
    REQUIRE(targetController.snapshot().phase == beforeController.phase);
    REQUIRE(targetController.snapshot().sequence == beforeController.sequence);

    REQUIRE(restoreVolumeFluidEmitterCheckpoint(*target, targetController, decoded.value()).ok());
    REQUIRE(target->particleCount() == source->particleCount());
    REQUIRE(target->particles()[0].actorGroup == 5);
    REQUIRE(glm::length(target->particles()[0].color - emission.prototype.color) < 1e-6f);
    REQUIRE(targetController.snapshot().phase == sourceController.snapshot().phase);
    REQUIRE(targetController.snapshot().sequence == sourceController.snapshot().sequence);
    REQUIRE(sourceController.advance(*source, emission, .01f, 100.f, 2, 0.f).ok());
    REQUIRE(targetController.advance(*target, decoded.value().emission, .01f, 100.f, 2, 0.f).ok());
    REQUIRE(target->particleCount() == source->particleCount());

    encoded.set("extra", 1);
    REQUIRE(!decodeVolumeFluidEmitterCheckpoint(encoded).ok());
}

TEST_CASE("fluids.volumeEmitter.activeParticleCountReadsOnlyItsActorWithoutMassWork") {
    VolumeFluidSettings settings;
    settings.gravity  = {0, 0, 0};
    settings.capacity = 8;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle a, b;
    a.position   = {0, 1, 0};
    a.actorGroup = 4;
    b            = a;
    b.position.x = .2f;
    b.actorGroup = 8;
    REQUIRE(solver->emit(std::array{a, a, b}).ok());
    REQUIRE(solver->actorParticleCount(4).value() == 2);
    REQUIRE(solver->actorParticleCount(8).value() == 1);
    REQUIRE(solver->actorParticleCount(9).value() == 0);
    REQUIRE(!solver->actorParticleCount(0x01000000u).ok());
    REQUIRE(solver->particleCount() == 3);
    REQUIRE(solver->killParticle(0).ok());
    REQUIRE(solver->actorParticleCount(4).value() == 1);
}

TEST_CASE("fluids.volumeEmitter.actorCapacityIsIndependentAndPreflightsAllControllers") {
    auto created = VolumeFluid::create(VolumeFluidSettings{.capacity = 12});
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();

    VolumeFluidEmission a;
    a.shape                = VolumeFluidEmissionShape::Edge;
    a.extent               = glm::vec3(0.f);
    a.prototype.actorGroup = 4;
    a.actorCapacity        = 2;
    VolumeFluidEmission b  = a;
    b.prototype.actorGroup = 8;
    VolumeFluidEmitter streamA, streamB;

    REQUIRE(streamA.advance(*solver, a, .02f, 1000.f, 8, 0.f).value() == 2);
    REQUIRE(streamA.advance(*solver, a, .02f, 1000.f, 8, 0.f).value() == 0);
    REQUIRE(streamB.advance(*solver, b, .02f, 1000.f, 8, 0.f).value() == 2);
    REQUIRE(solver->actorParticleCount(4).value() == 2);
    REQUIRE(solver->actorParticleCount(8).value() == 2);
    REQUIRE(solver->particleCount() == 4);

    auto burst                 = a;
    burst.prototype.actorGroup = 12;
    burst.actorCapacity        = 1;
    const auto before          = solver->snapshot();
    REQUIRE(!emitVolumeFluidBurst(*solver, burst, 2).ok());
    REQUIRE(solver->snapshot().particles.size() == before.particles.size());

    auto jetEmission                 = a;
    jetEmission.prototype.actorGroup = 16;
    jetEmission.actorCapacity        = 2;
    jetEmission.speed                = 10.f;
    VolumeFluidJetEmitter jet;
    REQUIRE(jet.advance(*solver, jetEmission, 1.f / 30.f, 8, 0.f).value() == 2);
    REQUIRE(jet.advance(*solver, jetEmission, 1.f / 30.f, 8, 0.f).value() == 0);
    REQUIRE(solver->actorParticleCount(16).value() == 2);
}

TEST_CASE("fluids.volumeEmitter.descriptionV13MigratesActorCapacityStrictly") {
    VolumeFluidEmission emission;
    emission.actorCapacity = 37;
    auto encoded           = encodeVolumeFluidEmission(emission);
    REQUIRE(encoded.find("version")->asInt() == 14);
    REQUIRE(decodeVolumeFluidEmission(encoded).value().actorCapacity == 37);

    eve::Value  legacyDescription(eve::Value::Object{});
    const auto& currentDescription = *encoded.find("description");
    for (const auto& key : currentDescription.keys())
        if (key != "actorCapacity") legacyDescription.set(key, *currentDescription.find(key));
    encoded.set("version", 13);
    encoded.set("description", legacyDescription);
    auto migrated = decodeVolumeFluidEmission(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().actorCapacity == 1000);

    auto invalidDescription = *encodeVolumeFluidEmission(emission).find("description");
    invalidDescription.set("actorCapacity", 0);
    encoded = encodeVolumeFluidEmission(emission);
    encoded.set("description", invalidDescription);
    REQUIRE(!decodeVolumeFluidEmission(encoded).ok());
}

TEST_CASE("fluids.volumeEmitter.killParticleUsesActorLocalIndexWithoutCrossActorDeletion") {
    auto created = VolumeFluid::create(VolumeFluidSettings{.capacity = 8});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].actorGroup = 4;
    particles[0].position   = {-.2f, 1.f, 0.f};
    particles[0].color      = {1, 0, 0, 1};
    particles[1].actorGroup = 8;
    particles[1].position   = {0.f, 1.f, 0.f};
    particles[1].color      = {0, 1, 0, 1};
    particles[2].actorGroup = 4;
    particles[2].position   = {.2f, 1.f, 0.f};
    particles[2].color      = {0, 0, 1, 1};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->configureParticleEvents(4).ok());
    const auto emittedEvents = solver->drainParticleEvents();
    REQUIRE(emittedEvents.events.empty());

    REQUIRE(solver->killActorParticle(4, 1).ok());
    REQUIRE(solver->particleCount() == 2);
    REQUIRE(solver->actorParticleCount(4).value() == 1);
    REQUIRE(solver->actorParticleCount(8).value() == 1);
    REQUIRE(solver->particles()[0].color == glm::vec4(1, 0, 0, 1));
    REQUIRE(solver->particles()[1].color == glm::vec4(0, 1, 0, 1));
    auto events = solver->drainParticleEvents();
    REQUIRE(events.events.size() == 1);
    REQUIRE(events.events[0].actorGroup == 4);
    REQUIRE(events.events[0].particle.color == glm::vec4(0, 0, 1, 1));

    const auto before = solver->snapshot();
    REQUIRE(!solver->killActorParticle(4, 1).ok());
    REQUIRE(!solver->killActorParticle(0x01000000u, 0).ok());
    REQUIRE(solver->snapshot().particles.size() == before.particles.size());
}

TEST_CASE("fluids.volumeEmitter.emitParticleUsesDistributionSequenceOffsetAndCapacity") {
    auto created = VolumeFluid::create(VolumeFluidSettings{.capacity = 8});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidEmission emission;
    emission.shape                = VolumeFluidEmissionShape::Distribution;
    emission.origin               = {0.f, 1.f, 0.f};
    emission.direction            = {0.f, 0.f, 1.f};
    emission.speed                = 3.f;
    emission.actorCapacity        = 2;
    emission.prototype.actorGroup = 19;
    emission.prototype.color      = glm::vec4(1.f);
    emission.distribution = {{{.1f, 0.f, 0.f}, {1, 0, 0, 1}, {1, 0, 0}}, {{0.f, .1f, 0.f}, {0, 1, 0, 1}, {0, 1, 0}}};
    VolumeFluidEmitter emitter;

    REQUIRE(emitter.emitParticle(*solver, emission, .5f, .02f).value() == 1);
    REQUIRE(emitter.emitParticle(*solver, emission, 1.f, .02f).value() == 1);
    REQUIRE(emitter.emitParticle(*solver, emission, 0.f, .02f).value() == 0);
    REQUIRE(glm::length(solver->particles()[0].position - glm::vec3(.13f, 1.f, 0.f)) < 1e-6f);
    REQUIRE(glm::length(solver->particles()[1].position - glm::vec3(0.f, 1.16f, 0.f)) < 1e-6f);
    REQUIRE(solver->particles()[0].color == glm::vec4(1, 0, 0, 1));
    REQUIRE(solver->particles()[1].color == glm::vec4(0, 1, 0, 1));

    const auto before           = solver->snapshot();
    const auto controllerBefore = emitter.snapshot();
    REQUIRE(!emitter.emitParticle(*solver, emission, -0.1f, .02f).ok());
    REQUIRE(!emitter.emitParticle(*solver, emission, .5f, .04f).ok());
    REQUIRE(solver->snapshot().particles.size() == before.particles.size());
    REQUIRE(emitter.snapshot().sequence == controllerBefore.sequence);
}

TEST_CASE("fluids.volumeEmitter.blueprintMetricsMatchFluid3DThreeDimensionalFormulas") {
    auto defaults = evaluateVolumeFluidEmitterBlueprint3D(1.f, 1000.f, 2.f);
    REQUIRE(defaults.ok());
    REQUIRE(std::abs(defaults.value().particleSize - .1f) < 1e-7f);
    REQUIRE(std::abs(defaults.value().particleMass - 1.f) < 1e-6f);
    REQUIRE(std::abs(defaults.value().smoothingRadius - .2f) < 1e-7f);

    auto denser = evaluateVolumeFluidEmitterBlueprint3D(8.f, 1000.f, 2.f);
    REQUIRE(denser.ok());
    REQUIRE(std::abs(denser.value().particleSize - .05f) < 1e-7f);
    REQUIRE(std::abs(denser.value().particleMass - .125f) < 1e-6f);
    REQUIRE(std::abs(denser.value().smoothingRadius - .1f) < 1e-7f);

    REQUIRE(!evaluateVolumeFluidEmitterBlueprint3D(0.f, 1000.f, 2.f).ok());
    REQUIRE(!evaluateVolumeFluidEmitterBlueprint3D(1.f, 0.f, 2.f).ok());
    REQUIRE(!evaluateVolumeFluidEmitterBlueprint3D(1.f, 1000.f, .99f).ok());
    REQUIRE(!evaluateVolumeFluidEmitterBlueprint3D(std::numeric_limits<float>::quiet_NaN(), 1000.f, 2.f).ok());
}

TEST_CASE("fluids.volumeEmitter.fluidBlueprintPreparesAllFluid3DMaterialFieldsAtomically") {
    VolumeFluidEmitterBlueprint3D blueprint;
    blueprint.capacity            = 321;
    blueprint.resolution          = 8.f;
    blueprint.restDensity         = 875.f;
    blueprint.smoothing           = 3.f;
    blueprint.viscosity           = .7f;
    blueprint.surfaceTension      = 1.5f;
    blueprint.buoyancy            = -.25f;
    blueprint.atmosphericDrag     = .4f;
    blueprint.atmosphericPressure = 2.5f;
    blueprint.vorticity           = .8f;
    blueprint.diffusion           = .6f;
    blueprint.diffusionData       = {1.f, 2.f, 3.f, 4.f};
    auto prepared                 = prepareVolumeFluidEmitterBlueprint3D(blueprint);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.value().settings.capacity == 321);
    REQUIRE(std::abs(prepared.value().settings.spacing - .05f) < 1e-7f);
    REQUIRE(prepared.value().emission.actorCapacity == 321);
    const auto& particle = prepared.value().emission.prototype;
    REQUIRE(particle.material.phase == VolumeFluidPhase::Liquid);
    REQUIRE(particle.material.density == 875.f);
    REQUIRE(particle.material.smoothing == 3.f);
    REQUIRE(particle.material.viscosity == .7f);
    REQUIRE(particle.material.cohesion == 1.5f);
    REQUIRE(particle.material.buoyancy == -.25f);
    REQUIRE(particle.material.drag == .4f);
    REQUIRE(particle.material.atmosphericPressure == 2.5f);
    REQUIRE(particle.material.vorticity == .8f);
    REQUIRE(particle.material.diffusion == .6f);
    REQUIRE(particle.data == glm::vec4(1.f, 2.f, 3.f, 4.f));
    auto encoded = encodeVolumeFluidEmitterBlueprint3D(blueprint);
    auto decoded = decodeVolumeFluidEmitterBlueprint3D(encoded);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().capacity == 321);
    REQUIRE(decoded.value().diffusionData == glm::vec4(1.f, 2.f, 3.f, 4.f));
    encoded.set("futureField", 1.0);
    REQUIRE(!decodeVolumeFluidEmitterBlueprint3D(encoded).ok());
    blueprint.atmosphericDrag = -1.f;
    REQUIRE(!prepareVolumeFluidEmitterBlueprint3D(blueprint).ok());
}

TEST_CASE("fluids.volumeEmitter.granularBlueprintPreparesCapacityRadiusAndDensity") {
    VolumeGranularEmitterBlueprint3D blueprint;
    blueprint.capacity    = 64;
    blueprint.resolution  = 8.f;
    blueprint.restDensity = 1450.f;
    blueprint.randomness  = 35.f;
    auto prepared         = prepareVolumeGranularEmitterBlueprint3D(blueprint);
    REQUIRE(prepared.ok());
    REQUIRE(prepared.value().settings.capacity == 64);
    REQUIRE(std::abs(prepared.value().settings.spacing - .05f) < 1e-7f);
    REQUIRE(prepared.value().emission.actorCapacity == 64);
    REQUIRE(prepared.value().emission.granularRadiusRandomness == 35.f);
    REQUIRE(prepared.value().emission.prototype.material.phase == VolumeFluidPhase::Granular);
    REQUIRE(prepared.value().emission.prototype.material.density == 1450.f);
    REQUIRE(prepared.value().metrics.smoothingRadius == 0.f);
    auto encoded = encodeVolumeGranularEmitterBlueprint3D(blueprint);
    REQUIRE(decodeVolumeGranularEmitterBlueprint3D(encoded).ok());
    encoded.set("futureField", int64_t(1));
    REQUIRE(!decodeVolumeGranularEmitterBlueprint3D(encoded).ok());
    blueprint.randomness = 100.01f;
    REQUIRE(!prepareVolumeGranularEmitterBlueprint3D(blueprint).ok());
}

// fluids_volume_foam.cpp
using namespace eve::fluids;
namespace {
std::unique_ptr<VolumeFluid> shear() {
    auto created = VolumeFluid::create({});
    if (!created) return {};
    auto                             fluid = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(2);
    particles[0].position = {-.05f, 1.f, 0.f};
    particles[0].velocity = {0.f, -1.f, 0.f};
    particles[1].position = {.05f, 1.f, 0.f};
    particles[1].velocity = {0.f, 1.f, 0.f};
    if (!fluid->emit(particles)) return {};
    return fluid;
}
}  // namespace
TEST_CASE("fluids.volume.foam.thresholdsBudgetAndReplay") {
    auto fluid = shear();
    REQUIRE(bool(fluid));
    VolumeFluidFoam foam;
    auto            state             = foam.snapshot();
    state.settings.rate               = 100.f;
    state.settings.vorticityThreshold = 0.f;
    state.settings.densityThreshold   = 1e9f;
    state.settings.randomness         = .01f;
    state.settings.maxPerStep         = 1;
    REQUIRE(foam.restore(state).ok());
    VolumeFluidDiffuse pool;
    auto               emitted = foam.advance(*fluid, pool, .02f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    const auto  p       = pool.snapshot().particles[0];
    const float nearest = std::min(glm::length(p.position - glm::vec3(-.05f, 1.f, 0.f)),
                                   glm::length(p.position - glm::vec3(.05f, 1.f, 0.f)));
    REQUIRE(nearest <= .010001f);
    REQUIRE(p.life == 2.f);
    VolumeFluidFoam    replay;
    VolumeFluidDiffuse replayPool;
    REQUIRE(replay.restore(state).ok());
    REQUIRE(replay.advance(*fluid, replayPool, .02f).ok());
    REQUIRE(replayPool.snapshot().particles[0].position == p.position);
    REQUIRE(replay.snapshot().randomState == foam.snapshot().randomState);
    REQUIRE(fluid->particles()[0].position.x == -.05f);
    state.settings.densityThreshold = 0.f;
    REQUIRE(foam.restore(state).ok());
    REQUIRE(foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(pool.particleCount() == 1);
    REQUIRE(foam.snapshot().randomState == state.randomState);
    state.settings.densityThreshold   = 1e9f;
    state.settings.vorticityThreshold = 1e6f;
    REQUIRE(foam.restore(state).ok());
    REQUIRE(foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(pool.particleCount() == 1);
}
TEST_CASE("fluids.volume.foam.creditCapacityAndStrictRestore") {
    auto fluid = shear();
    REQUIRE(bool(fluid));
    VolumeFluidFoam foam;
    auto            state             = foam.snapshot();
    state.settings.rate               = 1.f;
    state.credit                      = "0.99";
    state.settings.vorticityThreshold = 0.f;
    state.settings.densityThreshold   = 1e9f;
    REQUIRE(foam.restore(state).ok());
    VolumeFluidDiffuse pool;
    auto               capacity = pool.snapshot();
    capacity.capacity           = 1;
    REQUIRE(pool.restore(capacity).ok());
    auto emitted = foam.advance(*fluid, pool, .02f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    auto saved = foam.snapshot();
    REQUIRE(std::abs(std::stod(saved.credit) - .01) < 1e-8);
    REQUIRE(decodeVolumeFluidFoam(encodeVolumeFluidFoam(saved)).ok());
    auto encoded = encodeVolumeFluidFoam(saved);
    encoded.set("unknown", 3);
    REQUIRE(!decodeVolumeFluidFoam(encoded).ok());
    state.credit = "1";
    REQUIRE(!foam.restore(state).ok());
    state.credit      = "0";
    state.randomState = 0;
    REQUIRE(!foam.restore(state).ok());
    REQUIRE(foam.snapshot().credit == saved.credit);
    REQUIRE(foam.snapshot().randomState == saved.randomState);
    REQUIRE(!foam.advance(*fluid, pool, -.01f).ok());
    REQUIRE(foam.snapshot().credit == saved.credit);
    saved.credit = "0.99";
    REQUIRE(foam.restore(saved).ok());
    emitted = foam.advance(*fluid, pool, .02f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 0);
    REQUIRE(foam.snapshot().randomState == saved.randomState);
    pool.clear();
    emitted = foam.advance(*fluid, pool, 0.f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 0);
}
TEST_CASE("fluids.volume.foam.queryFailureRollback") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             fluid = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(2001);
    for (auto& p : particles) p.position = {0.f, 1.f, 0.f};
    REQUIRE(fluid->emit(particles).ok());
    VolumeFluidFoam    foam;
    VolumeFluidDiffuse pool;
    auto               state = foam.snapshot();
    REQUIRE(!foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(foam.snapshot().credit == state.credit);
    REQUIRE(foam.snapshot().randomState == state.randomState);
    REQUIRE(pool.particleCount() == 0);
    state.settings.rate = 0.f;
    REQUIRE(foam.restore(state).ok());
    REQUIRE(foam.advance(*fluid, pool, .02f).ok());
    REQUIRE(pool.particleCount() == 0);
}

TEST_CASE("fluids.volume.foam.actorSourceMatchesEmitterOwnership") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             fluid = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(4);
    for (size_t i = 0; i < particles.size(); ++i) {
        const bool right        = i >= 2;
        const bool upper        = (i % 2) == 1;
        particles[i].position   = {right ? .55f : -.55f, upper ? 1.05f : .95f, 0.f};
        particles[i].velocity   = {upper ? 1.f : -1.f, 0.f, 0.f};
        particles[i].actorGroup = right ? 9u : 3u;
    }
    REQUIRE(fluid->emit(particles).ok());
    VolumeFluidFoam foam;
    auto            state             = foam.snapshot();
    state.settings.rate               = 100.f;
    state.settings.randomness         = 0.f;
    state.settings.vorticityThreshold = 0.f;
    state.settings.densityThreshold   = 1e9f;
    state.settings.maxPerStep         = 1;
    REQUIRE(foam.restore(state).ok());
    VolumeFluidDiffuse pool;
    auto               emitted = foam.advanceFromActor(*fluid, pool, .02f, 9);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    REQUIRE(pool.particleCount() == 1);
    REQUIRE(pool.snapshot().particles[0].position.x > .4f);

    REQUIRE(fluid->step(1.f / 120.f, 1).ok());
    VolumeFluidFoam interpolated;
    REQUIRE(interpolated.restore(state).ok());
    VolumeFluidDiffuse interpolatedPool;
    emitted = interpolated.advanceFromActorInterpolated(*fluid, interpolatedPool, .02f, 9, 0.f);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 1);
    REQUIRE(std::abs(interpolatedPool.snapshot().particles[0].position.x - .55f) < 1e-5f);
    const auto interpolationState = interpolated.snapshot();
    REQUIRE(!interpolated.advanceFromActorInterpolated(*fluid, interpolatedPool, .02f, 9, -.01f).ok());
    REQUIRE(interpolated.snapshot().credit == interpolationState.credit);
    REQUIRE(interpolated.snapshot().randomState == interpolationState.randomState);

    VolumeFluidFoam absent;
    REQUIRE(absent.restore(state).ok());
    VolumeFluidDiffuse absentPool;
    const auto         before = absent.snapshot();
    emitted                   = absent.advanceFromActor(*fluid, absentPool, .02f, 77);
    REQUIRE(emitted.ok());
    REQUIRE(emitted.value() == 0);
    REQUIRE(absentPool.particleCount() == 0);
    REQUIRE(absent.snapshot().randomState == before.randomState);
    REQUIRE(!absent.advanceFromActor(*fluid, absentPool, -.01f, 3).ok());
}

// fluids_volume_grid_debug.cpp
using namespace eve::fluids;

TEST_CASE("fluids.volume.gridDebuggerReturnsOnlyStableOccupiedCells") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-1.90f, .10f, -.90f};
    particles[1].position = {-1.85f, .15f, -.85f};
    particles[2].position = {-1.50f, .10f, -.90f};
    REQUIRE(solver->emit(particles).ok());

    auto cells = solver->debugParticleGrid(2);
    REQUIRE(cells.ok());
    REQUIRE(cells.value().size() == 2);
    REQUIRE(cells.value()[0].particleCount == 2);
    REQUIRE(cells.value()[1].particleCount == 1);
    REQUIRE(glm::length(cells.value()[0].center - glm::vec3(-1.9f, .1f, -.9f)) < 1e-5f);
    REQUIRE(glm::length(cells.value()[0].size - glm::vec3(.2f)) < 1e-6f);
    REQUIRE(cells.value()[0].center.x < cells.value()[1].center.x);
}

TEST_CASE("fluids.volume.gridDebuggerBudgetFailureDoesNotMutateSolver") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position = {-1.9f, .1f, -.9f};
    particles[1].position = {1.9f, 2.9f, .9f};
    REQUIRE(solver->emit(particles).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->debugParticleGrid(1).ok());
    REQUIRE(!solver->debugParticleGrid(0).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == before.particles.size());
    REQUIRE(after.particles[0].position == before.particles[0].position);
    REQUIRE(after.particles[1].position == before.particles[1].position);
}

TEST_CASE("fluids.volume.particleFrameDebuggerReturnsActorLocalAxesOnDemand") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position    = {.5f, 1.f, .5f};
    particles[0].actorGroup  = 7;
    particles[0].orientation = {0.f, 0.f, .70710678f, .70710678f};
    particles[1].position    = {-.5f, 1.f, 0.f};
    particles[1].actorGroup  = 8;
    REQUIRE(solver->emit(particles).ok());
    auto frames = solver->debugParticleFrames(7, 2.f, 1);
    REQUIRE(frames.ok());
    REQUIRE(frames.value().size() == 1);
    REQUIRE(frames.value()[0].particleIndex == 0);
    REQUIRE(glm::length(frames.value()[0].x - glm::vec3(.5f, 3.f, .5f)) < 1e-5f);
    REQUIRE(glm::length(frames.value()[0].y - glm::vec3(-1.5f, 1.f, .5f)) < 1e-5f);
    REQUIRE(glm::length(frames.value()[0].z - glm::vec3(.5f, 1.f, 2.5f)) < 1e-5f);
}

TEST_CASE("fluids.volume.particleFrameDebuggerBudgetAndValidationAreAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].actorGroup = particles[1].actorGroup = 3;
    particles[0].position                             = {-.1f, 1.f, 0.f};
    particles[1].position                             = {.1f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->debugParticleFrames(3, 1.f, 1).ok());
    REQUIRE(!solver->debugParticleFrames(3, 0.f, 2).ok());
    REQUIRE(!solver->debugParticleFrames(4, 1.f, 2).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == before.particles.size());
    REQUIRE(after.particles[0].position == before.particles[0].position);
    REQUIRE(after.particles[1].position == before.particles[1].position);
}

TEST_CASE("fluids.volume.particleInstancesFilterScaleColorAndInterpolate") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position    = {0.f, 1.f, 0.f};
    particles[0].velocity    = {1.f, 0.f, 0.f};
    particles[0].radii       = {.1f, .2f, .3f};
    particles[0].orientation = {0.f, 0.f, .70710678f, .70710678f};
    particles[0].color       = {.2f, .3f, .4f, .5f};
    particles[0].actorGroup  = 7;
    particles[1].position    = {.5f, 1.f, 0.f};
    particles[1].actorGroup  = 8;
    particles[2].position    = {1.f, 1.f, 0.f};
    particles[2].actorGroup  = 7;
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());

    auto instances = solver->particleInstances(7, {2.f, 3.f, 4.f}, .5f, 2);
    REQUIRE(instances.ok());
    REQUIRE(instances.value().size() == 2);
    REQUIRE(instances.value()[0].particleIndex == 0);
    REQUIRE(instances.value()[1].particleIndex == 2);
    CHECK(std::abs(instances.value()[0].position.x - 1.f / 240.f) < 1e-5f);
    CHECK(glm::length(instances.value()[0].scale - glm::vec3(.2f, .6f, 1.2f)) < 1e-5f);
    CHECK(instances.value()[0].orientation == particles[0].orientation);
    CHECK(instances.value()[0].color == particles[0].color);
}

TEST_CASE("fluids.volume.particleInstancesBudgetAndValidationAreAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position   = {0.f, 1.f, 0.f};
    particles[1].position   = {.2f, 1.f, 0.f};
    particles[0].actorGroup = particles[1].actorGroup = 3;
    REQUIRE(solver->emit(particles).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->particleInstances(3, glm::vec3(1.f), 1.f, 1).ok());
    REQUIRE(!solver->particleInstances(4, glm::vec3(1.f), 1.f, 2).ok());
    REQUIRE(!solver->particleInstances(3, {0.f, 1.f, 1.f}, 1.f, 2).ok());
    REQUIRE(!solver->particleInstances(3, glm::vec3(1.f), 1.1f, 2).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == before.particles.size());
    REQUIRE(after.particles[0].position == before.particles[0].position);
    REQUIRE(after.particles[1].position == before.particles[1].position);
}

TEST_CASE("fluids.volume.particleImpostorsApplyRadiusTintAndInterpolation") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();

    VolumeFluidParticle particle;
    particle.position    = {0.f, 1.f, 0.f};
    particle.velocity    = {2.f, 0.f, 0.f};
    particle.radii       = {.1f, .2f, .3f};
    particle.orientation = {0.f, 0.f, .70710677f, .70710677f};
    particle.color       = {.8f, .5f, .25f, .75f};
    particle.actorGroup  = 9;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());

    auto instances = solver->particleImpostors(9, 2.f, {.5f, .25f, 1.f, .5f}, .5f, 1);
    REQUIRE(instances.ok());
    REQUIRE(instances.value().size() == 1);
    CHECK(glm::length(instances.value()[0].position - glm::vec3(1.f / 120.f, 1.f, 0.f)) < 1e-5f);
    CHECK(glm::length(instances.value()[0].scale - glm::vec3(.2f, .4f, .6f)) < 1e-6f);
    CHECK(glm::length(instances.value()[0].color - glm::vec4(.4f, .125f, .25f, .375f)) < 1e-6f);
    CHECK(glm::length(instances.value()[0].orientation - particle.orientation) < 1e-6f);
    REQUIRE(!solver->particleImpostors(9, 0.f, glm::vec4(1.f), 1.f, 1).ok());
    REQUIRE(!solver->particleImpostors(9, 1.f, {1.1f, 1.f, 1.f, 1.f}, 1.f, 1).ok());
    REQUIRE(!solver->particleImpostors(9, 1.f, glm::vec4(1.f), 1.f, 0).ok());

    std::vector<VolumeFluidParticleInstance> reused;
    reused.reserve(2);
    REQUIRE(solver->copyParticleImpostors(9, 1.f, glm::vec4(1.f), 1.f, 1, reused).ok());
    const auto* storage = reused.data();
    REQUIRE(solver->copyParticleImpostors(9, 3.f, glm::vec4(.5f), 0.f, 1, reused).ok());
    CHECK(reused.data() == storage);
    CHECK(glm::length(reused[0].scale - glm::vec3(.3f, .6f, .9f)) < 1e-6f);
    const auto preserved = reused[0];
    REQUIRE(!solver->copyParticleImpostors(9, -1.f, glm::vec4(1.f), 1.f, 1, reused).ok());
    REQUIRE(reused.size() == 1);
    CHECK(reused[0].position == preserved.position);
    CHECK(reused[0].scale == preserved.scale);
    CHECK(reused[0].color == preserved.color);
}

TEST_CASE("fluids.volume.sdfSliceDebuggerSamplesAndTransformsOnDemand") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                   solver = std::move(created).takeValue();
    VolumeFluidSdfCollider collider;
    collider.label    = 42;
    collider.sdf      = MeshSdf::makeSphere({0.f, 0.f, 0.f}, .5f, {5, 7, 9});
    collider.position = {1.f, 2.f, 3.f};
    collider.rotation = {0.f, 0.f, .70710678f, .70710678f};
    collider.scale    = 2.f;
    REQUIRE(solver->setSdfColliders(std::span(&collider, 1)).ok());

    auto slice = solver->debugSdfSlice(42, VolumeFluidSdfSliceAxis::Z, .5f, .5f, 35);
    REQUIRE(slice.ok());
    REQUIRE(slice.value().width == 5);
    REQUIRE(slice.value().height == 7);
    REQUIRE(slice.value().values.size() == 35);
    CHECK(glm::length(slice.value().stepX - glm::vec3(0.f, collider.sdf.cellSize * 2.f, 0.f)) < 1e-5f);
    CHECK(glm::length(slice.value().stepY - glm::vec3(-collider.sdf.cellSize * 2.f, 0.f, 0.f)) < 1e-5f);
    const auto center = slice.value().values[size_t(3) * 5 + 2];
    CHECK(center < .1f);
}

TEST_CASE("fluids.volume.sdfSliceDebuggerRejectsWithoutPartialState") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                   solver = std::move(created).takeValue();
    VolumeFluidSdfCollider collider;
    collider.label = 7;
    collider.sdf   = MeshSdf::makeSphere(glm::vec3(0.f), .5f, {5, 5, 5});
    REQUIRE(solver->setSdfColliders(std::span(&collider, 1)).ok());
    const auto before = solver->snapshot();
    REQUIRE(!solver->debugSdfSlice(8, VolumeFluidSdfSliceAxis::X, .5f, .5f, 25).ok());
    REQUIRE(!solver->debugSdfSlice(7, VolumeFluidSdfSliceAxis::X, .5f, .5f, 24).ok());
    REQUIRE(!solver->debugSdfSlice(7, VolumeFluidSdfSliceAxis(9), .5f, .5f, 25).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.sdfColliders.size() == before.sdfColliders.size());
    REQUIRE(after.sdfColliders[0].sdf.distances == before.sdfColliders[0].sdf.distances);
}

// fluids_volume_heightfield.cpp
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
}  // namespace test_fluids_volume_emitter_part2
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_mesh_emitter)
#include "PrivateTestScope.h"
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
namespace test_fluids_volume_mesh_emitter {
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
}  // namespace test_fluids_volume_mesh_emitter
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids_volume_triggers)
#include "PrivateTestScope.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids_volume_triggers.cpp
namespace test_fluids_volume_triggers {
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
}  // namespace test_fluids_volume_triggers
#endif
