#include "graphics/vulkan/Graphics.h"

#include "graphics/vulkan/GraphicsInternal.h"

#include "graphics/shaders/particle_resident_comp_spv.inc"
#include "graphics/shaders/particle_resident_frag_spv.inc"
#include "graphics/shaders/particle_resident_vert_spv.inc"

#include <algorithm>
#include <array>
#include <cstring>

namespace eve::graphics::vulkan {

namespace {

struct alignas(16) ParticleMeta {
    std::uint32_t vertexCount   = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstVertex   = 0;
    std::uint32_t firstInstance = 0;
    std::uint32_t alive         = 0;
    std::uint32_t spawned       = 0;
    std::uint32_t killed        = 0;
    std::uint32_t dropped       = 0;
};

static_assert(sizeof(ParticleMeta) == 32, "particle metadata must match GLSL");

struct alignas(16) ParticleComputePush {
    float         stepEmitterX[4]{};
    float         forces0[4]{};
    float         forces1[4]{};
    float         localOffset[2]{};
    std::uint32_t capacity   = 0;
    std::uint32_t spawnCount = 0;
};

static_assert(sizeof(ParticleComputePush) == 64, "particle compute push layout must match GLSL");

struct alignas(16) ParticleDrawPush {
    float         viewportCamera[4]{};
    float         cameraParticle[4]{};
    float         sizeMode[4]{};
    float         colorStart[4]{};
    float         colorEnd[4]{};
    std::uint32_t flipbook[4]{};
};

static_assert(sizeof(ParticleDrawPush) == 96, "particle draw push layout must match GLSL");

vk::Pipeline createParticleDrawPipeline(vkb::Device& device, const vkb::BuiltRenderPass& renderPass,
                                        vk::PipelineLayout layout, vk::ShaderModule vert, vk::ShaderModule frag,
                                        BlendMode blend) {
    const std::array stages{
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, vert, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, frag, "main"),
    };
    vk::PipelineVertexInputStateCreateInfo   vertexInput{};
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    vk::Viewport                             viewport(0.f, 0.f, 1.f, 1.f, 0.f, 1.f);
    vk::Rect2D                               scissor({0, 0}, {1, 1});
    vk::PipelineViewportStateCreateInfo      viewportState({}, 1, &viewport, 1, &scissor);
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode    = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace   = vk::FrontFace::eCounterClockwise;
    rasterizer.lineWidth   = 1.f;
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    const auto                              attachment = makeBlendAttachment(blend);
    vk::PipelineColorBlendStateCreateInfo   colorBlend{};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &attachment;
    const std::array                   dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);

    vk::GraphicsPipelineCreateInfo info{};
    info.stageCount          = std::uint32_t(stages.size());
    info.pStages             = stages.data();
    info.pVertexInputState   = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState      = &viewportState;
    info.pRasterizationState = &rasterizer;
    info.pMultisampleState   = &multisampling;
    info.pDepthStencilState  = &depthStencil;
    info.pColorBlendState    = &colorBlend;
    info.pDynamicState       = &dynamicState;
    info.layout              = layout;
    info.renderPass          = renderPass.handle;
    const auto result        = device->createGraphicsPipeline(vk::PipelineCache{}, info);
    if (result.result != vk::Result::eSuccess) throw std::runtime_error("failed_create_gpu_particle_pipeline");
    return result.value;
}

void updateStorageBinding(vk::WriteDescriptorSet& write, vk::DescriptorBufferInfo& info, vk::DescriptorSet set,
                          std::uint32_t binding, vk::Buffer buffer, vk::DeviceSize size) {
    info.buffer           = buffer;
    info.offset           = 0;
    info.range            = size;
    write.dstSet          = set;
    write.dstBinding      = binding;
    write.descriptorCount = 1;
    write.descriptorType  = vk::DescriptorType::eStorageBuffer;
    write.pBufferInfo     = &info;
}

}  // namespace

