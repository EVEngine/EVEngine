#include <array>
#include <cmath>
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshParticleEmitter.h"
#include "stylize/MeshVfxAsset.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "stylize/SkillMeshEffect.h"
#include "stylize/Stylize.h"
#include "stylize/TrailEffect.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// stylize_mesh_particles.cpp
using namespace eve::stylize;

TEST_CASE("stylize.meshParticles.fixedStepDeterminismAndBurst") {
    MeshParticleEmitterConfig config;
    config.capacity         = 32;
    config.emissionRate     = 10.f;
    config.lifetimeMin      = 2.f;
    config.lifetimeMax      = 2.f;
    config.speedMin         = 1.f;
    config.speedMax         = 1.f;
    config.fixedStepSeconds = 0.1f;
    config.maximumSubsteps  = 8;
    config.randomSeed       = 42;
    config.bursts           = {{0.2f, 3}};

    MeshParticleEmitter first(config);
    MeshParticleEmitter second(config);
    first.start();
    second.start();
    auto firstReport  = first.advance(0.3f);
    auto secondReport = second.advance(0.1f);
    secondReport      = second.advance(0.2f);
    REQUIRE_EQ(firstReport.alive, 6u);
    REQUIRE_EQ(first.snapshot().size(), second.snapshot().size());
    const auto a = first.snapshot();
    const auto b = second.snapshot();
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE_EQ(a[i].stableId, b[i].stableId);
        REQUIRE_EQ(a[i].model, b[i].model);
        REQUIRE_EQ(a[i].color, b[i].color);
    }
}

TEST_CASE("stylize.meshParticles.capacityReportsDroppedSpawns") {
    MeshParticleEmitterConfig config;
    config.capacity         = 4;
    config.fixedStepSeconds = 0.1f;
    config.bursts           = {{0.f, 7}};
    MeshParticleEmitter emitter(config);
    emitter.start();
    const auto report = emitter.advance(0.1f);
    REQUIRE_EQ(report.spawned, 4u);
    REQUIRE_EQ(report.dropped, 3u);
    REQUIRE_EQ(report.alive, 4u);
}

TEST_CASE("stylize.meshParticles.substepGuardReportsDiscardedTime") {
    MeshParticleEmitterConfig config;
    config.fixedStepSeconds = 0.1f;
    config.maximumSubsteps  = 2;
    MeshParticleEmitter emitter(config);
    emitter.start();
    const auto report = emitter.advance(0.55f);
    REQUIRE_EQ(report.simulatedSteps, 2u);
    REQUIRE(report.discardedTimeSeconds > 0.29f);
    REQUIRE(report.discardedTimeSeconds < 0.31f);
}

TEST_CASE("stylize.meshParticles.bridgeUsesSharedLodAndAlphaOrdering") {
    MeshParticleEmitterConfig config;
    config.capacity         = 3;
    config.fixedStepSeconds = 0.1f;
    MeshParticleEmitter emitter(config);
    emitter.setOrigin({0.f, 0.f, 5.f});
    const auto emitted = emitter.emit(3);
    REQUIRE(emitted == std::size_t{3});
    MeshEffectInstance    effect("slash");
    const MeshVfxBatchKey key{11, 12, 13, 0, MeshVfxBatchBlend::Alpha};
    const auto            inputs =
        buildMeshParticleRenderInputs(emitter.snapshot(), &effect, reinterpret_cast<eve::graphics::Mesh*>(1), nullptr,
                                      key, 77, glm::vec3(0.f), 100.f, 50.f, 2);
    REQUIRE_EQ(inputs.lodCandidates.size(), 3u);
    REQUIRE_EQ(inputs.renderItems.size(), 3u);
    REQUIRE_EQ(inputs.commands.size(), 3u);
    REQUIRE_EQ(inputs.commands[0].stableInstanceId, inputs.renderItems[0].stableInstanceId);
    REQUIRE_EQ(static_cast<int>(inputs.commands[0].kind), static_cast<int>(MeshVfxRendererCommand::Kind::Particle));

    MeshVfxScalabilityPolicy policy;
    policy.maximumDistance = 50.f;
    policy.workUnitBudget  = 12;
    const auto decisions   = MeshVfxScalabilityPlanner(policy).plan(inputs.lodCandidates);
    const auto queue       = MeshVfxRenderBatchPlanner{}.build(inputs.renderItems, decisions, 0);
    REQUIRE_EQ(queue.stats.queuedDraws, 3u);
    REQUIRE_EQ(queue.batches.size(), 1u);
}

