#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
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

TEST_CASE("fluids.volume.renderCopyOwnership") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle p{{0.f, 1.f, 0.f}, {0.f, 0.f, 0.f}};
    p.radii       = {.04f, .08f, .12f};
    p.orientation = {0.f, 0.f, .70710677f, .70710677f};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    REQUIRE(solver->copyRenderData(positions, colors) == 0.1f);
    REQUIRE(solver->particleCount() == 1);
    REQUIRE(positions[0].y == 1.f);
    const auto view = solver->particleView();
    REQUIRE(view.size() == 1);
    REQUIRE(view[0].position == p.position);
    std::vector<glm::vec3> radii;
    std::vector<glm::vec4> orientations;
    REQUIRE(solver->copySurfaceRenderData(positions, colors, radii, orientations) == .1f);
    REQUIRE(radii[0] == p.radii);
    REQUIRE(glm::length(orientations[0] - p.orientation) < 1e-6f);
    const auto* radiiStorage = radii.data();
    const auto* storage      = positions.data();
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(positions[0].y == 1.f);
    REQUIRE(solver->copyRenderData(positions, colors) == 0.1f);
    REQUIRE(positions.data() == storage);
    REQUIRE(solver->copySurfaceRenderData(positions, colors, radii, orientations) == .1f);
    REQUIRE(radii.data() == radiiStorage);
    REQUIRE(positions[0].y < 1.f);
    solver->clear();
    REQUIRE(solver->particleCount() == 0);
    REQUIRE(solver->copyRenderData(positions, colors) == 0.1f);
    REQUIRE(positions.empty());
    REQUIRE(colors.empty());
    REQUIRE(solver->copySurfaceRenderData(positions, colors, radii, orientations) == .1f);
    REQUIRE(radii.empty());
    REQUIRE(orientations.empty());
}

TEST_CASE("fluids.volume.renderInterpolationTracksCompactionTeleportAndRestore") {
    VolumeFluidSettings settings;
    settings.gravity  = {0.f, 0.f, 0.f};
    settings.capacity = 4;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position = {-1.f, 1.f, 0.f};
    particles[0].velocity = {1.f, 0.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[1].velocity = {2.f, 0.f, 0.f};
    particles[1].life     = .001f;
    particles[2].position = {1.f, 1.f, 0.f};
    particles[2].velocity = {3.f, 0.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    std::vector<glm::vec3> positions;
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions.size() == 2);
    REQUIRE(positions[0] == glm::vec3(-1.f, 1.f, 0.f));
    REQUIRE(positions[1] == glm::vec3(1.f, 1.f, 0.f));
    REQUIRE(solver->copyInterpolatedPositions(.5f, positions).ok());
    REQUIRE(std::abs(positions[0].x - (-1.f + .5f / 120.f)) < 1e-5f);
    REQUIRE(std::abs(positions[1].x - (1.f + 1.5f / 120.f)) < 1e-5f);
    const auto preserved = positions;
    REQUIRE(!solver->copyInterpolatedPositions(1.01f, positions).ok());
    REQUIRE(positions == preserved);
    REQUIRE(solver->killParticle(0).ok());
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions[0].x == 1.f);
    REQUIRE(solver->teleportActor({0, 0, 0}, {0, 0, 0, 1}, {.5f, 0, 0}, {0, 0, 0, 1}).ok());
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions[0] == solver->particleView()[0].position);
    const auto saved = solver->snapshot();
    REQUIRE(solver->restore(saved).ok());
    REQUIRE(solver->copyInterpolatedPositions(0.f, positions).ok());
    REQUIRE(positions[0] == saved.particles[0].position);
}

