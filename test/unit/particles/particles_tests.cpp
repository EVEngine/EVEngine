// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "particles_effect_asset.cpp",
    "particles_gpu_resident.cpp",
    "particles_p2_p3.cpp",
    "particles_playback.cpp",
    "particles_renderer.cpp",
    "particles_scalability.cpp",
    "particles.cpp",
    "pcg_material_selector.cpp"
  ]
}
*/
// clang-format on

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_effect_asset)
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"

namespace test_particle_effect_asset_contract {
TEST_CASE("particles.ParticleEffect parse: named emitters retain configuration and parent transforms") {
    auto parsed = eve::particles::ParticleEffect::tryFromText(R"({
        "type":"eve.particle-effect", "version":1,
        "parameters":{"intensity":2.0},
        "emitters":[
            {"name":"core","offset":[10,0],"parameters":{"intensity":3.0},
             "emitter":{"buffer":32,"emissionRate":0,"layer":3}},
            {"name":"haze","enabled":false,"offset":[-4,6],
             "emitter":{"buffer":8,"emissionRate":0,"layer":-2}}
        ]
    })");
    REQUIRE(parsed.ok());
    std::unique_ptr<eve::particles::ParticleEffect> effect(parsed.value());
    REQUIRE_EQ(effect->getEmitterCount(), 2);
    auto* core = effect->getEmitterByName("core");
    auto* haze = effect->getEmitterByName("haze");
    REQUIRE(core != nullptr);
    REQUIRE(haze != nullptr);
    CHECK_EQ(core->getBufferSize(), 32);
    CHECK_EQ(effect->getEmitter(2), nullptr);
    effect->setPosition(100.0f, 200.0f);
    effect->setScale(2.0f);
    CHECK_EQ(core->getX(), 120.0f);
    CHECK_EQ(core->getY(), 200.0f);
    CHECK_EQ(haze->getX(), 92.0f);
    CHECK_EQ(haze->getY(), 212.0f);
    effect->setLayer(10);
    CHECK_EQ(core->getLayer(), 13);
    CHECK_EQ(haze->getLayer(), 8);
    effect->start();
    CHECK(core->isActive());
    CHECK(!haze->isActive());
    CHECK(effect->emit("core", 3));
    CHECK(!effect->emit("missing", 3));
}

TEST_CASE("particles.ParticleEffect parse: unsupported versions and duplicate names reject") {
    SUB_CASE("future version") {
        auto result = eve::particles::ParticleEffect::tryFromText(
            R"({"type":"eve.particle-effect","version":3,"emitters":[{"name":"a","emitter":{}}]})");
        CHECK(!result.ok());
    };
    SUB_CASE("duplicate emitter names") {
        auto result = eve::particles::ParticleEffect::tryFromText(
            R"({"type":"eve.particle-effect","version":1,"emitters":[{"name":"a","emitter":{}},{"name":"a","emitter":{}}]})");
        REQUIRE(!result.ok());
    };
}

TEST_CASE("particles.ParticleEffect reload: valid replacement commits and invalid replacement is atomic") {
    auto parsed = eve::particles::ParticleEffect::tryFromText(
        R"({"type":"eve.particle-effect","version":1,"emitters":[{"name":"old","emitter":{"buffer":8}}]})");
    REQUIRE(parsed.ok());
    std::unique_ptr<eve::particles::ParticleEffect> effect(parsed.value());
    effect->setPosition(12.0f, 34.0f);
    REQUIRE(effect
                ->reloadFromText(
                    R"({"type":"eve.particle-effect","version":2,"emitters":[{"name":"new","emitter":{"buffer":16}}]})")
                .ok());
    REQUIRE_EQ(effect->getEmitterCount(), 1);
    REQUIRE(effect->getEmitterByName("new") != nullptr);
    CHECK_EQ(effect->getEmitterByName("new")->getBufferSize(), 16);
    CHECK_EQ(effect->getX(), 12.0f);
    auto rejected = effect->reloadFromText(R"({"type":"eve.particle-effect","version":2,"emitters":[]})");
    CHECK(!rejected.ok());
    REQUIRE_EQ(effect->getEmitterCount(), 1);
    CHECK(effect->getEmitterByName("new") != nullptr);
}

