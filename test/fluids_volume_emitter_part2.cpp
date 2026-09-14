#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidEmitter.h"
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