TEST_CASE("fluids.volume.killParticleRemapsOwnedLinksAtomically") {
    VolumeFluidSnapshot state;
    state.settings.gravity  = {0, 0, 0};
    state.settings.capacity = 8;
    state.particles.resize(3);
    for (unsigned i = 0; i < 3; ++i) state.particles[i].position = {-.3f + .3f * float(i), 1.f, 0.f};
    state.particles[2].material.phase = VolumeFluidPhase::Solid;
    VolumeFluidCollider collider;
    collider.label = 7;
    state.colliders.push_back(collider);
    state.attachments.push_back({2, 7, {0, 1, 0}, {0, 0, 0, 1}});
    state.simplexes = {{{0, 2, 0}, 2}, {{1, 2, 0}, 2}};
    auto created    = VolumeFluid::create(state.settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->restore(state).ok());

    REQUIRE(solver->killParticle(1).ok());
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == 2);
    REQUIRE(after.settings.capacity == 8);
    REQUIRE(after.attachments.size() == 1);
    REQUIRE(after.attachments[0].particleIndex == 1);
    REQUIRE(after.simplexes.size() == 1);
    REQUIRE(after.simplexes[0].particleIndices[0] == 0);
    REQUIRE(after.simplexes[0].particleIndices[1] == 1);
    REQUIRE(solver->availableCapacity() == 6);

    const auto beforeInvalid = solver->snapshot();
    REQUIRE(!solver->killParticle(2).ok());
    const auto unchanged = solver->snapshot();
    REQUIRE(unchanged.particles.size() == beforeInvalid.particles.size());
    REQUIRE(unchanged.attachments.size() == beforeInvalid.attachments.size());
    REQUIRE(unchanged.simplexes.size() == beforeInvalid.simplexes.size());
}

TEST_CASE("fluids.volume.killActorParticlesCompactsOnceAndRemapsOwnedLinks") {
    VolumeFluidSnapshot state;
    state.settings.gravity  = {0, 0, 0};
    state.settings.capacity = 8;
    state.particles.resize(4);
    for (unsigned i = 0; i < 4; ++i) {
        state.particles[i].position   = {-.6f + .4f * float(i), 1.f, 0.f};
        state.particles[i].actorGroup = (i == 0 || i == 2) ? 7 : 8;
    }
    state.particles[3].material.phase = VolumeFluidPhase::Solid;
    VolumeFluidCollider collider;
    collider.label = 71;
    state.colliders.push_back(collider);
    state.attachments.push_back({3, 71, state.particles[3].position, {0, 0, 0, 1}});
    state.stitches.push_back({1, 3, .01f});
    state.simplexes = {{{0, 1, 0}, 2}, {{1, 3, 0}, 2}};
    auto created    = VolumeFluid::create(state.settings);
    REQUIRE(created.ok());
    auto solver = std::move(created).takeValue();
    REQUIRE(solver->restore(state).ok());
    REQUIRE(solver->killActorParticles(7).value() == 2);
    const auto after = solver->snapshot();
    REQUIRE(after.particles.size() == 2);
    CHECK(after.particles[0].actorGroup == 8);
    CHECK(after.particles[1].actorGroup == 8);
    REQUIRE(after.attachments.size() == 1);
    CHECK(after.attachments[0].particleIndex == 1);
    REQUIRE(after.stitches.size() == 1);
    CHECK(after.stitches[0].particleIndex1 == 0);
    CHECK(after.stitches[0].particleIndex2 == 1);
    REQUIRE(after.simplexes.size() == 1);
    CHECK(after.simplexes[0].particleIndices[0] == 0);
    CHECK(after.simplexes[0].particleIndices[1] == 1);
    CHECK(solver->availableCapacity() == 6);
    const auto unchanged = solver->snapshot();
    CHECK(!solver->killActorParticles(7).ok());
    CHECK(solver->snapshot().particles.size() == unchanged.particles.size());
    CHECK(solver->snapshot().attachments.size() == unchanged.attachments.size());
}

TEST_CASE("fluids.volume.freeFall") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                      solver = std::move(result).takeValue();
    const VolumeFluidParticle p{{0.f, 2.f, 0.f}, {0.f, 0.f, 0.f}};
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    for (int i = 0; i < 12; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    const auto out = solver->particles();
    REQUIRE(std::abs(out[0].velocity.y + 9.81f * 0.2f) < 0.001f);
    REQUIRE(std::abs(out[0].position.y - (2.f - 0.5f * 9.81f * 0.04f)) < 0.005f);
}