TEST_CASE("particles.ParticleEffect naturalDuration: bounded emitters include tail while loops are unsupported") {
    auto finite = eve::particles::ParticleEffect::tryFromText(R"({
        "type":"eve.particle-effect", "version":1,
        "emitters":[
            {"name":"short","emitter":{"emitterLife":0.3,"particleLifetime":[0.2,1.2]}},
            {"name":"long","emitter":{"emitterLife":0.8,"particleLifetime":[0.2,0.9]}}
        ]
    })");
    REQUIRE(finite.ok());
    std::unique_ptr<eve::particles::ParticleEffect> effect(finite.value());
    auto                                            duration = effect->naturalDuration();
    REQUIRE(duration.ok());
    CHECK(std::fabs(duration.value().seconds() - 1.7) < 0.001);
    auto loop = eve::particles::ParticleEffect::tryFromText(
        R"({"type":"eve.particle-effect","version":1,"emitters":[{"name":"loop","emitter":{"emitterLife":-1,"looping":true}}]})");
    REQUIRE(loop.ok());
    std::unique_ptr<eve::particles::ParticleEffect> looping(loop.value());
    CHECK(!looping->naturalDuration().ok());
}
}  // namespace test_particle_effect_asset_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_gpu_resident)
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"

namespace test_particle_gpu_contract {
TEST_CASE("particles.ParticleSimSystem update: unsupported collision falls back to CPU without losing particles") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(64);
    REQUIRE(emitter != nullptr);
    emitter->setRandomSeed(8128);
    emitter->setGpuSimulation(true);
    emitter->setCollision("bounce", 2.0f, 0.5f, 0.0f);
    emitter->setEmissionRate(40.0f);
    emitter->setParticleLifetime(2.0f, 2.0f);
    emitter->start();
    eve::particles::ParticleSimSystem::update(0.25f);
    CHECK(emitter->getGpuSimulation());
    CHECK(!emitter->gpuSim()->residentActive);
    CHECK_EQ(emitter->gpuSim()->residentHandle, std::uint64_t(0));
    CHECK(emitter->getCount() > 0);
    CHECK_EQ(emitter->getCount(), emitter->sim()->alive);
    emitter->release();
}
}  // namespace test_particle_gpu_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_p2_p3)
#include "common/ParticleSdf.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleEmitterPool.h"
#include "particles/Particles.h"

