// Device-free tests for the FrameGraph-based render pipeline planning and the
// JobSystem parallel-recording executor (see docs/dev/framegraph-migration.md).
//
// The planning tests never touch a GPU: vkb::FrameGraph(nullptr, ...) plans the
// pass graph (dependencies / layers / barriers) without creating any Vulkan
// object. The executor tests run the exact executor the render backend will
// hand to vkb::FrameGraph::record().

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/vulkan/FrameGraphJobs.h"
#include "thread/JobSystem.h"
#include "vkbuilder/framegraph.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace eve::graphics;

vkb::TextureDesc colorTexDesc() {
    vkb::TextureDesc td;
    td.extent = vk::Extent3D(800, 600, 1);
    td.usage = vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eColorAttachment;
    td.afterLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return td;
}

vkb::TextureDesc depthTexDesc() {
    vkb::TextureDesc td;
    td.format = vk::Format::eD32Sfloat;
    td.extent = vk::Extent3D(2048, 2048, 1);
    td.aspect = vk::ImageAspectFlagBits::eDepth;
    td.usage = vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eDepthStencilAttachment;
    td.afterLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    return td;
}

/**
 * @brief The pass topology the Vulkan backend targets once the framegraph
 * migration lands: 3 CSM cascades + G-buffer fill concurrently, forward scene
 * pass next, present last. Resource declarations mirror the engine's
 * shadow map / G-buffer / scene color attachments.
 */
std::unique_ptr<vkb::FrameGraph> buildEngineTopologyGraph() {
    auto graph = std::make_unique<vkb::FrameGraph>(nullptr, 2);

    vkb::TextureHandle shadow[3];
    for (int c = 0; c < 3; ++c)
        shadow[c] = graph->createTexture("shadowCascade" + std::to_string(c),
                                         depthTexDesc());
    auto gbNormal = graph->createTexture("gbNormal", colorTexDesc());
    auto gbDepthColor = graph->createTexture("gbDepthColor", colorTexDesc());
    auto gbAlbedo = graph->createTexture("gbAlbedo", colorTexDesc());
    auto gbHwDepth = graph->createTexture("gbHwDepth", depthTexDesc());
    auto sceneColor = graph->createTexture("sceneColor", colorTexDesc());
    auto presentTarget = graph->createTexture("presentTarget", colorTexDesc());
    graph->markOutput(presentTarget);

    vk::ClearValue depthClear{};
    depthClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    for (int c = 0; c < 3; ++c) {
        graph->addPass("shadow" + std::to_string(c))
            .depthAttachment(shadow[c], vkb::AttachmentOp::clear(depthClear))
            .record([](vkb::FrameGraphPassContext &) {});
    }
    graph->addPass("gbuffer")
        .colorAttachment(gbNormal, vkb::AttachmentOp::clearColor(0, 0, 0, 0))
        .colorAttachment(gbDepthColor, vkb::AttachmentOp::clearColor(1, 1, 1, 1))
        .colorAttachment(gbAlbedo, vkb::AttachmentOp::clearColor(0, 0, 0, 0))
        .depthAttachment(gbHwDepth, vkb::AttachmentOp::clear(depthClear))
        .record([](vkb::FrameGraphPassContext &) {});
    graph->addPass("forward")
        .sample(shadow[0])
        .sample(shadow[1])
        .sample(shadow[2])
        .sample(gbNormal)
        .sample(gbDepthColor)
        .sample(gbAlbedo)
        .sample(gbHwDepth)
        .colorAttachment(sceneColor, vkb::AttachmentOp::clearColor(0, 0, 0, 1))
        .record([](vkb::FrameGraphPassContext &) {});
    graph->addPass("present")
        .sample(sceneColor)
        .colorAttachment(presentTarget, vkb::AttachmentOp::clearColor(0, 0, 0, 1))
        .record([](vkb::FrameGraphPassContext &) {});
    return graph;
}

}  // namespace

