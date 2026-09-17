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

TEST_CASE("fluids.volume.ownedSdfColliderObstacleContainerAndCodec") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    settings.minimum    = {-1.f, .2f, -.8f};
    settings.maximum    = {1.4f, 1.8f, .8f};
    auto created        = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                   solver = std::move(created).takeValue();
    VolumeFluidSdfCollider obstacle;
    obstacle.label    = 41;
    obstacle.sdf      = MeshSdf::makeSphere({0, 1, 0}, .5f, {24, 24, 24});
    obstacle.position = {.2f, 0, 0};
    obstacle.scale    = 1.5f;
    obstacle.friction = 0;
    REQUIRE(solver->setSdfColliders(std::span(&obstacle, 1)).ok());
    VolumeFluidParticle particle;
    particle.position           = {.9f, 1.5f, 0};
    particle.material.cohesion  = 0;
    particle.material.viscosity = 0;
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->step(1.f / 120.f, 1).ok());
    REQUIRE(solver->particles()[0].position.x > .98f);
    REQUIRE(!solver->contacts().empty());
    REQUIRE(solver->contacts()[0].colliderLabel == 41);
    auto encoded = encodeVolumeFluid(solver->snapshot());
    auto decoded = decodeVolumeFluid(encoded);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().version == 20);
    REQUIRE(decoded.value().sdfColliders.size() == 1);
    REQUIRE(decoded.value().sdfColliders[0].sdf.distances == obstacle.sdf.distances);

    VolumeFluidSdfPose pose;
    pose.label                 = 41;
    pose.position              = {.35f, .1f, 0};
    pose.scale                 = 1.25f;
    pose.velocity              = {.5f, 0, 0};
    pose.angularVelocity       = {0, 1, 0};
    const auto originalSamples = solver->snapshot().sdfColliders[0].sdf.distances;
    REQUIRE(solver->updateSdfColliderPoses(std::span(&pose, 1)).ok());
    const auto moved = solver->snapshot().sdfColliders[0];
    REQUIRE(moved.position == pose.position);
    REQUIRE(moved.scale == pose.scale);
    REQUIRE(moved.velocity == pose.velocity);
    REQUIRE(moved.angularVelocity == pose.angularVelocity);
    REQUIRE(moved.sdf.distances == originalSamples);
    auto              poseValue = encodeVolumeFluidSdfPose(pose);
    eve::Value::Array poseArray;
    poseArray.push_back(poseValue);
    auto decodedPoses = decodeVolumeFluidSdfPoses(eve::Value(std::move(poseArray)));
    REQUIRE(decodedPoses.ok());
    REQUIRE(decodedPoses.value().size() == 1);
    REQUIRE(decodedPoses.value()[0].position == pose.position);
    auto invalidPose  = pose;
    invalidPose.label = 999;
    REQUIRE(!solver->updateSdfColliderPoses(std::span(&invalidPose, 1)).ok());
    REQUIRE(solver->snapshot().sdfColliders[0].position == pose.position);

    auto invalid = obstacle;
    invalid.sdf.distances.pop_back();
    REQUIRE(!solver->setSdfColliders(std::span(&invalid, 1)).ok());
    REQUIRE(solver->snapshot().sdfColliders[0].label == 41);

    VolumeFluidSettings enclosedSettings = settings;
    enclosedSettings.minimum             = {-.7f, .3f, -.7f};
    enclosedSettings.maximum             = {.7f, 1.7f, .7f};
    auto enclosedCreated                 = VolumeFluid::create(enclosedSettings);
    REQUIRE(enclosedCreated.ok());
    auto                   enclosed = std::move(enclosedCreated).takeValue();
    VolumeFluidSdfCollider container;
    container.label    = 42;
    container.inverted = true;
    container.sdf      = MeshSdf::makeSphere({0, 1, 0}, .6f, {32, 32, 32});
    container.friction = 0;
    REQUIRE(enclosed->setSdfColliders(std::span(&container, 1)).ok());
    particle.position = {.62f, 1, 0};
    REQUIRE(enclosed->emit(std::span(&particle, 1)).ok());
    REQUIRE(enclosed->step(1.f / 120.f, 1).ok());
    REQUIRE(enclosed->particles()[0].position.x < .56f);
    container.sdf.origin.x = .5f;
    REQUIRE(!enclosed->setSdfColliders(std::span(&container, 1)).ok());
}