TEST_CASE("fluids.volume.worldSpaceGravityUpdatesAtomically") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->setGravity({0.f, 9.81f, 0.f}).ok());
    REQUIRE(solver->gravity() == glm::vec3(0.f, 9.81f, 0.f));
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particleView()[0].velocity.y > 0.f);
    const auto before = solver->snapshot();
    REQUIRE(!solver->setGravity({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}).ok());
    REQUIRE(!solver->setGravity({1001.f, 0.f, 0.f}).ok());
    REQUIRE(solver->gravity() == before.settings.gravity);
    REQUIRE(solver->snapshot().settings.gravity == before.settings.gravity);
}

TEST_CASE("fluids.volume.actorMassPropertiesAreWeightedAndExplicit") {
    VolumeFluidSettings settings;
    settings.spacing = .1f;
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position         = {0.f, 1.f, 0.f};
    particles[0].actorGroup       = 7;
    particles[0].material.density = 1000.f;
    particles[1].position         = {1.f, 1.f, 0.f};
    particles[1].actorGroup       = 7;
    particles[1].material.density = 500.f;
    particles[2].position         = {1.5f, 1.f, 0.f};
    particles[2].actorGroup       = 8;
    particles[2].material.density = 1000.f;
    REQUIRE(solver->emit(particles).ok());

    auto properties = solver->actorMassProperties(7);
    REQUIRE(properties.ok());
    CHECK(properties.value().particleCount == 2);
    CHECK(std::abs(properties.value().mass - 1.5f) < 1e-6f);
    CHECK(std::abs(properties.value().centerOfMass.x - 1.f / 3.f) < 1e-6f);
    CHECK(properties.value().centerOfMass.y == 1.f);
    CHECK(!solver->actorMassProperties(9).ok());
    CHECK(!solver->actorMassProperties(0x01000000u).ok());
}

TEST_CASE("fluids.volume.particleDragIsBoundedConstantWork") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    particle.velocity = {1.f, 0.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());

    REQUIRE(solver->applyParticleDrag(0, {1.f, 1.f, 0.f}, 12.f, 2.f, .01f).ok());
    CHECK(std::abs(solver->particleView()[0].velocity.x - 1.1f) < 1e-6f);
    const auto before = solver->snapshot();
    CHECK(!solver->applyParticleDrag(1, {1.f, 1.f, 0.f}, 12.f, 2.f, .01f).ok());
    CHECK(!solver->applyParticleDrag(0, {100.f, 1.f, 0.f}, 10000.f, 0.f, 1.f / 30.f).ok());
    CHECK(!solver->applyParticleDrag(0, {1.f, 1.f, 0.f}, -1.f, 2.f, .01f).ok());
    CHECK(solver->snapshot().particles[0].velocity == before.particles[0].velocity);
}

TEST_CASE("fluids.volume.admissionAtomic") {
    VolumeFluidSettings settings;
    settings.capacity = 1;
    auto result       = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                      solver  = std::move(result).takeValue();
    const VolumeFluidParticle batch[] = {{{0.f, 1.f, 0.f}, {}}, {{0.f, 2.f, 0.f}, {}}};
    REQUIRE(!solver->emit(batch).ok());
    REQUIRE(solver->particles().empty());
    REQUIRE(solver->emit(std::span(batch, 1)).ok());
    REQUIRE(!solver->step(std::numeric_limits<float>::quiet_NaN()).ok());
    REQUIRE(!solver->step(0.03f, 1).ok());
    REQUIRE(solver->particles()[0].position.y == 1.f);
    auto invalid = VolumeFluid::create(VolumeFluidSettings{.capacity = 0});
    REQUIRE(!invalid.ok());
}

