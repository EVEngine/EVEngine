// Vulkan backend implementation — GPU-driven rendering (stages 0-3).
//
// Bindless resources, GPU tables, per-frame arena, CPU/GPU indirect draws,
// HZB + frustum cull chain, visibility-buffer pass, fullscreen resolve and
// VirtualGeometry hardware-raster integration. Kept in its own TU (matching
// dev's split-graphics-backend layout) so the feature stays reviewable.

#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"
#include "graphics/vulkan/Canvas.h"
#include "common/Assert.h"
#include "graphics/IndirectBuilder.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/Shadow.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "zeroerr/assert.h"

#include <glm/gtc/matrix_transform.hpp>

#include "graphics/shaders/gpu_emit_comp_spv.inc"
#include "graphics/shaders/vg_main_cull_comp_spv.inc"
#include "graphics/shaders/hzb_build_comp_spv.inc"
#include "graphics/shaders/gpu_cull_comp_spv.inc"
#include "graphics/shaders/mesh3d_gpudriven_vert_spv.inc"
#include "graphics/shaders/mesh3d_gpudriven_frag_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_vis_vert_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_vis_frag_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_vgvis_vert_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_vgvis_frag_spv.inc"
#include "graphics/shaders/resolve_vis_vert_spv.inc"
#include "graphics/shaders/resolve_vis_frag_spv.inc"

namespace eve::graphics::vulkan {

bool Graphics::gpuDrivenResolveWanted() const {
    return gpuDrivenEnabled_ && gpuDrivenCaps_.gpuDrivenCullAvailable() &&
           renderControl_ && renderControl_->isEnabled("visResolve") &&
           sceneColorSamples == vk::SampleCountFlagBits::e1;
}

vk::DescriptorSet Graphics::bindlessSetForFrame() const {
    if (bindlessSets_.empty()) return nullptr;
    return bindlessSets_[currentFrameSlot() % bindlessSets_.size()];
}

void Graphics::initGpuDrivenResources() {
    if (!gpuDrivenCaps_.gpuDrivenAvailable()) return;
    frameArenas_.resize(frameSlotCount());
    for (auto &arena : frameArenas_) {
        arena.ensure(device, 8u << 20,
                     vk::BufferUsageFlagBits::eStorageBuffer |
                         vk::BufferUsageFlagBits::eIndirectBuffer |
                         vk::BufferUsageFlagBits::eTransferDst);
    }
    createBindlessSet();
    createMesh3DGpuDrivenPipeline();
}

void Graphics::createGpuDrivenVisResources(int width, int height) {
    if (!gpuDrivenCaps_.gpuDrivenAvailable()) return;
    if (gbufferVisRenderPass || gbufferSlots.empty()) return;
    const uint32_t w = uint32_t(width);
    const uint32_t h = uint32_t(height);
    const vk::Format colorFmt = pickGBufferColorFormat(device);
    const vk::Format depthFmt = vk::Format::eD32Sfloat;
    const vk::Format visIDFmt = vk::Format::eR32G32Uint;
    const vk::Format visBaryFmt = vk::Format::eR16G16Sfloat;

    gbufferVisRenderPass =
        device.createRenderPass()
            .addSampledColorAttachment(colorFmt)
            .addSampledColorAttachment(colorFmt)
            .addSampledColorAttachment(colorFmt)
            .addSampledColorAttachment(visIDFmt)
            .addSampledColorAttachment(visBaryFmt)
            .addSampledDepthAttachment(depthFmt)
            .addSubpass(vkb::SubpassBuilder()
                            .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(1, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(2, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(3, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(4, vk::ImageLayout::eColorAttachmentOptimal)
                            .setDepthStencilAttachment(
                                5, vk::ImageLayout::eDepthStencilAttachmentOptimal))
            .addExternalShaderReadDependencies()
            .build();
    for (auto &slot : gbufferSlots) {
        slot.visFramebuffer = gbufferVisRenderPass.createFramebuffer(
            device, w, h,
            {slot.normal.asAttachment(), slot.depthColor.asAttachment(), slot.albedo.asAttachment(),
             slot.visID.asAttachment(), slot.visBary.asAttachment(), slot.depth.asAttachment()});
    }

    if (!mesh3dGpuDrivenPipelineLayout) return;
    // Instance vis pass: no vertex input (fetches from the pooled buffers via
    // gl_VertexIndex); draws non-indexed with the cull chain's NI commands.
    {
        std::vector<uint32_t> visVert(mesh3d_gbuffer_vis_vert_spv,
                                      mesh3d_gbuffer_vis_vert_spv +
                                          mesh3d_gbuffer_vis_vert_spv_count);
        std::vector<uint32_t> visFrag(mesh3d_gbuffer_vis_frag_spv,
                                      mesh3d_gbuffer_vis_frag_spv +
                                          mesh3d_gbuffer_vis_frag_spv_count);
        vk::ShaderModule visVertModule =
            vkb::PipelineBuilder::createShaderModule(device.instance, visVert);
        vk::ShaderModule visFragModule =
            vkb::PipelineBuilder::createShaderModule(device.instance, visFrag);
        gbufferVisPipeline =
            device.createPipeline()
                .useClassicPipeline(visVertModule, visFragModule)
                .setPipelineLayout(mesh3dGpuDrivenPipelineLayout)
                .setDynamicStatesViewportScissor()
                .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                               vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise)
                .setDepthStencil(true, true, vk::CompareOp::eLess)
                .setColorAttachmentCount(5)
                .build(gbufferVisRenderPass);
        device->destroyShaderModule(visVertModule);
        device->destroyShaderModule(visFragModule);
    }
    // VG variant: same vis attachments, cluster-stream fetch (no vertex input).
    {
        std::vector<uint32_t> vgVisVert(mesh3d_gbuffer_vgvis_vert_spv,
                                        mesh3d_gbuffer_vgvis_vert_spv +
                                            mesh3d_gbuffer_vgvis_vert_spv_count);
        std::vector<uint32_t> vgVisFrag(mesh3d_gbuffer_vgvis_frag_spv,
                                        mesh3d_gbuffer_vgvis_frag_spv +
                                            mesh3d_gbuffer_vgvis_frag_spv_count);
        vk::ShaderModule vgVisVertModule =
            vkb::PipelineBuilder::createShaderModule(device.instance, vgVisVert);
        vk::ShaderModule vgVisFragModule =
            vkb::PipelineBuilder::createShaderModule(device.instance, vgVisFrag);
        gbufferVgVisPipeline =
            device.createPipeline()
                .useClassicPipeline(vgVisVertModule, vgVisFragModule)
                .setPipelineLayout(mesh3dGpuDrivenPipelineLayout)
                .setDynamicStatesViewportScissor()
                .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                               vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise)
                .setDepthStencil(true, true, vk::CompareOp::eLess)
                .setColorAttachmentCount(5)
                .build(gbufferVisRenderPass);
        device->destroyShaderModule(vgVisVertModule);
        device->destroyShaderModule(vgVisFragModule);
    }
}

// ---- GPU-driven (stage 0): bindless set + per-frame arena + tables ----

FrameArena &Graphics::currentFrameArena() {
    if (frameArenas_.empty()) {
        static FrameArena fallback;  // never used for recording; guards odd call order
        return fallback;
    }
    return frameArenas_[currentFrameSlot() % frameArenas_.size()];
}

void Graphics::createBindlessSet() {
    if (!gpuDrivenCaps_.gpuDrivenAvailable()) return;
    if (!whiteTexture || !whiteTexture->gpuHandle) return;
    if (!defaultBindlessCube || !defaultBindlessCube->gpuHandle) return;
    auto *white = static_cast<GpuTexture *>(whiteTexture->gpuHandle);
    auto *whiteCube = static_cast<GpuTexture *>(defaultBindlessCube->gpuHandle);

    auto samplerBinding = [](uint32_t binding, uint32_t count, vk::ShaderStageFlags stages) {
        vk::DescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        b.descriptorCount = count;
        b.stageFlags = stages;
        return b;
    };
    auto storageBinding = [](uint32_t binding, vk::ShaderStageFlags stages) {
        vk::DescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = vk::DescriptorType::eStorageBuffer;
        b.descriptorCount = 1;
        b.stageFlags = stages;
        return b;
    };
    auto uniformBinding = [](uint32_t binding, vk::ShaderStageFlags stages) {
        vk::DescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = vk::DescriptorType::eUniformBuffer;
        b.descriptorCount = 1;
        b.stageFlags = stages;
        return b;
    };
    const vk::ShaderStageFlags allStages = vk::ShaderStageFlagBits::eVertex |
                                           vk::ShaderStageFlagBits::eFragment |
                                           vk::ShaderStageFlagBits::eCompute;
    const vk::ShaderStageFlags computeOnly = vk::ShaderStageFlagBits::eCompute;
    const vk::ShaderStageFlags vertFrag =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    std::vector<vk::DescriptorSetLayoutBinding> bindings{
        samplerBinding(0, kMaxBindlessTextures, allStages),
        samplerBinding(1, kMaxBindlessCubemaps, allStages),
        storageBinding(2, allStages),
        storageBinding(3, allStages),
        storageBinding(4, allStages),
        storageBinding(5, allStages),  // unused by shaders; kept valid
        uniformBinding(6, computeOnly),
        storageBinding(7, computeOnly),
        storageBinding(8, computeOnly),
        storageBinding(9, computeOnly),
        storageBinding(10, computeOnly),
        samplerBinding(11, 1, computeOnly),
        storageBinding(12, computeOnly),
        storageBinding(13, computeOnly),
        storageBinding(14, computeOnly),
        samplerBinding(15, 1, vk::ShaderStageFlagBits::eFragment),  // visID (resolve)
        samplerBinding(16, 1, vk::ShaderStageFlagBits::eFragment),  // visBary (resolve)
        storageBinding(17, computeOnly),
        storageBinding(18, vertFrag),  // pooled vertex positions
        storageBinding(19, vertFrag),  // pooled vertex normals
        storageBinding(20, vertFrag),  // pooled vertex uvs
        storageBinding(21, vertFrag),  // pooled indices
        storageBinding(22, allStages),  // VG position stream (float xyz)
        storageBinding(23, allStages),  // VG triangle stream (u32)
        storageBinding(24, allStages),  // VG cluster table (uvec4 x 4)
        storageBinding(25, allStages),  // VG cluster -> asset id
        storageBinding(26, allStages),  // VG visible list (per slot)
        storageBinding(27, allStages),  // VG indirect commands (per slot)
        storageBinding(28, vk::ShaderStageFlagBits::eFragment),  // VG asset -> material
        storageBinding(29, vk::ShaderStageFlagBits::eVertex |
                               vk::ShaderStageFlagBits::eFragment |
                               vk::ShaderStageFlagBits::eCompute),  // VG asset models
        storageBinding(30, computeOnly),  // non-indexed indirect commands (vis pass)
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = uint32_t(bindings.size());
    layoutInfo.pBindings = bindings.data();
    bindlessSetLayoutUnique_ = device->createDescriptorSetLayoutUnique(layoutInfo);
    bindlessSetLayout_ = *bindlessSetLayoutUnique_;

    // kAsyncResourceCopies sets: one per frame-in-flight slot, so frame N+1 can
    // rewrite its own set while frame N's pending command buffers still hold
    // the previous contents of the other set (no UPDATE_AFTER_BIND required).
    const uint32_t setCount = kAsyncResourceCopies;
    std::array<vk::DescriptorPoolSize, 3> poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler,
                               (kMaxBindlessTextures + kMaxBindlessCubemaps + 3) * setCount},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 25 * setCount},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1 * setCount},
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = uint32_t(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    bindlessPool_ = device->createDescriptorPool(poolInfo);

    {
        std::vector<vk::DescriptorSetLayout> layouts(setCount, bindlessSetLayout_);
        vk::DescriptorSetAllocateInfo alloc{};
        alloc.descriptorPool = bindlessPool_;
        alloc.descriptorSetCount = setCount;
        alloc.pSetLayouts = layouts.data();
        bindlessSets_ = device->allocateDescriptorSets(alloc);
    }

    bindlessTextures2D_.assign(kMaxBindlessTextures, white);
    bindlessCubemaps_.assign(kMaxBindlessCubemaps, whiteCube);
    bindlessFree2D_.clear();
    bindlessFreeCube_.clear();
    bindlessFree2D_.reserve(kMaxBindlessTextures);
    bindlessFreeCube_.reserve(kMaxBindlessCubemaps);
    for (uint32_t i = 0; i < kMaxBindlessTextures; ++i) bindlessFree2D_.push_back(i);
    for (uint32_t i = 0; i < kMaxBindlessCubemaps; ++i) bindlessFreeCube_.push_back(i);

    vk::DescriptorImageInfo white2D{white->sampler, white->imageView(),
                                    vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo whiteCubeInfo{whiteCube->sampler, whiteCube->cubeImage.imageView(),
                                          vk::ImageLayout::eShaderReadOnlyOptimal};
    // Placeholder fill: single-element writes keep the per-call update tiny;
    // ~1.1k single-element calls per set at init cost ~6 ms, which is fine.
    constexpr uint32_t kDescriptorChunk = 1;
    auto chunkFill = [&](vk::DescriptorSet set, uint32_t binding, uint32_t total,
                         const vk::DescriptorImageInfo &info) {
        for (uint32_t base = 0; base < total; base += kDescriptorChunk) {
            vk::WriteDescriptorSet w{};
            w.dstSet = set;
            w.dstBinding = binding;
            w.dstArrayElement = base;
            w.descriptorCount = std::min(kDescriptorChunk, total - base);
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.pImageInfo = &info;
            device->updateDescriptorSets(1, &w, 0, nullptr);
        }
    };
    for (vk::DescriptorSet set : bindlessSets_) {
        chunkFill(set, 0, kMaxBindlessTextures, white2D);
        chunkFill(set, 1, kMaxBindlessCubemaps, whiteCubeInfo);
    }

    // GPU resource tables (fixed capacity at startup; entries registered lazily).
    constexpr uint32_t kMaxMeshRecords = 4096;
    constexpr uint32_t kMaxMaterialRecords = 1024;
    meshTableBuffer_ = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                          kMaxMeshRecords * sizeof(GpuMeshRecord),
                                          kHostVisibleCoherent);
    meshTableCapacity_ = kMaxMeshRecords;
    meshTableRecords_.reserve(kMaxMeshRecords);
    materialTableBuffer_ = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                              kMaxMaterialRecords * sizeof(GpuMaterialRecord),
                                              kHostVisibleCoherent);
    materialTableCapacity_ = kMaxMaterialRecords;
    materialTableRecords_.reserve(kMaxMaterialRecords);

