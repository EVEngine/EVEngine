#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <glm/mat4x4.hpp>
#include <iostream>
#include <random>
#include <vector>
#include "Fixtures.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "stylize/MeshEffect.h"
#include "stylize/MeshEffectRenderer.h"
#include "stylize/MeshVfxBatchSubmission.h"
#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"
#include "stylize/TrailEffect.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// stylize_mesh_vfx_benchmark.cpp
using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_benchmark") {
    if (!std::getenv("EVENGINE_MESH_VFX_BENCHMARK")) return;

    constexpr std::size_t                 instanceCount = 50'000;
    constexpr int                         sampleCount   = 12;
    std::mt19937                          rng(0x4d565846u);
    std::uniform_real_distribution<float> distance(0.0f, 120.0f);
    std::uniform_real_distribution<float> pixels(2.0f, 180.0f);
    std::uniform_int_distribution<int>    priority(-4, 12);

    std::vector<MeshVfxLodCandidate> candidates;
    std::vector<MeshVfxRenderItem>   items;
    candidates.reserve(instanceCount);
    items.reserve(instanceCount);
    for (std::size_t index = 0; index < instanceCount; ++index) {
        const auto  id = static_cast<std::uint64_t>(index + 1);
        const float d  = distance(rng);
        candidates.push_back({id, d, pixels(rng), priority(rng), index % 19 != 0});
        MeshVfxBatchKey key;
        key.pipelineId = index % 3;
        key.materialId = index % 24;
        key.meshId     = index % 64;
        key.renderPass = static_cast<std::uint32_t>(index % 2);
        key.blend      = index % 5 == 0 ? MeshVfxBatchBlend::Alpha : MeshVfxBatchBlend::Additive;
        items.push_back({id, key, d});
    }

    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 20'000;
    MeshVfxScalabilityPlanner lodPlanner(policy);
    MeshVfxRenderBatchPlanner batchPlanner;
    std::vector<double>       samples;
    samples.reserve(sampleCount);
    MeshVfxRenderQueue lastQueue;

    for (int sample = 0; sample < sampleCount; ++sample) {
        const auto start     = std::chrono::steady_clock::now();
        const auto decisions = lodPlanner.plan(candidates);
        lastQueue            = batchPlanner.build(items, decisions, static_cast<std::uint64_t>(sample));
        const auto end       = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());
    const double medianMs = samples[samples.size() / 2];
    REQUIRE_EQ(lastQueue.stats.inputItems, static_cast<std::uint32_t>(instanceCount));
    REQUIRE(lastQueue.stats.queuedDraws > 0);
    REQUIRE(lastQueue.stats.culledDraws > 0);
    REQUIRE(lastQueue.stats.dynamicMeshRefreshes <= lastQueue.stats.queuedDraws);
    REQUIRE(lastQueue.batches.size() <= lastQueue.stats.queuedDraws);

    std::cout << "MESH_VFX_BENCHMARK_JSON={\"instances\":" << instanceCount << ",\"samples\":" << sampleCount
              << ",\"medianMs\":" << medianMs << ",\"queuedDraws\":" << lastQueue.stats.queuedDraws
              << ",\"batches\":" << lastQueue.batches.size() << ",\"culledDraws\":" << lastQueue.stats.culledDraws
              << ",\"meshRefreshes\":" << lastQueue.stats.dynamicMeshRefreshes << "}\n";
}

// stylize_mesh_vfx_refraction.cpp
using namespace eve::graphics;
using namespace eve::stylize;

TEST_CASE("stylize.meshVfx.sceneColorRefractionSamplesBoundTexture") {
    eve::window::Window *window   = nullptr;
    Graphics            *graphics = nullptr;
    openGfxWindow(window, graphics, 64, 64);
    REQUIRE(graphics != nullptr);

    const uint8_t greenPixel[4] = {0, 255, 0, 255};
    Texture      *sceneColor    = graphics->newTexture(1, 1, greenPixel);
    Canvas       *target        = graphics->newCanvas(64, 64);
    REQUIRE(sceneColor != nullptr);
    REQUIRE(target != nullptr);

    MeshEffectInstance effect("slash");
    effect.style().setFloat("refractionStrength", 1.f);
    effect.style().setFloat("flowWarp", 0.f);
    effect.style().setFloat("edgeDistortion", 0.f);
    effect.play();
    effect.update(0.1f);

    TrailMeshSnapshot ribbon;
    ribbon.vertices = {
        {{-0.8f, -0.8f, 0.f}, {0.f, 0.f}, 1.f},
        {{-0.8f, 0.8f, 0.f}, {0.f, 1.f}, 1.f},
        {{0.8f, -0.8f, 0.f}, {1.f, 0.f}, 1.f},
        {{0.8f, 0.8f, 0.f}, {1.f, 1.f}, 1.f},
    };
    ribbon.indices = {0, 1, 2, 2, 1, 3};

    MeshEffectRenderer renderer(*graphics);
    graphics->setMesh3DSceneColor(sceneColor);
    graphics->setMesh3DViewProj(glm::mat4(1.f));
    graphics->begin3DFrameToCanvas(target);
    auto submitted = renderer.submitTrail(effect, ribbon);
    REQUIRE(submitted.hasValue());
    REQUIRE(static_cast<int>(submitted.value()) == static_cast<int>(MeshEffectSubmitStatus::Drawn));
    graphics->end3DFrameToCanvas();
    graphics->setMesh3DSceneColor(nullptr);

    const Color center = target->getPixel(32, 32);
    REQUIRE(center.g > 0.05f);
    REQUIRE(center.g > center.r * 2.f);
    REQUIRE(center.g > center.b * 2.f);
    window->close();
}

// stylize_mesh_vfx_scalability.cpp
using namespace eve::stylize;


// stylize_mesh_vfx_submission.cpp
using namespace eve::stylize;

namespace {

class RecordingSink final : public IMeshVfxBatchSink {
public:
    MeshVfxSubmitStatus        beginResult = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus        drawResult  = MeshVfxSubmitStatus::Accepted;
    MeshVfxSubmitStatus        endResult   = MeshVfxSubmitStatus::Accepted;
    std::vector<std::uint64_t> draws;
    std::uint32_t              begins = 0;
    std::uint32_t              ends   = 0;

    MeshVfxSubmitStatus beginBatch(const MeshVfxBatchKey &) override {
        ++begins;
        return beginResult;
    }

    MeshVfxSubmitStatus submitDraw(const MeshVfxBatchedDraw &draw) override {
        draws.push_back(draw.stableInstanceId);
        return drawResult;
    }

    MeshVfxSubmitStatus endBatch() override {
        ++ends;
        return endResult;
    }
};

[[maybe_unused]] MeshVfxRenderQueue queueWithTwoDraws() {
    MeshVfxRenderQueue queue;
    MeshVfxRenderBatch batch;
    batch.draws.push_back(MeshVfxBatchedDraw{1, MeshVfxLodTier::Full, MeshVfxMeshUpdate::Refresh, 1});
    batch.draws.push_back(MeshVfxBatchedDraw{2, MeshVfxLodTier::Reduced, MeshVfxMeshUpdate::Reuse, 2});
    queue.batches.push_back(std::move(batch));
    return queue;
}

}  // namespace
