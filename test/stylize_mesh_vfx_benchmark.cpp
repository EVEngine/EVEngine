#include "stylize/MeshVfxRenderBatch.h"
#include "stylize/MeshVfxScalability.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_benchmark") {
    if (!std::getenv("EVENGINE_MESH_VFX_BENCHMARK"))
        return;

    constexpr std::size_t instanceCount = 50'000;
    constexpr int sampleCount = 12;
    std::mt19937 rng(0x4d565846u);
    std::uniform_real_distribution<float> distance(0.0f, 120.0f);
    std::uniform_real_distribution<float> pixels(2.0f, 180.0f);
    std::uniform_int_distribution<int> priority(-4, 12);

    std::vector<MeshVfxLodCandidate> candidates;
    std::vector<MeshVfxRenderItem> items;
    candidates.reserve(instanceCount);
    items.reserve(instanceCount);
    for (std::size_t index = 0; index < instanceCount; ++index) {
        const auto id = static_cast<std::uint64_t>(index + 1);
        const float d = distance(rng);
        candidates.push_back({id, d, pixels(rng), priority(rng), index % 19 != 0});
        MeshVfxBatchKey key;
        key.pipelineId = index % 3;
        key.materialId = index % 24;
        key.meshId = index % 64;
        key.renderPass = static_cast<std::uint32_t>(index % 2);
        key.blend = index % 5 == 0 ? MeshVfxBatchBlend::Alpha : MeshVfxBatchBlend::Additive;
        items.push_back({id, key, d});
    }

    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 20'000;
    MeshVfxScalabilityPlanner lodPlanner(policy);
    MeshVfxRenderBatchPlanner batchPlanner;
    std::vector<double> samples;
    samples.reserve(sampleCount);
    MeshVfxRenderQueue lastQueue;

    for (int sample = 0; sample < sampleCount; ++sample) {
        const auto start = std::chrono::steady_clock::now();
        const auto decisions = lodPlanner.plan(candidates);
        lastQueue = batchPlanner.build(items, decisions, static_cast<std::uint64_t>(sample));
        const auto end = std::chrono::steady_clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());
    const double medianMs = samples[samples.size() / 2];
    REQUIRE_EQ(lastQueue.stats.inputItems, static_cast<std::uint32_t>(instanceCount));
    REQUIRE(lastQueue.stats.queuedDraws > 0);
    REQUIRE(lastQueue.stats.culledDraws > 0);
    REQUIRE(lastQueue.stats.dynamicMeshRefreshes <= lastQueue.stats.queuedDraws);
    REQUIRE(lastQueue.batches.size() <= lastQueue.stats.queuedDraws);

    std::cout << "MESH_VFX_BENCHMARK_JSON={\"instances\":" << instanceCount
              << ",\"samples\":" << sampleCount << ",\"medianMs\":" << medianMs
              << ",\"queuedDraws\":" << lastQueue.stats.queuedDraws
              << ",\"batches\":" << lastQueue.batches.size()
              << ",\"culledDraws\":" << lastQueue.stats.culledDraws
              << ",\"meshRefreshes\":" << lastQueue.stats.dynamicMeshRefreshes << "}\n";
}