    // Bind the resource tables into the bindless set (bindings 2-5). Binding 4
    // (per-frame instances) is rewritten before each frame; these writes make
    // every binding valid so the set is safe to bind at any time.
    auto tableWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::Buffer buffer) {
        vk::DescriptorBufferInfo info{buffer, 0, VK_WHOLE_SIZE};
        vk::WriteDescriptorSet w{};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = vk::DescriptorType::eStorageBuffer;
        w.pBufferInfo = &info;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    };
    for (vk::DescriptorSet set : bindlessSets_) {
        tableWrite(set, 2, meshTableBuffer_.buffer);
        tableWrite(set, 3, materialTableBuffer_.buffer);
        tableWrite(set, 4, meshTableBuffer_.buffer);  // placeholder; rewritten per frame
        tableWrite(set, 5, meshTableBuffer_.buffer);  // unused by shaders
    }
    // Stage 2 placeholders: every binding must be valid before the set binds.
    gpuDrivenCullParamsPlaceholder_ =
        vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eUniformBuffer, 256,
                           kHostVisibleCoherent);
    {
        vk::DescriptorBufferInfo ubo{gpuDrivenCullParamsPlaceholder_.buffer, 0, 256};
        for (vk::DescriptorSet set : bindlessSets_) {
            vk::WriteDescriptorSet w{};
            w.dstSet = set;
            w.dstBinding = 6;
            w.descriptorCount = 1;
            w.descriptorType = vk::DescriptorType::eUniformBuffer;
            w.pBufferInfo = &ubo;
            device->updateDescriptorSets(1, &w, 0, nullptr);
        }
    }
    for (uint32_t b : {7u, 8u, 9u, 10u, 12u, 13u, 14u, 17u})
        for (vk::DescriptorSet set : bindlessSets_) tableWrite(set, b, meshTableBuffer_.buffer);
    {
        vk::DescriptorImageInfo depthInfo{white->sampler, white->imageView(),
                                          vk::ImageLayout::eShaderReadOnlyOptimal};
        for (vk::DescriptorSet set : bindlessSets_) {
            vk::WriteDescriptorSet w{};
            w.dstSet = set;
            w.dstBinding = 11;
            w.descriptorCount = 1;
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.pImageInfo = &depthInfo;
            device->updateDescriptorSets(1, &w, 0, nullptr);
        }
    }
    // Stage 3 placeholders: visID/visBary are rewritten per frame by the
    // resolve; keep them valid so the set is safe to bind anywhere.
    for (vk::DescriptorSet set : bindlessSets_) {
        vk::DescriptorImageInfo visInfo{white->sampler, white->imageView(),
                                        vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::WriteDescriptorSet w{};
        w.dstSet = set;
        w.dstBinding = 15;
        w.descriptorCount = 1;
        w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        w.pImageInfo = &visInfo;
        device->updateDescriptorSets(1, &w, 0, nullptr);
        w.dstBinding = 16;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    }
    // Pooled vertex/index buffers for the vis resolve (grows lazily).
    ensureGpuVertexPool();
    bindGpuVertexPoolBindless();
    // Shared virtual-geometry cluster pool (grows lazily on upload).
    ensureVgBuffers();
    bindVgPoolBindless();
}

void Graphics::ensureGpuVertexPool() {
    if (gpuVertexPool_.positions.buffer) return;
    constexpr uint32_t kInitialVertices = 256u << 10;   // 256k verts (~11 MB total)
    constexpr uint32_t kInitialIndices = 768u << 10;    // 768k u32 indices (3 MB)
    const auto hostMem = kHostVisibleCoherent;
    gpuVertexPool_.positions = vkb::GenericBuffer(
        device, vk::BufferUsageFlagBits::eStorageBuffer, kInitialVertices * sizeof(glm::vec4),
        hostMem);
    gpuVertexPool_.normals = vkb::GenericBuffer(
        device, vk::BufferUsageFlagBits::eStorageBuffer, kInitialVertices * sizeof(glm::vec4),
        hostMem);
    gpuVertexPool_.uvs = vkb::GenericBuffer(
        device, vk::BufferUsageFlagBits::eStorageBuffer, kInitialVertices * sizeof(glm::vec2),
        hostMem);
    gpuVertexPool_.indices = vkb::GenericBuffer(
        device, vk::BufferUsageFlagBits::eStorageBuffer, kInitialIndices * sizeof(uint32_t),
        hostMem);
    gpuVertexPool_.vertexCount = 0;
    gpuVertexPool_.indexCount = 0;
}

void Graphics::growGpuVertexPool(uint32_t needVertices, uint32_t needIndices) {
    // Pool growth reallocates the buffers; a pending frame may still be reading
    // them, so drain the GPU first (rare path: first-time mesh registration).
    device->waitIdle();
    const uint32_t oldVerts = gpuVertexPool_.vertexCount;
    const uint32_t oldInds = gpuVertexPool_.indexCount;
    const auto hostMem = kHostVisibleCoherent;
    auto copyInto = [&](vkb::GenericBuffer &dst, vkb::GenericBuffer &src, vk::DeviceSize oldBytes,
                        vk::DeviceSize newBytes) {
        vkb::GenericBuffer grown(device, vk::BufferUsageFlagBits::eStorageBuffer, newBytes,
                                 hostMem);
        if (oldBytes > 0) {
            void *srcMap = src.map();
            void *dstMap = grown.map();
            std::memcpy(dstMap, srcMap, oldBytes);
            grown.unmap();
            src.unmap();
        }
        src.release();
        dst = std::move(grown);
    };
    const uint32_t newVerts = std::max(needVertices, oldVerts * 2u);
    const uint32_t newInds = std::max(needIndices, oldInds * 2u);
    copyInto(gpuVertexPool_.positions, gpuVertexPool_.positions,
             vk::DeviceSize(oldVerts) * sizeof(glm::vec4),
             vk::DeviceSize(newVerts) * sizeof(glm::vec4));
    copyInto(gpuVertexPool_.normals, gpuVertexPool_.normals,
             vk::DeviceSize(oldVerts) * sizeof(glm::vec4),
             vk::DeviceSize(newVerts) * sizeof(glm::vec4));
    copyInto(gpuVertexPool_.uvs, gpuVertexPool_.uvs,
             vk::DeviceSize(oldVerts) * sizeof(glm::vec2),
             vk::DeviceSize(newVerts) * sizeof(glm::vec2));
    copyInto(gpuVertexPool_.indices, gpuVertexPool_.indices,
             vk::DeviceSize(oldInds) * sizeof(uint32_t),
             vk::DeviceSize(newInds) * sizeof(uint32_t));
    bindGpuVertexPoolBindless();
}

void Graphics::bindGpuVertexPoolBindless() {
    if (bindlessSets_.empty() || !gpuVertexPool_.positions.buffer) return;
    auto bufWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::Buffer buffer,
                        vk::DeviceSize size) {
        vk::DescriptorBufferInfo info{buffer, 0, size};
        vk::WriteDescriptorSet w{};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = vk::DescriptorType::eStorageBuffer;
        w.pBufferInfo = &info;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    };
    const uint32_t cap = [&]() {
        const vk::DeviceSize bytes = gpuVertexPool_.positions.size;
        return uint32_t(bytes / sizeof(glm::vec4));
    }();
    const uint32_t indCap = uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t));
    for (vk::DescriptorSet set : bindlessSets_) {
        bufWrite(set, 18, gpuVertexPool_.positions.buffer,
                 vk::DeviceSize(cap) * sizeof(glm::vec4));
        bufWrite(set, 19, gpuVertexPool_.normals.buffer, vk::DeviceSize(cap) * sizeof(glm::vec4));
        bufWrite(set, 20, gpuVertexPool_.uvs.buffer, vk::DeviceSize(cap) * sizeof(glm::vec2));
        bufWrite(set, 21, gpuVertexPool_.indices.buffer,
                 vk::DeviceSize(indCap) * sizeof(uint32_t));
    }
}

void Graphics::appendGpuMeshToPool(GpuMesh &gpu) {
    if (!gpu.vertices.buffer || !gpu.indices.buffer) return;
    ensureGpuVertexPool();
    const uint32_t nVerts = gpu.record.vertexCount;
    const uint32_t nInds = gpu.record.indexCount;
    if (nVerts == 0 || nInds == 0) return;
    const uint32_t newVerts = gpuVertexPool_.vertexCount + nVerts;
    const uint32_t newInds = gpuVertexPool_.indexCount + nInds;
    const uint32_t capVerts =
        uint32_t(gpuVertexPool_.positions.size / sizeof(glm::vec4));
    const uint32_t capInds = uint32_t(gpuVertexPool_.indices.size / sizeof(uint32_t));
    if (newVerts > capVerts || newInds > capInds) {
        growGpuVertexPool(newVerts, newInds);
    }

    void *vMap = gpu.vertices.map();
    void *iMap = gpu.indices.map();
    if (!vMap || !iMap) return;
    auto *verts = static_cast<const MeshVertex *>(vMap);
    auto *posDst = static_cast<glm::vec4 *>(gpuVertexPool_.positions.map());
    auto *nrmDst = static_cast<glm::vec4 *>(gpuVertexPool_.normals.map());
    auto *uvDst = static_cast<glm::vec2 *>(gpuVertexPool_.uvs.map());
    auto *idxDst = static_cast<uint32_t *>(gpuVertexPool_.indices.map());
    if (!posDst || !nrmDst || !uvDst || !idxDst) {
        gpu.vertices.unmap();
        gpu.indices.unmap();
        return;
    }
    posDst += gpuVertexPool_.vertexCount;
    nrmDst += gpuVertexPool_.vertexCount;
    uvDst += gpuVertexPool_.vertexCount;
    idxDst += gpuVertexPool_.indexCount;
    for (uint32_t i = 0; i < nVerts; ++i) {
        posDst[i] = glm::vec4(verts[i].pos, 0.f);
        nrmDst[i] = glm::vec4(verts[i].normal, 0.f);
        uvDst[i] = verts[i].uv;
    }
    if (gpu.indexType == vk::IndexType::eUint16) {
        const auto *src16 = static_cast<const uint16_t *>(iMap);
        for (uint32_t i = 0; i < nInds; ++i) idxDst[i] = uint32_t(src16[i]);
    } else {
        const auto *src32 = static_cast<const uint32_t *>(iMap);
        for (uint32_t i = 0; i < nInds; ++i) idxDst[i] = src32[i];
    }
    gpuVertexPool_.positions.unmap();
    gpuVertexPool_.normals.unmap();
    gpuVertexPool_.uvs.unmap();
    gpuVertexPool_.indices.unmap();
    gpu.vertices.unmap();
    gpu.indices.unmap();

    // Pool offsets are vertex/index counts, resolved by the vis shaders.
    gpu.record.vertexOffset = gpuVertexPool_.vertexCount;
    gpu.record.indexOffset = gpuVertexPool_.indexCount;
    gpu.record.firstIndex = 0;  // vis pass draws non-indexed from the pool
    gpu.record.vertexBase = 0;
    gpuVertexPool_.vertexCount = newVerts;
    gpuVertexPool_.indexCount = newInds;
}

