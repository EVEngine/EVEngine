#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_effect_asset)
#include "PrivateTestScope.h"
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionPreview.h"
#include "action/ActionVfxBlock.h"
#include "action/ActionVfxDuration.h"
#include "common/Capability.h"
#include "filesystem/Filesystem.h"
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_effect_asset.cpp
namespace test_particles_effect_asset_unit {
using namespace eve::particles;

namespace {

[[maybe_unused]] eve::LogicalId logicalId(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

class TestVfxAttachmentSource final : public eve::IAttachmentPointSource {
public:
    eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(std::string_view     name,
                                                            eve::AttachmentPoint localOffset) const override {
        ++calls;
        if (name != "hand_r")
            return eve::Result<eve::AttachmentPoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing test bone", "bone"));
        return eve::Result<eve::AttachmentPoint>::success(
            {50.f + localOffset.x, 60.f + localOffset.y, 70.f + localOffset.z});
    }
    mutable int calls = 0;
};

[[maybe_unused]] int liveParticleCount() {
    int  count = 0;
    auto view  = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [config] = *it;
        if (config->entity) count += config->entity->getCount();
    }
    return count;
}

[[maybe_unused]] float firstPlaybackSpeed() {
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [config] = *it;
        if (config->entity) return config->entity->getPlaybackSpeed();
    }
    return 0.0f;
}

}  // namespace

TEST_CASE("particles.effectAsset.versionedMultiEmitterContract") {
    const char *json = R"({
        "type": "eve.particle-effect",
        "version": 1,
        "parameters": { "intensity": 2.0 },
        "emitters": [
            {
                "name": "core",
                "offset": [10.0, 0.0],
                "rotation": 0.1,
                "parameters": { "intensity": 3.0 },
                "emitter": {
                    "buffer": 32,
                    "direction": 0.25,
                    "layer": 3,
                    "emissionRate": 0.0,
                    "parameterBindings": [
                        { "parameter": "intensity", "target": "spawnRate", "scale": 0.5 }
                    ]
                }
            },
            {
                "name": "haze",
                "enabled": false,
                "offset": [-4.0, 6.0],
                "emitter": { "buffer": 8, "emissionRate": 0.0, "layer": -2 }
            }
        ]
    })";

    auto     *particles = Particles::create();
    const int before    = particles->getEmitterCount();
    auto     *effect    = particles->newEffectFromText(json);
    REQUIRE(effect != nullptr);
    CHECK(particles->getLastEffectError().empty());
    CHECK_EQ(effect->getVersion(), 1);
    CHECK_EQ(effect->getEmitterCount(), 2);
    CHECK_EQ(effect->getEmitterName(0), std::string("core"));
    CHECK_EQ(effect->getEmitterName(1), std::string("haze"));
    CHECK(effect->getEmitter(2) == nullptr);

    auto *core = effect->getEmitterByName("core");
    auto *haze = effect->getEmitterByName("haze");
    REQUIRE(core != nullptr);
    REQUIRE(haze != nullptr);
    CHECK_EQ(core->getBufferSize(), 32);
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    effect->setPosition(100.f, 200.f);
    effect->setScale(2.f);
    effect->setRotation(1.57079632679f);
    CHECK(std::abs(core->getX() - 100.f) < 0.001f);
    CHECK(std::abs(core->getY() - 220.f) < 0.001f);
    CHECK(std::abs(core->getDirection() - 1.9207963f) < 0.001f);
    CHECK(std::abs(haze->getX() - 88.f) < 0.001f);
    CHECK(std::abs(haze->getY() - 192.f) < 0.001f);

    effect->setLayer(10);
    CHECK_EQ(core->getLayer(), 13);
    CHECK_EQ(haze->getLayer(), 8);
    effect->setVisible(false);
    CHECK(!core->isVisible());
    CHECK(!haze->isVisible());
    effect->setVisible(true);
    CHECK(core->isVisible());
    CHECK(!haze->isVisible());

    CHECK(effect->hasFloatParameter("intensity"));
    CHECK(std::abs(effect->getFloatParameter("intensity") - 2.f) < 0.001f);
    CHECK(std::abs(core->getFloatParameter("intensity") - 3.f) < 0.001f);
    effect->setFloatParameter("intensity", 4.f);
    CHECK(std::abs(core->getFloatParameter("intensity") - 4.f) < 0.001f);
    CHECK(std::abs(haze->getFloatParameter("intensity") - 4.f) < 0.001f);
    CHECK(std::abs(core->getResolvedParameterScale("spawnRate") - 2.f) < 0.001f);

    effect->start();
    CHECK(core->isActive());
    CHECK(!haze->isActive());
    CHECK(effect->emit("core", 3));
    CHECK(!effect->emit("missing", 3));
    effect->pause();
    CHECK(core->isPaused());
    effect->stop();
    CHECK(core->isStopped());

    delete effect;
    CHECK_EQ(particles->getEmitterCount(), before);
}

TEST_CASE("particles.effectAsset.rejectsUnknownVersionAndDuplicateNames") {
    auto *particles   = Particles::create();
    auto *unsupported = particles->newEffectFromText(
        R"({"type":"eve.particle-effect","version":3,"emitters":[{"name":"a","emitter":{}}]})");
    CHECK(unsupported == nullptr);
    CHECK(particles->getLastEffectError().find("unsupported") != std::string::npos);

    auto *duplicate = particles->newEffectFromText(R"({
        "type":"eve.particle-effect",
        "version":1,
        "emitters":[{"name":"a","emitter":{}},{"name":"a","emitter":{}}]
    })");
    CHECK(duplicate == nullptr);
    CHECK(particles->getLastEffectError().find("duplicate") != std::string::npos);
}

TEST_CASE("particles.effectAsset.v2EventRoutesTimelineAndTransactionalReload") {
    const char *json = R"({
        "type": "eve.particle-effect",
        "version": 2,
        "parameters": { "intensity": 1.0 },
        "eventRoutes": [
            { "from": "core", "on": "death", "to": "haze", "inheritVelocity": 0.25 }
        ],
        "timeline": {
            "duration": 0.5,
            "looping": true,
            "cues": [
                { "time": 0.0, "action": "emit", "emitter": "core", "count": 4 },
                { "time": 0.2, "action": "setParameter", "parameter": "intensity", "value": 2.0 }
            ]
        },
        "emitters": [
            {
                "name": "core",
                "emitter": {
                    "buffer": 32,
                    "emitterLife": 0.4,
                    "emissionRate": 0.0,
                    "particleLifetime": [0.05, 0.05],
                    "parameterBindings": [
                        { "parameter": "intensity", "target": "size", "scale": 1.0 }
                    ]
                }
            },
            {
                "name": "haze",
                "emitter": {
                    "buffer": 32,
                    "emitterLife": 0.4,
                    "emissionRate": 0.0,
                    "particleLifetime": [0.2, 0.2]
                }
            }
        ]
    })";

    auto parsed = ParticleEffect::tryFromText(json);
    REQUIRE(parsed.ok());
    std::unique_ptr<ParticleEffect> effect(parsed.value());
    CHECK_EQ(effect->getVersion(), 2);
    CHECK_EQ(effect->eventRoutes().size(), 1u);
    CHECK_EQ(effect->eventRoutes()[0].from, std::string("core"));
    CHECK_EQ(effect->timeline().cues.size(), 2u);
    CHECK(effect->timeline().looping);

    auto *particles = Particles::create();
    REQUIRE(particles != nullptr);
    effect->start();
    CHECK(effect->isTimelinePlaying());
    CHECK_EQ(effect->getEmitterByName("core")->getCount(), 4);

    particles->update(0.21f);
    CHECK(std::abs(effect->getFloatParameter("intensity") - 2.f) < 0.001f);
    CHECK(effect->getTimelineSeconds() > 0.19f);

    const char *reloaded = R"({
        "type":"eve.particle-effect","version":2,
        "timeline":{"duration":1.0,"looping":false,"cues":[
            {"time":0.0,"action":"emit","emitter":"only","count":2}
        ]},
        "emitters":[{"name":"only","emitter":{"buffer":16,"emissionRate":0.0,"particleLifetime":[0.2,0.2]}}]
    })";
    effect->setPosition(12.f, 34.f);
    auto reload = effect->reloadFromText(reloaded);
    REQUIRE(reload.ok());
    CHECK_EQ(effect->getEmitterCount(), 1);
    CHECK(effect->getEmitterByName("only") != nullptr);
    CHECK(effect->getEmitterByName("core") == nullptr);
    CHECK(std::abs(effect->getX() - 12.f) < 0.001f);
    CHECK(std::abs(effect->getY() - 34.f) < 0.001f);

    auto bad = effect->reloadFromText(R"({"type":"eve.particle-effect","version":2,"emitters":[]})");
    CHECK(!bad.ok());
    bad.ignore();
    CHECK_EQ(effect->getEmitterCount(), 1);
    CHECK(std::abs(effect->getX() - 12.f) < 0.001f);
}