TEST_CASE("fluids.volume.damBreakContainedRepeatable") {
    VolumeFluidSettings settings;
    settings.spacing = 0.12f;
    auto result      = VolumeFluid::create(settings);
    auto second      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    REQUIRE(second.ok());
    auto                             solver = std::move(result).takeValue();
    auto                             replay = std::move(second).takeValue();
    std::vector<VolumeFluidParticle> batch;
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 9; ++y)
            for (int x = 0; x < 5; ++x)
                batch.push_back({{-1.7f + x * 0.12f, 0.1f + y * 0.12f, -0.24f + z * 0.12f}, {}});
    REQUIRE(solver->emit(batch).ok());
    REQUIRE(replay->emit(batch).ok());
    for (int i = 0; i < 60; ++i) {
        REQUIRE(solver->step(1.f / 60.f).ok());
        REQUIRE(replay->step(1.f / 60.f).ok());
    }
    const auto a = solver->particles(), b = replay->particles();
    float      maxX = -2.f;
    for (size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::isfinite(a[i].position.x));
        REQUIRE(a[i].position.y >= 0.06f);
        REQUIRE(a[i].position.y <= 2.94f);
        REQUIRE(a[i].position.x >= -1.94f);
        REQUIRE(a[i].position.x <= 1.94f);
        REQUIRE(a[i].position.z == b[i].position.z);
        REQUIRE(a[i].position.x == b[i].position.x);
        REQUIRE(a[i].position.y == b[i].position.y);
        maxX = std::max(maxX, a[i].position.x);
    }
    REQUIRE(maxX > -1.f);
}

TEST_CASE("fluids.volume.gasDiffusionLifetimeAndGranules") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidParticle pair[2];
    pair[0].position       = {0.f, 1.f, 0.f};
    pair[1].position       = {0.08f, 1.f, 0.f};
    pair[0].material.phase = pair[1].material.phase = VolumeFluidPhase::Gas;
    pair[0].material.buoyancy = pair[1].material.buoyancy = -1.f;
    pair[0].material.diffusion = pair[1].material.diffusion = 20.f;
    pair[0].color                                           = {1.f, 0.f, 0.f, 1.f};
    pair[1].color                                           = {0.f, 0.f, 1.f, 1.f};
    pair[0].life                                            = 0.1f;
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    auto state = solver->particles();
    REQUIRE(state[0].velocity.y > 0.f);
    REQUIRE(state[0].color.b > 0.f);
    REQUIRE(state[1].color.r > 0.f);
    REQUIRE(std::abs(state[0].color.r + state[1].color.r - 1.f) < 1e-5f);
    for (int i = 0; i < 8; ++i) REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(solver->particles().size() == 1);
    solver->clear();
    pair[0].life           = 0.f;
    pair[0].material.phase = pair[1].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    state           = solver->particles();
    const auto diff = state[0].position - state[1].position;
    REQUIRE(std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) >= 0.099f);
}

TEST_CASE("fluids.volume.phaseRenderCopyIsBoundedAndReportsTotal") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    for (auto& particle : particles) particle.position.y = 1.f;
    particles[0].material.phase = VolumeFluidPhase::Gas;
    particles[0].position.x     = .1f;
    particles[1].material.phase = VolumeFluidPhase::Liquid;
    particles[1].position.x     = .2f;
    particles[2].material.phase = VolumeFluidPhase::Gas;
    particles[2].position.x     = .3f;
    REQUIRE(solver->emit(particles).ok());
    std::vector<glm::vec3> positions;
    std::vector<glm::vec4> colors;
    CHECK(solver->copyPhaseRenderData(VolumeFluidPhase::Gas, positions, colors, 1) == 2);
    REQUIRE(positions.size() == 1);
    CHECK(positions[0].x == .1f);
    CHECK(colors.size() == 1);
}