uint32_t Graphics::registerBindlessTexture2D(GpuTexture *tex) {
    if (!tex || bindlessSets_.empty()) return kInvalidBindlessSlot;
    if (tex->bindlessIndex2D != kInvalidBindlessSlot) return tex->bindlessIndex2D;
    if (bindlessFree2D_.empty()) return kInvalidBindlessSlot;
    const uint32_t slot = bindlessFree2D_.front();
    bindlessFree2D_.erase(bindlessFree2D_.begin());
    bindlessTextures2D_[slot] = tex;
    tex->bindlessIndex2D = slot;
    vk::DescriptorImageInfo img{tex->sampler, tex->imageView(),
                                vk::ImageLayout::eShaderReadOnlyOptimal};
    for (vk::DescriptorSet set : bindlessSets_) {
        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = 0;
        write.dstArrayElement = slot;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &img;
        device->updateDescriptorSets(1, &write, 0, nullptr);
    }
    return slot;
}

uint32_t Graphics::registerBindlessTextureCube(GpuTexture *tex) {
    if (!tex || bindlessSets_.empty()) return kInvalidBindlessSlot;
    if (tex->bindlessIndexCube != kInvalidBindlessSlot) return tex->bindlessIndexCube;
    if (bindlessFreeCube_.empty()) return kInvalidBindlessSlot;
    const uint32_t slot = bindlessFreeCube_.front();
    bindlessFreeCube_.erase(bindlessFreeCube_.begin());
    bindlessCubemaps_[slot] = tex;
    tex->bindlessIndexCube = slot;
    vk::DescriptorImageInfo img{tex->sampler, tex->imageView(),
                                vk::ImageLayout::eShaderReadOnlyOptimal};
    for (vk::DescriptorSet set : bindlessSets_) {
        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = 1;
        write.dstArrayElement = slot;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &img;
        device->updateDescriptorSets(1, &write, 0, nullptr);
    }
    return slot;
}

uint32_t Graphics::gpuDrivenReflectionProbeSlot(Texture *cubemap) {
    if (!cubemap || !cubemap->gpuHandle) return kInvalidGpuDrivenSlot;
    auto *gpu = static_cast<GpuTexture *>(cubemap->gpuHandle);
    if (!gpu->isCube) return kInvalidGpuDrivenSlot;
    return registerBindlessTextureCube(gpu);
}

void Graphics::unregisterBindlessTexture(GpuTexture *tex) {
    if (!tex || bindlessSets_.empty()) return;
    auto *white = static_cast<GpuTexture *>(whiteTexture->gpuHandle);
    GpuTexture *whiteCube = white;
    if (defaultBindlessCube && defaultBindlessCube->gpuHandle)
        whiteCube = static_cast<GpuTexture *>(defaultBindlessCube->gpuHandle);

    auto restore = [&](uint32_t binding, uint32_t slot, GpuTexture *placeholder,
                       bool cube) {
        vk::DescriptorImageInfo img{
            placeholder->sampler,
            cube ? placeholder->cubeImage.imageView() : placeholder->imageView(),
            vk::ImageLayout::eShaderReadOnlyOptimal};
        for (vk::DescriptorSet set : bindlessSets_) {
            vk::WriteDescriptorSet write{};
            write.dstSet = set;
            write.dstBinding = binding;
            write.dstArrayElement = slot;
            write.descriptorCount = 1;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.pImageInfo = &img;
            device->updateDescriptorSets(1, &write, 0, nullptr);
        }
    };

    if (tex->bindlessIndex2D != kInvalidBindlessSlot) {
        const uint32_t slot = tex->bindlessIndex2D;
        bindlessTextures2D_[slot] = white;
        bindlessFree2D_.push_back(slot);
        restore(0, slot, white, false);
        tex->bindlessIndex2D = kInvalidBindlessSlot;
    }
    if (tex->bindlessIndexCube != kInvalidBindlessSlot) {
        const uint32_t slot = tex->bindlessIndexCube;
        bindlessCubemaps_[slot] = whiteCube;
        bindlessFreeCube_.push_back(slot);
        restore(1, slot, whiteCube, true);
        tex->bindlessIndexCube = kInvalidBindlessSlot;
    }
}

void Graphics::ensureVgBuffers() {
    if (vgGpu_.positions.buffer) return;
    const auto hostMem = kHostVisibleCoherent;
    constexpr uint32_t kInitClusters = 16384;
    constexpr uint32_t kInitVertFloats = 1u << 20;   // ~1M floats (~4MB)
    constexpr uint32_t kInitTriangles = 2u << 20;    // ~2M u32 indices
    vgGpu_.positions = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                          kInitVertFloats * sizeof(float), hostMem);
    vgGpu_.triangles = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                          kInitTriangles * sizeof(uint32_t), hostMem);
    vgGpu_.clusters = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                         kInitClusters * sizeof(GpuVgCluster), hostMem);
    vgGpu_.clusterAssets =
        vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                           kInitClusters * sizeof(uint32_t), hostMem);
    constexpr uint32_t kVisBytes = (kMaxVgClusters + 1) * sizeof(uint32_t);
    constexpr uint32_t kIndBytes = kMaxVgClusters * sizeof(glm::uvec4);
    vgGpu_.visible = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer |
                                                    vk::BufferUsageFlagBits::eIndirectBuffer,
                                        kVisBytes * kAsyncResourceCopies, hostMem);
    vgGpu_.indirect = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer |
                                                     vk::BufferUsageFlagBits::eIndirectBuffer,
                                         kIndBytes * kAsyncResourceCopies, hostMem);
    vgGpu_.assetMaterials =
        vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                           kMaxVgAssets * sizeof(uint32_t) * kAsyncResourceCopies, hostMem);
    vgGpu_.assetModels =
        vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                           kMaxVgAssets * sizeof(glm::mat4) * kAsyncResourceCopies, hostMem);
    {
        void *m = vgGpu_.visible.map();
        std::memset(m, 0, kVisBytes * kAsyncResourceCopies);
        vgGpu_.visible.unmap();
        m = vgGpu_.assetMaterials.map();
        std::memset(m, 0, kMaxVgAssets * sizeof(uint32_t) * kAsyncResourceCopies);
        vgGpu_.assetMaterials.unmap();
        m = vgGpu_.assetModels.map();
        for (size_t s = 0; s < kAsyncResourceCopies; ++s)
            for (uint32_t a = 0; a < kMaxVgAssets; ++a)
                static_cast<glm::mat4 *>(m)[s * kMaxVgAssets + a] = glm::mat4(1.f);
        vgGpu_.assetModels.unmap();
    }
    bindVgPoolBindless();
}

void Graphics::growVgBuffers(uint32_t needClusters, uint32_t needVertices,
                             uint32_t needTriangles) {
    device->waitIdle();
    const auto hostMem = kHostVisibleCoherent;
    auto growBuffer = [&](vkb::GenericBuffer &dst, uint32_t elementSize, uint32_t oldElems,
                          uint32_t newElems) {
        if (newElems <= oldElems) return;
        vkb::GenericBuffer grown(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                 vk::DeviceSize(newElems) * elementSize, hostMem);
        if (oldElems > 0) {
            void *srcMap = dst.map();
            void *dstMap = grown.map();
            std::memcpy(dstMap, srcMap, vk::DeviceSize(oldElems) * elementSize);
            grown.unmap();
            dst.unmap();
        }
        dst.release();
        dst = std::move(grown);
    };
    const uint32_t curClusters = uint32_t(vgGpu_.clusters.size / sizeof(GpuVgCluster));
    const uint32_t curVertFloats = uint32_t(vgGpu_.positions.size / sizeof(float));
    const uint32_t curTriangles = uint32_t(vgGpu_.triangles.size / sizeof(uint32_t));
    growBuffer(vgGpu_.clusters, uint32_t(sizeof(GpuVgCluster)), curClusters,
               std::max(needClusters, curClusters * 2u));
    growBuffer(vgGpu_.clusterAssets, uint32_t(sizeof(uint32_t)), curClusters,
               std::max(needClusters, curClusters * 2u));
    growBuffer(vgGpu_.positions, uint32_t(sizeof(float)), curVertFloats,
               std::max(needVertices, curVertFloats * 2u));
    growBuffer(vgGpu_.triangles, uint32_t(sizeof(uint32_t)), curTriangles,
               std::max(needTriangles, curTriangles * 2u));
    bindVgPoolBindless();
}

void Graphics::bindVgPoolBindless() {
    if (bindlessSets_.empty() || !vgGpu_.positions.buffer) return;
    auto bufWrite = [&](vk::DescriptorSet set, uint32_t binding, vk::Buffer buffer,
                        vk::DeviceSize offset, vk::DeviceSize size) {
        vk::DescriptorBufferInfo info{buffer, offset, size};
        vk::WriteDescriptorSet w{};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = vk::DescriptorType::eStorageBuffer;
        w.pBufferInfo = &info;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    };
    const vk::DeviceSize posBytes = vgGpu_.positions.size;
    const vk::DeviceSize triBytes = vgGpu_.triangles.size;
    const vk::DeviceSize clBytes = vgGpu_.clusters.size;
    const vk::DeviceSize claBytes = vgGpu_.clusterAssets.size;
    for (vk::DescriptorSet set : bindlessSets_) {
        bufWrite(set, 22, vgGpu_.positions.buffer, 0, posBytes);
        bufWrite(set, 23, vgGpu_.triangles.buffer, 0, triBytes);
        bufWrite(set, 24, vgGpu_.clusters.buffer, 0, clBytes);
        bufWrite(set, 25, vgGpu_.clusterAssets.buffer, 0, claBytes);
        bufWrite(set, 28, vgGpu_.assetMaterials.buffer, 0, vgGpu_.assetMaterials.size);
        bufWrite(set, 29, vgGpu_.assetModels.buffer, 0, vgGpu_.assetModels.size);
        bufWrite(set, 26, vgGpu_.visible.buffer, 0, vgGpu_.visible.size);
        bufWrite(set, 27, vgGpu_.indirect.buffer, 0, vgGpu_.indirect.size);
    }
}

void Graphics::bindVgFrameBindless(vk::DescriptorSet bindless, size_t slot) {
    if (!bindless || !vgGpu_.visible.buffer) return;
    const vk::DeviceSize slotVis = (vk::DeviceSize(kMaxVgClusters) + 1) * sizeof(uint32_t);
    const vk::DeviceSize slotInd = vk::DeviceSize(kMaxVgClusters) * sizeof(glm::uvec4);
    const vk::DeviceSize slotMat = vk::DeviceSize(kMaxVgAssets) * sizeof(uint32_t);
    const vk::DeviceSize slotModel = vk::DeviceSize(kMaxVgAssets) * sizeof(glm::mat4);
    const vk::DeviceSize visOff = slotVis * slot;
    const vk::DeviceSize indOff = slotInd * slot;
    const vk::DeviceSize matOff = slotMat * slot;
    const vk::DeviceSize modelOff = slotModel * slot;
    vk::DescriptorBufferInfo infos[4]{
        {vgGpu_.visible.buffer, visOff, slotVis},
        {vgGpu_.indirect.buffer, indOff, slotInd},
        {vgGpu_.assetMaterials.buffer, matOff, slotMat},
        {vgGpu_.assetModels.buffer, modelOff, slotModel},
    };
    vk::WriteDescriptorSet w[4]{};
    const uint32_t bindings[4] = {26, 27, 28, 29};
    for (int i = 0; i < 4; ++i) {
        w[i].dstSet = bindless;
        w[i].dstBinding = bindings[i];
        w[i].descriptorCount = 1;
        w[i].descriptorType = vk::DescriptorType::eStorageBuffer;
        w[i].pBufferInfo = &infos[i];
    }
    device->updateDescriptorSets(4, w, 0, nullptr);
}

