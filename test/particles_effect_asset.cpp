#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionPreview.h"
#include "action/ActionVfxBlock.h"
#include "common/Capability.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "particles/ParticleEffect.h"
#include "particles/ParticleEmitter.h"
#include "particles/Particles.h"
#include "filesystem/Filesystem.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneObject.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace eve::particles;

namespace {

eve::LogicalId logicalId(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

class TestVfxAttachmentSource final : public eve::IAttachmentPointSource {
public:
    eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(
        std::string_view name, eve::AttachmentPoint localOffset) const override {
        ++calls;
        if (name != "hand_r")
            return eve::Result<eve::AttachmentPoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing test bone", "bone"));
        return eve::Result<eve::AttachmentPoint>::success(
            {50.f + localOffset.x, 60.f + localOffset.y, 70.f + localOffset.z});
    }
    mutable int calls = 0;
};

int liveParticleCount() {
    int count = 0;
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [config] = *it;
        if (config->entity) count += config->entity->getCount();
    }
    return count;
}

float firstPlaybackSpeed() {
    auto view = ecs::View<ParticleEmitter, ParticleEmitter::Config>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [config] = *it;
        if (config->entity) return config->entity->getPlaybackSpeed();
    }
    return 0.0f;
}

}  // namespace

TEST_CASE("particles.effectAsset.versionedMultiEmitterContract") {
    const char* json = R"({
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

    auto*     particles = Particles::create();
    const int before    = particles->getEmitterCount();
    auto*     effect    = particles->newEffectFromText(json);
    REQUIRE(effect != nullptr);
    CHECK(particles->getLastEffectError().empty());
    CHECK_EQ(effect->getVersion(), 1);
    CHECK_EQ(effect->getEmitterCount(), 2);
    CHECK_EQ(effect->getEmitterName(0), std::string("core"));
    CHECK_EQ(effect->getEmitterName(1), std::string("haze"));
    CHECK(effect->getEmitter(2) == nullptr);

    auto* core = effect->getEmitterByName("core");
    auto* haze = effect->getEmitterByName("haze");
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
    auto* particles   = Particles::create();
    auto* unsupported = particles->newEffectFromText(
        R"({"type":"eve.particle-effect","version":2,"emitters":[{"name":"a","emitter":{}}]})");
    CHECK(unsupported == nullptr);
    CHECK(particles->getLastEffectError().find("unsupported") != std::string::npos);

    auto* duplicate = particles->newEffectFromText(R"({
        "type":"eve.particle-effect",
        "version":1,
        "emitters":[{"name":"a","emitter":{}},{"name":"a","emitter":{}}]
    })");
    CHECK(duplicate == nullptr);
    CHECK(particles->getLastEffectError().find("duplicate") != std::string::npos);
}

TEST_CASE("particles.effectAsset.playbackLabAssetIsConsumable") {
    const std::string path = std::string(EVENGINE_SOURCE_DIR) + "/examples/particle-playback-lab/impact.effect.json";
    std::ifstream     input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream text;
    text << input.rdbuf();

    std::string error;
    auto*       effect = ParticleEffect::fromText(text.str(), path, &error);
    REQUIRE(effect != nullptr);
    CHECK(error.empty());
    CHECK_EQ(effect->getSourcePath(), path);
    CHECK_EQ(effect->getEmitterCount(), 2);
    CHECK(effect->getEmitterByName("core") != nullptr);
    CHECK(effect->getEmitterByName("sparks") != nullptr);
    CHECK(effect->hasFloatParameter("intensity"));
    delete effect;
}