TEST_CASE("fluids.volume.granularAngularIntegrationAndRollingContact") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle granular;
    granular.position                 = {0, .54f, 0};
    granular.velocity                 = {1, 0, 0};
    granular.angularVelocity          = {0, 0, 3};
    granular.material.phase           = VolumeFluidPhase::Granular;
    granular.material.rollingContacts = true;
    granular.material.rollingFriction = 0.f;
    VolumeFluidCollider floor;
    floor.shape      = VolumeFluidColliderShape::Box;
    floor.center     = {0, 0, 0};
    floor.halfExtent = {2, .5f, 2};
    floor.friction   = 1.f;
    REQUIRE(solver->setColliders(std::span(&floor, 1)).ok());
    REQUIRE(solver->emit(std::span(&granular, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto contacted = solver->particles()[0];
    REQUIRE(std::abs(contacted.angularVelocity.z - 3.f) > .1f);
    REQUIRE(std::abs(contacted.orientation.z) > .001f);

    solver->clear();
    granular.velocity                 = {0, 0, 0};
    granular.angularVelocity          = {0, 0, 10};
    granular.material.rollingFriction = 1.f;
    REQUIRE(solver->emit(std::span(&granular, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(solver->particles()[0].angularVelocity) < .001f);

    solver->clear();
    auto liquid                     = granular;
    liquid.position                 = {0, 1, 0};
    liquid.material.phase           = VolumeFluidPhase::Liquid;
    liquid.material.rollingContacts = false;
    liquid.angularVelocity          = {0, 0, 3};
    const auto original             = liquid.orientation;
    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->emit(std::span(&liquid, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].orientation == original);
    auto invalid                                  = solver->snapshot();
    invalid.particles[0].material.rollingFriction = 1.01f;
    REQUIRE(!solver->restore(invalid).ok());
    REQUIRE(solver->snapshot().particles[0].material.rollingFriction == 1.f);
    solver->clear();
    granular.angularVelocity = {0, 0, 1001};
    REQUIRE(!solver->emit(std::span(&granular, 1)).ok());
}

TEST_CASE("fluids.volume.granularPairStaticDynamicAndRollingFriction") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto run            = [&](float friction, bool rolling) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle pair[2];
        pair[0].position = {-.045f, 1, 0};
        pair[1].position = {.045f, 1, 0};
        pair[0].velocity = {0, 0, 1};
        pair[1].velocity = {0, 0, -1};
        for (auto& p : pair) {
            p.material.phase           = VolumeFluidPhase::Granular;
            p.material.cohesion        = 0;
            p.material.dynamicFriction = friction;
            p.material.staticFriction  = friction;
            p.material.rollingContacts = rolling;
            p.material.rollingFriction = 0;
        }
        REQUIRE(solver->emit(pair).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto       frictionless = run(0, false), frictional = run(1, true);
    const auto freeRelative =
        std::abs(frictionless->particles()[0].velocity.z - frictionless->particles()[1].velocity.z);
    const auto frictionRelative =
        std::abs(frictional->particles()[0].velocity.z - frictional->particles()[1].velocity.z);
    REQUIRE(frictionRelative < freeRelative - .1f);
    REQUIRE(std::abs(frictional->particles()[0].angularVelocity.y) > .1f);
    REQUIRE(std::abs(frictional->particles()[1].angularVelocity.y) > .1f);
    auto rolling                       = run(0, true);
    auto rollingState                  = rolling->snapshot();
    rollingState.particles[0].position = {-.045f, 1, 0};
    rollingState.particles[1].position = {.045f, 1, 0};
    rollingState.particles[0].velocity = rollingState.particles[1].velocity = glm::vec3(0.f);
    rollingState.particles[0].angularVelocity                               = {0, 10, 0};
    rollingState.particles[1].angularVelocity                               = {0, -10, 0};
    for (auto& p : rollingState.particles) p.material.rollingFriction = 1.f;
    REQUIRE(rolling->restore(rollingState).ok());
    REQUIRE(rolling->step(1.f / 120.f, 1).ok());
    REQUIRE(glm::length(rolling->particles()[0].angularVelocity - rolling->particles()[1].angularVelocity) < .001f);
    auto invalid                                 = rolling->snapshot();
    invalid.particles[0].material.staticFriction = 1.01f;
    REQUIRE(!rolling->restore(invalid).ok());
    REQUIRE(rolling->snapshot().particles[0].material.staticFriction == 0.f);
}

TEST_CASE("fluids.volume.atmosphericSurfacePressureDragAndMigration") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle pair[2];
    pair[0].position = {-.075f, 1, 0};
    pair[1].position = {.075f, 1, 0};
    for (auto& p : pair) {
        p.material.cohesion            = 0;
        p.material.viscosity           = 0;
        p.material.atmosphericPressure = 10.f;
    }
    REQUIRE(solver->emit(pair).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto pressured = solver->particles();
    REQUIRE(pressured[0].velocity.x > 0.f);
    REQUIRE(pressured[1].velocity.x < 0.f);
    REQUIRE(glm::length(pressured[0].velocity + pressured[1].velocity) < 1e-6f);

    solver->clear();
    VolumeFluidParticle exposed;
    exposed.position           = {0, 1, 0};
    exposed.velocity           = {1, 0, 0};
    exposed.material.cohesion  = 0;
    exposed.material.viscosity = 0;
    exposed.material.drag      = 60.f;
    REQUIRE(solver->emit(std::span(&exposed, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x > 0.f);
    REQUIRE(solver->particles()[0].velocity.x < .7f);

    auto state    = solver->snapshot();
    auto encoded  = encodeVolumeFluid(state);
    auto particle = encoded.find("particles")->at(0);
    encoded.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(particle))}));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 9);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().particles[0].material.atmosphericPressure == 0.f);
    auto invalid                                      = state;
    invalid.particles[0].material.atmosphericPressure = 1001.f;
    REQUIRE(!solver->restore(invalid).ok());
    REQUIRE(solver->snapshot().particles[0].material.atmosphericPressure == 0.f);
}