TEST_CASE("particles.effectAsset.v1StillLoadsAndIgnoresUnknownOptionalFields") {
    auto parsed = ParticleEffect::tryFromText(R"({
        "type":"eve.particle-effect",
        "version":1,
        "artistNote":"ignored unknown field",
        "emitters":[{"name":"a","emitter":{"buffer":8,"emissionRate":0.0}}]
    })");
    REQUIRE(parsed.ok());
    std::unique_ptr<ParticleEffect> effect(parsed.value());
    CHECK_EQ(effect->getVersion(), 1);
    CHECK_EQ(effect->getEmitterCount(), 1);
}


TEST_CASE("particles.effectAsset.naturalDurationUsesFiniteEmissionAndRejectsUnboundedAssets") {
    std::string error;
    auto       *finite = ParticleEffect::fromText(R"({
        "type":"eve.particle-effect", "version":1,
        "emitters":[
            {"name":"short", "emitter":{"emitterLife":0.3,"particleLifetime":[0.2,1.2]}},
            {"name":"long", "emitter":{"emitterLife":0.8,"particleLifetime":[0.2,0.9]}}
        ]
    })",
                                                  "", &error);
    REQUIRE(finite != nullptr);
    auto finiteDuration = finite->naturalDuration();
    REQUIRE(finiteDuration.ok());
    CHECK(std::abs(finiteDuration.value().seconds() - 1.7) < 0.001);
    delete finite;

    auto *unbounded = ParticleEffect::fromText(R"({
        "type":"eve.particle-effect", "version":1,
        "emitters":[{"name":"loop", "emitter":{"emitterLife":-1,"looping":true}}]
    })",
                                               "", &error);
    REQUIRE(unbounded != nullptr);
    auto unboundedDuration = unbounded->naturalDuration();
    CHECK(!unboundedDuration.ok());
    REQUIRE(unboundedDuration.error() != nullptr);
    CHECK_EQ(unboundedDuration.error()->code(), eve::DiagnosticCode::Unsupported);
    delete unbounded;
}