struct Graphics::GpuParticleResource {
    struct Slot {
        vkb::GenericBuffer state;
        vkb::GenericBuffer meta;
        vkb::GenericBuffer indirect;
        vkb::GenericBuffer spawns;
        vkb::GenericBuffer scratchState;
        vkb::GenericBuffer scratchMeta;
        vk::DescriptorSet  computeSet{};
        vk::DescriptorSet  drawSet{};
        GpuTexture*        drawTexture = nullptr;
        std::uint64_t      serial      = 0;
        bool               initialized = false;
    };

    std::uint32_t                 capacity = 0;
    std::vector<Slot>             slots;
    int                           lastOutputSlot = -1;
    bool                          pendingUpdate  = false;
    bool                          pendingReset   = true;
    GpuParticleUpdate             update;
    std::vector<GpuParticleSpawn> spawns;
    GpuParticleStats              stats;
    std::uint64_t                 statsSerial = 0;
};

bool Graphics::canSubmitGpuParticles() const {
    if (!supportsGpuParticles() || !gpuParticleComputePipeline_ || !gpuParticleAlphaPipeline_) return false;
    if (activeCanvas != nullptr) return false;
    return !(swapchainPassOpen && !sceneColorPassOpen);
}

GpuParticleHandle Graphics::createGpuParticleEmitter(std::uint32_t capacity) {
    if (!supportsGpuParticles() || capacity == 0) return kInvalidGpuParticleHandle;
    auto* resource                 = new GpuParticleResource();
    resource->capacity             = capacity;
    const GpuParticleHandle handle = nextGpuParticleHandle_++;
    gpuParticles_.emplace(handle, resource);
    return handle;
}

void Graphics::releaseGpuParticleEmitter(GpuParticleHandle handle) {
    auto found = gpuParticles_.find(handle);
    if (found == gpuParticles_.end()) return;
    waitForSharedGpuResources();
    std::vector<vk::DescriptorSet> sets;
    for (const auto& slot : found->second->slots) {
        if (slot.computeSet) sets.push_back(slot.computeSet);
        if (slot.drawSet) sets.push_back(slot.drawSet);
    }
    if (!sets.empty() && descriptorPool) device->freeDescriptorSets(descriptorPool, sets);
    delete found->second;
    gpuParticles_.erase(found);
}

void Graphics::resetGpuParticleEmitter(GpuParticleHandle handle) {
    auto found = gpuParticles_.find(handle);
    if (found == gpuParticles_.end()) return;
    auto& resource         = *found->second;
    resource.pendingReset  = true;
    resource.pendingUpdate = true;
    resource.update        = {};
    resource.spawns.clear();
    resource.stats = {};
}

bool Graphics::updateGpuParticleEmitter(GpuParticleHandle handle, const GpuParticleUpdate& update,
                                        const GpuParticleSpawn* spawns, std::uint32_t spawnCount) {
    if (!canSubmitGpuParticles()) return false;
    auto found = gpuParticles_.find(handle);
    if (found == gpuParticles_.end()) return false;
    auto& resource               = *found->second;
    resource.update              = update;
    const std::uint32_t accepted = std::min(spawnCount, resource.capacity);
    if (accepted > 0 && spawns)
        resource.spawns.assign(spawns, spawns + accepted);
    else
        resource.spawns.clear();
    resource.pendingUpdate = true;
    return true;
}

bool Graphics::drawGpuParticleEmitter(GpuParticleHandle handle, const GpuParticleDraw& draw) {
    if (!canSubmitGpuParticles()) return false;
    if (gpuParticles_.find(handle) == gpuParticles_.end()) return false;
    const std::uint32_t index = std::uint32_t(gpuParticleDraws_.size());
    gpuParticleDraws_.push_back({handle, draw});
    auto& spans = recordingEngine3D_ ? engine3DSpans : overlaySpans;
    spans.push_back({OverlayKind::GpuParticles, index, 0, 0});
    return true;
}