TEST_CASE("fluids.volume.transientPerParticleWind") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position           = {0, 1, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    particle.material.drag      = 60;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    const glm::vec3 wind[] = {glm::vec3(6, 0, 0)};
    REQUIRE(solver->setParticleWinds(wind).ok());
    const glm::vec3 invalid[] = {glm::vec3(0), glm::vec3(0)};
    REQUIRE(!solver->setParticleWinds(invalid).ok());
    REQUIRE(!solver->step(-1.f, 1).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const float driven = solver->particles()[0].velocity.x;
    REQUIRE(driven > 2.f);
    REQUIRE(driven < 3.f);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x > 0.f);
    REQUIRE(solver->particles()[0].velocity.x < driven);
    REQUIRE(solver->setParticleWinds(wind).ok());
    REQUIRE(solver->setParticleWinds({}).ok());
    const float before = solver->particles()[0].velocity.x;
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x < before);
    const glm::vec3 nonfinite[] = {glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0, 0)};
    REQUIRE(!solver->setParticleWinds(nonfinite).ok());
}

TEST_CASE("fluids.volume.boundedFluid3DWindZones") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position           = {1, 1, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    particle.material.drag      = 60;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidWindZone ambient;
    ambient.direction = {0, 1, 0};
    ambient.intensity = 2;
    VolumeFluidWindZone sphere;
    sphere.type                       = VolumeFluidWindZoneType::Spherical;
    sphere.center                     = {0, 1, 0};
    sphere.radius                     = 2;
    sphere.intensity                  = 4;
    sphere.radial                     = true;
    const VolumeFluidWindZone zones[] = {ambient, sphere};
    REQUIRE(solver->accumulateWindZones(zones, 0.f).ok());
    auto invalid   = sphere;
    invalid.radius = 0;
    REQUIRE(!solver->accumulateWindZones(std::span(&invalid, 1), 0.f).ok());
    REQUIRE(!solver->accumulateWindZones(zones, -1.f).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto velocity = solver->particles()[0].velocity;
    REQUIRE(velocity.x > 0.f);
    REQUIRE(velocity.y > 0.f);
    REQUIRE(std::abs(velocity.x / velocity.y - 1.5f) < .001f);

    eve::Value zoneValue = eve::fluids::encodeVolumeFluidWindZone(ambient);
    auto       decoded   = decodeVolumeFluidWindZones(eve::Value(eve::Value::Array{zoneValue}));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().size() == 1);
    REQUIRE(decoded.value()[0].intensity == 2.f);
    zoneValue.set("unknown", 1);
    REQUIRE(!decodeVolumeFluidWindZones(eve::Value(eve::Value::Array{zoneValue})).ok());
    REQUIRE(!decodeVolumeFluidWindZones(eve::Value(eve::Value::Array(65, eve::Value{}))).ok());

    VolumeFluidSettings largeSettings;
    largeSettings.capacity = 65536;
    auto largeCreated      = VolumeFluid::create(largeSettings);
    REQUIRE(largeCreated.ok());
    auto                             large = std::move(largeCreated).takeValue();
    std::vector<VolumeFluidParticle> particles(62501);
    for (auto& p : particles) p.position = {0, 1, 0};
    REQUIRE(large->emit(particles).ok());
    std::vector<VolumeFluidWindZone> spherical(64, sphere);
    REQUIRE(!large->accumulateWindZones(spherical, 0.f).ok());
}