TEST_CASE("particles.actionVfxStopEmittingPreservesThenReapsResiduals") {
    auto *filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto *particles = Particles::create();
    auto  registry  = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    const int                       before = particles->getEmitterCount();

    eve::action::ActionAdvance enter;
    enter.id           = eve::action::ActionExecutionId{95};
    enter.totalElapsed = eve::Duration::fromNanoseconds(100'000'000);
    eve::Value::Object payload{{"uri", "examples/particle-playback-lab/impact.effect.json"},
                               {"stopBehavior", "stop_emitting"}};
    enter.timelineEvents.push_back({eve::action::ActionTimelineEventKind::StateEnter,
                                    logicalId("presentation-track:vfx"), logicalId("presentation-vfx:residual"),
                                    logicalId("presentation:vfx-state"), enter.totalElapsed, payload});
    enter.activeBlocks.push_back({logicalId("presentation-track:vfx"), logicalId("presentation-vfx:residual"),
                                  logicalId("presentation:vfx-state"), enter.totalElapsed,
                                  eve::Duration::fromNanoseconds(2'000'000'000), payload});
    eve::action::ActionNotifyContext context;
    context.executionId = enter.id;
    REQUIRE(runtime.apply(enter, context).ok());
    particles->update(0.1f);
    CHECK(liveParticleCount() > 0);

    context.time = eve::Duration::fromNanoseconds(200'000'000);
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    for (int i = 0; i < 100; ++i) particles->update(0.1f);
    CHECK_EQ(liveParticleCount(), 0);

    eve::action::ActionAdvance reap;
    reap.id           = enter.id;
    reap.totalElapsed = eve::Duration::fromNanoseconds(4'000'000'000);
    REQUIRE(runtime.apply(reap, context).ok());
    CHECK_EQ(particles->getEmitterCount(), before);
}

TEST_CASE("particles.instantActionNotifyRetainsThenDeterministicallyReleasesEffect") {
    auto *filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto *particles = Particles::create();
    auto  registry  = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    const int                       before = particles->getEmitterCount();
    eve::action::ActionAdvance      trigger;
    trigger.id = eve::action::ActionExecutionId{94};
    trigger.timelineEvents.push_back(
        {eve::action::ActionTimelineEventKind::Notify,
         logicalId("presentation-track:vfx"),
         logicalId("presentation-vfx:hit"),
         logicalId("presentation:vfx"),
         eve::Duration::zero(),
         {{"uri", "examples/particle-playback-lab/impact.effect.json"}, {"lifetimeSeconds", 1.5}}});
    eve::action::ActionNotifyContext context;
    context.executionId = trigger.id;
    REQUIRE(runtime.apply(trigger, context).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    eve::action::ActionAdvance expired;
    expired.id           = trigger.id;
    expired.totalElapsed = eve::Duration::fromNanoseconds(2'000'000'000LL);
    REQUIRE(runtime.apply(expired, context).ok());
    CHECK_EQ(particles->getEmitterCount(), before);
}
}  // namespace test_particles_effect_asset_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_gpu_resident)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/RenderSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_gpu_resident.cpp
namespace test_particles_gpu_resident_unit {
using namespace eve::particles;

namespace {

[[maybe_unused]] double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

[[maybe_unused]] eve::graphics::Mesh *makeLeftHalfDepthPlane(eve::graphics::Graphics *gfx) {
    const std::array<float, 12> positions = {
        -1.f, -1.f, 0.5f, 0.f, -1.f, 0.5f, 0.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const std::array<float, 12> normals = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const std::array<float, 8>         uvs     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 0, 3, 2};
    return gfx->newMeshFromArrays(positions.data(), normals.data(), uvs.data(), 4, indices.data(), 6);
}

[[maybe_unused]] eve::graphics::Mesh *makeFullscreenPlane(eve::graphics::Graphics *gfx) {
    const std::array<float, 12> positions = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const std::array<float, 12> normals = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const std::array<float, 8>         uvs     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 0, 3, 2};
    return gfx->newMeshFromArrays(positions.data(), normals.data(), uvs.data(), 4, indices.data(), 6);
}

[[maybe_unused]] float colorDistance(const Color &a, const Color &b) {
    const float dr = a.r - b.r;
    const float dg = a.g - b.g;
    const float db = a.b - b.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
}

}  // namespace

TEST_CASE("particles.gpu.unsupportedFeaturesFallBackToCpu") {
    auto *emitter = Particles::create()->newEmitter(256);
    emitter->setRandomSeed(8128);
    emitter->setGpuSimulation(true);
    emitter->setCollision("bounce", 2.f, 0.5f, 0.f);
    emitter->setEmissionRate(120.f);
    emitter->setParticleLifetime(2.f, 2.f);
    emitter->start();

    ParticleSimSystem::update(0.25f);

    CHECK(emitter->getGpuSimulation());
    CHECK(!emitter->gpuSim()->residentActive);
    CHECK_EQ(emitter->gpuSim()->residentHandle, std::uint64_t(0));
    REQUIRE_GT(emitter->getCount(), 0);
    REQUIRE_EQ(emitter->getCount(), emitter->sim()->alive);
}
}  // namespace test_particles_gpu_resident_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_p2_p3)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "common/ParticleSdf.h"
#include "graphics/Graphics.h"
#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleEmitterPool.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_p2_p3.cpp
namespace test_particles_p2_p3_unit {
using namespace eve::particles;

namespace {

class CircleSdf final : public eve::IParticleSdfField {
public:
    CircleSdf(float x, float y, float radius) : x_(x), y_(y), radius_(radius) {}

    float sample(float x, float y) const override {
        const float dx = x - x_;
        const float dy = y - y_;
        return std::sqrt(dx * dx + dy * dy) - radius_;
    }

    eve::ParticleSdfGradientStatus gradient(float x, float y, float &outNx, float &outNy) const override {
        const float dx  = x - x_;
        const float dy  = y - y_;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-5f) {
            outNx = 0.f;
            outNy = 1.f;
            return eve::ParticleSdfGradientStatus::Defined;
        }
        outNx = dx / len;
        outNy = dy / len;
        return eve::ParticleSdfGradientStatus::Defined;
    }

private:
    float x_      = 0.f;
    float y_      = 0.f;
    float radius_ = 1.f;
};

[[maybe_unused]] eve::graphics::Texture *makeSoftDisc(eve::graphics::Graphics *gfx, int size) {
    std::vector<std::uint8_t> pixels(std::size_t(size * size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float nx = (float(x) + 0.5f) / float(size) * 2.f - 1.f;
            const float ny = (float(y) + 0.5f) / float(size) * 2.f - 1.f;
            const float r  = std::sqrt(nx * nx + ny * ny);
            const float a  = std::clamp(1.f - r, 0.f, 1.f);
            auto       *px = &pixels[std::size_t(y * size + x) * 4u];
            const auto  v  = std::uint8_t(a * 255.f);
            px[0]          = v;
            px[1]          = v;
            px[2]          = v;
            px[3]          = v;
        }
    }
    return gfx->newTexture(size, size, pixels.data());
}

[[maybe_unused]] std::string readFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

[[maybe_unused]] float luma(const Color &color) { return color.r * 0.2126f + color.g * 0.7152f + color.b * 0.0722f; }

}  // namespace

TEST_CASE("particles.pool.acquireRecyclePreservesCapacity") {
    ParticleEmitterPool pool;
    auto                first = pool.acquire(128);
    REQUIRE(first.ok());
    ParticleEmitter *a = first.value();
    REQUIRE(a != nullptr);
    CHECK_EQ(a->getBufferSize(), 128);
    a->emit(3);
    CHECK_EQ(a->getCount(), 3);
    REQUIRE(pool.recycle(a).ok());
    CHECK_EQ(pool.idleCount(), 1u);

    auto second = pool.acquire(64);
    REQUIRE(second.ok());
    CHECK_EQ(second.value(), a);
    CHECK_EQ(a->getCount(), 0);
    CHECK_EQ(pool.idleCount(), 0u);
    REQUIRE(pool.recycle(a).ok());
    pool.clear();
    CHECK_EQ(pool.idleCount(), 0u);
}

TEST_CASE("particles.sdf.circleFieldKillsInboundParticles") {
    auto *particles = Particles::create();
    REQUIRE(particles != nullptr);
    auto *emitter = particles->newEmitter(32);
    REQUIRE(emitter != nullptr);
    emitter->setPosition(0.f, 0.f);
    emitter->setEmissionRate(0.f);
    emitter->setParticleLifetime(2.f, 2.f);
    emitter->setSpeed(0.f, 0.f);
    emitter->setCollision("kill", 2.f);
    CircleSdf field(0.f, 0.f, 8.f);
    emitter->setSdfField(&field);
    CHECK(emitter->getSdfField() == &field);
    emitter->setGpuSimulation(true);
    CHECK(!emitter->isGpuFeatureSetSupported());
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("sdf"));
    emitter->emit(1);
    CHECK_EQ(emitter->getCount(), 1);
    particles->update(1.f / 60.f);
    CHECK_EQ(emitter->getCount(), 0);
    emitter->setSdfField(nullptr);
    emitter->release();
}

TEST_CASE("particles.motionVector.policyRecordedButInactive") {
    auto *emitter = Particles::create()->newEmitter(8);
    REQUIRE(emitter != nullptr);
    emitter->setMotionVectorPolicy("velocity");
    CHECK_EQ(emitter->getMotionVectorPolicy(), std::string("velocity"));
    CHECK(!emitter->isMotionVectorActive());
    emitter->setMotionVectorPolicy("spawn_delta");
    CHECK_EQ(emitter->getMotionVectorPolicy(), std::string("spawn_delta"));
    emitter->setMotionVectorPolicy("bogus");
    CHECK_EQ(emitter->getMotionVectorPolicy(), std::string("none"));
    emitter->release();
}
}  // namespace test_particles_p2_p3_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_playback)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include "graphics/Graphics.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_playback.cpp
namespace test_particles_playback_unit {
using namespace eve::particles;

namespace {

[[maybe_unused]] void configureDeterministicEmitter(ParticleEmitter *emitter, int seed) {
    emitter->setRandomSeed(seed);
    emitter->setPosition(12.f, 34.f);
    emitter->setParticleLifetime(0.5f, 2.f);
    emitter->setDirection(0.7f);
    emitter->setSpread(1.2f);
    emitter->setSpeed(10.f, 80.f);
    emitter->setSizeVariation(0.5f);
    emitter->setStartRotation(-90.f, 90.f);
}

}  // namespace

TEST_CASE("particles.playback.seedIsReproducible") {
    auto *particles = Particles::create();
    auto *a         = particles->newEmitter(16);
    auto *b         = particles->newEmitter(16);
    configureDeterministicEmitter(a, 1337);
    configureDeterministicEmitter(b, 1337);

    a->emit(8);
    b->emit(8);
    REQUIRE(a->getCount() == b->getCount());
    for (int i = 0; i < a->getCount(); ++i) {
        const Particle &pa = a->sim()->particles[size_t(i)];
        const Particle &pb = b->sim()->particles[size_t(i)];
        CHECK(std::abs(pa.life - pb.life) < 1e-6f);
        CHECK(std::abs(pa.vx - pb.vx) < 1e-6f);
        CHECK(std::abs(pa.vy - pb.vy) < 1e-6f);
        CHECK(std::abs(pa.size - pb.size) < 1e-6f);
        CHECK(std::abs(pa.rot - pb.rot) < 1e-6f);
    }
}

TEST_CASE("particles.playback.startRewindsFixedSeed") {
    auto *emitter = Particles::create()->newEmitter(8);
    configureDeterministicEmitter(emitter, 42);
    emitter->start();
    emitter->emit(1);
    const Particle first = emitter->sim()->particles[0];

    emitter->reset();
    emitter->start();
    emitter->emit(1);
    const Particle replay = emitter->sim()->particles[0];
    CHECK(std::abs(first.life - replay.life) < 1e-6f);
    CHECK(std::abs(first.vx - replay.vx) < 1e-6f);
    CHECK(std::abs(first.vy - replay.vy) < 1e-6f);
}

TEST_CASE("particles.playback.fixedStepAndSpeed") {
    auto *emitter = Particles::create()->newEmitter(4);
    emitter->setRandomSeed(7);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setDirection(0.f);
    emitter->setSpread(0.f);
    emitter->setSpeed(10.f, 10.f);
    emitter->setFixedTimeStep(0.1f, 4);
    emitter->setPlaybackSpeed(2.f);
    emitter->emit(1);

    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.025f);
    CHECK(std::abs(emitter->sim()->particles[0].x) < 1e-6f);
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.025f);
    CHECK(std::abs(emitter->sim()->particles[0].x - 1.f) < 1e-5f);
}

TEST_CASE("particles.playback.loopResetsTimelineAndBursts") {
    auto *emitter = Particles::create()->newEmitter(16);
    emitter->setRandomSeed(9);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setEmitterLifetime(0.1f);
    emitter->setLooping(true);
    emitter->addBurst(0.f, 2);
    emitter->start();

    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.11f);
    CHECK(emitter->isActive());
    CHECK_EQ(emitter->getCount(), 2);
    CHECK(emitter->sim()->emitterAge < 0.1f);
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);
    CHECK_EQ(emitter->getCount(), 4);
}

TEST_CASE("particles.playback.distanceEmissionIsEvenlySpaced") {
    auto *emitter = Particles::create()->newEmitter(16);
    emitter->setRandomSeed(10);
    emitter->setParticleLifetime(10.f, 10.f);
    emitter->setSpeed(0.f, 0.f);
    emitter->setEmissionRateOverDistance(0.5f);
    emitter->setPosition(0.f, 0.f);
    emitter->start();
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);

    emitter->setPosition(10.f, 0.f);
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.01f);
    REQUIRE(emitter->getCount() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(std::abs(emitter->sim()->particles[size_t(i)].x - float((i + 1) * 2)) < 1e-5f);
        CHECK(std::abs(emitter->sim()->particles[size_t(i)].y) < 1e-6f);
    }
}