namespace test_particle_pool_contract {
class CircleField final : public eve::IParticleSdfField {
public:
    float                          sample(float x, float y) const override { return std::sqrt(x * x + y * y) - 8.0f; }
    eve::ParticleSdfGradientStatus gradient(float x, float y, float& nx, float& ny) const override {
        const float length = std::sqrt(x * x + y * y);
        nx                 = length > 0.0f ? x / length : 0.0f;
        ny                 = length > 0.0f ? y / length : 1.0f;
        return eve::ParticleSdfGradientStatus::Defined;
    }
};

TEST_CASE("particles.ParticleEmitterPool recycle: re-acquire resets count and retains capacity") {
    eve::particles::ParticleEmitterPool pool;
    auto                                acquired = pool.acquire(128);
    REQUIRE(acquired.ok());
    auto* emitter = acquired.value();
    REQUIRE(emitter != nullptr);
    emitter->emit(3);
    CHECK_EQ(emitter->getCount(), 3);
    REQUIRE(pool.recycle(emitter).ok());
    CHECK_EQ(pool.idleCount(), 1u);
    auto reused = pool.acquire(64);
    REQUIRE(reused.ok());
    CHECK_EQ(reused.value(), emitter);
    CHECK_EQ(emitter->getCount(), 0);
    CHECK_EQ(emitter->getBufferSize(), 128);
    REQUIRE(pool.recycle(emitter).ok());
}

TEST_CASE("particles.ParticleEmitter SDF collision: inbound particle dies and GPU fallback is observable") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(32);
    REQUIRE(emitter != nullptr);
    emitter->setPosition(0.0f, 0.0f);
    emitter->setEmissionRate(0.0f);
    emitter->setParticleLifetime(2.0f, 2.0f);
    emitter->setSpeed(0.0f, 0.0f);
    emitter->setCollision("kill", 2.0f);
    CircleField field;
    emitter->setSdfField(&field);
    emitter->setGpuSimulation(true);
    CHECK(!emitter->isGpuFeatureSetSupported());
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("sdf"));
    emitter->emit(1);
    CHECK_EQ(emitter->getCount(), 1);
    eve::particles::Particles::create()->update(1.0f / 60.0f);
    CHECK_EQ(emitter->getCount(), 0);
    emitter->setSdfField(nullptr);
    emitter->release();
}
}  // namespace test_particle_pool_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_playback)
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
namespace test_particle_playback_contract {
void deterministic(eve::particles::ParticleEmitter* emitter, int seed) {
    emitter->setRandomSeed(seed);
    emitter->setParticleLifetime(0.5f, 2.0f);
    emitter->setDirection(0.7f);
    emitter->setSpread(1.2f);
    emitter->setSpeed(10.0f, 80.0f);
    emitter->setSizeVariation(0.5f);
}

TEST_CASE("particles.ParticleEmitter randomSeed/reset: same seed reproduces sampled particle properties") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(8);
    REQUIRE(emitter != nullptr);
    deterministic(emitter, 42);
    emitter->start();
    emitter->emit(1);
    const eve::particles::Particle first = emitter->sim()->particles[0];
    emitter->reset();
    emitter->start();
    emitter->emit(1);
    const eve::particles::Particle replay = emitter->sim()->particles[0];
    CHECK_EQ(first.life, replay.life);
    CHECK_EQ(first.vx, replay.vx);
    CHECK_EQ(first.vy, replay.vy);
    CHECK_EQ(first.size, replay.size);
    emitter->release();
}

TEST_CASE("particles.advanceEmitterSim: fixed step accumulates time and distance emits spaced particles") {
    SUB_CASE("fixed step and playback speed") {
        auto* emitter = eve::particles::Particles::create()->newEmitter(4);
        emitter->setRandomSeed(7);
        emitter->setParticleLifetime(10.0f, 10.0f);
        emitter->setDirection(0.0f);
        emitter->setSpread(0.0f);
        emitter->setSpeed(10.0f, 10.0f);
        emitter->setFixedTimeStep(0.1f, 4);
        emitter->setPlaybackSpeed(2.0f);
        emitter->emit(1);
        eve::particles::advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.025f);
        CHECK(std::fabs(emitter->sim()->particles[0].x) < 1e-6f);
        eve::particles::advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.025f);
        CHECK(std::fabs(emitter->sim()->particles[0].x - 1.0f) < 1e-5f);
        emitter->release();
    };
    SUB_CASE("emission over traveled distance") {
        auto* emitter = eve::particles::Particles::create()->newEmitter(16);
        emitter->setRandomSeed(10);
        emitter->setParticleLifetime(10.0f, 10.0f);
        emitter->setSpeed(0.0f, 0.0f);
        emitter->setEmissionRateOverDistance(0.5f);
        emitter->setPosition(0.0f, 0.0f);
        emitter->start();
        eve::particles::advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);
        emitter->setPosition(10.0f, 0.0f);
        eve::particles::advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);
        REQUIRE_EQ(emitter->getCount(), 5);
        for (int index = 0; index < 5; ++index)
            CHECK(std::fabs(emitter->sim()->particles[std::size_t(index)].x - float((index + 1) * 2)) < 1e-5f);
        emitter->release();
    };
}
}  // namespace test_particle_playback_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_renderer)
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"