TEST_CASE("fluids.volume.externalForcesUseInverseMassAndClearAfterSuccessfulStep") {
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
    REQUIRE(solver->emit(particles).ok());
    const glm::vec3 forces[2] = {{8.f, 0.f, 0.f}, {8.f, 0.f, 0.f}};
    REQUIRE(solver->setParticleExternalForces(forces).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto first = solver->particles();
    REQUIRE(std::abs(first[0].velocity.x - 2.f * first[1].velocity.x) < 1e-5f);
    REQUIRE(first[0].velocity.x > 0.f);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto second = solver->particles();
    REQUIRE(std::abs(second[0].velocity.x - first[0].velocity.x) < 1e-5f);
    REQUIRE(std::abs(second[1].velocity.x - first[1].velocity.x) < 1e-5f);
}

TEST_CASE("fluids.volume.externalForceZonesAreBoundedAndAtomic") {
    VolumeFluidSettings settings;
    settings.gravity = {0.f, 0.f, 0.f};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    VolumeFluidWindZone zone;
    zone.direction = {1.f, 0.f, 0.f};
    zone.intensity = 8.f;
    REQUIRE(solver->accumulateExternalForceZones(std::span(&zone, 1), 0.f).ok());
    auto invalid      = zone;
    invalid.intensity = 1001.f;
    REQUIRE(!solver->accumulateExternalForceZones(std::span(&invalid, 1), 0.f).ok());
    REQUIRE(!solver->step(1.f / 120.f, 0).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].velocity.x > 0.f);
}

TEST_CASE("fluids.volume.perParticleSmoothingRadiusAndMigration") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto run            = [&](float firstSmoothing, float secondSmoothing) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle pair[2];
        pair[0].position = {-.125f, 1, 0};
        pair[1].position = {.125f, 1, 0};
        pair[0].velocity = {1, 0, 0};
        pair[1].velocity = {-1, 0, 0};
        for (auto& p : pair) {
            p.material.cohesion  = 0;
            p.material.viscosity = 100;
        }
        pair[0].material.smoothing = firstSmoothing;
        pair[1].material.smoothing = secondSmoothing;
        REQUIRE(solver->emit(pair).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto compact = run(2, 2), mixed = run(1, 4);
    REQUIRE(std::abs(compact->particles()[0].velocity.x - 1.f) < 1e-6f);
    REQUIRE(mixed->particles()[0].velocity.x < .999f);
    REQUIRE(std::abs(mixed->particles()[0].velocity.x + mixed->particles()[1].velocity.x) < 1e-6f);

    auto              current = mixed->snapshot();
    auto              encoded = encodeVolumeFluid(current);
    eve::Value::Array particles;
    for (size_t i = 0; i < encoded.find("particles")->arraySize(); ++i) {
        const auto& currentParticle = encoded.find("particles")->at(i);
        const auto* source          = currentParticle.find("material");
        eve::Value  material(eve::Value::Object{});
        for (const auto& key : source->keys())
            if (key != "smoothing" && key != "rollingContacts" && key != "rollingFriction" &&
                key != "dynamicFriction" && key != "staticFriction" && key != "stickiness" && key != "stickDistance" &&
                key != "frictionCombine" && key != "stickinessCombine")
                material.set(key, *source->find(key));
        eve::Value particle(eve::Value::Object{});
        for (const auto& key : currentParticle.keys())
            if (key != "material" && key != "angularVelocity") particle.set(key, *currentParticle.find(key));
        particle.set("material", std::move(material));
        particles.push_back(std::move(particle));
    }
    encoded.set("particles", eve::Value(std::move(particles)));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 10);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().particles[0].material.smoothing == 2.f);
    REQUIRE(!migrated.value().particles[0].material.rollingContacts);
    REQUIRE(migrated.value().particles[0].material.rollingFriction == 0.f);
    REQUIRE(migrated.value().particles[0].material.dynamicFriction == .2f);
    REQUIRE(migrated.value().particles[0].material.staticFriction == .2f);
    REQUIRE(migrated.value().particles[0].angularVelocity == glm::vec3(0.f));
    auto invalid                            = current;
    invalid.particles[0].material.smoothing = 4.01f;
    REQUIRE(!mixed->restore(invalid).ok());
    REQUIRE(mixed->snapshot().particles[0].material.smoothing == 1.f);
}

