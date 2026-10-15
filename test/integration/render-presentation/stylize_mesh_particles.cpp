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


TEST_CASE("stylize.meshVfx.gpuRibbonSubmission") {
    auto* window   = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width    = 64;
    settings.height   = 64;
    settings.centered = true;
    REQUIRE(window->setWindowSettings(settings));

    auto* stylize = Stylize::create();
    auto  effect  = stylize->createMeshEffect("slash");
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
    CHECK(static_cast<int>(submitted.value()) == static_cast<int>(eve::stylize::MeshEffectSubmitStatus::Drawn));
    graphics->end3DFrameToCanvas();

    auto descriptor = graphics->describeMesh(renderer->trailMesh());
    REQUIRE(descriptor.has_value());
    CHECK_EQ(descriptor->vertexCount, 4u);
    CHECK_EQ(descriptor->indexCount, 6u);
    const eve::graphics::Color center        = target->getPixel(32, 32);
    const bool                 renderedColor = center.r > 0.05f || center.g > 0.05f || center.b > 0.05f;
    CHECK(renderedColor);
    window->close();
}


TEST_CASE("stylize.meshVfx.gpuBatchQueueSubmission") {
    auto* window   = eve::window::Window::create();
    auto* graphics = eve::graphics::Graphics::create();
    REQUIRE(window != nullptr);
    REQUIRE(graphics != nullptr);
    eve::window::WindowSettings settings;
    settings.width  = 64;
    settings.height = 64;
    REQUIRE(window->setWindowSettings(settings));

    auto* stylize = Stylize::create();
    auto  effect  = stylize->createMeshEffect("slash");
    effect->setPlayback(MeshEffectPlayback{0.f, 1.f, 0.f, false});
    effect->play();
    auto renderer = stylize->createMeshEffectRenderer(*graphics);

    TrailEmitter trail(TrailSettings{8, 1.f, 0.f, 2.f});
    (void)trail.append({-0.8f, -0.25f, 0.f}, {-0.8f, 0.25f, 0.f});
    (void)trail.append({0.8f, -0.25f, 0.f}, {0.8f, 0.25f, 0.f});
    eve::stylize::MeshVfxRendererCommand command;
    command.stableInstanceId = 17;
    command.kind             = eve::stylize::MeshVfxRendererCommand::Kind::Trail;
    command.effect           = effect.get();
    command.trail            = trail.buildMesh();

    eve::stylize::MeshVfxRenderQueue queue;
    eve::stylize::MeshVfxRenderBatch batch;
    batch.key.blend = eve::stylize::MeshVfxBatchBlend::Additive;
    batch.draws.push_back({17, eve::stylize::MeshVfxLodTier::Full, eve::stylize::MeshVfxMeshUpdate::Refresh, 1});
    queue.batches.push_back(std::move(batch));

    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->setMesh3DView(glm::mat4(1.f));
    graphics->setMesh3DCameraPos({0.f, 0.f, 2.f});
    auto* target = graphics->newCanvas(64, 64);
    REQUIRE(target != nullptr);
    graphics->begin3DFrameToCanvas(target);
    const auto submitted =
        renderer->submitQueue(queue, std::span<const eve::stylize::MeshVfxRendererCommand>(&command, 1));
    REQUIRE(submitted.ok());
    CHECK(static_cast<int>(submitted.value().status) ==
          static_cast<int>(eve::stylize::MeshVfxQueueSubmitStatus::Complete));
    CHECK_EQ(submitted.value().acceptedBatches, 1u);
    CHECK_EQ(submitted.value().acceptedDraws, 1u);
    graphics->end3DFrameToCanvas();
    window->close();
}

// stylize_mesh_vfx_asset.cpp
using namespace eve::stylize;


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