TEST_CASE("fluids.volume.raycastSortedFilteredAndBounded") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(4);
    for (size_t i = 0; i < particles.size(); ++i) {
        particles[i].position       = {float(i) * .5f, 1.f, 0.f};
        particles[i].material.phase = VolumeFluidPhase(i);
    }
    REQUIRE(solver->emit(particles).ok());
    auto hits = solver->raycast({-1.f, 1.f, 0.f}, {5.f, 0.f, 0.f}, 2.1f, 4, 0x0f);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 3);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(std::abs(hits.value()[0].distance - .95f) < 1e-6f);
    REQUIRE(std::abs(hits.value()[0].point.x + .05f) < 1e-6f);
    REQUIRE(hits.value()[0].normal.x == -1.f);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(hits.value()[2].particle.material.phase == VolumeFluidPhase::Granular);
    auto nearest = solver->raycast({-1.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, 10.f, 2, 0x0f);
    REQUIRE(nearest.ok());
    REQUIRE(nearest.value().size() == 2);
    REQUIRE(nearest.value()[0].particleIndex == 0);
    REQUIRE(nearest.value()[1].particleIndex == 1);
    auto granular = solver->raycast({-1.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, 10.f, 4, 1u << 2);
    REQUIRE(granular.ok());
    REQUIRE(granular.value().size() == 1);
    REQUIRE(granular.value()[0].particleIndex == 2);
    auto inside = solver->raycast(particles[0].position, {1.f, 0.f, 0.f}, 0.f, 1, 1);
    REQUIRE(inside.ok());
    REQUIRE(inside.value().size() == 1);
    REQUIRE(inside.value()[0].distance == 0.f);
    REQUIRE(inside.value()[0].normal.x == -1.f);
    REQUIRE(!solver->raycast({}, {}, 1.f).ok());
    REQUIRE(!solver->raycast({}, {0, 0, 1}, -1.f).ok());
    REQUIRE(!solver->raycast({}, {0, 0, 1}, 1.f, 0).ok());
    REQUIRE(!solver->raycast({}, {0, 0, 1}, 1.f, 1, 0x10).ok());
    REQUIRE(solver->particleCount() == 4);
}

TEST_CASE("fluids.volume.raycastRejectsUnboundedSource") {
    VolumeFluidSettings settings;
    settings.capacity = 65537;
    auto result       = VolumeFluid::create(settings);
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(settings.capacity);
    for (auto& particle : particles) particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(!solver->raycast({0.f, 1.f, -1.f}, {0.f, 0.f, 1.f}, 2.f).ok());
    REQUIRE(!solver->querySphere({0.f, 1.f, 0.f}, .1f, 0.f, 1.f).ok());
    REQUIRE(!solver->queryBox({0.f, 1.f, 0.f}, {.1f, .1f, .1f}, {0, 0, 0, 1}, 0.f, 1.f).ok());
    REQUIRE(solver->particleCount() == settings.capacity);
}

TEST_CASE("fluids.volume.sphereDistanceQuerySignedOffsetAndFilter") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position       = {0.f, 1.f, 0.f};
    particles[1].position       = {.2f, 1.f, 0.f};
    particles[1].material.phase = VolumeFluidPhase::Gas;
    particles[2].position       = {.4f, 1.f, 0.f};
    particles[2].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(particles).ok());
    auto hits = solver->querySphere({0.f, 1.f, 0.f}, .1f, .02f, .1f, 4, 0x0f);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 2);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(std::abs(hits.value()[0].distance + .17f) < 1e-6f);
    REQUIRE(hits.value()[0].normal.y == 1.f);
    REQUIRE(std::abs(hits.value()[0].queryPoint.y - 1.12f) < 1e-6f);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(std::abs(hits.value()[1].distance - .03f) < 1e-6f);
    REQUIRE(std::abs(hits.value()[1].queryPoint.x - .12f) < 1e-6f);
    REQUIRE(hits.value()[1].normal.x == 1.f);
    auto gas = solver->querySphere({0.f, 1.f, 0.f}, .5f, 0.f, 0.f, 1, 1u << 1);
    REQUIRE(gas.ok());
    REQUIRE(gas.value().size() == 1);
    REQUIRE(gas.value()[0].particle.material.phase == VolumeFluidPhase::Gas);
    hits.value()[0].particle.position.x = 9.f;
    REQUIRE(solver->particles()[0].position.x == 0.f);
    REQUIRE(!solver->querySphere({}, -1.f, 0.f, 1.f).ok());
    REQUIRE(!solver->querySphere({}, 1.f, -1.f, 1.f).ok());
    REQUIRE(!solver->querySphere({}, 1.f, 0.f, -1.f).ok());
    REQUIRE(!solver->querySphere({}, 1.f, 0.f, 1.f, 0, 15).ok());
    REQUIRE(!solver->querySphere({}, 1.f, 0.f, 1.f, 1, 16).ok());
    REQUIRE(solver->particleCount() == 3);
}

