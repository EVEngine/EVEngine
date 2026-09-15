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

#include "fluids/VolumeFluidCodec.h"

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