uint32_t Graphics::gpuDrivenVgUpload(const GpuVgAssetUpload &asset) {
    // The compacted cluster command stream needs an indirect-count draw. Keep
    // ordinary GPU-driven meshes available on devices without that optional
    // Vulkan 1.2 feature, but reject VG explicitly instead of accepting an
    // asset that can never be rasterized.
    if (!gpuDrivenCaps_.drawIndirectCount) return kInvalidBindlessSlot;
    if (!asset.positions || asset.vertexCount <= 0 || !asset.triangles ||
        asset.triangleCount <= 0 || !asset.clusters || asset.clusterCount <= 0)
        return kInvalidBindlessSlot;
    if (vgAssetCount_ >= kMaxVgAssets) return kInvalidBindlessSlot;
    ensureVgBuffers();

    const uint32_t assetId = vgAssetCount_;
    const uint32_t vertBase = vgVertexCount_;   // global vertex index base
    const uint32_t triBase = vgTriangleCount_;  // global index-stream base
    const uint32_t clusterBase = vgClusterCount_;
    const uint32_t needClusters = clusterBase + uint32_t(asset.clusterCount);
    const uint32_t needVerts = uint32_t(vertBase + uint32_t(asset.vertexCount)) * 3;
    const uint32_t needTris = triBase + uint32_t(asset.triangleCount);
    const uint32_t curClusters = uint32_t(vgGpu_.clusters.size / sizeof(GpuVgCluster));
    const uint32_t curVerts = uint32_t(vgGpu_.positions.size / sizeof(float));
    const uint32_t curTris = uint32_t(vgGpu_.triangles.size / sizeof(uint32_t));
    if (needClusters > curClusters || needVerts > curVerts || needTris > curTris)
        growVgBuffers(needClusters, needVerts, needTris);

    void *posMap = vgGpu_.positions.map();
    void *triMap = vgGpu_.triangles.map();
    void *clMap = vgGpu_.clusters.map();
    void *claMap = vgGpu_.clusterAssets.map();
    if (!posMap || !triMap || !clMap || !claMap) {
        if (posMap) vgGpu_.positions.unmap();
        if (triMap) vgGpu_.triangles.unmap();
        if (clMap) vgGpu_.clusters.unmap();
        if (claMap) vgGpu_.clusterAssets.unmap();
        return kInvalidBindlessSlot;
    }
    std::memcpy(static_cast<char *>(posMap) + size_t(vertBase) * 3 * sizeof(float), asset.positions,
                size_t(asset.vertexCount) * 3 * sizeof(float));
    {
        auto *dst = static_cast<uint32_t *>(triMap) + triBase;
        for (int i = 0; i < asset.triangleCount; ++i) dst[i] = asset.triangles[i] + vertBase;
    }
    std::memcpy(static_cast<char *>(clMap) + vgClusterCount_ * sizeof(GpuVgCluster),
                asset.clusters, size_t(asset.clusterCount) * sizeof(GpuVgCluster));
    {
        auto *dst = static_cast<GpuVgCluster *>(clMap) + vgClusterCount_;
        for (int i = 0; i < asset.clusterCount; ++i) {
            // Cluster triStart is a triangle ordinal; vgTriangleCount_ tracks
            // uint32 indices. The vertex/resolve shaders multiply triStart by
            // three, so convert the index offset before rebasing the cluster.
            dst[i].u1[0] += triBase / 3u;
        }
    }
    auto *cla = static_cast<uint32_t *>(claMap);
    for (int i = 0; i < asset.clusterCount; ++i) cla[vgClusterCount_ + uint32_t(i)] = assetId;
    vgGpu_.positions.unmap();
    vgGpu_.triangles.unmap();
    vgGpu_.clusters.unmap();
    vgGpu_.clusterAssets.unmap();

    vgClusterCount_ = needClusters;
    vgVertexCount_ = vertBase + uint32_t(asset.vertexCount);
    vgTriangleCount_ = needTris;
    vgAssetCount_ = assetId + 1;
    // Default identity model per slot; vgSetInstance overwrites the current slot.
    {
        void *m = vgGpu_.assetModels.map();
        auto *models = static_cast<glm::mat4 *>(m);
        for (size_t s = 0; s < kAsyncResourceCopies; ++s)
            models[s * kMaxVgAssets + assetId] = glm::mat4(1.f);
        vgGpu_.assetModels.unmap();
    }
    return assetId;
}

uint32_t Graphics::gpuDrivenVgAssetId(Mesh *mesh) const {
    if (!mesh || !mesh->gpuHandle) return kInvalidBindlessSlot;
    const auto *gpu = static_cast<const GpuMesh *>(mesh->gpuHandle);
    return gpu->record.vgAssetId;
}

bool Graphics::gpuDrivenVgAttachToMesh(Mesh *mesh, uint32_t vgAssetId) {
    if (!mesh || !mesh->gpuHandle) return false;
    if (vgAssetId >= vgAssetCount_ || vgAssetId >= kMaxVgAssets) return false;
    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    gpu->record.vgAssetId = vgAssetId;
    if (gpu->gpuRecordIndex != kInvalidBindlessSlot &&
        gpu->gpuRecordIndex < meshTableRecords_.size()) {
        meshTableRecords_[gpu->gpuRecordIndex].vgAssetId = vgAssetId;
        syncMeshTable();
    }
    return true;
}

bool Graphics::gpuDrivenVgSetInstance(uint32_t vgAssetId, const glm::mat4 &model,
                                      uint32_t materialId) {
    if (vgAssetId >= vgAssetCount_ || vgAssetId >= kMaxVgAssets) return false;
    if (vgGpu_.assetModels.buffer) {
        const size_t slot = currentFrameSlot() % kAsyncResourceCopies;
        void *m = vgGpu_.assetModels.map();
        static_cast<glm::mat4 *>(m)[slot * kMaxVgAssets + vgAssetId] = model;
        vgGpu_.assetModels.unmap();
        void *am = vgGpu_.assetMaterials.map();
        static_cast<uint32_t *>(am)[slot * kMaxVgAssets + vgAssetId] = materialId;
        vgGpu_.assetMaterials.unmap();
    }
    vgAnyThisFrame_ = true;
    return true;
}

uint32_t Graphics::debugGpuDrivenVgVisibleCount() const {
    if (!vgGpu_.visible.buffer || vgAssetCount_ == 0) return 0;
    void *map = vgGpu_.visible.map();
    if (!map) return 0;
    const size_t slot = vgLastVisible_ % kAsyncResourceCopies;
    const uint32_t count = static_cast<const uint32_t *>(map)[slot * (kMaxVgClusters + 1)];
    vgGpu_.visible.unmap();
    return count;
}

uint32_t Graphics::registerMeshRecord(GpuMesh *gpu) {
    if (!gpu) return kInvalidBindlessSlot;
    if (gpu->gpuRecordIndex != kInvalidBindlessSlot) return gpu->gpuRecordIndex;
    if (meshTableRecords_.size() >= meshTableCapacity_) return kInvalidBindlessSlot;
    // dev's mesh factories do not populate GpuMeshRecord; build it on first
    // registration from the host-visible buffers (bounds + ranges).
    if (gpu->record.vertexCount == 0 && gpu->vertices.buffer) {
        const uint32_t vertexCount = uint32_t(gpu->vertices.size / sizeof(MeshVertex));
        gpu->record.vertexCount = vertexCount;
        gpu->record.indexCount = gpu->indexCount;
        gpu->record.indexType = gpu->indexType == vk::IndexType::eUint16 ? 0u : 1u;
        void *map = gpu->vertices.map();
        if (map && vertexCount > 0) {
            const auto *verts = static_cast<const MeshVertex *>(map);
            glm::vec3 minv(1e30f), maxv(-1e30f);
            for (uint32_t i = 0; i < vertexCount; ++i) {
                minv = glm::min(minv, verts[i].pos);
                maxv = glm::max(maxv, verts[i].pos);
            }
            const glm::vec3 center = (minv + maxv) * 0.5f;
            float radius = 0.f;
            for (uint32_t i = 0; i < vertexCount; ++i)
                radius = std::max(radius, glm::length(verts[i].pos - center));
            gpu->record.boundsCenterRadius = glm::vec4(center, radius);
        }
        if (map) gpu->vertices.unmap();
    }
    // Stage 3: lazily pool the mesh's vertices/indices so the vis resolve can
    // fetch attributes by (pool offset + triangle + barycentric).
    appendGpuMeshToPool(*gpu);
    const uint32_t idx = uint32_t(meshTableRecords_.size());
    meshTableRecords_.push_back(gpu->record);
    meshRecordOwners_.push_back(gpu);
    gpu->gpuRecordIndex = idx;
    syncMeshTable();
    return idx;
}

void Graphics::syncMeshTable() {
    if (!meshTableBuffer_.buffer || meshTableRecords_.empty()) return;
    meshTableBuffer_.updateLocal(vkb::FrameSlot::gpuIdle(), meshTableRecords_.data(),
                                 meshTableRecords_.size() * sizeof(GpuMeshRecord));
}

GpuMaterialRecord Graphics::buildMaterialRecord(Material *material) {
    GpuMaterialRecord rec{};
    if (!material) return rec;
    rec.tint = glm::vec4(material->getTintR(), material->getTintG(), material->getTintB(),
                         material->getTintA());
    rec.pbr = glm::vec4(material->getMetallic(), material->getRoughness(),
                        material->getReceiveShadow() ? 1.f : 0.f,
                        material->getReceiveLight() ? 1.f : 0.f);
    rec.texBomb = glm::vec4(material->getTexCellBombScale(), material->getTexCellBombStrength(),
                            material->getTexCellBombRotation(), 0.f);
    rec.parallax = glm::vec4(material->getParallaxScale(), material->getParallaxMinLayers(),
                             material->getParallaxMaxLayers(), 0.f);
    auto slotOf = [](Texture *t) {
        if (!t || !t->gpuHandle) return kInvalidBindlessSlot;
        return static_cast<GpuTexture *>(t->gpuHandle)->bindlessIndex2D;
    };
    rec.textureSlots[0] = slotOf(material->getAlbedoTexture());
    rec.textureSlots[1] = slotOf(material->getNormalTexture());
    rec.textureSlots[2] = slotOf(material->getHeightTexture());
    rec.textureSlots[3] = kInvalidBindlessSlot;  // env is camera state, not material state
    const std::string model = material->getShadingModel();
    rec.shadingModel = (model == "unlit") ? 1u : (model == "hair") ? 2u
                        : (model == "custom")                     ? 3u
                                                                  : 0u;
    if (material->getCastShadow()) rec.flags |= 1u;
    if (material->getCastOcclusion()) rec.flags |= 2u;
    return rec;
}

uint32_t Graphics::materialTableGetOrCreate(Material *material) {
    if (!material) return kInvalidBindlessSlot;
    auto it = materialTableIndex_.find(material);
    if (it != materialTableIndex_.end()) {
        materialTableRecords_[it->second] = buildMaterialRecord(material);
        syncMaterialTable();
        return it->second;
    }
    if (materialTableRecords_.size() >= materialTableCapacity_) return kInvalidBindlessSlot;
    const uint32_t idx = uint32_t(materialTableRecords_.size());
    materialTableIndex_.emplace(material, idx);
    materialTableRecords_.push_back(buildMaterialRecord(material));
    syncMaterialTable();
    return idx;
}

void Graphics::syncMaterialTable() {
    if (!materialTableBuffer_.buffer || materialTableRecords_.empty()) return;
    materialTableBuffer_.updateLocal(vkb::FrameSlot::gpuIdle(), materialTableRecords_.data(),
                                     materialTableRecords_.size() *
                                         sizeof(GpuMaterialRecord));
}

uint32_t Graphics::gpuDrivenMeshRecord(Mesh *mesh) {
    if (!mesh || !mesh->gpuHandle) return kInvalidBindlessSlot;
    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    if (gpu->gpuRecordIndex == kInvalidBindlessSlot) registerMeshRecord(gpu);
    return gpu->gpuRecordIndex;
}

bool Graphics::gpuDrivenMaterialUsable(Material *material) {
    if (!material ||
        material->virtualTextureMode() == MaterialVirtualTextureMode::AtlasPageTable)
        return false;
    const uint32_t id = materialTableGetOrCreate(material);
    // Any material with a GPU table record is representable by the bindless
    // path; descriptor-array indexing handles arbitrary slots.
    return id != kInvalidBindlessSlot;
}

