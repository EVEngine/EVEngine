// Deferred shadow/G-buffer graph ownership and recording.
#include <array>
#include <memory>
#include <string>
#include "graphics/vulkan/FrameGraphJobs.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"
#include "thread/Thread.h"

namespace eve::graphics::vulkan {

void Graphics::resetDeferredFrameGraphs() {
    // Graph command pools/framebuffers borrow these targets. Retire every
    // submitted slot before destroying graphs, then destroy target images.
    if (device.instance) device->waitIdle();
    for (auto &graph : deferredFrameGraphs_) graph.reset();
    deferredGraphRecorded_     = false;
    deferredGraphRecordedSlot_ = 0;
}

vkb::FrameGraph *Graphics::currentDeferredFrameGraph() {
    if (deferredFrameGraphs_[0] == nullptr) return nullptr;
    return deferredFrameGraphs_[currentFrameSlot() % deferredFrameGraphs_.size()].get();
}

void Graphics::buildDeferredFrameGraphs() {
    // One FrameGraph per in-flight slot imports the engine-owned targets and
    // owns the deferred passes: the 3 CSM cascades (per-layer views of the
    // shadow array) + the G-buffer fill share one dependency-free layer, so the
    // JobSystem executor records all four command buffers concurrently (see
    // recordDeferredFrameGraph). The engine keeps image ownership so
    // renderEntityIdMask / readGBufferToImageData and the postFX wrappers are
    // unaffected. Each graph is only used on its slot's frames, so its command
    // buffer is reused two frames later — by then the present slot fence
    // guarantees the previous graph submit completed (same queue, submitted
    // before the present command buffer).
    const vk::Format depthFmt     = vk::Format::eD32Sfloat;
    const vk::Format colorFmt     = pickGBufferColorFormat(device);
    const uint32_t   mapSize      = uint32_t(ShadowConfig::kMapSize);
    const uint32_t   shadowLayers = uint32_t(ShadowConfig::kCascades);
    const uint32_t   w            = gbufferWidth > 0 ? uint32_t(gbufferWidth) : 1u;
    const uint32_t   h            = gbufferHeight > 0 ? uint32_t(gbufferHeight) : 1u;

    for (size_t i = 0; i < deferredFrameGraphs_.size(); ++i) {
        auto graph = std::make_unique<vkb::FrameGraph>(&device, 1);

        vkb::TextureDesc shadowDesc;
        shadowDesc.format      = depthFmt;
        shadowDesc.extent      = vk::Extent3D{mapSize, mapSize, 1};
        shadowDesc.arrayLayers = shadowLayers;
        shadowDesc.aspect      = vk::ImageAspectFlagBits::eDepth;
        shadowDesc.usage       = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment;
        // Shadow descriptors sample the depth array in SHADER_READ_ONLY, matching
        // the legacy shadow pass and the layout of every imported cascade.
        shadowDesc.afterLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        vk::ClearValue shadowClear{};
        shadowClear.depthStencil  = vk::ClearDepthStencilValue{1.0f, 0};
        const bool haveShadowSlot = i < shadowMaps.size() && shadowMaps[i].image.layerCount() >= shadowLayers;
        if (haveShadowSlot) {
            const vk::Image shadowImage = shadowMaps[i].image.image();
            for (uint32_t c = 0; c < shadowLayers; ++c) {
                auto shadowH = graph->importTexture("shadowCascade" + std::to_string(c), shadowImage,
                                                    shadowMaps[i].image.layerView(c), shadowDesc);
                graph->addPass("shadow" + std::to_string(c))
                    .depthAttachment(shadowH, vkb::AttachmentOp::clear(shadowClear))
                    .record([this, c](vkb::FrameGraphPassContext &ctx) { recordShadowCascadePass(ctx, int(c)); });
            }
        }

        if (i < gbufferSlots.size() && gbufferWidth > 0 && gbufferHeight > 0) {
            auto            &slot = gbufferSlots[i];
            vkb::TextureDesc colorDesc;
            colorDesc.format = colorFmt;
            colorDesc.extent = vk::Extent3D{w, h, 1};
            colorDesc.usage  = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eTransferSrc;
            colorDesc.afterLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            auto normalH = graph->importTexture("gbNormal", slot.normal.image(), slot.normal.imageView(), colorDesc);
            auto depthColorH =
                graph->importTexture("gbDepthColor", slot.depthColor.image(), slot.depthColor.imageView(), colorDesc);
            auto albedoH = graph->importTexture("gbAlbedo", slot.albedo.image(), slot.albedo.imageView(), colorDesc);

            vkb::TextureDesc depthDesc;
            depthDesc.format      = depthFmt;
            depthDesc.extent      = vk::Extent3D{w, h, 1};
            depthDesc.aspect      = vk::ImageAspectFlagBits::eDepth;
            depthDesc.usage       = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment;
            depthDesc.afterLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            auto depthH = graph->importTexture("gbHwDepth", slot.depth.image(), slot.depth.imageView(), depthDesc);

            std::array<vk::ClearValue, 4> clears{};
            clears[0].color        = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
            clears[1].color        = vk::ClearColorValue(std::array<float, 4>{1, 1, 1, 1});
            clears[2].color        = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
            clears[3].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
            graph->addPass("gbuffer")
                .colorAttachment(normalH, vkb::AttachmentOp::clear(clears[0]))
                .colorAttachment(depthColorH, vkb::AttachmentOp::clear(clears[1]))
                .colorAttachment(albedoH, vkb::AttachmentOp::clear(clears[2]))
                .depthAttachment(depthH, vkb::AttachmentOp::clear(clears[3]))
                .record([this](vkb::FrameGraphPassContext &ctx) { recordGBufferPassDraws(ctx); });
        }
        graph->compile();
        deferredFrameGraphs_[i] = std::move(graph);
    }
}

void Graphics::recordShadowCascadePass(vkb::FrameGraphPassContext &ctx, int cascade) {
    // Runs inside the FrameGraph's "shadow<cascade>" render-pass instance
    // (already begun with a depth clear); only draw commands go here. The pass
    // may be recorded on a JobSystem worker, so everything below must be
    // read-only: shadowCascadeDraws was captured by endShadowPass on the main
    // thread before the graph records.
    auto              &cb     = ctx.commandBuffer();
    const vk::Extent2D extent = ctx.extent();
    const uint32_t     size   = extent.width ? extent.width : uint32_t(ShadowConfig::kMapSize);
    setViewportAndScissor(cb, size, size);
    vk::Pipeline boundPipeline{};
    for (const auto &d : shadowCascadeDraws[cascade]) {
        if (!d.mesh || !d.mesh->gpuHandle) continue;
        const bool   wantAlpha = d.alphaTest && shadowAlphaPipeline;
        const bool   skinned   = d.skinSet && d.mesh->hasGpuSkinning();
        vk::Pipeline wanted    = skinned ? (wantAlpha ? shadowSkinAlphaPipeline : shadowSkinPipeline)
                                         : (wantAlpha ? shadowAlphaPipeline : shadowPipeline);
        if (wanted != boundPipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, wanted);
            boundPipeline = wanted;
        }
        auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
        if (skinned) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skinPassPipelineLayout, 0, 1, &d.skinSet, 1,
                                  &d.skinUboOffset);
        } else if (wantAlpha) {
            Texture *alb = d.albedo ? d.albedo : whiteTexture;
            if (alb && alb->gpuHandle && texSetLayout) {
                auto *gpuTex = static_cast<GpuTexture *>(alb->gpuHandle);
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shadowAlphaPipelineLayout, 0, 1,
                                      gpuTex->descriptorSet.ptr(), 0, nullptr);
            }
        }
        if (!skinned)
            cb.pushConstants(wantAlpha ? shadowAlphaPipelineLayout : shadowPipelineLayout,
                             vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4), &d.mvp);
        drawIndexedMesh(cb, *gpuMesh);
    }
}