GpuParticleStats Graphics::getGpuParticleStats(GpuParticleHandle handle) const {
    auto found = gpuParticles_.find(handle);
    return found == gpuParticles_.end() ? GpuParticleStats{} : found->second->stats;
}

void Graphics::createGpuParticlePipelines() {
    if (gpuParticleComputePipeline_ || !descriptorPool || !renderpass) return;

    std::array<vk::DescriptorSetLayoutBinding, 5> computeBindings{};
    for (std::uint32_t binding = 0; binding < computeBindings.size(); ++binding) {
        computeBindings[binding] = {binding, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
    }
    gpuParticleComputeSetLayout_ =
        device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, computeBindings));

    const std::array drawBindings{
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1,
                                       vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex),
    };
    gpuParticleDrawSetLayout_ = device->createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo({}, drawBindings));

    const auto computePush    = pushConstantRange(vk::ShaderStageFlagBits::eCompute, sizeof(ParticleComputePush));
    gpuParticleComputeLayout_ = createPipelineLayout(device, gpuParticleComputeSetLayout_, &computePush);
    const auto drawPush       = pushConstantRange(vk::ShaderStageFlagBits::eVertex, sizeof(ParticleDrawPush));
    gpuParticleDrawLayout_    = createPipelineLayout(device, gpuParticleDrawSetLayout_, &drawPush);

    const auto computeSpv = std::vector<std::uint32_t>(particle_resident_comp_spv,
                                                       particle_resident_comp_spv + particle_resident_comp_spv_count);
    vk::ShaderModule computeModule = vkb::PipelineBuilder::createShaderModule(device.instance, computeSpv);
    vk::PipelineShaderStageCreateInfo stage{};
    stage.stage  = vk::ShaderStageFlagBits::eCompute;
    stage.module = computeModule;
    stage.pName  = "main";
    vk::ComputePipelineCreateInfo computeInfo{};
    computeInfo.stage  = stage;
    computeInfo.layout = gpuParticleComputeLayout_;
    auto computeResult = device->createComputePipeline({}, computeInfo);
    if (computeResult.result == vk::Result::eSuccess) gpuParticleComputePipeline_ = computeResult.value;
    device->destroyShaderModule(computeModule);

    const auto vertSpv    = std::vector<std::uint32_t>(particle_resident_vert_spv,
                                                       particle_resident_vert_spv + particle_resident_vert_spv_count);
    const auto fragSpv    = std::vector<std::uint32_t>(particle_resident_frag_spv,
                                                       particle_resident_frag_spv + particle_resident_frag_spv_count);
    vk::ShaderModule vert = vkb::PipelineBuilder::createShaderModule(device.instance, vertSpv);
    vk::ShaderModule frag = vkb::PipelineBuilder::createShaderModule(device.instance, fragSpv);
    gpuParticleAlphaPipeline_ =
        createParticleDrawPipeline(device, renderpass, gpuParticleDrawLayout_, vert, frag, BlendMode::Alpha);
    gpuParticleAdditivePipeline_ =
        createParticleDrawPipeline(device, renderpass, gpuParticleDrawLayout_, vert, frag, BlendMode::Additive);
    gpuParticlePremultipliedPipeline_ =
        createParticleDrawPipeline(device, renderpass, gpuParticleDrawLayout_, vert, frag, BlendMode::Premultiplied);
    gpuParticleMultiplyPipeline_ =
        createParticleDrawPipeline(device, renderpass, gpuParticleDrawLayout_, vert, frag, BlendMode::Multiply);
    gpuParticleOpaquePipeline_ =
        createParticleDrawPipeline(device, renderpass, gpuParticleDrawLayout_, vert, frag, BlendMode::Opaque);
    device->destroyShaderModule(vert);
    device->destroyShaderModule(frag);
}