TEST_CASE("stylize.meshParticles.curvesDriveScaleColorAndRotation") {
    MeshParticleEmitterConfig config;
    config.capacity           = 1;
    config.lifetimeMin        = 1.f;
    config.lifetimeMax        = 1.f;
    config.fixedStepSeconds   = 0.25f;
    config.rotationMinRadians = 0.f;
    config.rotationMaxRadians = 0.f;
    config.angularVelocityMin = 2.f;
    config.angularVelocityMax = 2.f;
    config.scaleCurve         = {{1.f, 0.f}, {0.f, 2.f}, {0.5f, 1.f}};
    config.colorGradient      = {{0.f, {1.f, 0.f, 0.f, 1.f}}, {1.f, {0.f, 0.f, 1.f, 0.f}}};
    MeshParticleEmitter emitter(config);
    const auto          emitted = emitter.emit(1);
    REQUIRE(emitted == std::size_t{1});
    const auto report = emitter.advance(0.5f);
    REQUIRE_EQ(report.simulatedSteps, 2u);
    const auto particles = emitter.snapshot();
    REQUIRE_EQ(particles.size(), 1u);
    REQUIRE(particles[0].color.r > 0.49f);
    REQUIRE(particles[0].color.r < 0.51f);
    REQUIRE(particles[0].color.b > 0.49f);
    REQUIRE(particles[0].color.b < 0.51f);
    const float axisScale = glm::length(glm::vec3(particles[0].model[0]));
    REQUIRE(axisScale > 0.99f);
    REQUIRE(axisScale < 1.01f);
}

TEST_CASE("stylize.meshParticles.volumeShapesStayInsideConfiguredBounds") {
    MeshParticleEmitterConfig sphere;
    sphere.capacity     = 64;
    sphere.shape        = MeshParticleShape::Sphere;
    sphere.sphereRadius = 3.f;
    sphere.randomSeed   = 7;
    MeshParticleEmitter sphereEmitter(sphere);
    const auto          sphereEmitted = sphereEmitter.emit(64);
    REQUIRE(sphereEmitted == std::size_t{64});
    for (const auto& particle : sphereEmitter.snapshot()) {
        REQUIRE(glm::length(glm::vec3(particle.model[3])) <= 3.0001f);
    }

    MeshParticleEmitterConfig box;
    box.capacity   = 64;
    box.shape      = MeshParticleShape::Box;
    box.boxExtents = {1.f, 2.f, 3.f};
    box.randomSeed = 9;
    MeshParticleEmitter boxEmitter(box);
    const auto          boxEmitted = boxEmitter.emit(64);
    REQUIRE(boxEmitted == std::size_t{64});
    for (const auto& particle : boxEmitter.snapshot()) {
        const glm::vec3 position(particle.model[3]);
        REQUIRE(std::abs(position.x) <= 1.0001f);
        REQUIRE(std::abs(position.y) <= 2.0001f);
        REQUIRE(std::abs(position.z) <= 3.0001f);
    }
}

// stylize_mesh_vfx.cpp
using eve::stylize::MeshEffectPlayback;
using eve::stylize::MeshEffectState;
using eve::stylize::MeshEffectTargetHandle;
using eve::stylize::prepareTrailUpload;
using eve::stylize::SkillMeshEffectKind;
using eve::stylize::Stylize;
using eve::stylize::TrailAppendResult;
using eve::stylize::TrailEmitter;
using eve::stylize::TrailSettings;

