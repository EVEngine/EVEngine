#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace eve::particles;

namespace {

[[maybe_unused]] void resetScalability() {
    auto& budget                = particleBudgetConfig();
    budget.maxParticles         = 0;
    budget.maxSimulatedEmitters = 0;
    budget.qualityLevel         = 3;
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    std::vector<ParticleEmitter*> existing;
    auto                          view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg] = *it;
        if (cfg->entity) existing.push_back(cfg->entity);
    }
    for (auto* emitter : existing) {
        emitter->stop();
        emitter->reset();
    }
}

[[maybe_unused]] ParticleEmitter* makeBudgetEmitter(int priority, float rate = 100.f) {
    auto* emitter = Particles::create()->newEmitter(256);
    emitter->setPriority(priority);
    emitter->setRandomSeed(1000 + priority);
    emitter->setEmissionRate(rate);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->start();
    return emitter;
}

}  // namespace

TEST_CASE("particles.scalability.priorityClaimsEmitterBudgetFirst") {
    resetScalability();
    auto* low                                   = makeBudgetEmitter(1);
    auto* high                                  = makeBudgetEmitter(100);
    auto* medium                                = makeBudgetEmitter(50);
    particleBudgetConfig().maxSimulatedEmitters = 2;

    ParticleSimSystem::update(0.1f);
    REQUIRE_EQ(high->getCount(), 10);
    REQUIRE_EQ(medium->getCount(), 10);
    REQUIRE_EQ(low->getCount(), 0);
    REQUIRE_EQ(particleFrameStats().emittersSimulated, 2);
    REQUIRE_EQ(particleFrameStats().emittersBudgetSkipped, 1);
}

TEST_CASE("particles.scalability.globalParticleBudgetDropsExcessSpawns") {
    resetScalability();
    auto* high                          = makeBudgetEmitter(100);
    auto* low                           = makeBudgetEmitter(1);
    particleBudgetConfig().maxParticles = 12;

    ParticleSimSystem::update(1.f);
    REQUIRE_EQ(high->getCount(), 12);
    REQUIRE_EQ(low->getCount(), 0);
    REQUIRE_EQ(particleFrameStats().particlesAfter, 12);
    REQUIRE_EQ(particleFrameStats().particlesSpawned, 12);
    REQUIRE_GT(particleFrameStats().droppedSpawns, 0);
}

TEST_CASE("particles.scalability.perEmitterSpawnCapIsObservable") {
    resetScalability();
    auto* emitter = makeBudgetEmitter(1);
    emitter->setMaxSpawnPerFrame(7);

    ParticleSimSystem::update(1.f);
    REQUIRE_EQ(emitter->getCount(), 7);
    REQUIRE_EQ(particleFrameStats().particlesSpawned, 7);
    REQUIRE_GT(particleFrameStats().droppedSpawns, 0);
}

TEST_CASE("particles.scalability.qualityAndDistanceCulling") {
    resetScalability();
    auto* emitter = makeBudgetEmitter(1);
    emitter->setMinimumQuality(3);
    particleBudgetConfig().qualityLevel = 2;
    ParticleSimSystem::update(0.1f);
    REQUIRE_EQ(emitter->getCount(), 0);
    REQUIRE_EQ(particleFrameStats().emittersQualitySkipped, 1);

    particleBudgetConfig().qualityLevel = 3;
    auto* camera                        = eve::graphics::Camera2D::createCamera();
    camera->setPosition(0.f, 0.f);
    emitter->setCamera(camera);
    emitter->setPosition(1000.f, 0.f);
    emitter->setCullDistance(100.f);
    emitter->setCullingMode("pause");
    ParticleSimSystem::update(0.1f);
    REQUIRE_EQ(emitter->getCount(), 0);
    REQUIRE_EQ(particleFrameStats().emittersCulled, 1);

    emitter->setCullingMode("always");
    ParticleSimSystem::update(0.1f);
    REQUIRE_EQ(emitter->getCount(), 10);
}

TEST_CASE("particles.scalability.configAppliesBudgetPolicy") {
    resetScalability();
    auto* emitter = Particles::create()->newEmitter(16);
    REQUIRE(emitter->applyConfig(R"({
        "priority": 42,
        "minimumQuality": 2,
        "cullingMode": "pause",
        "cullDistance": 800,
        "maxSpawnPerFrame": 33
    })"));
    CHECK_EQ(emitter->getPriority(), 42);
    CHECK_EQ(emitter->getMinimumQuality(), 2);
    CHECK_EQ(emitter->getCullingMode(), "pause");
    CHECK(std::abs(emitter->getCullDistance() - 800.f) < 1e-6f);
    CHECK_EQ(emitter->getMaxSpawnPerFrame(), 33);
}