TEST_CASE("fluids.volume.orientedBoxDistanceInsideOutsideAndOffset") {
    auto result = VolumeFluid::create({});
    REQUIRE(result.ok());
    auto                             solver = std::move(result).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position       = {0.f, 1.f, 0.f};
    particles[1].position       = {0.f, 1.3f, 0.f};
    particles[1].material.phase = VolumeFluidPhase::Gas;
    particles[2].position       = {.3f, 1.f, 0.f};
    particles[2].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(particles).ok());
    const glm::vec4 quarterTurn{0.f, 0.f, .70710678f, .70710678f};
    auto            hits = solver->queryBox({0.f, 1.f, 0.f}, {.2f, .05f, .05f}, quarterTurn, .02f, .04f, 4, 15);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 2);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(std::abs(hits.value()[0].distance + .12f) < 1e-5f);
    REQUIRE(hits.value()[0].normal.x > 0.999f);
    REQUIRE(std::abs(hits.value()[0].queryPoint.x - .07f) < 1e-5f);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(std::abs(hits.value()[1].distance - .03f) < 1e-5f);
    REQUIRE(hits.value()[1].normal.y > 0.999f);
    REQUIRE(std::abs(hits.value()[1].queryPoint.y - 1.22f) < 1e-5f);
    auto gas = solver->queryBox({0.f, 1.f, 0.f}, {.2f, .05f, .05f}, quarterTurn, 0.f, 1.f, 1, 1u << 1);
    REQUIRE(gas.ok());
    REQUIRE(gas.value().size() == 1);
    REQUIRE(gas.value()[0].particleIndex == 1);
    hits.value()[0].particle.position.y = 9.f;
    REQUIRE(solver->particles()[0].position.y == 1.f);
    REQUIRE(!solver->queryBox({}, {-.1f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, 1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 2}, 0.f, 1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, -1.f, 1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, -1.f).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, 1.f, 0, 15).ok());
    REQUIRE(!solver->queryBox({}, {1.f, 1.f, 1.f}, {0, 0, 0, 1}, 0.f, 1.f, 1, 16).ok());
    REQUIRE(solver->particleCount() == 3);
}

TEST_CASE("fluids.volume.mixedQueryBatchIsGroupedBoundedAndAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(3);
    particles[0].position       = {0.f, 1.f, 0.f};
    particles[1].position       = {.2f, 1.f, 0.f};
    particles[1].material.phase = VolumeFluidPhase::Gas;
    particles[2].position       = {.4f, 1.f, 0.f};
    particles[2].material.phase = VolumeFluidPhase::Granular;
    REQUIRE(solver->emit(particles).ok());
    std::vector<VolumeFluidQueryShape> queries(3);
    queries[0].center        = {0.f, 1.f, 0.f};
    queries[0].size          = {.1f, 0.f, 0.f};
    queries[0].maxDistance   = .1f;
    queries[1].type          = VolumeFluidQueryType::Box;
    queries[1].center        = {.2f, 1.f, 0.f};
    queries[1].size          = {.1f, .1f, .1f};
    queries[1].maxDistance   = 0.f;
    queries[1].phaseMask     = 1u << 1;
    queries[2].type          = VolumeFluidQueryType::Ray;
    queries[2].center        = {-1.f, 1.f, 0.f};
    queries[2].size          = {1.f, 1.f, 0.f};
    queries[2].contactOffset = .5f;
    queries[2].maxDistance   = 99.f;
    auto hits                = solver->queryBatch(queries, 2);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 5);
    REQUIRE(hits.value()[0].queryIndex == 0);
    REQUIRE(hits.value()[0].particleIndex == 0);
    REQUIRE(hits.value()[1].queryIndex == 0);
    REQUIRE(hits.value()[1].particleIndex == 1);
    REQUIRE(hits.value()[2].queryIndex == 1);
    REQUIRE(hits.value()[2].particleIndex == 1);
    REQUIRE(hits.value()[3].queryIndex == 2);
    REQUIRE(hits.value()[3].particleIndex == 0);
    REQUIRE(hits.value()[4].queryIndex == 2);
    REQUIRE(hits.value()[4].particleIndex == 1);
    REQUIRE(std::abs(hits.value()[3].distance + .5f) < 1e-6f);
    REQUIRE(std::abs(hits.value()[3].queryPoint.x - .45f) < 1e-6f);
    hits.value()[0].particle.position.x = 9.f;
    REQUIRE(solver->particles()[0].position.x == 0.f);
    queries[0].size.y = 1.f;
    REQUIRE(!solver->queryBatch(queries, 2).ok());
    REQUIRE(solver->particleCount() == 3);
    REQUIRE(!solver->queryBatch({}, 0).ok());
}

TEST_CASE("fluids.volume.mixedQueryBatchRejectsCandidateExplosion") {
    VolumeFluidSettings settings;
    settings.capacity = 20000;
    auto created      = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                             solver = std::move(created).takeValue();
    std::vector<VolumeFluidParticle> particles(settings.capacity);
    for (auto& particle : particles) particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    std::vector<VolumeFluidQueryShape> queries(201);
    for (auto& query : queries) query.size = {0.f, 0.f, 0.f};
    REQUIRE(!solver->queryBatch(queries, 1).ok());
    REQUIRE(solver->particleCount() == settings.capacity);
}