TEST_CASE("stylize.meshVfx.playbackAndBinding") {
    auto* module = Stylize::create();
    auto  effect = module->createMeshEffect("ember");
    REQUIRE(effect.get() != nullptr);
    CHECK(!effect->isBound());
    effect->bindTarget(MeshEffectTargetHandle(7, 2));
    CHECK(effect->isBound());
    CHECK_EQ(effect->target().index(), 7u);
    CHECK_EQ(effect->target().generation(), 2u);
    effect->setPlayback(MeshEffectPlayback{0.1f, 0.2f, 0.1f, false});
    effect->play();
    CHECK(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::FadingIn));
    effect->update(0.05f);
    CHECK(std::fabs(effect->intensity() - 0.5f) < 0.001f);
    effect->update(0.10f);
    CHECK(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::Active));
    CHECK(std::fabs(effect->style().getFloat("time") - 0.15f) < 0.001f);
    effect->stop(0.1f);
    effect->update(0.05f);
    CHECK(std::fabs(effect->intensity() - 0.5f) < 0.001f);
    effect->update(0.05f);
    CHECK(static_cast<int>(effect->state()) == static_cast<int>(MeshEffectState::Finished));
    effect->unbindTarget();
    CHECK(!effect->isBound());
}

TEST_CASE("stylize.meshVfx.ribbonGeometryAndDiscontinuity") {
    TrailEmitter trail(TrailSettings{8, 1.f, 0.01f, 1.f});
    CHECK(static_cast<int>(trail.append({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::StartedSegment));
    CHECK(static_cast<int>(trail.append({0.5f, 0.f, 0.f}, {0.5f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::Added));
    auto mesh = trail.buildMesh();
    CHECK_EQ(mesh.vertices.size(), 4u);
    CHECK_EQ(mesh.indices.size(), 6u);
    CHECK_EQ(mesh.vertices[0].uv.x, 0.f);
    CHECK_EQ(mesh.vertices[2].uv.x, 1.f);
    CHECK(static_cast<int>(trail.append({4.f, 0.f, 0.f}, {4.f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::StartedSegment));
    mesh = trail.buildMesh();
    CHECK_EQ(mesh.vertices.size(), 6u);
    CHECK_EQ(mesh.indices.size(), 6u);
    trail.update(1.f);
    CHECK_EQ(trail.sampleCount(), 0u);
    CHECK(trail.buildMesh().indices.empty());
}

TEST_CASE("stylize.meshVfx.rejectsInvalidConfiguration") {
    bool threw = false;
    try {
        TrailEmitter invalid(TrailSettings{1, 1.f, 0.f, 1.f});
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        auto* module = Stylize::create();
        auto  effect = module->createMeshEffect("vignette");
        (void)effect;
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("stylize.meshVfx.preparesBackendUpload") {
    TrailEmitter trail(TrailSettings{8, 1.f, 0.f, 2.f});
    (void)trail.append({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    (void)trail.append({0.5f, 0.f, 0.f}, {0.5f, 1.f, 0.f});
    auto prepared = prepareTrailUpload(trail.buildMesh());
    REQUIRE(prepared.ok());
    const auto& upload = prepared.value();
    CHECK_EQ(upload.positions.size(), 12u);
    CHECK_EQ(upload.normals.size(), 12u);
    CHECK_EQ(upload.uvs.size(), 8u);
    CHECK_EQ(upload.indices.size(), 6u);
    CHECK(std::fabs(upload.normals[2]) > 0.9f);
}


TEST_CASE("stylize.meshVfx.skillRecipesComposeRuntime") {
    auto* stylize = Stylize::create();
    auto  slash   = stylize->createSkillMeshEffect(SkillMeshEffectKind::WeaponSlash);
    REQUIRE(slash.get() != nullptr);
    CHECK(slash->hasTrail());
    CHECK_EQ(slash->effect().style().getStyle(), "slash");
    slash->bindTarget(MeshEffectTargetHandle(4, 9));
    slash->play();
    CHECK(static_cast<int>(slash->appendBlade({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f})) ==
          static_cast<int>(TrailAppendResult::StartedSegment));
    slash->update(0.03f);
    CHECK(slash->effect().intensity() > 0.f);
    CHECK_EQ(slash->trail().sampleCount(), 1u);

    auto impact = stylize->createSkillMeshEffect(SkillMeshEffectKind::ImpactFlash);
    REQUIRE(impact.get() != nullptr);
    CHECK(!impact->hasTrail());
    CHECK_EQ(impact->effect().style().getStyle(), "rim");
    impact->play();
    impact->update(0.2f);
    CHECK(static_cast<int>(impact->effect().state()) == static_cast<int>(MeshEffectState::Finished));

    bool rejectedBladeSample = false;
    try {
        (void)impact->appendBlade({0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    } catch (const eve::Exception&) {
        rejectedBladeSample = true;
    }
    CHECK(rejectedBladeSample);
}


// stylize_mesh_vfx_asset.cpp
using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_asset parses layered version one assets") {
    auto parsed = MeshVfxAsset::fromJson(R"({
        "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
        "layers":[
          {"style":"slash","playback":{"fadeIn":0.02,"hold":0.1,"fadeOut":0.2},"parameters":{"intensity":2}},
          {"style":"rim","playback":{"loop":true}}
        ],
        "trail":{"maxSamples":48,"lifetime":0.25,"minDistance":0.01,"teleportDistance":3}
    })");
    REQUIRE(parsed.ok());
    auto asset = std::move(parsed).takeValue();
    REQUIRE_EQ(asset.layers.size(), 2u);
    REQUIRE(asset.trail.has_value());
    auto created = MeshVfxAssetInstance::create(asset);
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    REQUIRE_EQ(instance->layerCount(), 2u);
    REQUIRE(instance->hasTrail());
}

TEST_CASE("stylize.mesh_vfx_asset migrates legacy version zero") {
    auto parsed = MeshVfxAsset::fromJson(
        R"({"schemaVersion":0,"style":"aura","playback":{"hold":0.5},"parameters":{"power":1.5}})");
    REQUIRE(parsed.ok());
    REQUIRE_EQ(parsed.value().layers.size(), 1u);
    REQUIRE_EQ(parsed.value().layers.front().style, "aura");
}

TEST_CASE("stylize.mesh_vfx_asset rejects unknown and invalid fields") {
    auto unknown = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","typo":1}]})");
    REQUIRE(!unknown.ok());
    auto invalid = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","playback":{"hold":-1}}]})");
    REQUIRE(!invalid.ok());
}

TEST_CASE("stylize.mesh_vfx_asset reload is transactional") {
    auto parsed =
        MeshVfxAsset::fromJson(R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}]})");
    REQUIRE(parsed.ok());
    MeshVfxAssetSlot slot(std::move(parsed).takeValue());
    auto             failed = slot.reload(R"({"schemaVersion":99})");
    REQUIRE(!failed.ok());
    REQUIRE_EQ(slot.revision(), 1u);
    REQUIRE_EQ(slot.asset().layers.front().style, "rim");
    auto applied = slot.reload(R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"ember"}]})");
    REQUIRE(applied.ok());
    REQUIRE_EQ(applied.value(), 2u);
    REQUIRE_EQ(slot.asset().layers.front().style, "ember");
}

TEST_CASE("stylize.mesh_vfx_asset evaluates authored parameter curves") {
    MeshVfxFloatCurve curve{{{0.f, 0.f}, {0.5f, 2.f}, {1.f, 0.f}}};
    REQUIRE_EQ(curve.evaluate(0.25f), 1.f);
    REQUIRE_EQ(curve.evaluate(0.75f), 1.f);
    auto invalid = MeshVfxAsset::fromJson(
        R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim","curves":{"intensity":[[0.5,1],[0.25,2]]}}]})");
    REQUIRE(!invalid.ok());
}

TEST_CASE("stylize.mesh_vfx_asset emits ordered timeline events without callbacks") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim","playback":{"fadeIn":0,"hold":1,"fadeOut":0}}],
      "events":[{"time":0,"name":"cast"},{"time":0.25,"name":"hit"},{"time":0.75,"name":"recover"}]
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto instance = std::move(created).takeValue();
    instance->play();
    auto events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "cast");
    instance->update(0.3f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "hit");
    instance->update(0.5f);
    events = instance->drainEvents();
    REQUIRE_EQ(events.size(), 1u);
    REQUIRE_EQ(events.front(), "recover");
}

