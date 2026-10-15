#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