void Graphics::destroyGpuParticleResources() {
    gpuParticleDraws_.clear();
    for (auto& [handle, resource] : gpuParticles_) {
        (void)handle;
        std::vector<vk::DescriptorSet> sets;
        for (const auto& slot : resource->slots) {
            if (slot.computeSet) sets.push_back(slot.computeSet);
            if (slot.drawSet) sets.push_back(slot.drawSet);
        }
        if (!sets.empty() && descriptorPool) device->freeDescriptorSets(descriptorPool, sets);
        delete resource;
    }
    gpuParticles_.clear();
    auto destroyPipeline = [&](vk::Pipeline& pipeline) {
        if (pipeline) device->destroyPipeline(pipeline);
        pipeline = nullptr;
    };
    destroyPipeline(gpuParticleComputePipeline_);
    destroyPipeline(gpuParticleAlphaPipeline_);
    destroyPipeline(gpuParticleAdditivePipeline_);
    destroyPipeline(gpuParticlePremultipliedPipeline_);
    destroyPipeline(gpuParticleMultiplyPipeline_);
    destroyPipeline(gpuParticleOpaquePipeline_);
    if (gpuParticleComputeLayout_) device->destroyPipelineLayout(gpuParticleComputeLayout_);
    if (gpuParticleDrawLayout_) device->destroyPipelineLayout(gpuParticleDrawLayout_);
    gpuParticleComputeLayout_ = nullptr;
    gpuParticleDrawLayout_    = nullptr;
    if (gpuParticleComputeSetLayout_) device->destroyDescriptorSetLayout(gpuParticleComputeSetLayout_);
    if (gpuParticleDrawSetLayout_) device->destroyDescriptorSetLayout(gpuParticleDrawSetLayout_);
    gpuParticleComputeSetLayout_ = nullptr;
    gpuParticleDrawSetLayout_    = nullptr;
}