TEST_CASE("stylize.mesh_vfx_asset serializes migrated assets in canonical schema") {
    auto legacy = MeshVfxAsset::fromJson(
        R"({"schemaVersion":0,"style":"aura","playback":{"hold":0.5},"events":[{"time":0.5,"name":"pulse"}]})");
    REQUIRE(legacy.ok());
    auto encoded = legacy.value().toJson();
    REQUIRE(encoded.ok());
    auto decoded = MeshVfxAsset::fromJson(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE_EQ(decoded.value().layers.front().style, "aura");
    REQUIRE_EQ(decoded.value().events.front().name, "pulse");
}

namespace {
class MeshVfxAttachmentSource final : public eve::IAttachmentPointSource {
public:
    eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(std::string_view     name,
                                                            eve::AttachmentPoint offset) const override {
        if (name == "weapon.root") return eve::Result<eve::AttachmentPoint>::success({offset.x, offset.y, offset.z});
        if (name == "weapon.tip")
            return eve::Result<eve::AttachmentPoint>::success({offset.x + 1.f, offset.y, offset.z});
        return eve::Result<eve::AttachmentPoint>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "missing test attachment"));
    }
};
}  // namespace

TEST_CASE("stylize.mesh_vfx_asset samples authored weapon attachments into its trail") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"slash"}],
      "trail":{"maxSamples":8,"lifetime":0.2,"minDistance":0,"teleportDistance":5,
               "rootAttachment":"weapon.root","tipAttachment":"weapon.tip",
               "rootOffset":[0,0.25,0],"tipOffset":[0,0.25,0]}
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto                    instance = std::move(created).takeValue();
    MeshVfxAttachmentSource source;
    auto                    sampled = instance->sampleTrail(source);
    REQUIRE(sampled.ok());
    REQUIRE(instance->trail().sampleCount() > 0u);
    auto encoded = parsed.value().toJson();
    REQUIRE(encoded.ok());
    REQUIRE(MeshVfxAsset::fromJson(encoded.value()).ok());
}

