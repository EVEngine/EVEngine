#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/SkillMeshEffect.h"
#include "stylize/Stylize.h"
#include "stylize/TrailEffect.h"
#include "window/Window.h"

#include <cmath>

using eve::stylize::MeshEffectPlayback;
using eve::stylize::MeshEffectState;
using eve::stylize::MeshEffectTargetHandle;
using eve::stylize::Stylize;
using eve::stylize::SkillMeshEffectKind;
using eve::stylize::TrailAppendResult;
using eve::stylize::TrailEmitter;
using eve::stylize::TrailSettings;
using eve::stylize::prepareTrailUpload;

TEST_CASE("stylize.meshVfx.playbackAndBinding") {
    auto* module = Stylize::create();
    auto effect = module->createMeshEffect("ember");
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
        auto effect = module->createMeshEffect("vignette");
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

TEST_CASE("stylize.meshVfx.gpuRibbonSubmission") {
    auto* window = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 64;
    settings.height = 64;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    auto* stylize = Stylize::create();
    auto effect = stylize->createMeshEffect("slash");
    effect->setPlayback(MeshEffectPlayback{0.f, 1.f, 0.f, false});
    effect->play();
    auto renderer = stylize->createMeshEffectRenderer(*graphics);

    TrailEmitter trail(TrailSettings{8, 1.f, 0.f, 2.f});
    (void)trail.append({-0.8f, -0.25f, 0.f}, {-0.8f, 0.25f, 0.f});
    (void)trail.append({0.8f, -0.25f, 0.f}, {0.8f, 0.25f, 0.f});

    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->setMesh3DView(glm::mat4(1.f));
    graphics->setMesh3DCameraPos({0.f, 0.f, 2.f});
    auto* target = graphics->newCanvas(64, 64);
    REQUIRE(target != nullptr);
    graphics->begin3DFrameToCanvas(target);
    auto submitted = renderer->submitTrail(*effect, trail.buildMesh());
    REQUIRE(submitted.ok());
    CHECK(static_cast<int>(submitted.value()) ==
          static_cast<int>(eve::stylize::MeshEffectSubmitStatus::Drawn));
    graphics->end3DFrameToCanvas();

    auto descriptor = graphics->describeMesh(renderer->trailMesh());
    REQUIRE(descriptor.has_value());
    CHECK_EQ(descriptor->vertexCount, 4u);
    CHECK_EQ(descriptor->indexCount, 6u);
    const eve::graphics::Color center = target->getPixel(32, 32);
    const bool renderedColor = center.r > 0.05f || center.g > 0.05f || center.b > 0.05f;
    CHECK(renderedColor);
    window->close();
}

TEST_CASE("stylize.meshVfx.skillRecipesComposeRuntime") {
    auto* stylize = Stylize::create();
    auto slash = stylize->createSkillMeshEffect(SkillMeshEffectKind::WeaponSlash);
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

TEST_CASE("stylize.meshVfx.gpuBatchQueueSubmission") {
    auto* window = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width = 64;
    settings.height = 64;
    REQUIRE(window->setWindowSettings(settings));

    auto* stylize = Stylize::create();
    auto effect = stylize->createMeshEffect("slash");
    effect->setPlayback(MeshEffectPlayback{0.f, 1.f, 0.f, false});
    effect->play();
    auto renderer = stylize->createMeshEffectRenderer(*graphics);

    TrailEmitter trail(TrailSettings{8, 1.f, 0.f, 2.f});
    (void)trail.append({-0.8f, -0.25f, 0.f}, {-0.8f, 0.25f, 0.f});
    (void)trail.append({0.8f, -0.25f, 0.f}, {0.8f, 0.25f, 0.f});
    eve::stylize::MeshVfxRendererCommand command;
    command.stableInstanceId = 17;
    command.kind = eve::stylize::MeshVfxRendererCommand::Kind::Trail;
    command.effect = effect.get();
    command.trail = trail.buildMesh();

    eve::stylize::MeshVfxRenderQueue queue;
    eve::stylize::MeshVfxRenderBatch batch;
    batch.key.blend = eve::stylize::MeshVfxBatchBlend::Additive;
    batch.draws.push_back({17, eve::stylize::MeshVfxLodTier::Full,
                           eve::stylize::MeshVfxMeshUpdate::Refresh, 1});
    queue.batches.push_back(std::move(batch));

    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->setMesh3DView(glm::mat4(1.f));
    graphics->setMesh3DCameraPos({0.f, 0.f, 2.f});
    auto* target = graphics->newCanvas(64, 64);
    REQUIRE(target != nullptr);
    graphics->begin3DFrameToCanvas(target);
    const auto submitted = renderer->submitQueue(queue, std::span<const eve::stylize::MeshVfxRendererCommand>(&command, 1));
    REQUIRE(submitted.ok());
    CHECK(static_cast<int>(submitted.value().status) ==
          static_cast<int>(eve::stylize::MeshVfxQueueSubmitStatus::Complete));
    CHECK_EQ(submitted.value().acceptedBatches, 1u);
    CHECK_EQ(submitted.value().acceptedDraws, 1u);
    graphics->end3DFrameToCanvas();
    window->close();
}