TEST_CASE("fluids.volume.queryColorsAreAtomicAndLaterOverlapsWin") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-.2f, 1.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[2].position = {.2f, 1.f, 0.f};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidQueryShape queries[2];
    queries[0].type          = VolumeFluidQueryType::Box;
    queries[0].center        = {-.1f, 1.f, 0.f};
    queries[0].size          = {.3f, .2f, .2f};
    queries[1].type          = VolumeFluidQueryType::Box;
    queries[1].center        = {.1f, 1.f, 0.f};
    queries[1].size          = {.3f, .2f, .2f};
    const glm::vec4 colors[] = {{1.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    auto            counts   = solver->applyQueryColors(queries, colors, {0.f, 1.f, 1.f, 1.f}, 3);
    REQUIRE(counts.ok());
    REQUIRE(counts.value().size() == 2);
    REQUIRE(counts.value()[0] == 2);
    REQUIRE(counts.value()[1] == 2);
    const auto colored = solver->particles();
    REQUIRE(colored[0].color == colors[0]);
    REQUIRE(colored[1].color == colors[1]);
    REQUIRE(colored[2].color == colors[1]);
    queries[1].rotation = {0.f, 0.f, 0.f, 2.f};
    REQUIRE(!solver->applyQueryColors(queries, colors, {0.f, 0.f, 1.f, 1.f}, 3).ok());
    REQUIRE(solver->particles()[0].color == colors[0]);
    const glm::vec4 invalid[] = {{2.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    queries[1].rotation       = {0.f, 0.f, 0.f, 1.f};
    REQUIRE(!solver->applyQueryColors(queries, invalid, {0.f, 1.f, 1.f, 1.f}, 3).ok());
    REQUIRE(solver->particles()[1].color == colors[1]);
}

TEST_CASE("fluids.volume.persistentQueryColorsPreserveOutsideAndAreAtomic") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-.2f, 1.f, 0.f};
    particles[1].position = {0.f, 1.f, 0.f};
    particles[2].position = {.2f, 1.f, 0.f};
    particles[0].color    = {0.f, 0.f, 1.f, 1.f};
    particles[1].color    = {0.f, 1.f, 0.f, 1.f};
    particles[2].color    = {0.f, 1.f, 1.f, 1.f};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidQueryShape queries[2];
    queries[0].type                      = VolumeFluidQueryType::Box;
    queries[0].center                    = {-.2f, 1.f, 0.f};
    queries[0].size                      = {.1f, .1f, .1f};
    queries[1].type                      = VolumeFluidQueryType::Box;
    queries[1].center                    = {0.f, 1.f, 0.f};
    queries[1].size                      = {.1f, .1f, .1f};
    const glm::vec4             colors[] = {{1.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    auto                        counts   = solver->applyQueryColorsPreservingOutside(queries, colors, 3);
    const std::vector<unsigned> expectedCounts{1, 1};
    REQUIRE(counts.ok());
    REQUIRE(counts.value() == expectedCounts);
    REQUIRE(solver->particles()[0].color == colors[0]);
    REQUIRE(solver->particles()[1].color == colors[1]);
    REQUIRE(solver->particles()[2].color == particles[2].color);
    queries[1].rotation = {0.f, 0.f, 0.f, 2.f};
    REQUIRE(!solver->applyQueryColorsPreservingOutside(queries, colors, 3).ok());
    REQUIRE(solver->particles()[0].color == colors[0]);
    const glm::vec4 invalid[] = {{2.f, 0.f, 0.f, 1.f}, {1.f, 1.f, 0.f, 1.f}};
    queries[1].rotation       = {0.f, 0.f, 0.f, 1.f};
    REQUIRE(!solver->applyQueryColorsPreservingOutside(queries, invalid, 3).ok());
    REQUIRE(solver->particles()[1].color == colors[1]);
}

TEST_CASE("fluids.volume.mixedQueryBatchCodecIsStrictAndOwning") {
    eve::Value query(eve::Value::Object{});
    query.set("type", 0);
    query.set("center", eve::Value(eve::Value::Array{0.f, 1.f, 0.f}));
    query.set("size", eve::Value(eve::Value::Array{.1f, 0.f, 0.f}));
    query.set("rotation", eve::Value(eve::Value::Array{0.f, 0.f, 0.f, 1.f}));
    query.set("contactOffset", 0.f);
    query.set("maxDistance", 1.f);
    query.set("phaseMask", 15);
    query.set("collisionFilter", int64_t(0xffff0001u));
    auto decoded = decodeVolumeFluidQueries(eve::Value(eve::Value::Array{query}));
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().size() == 1);
    REQUIRE(decoded.value()[0].size.x == .1f);
    query.set("unknown", 1);
    REQUIRE(!decodeVolumeFluidQueries(eve::Value(eve::Value::Array{query})).ok());
    REQUIRE(!decodeVolumeFluidQueries(eve::Value(eve::Value::Array(257, query))).ok());
    auto colors =
        decodeVolumeFluidColors(eve::Value(eve::Value::Array{eve::Value(eve::Value::Array{1.f, 0.f, 0.f, 1.f})}));
    REQUIRE(colors.ok());
    REQUIRE(colors.value()[0].x == 1.f);
    REQUIRE(!decodeVolumeFluidColors(eve::Value(eve::Value::Array{eve::Value(eve::Value::Array{1.f, 0.f, 0.f})})).ok());
}

TEST_CASE("fluids.volume.queryCollisionFiltersMatchBothDirections") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle a, b;
    a.position                            = {0.f, 1.f, 0.f};
    b.position                            = {.2f, 1.f, 0.f};
    a.collisionFilter                     = 0x00020001u;
    b.collisionFilter                     = 0x00010002u;
    const VolumeFluidParticle particles[] = {a, b};
    REQUIRE(solver->emit(particles).ok());
    auto sphere = solver->querySphere({0.f, 1.f, 0.f}, .5f, 0.f, 0.f, 4, 15, 0x00010002u);
    REQUIRE(sphere.ok());
    REQUIRE(sphere.value().size() == 1);
    REQUIRE(sphere.value()[0].particleIndex == 0);
    VolumeFluidQueryShape query;
    query.center          = {0.f, 1.f, 0.f};
    query.size            = {.5f, 0.f, 0.f};
    query.collisionFilter = 0x00010002u;
    auto batch            = solver->queryBatch(std::span(&query, 1), 4);
    REQUIRE(batch.ok());
    REQUIRE(batch.value().size() == 1);
    REQUIRE(batch.value()[0].particleIndex == 0);
    query.collisionFilter = 0;
    REQUIRE(!solver->queryBatch(std::span(&query, 1), 4).ok());
    VolumeFluidParticle invalid = a;
    invalid.position.y          = 1.2f;
    invalid.collisionFilter     = 0;
    REQUIRE(!solver->emit(std::span(&invalid, 1)).ok());
    REQUIRE(solver->particleCount() == 2);
}

TEST_CASE("fluids.volume.particleInteractionsRequireReciprocalCollisionFilters") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 2;
    auto make           = [&](unsigned secondFilter) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position        = {0, 1, 0};
        particles[1].position        = {0, 1, 0};
        particles[0].collisionFilter = 0x00020001u;
        particles[1].collisionFilter = secondFilter;
        particles[0].actorGroup      = 1;
        particles[1].actorGroup      = 2;
        REQUIRE(solver->emit(particles).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto       oneWay   = make(0x00020002u);
    const auto isolated = oneWay->particles();
    REQUIRE(glm::length(isolated[0].position - isolated[1].position) == 0.f);
    REQUIRE(isolated[0].collisionFilter == 0x00020001u);
    REQUIRE(isolated[1].collisionFilter == 0x00020002u);
    auto       reciprocal  = make(0x00010002u);
    const auto interacting = reciprocal->particles();
    REQUIRE(glm::length(interacting[0].position - interacting[1].position) > .001f);
}

TEST_CASE("fluids.volume.actorGroupsRequireMutualSelfCollision") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 2;
    auto make           = [&](unsigned secondGroup, bool firstSelf, bool secondSelf) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particles[2];
        particles[0].position        = {0, 1, 0};
        particles[1].position        = {0, 1, 0};
        particles[0].collisionFilter = 0x00020001u;
        particles[1].collisionFilter = 0x00020002u;
        particles[0].actorGroup      = 7;
        particles[1].actorGroup      = secondGroup;
        particles[0].selfCollide     = firstSelf;
        particles[1].selfCollide     = secondSelf;
        REQUIRE(solver->emit(particles).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    REQUIRE(glm::length(make(7, true, true)->particles()[0].position - glm::vec3(0, 1, 0)) > .001f);
    REQUIRE(make(7, false, true)->particles()[0].position == glm::vec3(0, 1, 0));
    REQUIRE(make(7, true, false)->particles()[0].position == glm::vec3(0, 1, 0));
    REQUIRE(make(8, true, true)->particles()[0].position == glm::vec3(0, 1, 0));

    auto              current = make(7, true, true)->snapshot();
    auto              encoded = encodeVolumeFluid(current);
    eve::Value::Array particles;
    for (size_t i = 0; i < encoded.find("particles")->arraySize(); ++i) {
        const auto& source = encoded.find("particles")->at(i);
        eve::Value  legacy(eve::Value::Object{});
        for (const auto& key : source.keys())
            if (key != "actorGroup" && key != "selfCollide") legacy.set(key, *source.find(key));
        particles.push_back(withoutRecentMaterialFields(std::move(legacy)));
    }
    encoded.set("particles", eve::Value(std::move(particles)));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 8);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().particles[0].actorGroup == 0);
    REQUIRE(migrated.value().particles[0].selfCollide);
    VolumeFluidParticle invalid;
    invalid.actorGroup = 0x01000000u;
    auto created       = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    REQUIRE(!created.value()->emit(std::span(&invalid, 1)).ok());
}