namespace {
class MeshVfxAnimationEvents final : public eve::IAnimationEventSource {
public:
    std::vector<std::string> names;
    std::size_t              animationEventCount() const noexcept override { return names.size(); }
    std::string              animationEventName(std::size_t index) const override {
        return index < names.size() ? names[index] : std::string{};
    }
};
}  // namespace

TEST_CASE("stylize.mesh_vfx_asset maps animation notifications to lifecycle actions") {
    auto parsed = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim"}],
      "trail":{"rootAttachment":"root","tipAttachment":"tip"},
      "animationTriggers":{"play":["attack.start"],"stop":["attack.end"],"trailBreak":["attack.cancel"]}
    })");
    REQUIRE(parsed.ok());
    auto created = MeshVfxAssetInstance::create(parsed.value());
    REQUIRE(created.ok());
    auto                   instance = std::move(created).takeValue();
    MeshVfxAnimationEvents events;
    events.names  = {"ignored", "attack.start"};
    auto dispatch = instance->processAnimationEvents(events);
    REQUIRE_EQ(dispatch.handled, 1u);
    REQUIRE_EQ(dispatch.played, 1u);
    events.names = {"attack.cancel", "attack.end"};
    dispatch     = instance->processAnimationEvents(events);
    REQUIRE_EQ(dispatch.trailBreaks, 1u);
    REQUIRE_EQ(dispatch.stopped, 1u);

    auto ambiguous = MeshVfxAsset::fromJson(R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}],
      "animationTriggers":{"play":["same"],"stop":["same"]}
    })");
    REQUIRE(!ambiguous.ok());
}

// stylize_mesh_vfx_batch.cpp
using namespace eve::stylize;

namespace {

[[maybe_unused]] MeshVfxLodDecision visible(std::uint64_t id, MeshVfxLodTier tier, std::uint32_t interval = 1) {
    MeshVfxLodDecision decision;
    decision.stableInstanceId   = id;
    decision.tier               = tier;
    decision.meshUpdateInterval = interval;
    decision.trailSampleStride  = interval;
    return decision;
}

[[maybe_unused]] MeshVfxBatchKey key(std::uint64_t material, MeshVfxBatchBlend blend) {
    return MeshVfxBatchKey{1, material, 3, 0, blend};
}

}  // namespace