TEST_CASE("particles.playback.configAppliesProfessionalControls") {
    auto       *emitter = Particles::create()->newEmitter(8);
    const char *json    = R"({
        "emissionRateOverDistance": 3.5,
        "emitterLife": 2.0,
        "looping": true,
        "playbackSpeed": 0.5,
        "fixedTimeStep": 0.0166667,
        "maxSubSteps": 6,
        "randomSeed": 1234,
        "autoRandomSeed": false
    })";
    REQUIRE(emitter->applyConfig(json));
    CHECK(std::abs(emitter->getEmissionRateOverDistance() - 3.5f) < 1e-6f);
    CHECK(emitter->getLooping());
    CHECK(std::abs(emitter->getPlaybackSpeed() - 0.5f) < 1e-6f);
    CHECK(std::abs(emitter->getFixedTimeStep() - 0.0166667f) < 1e-6f);
    CHECK_EQ(emitter->config()->maxSubSteps, 6);
    CHECK_EQ(emitter->getRandomSeed(), 1234);
    CHECK(!emitter->getAutoRandomSeed());
}

TEST_CASE("particles.playback.exposedParametersDriveRuntimeScales") {
    auto *emitter = Particles::create()->newEmitter(32);
    emitter->setRandomSeed(91);
    emitter->setEmissionRate(10.f);
    emitter->setParticleLifetime(5.f, 5.f);
    emitter->setSpeed(20.f, 20.f);
    emitter->setSizeVariation(0.f);
    emitter->setFloatParameter("intensity", 2.f);
    emitter->bindFloatParameter("intensity", "emission");
    emitter->bindFloatParameter("intensity", "speed", 0.5f, 0.f);
    emitter->bindFloatParameter("intensity", "size", 0.75f, 0.f);
    emitter->start();

    advanceEmitterSim(*emitter->config(), *emitter->sim(), 0.1f);
    REQUIRE_EQ(emitter->getCount(), 2);
    CHECK(std::abs(emitter->sim()->particles[0].size - 1.5f) < 1e-5f);
    const auto &particle = emitter->sim()->particles[0];
    CHECK(std::abs(std::sqrt(particle.vx * particle.vx + particle.vy * particle.vy) - 20.f) < 1e-4f);

    emitter->bindFloatParameter("freeze", "playback");
    emitter->setFloatParameter("freeze", 0.f);
    const float life = emitter->sim()->particles[0].life;
    advanceEmitterSim(*emitter->config(), *emitter->sim(), 1.f);
    CHECK(std::abs(emitter->sim()->particles[0].life - life) < 1e-5f);
}
}  // namespace test_particles_playback_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_renderer)
#include "PrivateTestScope.h"
#include <cmath>
#include <string>
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_renderer.cpp
namespace test_particles_renderer {
using namespace eve::particles;

TEST_CASE("particles.renderer.facingAndSortContract") {
    auto *emitter = Particles::create()->newEmitter(32);

    emitter->setRenderMode("axis");
    emitter->setRenderAxis(37.5f);
    emitter->setSortMode("distance");
    CHECK_EQ(emitter->config()->renderMode, std::string("axis"));
    CHECK(std::abs(emitter->config()->renderAxisDegrees - 37.5f) < 1e-5f);
    CHECK_EQ(emitter->getSortMode(), std::string("distance"));

    emitter->setRenderMode("velocity", 2.25f);
    CHECK_EQ(emitter->config()->renderMode, std::string("stretched"));
    CHECK(std::abs(emitter->config()->stretchFactor - 2.25f) < 1e-5f);

    emitter->setSortMode("unsupported");
    CHECK_EQ(emitter->getSortMode(), std::string("none"));

    emitter->setRibbon(0.65f, 3.f);
    CHECK_EQ(emitter->config()->renderMode, std::string("ribbon"));
    CHECK(std::abs(emitter->config()->ribbonWidth - 0.65f) < 1e-5f);
    CHECK(std::abs(emitter->config()->ribbonMinSegmentLength - 3.f) < 1e-5f);

    emitter->setSoftParticles(true, 1.2f, 0.f);
    CHECK(emitter->config()->softParticles);
    CHECK(std::abs(emitter->config()->softParticleDepth - 1.f) < 1e-5f);
    CHECK(emitter->config()->softFadeDistance >= 1e-5f);
    CHECK(!emitter->isSoftParticlesActive());

    emitter->setMaterialMode("lit");
    CHECK_EQ(emitter->getMaterialMode(), std::string("lit"));
    emitter->setGpuSimulation(true);
    CHECK(!emitter->isGpuFeatureSetSupported());
    CHECK_EQ(emitter->getSimulationBackend(), std::string("cpu"));
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("lit_material"));
    emitter->setMaterialMode("distortion");
    emitter->setDistortionStrength(14.5f);
    CHECK_EQ(emitter->getMaterialMode(), std::string("distortion"));
    CHECK(std::abs(emitter->getDistortionStrength() - 14.5f) < 1e-5f);
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("distortion_material"));
    emitter->setDistortionStrength(-4.f);
    CHECK_EQ(emitter->getDistortionStrength(), 0.f);
    emitter->setMaterialMode("unsupported");
    CHECK_EQ(emitter->getMaterialMode(), std::string("unlit"));
    CHECK(emitter->isGpuFeatureSetSupported());
    CHECK_EQ(emitter->getGpuFallbackReason(), std::string("backend_unavailable"));
}

TEST_CASE("particles.renderer.jsonContract") {
    auto *emitter = Particles::create()->newEmitter(32);
    REQUIRE(emitter->applyConfig(R"({
        "renderMode": "axis",
        "renderAxis": -22.5,
        "sortMode": "youngest",
        "ribbon": {"width": 0.75, "minSegmentLength": 2.5},
        "softParticles": {"enabled": true, "depth": 0.42, "fadeDistance": 0.08},
        "material": {"mode": "lit", "normalTexture": "particles/test-normal.png", "distortionStrength": 12.5},
        "parameters": {"intensity": 1.75},
        "parameterBindings": [
          {"parameter": "intensity", "target": "emission", "scale": 2.0, "offset": 0.5}
        ]
    })"));

    CHECK_EQ(emitter->config()->renderMode, std::string("ribbon"));
    CHECK(std::abs(emitter->config()->renderAxisDegrees + 22.5f) < 1e-5f);
    CHECK_EQ(emitter->getSortMode(), std::string("youngest"));
    CHECK(std::abs(emitter->config()->ribbonWidth - 0.75f) < 1e-5f);
    CHECK(emitter->config()->softParticles);
    CHECK(std::abs(emitter->config()->softParticleDepth - 0.42f) < 1e-5f);
    CHECK_EQ(emitter->getMaterialMode(), std::string("lit"));
    CHECK(std::abs(emitter->getDistortionStrength() - 12.5f) < 1e-5f);
    CHECK_EQ(emitter->resource()->normalTexturePath, std::string("particles/test-normal.png"));
    CHECK(std::abs(emitter->getFloatParameter("intensity") - 1.75f) < 1e-5f);
    CHECK(std::abs(emitter->getResolvedParameterScale("emission") - 4.f) < 1e-5f);
}
}  // namespace test_particles_renderer
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles_scalability)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleRuntime.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles_scalability.cpp
namespace test_particles_scalability_unit {
using namespace eve::particles;

namespace {

[[maybe_unused]] void resetScalability() {
    auto &budget                = particleBudgetConfig();
    budget.maxParticles         = 0;
    budget.maxSimulatedEmitters = 0;
    budget.qualityLevel         = 3;
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    std::vector<ParticleEmitter *> existing;
    auto                           view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [cfg] = *it;
        if (cfg->entity) existing.push_back(cfg->entity);
    }
    for (auto *emitter : existing) {
        emitter->stop();
        emitter->reset();
    }
}

[[maybe_unused]] ParticleEmitter *makeBudgetEmitter(int priority, float rate = 100.f) {
    auto *emitter = Particles::create()->newEmitter(256);
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
    auto *low                                   = makeBudgetEmitter(1);
    auto *high                                  = makeBudgetEmitter(100);
    auto *medium                                = makeBudgetEmitter(50);
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
    auto *high                          = makeBudgetEmitter(100);
    auto *low                           = makeBudgetEmitter(1);
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
    auto *emitter = makeBudgetEmitter(1);
    emitter->setMaxSpawnPerFrame(7);

    ParticleSimSystem::update(1.f);
    REQUIRE_EQ(emitter->getCount(), 7);
    REQUIRE_EQ(particleFrameStats().particlesSpawned, 7);
    REQUIRE_GT(particleFrameStats().droppedSpawns, 0);
}

TEST_CASE("particles.scalability.qualityAndDistanceCulling") {
    resetScalability();
    auto *emitter = makeBudgetEmitter(1);
    emitter->setMinimumQuality(3);
    particleBudgetConfig().qualityLevel = 2;
    ParticleSimSystem::update(0.1f);
    REQUIRE_EQ(emitter->getCount(), 0);
    REQUIRE_EQ(particleFrameStats().emittersQualitySkipped, 1);

    particleBudgetConfig().qualityLevel = 3;
    auto *camera                        = eve::graphics::Camera2D::createCamera();
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
    auto *emitter = Particles::create()->newEmitter(16);
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
}  // namespace test_particles_scalability_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_particles)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <string>
#include "filesystem/Filesystem.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "particles/FloatingPointFixParticles.h"
#include "particles/GroundParticleCulling.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"
#include "particles/Particles.h"
#include "particles/UnderwaterParticles.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// particles.cpp
namespace test_particles_unit {
using namespace eve::particles;

namespace {

[[maybe_unused]] void hideAllEmitters() {
    if (ecs::current()->getManager<ParticleEmitter>() == nullptr) return;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

}  // namespace

TEST_CASE("particles.newEmitter.ecsView") {
    auto            *mod    = Particles::create();
    int              before = mod->getEmitterCount();
    ParticleEmitter *e      = mod->newEmitter(64);
    REQUIRE(e != nullptr);
    CHECK_EQ(e->getBufferSize(), 64);
    CHECK_EQ(e->getCount(), 0);
    CHECK_EQ(mod->getEmitterCount(), before + 1);
    CHECK(e->config()->entity == e);
}

TEST_CASE("particles.emitter.emitAndLifetime") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->setParticleLifetime(1.f, 1.f);
    e->setSpeed(0.f, 0.f);
    e->emit(10);
    CHECK_EQ(e->getCount(), 10);