namespace test_particle_renderer_contract {
TEST_CASE("particles.ParticleEmitter renderer settings: mode aliases and unsupported values normalize") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(16);
    REQUIRE(emitter != nullptr);
    emitter->setRenderMode("axis");
    emitter->setRenderAxis(37.5f);
    emitter->setSortMode("distance");
    CHECK_EQ(emitter->config()->renderMode, std::string("axis"));
    CHECK_EQ(emitter->config()->renderAxisDegrees, 37.5f);
    CHECK_EQ(emitter->getSortMode(), std::string("distance"));
    emitter->setRenderMode("velocity", 2.25f);
    CHECK_EQ(emitter->config()->renderMode, std::string("stretched"));
    CHECK_EQ(emitter->config()->stretchFactor, 2.25f);
    emitter->setSortMode("unsupported");
    CHECK_EQ(emitter->getSortMode(), std::string("none"));
    emitter->setMaterialMode("unsupported");
    CHECK_EQ(emitter->getMaterialMode(), std::string("unlit"));
    emitter->release();
}

TEST_CASE("particles.ParticleEmitter applyConfig: ribbon and soft-particle settings are observable") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(16);
    REQUIRE(emitter != nullptr);
    REQUIRE(emitter->applyConfig(R"({
        "renderMode":"axis", "renderAxis":-22.5, "sortMode":"youngest",
        "ribbon":{"width":0.75,"minSegmentLength":2.5},
        "softParticles":{"enabled":true,"depth":0.42,"fadeDistance":0.08},
        "material":{"mode":"lit","normalTexture":"particles/test-normal.png"}
    })"));
    CHECK_EQ(emitter->config()->renderMode, std::string("ribbon"));
    CHECK_EQ(emitter->getSortMode(), std::string("youngest"));
    CHECK_EQ(emitter->config()->ribbonWidth, 0.75f);
    CHECK(emitter->config()->softParticles);
    CHECK_EQ(emitter->config()->softParticleDepth, 0.42f);
    CHECK_EQ(emitter->getMaterialMode(), std::string("lit"));
    CHECK(!emitter->isSoftParticlesActive());
    emitter->release();
}
}  // namespace test_particle_renderer_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_scalability)
#include <vector>
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"

namespace test_particle_scalability_contract {
void resetBudget() {
    auto& budget                = eve::particles::particleBudgetConfig();
    budget.maxParticles         = 0;
    budget.maxSimulatedEmitters = 0;
    budget.qualityLevel         = 3;
    if (ecs::current()->getManager<eve::particles::ParticleEmitter>() == nullptr) return;
    std::vector<eve::particles::ParticleEmitter*> existing;
    auto view = ecs::View<eve::particles::ParticleEmitter, eve::particles::ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [configuration] = *it;
        if (configuration->entity) existing.push_back(configuration->entity);
    }
    for (auto* emitter : existing) {
        emitter->stop();
        emitter->reset();
    }
}

eve::particles::ParticleEmitter* budgetEmitter(int priority) {
    auto* emitter = eve::particles::Particles::create()->newEmitter(32);
    emitter->setPriority(priority);
    emitter->setRandomSeed(1000 + priority);
    emitter->setEmissionRate(10.0f);
    emitter->setParticleLifetime(10.0f, 10.0f);
    emitter->start();
    return emitter;
}