TEST_CASE("stylize.mesh_vfx_batch filters LOD and groups additive state") {
    const std::array items{
        MeshVfxRenderItem{3, key(8, MeshVfxBatchBlend::Additive), 2.0f},
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 4.0f},
        MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 8.0f},
        MeshVfxRenderItem{4, key(9, MeshVfxBatchBlend::Additive), 1.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Full),
        visible(2, MeshVfxLodTier::Reduced),
        visible(3, MeshVfxLodTier::Minimal),
        visible(4, MeshVfxLodTier::Culled),
    };

    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
    REQUIRE_EQ(queue.stats.queuedDraws, 3u);
    REQUIRE_EQ(queue.stats.culledDraws, 1u);
    REQUIRE_EQ(queue.batches.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 1u);
    REQUIRE_EQ(queue.batches[0].draws[1].stableInstanceId, 2u);
}

TEST_CASE("stylize.mesh_vfx_batch preserves alpha back-to-front order") {
    const std::array items{
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Alpha), 2.0f},
        MeshVfxRenderItem{2, key(8, MeshVfxBatchBlend::Alpha), 9.0f},
        MeshVfxRenderItem{3, key(7, MeshVfxBatchBlend::Alpha), 5.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Full),
        visible(2, MeshVfxLodTier::Full),
        visible(3, MeshVfxLodTier::Full),
    };
    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 0);
    REQUIRE_EQ(queue.batches.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws.size(), 1u);
    REQUIRE_EQ(queue.batches[1].draws.size(), 2u);
    REQUIRE_EQ(queue.batches[0].draws[0].stableInstanceId, 2u);
    REQUIRE_EQ(queue.batches[1].draws[0].stableInstanceId, 3u);
    REQUIRE_EQ(queue.batches[1].draws[1].stableInstanceId, 1u);

    // Equal material keys must not merge across an intervening alpha draw.
    auto interleavedItems         = items;
    interleavedItems[2].viewDepth = 10.0f;
    const auto interleaved        = MeshVfxRenderBatchPlanner{}.build(interleavedItems, decisions, 0);
    REQUIRE_EQ(interleaved.batches.size(), 3u);
    for (const auto& batch : interleaved.batches) {
        REQUIRE_EQ(batch.draws.size(), 1u);
    }
    REQUIRE_EQ(interleaved.batches[0].draws[0].stableInstanceId, 3u);
    REQUIRE_EQ(interleaved.batches[1].draws[0].stableInstanceId, 2u);
    REQUIRE_EQ(interleaved.batches[2].draws[0].stableInstanceId, 1u);
}

TEST_CASE("stylize.mesh_vfx_batch staggers reduced mesh refreshes") {
    const std::array items{
        MeshVfxRenderItem{1, key(7, MeshVfxBatchBlend::Additive), 1.0f},
        MeshVfxRenderItem{2, key(7, MeshVfxBatchBlend::Additive), 1.0f},
        MeshVfxRenderItem{9, key(7, MeshVfxBatchBlend::Additive), 1.0f},
    };
    const std::array decisions{
        visible(1, MeshVfxLodTier::Reduced, 2),
        visible(2, MeshVfxLodTier::Reduced, 2),
    };
    const auto queue = MeshVfxRenderBatchPlanner{}.build(items, decisions, 4);
    REQUIRE_EQ(queue.stats.queuedDraws, 2u);
    REQUIRE_EQ(queue.stats.missingDecisions, 1u);
    REQUIRE_EQ(queue.stats.dynamicMeshRefreshes, 1u);
    REQUIRE_EQ(static_cast<int>(queue.batches[0].draws[0].meshUpdate), static_cast<int>(MeshVfxMeshUpdate::Reuse));
    REQUIRE_EQ(static_cast<int>(queue.batches[0].draws[1].meshUpdate), static_cast<int>(MeshVfxMeshUpdate::Refresh));
}