void Graphics::recordGpuParticleCompute(vk::CommandBuffer cb) {
    if (!cb || !gpuParticleComputePipeline_) return;
    const std::size_t frameSlot     = currentFrameSlot();
    const std::size_t requiredSlots = std::max<std::size_t>(2, frameSlotCount());
    using BufferUsage               = vk::BufferUsageFlagBits;
    using Memory                    = vk::MemoryPropertyFlagBits;

    for (auto& [handle, owned] : gpuParticles_) {
        (void)handle;
        auto& resource = *owned;
        if (!resource.pendingUpdate && !resource.pendingReset) continue;
        if (resource.slots.size() < requiredSlots) resource.slots.resize(requiredSlots);
        auto&                slot       = resource.slots[frameSlot % resource.slots.size()];
        const vk::DeviceSize stateBytes = vk::DeviceSize(resource.capacity) * sizeof(GpuParticleSpawn);
        const vk::DeviceSize metaBytes  = sizeof(ParticleMeta);

        if (!slot.state.buffer) {
            const auto stateUsage = BufferUsage::eStorageBuffer | BufferUsage::eTransferSrc | BufferUsage::eTransferDst;
            const auto metaUsage  = BufferUsage::eStorageBuffer | BufferUsage::eTransferSrc | BufferUsage::eTransferDst;
            slot.state.allocate(frameToken(), device, stateUsage, stateBytes);
            slot.scratchState.allocate(frameToken(), device, stateUsage, stateBytes);
            slot.meta.allocate(frameToken(), device, metaUsage, metaBytes,
                               Memory::eHostVisible | Memory::eHostCoherent);
            slot.indirect.allocate(frameToken(), device, BufferUsage::eIndirectBuffer | BufferUsage::eTransferDst,
                                   sizeof(vk::DrawIndirectCommand));
            slot.scratchMeta.allocate(frameToken(), device, metaUsage, metaBytes);
            slot.spawns.allocate(frameToken(), device, BufferUsage::eStorageBuffer, stateBytes,
                                 Memory::eHostVisible | Memory::eHostCoherent);
            const ParticleMeta zero{};
            slot.meta.updateLocal(frameToken(), zero);
        }

        if (slot.initialized && slot.serial > resource.statsSerial) {
            const auto* meta = static_cast<const ParticleMeta*>(slot.meta.map());
            if (meta) {
                resource.stats.alive     = meta->alive;
                resource.stats.instances = meta->instanceCount;
                resource.stats.spawned   = meta->spawned;
                resource.stats.killed    = meta->killed;
                resource.stats.dropped   = meta->dropped;
                slot.meta.unmap();
                resource.statsSerial = slot.serial;
            }
        }

        if (!resource.spawns.empty()) slot.spawns.updateLocal(frameToken(), resource.spawns);

        vk::Buffer      previousState = slot.scratchState.buffer;
        vk::Buffer      previousMeta  = slot.scratchMeta.buffer;
        vk::AccessFlags previousStateAccess{};
        vk::AccessFlags previousMetaAccess{};
        if (!resource.pendingReset && resource.lastOutputSlot >= 0) {
            auto& previous = resource.slots[std::size_t(resource.lastOutputSlot)];
            if (&previous == &slot) {
                std::array<vk::BufferMemoryBarrier, 2> toTransfer{};
                toTransfer[0].buffer        = slot.state.buffer;
                toTransfer[0].size          = VK_WHOLE_SIZE;
                toTransfer[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
                toTransfer[0].dstAccessMask = vk::AccessFlagBits::eTransferRead;
                toTransfer[1].buffer        = slot.meta.buffer;
                toTransfer[1].size          = VK_WHOLE_SIZE;
                toTransfer[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
                toTransfer[1].dstAccessMask = vk::AccessFlagBits::eTransferRead;
                cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, {},
                                   nullptr, toTransfer, nullptr);
                cb.copyBuffer(slot.state.buffer, slot.scratchState.buffer, vk::BufferCopy{0, 0, stateBytes});
                cb.copyBuffer(slot.meta.buffer, slot.scratchMeta.buffer, vk::BufferCopy{0, 0, metaBytes});
                previousStateAccess = vk::AccessFlagBits::eTransferWrite;
                previousMetaAccess  = vk::AccessFlagBits::eTransferWrite;
            } else {
                previousState       = previous.state.buffer;
                previousMeta        = previous.meta.buffer;
                previousStateAccess = vk::AccessFlagBits::eShaderWrite;
                previousMetaAccess  = vk::AccessFlagBits::eShaderWrite;
            }
        } else {
            cb.fillBuffer(slot.scratchMeta.buffer, 0, metaBytes, 0);
            previousMetaAccess = vk::AccessFlagBits::eTransferWrite;
        }

        cb.fillBuffer(slot.meta.buffer, 0, metaBytes, 0);

        std::array<vk::BufferMemoryBarrier, 5> toCompute{};
        const std::array<vk::Buffer, 5>        computeBuffers{previousState, previousMeta, slot.spawns.buffer,
                                                              slot.meta.buffer, slot.state.buffer};
        const std::array<vk::AccessFlags, 5>   sourceAccess{
            previousStateAccess,
            previousMetaAccess,
            vk::AccessFlagBits::eHostWrite,
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
        };
        const std::array<vk::AccessFlags, 5> destinationAccess{
            vk::AccessFlagBits::eShaderRead,  vk::AccessFlagBits::eShaderRead,
            vk::AccessFlagBits::eShaderRead,  vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eShaderWrite,
        };
        for (std::size_t i = 0; i < toCompute.size(); ++i) {
            toCompute[i].buffer        = computeBuffers[i];
            toCompute[i].size          = VK_WHOLE_SIZE;
            toCompute[i].srcAccessMask = sourceAccess[i];
            toCompute[i].dstAccessMask = destinationAccess[i];
        }
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eHost |
                               vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eVertexShader,
                           vk::PipelineStageFlagBits::eComputeShader, {}, nullptr, toCompute, nullptr);

        if (!slot.computeSet) {
            vk::DescriptorSetAllocateInfo allocate{};
            allocate.descriptorPool     = descriptorPool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts        = &gpuParticleComputeSetLayout_;
            slot.computeSet             = device->allocateDescriptorSets(allocate).front();
        }
        std::array<vk::DescriptorBufferInfo, 5> infos{};
        std::array<vk::WriteDescriptorSet, 5>   writes{};
        updateStorageBinding(writes[0], infos[0], slot.computeSet, 0, previousState, stateBytes);
        updateStorageBinding(writes[1], infos[1], slot.computeSet, 1, slot.state.buffer, stateBytes);
        updateStorageBinding(writes[2], infos[2], slot.computeSet, 2, slot.spawns.buffer, stateBytes);
        updateStorageBinding(writes[3], infos[3], slot.computeSet, 3, previousMeta, metaBytes);
        updateStorageBinding(writes[4], infos[4], slot.computeSet, 4, slot.meta.buffer, metaBytes);
        device->updateDescriptorSets(writes, nullptr);

        ParticleComputePush push{};
        push.stepEmitterX[0] = resource.update.dt;
        push.stepEmitterX[1] = resource.update.emitterX;
        push.stepEmitterX[2] = resource.update.emitterY;
        push.stepEmitterX[3] = resource.update.gravityX;
        push.forces0[0]      = resource.update.gravityY;
        push.forces0[1]      = resource.update.damping;
        push.forces0[2]      = resource.update.velocityLimit;
        push.forces0[3]      = resource.update.noiseStrength;
        push.forces1[0]      = resource.update.noiseFrequency;
        push.forces1[1]      = resource.update.noiseSpeed;
        push.forces1[2]      = resource.update.time;
        push.forces1[3]      = resource.update.frameRate;
        push.localOffset[0]  = resource.update.localOffsetX;
        push.localOffset[1]  = resource.update.localOffsetY;
        push.capacity        = resource.capacity;
        push.spawnCount      = std::uint32_t(resource.spawns.size());

        cb.bindPipeline(vk::PipelineBindPoint::eCompute, gpuParticleComputePipeline_);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, gpuParticleComputeLayout_, 0, slot.computeSet, nullptr);
        cb.pushConstants(gpuParticleComputeLayout_, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
        cb.dispatch((resource.capacity + 63u) / 64u, 1, 1);

        std::array<vk::BufferMemoryBarrier, 2> afterCompute{};
        afterCompute[0].buffer        = slot.state.buffer;
        afterCompute[0].size          = VK_WHOLE_SIZE;
        afterCompute[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        afterCompute[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
        afterCompute[1].buffer        = slot.meta.buffer;
        afterCompute[1].size          = sizeof(vk::DrawIndirectCommand);
        afterCompute[1].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        afterCompute[1].dstAccessMask = vk::AccessFlagBits::eTransferRead;
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                           vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eTransfer, {}, nullptr,
                           afterCompute, nullptr);
        cb.copyBuffer(slot.meta.buffer, slot.indirect.buffer, vk::BufferCopy{0, 0, sizeof(vk::DrawIndirectCommand)});
        vk::BufferMemoryBarrier indirectReady{};
        indirectReady.buffer        = slot.indirect.buffer;
        indirectReady.size          = sizeof(vk::DrawIndirectCommand);
        indirectReady.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        indirectReady.dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead;
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eDrawIndirect, {}, nullptr,
                           indirectReady, nullptr);

        slot.initialized        = true;
        slot.serial             = ++resource.stats.submittedFrames;
        resource.lastOutputSlot = int(frameSlot % resource.slots.size());
        resource.pendingUpdate  = false;
        resource.pendingReset   = false;
        resource.spawns.clear();
    }
}