TEST_CASE("particles.actionBlockProviderOwnsRealVfxEnterUpdateExit") {
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* particles = Particles::create();
    auto* scene = eve::scene::Scene::create();
    auto mounted = scene->mountAs("action-vfx", eve::scene::node("source").withPosition(5.f, 6.f, 7.f));
    REQUIRE(mounted.ok());
    auto source = eve::scene::SceneObject::createObject("action-vfx", "source");
    REQUIRE(source.ok());
    auto  registry  = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    const int                       before = particles->getEmitterCount();

    eve::action::ActionAdvance advance;
    advance.id           = eve::action::ActionExecutionId{91};
    advance.totalElapsed = eve::Duration::fromNanoseconds(10);
    eve::Value::Object payload{
        {"uri", "examples/particle-playback-lab/impact.effect.json"},
        {"stopBehavior", "clear_immediately"},
        {"clipStartTime", 0.2},
        {"clipEndTime", 1.2},
        {"bone", "hand_r"},
        {"positionOffset", eve::Value::Array{12.0, 34.0, 0.0}},
        {"rotationOffsetDegrees", eve::Value::Array{0.0, 0.0, 90.0}},
        {"scale", eve::Value::Array{2.0, 2.0, 2.0}},
    };
    advance.timelineEvents.push_back({eve::action::ActionTimelineEventKind::StateEnter,
                                      logicalId("presentation-track:vfx"), logicalId("presentation-vfx:impact"),
                                      logicalId("presentation:vfx-state"), advance.totalElapsed, payload});
    advance.activeBlocks.push_back({logicalId("presentation-track:vfx"), logicalId("presentation-vfx:impact"),
                                    logicalId("presentation:vfx-state"), eve::Duration::zero(),
                                    eve::Duration::fromNanoseconds(1'000'000'000), payload});
    eve::action::ActionNotifyContext context;
    context.executionId = advance.id;
    context.playbackRate = 2.0;
    context.source = ecs::handle_of(source.value());
    TestVfxAttachmentSource attachmentSource;
    context.sourceAttachment = std::cref(attachmentSource);
    auto applied = runtime.apply(advance, context);
    REQUIRE(applied.ok());
    CHECK_EQ(attachmentSource.calls, 2);
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    CHECK(std::abs(firstPlaybackSpeed() - 2.0f) < 0.001f);

    context.time = eve::Duration::fromNanoseconds(20);
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(particles->getEmitterCount(), before);
}

TEST_CASE("particles.actionVfxStopEmittingPreservesThenReapsResiduals") {
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* particles = Particles::create();
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    const int before = particles->getEmitterCount();

    eve::action::ActionAdvance enter;
    enter.id           = eve::action::ActionExecutionId{95};
    enter.totalElapsed = eve::Duration::fromNanoseconds(100'000'000);
    eve::Value::Object payload{{"uri", "examples/particle-playback-lab/impact.effect.json"},
                               {"stopBehavior", "stop_emitting"}};
    enter.timelineEvents.push_back({eve::action::ActionTimelineEventKind::StateEnter,
                                    logicalId("presentation-track:vfx"),
                                    logicalId("presentation-vfx:residual"),
                                    logicalId("presentation:vfx-state"), enter.totalElapsed, payload});
    enter.activeBlocks.push_back({logicalId("presentation-track:vfx"),
                                  logicalId("presentation-vfx:residual"),
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
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* particles = Particles::create();
    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());
    const int before = particles->getEmitterCount();
    eve::action::ActionAdvance trigger;
    trigger.id = eve::action::ActionExecutionId{94};
    trigger.timelineEvents.push_back({eve::action::ActionTimelineEventKind::Notify,
                                      logicalId("presentation-track:vfx"),
                                      logicalId("presentation-vfx:hit"), logicalId("presentation:vfx"),
                                      eve::Duration::zero(),
                                      {{"uri", "examples/particle-playback-lab/impact.effect.json"},
                                       {"lifetimeSeconds", 1.5}}});
    eve::action::ActionNotifyContext context;
    context.executionId = trigger.id;
    REQUIRE(runtime.apply(trigger, context).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    eve::action::ActionAdvance expired;
    expired.id = trigger.id;
    expired.totalElapsed = eve::Duration::fromNanoseconds(2'000'000'000LL);
    REQUIRE(runtime.apply(expired, context).ok());
    CHECK_EQ(particles->getEmitterCount(), before);
}

TEST_CASE("particles.actionPreviewRetainsScrubsRebuildsAndCleansVfx") {
    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem->mountRealDirectory(EVENGINE_SOURCE_DIR, "/", false));
    auto* particles = Particles::create();
    REQUIRE_EQ(eve::cap::listenerCount<eve::action::IActionPreviewSinkProvider>(), 1u);
    auto* provider = eve::cap::listenerAt<eve::action::IActionPreviewSinkProvider>(0);
    REQUIRE(provider != nullptr);
    auto sink = provider->createActionPreviewSink();
    REQUIRE(sink.ok());
    const int before = particles->getEmitterCount();

    eve::Value::Object payload{{"uri", "examples/particle-playback-lab/impact.effect.json"},
                               {"clipStartTime", 0.2}, {"clipEndTime", 1.2},
                               {"stopBehavior", "clear_immediately"}};
    eve::action::ActionPreviewFrame frame;
    frame.reason  = eve::action::ActionPreviewReason::Seek;
    frame.current = eve::Duration::fromNanoseconds(500'000'000);
    frame.activeBlocks.push_back({logicalId("presentation-track:vfx"),
                                  logicalId("presentation-vfx:preview"),
                                  logicalId("presentation:vfx-state"),
                                  eve::Duration::fromNanoseconds(500'000'000),
                                  eve::Duration::fromNanoseconds(1'000'000'000), payload});
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    sink.value()->present(frame);
    const int seekParticles = liveParticleCount();
    CHECK(seekParticles > 0);

    frame.reason = eve::action::ActionPreviewReason::Advance;
    frame.current = eve::Duration::fromNanoseconds(750'000'000);
    frame.activeBlocks.front().localTime = frame.current;
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    sink.value()->present(frame);
    const int advancedParticles = liveParticleCount();
    CHECK(advancedParticles > 0);

    frame.reason = eve::action::ActionPreviewReason::Refresh;
    REQUIRE(sink.value()->prepare(frame).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 4);
    sink.value()->present(frame);
    CHECK_EQ(particles->getEmitterCount(), before + 2);
    CHECK_EQ(liveParticleCount(), advancedParticles);

    auto invalid = frame;
    invalid.activeBlocks.front().payload["clipEndTime"] = 0.1;
    CHECK(!sink.value()->prepare(invalid).ok());
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    eve::action::ActionPreviewFrame empty;
    empty.reason  = eve::action::ActionPreviewReason::Seek;
    empty.current = eve::Duration::fromNanoseconds(800'000'000);
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(particles->getEmitterCount(), before);

    eve::action::ActionPreviewCue instant;
    instant.kind   = eve::action::ActionPreviewCueKind::Vfx;
    instant.itemId = logicalId("presentation-vfx:instant-preview");
    instant.type   = logicalId("presentation:vfx");
    instant.time   = eve::Duration::fromNanoseconds(800'000'000);
    instant.payload = {{"uri", "examples/particle-playback-lab/impact.effect.json"},
                       {"lifetimeSeconds", 1.0}};
    empty.cues.push_back(std::move(instant));
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(particles->getEmitterCount(), before + 2);

    empty.reason  = eve::action::ActionPreviewReason::Advance;
    empty.current = eve::Duration::fromNanoseconds(2'000'000'000);
    empty.cues.clear();
    REQUIRE(sink.value()->prepare(empty).ok());
    sink.value()->present(empty);
    CHECK_EQ(particles->getEmitterCount(), before);
    sink.value().reset();
    CHECK_EQ(particles->getEmitterCount(), before);
}