bool Graphics::gpuDrivenSubmitOpaque(const GpuInstance *instances, uint32_t instanceCount) {
    if (!gpuDrivenEnabled() || !gpuDrivenCaps_.gpuDrivenAvailable()) return false;
    initGpuDrivenResources();
    if (!mesh3dGpuDrivenPipeline || bindlessSets_.empty() || !meshTableBuffer_.buffer) return false;
    if (!instances || instanceCount == 0) return false;

    // Sort by (material, mesh) and merge buckets using the GPU mesh table.
    eve::graphics::IndirectBuilder builder;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        builder.add(i, instances[i].meshId, instances[i].materialId, 0);
    }
    const uint32_t drawCount = builder.build(meshTableRecords_);
    if (drawCount == 0) return false;
    lastGpuDrivenDrawCount_ = drawCount;
    const auto &cmds = builder.commands();
    const auto &order = builder.sortedInstanceOrder();

    // Upload instances in the sorted order so each bucket is a contiguous range.
    std::vector<GpuInstance> sorted(instanceCount);
    for (uint32_t i = 0; i < instanceCount; ++i) sorted[i] = instances[order[i]];

    auto &arena = currentFrameArena();
    FrameArena::Alloc instAlloc = arena.alloc(instanceCount * sizeof(GpuInstance), 16);
    FrameArena::Alloc cmdAlloc = arena.alloc(drawCount * sizeof(GpuIndirectCommand), 16);
    if (!instAlloc.mapped || !cmdAlloc.mapped) return false;  // arena overflow: caller falls back
    std::memcpy(instAlloc.mapped, sorted.data(), instanceCount * sizeof(GpuInstance));
    std::memcpy(cmdAlloc.mapped, cmds.data(), drawCount * sizeof(GpuIndirectCommand));

    // Bind the arena instance buffer as bindless binding 4 (update before bind,
    // on the current frame slot's set only; the other slot's pending command
    // buffers keep pointing at their own frame's arena).
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless) return false;
    vk::DescriptorBufferInfo instInfo{arena.buffer(), instAlloc.offset, instAlloc.size};
    vk::WriteDescriptorSet instWrite{};
    instWrite.dstSet = bindless;
    instWrite.dstBinding = 4;
    instWrite.descriptorCount = 1;
    instWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
    instWrite.pBufferInfo = &instInfo;
    device->updateDescriptorSets(1, &instWrite, 0, nullptr);

    // Per-frame set0 through the shared ring (dynamic UBO offsets).
    const Graphics::GpuDrivenFrameSet0 s0 = gpuDrivenFrameSet0();
    if (!s0.set) return false;
    const uint32_t dynOffsets[2] = {s0.uboOffset, s0.shadowOffset};

    auto &cb = currentPresentCb();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 0, 1,
                          &s0.set, 2, dynOffsets);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 1, 1,
                          &bindless, 0, nullptr);
    const vk::DeviceSize stride = sizeof(GpuIndirectCommand);
    // Stage 1 keeps one host buffer pair per mesh (no pool yet), so each draw
    // group must bind the owning mesh's vertex/index buffers. The builder sorts
    // by (pipeline, material, mesh), so commands sharing a mesh are contiguous;
    // bind only when the mesh changes.
    GpuMesh *boundMesh = nullptr;
    for (uint32_t i = 0; i < drawCount; ++i) {
        const GpuInstance &first = sorted[cmds[i].firstInstance];
        if (first.meshId >= meshRecordOwners_.size()) {
            EV_ASSERT(false, "indirect draw references an unregistered mesh record");
            continue;
        }
        GpuMesh *mesh = meshRecordOwners_[first.meshId];
        if (mesh != boundMesh) {
            const vk::DeviceSize vbOffset = 0;
            cb.bindVertexBuffers(0, 1, mesh->vertices, &vbOffset);
            cb.bindIndexBuffer(mesh->indices.buffer, 0, mesh->indexType);
            boundMesh = mesh;
        }
        cb.drawIndexedIndirect(arena.buffer(), cmdAlloc.offset + vk::DeviceSize(i) * stride, 1,
                               stride);
    }
    return true;
}

GpuResidentSubmitStatus Graphics::gpuDrivenSubmitResident(const GpuResidentInstanceBatch &batch) {
    if (!gpuDrivenEnabled() || !gpuDrivenCaps_.gpuDrivenAvailable())
        return GpuResidentSubmitStatus::ResourceUnavailable;
    initGpuDrivenResources();
    if (!mesh3dGpuDrivenPipeline || bindlessSets_.empty() || !meshTableBuffer_.buffer)
        return GpuResidentSubmitStatus::ResourceUnavailable;
    if (batch.buffer.backend != GpuResidentBackend::Vulkan) return GpuResidentSubmitStatus::BackendMismatch;
    if (batch.buffer.nativeHandle == 0 || batch.buffer.offsetBytes % kGpuResidentStorageOffsetAlignment != 0 ||
        batch.buffer.strideBytes != sizeof(GpuInstance) || !batch.buckets || batch.bucketCount == 0 ||
        batch.instanceCount == 0)
        return GpuResidentSubmitStatus::InvalidArgument;
    if (batch.instanceCount > kMaxGpuDrivenInstances || batch.bucketCount > kMaxGpuDrivenBuckets)
        return GpuResidentSubmitStatus::CapacityExceeded;
    const uint64_t required = batch.buffer.offsetBytes + uint64_t(batch.instanceCount) * sizeof(GpuInstance);
    if (required < batch.buffer.offsetBytes || required > batch.buffer.sizeBytes)
        return GpuResidentSubmitStatus::InvalidArgument;

    std::vector<GpuIndirectCommand> commands(batch.bucketCount);
    uint64_t                        coveredInstances = 0;
    for (uint32_t i = 0; i < batch.bucketCount; ++i) {
        const GpuResidentInstanceBucket &bucket = batch.buckets[i];
        const uint64_t                   end    = uint64_t(bucket.firstInstance) + bucket.instanceCount;
        if (bucket.instanceCount == 0 || bucket.firstInstance != coveredInstances || end > batch.instanceCount ||
            bucket.meshId >= meshTableRecords_.size() || bucket.meshId >= meshRecordOwners_.size() ||
            bucket.materialId >= materialTableRecords_.size() || !meshRecordOwners_[bucket.meshId])
            return GpuResidentSubmitStatus::InvalidArgument;
        const GpuMeshRecord &mesh = meshTableRecords_[bucket.meshId];
        commands[i] = {mesh.indexCount, bucket.instanceCount, mesh.firstIndex, mesh.vertexBase, bucket.firstInstance};
        coveredInstances = end;
    }
    if (coveredInstances != batch.instanceCount) return GpuResidentSubmitStatus::InvalidArgument;

    auto             &arena        = currentFrameArena();
    FrameArena::Alloc commandAlloc = arena.alloc(batch.bucketCount * sizeof(GpuIndirectCommand), 16);
    if (!commandAlloc.mapped) return GpuResidentSubmitStatus::CapacityExceeded;
    std::memcpy(commandAlloc.mapped, commands.data(), batch.bucketCount * sizeof(GpuIndirectCommand));

    VkBuffer rawBuffer{};
    static_assert(sizeof(rawBuffer) <= sizeof(batch.buffer.nativeHandle));
    std::memcpy(&rawBuffer, &batch.buffer.nativeHandle, sizeof(rawBuffer));
    vk::Buffer              residentBuffer(rawBuffer);
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless || !residentBuffer) return GpuResidentSubmitStatus::ResourceUnavailable;
    vk::DescriptorBufferInfo instanceInfo{residentBuffer, batch.buffer.offsetBytes,
                                          uint64_t(batch.instanceCount) * sizeof(GpuInstance)};
    vk::WriteDescriptorSet   instanceWrite{};
    instanceWrite.dstSet          = bindless;
    instanceWrite.dstBinding      = 4;
    instanceWrite.descriptorCount = 1;
    instanceWrite.descriptorType  = vk::DescriptorType::eStorageBuffer;
    instanceWrite.pBufferInfo     = &instanceInfo;
    device->updateDescriptorSets(1, &instanceWrite, 0, nullptr);

    const Graphics::GpuDrivenFrameSet0 frame = gpuDrivenFrameSet0();
    if (!frame.set) return GpuResidentSubmitStatus::ResourceUnavailable;
    if (gpuDrivenScenePassPending()) gpuDrivenOpenScenePass();
    const uint32_t dynamicOffsets[2] = {frame.uboOffset, frame.shadowOffset};
    auto          &cb                = currentPresentCb();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 0, 1, &frame.set, 2,
                          dynamicOffsets);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 1, 1, &bindless, 0, nullptr);

    GpuMesh *boundMesh = nullptr;
    for (uint32_t i = 0; i < batch.bucketCount; ++i) {
        GpuMesh *mesh = meshRecordOwners_[batch.buckets[i].meshId];
        if (mesh != boundMesh) {
            const vk::DeviceSize vertexOffset = 0;
            cb.bindVertexBuffers(0, 1, mesh->vertices, &vertexOffset);
            cb.bindIndexBuffer(mesh->indices.buffer, 0, mesh->indexType);
            boundMesh = mesh;
        }
        cb.drawIndexedIndirect(arena.buffer(), commandAlloc.offset + uint64_t(i) * sizeof(GpuIndirectCommand), 1,
                               sizeof(GpuIndirectCommand));
    }
    lastGpuDrivenDrawCount_ = batch.bucketCount;
    return GpuResidentSubmitStatus::Submitted;
}

// --- Stage 2: HZB + GPU cull ------------------------------------------------

namespace {

/** @brief Gribb-Hartmann frustum planes from a projection*view matrix. */
void gpuDrivenFrustumPlanes(const glm::mat4 &m, glm::vec4 planes[6]) {
    // glm is column-major: row i is (m[0][i], m[1][i], m[2][i], m[3][i]).
    const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
    planes[0] = row3 + row0;  // left
    planes[1] = row3 - row0;  // right
    planes[2] = row3 + row1;  // bottom
    planes[3] = row3 - row1;  // top
    planes[4] = row3 + row2;  // near
    planes[5] = row3 - row2;  // far
    for (int i = 0; i < 6; ++i) {
        const float len = glm::length(glm::vec3(planes[i]));
        if (len > 1e-8f) planes[i] /= len;
    }
}

}  // namespace

Graphics::GpuDrivenCullSlot &Graphics::gpuDrivenCullSlot(uint32_t frameSlot) {
    // Callers guard with gpuDrivenCullReady_; this is a debug-only invariant.
    EV_ASSERT(!gpuDrivenCullSlots_.empty(), "gpuDriven cull slot accessed before resources");
    return gpuDrivenCullSlots_[frameSlot % gpuDrivenCullSlots_.size()];
}

Graphics::GpuDrivenCullSlot &Graphics::currentGpuDrivenCullSlot() {
    return gpuDrivenCullSlot(static_cast<uint32_t>(currentFrameSlot()));
}

void Graphics::ensureGpuDrivenCullResources(int width, int height) {
    if (!gpuDrivenCaps_.gpuDrivenCullAvailable() || width <= 0 || height <= 0) return;
    if (!bindlessSetLayout_) return;
    if (gpuDrivenCullReady_ && gpuDrivenCullWidth == width && gpuDrivenCullHeight == height)
        return;
    destroyGpuDrivenCullResources();

    gpuDrivenCullWidth = width;
    gpuDrivenCullHeight = height;

    // HZB word layout per slot: [16 header uints][mip0][mip1]...
    uint32_t mipOffsets[kMaxHzbMips] = {};
    uint32_t totalWords = kHZBHeaderWords;
    uint32_t maxMip = 0;
    {
        uint32_t w = uint32_t(width);
        uint32_t h = uint32_t(height);
        for (uint32_t m = 0; m < kMaxHzbMips; ++m) {
            mipOffsets[m] = totalWords;
            const uint32_t mw = std::max(w >> m, 1u);
            const uint32_t mh = std::max(h >> m, 1u);
            totalWords += mw * mh;
            maxMip = m;
            if (mw == 1 && mh == 1) break;
        }
    }
    gpuDrivenCullMaxMip = maxMip;

    const vk::DeviceSize flagBytes = kMaxGpuDrivenInstances * sizeof(uint32_t);
    const vk::DeviceSize compactBytes = kMaxGpuDrivenInstances * sizeof(GpuInstance);
    const vk::DeviceSize indirectBytes = kMaxGpuDrivenBuckets * sizeof(GpuIndirectCommand);
    const vk::DeviceSize indirectNIBytes = kMaxGpuDrivenBuckets * sizeof(glm::uvec4);
    const vk::DeviceSize counterBytes = kMaxGpuDrivenBuckets * sizeof(uint32_t);
    const vk::DeviceSize hzbBytes = totalWords * sizeof(uint32_t);

    gpuDrivenCullSlots_.resize(frameSlotCount());
    for (auto &slot : gpuDrivenCullSlots_) {
        slot.visibleFlags =
            vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer, flagBytes,
                               kHostVisibleCoherent);
        slot.compacted = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer,
                                            compactBytes, kHostVisibleCoherent);
        slot.indirect = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer |
                                                       vk::BufferUsageFlagBits::eIndirectBuffer,
                                           indirectBytes, kHostVisibleCoherent);
        slot.indirectNI = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer |
                                                         vk::BufferUsageFlagBits::eIndirectBuffer,
                                             indirectNIBytes, kHostVisibleCoherent);
        slot.bucketCounters =
            vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer, counterBytes,
                               kHostVisibleCoherent);
        slot.hzb = vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eStorageBuffer, hzbBytes,
                                      kHostVisibleCoherent);
        slot.cullParams =
            vkb::GenericBuffer(device, vk::BufferUsageFlagBits::eUniformBuffer,
                               sizeof(GpuCullParams), kHostVisibleCoherent);
        // Header offsets + zero depth (0 = near plane -> everything visible).
        void *hzbMap = slot.hzb.map();
        auto *u32 = static_cast<uint32_t *>(hzbMap);
        for (uint32_t m = 0; m <= maxMip; ++m) u32[m] = mipOffsets[m];
        for (uint32_t m = maxMip + 1; m < kHZBHeaderWords; ++m) u32[m] = 0;
        std::memset(static_cast<char *>(hzbMap) + kHZBHeaderWords * sizeof(uint32_t), 0,
                    (totalWords - kHZBHeaderWords) * sizeof(uint32_t));
        slot.hzb.unmap();
    }

    // Compute pipeline layout: set 1 = bindless (set 0 = empty placeholder so
    // the shaders' `layout(set = 1, ...)` bindings resolve to index 1).
    vk::DescriptorSetLayoutCreateInfo emptyInfo{};
    gpuDrivenComputeEmptyLayout_ = device->createDescriptorSetLayout(emptyInfo);
    vk::PushConstantRange pcr{vk::ShaderStageFlagBits::eCompute, 0, 20};
    vk::PipelineLayoutCreateInfo pli{};
    std::array<vk::DescriptorSetLayout, 2> computeLayouts{gpuDrivenComputeEmptyLayout_,
                                                          bindlessSetLayout_};
    pli.setLayoutCount = uint32_t(computeLayouts.size());
    pli.pSetLayouts = computeLayouts.data();
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    gpuDrivenComputeLayout = device->createPipelineLayout(pli);

    const auto spvOf = [](const uint32_t *p, size_t n) {
        return std::vector<uint32_t>(p, p + n);
    };
    hzbBuildPass_.create(device, gpuDrivenComputeLayout,
                         spvOf(hzb_build_comp_spv, hzb_build_comp_spv_count));
    cullPass_.create(device, gpuDrivenComputeLayout,
                     spvOf(gpu_cull_comp_spv, gpu_cull_comp_spv_count));
    emitPass_.create(device, gpuDrivenComputeLayout,
                     spvOf(gpu_emit_comp_spv, gpu_emit_comp_spv_count));
    vgCullPass_.create(device, gpuDrivenComputeLayout,
                       spvOf(vg_main_cull_comp_spv, vg_main_cull_comp_spv_count));
    gpuDrivenCullReady_ =
        hzbBuildPass_.pipeline() && cullPass_.pipeline() && emitPass_.pipeline();
}