TEST_CASE("fluids.volume.colliderInteractionsRequireReciprocalCollisionFilters") {
    VolumeFluidSettings settings;
    settings.gravity    = {0, 0, 0};
    settings.iterations = 1;
    auto make           = [&](unsigned colliderFilter) {
        auto created = VolumeFluid::create(settings);
        REQUIRE(created.ok());
        auto                solver = std::move(created).takeValue();
        VolumeFluidParticle particle;
        particle.position        = {0, 1, 0};
        particle.collisionFilter = 0x00020001u;
        VolumeFluidCollider collider;
        collider.center          = {0, 1, 0};
        collider.collisionFilter = colliderFilter;
        REQUIRE(solver->setColliders(std::span(&collider, 1)).ok());
        REQUIRE(solver->emit(std::span(&particle, 1)).ok());
        REQUIRE(solver->step(1.f / 120.f, 1).ok());
        return solver;
    };
    auto oneWay = make(0x00020002u);
    REQUIRE(oneWay->particles()[0].position == glm::vec3(0, 1, 0));
    REQUIRE(oneWay->contacts().empty());
    auto reciprocal = make(0x00010002u);
    REQUIRE(glm::length(reciprocal->particles()[0].position - glm::vec3(0, 1, 0)) > .001f);
    REQUIRE(reciprocal->contacts().size() == 1);
    auto before             = reciprocal->snapshot();
    auto invalid            = before.colliders[0];
    invalid.collisionFilter = 0;
    REQUIRE(!reciprocal->setColliders(std::span(&invalid, 1)).ok());
    REQUIRE(reciprocal->snapshot().colliders[0].collisionFilter == 0x00010002u);

    auto        encoded = encodeVolumeFluid(before);
    const auto& current = encoded.find("colliders")->at(0);
    eve::Value  oldCollider(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "collisionFilter" && key != "isTrigger") oldCollider.set(key, *current.find(key));
    const auto& currentParticle = encoded.find("particles")->at(0);
    eve::Value  oldParticle(eve::Value::Object{});
    for (const auto& key : currentParticle.keys())
        if (key != "actorGroup" && key != "selfCollide") oldParticle.set(key, *currentParticle.find(key));
    encoded.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(oldParticle))}));
    encoded.set("colliders", eve::Value(eve::Value::Array{oldCollider}));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 7);
    auto migrated = decodeVolumeFluid(encoded);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().version == 20);
    REQUIRE(migrated.value().colliders[0].collisionFilter == 0xffff0001u);
}