TEST_CASE("fluids.volume.colliderSolidifyMelt") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidCollider collider;
    collider.label    = 9;
    collider.solidify = true;
    REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
    VolumeFluidParticle p;
    p.position   = {0.f, 1.25f, 0.f};
    p.actorGroup = 23;
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].colliderLabel == 9);
    REQUIRE(solver->contacts()[0].particleIndex == 0);
    REQUIRE(solver->contacts()[0].actorGroup == 23);
    REQUIRE(solver->contacts()[0].impulse.y < 0.f);
    REQUIRE(solver->particles()[0].material.phase == VolumeFluidPhase::Solid);
    const auto frozen = solver->particles()[0].position;
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(solver->particles()[0].position.y == frozen.y);
    REQUIRE(solver->setColliders({}).ok());
    REQUIRE(solver->paint(frozen, 0.1f, {}).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    REQUIRE(solver->particles()[0].position.y < frozen.y);
}

TEST_CASE("fluids.volume.solidifyStopsFurtherColliderProjection") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidCollider first;
    first.label    = 1;
    first.solidify = true;
    VolumeFluidCollider second;
    second.label                          = 2;
    second.center                         = {0.1f, 1.3f, 0.f};
    const VolumeFluidCollider colliders[] = {first, second};
    REQUIRE(solver->setColliders(colliders).ok());
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.25f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto frozen = solver->particles()[0];
    REQUIRE(frozen.material.phase == VolumeFluidPhase::Solid);
    REQUIRE(frozen.position.x == 0.f);
    REQUIRE(solver->contacts().size() == 1);
    REQUIRE(solver->contacts()[0].colliderLabel == 1);
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position == frozen.position);
}

TEST_CASE("fluids.volume.solidifyDoesNotInjectViscousVelocity") {
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto result      = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                solver = std::move(result).takeValue();
    VolumeFluidCollider collider;
    collider.solidify = true;
    REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
    VolumeFluidParticle particles[2];
    particles[0].position = {0.f, 1.25f, 0.f};
    particles[1].position = {0.f, 1.44f, 0.f};
    for (auto& particle : particles) {
        particle.material.viscosity = 50.f;
        particle.material.cohesion  = 0.f;
    }
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    const auto state = solver->particles();
    REQUIRE(state[0].material.phase == VolumeFluidPhase::Solid);
    REQUIRE(state[1].material.phase == VolumeFluidPhase::Liquid);
    REQUIRE(glm::length(state[1].velocity) < 1e-6f);
}

TEST_CASE("fluids.volume.viscosityDampsShearWithoutBulkDrag") {
    const auto run = [&](float viscosity) {
        VolumeFluidSettings settings;
        settings.gravity = glm::vec3(0.f);
        auto result      = VolumeFluid::create(settings);
        REQUIRE(result.ok());
        auto                solver = std::move(result).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position = {-0.06f, 1.f, 0.f};
        particles[1].position = {0.06f, 1.f, 0.f};
        particles[0].velocity = {0.f, 0.04f, 0.1f};
        particles[1].velocity = {0.f, -0.04f, 0.1f};
        for (auto& particle : particles) {
            particle.material.viscosity = viscosity;
            particle.material.cohesion  = 0.f;
        }
        REQUIRE(solver->emit(particles).ok());
        for (int tick = 0; tick < 60; ++tick) REQUIRE(solver->step(1.f / 120.f, 1).ok());
        const auto state = solver->particles();
        const auto mean  = (state[0].velocity + state[1].velocity) * 0.5f;
        // Viscosity damps relative motion, not the common translation.
        REQUIRE(glm::length(mean - glm::vec3(0.f, 0.f, 0.1f)) < 1e-4f);
        return glm::length(state[0].velocity - state[1].velocity);
    };
    const float inviscid = run(0.f);
    const float medium   = run(10.f);
    const float viscous  = run(100.f);
    REQUIRE(std::abs(inviscid - 0.08f) < 1e-4f);
    REQUIRE(medium < inviscid * 0.95f);
    REQUIRE(viscous < medium * 0.5f);
}