TEST_CASE("particles.ParticleSimSystem budget: higher priority claims the limited emitter slots") {
    resetBudget();
    auto* low                                                   = budgetEmitter(1);
    auto* high                                                  = budgetEmitter(100);
    auto* middle                                                = budgetEmitter(50);
    eve::particles::particleBudgetConfig().maxSimulatedEmitters = 2;
    eve::particles::ParticleSimSystem::update(0.1f);
    CHECK_EQ(high->getCount(), 1);
    CHECK_EQ(middle->getCount(), 1);
    CHECK_EQ(low->getCount(), 0);
    CHECK_EQ(eve::particles::particleFrameStats().emittersSimulated, 2);
    REQUIRE_EQ(eve::particles::particleFrameStats().emittersBudgetSkipped, 1);
    eve::particles::particleBudgetConfig().maxSimulatedEmitters = 0;
    high->release();
    middle->release();
    low->release();
}

TEST_CASE("particles.ParticleEmitter spawn cap: excess work is counted instead of allocated") {
    resetBudget();
    auto* emitter = budgetEmitter(1);
    emitter->setEmissionRate(100.0f);
    emitter->setMaxSpawnPerFrame(3);
    eve::particles::ParticleSimSystem::update(0.1f);
    CHECK_EQ(emitter->getCount(), 3);
    CHECK_EQ(eve::particles::particleFrameStats().particlesSpawned, 3);
    REQUIRE(eve::particles::particleFrameStats().droppedSpawns > 0);
    emitter->release();
}
}  // namespace test_particle_scalability_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles)
#include "particles/FloatingPointFixParticles.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"

namespace test_particle_emitter_contract {
TEST_CASE("particles.Particles newEmitter: empty capacity is observable and emit is bounded") {
    auto*     module  = eve::particles::Particles::create();
    const int before  = module->getEmitterCount();
    auto*     emitter = module->newEmitter(4);
    REQUIRE(emitter != nullptr);
    CHECK_EQ(module->getEmitterCount(), before + 1);
    CHECK_EQ(emitter->getBufferSize(), 4);
    CHECK_EQ(emitter->getCount(), 0);
    CHECK_EQ(emitter->config()->entity, emitter);
    emitter->setParticleLifetime(10.0f, 10.0f);
    emitter->emit(100);
    CHECK_EQ(emitter->getCount(), 4);
    emitter->reset();
    CHECK_EQ(emitter->getCount(), 0);
    emitter->release();
}

TEST_CASE("particles.ParticleEmitter lifecycle: start emits and pause/stop prevent advancement") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(16);
    REQUIRE(emitter != nullptr);
    emitter->setRandomSeed(11);
    emitter->setEmissionRate(10.0f);
    emitter->setParticleLifetime(5.0f, 5.0f);
    CHECK(emitter->isStopped());
    emitter->start();
    CHECK(emitter->isActive());
    eve::particles::ParticleSimSystem::update(0.2f);
    CHECK_EQ(emitter->getCount(), 2);
    emitter->pause();
    const int pausedCount = emitter->getCount();
    eve::particles::ParticleSimSystem::update(0.2f);
    CHECK_EQ(emitter->getCount(), pausedCount);
    emitter->stop();
    CHECK(emitter->isStopped());
    emitter->release();
}

TEST_CASE("particles.ParticleEmitter simulation: lifetime expires and collision responses are distinct") {
    SUB_CASE("fixed lifetime") {
        auto* emitter = eve::particles::Particles::create()->newEmitter(8);
        emitter->setParticleLifetime(1.0f, 1.0f);
        emitter->setSpeed(0.0f, 0.0f);
        emitter->emit(2);
        eve::particles::ParticleSimSystem::update(0.5f);
        CHECK_EQ(emitter->getCount(), 2);
        eve::particles::ParticleSimSystem::update(0.6f);
        CHECK_EQ(emitter->getCount(), 0);
        emitter->release();
    };
    SUB_CASE("out-of-bounds particle is killed") {
        auto* emitter = eve::particles::Particles::create()->newEmitter(8);
        emitter->setParticleLifetime(5.0f, 5.0f);
        emitter->setSpeed(0.0f, 0.0f);
        emitter->setPosition(0.0f, 0.0f);
        emitter->setCollision("kill");
        emitter->setCollisionBounds(true, 10.0f, 0.0f, 100.0f, 100.0f);
        emitter->emit(1);
        eve::particles::ParticleSimSystem::update(0.1f);
        REQUIRE_EQ(emitter->getCount(), 0);
        emitter->release();
    };
}