void Graphics::destroyGpuDrivenCullResources() {
    hzbBuildPass_ = ComputePass{};
    cullPass_ = ComputePass{};
    emitPass_ = ComputePass{};
    vgCullPass_ = ComputePass{};
    if (gpuDrivenComputeLayout) {
        device->destroyPipelineLayout(gpuDrivenComputeLayout);
        gpuDrivenComputeLayout = nullptr;
    }
    if (gpuDrivenComputeEmptyLayout_) {
        device->destroyDescriptorSetLayout(gpuDrivenComputeEmptyLayout_);
        gpuDrivenComputeEmptyLayout_ = nullptr;
    }
    for (auto &slot : gpuDrivenCullSlots_) {
        slot.visibleFlags.release();
        slot.compacted.release();
        slot.indirect.release();
        slot.indirectNI.release();
        slot.bucketCounters.release();
        slot.hzb.release();
        slot.cullParams.release();
    }
    gpuDrivenCullSlots_.clear();
    gpuDrivenCullReady_ = false;
    gpuDrivenCullWidth = 0;
    gpuDrivenCullHeight = 0;
}

void Graphics::recordGpuDrivenHzbBuild() {
    if (!gpuDrivenCullReady_) return;
    auto *slot = currentGBufferSlot();
    if (!slot || !slot->depthGpu.sampler || !slot->depthGpu.imageView()) return;
    auto &cull = currentGpuDrivenCullSlot();
    auto &cb = currentPresentCb();

    // Depth attachment write -> compute shader read.
    vk::ImageMemoryBarrier imb{};
    imb.image = slot->depth.image();
    imb.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imb.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    imb.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                           vk::PipelineStageFlagBits::eLateFragmentTests,
                       vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 0, nullptr, 1,
                       &imb);

    uint32_t mipOffsets[kMaxHzbMips] = {};
    uint32_t totalWords = kHZBHeaderWords;
    {
        uint32_t w = uint32_t(gpuDrivenCullWidth);
        uint32_t h = uint32_t(gpuDrivenCullHeight);
        for (uint32_t m = 0; m <= gpuDrivenCullMaxMip; ++m) {
            mipOffsets[m] = totalWords;
            const uint32_t mw = std::max(w >> m, 1u);
            const uint32_t mh = std::max(h >> m, 1u);
            totalWords += mw * mh;
        }
    }
    for (uint32_t m = 0; m <= gpuDrivenCullMaxMip; ++m) {
        if (m > 0) {
            // Previous mip write -> this mip read.
            vk::BufferMemoryBarrier bmb{};
            bmb.buffer = cull.hzb.buffer;
            bmb.size = VK_WHOLE_SIZE;
            bmb.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            bmb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                               vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 1, &bmb,
                               0, nullptr);
        }
        const uint32_t mw = std::max(uint32_t(gpuDrivenCullWidth) >> m, 1u);
        const uint32_t mh = std::max(uint32_t(gpuDrivenCullHeight) >> m, 1u);
        struct HzbPush {
            uint32_t mip;
            uint32_t width;
            uint32_t height;
            uint32_t slotBase;
            uint32_t prevOffset;
        } push{m, mw, mh, 0u, mipOffsets[m > 0 ? m - 1 : 0]};
        cb.pushConstants(gpuDrivenComputeLayout, vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(push), &push);
        hzbBuildPass_.record(cb, (mw + 7) / 8, (mh + 7) / 8, 1);
    }
}

bool Graphics::gpuDrivenCullBegin(const GpuInstance *instances, uint32_t instanceCount) {
    if (!gpuDrivenCullEnabled() || !gpuDrivenCullReady_) return false;
    if (!instances || instanceCount == 0) return false;
    instanceCount = std::min(instanceCount, kMaxGpuDrivenInstances);

    eve::graphics::IndirectBuilder builder;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        builder.add(i, instances[i].meshId, instances[i].materialId, 0);
    }
    const uint32_t bucketCount = builder.build(meshTableRecords_);
    if (bucketCount == 0 || bucketCount > kMaxGpuDrivenBuckets) return false;
    const auto &cmds = builder.commands();
    const auto &order = builder.sortedInstanceOrder();

    std::vector<GpuInstance> sorted(instanceCount);
    for (uint32_t i = 0; i < instanceCount; ++i) sorted[i] = instances[order[i]];

    gpuDrivenBucketIds_.assign(instanceCount, 0);
    gpuDrivenBucketOffsets_.resize(bucketCount);
    gpuDrivenBucketMeshIds_.resize(bucketCount);
    for (uint32_t b = 0; b < bucketCount; ++b) {
        gpuDrivenBucketOffsets_[b] = cmds[b].firstInstance;
        gpuDrivenBucketMeshIds_[b] = sorted[cmds[b].firstInstance].meshId;
    }
    {
        uint32_t b = 0;
        for (uint32_t j = 0; j < instanceCount; ++j) {
            while (b + 1 < bucketCount && cmds[b + 1].firstInstance <= j) ++b;
            gpuDrivenBucketIds_[j] = b;
        }
    }

    auto &arena = currentFrameArena();
    // Storage-buffer descriptor offsets must honor minStorageBufferOffsetAlignment
    // (64 on this Intel driver); align conservatively to 64.
    gpuDrivenInstAlloc_ = arena.alloc(instanceCount * sizeof(GpuInstance), 64);
    gpuDrivenBucketIdAlloc_ = arena.alloc(instanceCount * sizeof(uint32_t), 64);
    gpuDrivenBucketOffAlloc_ = arena.alloc(bucketCount * sizeof(uint32_t), 64);
    if (!gpuDrivenInstAlloc_.mapped || !gpuDrivenBucketIdAlloc_.mapped ||
        !gpuDrivenBucketOffAlloc_.mapped) {
        gpuDrivenCullInstanceCount_ = 0;
        return false;
    }
    std::memcpy(gpuDrivenInstAlloc_.mapped, sorted.data(),
                instanceCount * sizeof(GpuInstance));
    std::memcpy(gpuDrivenBucketIdAlloc_.mapped, gpuDrivenBucketIds_.data(),
                instanceCount * sizeof(uint32_t));
    std::memcpy(gpuDrivenBucketOffAlloc_.mapped, gpuDrivenBucketOffsets_.data(),
                bucketCount * sizeof(uint32_t));

    gpuDrivenBucketCount_ = bucketCount;
    gpuDrivenCullInstanceCount_ = instanceCount;
    lastGpuDrivenDrawCount_ = bucketCount;  // debug counter: bucket draws the cull path emits

    // Cull source buffers: sorted instances (17), bucket ids (12), offsets (13).
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless) {
        gpuDrivenCullInstanceCount_ = 0;
        return false;
    }
    auto bufWrite = [&](uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset,
                        vk::DeviceSize size) {
        vk::DescriptorBufferInfo info{buffer, offset, size};
        vk::WriteDescriptorSet w{};
        w.dstSet = bindless;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = vk::DescriptorType::eStorageBuffer;
        w.pBufferInfo = &info;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    };
    bufWrite(17, arena.buffer(), gpuDrivenInstAlloc_.offset, gpuDrivenInstAlloc_.size);
    bufWrite(12, arena.buffer(), gpuDrivenBucketIdAlloc_.offset, gpuDrivenBucketIdAlloc_.size);
    bufWrite(13, arena.buffer(), gpuDrivenBucketOffAlloc_.offset, gpuDrivenBucketOffAlloc_.size);
    return true;
}