    ParticleSimSystem::update(0.5f);
    CHECK_EQ(e->getCount(), 10);

    ParticleSimSystem::update(0.6f);
    CHECK_EQ(e->getCount(), 0);
}

TEST_CASE("particles.emitter.emissionRate") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(100);
    e->setParticleLifetime(10.f, 10.f);
    e->setEmissionRate(50.f);
    e->setSpeed(0.f, 0.f);
    e->start();
    ParticleSimSystem::update(0.2f);  // ~10 particles
    CHECK_GE(e->getCount(), 9);
    CHECK_LE(e->getCount(), 11);
}

TEST_CASE("particles.emitter.startStopPause") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setEmissionRate(100.f);
    e->setParticleLifetime(5.f, 5.f);
    CHECK(e->isStopped());

    e->start();
    CHECK(e->isActive());
    e->pause();
    CHECK(e->isPaused());
    CHECK(!e->isActive());
    int c0 = e->getCount();
    ParticleSimSystem::update(1.f);
    CHECK_EQ(e->getCount(), c0);

    e->start();
    e->stop();
    CHECK(e->isStopped());
}

TEST_CASE("particles.underwater.ambienceAndTransitionLifecycle") {
    auto *mod        = Particles::create();
    auto *ambience   = mod->newEmitter(64);
    auto *transition = mod->newEmitter(16);
    REQUIRE(static_cast<bool>(applyUnderwaterParticles(ambience, transition, true, true, true, false)));
    CHECK(ambience->isActive());
    CHECK(ambience->isVisible());
    CHECK(transition->isActive());
    CHECK(transition->isVisible());

    CHECK(!applyUnderwaterParticles(ambience, transition, false, true, true, true));
    CHECK(ambience->isActive());
    REQUIRE(static_cast<bool>(applyUnderwaterParticles(ambience, transition, false, true, false, true)));
    CHECK(ambience->isStopped());
    CHECK(!ambience->isVisible());

    auto *surfaceVfx = mod->newEmitter(32);
    REQUIRE(static_cast<bool>(applyUnderwaterSurfaceVfx(surfaceVfx, true)));
    CHECK(surfaceVfx->isActive());
    CHECK(surfaceVfx->isVisible());
    REQUIRE(static_cast<bool>(applyUnderwaterSurfaceVfx(surfaceVfx, false)));
    CHECK(surfaceVfx->isStopped());
    CHECK(!surfaceVfx->isVisible());
    CHECK(!applyUnderwaterSurfaceVfx(nullptr, true));
}


TEST_CASE("particles.floatingOrigin.shiftsWorldParticlesWithoutChangingPlayback") {
    auto *mod     = Particles::create();
    auto *emitter = mod->newEmitter(8);
    emitter->setAutoRandomSeed(false);
    emitter->setRandomSeed(7);
    emitter->setPosition(1200.0F, 45.0F);
    emitter->emit(2);
    emitter->pause();
    const auto before0 = emitter->sim()->particles[0];
    const auto before1 = emitter->sim()->particles[1];
    REQUIRE(static_cast<bool>(shiftWorldSpaceParticles(emitter, 1000.0F, 0.0F)));
    CHECK(emitter->isPaused());
    CHECK_EQ(emitter->getX(), 1200.0F);
    CHECK_EQ(emitter->sim()->particles[0].x, before0.x - 1000.0F);
    CHECK_EQ(emitter->sim()->particles[0].y, before0.y);
    CHECK_EQ(emitter->sim()->particles[1].x, before1.x - 1000.0F);
    CHECK_EQ(emitter->sim()->particles[1].life, before1.life);

    emitter->setSimulationSpace("local");
    const float unchanged = emitter->sim()->particles[0].x;
    CHECK(!shiftWorldSpaceParticles(emitter, 1.0F, 0.0F));
    CHECK_EQ(emitter->sim()->particles[0].x, unchanged);
    emitter->setSimulationSpace("world");
    CHECK(!shiftWorldSpaceParticles(emitter, NAN, 0.0F));
    CHECK_EQ(emitter->sim()->particles[0].x, unchanged);
    CHECK(!shiftWorldSpaceParticles(nullptr, 1.0F, 0.0F));

    auto gpu            = emitter->gpuSim();
    gpu->residentActive = true;
    gpu->estimatedAlive = 3;
    auto queued         = shiftWorldSpaceParticles(emitter, 20.0F, -4.0F);
    REQUIRE(static_cast<bool>(queued));
    CHECK_EQ(queued.value(), 5);
    CHECK_EQ(gpu->pendingWorldOffsetX, -20.0F);
    CHECK_EQ(gpu->pendingWorldOffsetY, 4.0F);
    REQUIRE(static_cast<bool>(shiftWorldSpaceParticles(emitter, 5.0F, 1.0F)));
    CHECK_EQ(gpu->pendingWorldOffsetX, -25.0F);
    CHECK_EQ(gpu->pendingWorldOffsetY, 3.0F);
    gpu->residentActive = false;
    gpu->estimatedAlive = 0;
}

TEST_CASE("particles.emitter.motion") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setPosition(0.f, 0.f);
    e->setParticleLifetime(2.f, 2.f);
    e->setDirection(0.f);
    e->setSpread(0.f);
    e->setSpeed(100.f, 100.f);
    e->setLinearAcceleration(0.f, 0.f, 0.f, 0.f);
    e->emit(1);
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(e->getCount(), 1);
}

TEST_CASE("particles.emitter.applyPreset") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->applyPreset("spark");
    CHECK_GT(e->getEmissionRate(), 0.f);
    e->applyPreset("smoke");
    CHECK_GT(e->getEmissionRate(), 0.f);
    e->applyPreset("fire");
    CHECK_GT(e->getEmissionRate(), 0.f);
    e->applyPreset("unknown");
    CHECK_GT(e->getEmissionRate(), 0.f);
}

TEST_CASE("particles.system.updateAll") {
    auto            *mod = Particles::create();
    ParticleEmitter *a   = mod->newEmitter(16);
    ParticleEmitter *b   = mod->newEmitter(16);
    a->setParticleLifetime(1.f, 1.f);
    b->setParticleLifetime(1.f, 1.f);
    a->emit(3);
    b->emit(5);
    ParticleSimSystem::update(2.f);
    CHECK_EQ(a->getCount(), 0);
    CHECK_EQ(b->getCount(), 0);
}

TEST_CASE("particles.module.updateForwardsToSystem") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(1.f, 1.f);
    e->emit(4);
    mod->update(2.f);
    CHECK_EQ(e->getCount(), 0);
}