TEST_CASE("particles.shiftWorldSpaceParticles: world coordinates move while playback and local particles stay") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(8);
    REQUIRE(emitter != nullptr);
    emitter->setAutoRandomSeed(false);
    emitter->setRandomSeed(7);
    emitter->setPosition(1200.0f, 45.0f);
    emitter->emit(1);
    emitter->pause();
    const auto before = emitter->sim()->particles[0];
    REQUIRE(static_cast<bool>(eve::particles::shiftWorldSpaceParticles(emitter, 1000.0f, 0.0f)));
    CHECK(emitter->isPaused());
    CHECK_EQ(emitter->getX(), 1200.0f);
    CHECK_EQ(emitter->sim()->particles[0].x, before.x - 1000.0f);
    CHECK_EQ(emitter->sim()->particles[0].life, before.life);
    emitter->setSimulationSpace("local");
    const float local = emitter->sim()->particles[0].x;
    CHECK(!eve::particles::shiftWorldSpaceParticles(emitter, 1.0f, 0.0f));
    CHECK_EQ(emitter->sim()->particles[0].x, local);
    emitter->release();
}

TEST_CASE("particles.ParticleEmitter applyConfig: malformed JSON rejects without changing capacity") {
    auto* emitter = eve::particles::Particles::create()->newEmitter(8);
    REQUIRE(emitter != nullptr);
    CHECK(!emitter->applyConfig("{bad"));
    CHECK(!emitter->applyConfig("[1,2,3]"));
    CHECK_EQ(emitter->getBufferSize(), 8);
    REQUIRE(emitter->applyConfig(R"({"emissionRate":3.0,"particleLifetime":[1.0,1.0],"layer":3})"));
    CHECK_EQ(emitter->getEmissionRate(), 3.0f);
    CHECK_EQ(emitter->getLayer(), 3);
    emitter->release();
}
}  // namespace test_particle_emitter_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_material_selector)
#include "graphics/Texture.h"
#include "particles/ParticleEmitter.h"
#include "particles/PcgMaterialSelector.h"

namespace test_particle_material_selector_contract {
TEST_CASE("particles.PcgMaterialSelector selectAndApply: seed repeats and reserved slot zero is skipped") {
    eve::particles::PcgMaterialSelector selector;
    eve::graphics::Texture              reserved, first, second;
    REQUIRE(selector.add(&reserved).ok());
    REQUIRE(selector.add(&first).ok());
    REQUIRE(selector.add(&second).ok());
    auto* emitter  = eve::particles::ParticleEmitter::createEmitter(8);
    auto  selected = selector.selectAndApply(emitter, 20260912u);
    REQUIRE(selected.ok());
    CHECK(selected.value() >= 1);
    CHECK(selected.value() < 3);
    CHECK_EQ(emitter->getTexture(), selected.value() == 1 ? &first : &second);
    auto replay = selector.selectAndApply(emitter, 20260912u);
    REQUIRE(replay.ok());
    CHECK_EQ(replay.value(), selected.value());
    ecs::DestroyEntity(emitter);
}

TEST_CASE("particles.PcgMaterialSelector failure: invalid catalogue leaves existing material intact") {
    eve::particles::PcgMaterialSelector selector;
    eve::graphics::Texture              old, only;
    auto*                               emitter = eve::particles::ParticleEmitter::createEmitter(8);
    emitter->setTexture(&old);
    CHECK(!selector.add(nullptr).ok());
    REQUIRE(selector.add(&only).ok());
    CHECK(!selector.selectAndApply(emitter, 1u).ok());
    CHECK_EQ(emitter->getTexture(), &old);
    ecs::DestroyEntity(emitter);
}
}  // namespace test_particle_material_selector_contract
#endif
