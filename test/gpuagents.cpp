#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "gpuagents/AgentInstanceRenderer.h"
#include "gpuagents/EffectBackend.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/GpuAgents.h"
#include "gpuagents/ObstacleField.h"
#include "gpuagents/solvers/FishSolver.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace eve::gpuagents;

namespace {

float meanNeighborDistance(const std::vector<AgentState>& states) {
    double sum   = 0.0;
    int    pairs = 0;
    for (size_t i = 0; i < states.size(); ++i) {
        if (!states[i].alive) continue;
        for (size_t j = i + 1; j < states.size(); ++j) {
            if (!states[j].alive) continue;
            sum += glm::length(states[i].position - states[j].position);
            ++pairs;
        }
    }
    return pairs > 0 ? static_cast<float>(sum / pairs) : 0.f;
}

}  // namespace

TEST_CASE("gpuagents.obstacle_resolve_keeps_tangential") {
    ObstacleField field;
    field.bakeEmpty(glm::vec3(-5.f), glm::ivec3(32, 32, 32), 0.35f);
    field.carveSphere(glm::vec3(0.f), 1.5f);

    // Offset from the exact center so the SDF gradient is well-defined.
    glm::vec3 pos(0.15f, 0.05f, -0.1f);
    glm::vec3 vel(1.f, 0.f, 0.5f);
    const float beforeDist = field.sample(pos).distance;
    field.resolve(pos, vel, 0.25f, 0.3f);
    field.resolve(pos, vel, 0.25f, 0.3f);  // second pass for discrete-grid residual

    const auto sample = field.sample(pos);
    CHECK(sample.distance > beforeDist);
    CHECK(sample.distance >= 0.24f);
    // Tangential component should remain after canceling inward normal.
    CHECK(glm::length(glm::vec2(vel.x, vel.z)) > 0.2f);
}

TEST_CASE("gpuagents.fish_separation_and_cohesion") {
    EffectBackend backend;
    auto          profile = EffectProfile::makeFish(32);
    profile.fish.separationWeight = 4.0f;
    profile.fish.cohesionWeight   = 0.0f;
    profile.fish.alignmentWeight  = 0.0f;
    profile.fish.goalWeight       = 0.0f;
    profile.fish.depthWeight      = 0.0f;
    profile.fish.currentWeight    = 0.0f;
    profile.fish.maxSpeed         = 3.f;
    REQUIRE(backend.configure(profile, "fish").ok());

    REQUIRE(backend.simulation()
                .initialize(16,
                            [](int i) {
                                AgentState s;
                                // Tight cluster to trigger separation.
                                s.position = glm::vec3((i % 4) * 0.05f, 0.f, (i / 4) * 0.05f);
                                s.velocity = glm::vec3(0.05f, 0.f, 0.02f);
                                return s;
                            })
                .ok());

    const float meanBeforeSep = meanNeighborDistance(backend.simulation().states());
    EnvironmentSnapshot env;
    ObstacleField       empty;
    env.obstacles = &empty;
    for (int i = 0; i < 120; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    const float meanAfterSep = meanNeighborDistance(backend.simulation().states());
    CHECK(meanAfterSep > meanBeforeSep);

    // Reconfigure with cohesion-dominant weights on a spread flock.
    profile.fish.separationWeight = 0.2f;
    profile.fish.cohesionWeight   = 2.0f;
    profile.fish.alignmentWeight  = 0.2f;
    REQUIRE(backend.configure(profile, "fish-cohesion").ok());
    REQUIRE(backend.simulation()
                .initialize(16,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3((i % 4) * 3.f, 0.f, (i / 4) * 3.f);
                                s.velocity = glm::vec3(0.5f, 0.f, 0.f);
                                return s;
                            })
                .ok());
    const float meanBefore = meanNeighborDistance(backend.simulation().states());
    for (int i = 0; i < 180; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    const float meanAfter = meanNeighborDistance(backend.simulation().states());
    CHECK(meanAfter < meanBefore);
}

TEST_CASE("gpuagents.life_trail_deposit_and_follow") {
    GpuAgentWorld world;
    world.surface().initFlat(glm::vec3(-8.f, 0.f, -8.f), 16.f, 32, 0.f);

    auto backend = std::make_unique<EffectBackend>();
    auto profile = EffectProfile::makeLife(8, 32);
    profile.life.depositStrength = 0.8f;
    profile.life.trailDecayRate  = 0.02f;
    profile.life.trailFollow     = 2.0f;
    profile.life.moveSpeed       = 1.2f;
    REQUIRE(backend->configure(profile, "life").ok());
    REQUIRE(backend->simulation()
                .initialize(4,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(-2.f + i * 0.5f, 0.f, 0.f);
                                s.velocity = glm::vec3(1.f, 0.f, 0.f);
                                return s;
                            })
                .ok());

    EffectBackend* raw = backend.get();
    REQUIRE(world.registerBackend("life", raw).ok());

    for (int i = 0; i < 60; ++i) {
        REQUIRE(world.stepAll(1.f / 60.f).ok());
    }
    float trailSum = 0.f;
    for (const auto& c : world.surface().lifeField) trailSum += c.r;
    CHECK(trailSum > 0.5f);
    CHECK(raw->simulation().aliveCount() == 4);
    CHECK(raw->renderer().instanceCount() == 4);
}