TEST_CASE("fluids.volume.orientedEllipsoidQueriesAndCanonicalDefaults") {
    VolumeFluidSettings settings;
    settings.spacing = .2f;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0.f, 1.f, 0.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    REQUIRE(solver->particles()[0].radii == glm::vec3(.1f));
    solver->clear();
    particle.radii       = {.2f, .05f, .05f};
    particle.orientation = {0.f, 0.f, .70710678f, .70710678f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    auto vertical = solver->querySphere({0.f, 1.3f, 0.f}, 0.f, 0.f, 1.f, 1);
    REQUIRE(vertical.ok());
    REQUIRE(vertical.value().size() == 1);
    REQUIRE(std::abs(vertical.value()[0].distance - .1f) < 1e-5f);
    auto horizontal = solver->querySphere({.3f, 1.f, 0.f}, 0.f, 0.f, 1.f, 1);
    REQUIRE(horizontal.ok());
    REQUIRE(std::abs(horizontal.value()[0].distance - .25f) < 1e-5f);
    auto ray = solver->raycast({0.f, .7f, 0.f}, {0.f, 1.f, 0.f}, 1.f, 1);
    REQUIRE(ray.ok());
    REQUIRE(ray.value().size() == 1);
    REQUIRE(std::abs(ray.value()[0].distance - .1f) < 1e-5f);
    REQUIRE(ray.value()[0].normal.y < -.999f);
    auto state = solver->snapshot();
    REQUIRE(state.version == 20);
    REQUIRE(state.particles[0].radii == particle.radii);
    VolumeFluidParticle invalid = particle;
    invalid.radii               = {.1f, 0.f, .1f};
    REQUIRE(!solver->emit(std::span(&invalid, 1)).ok());
    invalid             = particle;
    invalid.orientation = {0, 0, 0, 2};
    REQUIRE(!solver->emit(std::span(&invalid, 1)).ok());
    REQUIRE(solver->particleCount() == 1);
}

TEST_CASE("fluids.volume.solidAttachmentRollbackAndLifetimeRemap") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle expired, live, shortSolid;
    expired.position                      = {1, 1, 0};
    expired.life                          = .001f;
    live.position                         = {0, 1.25f, 0};
    shortSolid.position                   = {.25f, 1, 0};
    shortSolid.life                       = .001f;
    const VolumeFluidParticle particles[] = {expired, live, shortSolid};
    VolumeFluidCollider       c;
    c.label    = 17;
    c.solidify = true;
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->emit(particles).ok());
    REQUIRE(solver->step(1.f / 60.f).ok());
    const auto before = solver->snapshot();
    REQUIRE(before.particles.size() == 1);
    REQUIRE(before.attachments.size() == 1);
    REQUIRE(before.attachments[0].particleIndex == 0);
    VolumeFluidThermalRule badRule;
    badRule.colliderLabel = 17;
    REQUIRE(!solver->stepWithColliders(1.f / 60.f, 4, {}, std::span(&badRule, 1)).ok());
    REQUIRE(solver->snapshot().attachments.size() == 1);
    REQUIRE(solver->snapshot().colliders[0].label == 17);
    c.center.x = 10;
    REQUIRE(!solver->stepWithColliders(1.f / 60.f, 4, std::span(&c, 1)).ok());
    REQUIRE(solver->snapshot().colliders[0].center.x == 0.f);
    REQUIRE(glm::length(solver->particles()[0].position - before.particles[0].position) == 0.f);
    auto invalid                         = before;
    invalid.attachments[0].particleIndex = 1;
    REQUIRE(!solver->restore(invalid).ok());
    invalid                              = before;
    invalid.attachments[0].colliderLabel = 99;
    REQUIRE(!solver->restore(invalid).ok());
    invalid = before;
    invalid.attachments.push_back(invalid.attachments[0]);
    REQUIRE(!solver->restore(invalid).ok());
    REQUIRE(solver->snapshot().attachments.size() == 1);
    solver->clear();
    REQUIRE(solver->snapshot().attachments.empty());
}