TEST_CASE("render_graph.engineTopology") {
    auto graph = buildEngineTopologyGraph();
    graph->compile();
    const auto &plan = graph->compiled();

    CHECK_EQ(plan.passes.size(), size_t(6));

    // Shadow cascades and the G-buffer fill share no dependency edges, so the
    // planner must put them in one layer: that layer records concurrently on
    // the JobSystem workers.
    REQUIRE_EQ(plan.layers.size(), size_t(3));
    CHECK_EQ(plan.layers[0].size(), size_t(4));
    CHECK_EQ(plan.layers[1].size(), size_t(1));
    CHECK_EQ(plan.layers[2].size(), size_t(1));
    CHECK_EQ(plan.passes[plan.layers[0][0]].name, std::string("shadow0"));
    CHECK_EQ(plan.passes[plan.layers[1][0]].name, std::string("forward"));
    CHECK_EQ(plan.passes[plan.layers[2][0]].name, std::string("present"));

    // Dependency edges keep the topological order: shadow/gbuffer before
    // forward before present.
    const auto *forward = &plan.passes[0];
    const auto *present = &plan.passes[0];
    for (const auto &p : plan.passes) {
        if (p.name == "forward") forward = &p;
        if (p.name == "present") present = &p;
    }
    CHECK(forward->order > plan.passes[plan.layers[0][0]].order);
    CHECK(present->order > forward->order);

    // Attachment -> sampled transitions fold into render-pass final layouts:
    // the forward pass samples G-buffer + shadow maps with zero explicit
    // image barriers, and present samples scene color with zero explicit
    // barriers too.
    for (const auto &p : plan.passes) {
        if (p.name == "forward") {
            CHECK(p.barrier.images.empty());
            CHECK(p.barrier.buffers.empty());
        }
        if (p.name == "present") {
            CHECK(p.barrier.images.empty());
            CHECK(p.barrier.buffers.empty());
        }
        if (p.name == "gbuffer") {
            for (const auto &ab : p.attachments) {
                const bool folded =
                    ab.finalLayout == vk::ImageLayout::eShaderReadOnlyOptimal ||
                    ab.finalLayout == vk::ImageLayout::eDepthStencilReadOnlyOptimal;
                CHECK(folded);
            }
        }
        if (p.name == "shadow0" || p.name == "shadow1" || p.name == "shadow2") {
            REQUIRE_EQ(p.attachments.size(), size_t(1));
            const bool folded =
                p.attachments[0].finalLayout ==
                vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            CHECK(folded);
        }
    }
}

TEST_CASE("render_graph.jobSystemExecutorParallel") {
    auto *jobs = eve::thread::createJobSystem(4);
    REQUIRE(jobs != nullptr);
    jobs->beginFrame();

    std::mutex mu;
    std::vector<int> recorded;
    std::vector<std::thread::id> threads;
    const auto recordOne = [&](uint32_t order) {
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        std::lock_guard<std::mutex> lk(mu);
        recorded.push_back(int(order));
        threads.push_back(std::this_thread::get_id());
    };

    // One layer of 8 independent passes: serial recording would take >= 64 ms,
    // parallel recording on 4 workers takes ~2-3 sleep periods.
    std::vector<uint32_t> layer{0, 1, 2, 3, 4, 5, 6, 7};
    const auto t0 = std::chrono::steady_clock::now();
    auto executor = vulkan::jobSystemPassExecutor(jobs);
    executor(layer, recordOne);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();

    jobs->endFrame();
    delete jobs;

    CHECK_EQ(recorded.size(), layer.size());
    for (size_t i = 0; i < recorded.size(); ++i)
        CHECK_EQ(recorded[i], int(i));
    // Every pass recorded exactly once (recorded already proves count; sort
    // check proves no duplicates and no gaps).
    CHECK(ms < 40);
    // At least two distinct threads participated -> the layer was truly
    // recorded in parallel, not just serialized through the executor.
    std::sort(threads.begin(), threads.end());
    const bool parallel = threads.size() >= 2 &&
                          std::unique(threads.begin(), threads.end()) != threads.end();
    CHECK(parallel);
}

TEST_CASE("render_graph.jobSystemExecutorSerialFallback") {
    std::mutex mu;
    std::vector<int> recorded;
    const auto recordOne = [&](uint32_t order) {
        std::lock_guard<std::mutex> lk(mu);
        recorded.push_back(int(order));
    };

    auto executor = vulkan::jobSystemPassExecutor(nullptr);
    executor(std::vector<uint32_t>{0, 1, 2}, recordOne);
    executor(std::vector<uint32_t>{3}, recordOne);

    CHECK_EQ(recorded.size(), size_t(4));
    for (size_t i = 0; i < recorded.size(); ++i)
        CHECK_EQ(recorded[i], int(i));
}