TEST_CASE("gpuagents.bird_speed_band_and_stall_recovery") {
    EffectBackend backend;
    auto          profile = EffectProfile::makeBird(8);
    profile.bird.stallSpeed  = 2.5f;
    profile.bird.cruiseSpeed = 6.f;
    profile.bird.maxSpeed    = 9.f;
    REQUIRE(backend.configure(profile).ok());
    REQUIRE(backend.simulation()
                .initialize(4,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(i * 2.f, 5.f, 0.f);
                                // Start nearly stalled.
                                s.velocity = glm::vec3(0.4f, -0.2f, 0.f);
                                return s;
                            })
                .ok());

    EnvironmentSnapshot env;
    env.groundY  = 0.f;
    env.ceilingY = 40.f;
    ObstacleField empty;
    env.obstacles = &empty;
    for (int i = 0; i < 120; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    for (const auto& s : backend.simulation().states()) {
        if (!s.alive) continue;
        const float speed = glm::length(s.velocity);
        CHECK(speed >= profile.bird.stallSpeed * 0.45f);
        CHECK(speed <= profile.bird.maxSpeed + 0.05f);
        // Stall recovery should not leave birds buried under ground clearance forever.
        CHECK(s.position.y > env.groundY);
    }
}

TEST_CASE("gpuagents.petal_settles_on_ground") {
    EffectBackend backend;
    auto          profile = EffectProfile::makePetal(16);
    profile.petal.groundY     = 0.f;
    profile.petal.lifetime    = 30.f;
    profile.petal.settleSpeed = 0.5f;
    REQUIRE(backend.configure(profile).ok());
    REQUIRE(backend.simulation()
                .initialize(8,
                            [](int i) {
                                AgentState s;
                                s.position      = glm::vec3((i % 4) * 0.5f, 1.5f + 0.2f * i, (i / 4) * 0.5f);
                                s.velocity      = glm::vec3(0.f, -0.2f, 0.f);
                                s.customData[1] = 1.f;
                                return s;
                            })
                .ok());

    EnvironmentSnapshot env;
    env.windVelocity = glm::vec3(0.2f, 0.f, 0.f);
    ObstacleField empty;
    env.obstacles = &empty;
    for (int i = 0; i < 240; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    int settled = 0;
    for (const auto& s : backend.simulation().states()) {
        if (!s.alive) continue;
        if (s.customData[0] > 0.5f) ++settled;
    }
    CHECK(settled >= 1);
}

TEST_CASE("gpuagents.world_registers_and_module_spawn") {
    GpuAgents module;
    auto*     world = module.newWorld();
    REQUIRE(world != nullptr);
    world->obstacles().bakeEmpty(glm::vec3(-10.f), glm::ivec3(16, 16, 16), 1.f);
    world->obstacles().carveSphere(glm::vec3(0.f, 0.f, 0.f), 2.f);
    world->setWaterCurrent(0.2f, 0.f, 0.f);

    std::unique_ptr<EffectBackend> backend(module.newBackend(0, 32));
    REQUIRE(backend != nullptr);
    REQUIRE(module.spawnCloud(backend.get(), 12, 0.f, 1.f, 5.f, 1.5f).ok());
    REQUIRE(world->registerBackend("school", backend.get()).ok());
    CHECK(world->backendCount() == 1);

    for (int i = 0; i < 30; ++i) {
        REQUIRE(world->stepAll(1.f / 60.f).ok());
    }
    auto* found = world->find("school");
    REQUIRE(found != nullptr);
    CHECK(found->simulation().aliveCount() == 12);
    CHECK(found->renderer().instanceCount() == 12);

    // Agents that start inside the carved sphere must be pushed out.
    EffectBackend trapped;
    auto          trappedProfile = EffectProfile::makeFish(4);
    REQUIRE(trapped.configure(trappedProfile).ok());
    REQUIRE(trapped.simulation()
                .initialize(2,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(0.1f * i, 0.f, 0.f);
                                s.velocity = glm::vec3(0.5f, 0.f, 0.f);
                                return s;
                            })
                .ok());
    REQUIRE(world->registerBackend("trapped", &trapped).ok());
    for (int i = 0; i < 30; ++i) {
        REQUIRE(world->stepAll(1.f / 60.f).ok());
    }
    for (const auto& s : trapped.simulation().states()) {
        if (!s.alive) continue;
        CHECK(glm::length(s.position) > 1.7f);
    }
    delete world;
}

TEST_CASE("gpuagents.reset_clears_alive") {
    EffectBackend backend;
    REQUIRE(backend.configure(EffectProfile::makeFish(8)).ok());
    REQUIRE(backend.simulation()
                .initialize(5,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(i, 0, 0);
                                s.velocity = glm::vec3(1, 0, 0);
                                return s;
                            })
                .ok());
    CHECK(backend.simulation().aliveCount() == 5);
    backend.reset();
    CHECK(backend.simulation().aliveCount() == 0);
    CHECK(backend.renderer().sync(backend.simulation().states()) == 0);
}