TEST_CASE("fluids.volume.versionTwoMigratesWorldFixedSolids") {
    VolumeFluidSnapshot original;
    VolumeFluidParticle p;
    p.position       = {0, 1, 0};
    p.material.phase = VolumeFluidPhase::Solid;
    original.particles.push_back(p);
    auto        current         = encodeVolumeFluid(original);
    const auto& currentParticle = current.find("particles")->at(0);
    eve::Value  oldParticle(eve::Value::Object{});
    for (const auto& key : currentParticle.keys())
        if (key != "collisionFilter" && key != "radii" && key != "orientation" && key != "actorGroup" &&
            key != "selfCollide")
            oldParticle.set(key, *currentParticle.find(key));
    current.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(oldParticle))}));
    eve::Value old(eve::Value::Object{});
    for (const auto& key : current.keys())
        if (key != "attachments" && key != "simplexes") old.set(key, *current.find(key));
    old = withoutSdfColliders(old);
    old.set("version", 2);
    auto decoded = decodeVolumeFluid(old);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().version == 20);
    REQUIRE(decoded.value().attachments.empty());
    REQUIRE(decoded.value().particles[0].material.phase == VolumeFluidPhase::Solid);
    REQUIRE(decoded.value().particles[0].collisionFilter == 0xffff0001u);
    REQUIRE(decoded.value().particles[0].radii == glm::vec3(.05f));
    old.set("attachments", eve::Value(eve::Value::Array{}));
    REQUIRE(!decodeVolumeFluid(old).ok());
}

TEST_CASE("fluids.volume.outOfBoundsSolidificationRollsBackAttachment") {
    auto created = VolumeFluid::create({});
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidCollider c;
    c.solidify = true;
    c.center   = {0, 2.9f, 0};
    VolumeFluidParticle p;
    p.position = {0, 2.94f, 0};
    REQUIRE(solver->setColliders(std::span(&c, 1)).ok());
    REQUIRE(solver->emit(std::span(&p, 1)).ok());
    REQUIRE(!solver->step(1.f / 60.f).ok());
    REQUIRE(solver->snapshot().attachments.empty());
    REQUIRE(solver->contacts().empty());
    REQUIRE(solver->particles()[0].material.phase == VolumeFluidPhase::Liquid);
    REQUIRE(solver->particles()[0].position.y == p.position.y);
}

TEST_CASE("fluids.volume.solidificationPropagatesOneContactWave") {
    for (unsigned substeps : {1u, 4u})
        for (unsigned iterations : {1u, 10u}) {
            VolumeFluidSnapshot input;
            input.settings.gravity    = {0, 0, 0};
            input.settings.iterations = iterations;
            VolumeFluidCollider collider;
            collider.label    = 17;
            collider.radius   = .01f;
            collider.solidify = true;
            input.colliders.push_back(collider);
            for (unsigned i = 0; i < 4; ++i) {
                VolumeFluidParticle p;
                p.position           = {float(i) * .1f, 1, 0};
                p.material.cohesion  = 0;
                p.material.viscosity = 0;
                if (i == 0) p.material.phase = VolumeFluidPhase::Solid;
                input.particles.push_back(p);
            }
            input.attachments.push_back({0, 17, {0, 0, 0}});
            auto created = VolumeFluid::create(input.settings);
            REQUIRE(created.ok());
            auto solver = std::move(created).takeValue();
            REQUIRE(solver->restore(input).ok());
            for (unsigned wave = 1; wave <= 3; ++wave) {
                REQUIRE(solver->step(1.f / 120.f, substeps).ok());
                auto snapshot = solver->snapshot();
                REQUIRE(snapshot.attachments.size() == wave + 1);
                for (unsigned i = 0; i < 4; ++i)
                    REQUIRE((snapshot.particles[i].material.phase == VolumeFluidPhase::Solid) == (i <= wave));
            }
            collider.center.x = .1f;
            REQUIRE(solver->stepWithColliders(1.f / 120.f, 1, std::span(&collider, 1)).ok());
            for (unsigned i = 0; i < 4; ++i)
                REQUIRE(std::abs(solver->particles()[i].position.x - (float(i) * .1f + .1f)) < 1e-5f);
        }
}