void Graphics::gpuDrivenRecordComputeSection(const glm::mat4 &viewProj, const glm::vec3 &eye,
                                             float fovYDeg, float nearZ, float farZ) {
    if (!gpuDrivenCullReady_) return;
    auto &slot = currentGpuDrivenCullSlot();
    auto &cb = currentPresentCb();
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless) return;
    // Lazily-created defaults register into the bindless set (bindings 0/1);
    // do it BEFORE the set is bound to the recording command buffer.
    ensureFlatNormalTexture3D();
    ensureFlatHeightTexture3D();
    ensureDefaultEnvCubemap();
    // Per-frame bindless updates must happen BEFORE the set is bound to the
    // recording command buffer (no UPDATE_AFTER_BIND): updating it afterwards
    // invalidates the command buffer (VUID-vkCmdBindPipeline-commandBuffer-recording).
    createGBufferResources(gbufferWidth > 0 ? gbufferWidth : int(swapchain.extent.width),
                           gbufferHeight > 0 ? gbufferHeight : int(swapchain.extent.height));
    {
        auto *gbSlot = currentGBufferSlot();
        if (gbSlot && gbSlot->visIDGpu.sampler && gbSlot->visBaryGpu.sampler) {
            vk::DescriptorImageInfo visIDInfo{gbSlot->visIDGpu.sampler, gbSlot->visIDGpu.imageView(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo visBaryInfo{gbSlot->visBaryGpu.sampler,
                                                gbSlot->visBaryGpu.imageView(),
                                                vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::WriteDescriptorSet w[2]{};
            w[0].dstSet = bindless;
            w[0].dstBinding = 15;
            w[0].descriptorCount = 1;
            w[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w[0].pImageInfo = &visIDInfo;
            w[1].dstSet = bindless;
            w[1].dstBinding = 16;
            w[1].descriptorCount = 1;
            w[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w[1].pImageInfo = &visBaryInfo;
            device->updateDescriptorSets(2, w, 0, nullptr);
        }
    }
    bindVgFrameBindless(bindless, currentFrameSlot() % kAsyncResourceCopies);

    GpuCullParams params{};
    params.viewProj = viewProj;
    gpuDrivenFrustumPlanes(viewProj, params.frustumPlanes);
    params.cameraPos = glm::vec4(eye, 0.f);
    const int w = gpuDrivenCullWidth;
    const int h = gpuDrivenCullHeight;
    params.screen = glm::vec4(float(w), float(h), 1.f / float(w), 1.f / float(h));
    const float fovRad = fovYDeg * 0.017453292519943295f;
    params.clipNearFar =
        glm::vec4(nearZ, farZ, float(h) * 0.5f / std::tan(fovRad * 0.5f), 1.f);
    params.hzbInfo =
        glm::vec4(float(gpuDrivenCullMaxMip), 0.f, float(w), float(h));
    params.counts = glm::uvec4(gpuDrivenCullInstanceCount_, gpuDrivenBucketCount_,
                               kMaxGpuDrivenBuckets, 0u);
    {
        void *pMap = slot.cullParams.map();
        std::memcpy(pMap, &params, sizeof(params));
        slot.cullParams.unmap();
    }

    auto bufWrite = [&](uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset,
                        vk::DeviceSize size, vk::DescriptorType type) {
        vk::DescriptorBufferInfo info{buffer, offset, size};
        vk::WriteDescriptorSet w{};
        w.dstSet = bindless;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        w.pBufferInfo = &info;
        device->updateDescriptorSets(1, &w, 0, nullptr);
    };
    bufWrite(6, slot.cullParams.buffer, 0, sizeof(GpuCullParams),
             vk::DescriptorType::eUniformBuffer);
    bufWrite(7, slot.visibleFlags.buffer, 0, VK_WHOLE_SIZE,
             vk::DescriptorType::eStorageBuffer);
    bufWrite(8, slot.compacted.buffer, 0, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    bufWrite(9, slot.indirect.buffer, 0, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    bufWrite(30, slot.indirectNI.buffer, 0, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    bufWrite(14, slot.bucketCounters.buffer, 0, VK_WHOLE_SIZE,
             vk::DescriptorType::eStorageBuffer);
    // The draw consumes the compacted buffer as its instance source (binding 4).
    bufWrite(4, slot.compacted.buffer, 0, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    // HZB buffer + GBuffer depth sampler for the build pass.
    bufWrite(10, slot.hzb.buffer, 0, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    {
        auto *gbSlot = currentGBufferSlot();
        if (gbSlot && gbSlot->depthGpu.sampler && gbSlot->depthGpu.imageView()) {
            vk::DescriptorImageInfo depthInfo{gbSlot->depthGpu.sampler, gbSlot->depthGpu.imageView(),
                                              vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::WriteDescriptorSet wd{};
            wd.dstSet = bindless;
            wd.dstBinding = 11;
            wd.descriptorCount = 1;
            wd.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wd.pImageInfo = &depthInfo;
            device->updateDescriptorSets(1, &wd, 0, nullptr);
        }
    }

    // All descriptor updates are done; bind the bindless set ONCE for the
    // whole compute section (the set is not UPDATE_AFTER_BIND, so updating it
    // while bound to a recording command buffer would invalidate the buffer).
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, gpuDrivenComputeLayout, 1, 1,
                          &bindless, 0, nullptr);

    recordGpuDrivenHzbBuild();

    // HZB build writes (storage buffer) -> cull reads.
    {
        vk::BufferMemoryBarrier bmb{};
        bmb.buffer = slot.hzb.buffer;
        bmb.size = VK_WHOLE_SIZE;
        bmb.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bmb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                           vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 1, &bmb, 0,
                           nullptr);
    }
}

void Graphics::gpuDrivenVgComputeSection(const glm::mat4 &viewProj, const glm::vec3 &eye,
                                         float fovYDeg, float nearZ, float farZ) {
    gpuDrivenRecordComputeSection(viewProj, eye, fovYDeg, nearZ, farZ);
}

void Graphics::gpuDrivenCullEmit(const glm::mat4 &viewProj, const glm::vec3 &eye, float fovYDeg,
                                 float nearZ, float farZ) {
    if (!gpuDrivenCullReady_ || gpuDrivenCullInstanceCount_ == 0) return;
    gpuDrivenLastCullSlot_ = uint32_t(currentFrameSlot());
    auto &slot = currentGpuDrivenCullSlot();
    auto &cb = currentPresentCb();
    // This frame's own bindless set: the previous frame that owned this slot
    // completed at acquireForFrame()'s fence wait, so rewriting it is safe.
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless) return;

    // CPU reset of GPU-owned per-slot state (slot's previous frame is complete).
    {
        void *flagsMap = slot.visibleFlags.map();
        std::memset(flagsMap, 0, kMaxGpuDrivenInstances * sizeof(uint32_t));
        slot.visibleFlags.unmap();
        void *counterMap = slot.bucketCounters.map();
        std::memset(counterMap, 0, kMaxGpuDrivenBuckets * sizeof(uint32_t));
        slot.bucketCounters.unmap();
        // The emit pass only writes buckets that survived culling; clear the
        // commands so a stale instanceCount from an earlier frame cannot make
        // a culled bucket draw again (readbacks / validation read this too).
        void *cmdMap = slot.indirect.map();
        std::memset(cmdMap, 0, kMaxGpuDrivenBuckets * sizeof(GpuIndirectCommand));
        slot.indirect.unmap();
    }

    gpuDrivenRecordComputeSection(viewProj, eye, fovYDeg, nearZ, farZ);

    const uint32_t groups = (gpuDrivenCullInstanceCount_ + 63u) / 64u;
    // Arena host writes + previous-frame HZB shader writes visible to cull.
    {
        vk::BufferMemoryBarrier bmb{};
        bmb.buffer = currentFrameArena().buffer();
        bmb.size = VK_WHOLE_SIZE;
        bmb.srcAccessMask = vk::AccessFlagBits::eHostWrite | vk::AccessFlagBits::eShaderWrite;
        bmb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eHost |
                               vk::PipelineStageFlagBits::eComputeShader,
                           vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 1, &bmb, 0,
                           nullptr);
    }
    cullPass_.record(cb, groups, 1, 1);

    // Cull flags write -> emit read.
    {
        vk::BufferMemoryBarrier bmb{};
        bmb.buffer = slot.visibleFlags.buffer;
        bmb.size = VK_WHOLE_SIZE;
        bmb.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bmb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                           vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr, 1, &bmb, 0,
                           nullptr);
    }
    emitPass_.record(cb, groups, 1, 1);

    // Emit writes (compacted + commands) -> vertex read + indirect read.
    {
        vk::BufferMemoryBarrier bmb[2]{};
        bmb[0].buffer = slot.compacted.buffer;
        bmb[0].size = VK_WHOLE_SIZE;
        bmb[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bmb[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        bmb[1].buffer = slot.indirect.buffer;
        bmb[1].size = VK_WHOLE_SIZE;
        bmb[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bmb[1].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                           vk::PipelineStageFlagBits::eVertexShader |
                               vk::PipelineStageFlagBits::eDrawIndirect,
                           {}, 0, nullptr, 2, bmb, 0, nullptr);
    }
}

void Graphics::gpuDrivenOpenScenePass() {
    if (!gpuDrivenScenePassPending_) return;
    gpuDrivenScenePassPending_ = false;
    if (beginSceneColorRenderPass()) {
        ensureScenePassPipelines(activeScenePass(), activeSceneSamples());
    } else {
        ensureScenePassPipelines(renderpass, vk::SampleCountFlagBits::e1);
        beginSwapchainColorPass();
    }
    swapchainPassOpen = true;
}

Graphics::GpuDrivenFrameSet0 Graphics::gpuDrivenFrameSet0() {
    // Per-frame set0 + UBO (same shading state as the stage-1 path).
    ensureFlatNormalTexture3D();
    ensureFlatHeightTexture3D();
    ensureDefaultEnvCubemap();
    Texture *tex = whiteTexture;
    auto *gpuTex = static_cast<GpuTexture *>(tex->gpuHandle);
    auto *gpuNormal = static_cast<GpuTexture *>(mesh3dNormalTexture ? mesh3dNormalTexture->gpuHandle
                                                                     : flatNormalTexture3D->gpuHandle);
    auto *gpuHeight = static_cast<GpuTexture *>(mesh3dHeightTexture ? mesh3dHeightTexture->gpuHandle
                                                                     : flatHeightTexture3D->gpuHandle);
    Texture *envTex = mesh3dEnvTexture ? mesh3dEnvTexture : defaultEnvCubemap;
    auto *gpuEnv = static_cast<GpuTexture *>(envTex->gpuHandle);
    auto *gpuDepth = static_cast<GpuTexture *>(whiteTexture->gpuHandle);
    ensureDecalPlaceholders();
    auto *gpuDecalAlb = static_cast<GpuTexture *>(decalFlatAlbedo->gpuHandle);
    auto *gpuDecalNrm = static_cast<GpuTexture *>(decalFlatNormal->gpuHandle);
    auto *gpuDecalPrm = static_cast<GpuTexture *>(decalFlatParams->gpuHandle);
    if (decalLayerFresh) {
        if (auto *dslot = currentDecalSlot()) {
            gpuDecalAlb = &dslot->albedoGpu;
            gpuDecalNrm = &dslot->normalGpu;
            gpuDecalPrm = &dslot->paramsGpu;
        }
    }

    Mesh3DUBO ubo = mesh3dFrameUbo;
    ubo.model = glm::mat4(1.f);
    ubo.tint = glm::vec4(1.f);
    const int lightCount = std::max(0, std::min(mesh3dLighting.count, Lighting3DPack::kMaxLights));
    ubo.lightDir.w = float(lightCount);
    ubo.cameraPos.w = mesh3dRoughness;
    ubo.lightColor.w = mesh3dEnvIntensity;
    const uint32_t envSlot = gpuEnv->bindlessIndexCube;
    uint32_t probeSlots[ReflectionProbeUpload::kMaxProbes] = {kInvalidBindlessSlot,
                                                              kInvalidBindlessSlot};
    for (int i = 0; i < ReflectionProbeUpload::kMaxProbes; ++i) {
        if (i >= mesh3dReflectionProbes.count) continue;
        const auto &probe = mesh3dReflectionProbes.probes[i];
        if (!probe.cubemap || !probe.cubemap->gpuHandle) continue;
        auto *gpuProbe = static_cast<GpuTexture *>(probe.cubemap->gpuHandle);
        if (!gpuProbe->isCube || gpuProbe->bindlessIndexCube == kInvalidBindlessSlot) continue;
        probeSlots[i] = gpuProbe->bindlessIndexCube;
        ubo.reflectionProbeCenter[i] = glm::vec4(probe.center, probe.intensity);
        ubo.reflectionProbeExtent[i] = glm::vec4(probe.extent, probe.blendDistance);
    }
    ubo.bindlessEnv = glm::vec4(float(envSlot), mesh3dEnvIntensity,
                                float(probeSlots[0]), float(probeSlots[1]));
    ubo.ambient = glm::vec4(glm::vec3(mesh3dLighting.ambient), mesh3dMetallic);
    for (int i = 0; i < lightCount; ++i) ubo.lights[i] = mesh3dLighting.lights[i];
    int dirI = -1;
    for (int i = 0; i < lightCount; ++i) {
        if (mesh3dLighting.lights[i].posRadius.w <= 0.f) {
            dirI = i;
            break;
        }
    }
    if (dirI >= 0) {
        glm::vec3 d(mesh3dLighting.lights[dirI].posRadius);
        if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
        else d = glm::normalize(d);
        ubo.lightDir = glm::vec4(d, float(lightCount));
        ubo.lightColor =
            glm::vec4(glm::vec3(mesh3dLighting.lights[dirI].color), mesh3dEnvIntensity);
    } else {
        ubo.lightDir = glm::vec4(0.f, 1.f, 0.f, float(lightCount));
        ubo.lightColor = glm::vec4(0.f, 0.f, 0.f, mesh3dEnvIntensity);
    }
    auto &fslots = currentMesh3dFrameSlots();
    if (fslots.drawIndex >= fslots.capacity) {
        std::fprintf(stderr, "[vulkan] mesh3d UBO ring exhausted; gpu-driven draw skipped\n");
        return {};
    }
    const size_t slot = fslots.drawIndex++;
    ensureMesh3dStrides();
    const uint32_t uboOffset = uint32_t(slot) * mesh3dUboStride;
    const uint32_t shadowOffset = uint32_t(slot) * shadowUboStride;
    updateRingLocal(fslots.uboRing, uboOffset, &ubo, sizeof(ubo));
    ShadowUBO shadowUbo = mesh3dShadows.ubo;
    if (!mesh3dShadows.active) {
        shadowUbo.bias.y = 0.f;
        shadowUbo.splits.w = 0.f;
    }
    shadowUbo.bias.z = mesh3dShadowReceive ? 1.f : 0.f;
    updateRingLocal(fslots.shadowRing, shadowOffset, &shadowUbo, sizeof(shadowUbo));
    vk::DescriptorSet set = mesh3dSetFor(gpuTex, gpuNormal, gpuEnv, gpuHeight, gpuDepth,
                                         gpuDecalAlb, gpuDecalNrm, gpuDecalPrm, fslots);
    return {set, uboOffset, shadowOffset};
}

void Graphics::gpuDrivenDrawOpaque() {
    if (!gpuDrivenCullReady_ || gpuDrivenBucketCount_ == 0) return;
    if (!swapchainPassOpen && !sceneColorPassOpen) return;
    auto &slot = currentGpuDrivenCullSlot();

    const Graphics::GpuDrivenFrameSet0 s0 = gpuDrivenFrameSet0();
    if (!s0.set) return;
    const uint32_t dynOffsets[2] = {s0.uboOffset, s0.shadowOffset};

    auto &cb = currentPresentCb();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 0, 1,
                          &s0.set, 2, dynOffsets);
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless) return;
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 1, 1,
                          &bindless, 0, nullptr);

    const vk::DeviceSize stride = sizeof(GpuIndirectCommand);
    GpuMesh *boundMesh = nullptr;
    for (uint32_t b = 0; b < gpuDrivenBucketCount_; ++b) {
        const uint32_t meshId = gpuDrivenBucketMeshIds_[b];
        if (meshId >= meshRecordOwners_.size()) continue;
        GpuMesh *mesh = meshRecordOwners_[meshId];
        if (mesh != boundMesh) {
            const vk::DeviceSize vbOffset = 0;
            cb.bindVertexBuffers(0, 1, mesh->vertices, &vbOffset);
            cb.bindIndexBuffer(mesh->indices.buffer, 0, mesh->indexType);
            boundMesh = mesh;
        }
        cb.drawIndexedIndirect(slot.indirect.buffer, vk::DeviceSize(b) * stride, 1, stride);
    }
}

void Graphics::gpuDrivenRecordVisPass() {
    if (!gpuDrivenCullReady_) return;
    if (gpuDrivenBucketCount_ == 0 && !vgAnyThisFrame_) return;
    // The vis attachments live in the GBuffer slot set; make sure it exists
    // even when the AO/gbuffer feature is off (resolve needs it anyway).
    createGBufferResources(gbufferWidth > 0 ? gbufferWidth : int(swapchain.extent.width),
                           gbufferHeight > 0 ? gbufferHeight : int(swapchain.extent.height));
    auto *slot = currentGBufferSlot();
    if (!slot || !gbufferVisPipeline || !gbufferVisRenderPass || !slot->visFramebuffer) return;
    auto &cull = currentGpuDrivenCullSlot();
    auto &cb = currentPresentCb();
    // Stage 3 VG: cluster cull (frustum + HZB) runs right before the vis pass.
    recordVgCull();

    const uint32_t w = uint32_t(gbufferWidth);
    const uint32_t h = uint32_t(gbufferHeight);
    std::array<vk::ClearValue, 6> clears{};
    clears[0].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
    clears[1].color = vk::ClearColorValue(std::array<float, 4>{1, 1, 1, 1});
    clears[2].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
    clears[3].color = vk::ClearColorValue(std::array<uint32_t, 4>{0xFFFFFFFFu, 0u, 0u, 0u});
    clears[4].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
    clears[5].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = gbufferVisRenderPass;
    rpBegin.framebuffer = slot->visFramebuffer;
    rpBegin.renderArea = vk::Rect2D{{0, 0}, {w, h}};
    rpBegin.clearValueCount = uint32_t(clears.size());
    rpBegin.pClearValues = clears.data();
    slot->normal.beginColorAttachment();
    slot->depthColor.beginColorAttachment();
    slot->albedo.beginColorAttachment();
    slot->visID.beginColorAttachment();
    slot->visBary.beginColorAttachment();
    slot->depth.beginDepthAttachment();
    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    setViewportAndScissor(cb, w, h);

    const Graphics::GpuDrivenFrameSet0 s0 = gpuDrivenFrameSet0();
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (s0.set && bindless) {
        const uint32_t dynOffsets[2] = {s0.uboOffset, s0.shadowOffset};
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gbufferVisPipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 0,
                              1, &s0.set, 2, dynOffsets);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 1,
                              1, &bindless, 0, nullptr);
        const vk::DeviceSize stride = sizeof(glm::uvec4);
        for (uint32_t b = 0; b < gpuDrivenBucketCount_; ++b) {
            cb.drawIndirect(cull.indirectNI.buffer, vk::DeviceSize(b) * stride, 1, stride);
        }
        if (vgAnyThisFrame_) drawVgClusters(cb);
    }
    cb.endRenderPass();
    slot->normal.endSampledLayout();
    slot->depthColor.endSampledLayout();
    slot->albedo.endSampledLayout();
    slot->visID.endSampledLayout();
    slot->visBary.endSampledLayout();
    slot->depth.endSampledLayout();
}

void Graphics::gpuDrivenResolve() {
    if (!gpuDrivenCullReady_) return;
    if (gpuDrivenBucketCount_ == 0 && !vgAnyThisFrame_) return;
    if (!swapchainPassOpen && !sceneColorPassOpen) return;
    if (!resolveVisPipeline || !mesh3dGpuDrivenPipelineLayout) return;
    auto *slot = currentGBufferSlot();
    if (!slot || !slot->visIDGpu.sampler || !slot->visBaryGpu.sampler) return;
    auto &cb = currentPresentCb();
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless) return;

    const Graphics::GpuDrivenFrameSet0 s0 = gpuDrivenFrameSet0();
    if (!s0.set) return;
    const uint32_t dynOffsets[2] = {s0.uboOffset, s0.shadowOffset};
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resolveVisPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 0, 1,
                          &s0.set, 2, dynOffsets);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 1, 1,
                          &bindless, 0, nullptr);
    const uint32_t w =
        uint32_t(sceneColorPassOpen ? sceneColorWidth : int(swapchain.extent.width));
    const uint32_t h =
        uint32_t(sceneColorPassOpen ? sceneColorHeight : int(swapchain.extent.height));
    setViewportAndScissor(cb, w, h);
    cb.draw(3, 1, 0, 0);
}