TEST_CASE("particles.emitter.reset") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->emit(8);
    CHECK_EQ(e->getCount(), 8);
    e->reset();
    CHECK_EQ(e->getCount(), 0);
}

TEST_CASE("particles.emitter.bufferCap") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(4);
    e->setParticleLifetime(10.f, 10.f);
    e->emit(100);
    CHECK_EQ(e->getCount(), 4);
}

TEST_CASE("particles.renderSystem.skipsInvisible") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setParticleLifetime(5.f, 5.f);
    e->emit(3);
    e->setVisible(false);
    // No Graphics — render should early-out safely.
    ParticleRenderSystem::render(nullptr);
    CHECK_EQ(e->getCount(), 3);
}

TEST_CASE("particles.emitter.emissionAreaAndForces") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setPosition(100.f, 100.f);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionArea("ellipse", 40.f, 20.f);
    e->setRadialAcceleration(50.f, 50.f);
    e->setTangentialAcceleration(0.f, 0.f);
    e->setSizeVariation(0.5f);
    e->setSpin(1.f, 2.f);
    CHECK_EQ(e->getEmissionAreaType(), std::string("ellipse"));
    e->emit(20);
    CHECK_EQ(e->getCount(), 20);

    // Spawn offsets should leave the point origin for ellipse area.
    bool anyOffset = false;
    auto sim       = e->sim();
    for (int i = 0; i < sim->alive; ++i) {
        const auto &p = sim->particles[size_t(i)];
        if (std::abs(p.x - 100.f) > 0.5f || std::abs(p.y - 100.f) > 0.5f) anyOffset = true;
        CHECK_GT(p.size, 0.f);
        CHECK_GE(p.spin, 1.f);
        CHECK_LE(p.spin, 2.f);
    }
    CHECK(anyOffset);

    ParticleSimSystem::update(0.2f);
    CHECK_EQ(e->getCount(), 20);
}

TEST_CASE("particles.config.applyAreaAndAccel") {
    auto            *mod  = Particles::create();
    ParticleEmitter *e    = mod->newEmitter(16);
    const char      *json = R"({
      "emissionArea": {"type":"rect","x":8,"y":4},
      "radialAcceleration": [10, 20],
      "tangentialAcceleration": [-5, 5],
      "sizeVariation": 0.2,
      "spin": [1, 3]
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->getEmissionAreaType(), std::string("rect"));
    CHECK(std::abs(e->getEmissionAreaX() - 8.f) < 1e-4f);
    CHECK(std::abs(e->getEmissionAreaY() - 4.f) < 1e-4f);
    CHECK(std::abs(e->getSizeVariation() - 0.2f) < 1e-4f);
}

TEST_CASE("particles.emitter.blendAndFlipbookSetters") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);

    e->setBlendMode("additive");
    CHECK_EQ(e->getBlendMode(), std::string("additive"));
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Additive));
    e->setBlendMode("alpha");
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Alpha));
    e->setBlendMode("opaque");
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Opaque));
    e->setBlendMode("bogus");
    CHECK(int(e->draw()->blend) == int(eve::graphics::BlendMode::Alpha));

    e->setFlipbook(4, 2, 12.f, 0.5f);
    CHECK_EQ(e->config()->hframes, 4);
    CHECK_EQ(e->config()->vframes, 2);
    CHECK(std::abs(e->config()->frameRate - 12.f) < 1e-5f);
    CHECK(std::abs(e->config()->frameRandomStart - 0.5f) < 1e-5f);

    e->setStartRotation(-45.f, 45.f);
    CHECK(std::abs(e->config()->startRotMin + 45.f) < 1e-5f);
    CHECK(std::abs(e->config()->startRotMax - 45.f) < 1e-5f);
}

TEST_CASE("particles.emitter.gradientAndCurves") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);

    e->addColorStop(0.f, 1.f, 0.f, 0.f, 1.f);
    e->addColorStop(0.5f, 0.f, 1.f, 0.f, 1.f);
    e->addColorStop(1.f, 0.f, 0.f, 1.f, 1.f);
    CHECK_EQ(e->config()->colorGradient.size(), size_t(3));
    float r = 0, g = 0, b = 0, a = 0;
    e->config()->colorGradient.sample(0.25f, r, g, b, a);
    CHECK(std::abs(r - 0.5f) < 1e-4f);
    CHECK(std::abs(g - 0.5f) < 1e-4f);
    e->config()->colorGradient.sample(0.75f, r, g, b, a);
    CHECK(std::abs(g - 0.5f) < 1e-4f);
    CHECK(std::abs(b - 0.5f) < 1e-4f);

    e->addSizeCurvePoint(0.f, 0.2f);
    e->addSizeCurvePoint(1.f, 2.f);
    CHECK(std::abs(e->config()->sizeCurve.sample(0.5f, 1.f) - 1.1f) < 1e-4f);
    CHECK(std::abs(e->config()->sizeCurve.sample(0.f, 1.f) - 0.2f) < 1e-5f);
    CHECK(std::abs(e->config()->sizeCurve.sample(1.f, 1.f) - 2.f) < 1e-5f);

    e->addRotationCurvePoint(0.f, 0.f);
    e->addRotationCurvePoint(1.f, 90.f);
    CHECK(std::abs(e->config()->rotationCurve.sample(0.5f, 0.f) - 45.f) < 1e-4f);
}

TEST_CASE("particles.config.applyBlendFlipbookGradient") {
    auto            *mod  = Particles::create();
    ParticleEmitter *e    = mod->newEmitter(16);
    const char      *json = R"({
      "blendMode": "additive",
      "flipbook": {"hframes": 4, "vframes": 2, "frameRate": 15, "frameRandomStart": 0.25},
      "startRotation": [-30, 30],
      "colorOverLifetime": [{"t":0,"r":1,"g":0.5,"b":0,"a":1},{"t":1,"r":1,"g":0,"b":0,"a":0}],
      "sizeOverLifetime": [[0, 0.5], [1, 1.5]],
      "rotationOverLifetime": [{"t":0,"v":0},{"t":1,"v":120}]
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->getBlendMode(), std::string("additive"));
    CHECK_EQ(e->config()->hframes, 4);
    CHECK_EQ(e->config()->vframes, 2);
    CHECK(std::abs(e->config()->frameRate - 15.f) < 1e-5f);
    CHECK(std::abs(e->config()->frameRandomStart - 0.25f) < 1e-5f);
    CHECK(std::abs(e->config()->startRotMin + 30.f) < 1e-5f);
    CHECK(std::abs(e->config()->startRotMax - 30.f) < 1e-5f);
    CHECK_EQ(e->config()->colorGradient.size(), size_t(2));
    CHECK_EQ(e->config()->sizeCurve.size(), size_t(2));
    CHECK_EQ(e->config()->rotationCurve.size(), size_t(2));
    float r = 0, g = 0, b = 0, a = 0;
    e->config()->colorGradient.sample(0.5f, r, g, b, a);
    CHECK(std::abs(r - 1.f) < 1e-4f);
    CHECK(std::abs(g - 0.25f) < 1e-4f);
}

TEST_CASE("particles.sim.startRotationAndFlipbookAdvance") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(32);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setStartRotation(90.f, 90.f);
    e->setFlipbook(8, 8, 16.f, 0.f);
    e->emit(4);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(e->sim()->particles[size_t(i)].rot - static_cast<float>(3.14159265358979323846) * 0.5f) < 1e-4f);
    }

    ParticleSimSystem::update(0.25f);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(e->sim()->particles[size_t(i)].frame - 4.f) < 1e-4f);
        CHECK(std::abs(e->sim()->particles[size_t(i)].rot - (static_cast<float>(3.14159265358979323846) * 0.5f)) <
              1e-4f);
    }
}

TEST_CASE("particles.emitter.bursts") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->addBurst(0.1f, 20);
    e->start();

    ParticleSimSystem::update(0.05f);
    CHECK_EQ(e->getCount(), 0);
    ParticleSimSystem::update(0.06f);
    CHECK_EQ(e->getCount(), 20);
    // Bursts fire only once.
    ParticleSimSystem::update(0.2f);
    CHECK_EQ(e->getCount(), 20);
}