void Graphics::recordGBufferPassDraws(vkb::FrameGraphPassContext &ctx) {
    // Runs inside the FrameGraph's "gbuffer" render-pass instance (already
    // begun with the planned clear values); only draw commands go here. The
    // pass may be recorded on a JobSystem worker, so everything below must be
    // read-only: gbufferPassDraws was captured on the main thread.
    auto              &cb     = ctx.commandBuffer();
    const vk::Extent2D extent = ctx.extent();
    const uint32_t     w      = extent.width ? extent.width : uint32_t(gbufferWidth);
    const uint32_t     h      = extent.height ? extent.height : uint32_t(gbufferHeight);
    setViewportAndScissor(cb, w, h);
    vk::Pipeline boundPipeline{};
    for (const auto &d : gbufferPassDraws) {
        if (!d.mesh || !d.mesh->gpuHandle) continue;
        const bool   wantAlpha = d.alphaTest && gbufferAlphaPipeline;
        const bool   skinned   = d.skinSet && d.mesh->hasGpuSkinning();
        vk::Pipeline wanted    = skinned ? (wantAlpha ? gbufferSkinAlphaPipeline : gbufferSkinPipeline)
                                         : (wantAlpha ? gbufferAlphaPipeline : gbufferPipeline);
        if (wanted != boundPipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, wanted);
            boundPipeline = wanted;
        }
        auto    *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
        Texture *alb     = d.albedo ? d.albedo : whiteTexture;
        if (skinned) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skinPassPipelineLayout, 0, 1, &d.skinSet, 1,
                                  &d.skinUboOffset);
        } else if (alb && alb->gpuHandle && texSetLayout) {
            auto *gpuTex = static_cast<GpuTexture *>(alb->gpuHandle);
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, gbufferPipelineLayout, 0, 1,
                                  gpuTex->descriptorSet.ptr(), 0, nullptr);
        }
        if (!skinned)
            cb.pushConstants(gbufferPipelineLayout,
                             vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                             sizeof(GBufferPush), &d.push);
        drawIndexedMesh(cb, *gpuMesh);
    }
}