void Graphics::createResolveVisPipeline(const vkb::BuiltRenderPass &rp,
                                        vk::SampleCountFlagBits samples) {
    destroyPipeline(device, resolveVisPipeline);
    if (!mesh3dGpuDrivenPipelineLayout) return;
    std::vector<uint32_t> vert(resolve_vis_vert_spv, resolve_vis_vert_spv +
                                                         resolve_vis_vert_spv_count);
    std::vector<uint32_t> frag(resolve_vis_frag_spv, resolve_vis_frag_spv +
                                                         resolve_vis_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    resolveVisPipeline =
        device.createPipeline()
            .useClassicPipeline(vertModule, fragModule)
            .setPipelineLayout(mesh3dGpuDrivenPipelineLayout)
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                           vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise)
            .setMultisampler(false, samples)
            .setDepthStencil(true, true, vk::CompareOp::eLessOrEqual)
            .setColorAttachmentCount(1)
            .build(rp);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
}

void Graphics::recordVgCull() {
    if (!gpuDrivenCullReady_ || vgAssetCount_ == 0 || vgClusterCount_ == 0) return;
    if (!vgCullPass_.pipeline() || !vgGpu_.visible.buffer) return;
    auto &cb = currentPresentCb();
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless) return;

    const size_t slot = currentFrameSlot() % kAsyncResourceCopies;
    vgLastVisible_ = uint32_t(slot);

    // Uploads and per-frame instance/material projections are written through
    // host-visible mappings. Make those writes available before the compute
    // cull and the following vertex/fragment fetches; validation layers can
    // otherwise hide this race by perturbing submission timing.
    {
        std::array<vk::BufferMemoryBarrier, 6> barriers{};
        const vk::Buffer buffers[] = {
            vgGpu_.positions.buffer, vgGpu_.triangles.buffer, vgGpu_.clusters.buffer,
            vgGpu_.clusterAssets.buffer, vgGpu_.assetMaterials.buffer, vgGpu_.assetModels.buffer,
        };
        for (size_t i = 0; i < barriers.size(); ++i) {
            barriers[i].buffer = buffers[i];
            barriers[i].size = VK_WHOLE_SIZE;
            barriers[i].srcAccessMask = vk::AccessFlagBits::eHostWrite;
            barriers[i].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        }
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                           vk::PipelineStageFlagBits::eComputeShader |
                               vk::PipelineStageFlagBits::eVertexShader |
                               vk::PipelineStageFlagBits::eFragmentShader,
                           {}, 0, nullptr, uint32_t(barriers.size()), barriers.data(), 0, nullptr);
    }

    // Reset this slot's visible counter (the slot's fence was waited at frame
    // begin, so the previous use of this slot's buffers has completed).
    {
        const vk::DeviceSize slotVis = (vk::DeviceSize(kMaxVgClusters) + 1) * sizeof(uint32_t);
        void *map = vgGpu_.visible.map();
        if (!map) return;
        std::memset(static_cast<char *>(map) + slotVis * slot, 0, sizeof(uint32_t));
        vgGpu_.visible.unmap();
    }

    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, gpuDrivenComputeLayout, 1, 1,
                          &bindless, 0, nullptr);
    const uint32_t push[4]{vgClusterCount_, 0u, 0u, 0u};
    cb.pushConstants(gpuDrivenComputeLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push),
                     push);
    vgCullPass_.record(cb, (vgClusterCount_ + 63u) / 64u);

    // VG cull writes (visible + indirect) -> vertex read + indirect read.
    {
        vk::BufferMemoryBarrier bmb[2]{};
        bmb[0].buffer = vgGpu_.visible.buffer;
        bmb[0].size = VK_WHOLE_SIZE;
        bmb[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bmb[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        bmb[1].buffer = vgGpu_.indirect.buffer;
        bmb[1].size = VK_WHOLE_SIZE;
        bmb[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        bmb[1].dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                           vk::PipelineStageFlagBits::eVertexShader |
                               vk::PipelineStageFlagBits::eDrawIndirect,
                           {}, 0, nullptr, 2, bmb, 0, nullptr);
    }
}

void Graphics::drawVgClusters(vk::CommandBuffer cb) {
    if (!gbufferVgVisPipeline || vgAssetCount_ == 0 || vgClusterCount_ == 0) return;
    const vk::DescriptorSet bindless = bindlessSetForFrame();
    if (!bindless || !mesh3dGpuDrivenPipelineLayout) return;
    const size_t slot = currentFrameSlot() % kAsyncResourceCopies;
    const vk::DeviceSize slotInd = vk::DeviceSize(kMaxVgClusters) * sizeof(glm::uvec4);
    const vk::DeviceSize slotVis = (vk::DeviceSize(kMaxVgClusters) + 1) * sizeof(uint32_t);
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gbufferVgVisPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dGpuDrivenPipelineLayout, 1, 1,
                          &bindless, 0, nullptr);
    cb.drawIndirectCount(vgGpu_.indirect.buffer, slotInd * slot, vgGpu_.visible.buffer,
                         slotVis * slot, kMaxVgClusters, sizeof(glm::uvec4));
}

uint32_t Graphics::debugGpuDrivenVisibleCount() const {
    if (!gpuDrivenCullReady_ || gpuDrivenCullSlots_.empty()) return 0;
    auto &slot = gpuDrivenCullSlots_[gpuDrivenLastCullSlot_ % gpuDrivenCullSlots_.size()];
    uint32_t count = 0;
    void *map = slot.visibleFlags.map();
    if (!map) return 0;
    const uint32_t n = std::min(gpuDrivenCullInstanceCount_, kMaxGpuDrivenInstances);
    const auto *flags = static_cast<const uint32_t *>(map);
    for (uint32_t i = 0; i < n; ++i) count += flags[i] != 0 ? 1u : 0u;
    slot.visibleFlags.unmap();
    return count;
}

uint32_t Graphics::debugGpuDrivenCulledDrawCount() const {
    if (!gpuDrivenCullReady_ || gpuDrivenCullSlots_.empty()) return 0;
    auto &slot = gpuDrivenCullSlots_[gpuDrivenLastCullSlot_ % gpuDrivenCullSlots_.size()];
    uint32_t count = 0;
    void *map = slot.indirect.map();
    if (!map) return 0;
    const auto *cmds = static_cast<const GpuIndirectCommand *>(map);
    const uint32_t n = std::min(gpuDrivenBucketCount_, kMaxGpuDrivenBuckets);
    for (uint32_t b = 0; b < n; ++b) count += cmds[b].instanceCount > 0 ? 1u : 0u;
    slot.indirect.unmap();
    return count;
}

uint32_t Graphics::debugBindlessIndex(Texture *tex) const {
    if (!tex || !tex->gpuHandle) return kInvalidBindlessSlot;
    return static_cast<GpuTexture *>(tex->gpuHandle)->bindlessIndex2D;
}

uint32_t Graphics::debugMeshRecordIndex(Mesh *mesh) const {
    if (!mesh || !mesh->gpuHandle) return kInvalidBindlessSlot;
    return static_cast<GpuMesh *>(mesh->gpuHandle)->gpuRecordIndex;
}

void Graphics::createMesh3DGpuDrivenPipeline() {
    if (mesh3dGpuDrivenPipeline) return;
    if (!mesh3dSetLayout || !bindlessSetLayout_ || !gpuDrivenCaps_.gpuDrivenAvailable()) return;

    // Pipeline layout: set0 = per-frame (legacy mesh3d layout), set1 = bindless.
    // No push constants: instance indexing relies on gl_InstanceIndex, which
    // already includes the command's firstInstance per the Vulkan spec.
    std::array<vk::DescriptorSetLayout, 2> setLayouts{mesh3dSetLayout, bindlessSetLayout_};
    vk::PipelineLayoutCreateInfo pli{};
    pli.setLayoutCount = uint32_t(setLayouts.size());
    pli.pSetLayouts = setLayouts.data();
    mesh3dGpuDrivenPipelineLayout = device->createPipelineLayout(pli);
    mesh3dGpuDrivenPipeline =
        createMesh3DStylePipeline(embeddedSpirv(mesh3d_gpudriven_vert_spv),
                                  embeddedSpirv(mesh3d_gpudriven_frag_spv),
                                  mesh3dGpuDrivenPipelineLayout, renderpass,
                                  vk::SampleCountFlagBits::e1);
}
}  // namespace eve::graphics::vulkan