TEST_CASE("particles.emitter.gravityDampingLimit") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setGravity(0.f, 100.f);
    e->emit(1);
    ParticleSimSystem::update(1.f);
    CHECK(std::abs(e->sim()->particles[0].vy - 100.f) < 1e-3f);

    ParticleEmitter *d = mod->newEmitter(16);
    d->setParticleLifetime(5.f, 5.f);
    d->setSpeed(100.f, 100.f);
    d->setDamping(1.f);
    d->emit(1);
    ParticleSimSystem::update(0.5f);
    CHECK(std::abs(d->sim()->particles[0].vx - 50.f) < 1e-3f);

    ParticleEmitter *l = mod->newEmitter(16);
    l->setParticleLifetime(5.f, 5.f);
    l->setSpeed(100.f, 100.f);
    l->setLimitVelocity(30.f);
    l->emit(1);
    ParticleSimSystem::update(0.1f);
    const float sp = std::sqrt(l->sim()->particles[0].vx * l->sim()->particles[0].vx +
                               l->sim()->particles[0].vy * l->sim()->particles[0].vy);
    CHECK_LE(sp, 30.001f);
}

TEST_CASE("particles.emitter.inheritVelocity") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(64);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setEmissionRate(10.f);
    e->setInheritVelocity(0.5f);
    e->setPosition(0.f, 0.f);
    e->start();
    ParticleSimSystem::update(0.1f);
    e->setPosition(10.f, 0.f);
    ParticleSimSystem::update(0.1f);
    REQUIRE_GT(e->getCount(), 1);
    const auto &p = e->sim()->particles[size_t(e->sim()->alive - 1)];
    CHECK(std::abs(p.vx - 50.f) < 1e-3f);
}

TEST_CASE("particles.emitter.localSpace") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setSimulationSpace("local");
    e->setPosition(0.f, 0.f);
    e->emit(1);
    ParticleSimSystem::update(0.1f);
    e->setPosition(100.f, 50.f);
    ParticleSimSystem::update(0.1f);
    const auto &p = e->sim()->particles[0];
    CHECK(std::abs(p.x - 100.f) < 1e-3f);
    CHECK(std::abs(p.y - 50.f) < 1e-3f);
}

TEST_CASE("particles.emitter.collisionBounds") {
    auto            *mod = Particles::create();
    ParticleEmitter *k   = mod->newEmitter(16);
    k->setParticleLifetime(5.f, 5.f);
    k->setSpeed(0.f, 0.f);
    k->setPosition(0.f, 0.f);
    k->setCollision("kill");
    k->setCollisionBounds(true, 10.f, 0.f, 100.f, 100.f);
    k->emit(1);
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(k->getCount(), 0);

    ParticleEmitter *b = mod->newEmitter(16);
    b->setParticleLifetime(5.f, 5.f);
    b->setSpeed(100.f, 100.f);
    b->setDirection(0.f);
    b->setSpread(0.f);
    b->setPosition(0.f, 0.f);
    b->setCollision("bounce", 4.f, 1.f);
    b->setCollisionBounds(true, 0.f, -1000.f, 15.f, 1000.f);
    b->emit(1);
    ParticleSimSystem::update(0.2f);
    CHECK_LT(b->sim()->particles[0].vx, 0.f);
}

TEST_CASE("particles.emitter.overflowPauseAndMaxDelta") {
    auto            *mod = Particles::create();
    ParticleEmitter *p   = mod->newEmitter(4);
    p->setParticleLifetime(5.f, 5.f);
    p->setSpeed(0.f, 0.f);
    p->setEmissionRate(100.f);
    p->setOverflowMode("pause");
    p->start();
    ParticleSimSystem::update(0.1f);
    CHECK_EQ(p->getCount(), 4);
    CHECK_GT(p->sim()->emitAccum, 0.f);

    ParticleEmitter *m = mod->newEmitter(4);
    m->setParticleLifetime(5.f, 5.f);
    m->setSpeed(100.f, 100.f);
    m->setMaxDeltaTime(0.1f);
    m->emit(1);
    ParticleSimSystem::update(10.f);
    CHECK(std::abs(m->sim()->particles[0].x - 10.f) < 1e-2f);
}

TEST_CASE("particles.emitter.subEmitter") {
    auto            *mod    = Particles::create();
    ParticleEmitter *parent = mod->newEmitter(32);
    ParticleEmitter *child  = mod->newEmitter(32);
    ParticleEmitter *ashes  = mod->newEmitter(32);
    parent->setParticleLifetime(5.f, 5.f);
    parent->setSpeed(0.f, 0.f);
    child->setParticleLifetime(5.f, 5.f);
    child->setSpeed(0.f, 0.f);
    ashes->setParticleLifetime(5.f, 5.f);
    ashes->setSpeed(0.f, 0.f);
    parent->addSubEmitter(child, "birth");
    parent->addSubEmitter(ashes, "death");
    parent->emit(5);
    CHECK_EQ(child->getCount(), 5);
    CHECK_EQ(ashes->getCount(), 0);

    parent->setParticleLifetime(0.2f, 0.2f);
    parent->emit(1);
    ParticleSimSystem::update(0.3f);
    CHECK_EQ(ashes->getCount(), 1);
}

TEST_CASE("particles.emitter.forceFields") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setPosition(0.f, 0.f);
    e->emit(1);
    e->sim()->particles[0].x = 100.f;
    e->addForceField(0.f, 0.f, 200.f, 50.f, 1.f);  // attract toward origin
    ParticleSimSystem::update(1.f);
    CHECK(std::abs(e->sim()->particles[0].vx + 25.f) < 1e-3f);

    ParticleEmitter *r = mod->newEmitter(16);
    r->setParticleLifetime(5.f, 5.f);
    r->setSpeed(0.f, 0.f);
    r->setPosition(0.f, 0.f);
    r->emit(1);
    r->sim()->particles[0].x = 100.f;
    r->addForceField(0.f, 0.f, 200.f, -50.f, 1.f);  // repel
    ParticleSimSystem::update(1.f);
    CHECK(std::abs(r->sim()->particles[0].vx - 25.f) < 1e-3f);
}

TEST_CASE("particles.draw.shaderStorage") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    CHECK(e->getShader() == nullptr);
    auto *fake = reinterpret_cast<eve::graphics::Shader *>(uintptr_t(0x1234));
    e->setShader(fake);
    CHECK(e->getShader() == fake);
    e->setShader(nullptr);
    CHECK(e->getShader() == nullptr);
}

TEST_CASE("particles.lights.followParticles") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->setPosition(30.f, 40.f);
    e->setLights(true, 50.f, 2.f, 1.f, 0.5f, 0.2f, 3);
    e->emit(2);
    ParticleLightSystem::update();
    REQUIRE_EQ(e->lights()->pool.size(), size_t(3));
    CHECK(std::abs(e->lights()->pool[0]->getX() - 30.f) < 1e-3f);
    CHECK(std::abs(e->lights()->pool[0]->getY() - 40.f) < 1e-3f);
    CHECK(std::abs(e->lights()->pool[0]->getRadius() - 50.f) < 1e-3f);
    CHECK(e->lights()->pool[0]->isEnabled());
    CHECK(e->lights()->pool[1]->isEnabled());
    CHECK(!e->lights()->pool[2]->isEnabled());

    e->setLights(false);
    ParticleLightSystem::update();
    CHECK(!e->lights()->pool[0]->isEnabled());
}

TEST_CASE("particles.gpu.configAndFallback") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(16);
    e->setGpuSimulation(true);
    CHECK(e->getGpuSimulation());
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(100.f, 100.f);
    e->setDirection(0.f);
    e->setSpread(0.f);
    e->emit(1);
    ParticleSimSystem::update(0.1f);
    // Headless CI has no GPU device → the emitter must fall back to the CPU
    // integrator; either path moves the particle forward.
    CHECK_GT(e->sim()->particles[0].x, 1.f);
    CHECK_GT(e->getCount(), 0);

    ParticleEmitter *c    = mod->newEmitter(16);
    const char      *json = R"({"gpuSimulation": true})";
    CHECK(c->applyConfig(json));
    CHECK(c->getGpuSimulation());
}