void Graphics::drawGpuParticleRequest(vk::CommandBuffer cb, const GpuParticleDrawRequest& request) {
    auto found = gpuParticles_.find(request.handle);
    if (found == gpuParticles_.end()) return;
    auto& resource = *found->second;
    if (resource.lastOutputSlot < 0) return;
    auto& slot = resource.slots[std::size_t(resource.lastOutputSlot)];
    if (!slot.state.buffer || !slot.meta.buffer) return;

    Texture* texture = request.draw.texture ? request.draw.texture : whiteTexture;
    if (!texture || !texture->gpuHandle) return;
    auto* gpuTexture = static_cast<GpuTexture*>(texture->gpuHandle);
    if (!slot.drawSet) {
        vk::DescriptorSetAllocateInfo allocate{};
        allocate.descriptorPool     = descriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts        = &gpuParticleDrawSetLayout_;
        slot.drawSet                = device->allocateDescriptorSets(allocate).front();
    }
    if (slot.drawTexture != gpuTexture) {
        vk::DescriptorImageInfo image{};
        image.sampler     = gpuTexture->sampler;
        image.imageView   = gpuTexture->imageView();
        image.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        vk::DescriptorBufferInfo state{};
        state.buffer = slot.state.buffer;
        state.range  = VK_WHOLE_SIZE;
        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet          = slot.drawSet;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo      = &image;
        writes[1].dstSet          = slot.drawSet;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo     = &state;
        device->updateDescriptorSets(writes, nullptr);
        slot.drawTexture = gpuTexture;
    }

    vk::Pipeline pipeline = gpuParticleAlphaPipeline_;
    switch (request.draw.blend) {
        case BlendMode::Additive: pipeline = gpuParticleAdditivePipeline_; break;
        case BlendMode::Premultiplied: pipeline = gpuParticlePremultipliedPipeline_; break;
        case BlendMode::Multiply: pipeline = gpuParticleMultiplyPipeline_; break;
        case BlendMode::Opaque: pipeline = gpuParticleOpaquePipeline_; break;
        case BlendMode::Alpha:
        default: break;
    }

    ParticleDrawPush push{};
    push.viewportCamera[0] = std::max(request.draw.viewportWidth, 1.f);
    push.viewportCamera[1] = std::max(request.draw.viewportHeight, 1.f);
    push.viewportCamera[2] = request.draw.cameraX;
    push.viewportCamera[3] = request.draw.cameraY;
    push.cameraParticle[0] = request.draw.cameraZoom > 0.f ? request.draw.cameraZoom : 1.f;
    push.cameraParticle[1] = request.draw.cameraEnabled ? 1.f : 0.f;
    push.cameraParticle[2] = request.draw.particleWidth;
    push.cameraParticle[3] = request.draw.particleHeight;
    push.sizeMode[0]       = request.draw.sizeStart;
    push.sizeMode[1]       = request.draw.sizeEnd;
    push.sizeMode[2]       = request.draw.stretchFactor;
    push.sizeMode[3]       = request.draw.stretched ? 1.f : 0.f;
    std::copy_n(request.draw.colorStart, 4, push.colorStart);
    std::copy_n(request.draw.colorEnd, 4, push.colorEnd);
    push.flipbook[0] = std::uint32_t(std::max(request.draw.hframes, 1));
    push.flipbook[1] = std::uint32_t(std::max(request.draw.vframes, 1));

    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, gpuParticleDrawLayout_, 0, slot.drawSet, nullptr);
    cb.pushConstants(gpuParticleDrawLayout_, vk::ShaderStageFlagBits::eVertex, 0, sizeof(push), &push);
    vkCmdDrawIndirect(static_cast<VkCommandBuffer>(cb), static_cast<VkBuffer>(slot.indirect.buffer), 0, 1,
                      sizeof(VkDrawIndirectCommand));
}

}  // namespace eve::graphics::vulkan