void Graphics::recordDeferredFrameGraph() {
    const size_t slot = currentFrameSlot();
    if (deferredGraphRecorded_ && deferredGraphRecordedSlot_ == slot) {
        // render3D can be called several times per script frame (e.g. the
        // render tests call it 3x before present). The deferred graph's command
        // buffers must only be recorded once per slot per frame — re-recording
        // them while the previous submit is still in flight would reset
        // in-use command buffers (UB, GPU hang).
        return;
    }
    auto *graph = currentDeferredFrameGraph();
    if (!graph && (!shadowMaps.empty() || !gbufferSlots.empty())) {
        // Shadows can be enabled without a G-buffer pass (or vice versa); build
        // the deferred graphs on demand from whatever targets exist today.
        buildDeferredFrameGraphs();
        graph = currentDeferredFrameGraph();
    }
    if (!graph || !gbufferPipeline || !gbufferRenderPass || !shadowPipeline) {
        dropPendingOffscreenPasses();
        return;
    }
    // Record the declarative deferred passes (3 CSM cascades + G-buffer, one
    // independent layer) with the JobSystem executor — the four command
    // buffers are recorded concurrently on workers — then submit them on the
    // graphics queue before the swapchain pass begins. Layout transitions and
    // the render-pass instances are planned by the FrameGraph. Same-queue
    // submission order plus the present slot fence (waited in Present::begin)
    // keep this slot's graph command buffers safe to reuse two frames later.
    auto *jobs = thread::Thread::create()->getJobSystem();
    jobs->beginFrame();  // idempotent wait; recycles the per-frame arena
    // Re-plan every frame (cheap; device objects are cached) so the graph is
    // in the compiled phase for this record cycle — vkb::FrameGraph enforces
    // build -> compile -> record -> submit and record() exactly once per
    // compile.
    graph->compile();
    // Parallel executor: each pass owns a dedicated command pool (one pool per
    // frame slot per pass), so the workers never share a pool while recording
    // concurrently — the Vulkan external-synchronization rule for command
    // pools is satisfied structurally.
    recordFrameGraphWithJobSystem(*graph, jobs);
    graph->submit();
    if (!shadowMaps.empty()) currentShadowMap().image.setCurrentLayout(vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    jobs->endFrame();
    for (auto &d : shadowCascadeDraws) d.clear();
    gbufferPassDraws.clear();
    shadowPendingMask          = 0;
    gbufferPending             = false;
    deferredGraphRecorded_     = true;
    deferredGraphRecordedSlot_ = slot;
}

}  // namespace eve::graphics::vulkan