TEST_CASE("particles.scale.manyEmitters") {
    // Regression guard for ECS view iteration / simulation at scale: a few
    // hundred emitters sharing one ECS table must update without stalling or
    // losing emitters. (Output-volume note: this suite must be run with
    // redirected output — a full pipe blocks once it fills.)
    auto                          *mod       = Particles::create();
    constexpr int                  kEmitters = 128;
    std::vector<ParticleEmitter *> emitters;
    emitters.reserve(size_t(kEmitters));
    for (int i = 0; i < kEmitters; ++i) {
        ParticleEmitter *e = mod->newEmitter(32);
        e->setParticleLifetime(2.f, 2.f);
        e->setEmissionRate(10.f);
        e->setSpeed(10.f, 20.f);
        e->setPosition(float(i % 16) * 10.f, float(i / 16) * 10.f);
        e->start();
        emitters.push_back(e);
    }
    for (int frame = 0; frame < 10; ++frame) ParticleSimSystem::update(0.016f);

    int totalAlive = 0;
    for (auto *e : emitters) totalAlive += e->getCount();
    CHECK_GT(totalAlive, 0);
    // First and last emitters must both have been iterated by the system view.
    CHECK_GT(emitters[0]->getCount(), 0);
    CHECK_GT(emitters[size_t(kEmitters) - 1]->getCount(), 0);
    CHECK(emitters[size_t(kEmitters) - 1]->isActive());
}

TEST_CASE("particles.config.applyForceFieldsLights") {
    auto            *mod  = Particles::create();
    ParticleEmitter *e    = mod->newEmitter(16);
    const char      *json = R"({
      "forceFields": [{"x":0,"y":0,"radius":200,"strength":50,"falloff":2}],
      "lights": {"enabled":true,"max":6,"radius":80,"intensity":1.5,"color":[1,0.5,0.2]}
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->config()->forceFields.size(), size_t(1));
    CHECK(std::abs(e->config()->forceFields[0].strength - 50.f) < 1e-5f);
    CHECK(std::abs(e->config()->forceFields[0].falloff - 2.f) < 1e-5f);
    CHECK(e->config()->lights.enabled);
    CHECK_EQ(e->config()->lights.max, 6);
    CHECK(std::abs(e->config()->lights.r - 1.f) < 1e-5f);
    CHECK(std::abs(e->config()->lights.g - 0.5f) < 1e-5f);
    CHECK(std::abs(e->config()->lights.b - 0.2f) < 1e-5f);
    CHECK(std::abs(e->config()->lights.intensity - 1.5f) < 1e-5f);
}

TEST_CASE("particles.config.applyP1Json") {
    auto            *mod  = Particles::create();
    ParticleEmitter *e    = mod->newEmitter(32);
    const char      *json = R"({
      "bursts": [{"time":0.05,"count":12}],
      "prewarm": 0.5,
      "gravity": [0, 50],
      "damping": 0.2,
      "limitVelocity": 40,
      "velocityOverLifetime": [[0, 1], [1, 0.2]],
      "inheritVelocity": 0.4,
      "simulationSpace": "local",
      "noise": {"strength": 3, "frequency": 2, "speed": 1.5},
      "collision": {"mode":"bounce","radius":6,"restitution":0.8,"lifetimeLoss":0.1},
      "collisionBounds": {"enabled":true,"minX":0,"minY":0,"maxX":640,"maxY":480},
      "worldCollision": true,
      "renderMode": "stretched",
      "stretch": 1.5,
      "overflowMode": "pause",
      "maxDeltaTime": 0.05
    })";
    CHECK(e->applyConfig(json));
    CHECK_EQ(e->config()->bursts.size(), size_t(1));
    CHECK(std::abs(e->config()->prewarmSeconds - 0.5f) < 1e-5f);
    CHECK(std::abs(e->config()->gravityY - 50.f) < 1e-5f);
    CHECK(std::abs(e->config()->damping - 0.2f) < 1e-5f);
    CHECK(std::abs(e->config()->limitVelocity - 40.f) < 1e-5f);
    CHECK_EQ(e->config()->velocityCurve.size(), size_t(2));
    CHECK(std::abs(e->config()->inheritVelocity - 0.4f) < 1e-5f);
    CHECK_EQ(e->config()->simSpace, std::string("local"));
    CHECK(std::abs(e->config()->noiseStrength - 3.f) < 1e-5f);
    CHECK(std::abs(e->config()->noiseFrequency - 2.f) < 1e-5f);
    CHECK_EQ(e->config()->collisionMode, std::string("bounce"));
    CHECK(std::abs(e->config()->collisionRadius - 6.f) < 1e-5f);
    CHECK(std::abs(e->config()->collisionRestitution - 0.8f) < 1e-5f);
    CHECK(e->config()->collisionBoundsEnabled);
    CHECK(std::abs(e->config()->boundsMaxX - 640.f) < 1e-5f);
    CHECK(e->config()->worldCollision);
    CHECK_EQ(e->config()->renderMode, std::string("stretched"));
    CHECK(std::abs(e->config()->stretchFactor - 1.5f) < 1e-5f);
    CHECK_EQ(e->config()->overflowMode, std::string("pause"));
    CHECK(std::abs(e->config()->maxDeltaTime - 0.05f) < 1e-5f);
}

TEST_CASE("particles.renderSystem.cameraTransform") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    e->setPosition(0.f, 0.f);
    e->setParticleLifetime(5.f, 5.f);
    e->setSpeed(0.f, 0.f);
    e->emit(1);
    auto *cam         = eve::graphics::Camera2D::createCamera();
    cam->data()->x    = 0.f;
    cam->data()->y    = 0.f;
    cam->data()->zoom = 2.f;
    e->setCamera(cam);
    // No Graphics — must not crash; camera path is covered by wiring.
    ParticleRenderSystem::render(nullptr);
    CHECK_EQ(e->getCount(), 1);
}

TEST_CASE("particles.config.applyJsonText") {
    auto            *mod  = Particles::create();
    ParticleEmitter *e    = mod->newEmitter(32);
    const char      *json = R"({
      "preset": "spark",
      "x": 12.5,
      "y": 34,
      "emissionRate": 40,
      "particleLifetime": [0.5, 1.5],
      "speed": [10, 20],
      "colorStart": [1, 0, 0, 1],
      "colorEnd": [0, 0, 1, 0],
      "layer": 3,
      "autoStart": true
    })";
    CHECK(e->applyConfig(json));
    CHECK(std::abs(e->getX() - 12.5f) < 1e-4f);
    CHECK(std::abs(e->getY() - 34.f) < 1e-4f);
    CHECK(std::abs(e->getEmissionRate() - 40.f) < 1e-4f);
    CHECK(std::abs(e->getParticleLifetimeMin() - 0.5f) < 1e-4f);
    CHECK(std::abs(e->getParticleLifetimeMax() - 1.5f) < 1e-4f);
    CHECK_EQ(e->getLayer(), 3);
    CHECK(e->isActive());
}

TEST_CASE("particles.config.applyInvalid") {
    auto            *mod = Particles::create();
    ParticleEmitter *e   = mod->newEmitter(8);
    CHECK(!e->applyConfig("{bad"));
    CHECK(!e->applyConfig("[1,2,3]"));
}
}  // namespace test_particles_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_material_selector)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include "graphics/Texture.h"
#include "particles/ParticleEmitter.h"
#include "particles/PcgMaterialSelector.h"
// pcg_material_selector.cpp
namespace test_pcg_material_selector {
using namespace eve;
TEST_CASE("PcgMaterialSelector.skipsZeroAndIsDeterministic") {
    particles::PcgMaterialSelector selector;
    graphics::Texture              reserved, a, b;
    REQUIRE(selector.add(&reserved).ok());
    REQUIRE(selector.add(&a).ok());
    REQUIRE(selector.add(&b).ok());
    auto *emitter = particles::ParticleEmitter::createEmitter(8);
    auto  first   = selector.selectAndApply(emitter, 20260912u);
    REQUIRE(first.ok());
    CHECK(first.value() >= 1);
    CHECK(first.value() < 3);
    CHECK(emitter->getTexture() == (first.value() == 1 ? &a : &b));
    auto replay = selector.selectAndApply(emitter, 20260912u);
    REQUIRE(replay.ok());
    CHECK_EQ(replay.value(), first.value());
    ecs::DestroyEntity(emitter);
}
TEST_CASE("PcgMaterialSelector.failureIsAtomic") {
    particles::PcgMaterialSelector selector;
    graphics::Texture              old, only;
    auto                          *emitter = particles::ParticleEmitter::createEmitter(8);
    emitter->setTexture(&old);
    CHECK(!selector.add(nullptr).ok());
    REQUIRE(selector.add(&only).ok());
    CHECK(!selector.selectAndApply(emitter, 1u).ok());
    CHECK(emitter->getTexture() == &old);
    CHECK(!selector.selectAndApply(nullptr, 1u).ok());
    ecs::DestroyEntity(emitter);
}
}  // namespace test_pcg_material_selector
#endif