TEST_CASE("fluids.volume.versionFiveMigratesAttachmentLocalOrientation") {
    VolumeFluidSnapshot current;
    VolumeFluidParticle particle;
    particle.position       = {0, 1, 0};
    particle.material.phase = VolumeFluidPhase::Solid;
    particle.radii          = glm::vec3(.05f);
    particle.orientation    = {0, 0, .70710678f, .70710678f};
    current.particles.push_back(particle);
    VolumeFluidCollider collider;
    collider.label    = 41;
    collider.rotation = {0, 0, .38268343f, .92387953f};
    current.colliders.push_back(collider);
    current.attachments.push_back({0, 41, {0, 1, 0}, {0, 0, .38268343f, .92387953f}});
    auto       encoded    = encodeVolumeFluid(current);
    auto       attachment = encoded.find("attachments")->at(0);
    eve::Value legacyAttachment(eve::Value::Object{});
    for (const auto& key : attachment.keys())
        if (key != "localOrientation" && key != "constrainOrientation" && key != "dynamic" && key != "compliance" &&
            key != "breakThreshold")
            legacyAttachment.set(key, *attachment.find(key));
    encoded.set("attachments", eve::Value(eve::Value::Array{legacyAttachment}));
    encoded = withoutSdfColliders(encoded);
    encoded.set("version", 5);
    const auto& encodedParticle = encoded.find("particles")->at(0);
    eve::Value  legacyParticle(eve::Value::Object{});
    for (const auto& key : encodedParticle.keys())
        if (key != "actorGroup" && key != "selfCollide") legacyParticle.set(key, *encodedParticle.find(key));
    encoded.set("particles", eve::Value(eve::Value::Array{withoutRecentMaterialFields(std::move(legacyParticle))}));
    const auto& encodedCollider = encoded.find("colliders")->at(0);
    eve::Value  legacyCollider(eve::Value::Object{});
    for (const auto& key : encodedCollider.keys())
        if (key != "collisionFilter") legacyCollider.set(key, *encodedCollider.find(key));
    encoded.set("colliders", eve::Value(eve::Value::Array{legacyCollider}));
    eve::Value legacy(eve::Value::Object{});
    for (const auto& key : encoded.keys())
        if (key != "simplexes") legacy.set(key, *encoded.find(key));
    auto decoded = decodeVolumeFluid(legacy);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().version == 20);
    REQUIRE(std::abs(decoded.value().attachments[0].localOrientation.z - .38268343f) < 1e-5f);
    REQUIRE(std::abs(decoded.value().attachments[0].localOrientation.w - .92387953f) < 1e-5f);
    REQUIRE(decoded.value().attachments[0].constrainOrientation);
}

TEST_CASE("fluids.volume.simplexQueriesReturnBarycentricPointEdgeAndTriangleResults") {
    VolumeFluidSettings settings;
    settings.gravity = {0, 0, 0};
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[3];
    particles[0].position = {-1, 1, 0};
    particles[1].position = {1, 1, 0};
    particles[2].position = {0, 2, 0};
    REQUIRE(solver->emit(particles).ok());
    VolumeFluidSimplex triangle{{0, 1, 2}, 3};
    REQUIRE(solver->setSimplexes(std::span(&triangle, 1)).ok());
    std::vector<VolumeFluidQueryShape> queries(3);
    queries[0].type        = VolumeFluidQueryType::Sphere;
    queries[0].center      = {0, .5f, 0};
    queries[0].size        = {.1f, 0, 0};
    queries[0].maxDistance = 2.f;
    queries[1].type        = VolumeFluidQueryType::Box;
    queries[1].center      = {0, 0, 0};
    queries[1].size        = {.4f, .4f, .4f};
    queries[1].maxDistance = 2.f;
    queries[2].type        = VolumeFluidQueryType::Ray;
    queries[2].center      = {0, 0, 0};
    queries[2].size        = {0, 3, 0};
    queries[2].maxDistance = 2.f;
    auto hits              = solver->querySimplexes(queries, 2);
    REQUIRE(hits.ok());
    REQUIRE(hits.value().size() == 3);
    REQUIRE(hits.value()[0].queryIndex == 0);
    REQUIRE(hits.value()[0].simplexIndex == 0);
    REQUIRE(std::abs(hits.value()[0].simplexBary.x - .5f) < 1e-5f);
    REQUIRE(std::abs(hits.value()[0].simplexBary.y - .5f) < 1e-5f);
    REQUIRE(std::abs(hits.value()[0].distance - .35f) < 1e-5f);
    REQUIRE(hits.value()[1].queryIndex == 1);
    REQUIRE(std::abs(hits.value()[1].distance - .75f) < 1e-5f);
    REQUIRE(hits.value()[2].queryIndex == 2);
    REQUIRE(hits.value()[2].distance < 0.f);
    REQUIRE(std::abs(hits.value()[2].simplexBary.x + hits.value()[2].simplexBary.y + hits.value()[2].simplexBary.z -
                     1.f) < 1e-5f);
    REQUIRE(solver->setSimplexes(std::span<const VolumeFluidSimplex>{}).ok());
    auto implicit = solver->querySimplexes(std::span(queries.data(), 1), 4);
    REQUIRE(implicit.ok());
    REQUIRE(implicit.value().size() == 3);
}
