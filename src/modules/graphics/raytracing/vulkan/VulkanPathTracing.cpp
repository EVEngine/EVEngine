#include "common/Profile.h"
// eve-hot-path
#include "graphics/raytracing/vulkan/VulkanRayTracing.h"

#include "common/Diagnostic.h"
#include "graphics/Canvas.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/raytracing/vulkan/PathTracingData.h"
#include "graphics/shaders/pt_denoise_spv.inc"
#include "graphics/shaders/pt_path_rahit_spv.inc"
#include "graphics/shaders/pt_path_rchit_spv.inc"
#include "graphics/shaders/pt_path_rgen_spv.inc"
#include "graphics/shaders/pt_path_rmiss_spv.inc"
#include "graphics/shaders/pt_temporal_spv.inc"
#include "graphics/vulkan/Canvas.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>
#include <string>

namespace eve::graphics::raytracing {

VulkanRayTracing::PathPipeline::~PathPipeline() {
    if (!device) return;
    sbt.release();
    if (temporalPipeline) (*device)->destroyPipeline(temporalPipeline, device->allocation_callbacks);
    if (temporalModule) (*device)->destroyShaderModule(temporalModule, device->allocation_callbacks);
    if (denoisePipeline) (*device)->destroyPipeline(denoisePipeline, device->allocation_callbacks);
    if (denoiseModule) (*device)->destroyShaderModule(denoiseModule, device->allocation_callbacks);
    if (pipeline) (*device)->destroyPipeline(pipeline, device->allocation_callbacks);
    if (layout) (*device)->destroyPipelineLayout(layout, device->allocation_callbacks);
    if (setLayout) (*device)->destroyDescriptorSetLayout(setLayout, device->allocation_callbacks);
    for (auto module : modules)
        if (module) (*device)->destroyShaderModule(module, device->allocation_callbacks);
}

Result<void> VulkanRayTracing::ensurePathPipeline() {
    if (pathPipeline_) return Result<void>::success();
    // Device binding belongs to the caller; no singleton lookup on this path.
    if (!device_ || !caps_.rayTracingAvailable())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Path tracing requires an attached RT device", "graphics.raytracing"));

    if (device_->physical_device.properties.limits.maxPerStageDescriptorStorageImages < 10)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Path reconstruction requires ten storage images per shader stage",
            "graphics.raytracing"));
    const auto handleSize      = caps_.shaderGroupHandleSize;
    const auto handleAlignment = caps_.shaderGroupHandleAlignment;
    const auto baseAlignment   = caps_.shaderGroupBaseAlignment;
    if (!handleSize || !handleAlignment || !baseAlignment)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "Invalid ray-tracing shader group properties", "graphics.raytracing"));

    // All intermediate handles belong to this candidate. A failed creation or
    // upload leaves the live backend unchanged and permits a subsequent retry.
    auto candidate    = std::make_unique<PathPipeline>();
    candidate->device = device_;
    try {
        const std::array<const uint32_t*, 4> code{pt_path_rgen_spv, pt_path_rmiss_spv, pt_path_rchit_spv,
                                                  pt_path_rahit_spv};
        const std::array<size_t, 4>          sizes{sizeof(pt_path_rgen_spv), sizeof(pt_path_rmiss_spv),
                                          sizeof(pt_path_rchit_spv), sizeof(pt_path_rahit_spv)};
        for (size_t i = 0; i < code.size(); ++i) {
            vk::ShaderModuleCreateInfo info{};
            info.codeSize         = sizes[i];
            info.pCode            = code[i];
            candidate->modules[i] = (*device_)->createShaderModule(info, device_->allocation_callbacks);
        }

        std::array<vk::DescriptorSetLayoutBinding, 18> bindings{};
        for (uint32_t i = 0; i < bindings.size(); ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute;
            if ((i >= 3 && i <= 6) || i == 10 || i == 12) bindings[i].stageFlags |= vk::ShaderStageFlagBits::eAnyHitKHR;
            bindings[i].descriptorType = i == 0 ? vk::DescriptorType::eAccelerationStructureKHR
                                         : ((i >= 3 && i <= 7) || i == 10 || i == 12)
                                             ? vk::DescriptorType::eStorageBuffer
                                             : vk::DescriptorType::eStorageImage;
        }
        vk::DescriptorSetLayoutCreateInfo setInfo{};
        setInfo.bindingCount = uint32_t(bindings.size());
        setInfo.pBindings    = bindings.data();
        candidate->setLayout = (*device_)->createDescriptorSetLayout(setInfo, device_->allocation_callbacks);
        vk::PushConstantRange        push{vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute, 0,
                                   sizeof(path::Constants)};
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount         = 1;
        layoutInfo.pSetLayouts            = &candidate->setLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &push;
        candidate->layout                 = (*device_)->createPipelineLayout(layoutInfo, device_->allocation_callbacks);
        vk::ShaderModuleCreateInfo denoiseCode{};
        denoiseCode.codeSize     = sizeof(pt_denoise_spv);
        denoiseCode.pCode        = pt_denoise_spv;
        candidate->denoiseModule = (*device_)->createShaderModule(denoiseCode, device_->allocation_callbacks);
        vk::ComputePipelineCreateInfo denoiseInfo{};
        denoiseInfo.stage =
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eCompute, candidate->denoiseModule, "main"};
        denoiseInfo.layout         = candidate->layout;
        auto denoiseCreated        = (*device_)->createComputePipeline({}, denoiseInfo, device_->allocation_callbacks);
        candidate->denoisePipeline = denoiseCreated.value;
        if (denoiseCreated.result != vk::Result::eSuccess || !candidate->denoisePipeline)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "Path denoise pipeline creation failed", "graphics.raytracing"));

        vk::ShaderModuleCreateInfo temporalCode{};
        temporalCode.codeSize       = sizeof(pt_temporal_spv);
        temporalCode.pCode          = pt_temporal_spv;
        candidate->temporalModule   = (*device_)->createShaderModule(temporalCode, device_->allocation_callbacks);
        denoiseInfo.stage.module    = candidate->temporalModule;
        auto temporalCreated        = (*device_)->createComputePipeline({}, denoiseInfo, device_->allocation_callbacks);
        candidate->temporalPipeline = temporalCreated.value;
        if (temporalCreated.result != vk::Result::eSuccess || !candidate->temporalPipeline)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "Path temporal pipeline creation failed", "graphics.raytracing"));

        const std::array<vk::ShaderStageFlagBits, 4> kinds{
            vk::ShaderStageFlagBits::eRaygenKHR, vk::ShaderStageFlagBits::eMissKHR,
            vk::ShaderStageFlagBits::eClosestHitKHR, vk::ShaderStageFlagBits::eAnyHitKHR};
        std::array<vk::PipelineShaderStageCreateInfo, 4>      stages{};
        std::array<vk::RayTracingShaderGroupCreateInfoKHR, 3> groups{};
        for (uint32_t i = 0; i < stages.size(); ++i) {
            stages[i].stage  = kinds[i];
            stages[i].module = candidate->modules[i];
            stages[i].pName  = "main";
        }
        for (uint32_t i = 0; i < groups.size(); ++i) {
            groups[i].type               = i == 2 ? vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup
                                                  : vk::RayTracingShaderGroupTypeKHR::eGeneral;
            groups[i].generalShader      = i == 2 ? VK_SHADER_UNUSED_KHR : i;
            groups[i].closestHitShader   = i == 2 ? i : VK_SHADER_UNUSED_KHR;
            groups[i].anyHitShader       = i == 2 ? 3 : VK_SHADER_UNUSED_KHR;
            groups[i].intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        vk::RayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.stageCount = uint32_t(stages.size());
        pipelineInfo.pStages    = stages.data();
        pipelineInfo.groupCount = uint32_t(groups.size());
        pipelineInfo.pGroups    = groups.data();
        // Bounces are iterative in raygen; neither hit nor miss traces recursively.
        pipelineInfo.maxPipelineRayRecursionDepth = 1;
        pipelineInfo.layout                       = candidate->layout;
        auto created = (*device_)->createRayTracingPipelinesKHR(vk::DeferredOperationKHR{}, vk::PipelineCache{},
                                                                pipelineInfo, device_->allocation_callbacks);
        if (!created.value.empty()) candidate->pipeline = created.value.front();
        if (created.result != vk::Result::eSuccess || !candidate->pipeline)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "Path-tracing pipeline creation failed", "graphics.raytracing"));

        // Vulkan returns tightly packed handles, independent of SBT stride.
        std::vector<uint8_t> handles(size_t(handleSize) * 3);
        auto                 result =
            (*device_)->getRayTracingShaderGroupHandlesKHR(candidate->pipeline, 0, 3, handles.size(), handles.data());
        if (result != vk::Result::eSuccess)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "Path-tracing shader group retrieval failed", "graphics.raytracing"));
        const auto align = [](vk::DeviceSize value, vk::DeviceSize alignment) {
            return (value + alignment - 1) / alignment * alignment;
        };
        const auto stride        = align(handleSize, handleAlignment);
        const auto regionSpacing = align(stride, baseAlignment);
        // The allocation's device address is not assumed to have SBT base alignment.
        const auto allocationSize = regionSpacing * 3 + baseAlignment - 1;
        if (auto r = candidate->sbt.allocate(
                *device_, allocationSize,
                vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eTransferDst);
            !r.ok())
            return r;
        const auto           start = align(candidate->sbt.address, baseAlignment) - candidate->sbt.address;
        std::vector<uint8_t> bytes(size_t(allocationSize), 0);
        for (size_t i = 0; i < 3; ++i) {
            const auto offset = start + i * regionSpacing;
            std::memcpy(bytes.data() + offset, handles.data() + i * handleSize, handleSize);
            candidate->regions[i] = vk::StridedDeviceAddressRegionKHR{candidate->sbt.address + offset, stride, stride};
        }
        if (auto r = candidate->sbt.upload(uploadPool_, graphicsQueue_, bytes.data(), allocationSize); !r.ok())
            return r;
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Path-tracing pipeline: ") + error.what(), "graphics.raytracing"));
    }
    pathPipeline_ = std::move(candidate);
    return Result<void>::success();
}

struct VulkanRayTracing::PathFrame::OutputTextures {
    vkb::Device*                                     device = nullptr;
    vk::DescriptorPool                               pool{};
    vk::Sampler                                      sampler{};
    std::array<eve::graphics::vulkan::GpuTexture, 2> gpu;
    std::array<Texture, 2>                           textures;
    ~OutputTextures() {
        if (!device) return;
        if (pool) (*device)->destroyDescriptorPool(pool, device->allocation_callbacks);
        if (sampler) (*device)->destroySampler(sampler, device->allocation_callbacks);
    }
};

VulkanRayTracing::PathFrame::~PathFrame() {
    if (device && pool) (*device)->destroyDescriptorPool(pool, device->allocation_callbacks);
}

Result<void> VulkanRayTracing::ensurePathFrame(uint32_t width, uint32_t height) {
    if (!width || !height)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Path-tracing extent must be nonzero", "graphics.raytracing"));
    if (auto r = ensurePathPipeline(); !r.ok()) return r;
    if (pathFrame_ && pathFrame_->width == width && pathFrame_->height == height) return Result<void>::success();
    if (pathFrameGeneration_ == UINT64_MAX)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Path frame identity exhausted", "graphics.raytracing"));

    auto candidate    = std::make_unique<PathFrame>();
    candidate->device = device_;
    candidate->width  = width;
    candidate->height = height;
    try {
        for (size_t i = 0; i < candidate->images.size(); ++i) {
            vk::ImageCreateInfo info{};
            info.imageType   = vk::ImageType::e2D;
            info.format      = (i == 0 || i == 2 || (i >= 5 && i <= 8)) ? vk::Format::eR32G32B32A32Sfloat
                                                                        : vk::Format::eR16G16B16A16Sfloat;
            info.extent      = vk::Extent3D{width, height, 1};
            info.mipLevels   = 1;
            info.arrayLayers = 1;
            info.samples     = vk::SampleCountFlagBits::e1;
            info.tiling      = vk::ImageTiling::eOptimal;
            info.usage       = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                         vk::ImageUsageFlagBits::eTransferSrc;
            info.sharingMode   = vk::SharingMode::eExclusive;
            info.initialLayout = vk::ImageLayout::eUndefined;
            candidate->images[i].create(*device_, info, vk::ImageViewType::e2D, vk::ImageAspectFlagBits::eColor, false);
        }
        candidate->output = std::make_unique<PathFrame::OutputTextures>();
        auto& output      = *candidate->output;
        output.device     = device_;
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter    = vk::Filter::eNearest;
        samplerInfo.minFilter    = vk::Filter::eNearest;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW =
            vk::SamplerAddressMode::eClampToEdge;
        output.sampler = (*device_)->createSampler(samplerInfo, device_->allocation_callbacks);
        vk::DescriptorPoolSize       sampledSize{vk::DescriptorType::eCombinedImageSampler, 10};
        vk::DescriptorPoolCreateInfo sampledPool{};
        sampledPool.maxSets       = 2;
        sampledPool.poolSizeCount = 1;
        sampledPool.pPoolSizes    = &sampledSize;
        output.pool               = (*device_)->createDescriptorPool(sampledPool, device_->allocation_callbacks);
        std::array<vk::DescriptorSetLayout, 2> sampledLayouts{pathGraphics_->getTexSetLayout(),
                                                              pathGraphics_->getTexSetLayout()};
        vk::DescriptorSetAllocateInfo          sampledAllocate{output.pool, 2, sampledLayouts.data()};
        const auto                             sampledSets = (*device_)->allocateDescriptorSets(sampledAllocate);
        for (size_t i = 0; i < 2; ++i) {
            auto& gpu         = output.gpu[i];
            gpu.width         = int(width);
            gpu.height        = int(height);
            gpu.sampler       = output.sampler;
            gpu.sharedSampler = true;
            gpu.viewOverride  = candidate->images[i == 0 ? 1 : 4].imageView();
            vkb::UnboundSet           unbound{sampledSets[i]};
            vkb::DescriptorSetUpdater updater;
            updater.beginDescriptorSet(unbound);
            for (uint32_t binding = 0; binding < 5; ++binding)
                updater.beginImages(binding, 0, vk::DescriptorType::eCombinedImageSampler)
                    .image(
                        vkb::SampledImage{output.sampler, gpu.viewOverride, vk::ImageLayout::eShaderReadOnlyOptimal});
            updater.update(device_->instance);
            gpu.descriptorSet = std::move(unbound).publish();
            auto& texture     = output.textures[i];
            texture.width = texture.pixelWidth = int(width);
            texture.height = texture.pixelHeight = int(height);
            texture.gpuHandle                    = &gpu;
        }
        const std::array<vk::DescriptorPoolSize, 3> sizes{{{vk::DescriptorType::eAccelerationStructureKHR, 2},
                                                           {vk::DescriptorType::eStorageBuffer, 14},
                                                           {vk::DescriptorType::eStorageImage, 20}}};
        vk::DescriptorPoolCreateInfo                poolInfo{};
        poolInfo.maxSets       = 2;
        poolInfo.poolSizeCount = uint32_t(sizes.size());
        poolInfo.pPoolSizes    = sizes.data();
        candidate->pool        = (*device_)->createDescriptorPool(poolInfo, device_->allocation_callbacks);
        vk::DescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.descriptorPool = candidate->pool;
        const std::array<vk::DescriptorSetLayout, 2> layouts{pathPipeline_->setLayout, pathPipeline_->setLayout};
        allocateInfo.descriptorSetCount = uint32_t(layouts.size());
        allocateInfo.pSetLayouts        = layouts.data();
        const auto sets                 = (*device_)->allocateDescriptorSets(allocateInfo);
        std::copy(sets.begin(), sets.end(), candidate->sets.begin());
        // Size changes retire all history/guide images as one allocation set.
        if (pathFrame_) graphicsQueue_.waitIdle();
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Path-tracing frame: ") + error.what(), "graphics.raytracing"));
    }
    if (pathFrame_) pathGraphics_->retirePathSceneOutput();
    pathFrame_ = std::move(candidate);
    ++pathFrameGeneration_;
    return Result<void>::success();
}

Result<void> VulkanRayTracing::recordPathDispatch(vk::CommandBuffer command, vk::AccelerationStructureKHR scene,
                                                  const std::array<vk::DescriptorBufferInfo, 7>& buffers,
                                                  const path::Constants& constants, const PathTraceView& view,
                                                  uint32_t width, uint32_t height) {
    if (!command || !scene || !constants.sampling.y)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Path tracing needs a command, scene and samples", "graphics.raytracing"));
    for (const auto& buffer : buffers)
        if (!buffer.buffer || !buffer.range)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Path-tracing scene buffer is empty", "graphics.raytracing"));
    const bool sameFrame = pathFrame_ && pathFrame_->width == width && pathFrame_->height == height;
    const bool sameScene = sameFrame && pathFrame_->scene == scene && pathFrame_->buffers == buffers;
    if (constants.counts.y && (!sameScene || !pathFrame_->initialized))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Path-tracing history must reset after scene or size changes",
                                                       "graphics.raytracing"));
    if (auto r = ensurePathFrame(width, height); !r.ok()) return r;
    auto&          frame         = *pathFrame_;
    const uint32_t slot          = view.denoise == PathDenoise::Temporal ? frame.nextTemporalSlot : 0u;
    const auto     descriptorSet = frame.sets[slot];
    if (!sameScene) {
        try {
            // Stable scene descriptors are reused without per-frame queue waits.
            // The caller must submit the previous recording before replacing a scene.
            if (frame.initialized) graphicsQueue_.waitIdle();
            vk::WriteDescriptorSetAccelerationStructureKHR acceleration{};
            acceleration.accelerationStructureCount = 1;
            acceleration.pAccelerationStructures    = &scene;
            std::array<vk::DescriptorImageInfo, 10> images{};
            for (size_t i = 0; i < images.size(); ++i) {
                images[i].imageView   = frame.images[i].imageView();
                images[i].imageLayout = vk::ImageLayout::eGeneral;
            }
            // Both sets are immutable while frames are in flight. Set 1 reverses
            // current/history roles for radiance, normal, albedo and position.
            constexpr std::array<size_t, 10> reversed{5, 1, 8, 9, 4, 0, 7, 6, 2, 3};
            for (size_t setIndex = 0; setIndex < frame.sets.size(); ++setIndex) {
                std::array<vk::WriteDescriptorSet, 18> writes{};
                for (uint32_t i = 0; i < writes.size(); ++i) {
                    writes[i].dstSet          = frame.sets[setIndex];
                    writes[i].dstBinding      = i;
                    writes[i].descriptorCount = 1;
                    if (i == 0) {
                        writes[i].descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
                        writes[i].pNext          = &acceleration;
                    } else if ((i >= 3 && i <= 7) || i == 10 || i == 12) {
                        writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
                        writes[i].pBufferInfo    = &buffers[i == 12 ? 6 : (i == 10 ? 5 : i - 3)];
                    } else {
                        writes[i].descriptorType = vk::DescriptorType::eStorageImage;
                        const size_t imageIndex  = i >= 13 ? i - 8 : (i == 11 ? 4 : (i <= 2 ? i - 1 : i - 6));
                        writes[i].pImageInfo     = &images[setIndex == 0 ? imageIndex : reversed[imageIndex]];
                    }
                }
                (*device_)->updateDescriptorSets(writes, {});
            }
            frame.scene   = scene;
            frame.buffers = buffers;
        } catch (const std::exception& error) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                           std::string("Path-tracing descriptors: ") + error.what(),
                                                           "graphics.raytracing"));
        }
    }

    std::array<vk::ImageMemoryBarrier, 10> barriers{};
    for (size_t i = 0; i < barriers.size(); ++i) {
        auto& barrier = barriers[i];
        barrier.srcAccessMask =
            frame.initialized ? vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite : vk::AccessFlags{};
        barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        barrier.oldLayout           = !frame.initialized
                                          ? vk::ImageLayout::eUndefined
                                          : (frame.sampledOutput && (i == 1 || i == 4) ? vk::ImageLayout::eShaderReadOnlyOptimal
                                                                                       : vk::ImageLayout::eGeneral);
        barrier.newLayout           = vk::ImageLayout::eGeneral;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = frame.images[i].image();
        barrier.subresourceRange    = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    }
    // This also orders scene upload/build writes before shader and AS reads.
    vk::MemoryBarrier sceneBarrier{vk::AccessFlagBits::eMemoryWrite,
                                   vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite |
                                       vk::AccessFlagBits::eAccelerationStructureReadKHR};
    command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                            vk::PipelineStageFlagBits::eRayTracingShaderKHR | vk::PipelineStageFlagBits::eComputeShader,
                            {}, sceneBarrier, {}, barriers);
    command.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, pathPipeline_->pipeline);
    command.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, pathPipeline_->layout, 0, descriptorSet, {});
    command.pushConstants(pathPipeline_->layout,
                          vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants),
                          &constants);
    const auto& regions = pathPipeline_->regions;
    command.traceRaysKHR(regions[0], regions[1], regions[2], regions[3], width, height, 1);
    vk::MemoryBarrier outputBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eMemoryRead};
    command.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR, vk::PipelineStageFlagBits::eAllCommands,
                            {}, outputBarrier, {}, {});
    if (view.denoise == PathDenoise::Temporal) {
        const auto& previous = pathPreviousView_;
        const bool  valid = sameScene && frame.initialized && pathHistorySamples_ != 0 && !view.resetTemporalHistory &&
                           previous.denoise == PathDenoise::Temporal &&
                           view.firstSample == previous.firstSample + previous.samples &&
                           view.environment == previous.environment && view.seed == previous.seed &&
                           view.maxBounces == previous.maxBounces;
        path::TemporalConstants temporal;
        if (valid) temporal.previousViewProj = glm::inverse(previous.invViewProj);
        temporal.previousEye       = glm::vec4(previous.eye, 0.f);
        temporal.currentEye        = glm::vec4(view.eye, 0.f);
        temporal.parameters        = {3.f, .9f, .01f, .99f};
        const bool identicalCamera = valid && view.invViewProj == previous.invViewProj && view.eye == previous.eye;
        temporal.counts = {uint32_t(valid) | (identicalCamera ? 2u : 0u), view.samples, uint32_t(PathDenoise::Temporal),
                           32u * std::min(view.samples, 16u)};
        command.bindPipeline(vk::PipelineBindPoint::eCompute, pathPipeline_->temporalPipeline);
        command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pathPipeline_->layout, 0, descriptorSet, {});
        command.pushConstants(pathPipeline_->layout,
                              vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eCompute, 0,
                              sizeof(temporal), &temporal);
        command.dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
        vk::MemoryBarrier historyReady{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                {}, historyReady, {}, {});
    }
    if (constants.counts.z != 0u) {
        command.bindPipeline(vk::PipelineBindPoint::eCompute, pathPipeline_->denoisePipeline);
        command.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pathPipeline_->layout, 0, descriptorSet, {});
        command.dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eAllCommands, {},
                                outputBarrier, {}, {});
    }
    if (view.denoise == PathDenoise::Temporal) frame.nextTemporalSlot = slot ^ 1u;
    frame.initialized   = true;
    frame.sampledOutput = false;
    return Result<void>::success();
}

namespace {
bool finiteVector(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool finiteMatrix(const glm::mat4& value) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(value[column][row])) return false;
    return true;
}

// Ephemeral, padding-free value encoding for exact cache comparison. No digest
// collision can hide a geometry/material/light change; signed zero is canonical.
struct NativePathTexture {
    eve::graphics::vulkan::GpuTexture* gpu      = nullptr;  // Borrowed only during preparation.
    uint64_t                           identity = 0;
    uint32_t                           width = 0, height = 0, flags = 0;
};
// Private texture flags shared with pt_surface.glsl: packed RGBA8, repeat U/V,
// linear magnification, sRGB decode, premultiplied input.
constexpr uint32_t packedTexture = 1, repeatTextureU = 2, repeatTextureV = 4, linearTexture = 8, srgbTexture = 16,
                   premultipliedTexture = 32, mirrorTextureU = 64, mirrorTextureV = 128;
uint32_t textureSamplerFlags(const PathColorTexture& texture, uint32_t flags) {
    if (texture.filter != PathTextureFilter::Source) {
        flags &= ~linearTexture;
        if (texture.filter == PathTextureFilter::Linear) flags |= linearTexture;
    }
    const auto address = [&](PathTextureAddress mode, uint32_t repeat, uint32_t mirror) {
        if (mode == PathTextureAddress::Source) return;
        flags &= ~(repeat | mirror);
        if (mode == PathTextureAddress::Repeat) flags |= repeat;
        if (mode == PathTextureAddress::Mirror) flags |= mirror;
    };
    address(texture.addressU, repeatTextureU, mirrorTextureU);
    address(texture.addressV, repeatTextureV, mirrorTextureV);
    return flags;
}

struct SceneWords {
    std::vector<uint32_t> values;
    void                  scalar(float value) { values.push_back(std::bit_cast<uint32_t>(value == 0.f ? 0.f : value)); }
    void                  vector(const glm::vec3& value) {
        scalar(value.x);
        scalar(value.y);
        scalar(value.z);
    }
    void count(size_t size) { values.push_back(uint32_t(size)); }
};

std::vector<uint32_t> instanceWords(std::span<const PathInstance> instances) {
    SceneWords key;
    key.values.reserve(1 + instances.size() * 17);
    key.count(instances.size());
    for (const auto& instance : instances) {
        key.values.push_back(instance.geometry);
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row) key.scalar(instance.transform[column][row]);
    }
    return std::move(key.values);
}

std::vector<uint32_t> sceneWords(const PathSceneDescription& scene, std::span<const NativePathTexture> native) {
    SceneWords key;
    key.count(scene.geometry.size());
    for (const auto& geometry : scene.geometry) {
        key.values.push_back(geometry.material);
        key.count(geometry.positions.size());
        for (const auto& value : geometry.positions) key.vector(value);
        key.count(geometry.normals.size());
        for (const auto& value : geometry.normals) key.vector(value);
        key.count(geometry.texcoords.size());
        for (const auto& value : geometry.texcoords) {
            key.scalar(value.x);
            key.scalar(value.y);
        }
        key.count(geometry.tangents.size());
        for (const auto& tangent : geometry.tangents) key.vector(tangent);
        key.count(geometry.bitangents.size());
        for (const auto& bitangent : geometry.bitangents) key.vector(bitangent);
        key.count(geometry.indices.size());
        key.values.insert(key.values.end(), geometry.indices.begin(), geometry.indices.end());
    }
    key.count(scene.materials.size());
    for (const auto& material : scene.materials) {
        key.vector(material.baseColor);
        key.vector(material.emission);
        key.scalar(material.roughness);
        key.scalar(material.metallic);
        key.values.push_back(material.baseColorTexture);
        key.values.push_back(material.metallicRoughnessTexture);
        key.values.push_back(material.emissionTexture);
        key.values.push_back(material.normalTexture);
        key.scalar(material.normalScale);
        key.scalar(material.opacity);
        key.scalar(material.alphaCutoff);
        key.values.push_back(uint32_t(material.alphaMode));
    }
    key.count(scene.lights.size());
    for (const auto& light : scene.lights) {
        key.values.push_back(uint32_t(light.kind));
        key.vector(light.position);
        key.vector(light.directionToLight);
        key.vector(light.strength);
        key.vector(light.beamDirection);
        key.scalar(light.cosOuter);
        key.scalar(light.cosInner);
    }
    key.count(scene.colorTextures.size());
    for (size_t index = 0; index < scene.colorTextures.size(); ++index) {
        const auto& texture = scene.colorTextures[index];
        const auto& source  = native[index];
        key.values.push_back(source.gpu ? 1u : 0u);
        if (source.gpu) {
            key.values.insert(key.values.end(), {uint32_t(source.identity), uint32_t(source.identity >> 32),
                                                 source.width, source.height, source.flags});
        }
        key.scalar(texture.offset.x);
        key.scalar(texture.offset.y);
        key.scalar(texture.scale.x);
        key.scalar(texture.scale.y);
        key.scalar(texture.rotation);
        key.values.insert(key.values.end(),
                          {uint32_t(texture.filter), uint32_t(texture.addressU), uint32_t(texture.addressV)});
        key.values.push_back(texture.width);
        key.values.push_back(texture.height);
        key.count(texture.pixels.size());
        for (const auto& pixel : texture.pixels) key.vector(pixel);
        key.count(texture.alpha.size());
        for (float alpha : texture.alpha) key.scalar(alpha);
    }
    return std::move(key.values);
}
}  // namespace

Result<void> VulkanRayTracing::buildPathInstances(std::span<const PathInstance> input, const PathScene& source,
                                                  PathScene& destination) {
    EV_PROFILE_MODULE("graphics", "Path.BuildInstances");
    const auto invalid = [](const char* message) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "graphics.raytracing"));
    };
    if (input.empty() || input.size() > 0xffffffu) return invalid("Invalid path instance count");
    std::vector<path::Instance> instances;
    std::vector<path::Light>    lights;
    for (const auto& light : source.lights) {
        path::Light packed;
        packed.p0Area = glm::vec4(
            light.kind == PathLightKind::Directional ? glm::normalize(light.directionToLight) : light.position, 0.f);
        packed.p1Kind.w = light.kind == PathLightKind::Directional ? 2.f : 1.f;
        if (light.kind == PathLightKind::Spot) {
            packed.p1Kind = glm::vec4(glm::normalize(light.beamDirection), 3.f);
            packed.p2     = glm::vec4(light.cosOuter, light.cosInner, 0.f, 0.f);
        }
        packed.emission = glm::vec4(light.strength, 0.f);
        lights.push_back(packed);
    }
    std::vector<vk::AccelerationStructureInstanceKHR> accelerationInstances;
    instances.reserve(input.size());
    accelerationInstances.reserve(input.size());
    for (const auto& instance : input) {
        const auto& transform   = instance.transform;
        const float determinant = glm::determinant(transform);
        if (instance.geometry >= source.geometry.size() || !finiteMatrix(transform) || transform[0][3] != 0.f ||
            transform[1][3] != 0.f || transform[2][3] != 0.f || transform[3][3] != 1.f || !std::isfinite(determinant) ||
            std::abs(determinant) < 1e-12f)
            return invalid("Path instance needs a valid geometry and invertible affine transform");
        const auto&    geometry = source.geometry[instance.geometry];
        path::Instance packed;
        packed.objectToWorld = transform;
        packed.normalToWorld = glm::transpose(glm::inverse(transform));
        if (!finiteMatrix(packed.normalToWorld)) return invalid("Non-finite path normal transform");
        packed.offsets = {geometry.firstTriangle, geometry.firstVertex, uint32_t(lights.size()), 0u};
        vk::AccelerationStructureInstanceKHR acceleration{};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                acceleration.transform.matrix[row][column] = transform[column][row];
        acceleration.instanceCustomIndex = uint32_t(instances.size());
        acceleration.mask                = 0xff;
        acceleration.flags               = uint32_t(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
        if (geometry.alphaCoverage) acceleration.flags |= uint32_t(vk::GeometryInstanceFlagBitsKHR::eForceNoOpaque);
        acceleration.accelerationStructureReference = source.meshes[instance.geometry]->blas.deviceAddress;
        accelerationInstances.push_back(acceleration);
        instances.push_back(packed);
        for (const auto& emitter : geometry.emitters) {
            if (lights.size() >= UINT32_MAX - 1u) return invalid("Too many emissive path triangles");
            const glm::vec3 a    = transform * glm::vec4(emitter.a, 1.f);
            const glm::vec3 b    = transform * glm::vec4(emitter.b, 1.f);
            const glm::vec3 c    = transform * glm::vec4(emitter.c, 1.f);
            const float     area = glm::length(glm::cross(b - a, c - a)) * 0.5f;
            if (!std::isfinite(area) || area <= 0.f) return invalid("Invalid transformed emissive triangle area");
            path::Light light;
            light.p0Area   = glm::vec4(a, area);
            light.p1Kind   = glm::vec4(b, 0.f);
            light.p2       = glm::vec4(c, std::bit_cast<float>(emitter.primitive));
            light.emission = glm::vec4(emitter.emission, std::bit_cast<float>(uint32_t(instances.size() - 1)));
            lights.push_back(light);
        }
    }
    destination.lightCount = uint32_t(lights.size());
    if (lights.empty()) lights.emplace_back();
    const auto upload = [&](size_t slot, const auto& values) -> Result<void> {
        const auto bytes  = values.size() * sizeof(values[0]);
        auto&      buffer = destination.buffers[slot];
        return buffer.initializeHostVisible(*device_, values.data(), bytes, vk::BufferUsageFlagBits::eStorageBuffer);
    };
    if (auto r = upload(2, instances); !r.ok()) return r;
    if (auto r = upload(4, lights); !r.ok()) return r;
    return buildTlas(accelerationInstances, destination.tlas, destination.workspace);
}

Result<PathSceneToken> VulkanRayTracing::updatePathInstances(PathSceneToken                token,
                                                             std::span<const PathInstance> instances) {
    if (!pathGraphics_ || pathGraphics_->hasOpenPresentFrame())
        return Result<PathSceneToken>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Update path instances outside frame recording", "graphics.raytracing"));
    if (!pathScene_ || !device_ || token.generation != pathScene_->generation || instances.empty() ||
        instances.size() > 0xffffffu || pathGeneration_ == std::numeric_limits<uint64_t>::max())
        return Result<PathSceneToken>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid path scene identity for instance update", "graphics.raytracing"));
    try {
        return updatePathInstancesPrepared(instances, instanceWords(instances));
    } catch (const std::exception& error) {
        return Result<PathSceneToken>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Path instance key: ") + error.what(), "graphics.raytracing"));
    }
}

Result<PathSceneToken> VulkanRayTracing::updatePathInstancesPrepared(std::span<const PathInstance> instances,
                                                                     std::vector<uint32_t>         newInstanceWords) {
    auto candidate = pathSpareScene_ ? std::move(pathSpareScene_) : std::make_unique<PathScene>();
    try {
        if (auto r = buildPathInstances(instances, *pathScene_, *candidate); !r.ok())
            return Result<PathSceneToken>::failure(r.status());
    } catch (const std::exception& error) {
        return Result<PathSceneToken>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Path instance update: ") + error.what(), "graphics.raytracing"));
    }
    // Transfer immutable geometry only after every new GPU resource is ready.
    // The TLAS build has completed, retiring all prior uses on this queue.
    candidate->geometry = std::move(pathScene_->geometry);
    candidate->lights   = std::move(pathScene_->lights);
    candidate->meshes   = std::move(pathScene_->meshes);
    for (const size_t slot : {size_t(0), size_t(1), size_t(3), size_t(5), size_t(6)})
        candidate->buffers[slot] = std::move(pathScene_->buffers[slot]);
    candidate->generation = ++pathGeneration_;
    pathSpareScene_       = std::move(pathScene_);
    pathScene_            = std::move(candidate);
    pathInstanceWords_    = std::move(newInstanceWords);
    pathHistorySamples_   = 0;
    // Keep image allocations, but force descriptor refresh even if driver handles
    // are recycled through several updates without intervening draws.
    if (pathFrame_) pathFrame_->scene = vk::AccelerationStructureKHR{};
    return Result<PathSceneToken>::success(PathSceneToken{pathGeneration_});
}

Result<PathSceneToken> VulkanRayTracing::preparePathScene(Graphics* gfx, const PathSceneDescription& scene) {
    EV_PROFILE_MODULE("graphics", "Path.PrepareScene");
    const auto invalid = [](const char* message) {
        return Result<PathSceneToken>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "graphics.raytracing"));
    };
    auto* graphics = dynamic_cast<eve::graphics::vulkan::Graphics*>(gfx);
    if (!graphics || !graphics->supportsRayTracing())
        return Result<PathSceneToken>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Path scene requires Vulkan hardware ray tracing", "graphics.raytracing"));
    if (graphics->hasOpenPresentFrame()) return invalid("Prepare path scenes outside frame recording");
    reconcilePathRecording();
    std::vector<NativePathTexture> nativeTextures(scene.colorTextures.size());
    try {
        if (std::any_of(scene.colorTextures.begin(), scene.colorTextures.end(),
                        [](const PathColorTexture& texture) { return texture.sourceTexture != nullptr; }))
            graphics->ensureFileTexturesReady();
        for (size_t i = 0; i < scene.colorTextures.size(); ++i) {
            const auto& texture = scene.colorTextures[i];
            if (!std::isfinite(texture.offset.x) || !std::isfinite(texture.offset.y) ||
                !std::isfinite(texture.scale.x) || !std::isfinite(texture.scale.y) || !std::isfinite(texture.rotation))
                return invalid("Path texture UV transform must be finite");
            if (uint32_t(texture.filter) > uint32_t(PathTextureFilter::Linear) ||
                uint32_t(texture.addressU) > uint32_t(PathTextureAddress::Mirror) ||
                uint32_t(texture.addressV) > uint32_t(PathTextureAddress::Mirror))
                return invalid("Invalid path texture sampler override");
            if (!texture.sourceTexture) continue;
            if (texture.width || texture.height || !texture.pixels.empty() || !texture.alpha.empty())
                return invalid("Path texture cannot mix engine and pixel-span sources");
            auto identity = graphics->textureContentIdentity(texture.sourceTexture);
            if (!identity.ok()) return Result<PathSceneToken>::failure(identity.status());
            auto* gpu = static_cast<eve::graphics::vulkan::GpuTexture*>(texture.sourceTexture->gpuHandle);
            if (gpu->width <= 0 || gpu->height <= 0 ||
                gpu->image.currentLayout() != vk::ImageLayout::eShaderReadOnlyOptimal)
                return invalid("Path source texture must have valid dimensions and a sampled layout");
            const auto&    sampler = gpu->samplerState;
            const uint32_t flags =
                packedTexture | (sampler.repeatU ? repeatTextureU : 0u) | (sampler.repeatV ? repeatTextureV : 0u) |
                (sampler.mag == FilterMode::Linear ? linearTexture : 0u) | (texture.srgbDecode ? srgbTexture : 0u) |
                (texture.sourceTexture->hasPremultipliedAlpha() ? premultipliedTexture : 0u);
            nativeTextures[i] = {gpu, identity.value(), uint32_t(gpu->width), uint32_t(gpu->height), flags};
        }
    } catch (const std::exception& error) {
        return Result<PathSceneToken>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Path source texture: ") + error.what(), "graphics.raytracing"));
    }
    if (std::any_of(scene.geometry.begin(), scene.geometry.end(), [](const PathGeometry& g) { return g.sourceMesh; })) {
        struct Captured {
            std::vector<glm::vec3> positions, normals;
            std::vector<glm::vec2> uv;
            std::vector<glm::vec3> tangents, bitangents;
            std::vector<uint32_t>  indices;
        };
        try {
            // Generation identities are globally unique; never retain borrowed mesh pointers.
            bool                  cacheable = true;
            std::vector<uint32_t> meshWords;
            for (const auto& geometry : scene.geometry) {
                auto* mesh = geometry.sourceMesh;
                if (!mesh) {
                    cacheable = false;
                    continue;
                }
                if (!geometry.positions.empty() || !geometry.normals.empty() || !geometry.indices.empty() ||
                    !geometry.texcoords.empty() || !geometry.tangents.empty() || !geometry.bitangents.empty())
                    return invalid("Path geometry cannot mix mesh and span sources");
                auto description = graphics->describeMesh(mesh);
                if (!description.ok()) return Result<PathSceneToken>::failure(description.status());
                auto* gpu = static_cast<eve::graphics::vulkan::GpuMesh*>(mesh->gpuHandle);
                if (mesh->hasGpuSkinning() || gpu->residentVertexOverride)
                    return Result<PathSceneToken>::failure(
                        Diagnostic::error(DiagnosticCode::Unsupported,
                                          "Path mesh deformation snapshot is not available", "graphics.raytracing"));
                if (!mesh->geometryGeneration() || !mesh->tangentFrameRevision()) cacheable = false;
                for (uint64_t revision : {mesh->geometryGeneration(), mesh->texcoordRevision(),
                                          mesh->tangentFrameRevision(), uint64_t(gpu->dynamicWriteCount)}) {
                    meshWords.push_back(uint32_t(revision));
                    meshWords.push_back(uint32_t(revision >> 32));
                }
                meshWords.push_back(gpu->vertexCount);
                meshWords.push_back(gpu->indexCount);
                meshWords.push_back(uint32_t(gpu->indexType));
            }
            if (cacheable) {
                auto values = sceneWords(scene, nativeTextures);
                meshWords.insert(meshWords.end(), values.begin(), values.end());
                if (pathScene_ && pathGraphics_ == graphics && meshWords == pathMeshSceneWords_) {
                    auto instanceKey = instanceWords(scene.instances);
                    if (instanceKey == pathInstanceWords_)
                        return Result<PathSceneToken>::success(PathSceneToken{pathGeneration_});
                    if (pathGeneration_ == UINT64_MAX) return invalid("Path scene identity exhausted");
                    return updatePathInstancesPrepared(scene.instances, std::move(instanceKey));
                }
            }
            std::vector<Captured>     owned(scene.geometry.size());
            std::vector<PathGeometry> resolved(scene.geometry.begin(), scene.geometry.end());
            graphics->waitForSharedGpuResources();
            for (size_t i = 0; i < resolved.size(); ++i) {
                auto& geometry = resolved[i];
                auto* mesh     = geometry.sourceMesh;
                if (!mesh) continue;
                if (!geometry.positions.empty() || !geometry.normals.empty() || !geometry.indices.empty() ||
                    !geometry.texcoords.empty() || !geometry.tangents.empty() || !geometry.bitangents.empty())
                    return invalid("Path geometry cannot mix mesh and span sources");
                auto description = graphics->describeMesh(mesh);
                if (!description.ok()) return Result<PathSceneToken>::failure(description.status());
                auto* gpu = static_cast<eve::graphics::vulkan::GpuMesh*>(mesh->gpuHandle);
                if (gpu->vertexCount < 3 || gpu->indexCount < 3 || gpu->indexCount % 3)
                    return invalid("Invalid engine mesh geometry for path tracing");
                if (mesh->hasGpuSkinning() || gpu->residentVertexOverride)
                    return Result<PathSceneToken>::failure(
                        Diagnostic::error(DiagnosticCode::Unsupported,
                                          "Path mesh deformation snapshot is not available", "graphics.raytracing"));
                auto& copy = owned[i];
                copy.positions.resize(gpu->vertexCount);
                copy.normals.resize(gpu->vertexCount);
                copy.uv.resize(gpu->vertexCount);
                copy.indices.resize(gpu->indexCount);
                auto&       vertices = eve::graphics::vulkan::meshDrawVertices(*gpu);
                const auto* data     = static_cast<const eve::graphics::vulkan::MeshVertex*>(vertices.map());
                for (size_t v = 0; v < copy.positions.size(); ++v) {
                    copy.positions[v] = data[v].pos;
                    copy.normals[v]   = data[v].normal;
                    copy.uv[v]        = data[v].uv;
                }
                vertices.unmap();
                const auto uv0 = mesh->texcoordSet(0);
                if (!uv0.empty()) {
                    if (uv0.size() != size_t(gpu->vertexCount) * 2)
                        return invalid("Path UV0 override must match mesh vertices");
                    for (size_t v = 0; v < copy.uv.size(); ++v) copy.uv[v] = {uv0[v * 2], uv0[v * 2 + 1]};
                }
                if (mesh->hasImportedTangents()) {
                    const auto& t = mesh->importedTangents();
                    const auto& b = mesh->importedBitangents();
                    if (t.size() != size_t(gpu->vertexCount) * 3 || b.size() != t.size())
                        return invalid("Authored path tangent frame must match mesh vertices");
                    copy.tangents.resize(gpu->vertexCount);
                    copy.bitangents.resize(gpu->vertexCount);
                    for (size_t v = 0; v < copy.tangents.size(); ++v) {
                        copy.tangents[v]   = {t[v * 3], t[v * 3 + 1], t[v * 3 + 2]};
                        copy.bitangents[v] = {b[v * 3], b[v * 3 + 1], b[v * 3 + 2]};
                    }
                }
                if (gpu->cpuIndices.size() >= gpu->indexCount) {
                    std::copy_n(gpu->cpuIndices.begin(), gpu->indexCount, copy.indices.begin());
                } else {
                    auto&       indices     = eve::graphics::vulkan::meshDrawIndices(*gpu);
                    const void* dataIndices = indices.map();
                    for (size_t v = 0; v < copy.indices.size(); ++v)
                        copy.indices[v] = gpu->indexType == vk::IndexType::eUint16
                                              ? static_cast<const uint16_t*>(dataIndices)[v]
                                              : static_cast<const uint32_t*>(dataIndices)[v];
                    indices.unmap();
                }
                geometry.positions  = copy.positions;
                geometry.normals    = copy.normals;
                geometry.texcoords  = copy.uv;
                geometry.tangents   = copy.tangents;
                geometry.bitangents = copy.bitangents;
                geometry.indices    = copy.indices;
                geometry.sourceMesh = nullptr;
            }
            auto captured     = scene;
            captured.geometry = resolved;
            auto prepared     = preparePathScene(gfx, captured);
            if (prepared.ok()) {
                if (cacheable)
                    pathMeshSceneWords_ = std::move(meshWords);
                else
                    pathMeshSceneWords_.clear();
            }
            return prepared;
        } catch (const std::exception& error) {
            return Result<PathSceneToken>::failure(Diagnostic::error(
                DiagnosticCode::Failed, std::string("Path mesh capture: ") + error.what(), "graphics.raytracing"));
        }
    }
    if (scene.geometry.empty() || scene.instances.empty() || scene.materials.empty() ||
        scene.instances.size() > 0xffffffu || scene.materials.size() > UINT32_MAX)
        return invalid("Path scene requires geometry, instances and materials within index limits");
    uint64_t textureWordCount = 0;
    for (size_t i = 0; i < scene.colorTextures.size(); ++i) {
        const auto& texture = scene.colorTextures[i];
        const auto& native  = nativeTextures[i];
        if (native.gpu) {
            const uint64_t count = uint64_t(native.width) * native.height;
            if (count > UINT32_MAX - textureWordCount) return invalid("Path texture storage exceeds index capacity");
            textureWordCount += count;
            continue;
        }
        const uint64_t count = uint64_t(texture.width) * texture.height;
        if (!texture.width || !texture.height || texture.width > INT32_MAX / 2 || texture.height > INT32_MAX / 2 ||
            count != texture.pixels.size() || count * 4 > UINT32_MAX - textureWordCount)
            return invalid("Invalid path color texture dimensions");
        if (!texture.alpha.empty() && texture.alpha.size() != count) return invalid("Invalid path texture alpha count");
        for (float alpha : texture.alpha)
            if (!std::isfinite(alpha) || alpha < 0.f || alpha > 1.f)
                return invalid("Path texture alpha must be finite in [0,1]");
        textureWordCount += count * 4;
        for (const auto& pixel : texture.pixels)
            if (!finiteVector(pixel) || glm::any(glm::lessThan(pixel, glm::vec3(0.f))) ||
                glm::any(glm::greaterThan(pixel, glm::vec3(1.f))))
                return invalid("Path color textures require finite linear RGB in [0,1]");
    }
    for (const auto& material : scene.materials) {
        for (uint32_t texture : {material.baseColorTexture, material.metallicRoughnessTexture, material.emissionTexture,
                                 material.normalTexture})
            if (texture != UINT32_MAX && texture >= scene.colorTextures.size())
                return invalid("Invalid path material texture index");
        if (!std::isfinite(material.normalScale)) return invalid("Path normal scale must be finite");
        if (material.normalTexture != UINT32_MAX) {
            const auto& texture = scene.colorTextures[material.normalTexture];
            if (texture.sourceTexture && texture.srgbDecode) return invalid("Normal textures require linear decoding");
        }
        if (material.metallicRoughnessTexture != UINT32_MAX) {
            const auto& texture = scene.colorTextures[material.metallicRoughnessTexture];
            if (texture.sourceTexture && texture.srgbDecode)
                return invalid("Metallic-roughness textures require linear decoding");
        }
        if (!finiteVector(material.baseColor) || !finiteVector(material.emission) ||
            glm::any(glm::lessThan(material.baseColor, glm::vec3(0.f))) ||
            glm::any(glm::greaterThan(material.baseColor, glm::vec3(1.f))) ||
            glm::any(glm::lessThan(material.emission, glm::vec3(0.f))) || !std::isfinite(material.roughness) ||
            material.roughness < 0.f || material.roughness > 1.f || !std::isfinite(material.metallic) ||
            material.metallic < 0.f || material.metallic > 1.f || !std::isfinite(material.opacity) ||
            material.opacity < 0.f || material.opacity > 1.f || !std::isfinite(material.alphaCutoff) ||
            material.alphaCutoff < 0.f || material.alphaCutoff > 1.f ||
            (material.alphaMode != PathAlphaMode::Opaque && material.alphaMode != PathAlphaMode::Masked &&
             material.alphaMode != PathAlphaMode::Blend))
            return invalid("Invalid path material parameters");
    }
    if (scene.lights.size() >= UINT32_MAX - 1u) return invalid("Too many analytic path lights");
    for (const auto& light : scene.lights) {
        if ((light.kind != PathLightKind::Point && light.kind != PathLightKind::Directional &&
             light.kind != PathLightKind::Spot) ||
            !finiteVector(light.strength) || glm::any(glm::lessThan(light.strength, glm::vec3(0.f))) ||
            (light.kind != PathLightKind::Directional && !finiteVector(light.position)))
            return invalid("Invalid analytic path light");
        if (light.kind == PathLightKind::Spot) {
            const float lengthSquared = glm::dot(light.beamDirection, light.beamDirection);
            if (!finiteVector(light.beamDirection) || !std::isfinite(lengthSquared) || lengthSquared <= 1e-12f ||
                !std::isfinite(light.cosOuter) || !std::isfinite(light.cosInner) || light.cosOuter < -1.f ||
                light.cosInner > 1.f || light.cosOuter > light.cosInner)
                return invalid("Spot light requires a nonzero beam and ordered cone cosines in [-1,1]");
        }
        if (light.kind == PathLightKind::Directional) {
            const float lengthSquared = glm::dot(light.directionToLight, light.directionToLight);
            if (!finiteVector(light.directionToLight) || !std::isfinite(lengthSquared) || lengthSquared <= 1e-12f)
                return invalid("Directional path light needs a finite nonzero direction");
        }
    }
    uint64_t vertexCount = 0, triangleCount = 0;
    for (const auto& geometry : scene.geometry) {
        vertexCount += geometry.positions.size();
        triangleCount += geometry.indices.size() / 3;
        if (geometry.positions.size() < 3 || geometry.indices.empty() || geometry.indices.size() % 3 ||
            geometry.indices.size() > UINT32_MAX || vertexCount > UINT32_MAX || triangleCount > UINT32_MAX ||
            geometry.material >= scene.materials.size() ||
            (!geometry.normals.empty() && geometry.normals.size() != geometry.positions.size()))
            return invalid("Invalid path geometry dimensions or material index");
        if ((!geometry.texcoords.empty() && geometry.texcoords.size() != geometry.positions.size()) ||
            ((scene.materials[geometry.material].baseColorTexture != UINT32_MAX ||
              scene.materials[geometry.material].metallicRoughnessTexture != UINT32_MAX ||
              scene.materials[geometry.material].emissionTexture != UINT32_MAX ||
              scene.materials[geometry.material].normalTexture != UINT32_MAX) &&
             geometry.texcoords.empty()))
            return invalid("Textured path geometry requires one UV per vertex");
        for (const auto& uv : geometry.texcoords)
            if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return invalid("Non-finite path UV");
        for (const auto& position : geometry.positions)
            if (!finiteVector(position)) return invalid("Non-finite path vertex");
        for (const auto& normal : geometry.normals)
            if (!finiteVector(normal)) return invalid("Non-finite path normal");
        for (auto index : geometry.indices)
            if (index >= geometry.positions.size()) return invalid("Path index exceeds vertex count");
        if (geometry.tangents.size() != geometry.bitangents.size() ||
            (!geometry.tangents.empty() && geometry.tangents.size() != geometry.positions.size()))
            return invalid("Path tangent frame requires one pair per vertex");
        for (size_t i = 0; i < geometry.tangents.size(); ++i) {
            const auto   t = glm::dvec3(geometry.tangents[i]), b = glm::dvec3(geometry.bitangents[i]);
            const double tt = glm::dot(t, t), bb = glm::dot(b, b), tb = glm::dot(t, b);
            if (!finiteVector(geometry.tangents[i]) || !finiteVector(geometry.bitangents[i]) || tt <= 1e-20 ||
                bb <= 1e-20 || tt * bb - tb * tb <= tt * bb * 1e-12)
                return invalid("Path tangent frame requires finite independent directions");
        }
        if (scene.materials[geometry.material].normalTexture != UINT32_MAX && geometry.tangents.empty()) {
            for (size_t i = 0; i < geometry.indices.size(); i += 3) {
                auto        a           = geometry.texcoords[geometry.indices[i]];
                auto        b           = geometry.texcoords[geometry.indices[i + 1]] - a;
                auto        c           = geometry.texcoords[geometry.indices[i + 2]] - a;
                const auto& texture     = scene.colorTextures[scene.materials[geometry.material].normalTexture];
                float       determinant = (b.x * c.y - b.y * c.x) * texture.scale.x * texture.scale.y;
                if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12f)
                    return invalid("Normal-mapped triangles require nondegenerate UVs or authored tangent frames");
            }
        }
    }
    for (const auto& instance : scene.instances) {
        const auto& transform   = instance.transform;
        const float determinant = glm::determinant(transform);
        if (instance.geometry >= scene.geometry.size() || !finiteMatrix(transform) || transform[0][3] != 0.f ||
            transform[1][3] != 0.f || transform[2][3] != 0.f || transform[3][3] != 1.f || !std::isfinite(determinant) ||
            std::abs(determinant) < 1e-12f)
            return invalid("Path instance needs a valid geometry and invertible affine transform");
    }
    if (pathGeneration_ == std::numeric_limits<uint64_t>::max()) return invalid("Path scene identity exhausted");
    if (pathFrame_ && pathFrameGeneration_ == UINT64_MAX) return invalid("Path frame identity exhausted");
    if (device_ != &graphics->getDevice())
        attachDevice(&graphics->getDevice(), graphics->rayTracingCaps(), graphics->getUploadPool(),
                     graphics->getDevice().getQueue(vkb::QueueType::graphics));
    if (auto r = ensurePathPipeline(); !r.ok()) return Result<PathSceneToken>::failure(r.status());
    if (std::max(uint64_t(4), textureWordCount) * sizeof(uint32_t) >
        device_->physical_device.properties.limits.maxStorageBufferRange)
        return invalid("Path color textures exceed device storage-buffer capacity");

    if (std::max(size_t(1), scene.colorTextures.size()) >
        device_->physical_device.properties.limits.maxStorageBufferRange / sizeof(path::TextureRecord))
        return invalid("Path texture records exceed device storage-buffer capacity");

    auto                  candidate = std::make_unique<PathScene>();
    std::vector<uint32_t> newSceneWords, newInstanceWords;
    try {
        newSceneWords    = sceneWords(scene, nativeTextures);
        newInstanceWords = instanceWords(scene.instances);
        if (pathScene_ && pathGraphics_ == graphics && newSceneWords == pathSceneWords_) {
            if (newInstanceWords == pathInstanceWords_)
                return Result<PathSceneToken>::success(PathSceneToken{pathScene_->generation});
            return updatePathInstancesPrepared(scene.instances, std::move(newInstanceWords));
        }
        candidate->lights.assign(scene.lights.begin(), scene.lights.end());
        std::vector<path::Vertex>        vertices;
        std::vector<path::Triangle>      triangles;
        std::vector<path::Material>      materials;
        std::vector<uint32_t>            texels;
        std::vector<path::TextureRecord> textureRecords;
        for (size_t i = 0; i < scene.colorTextures.size(); ++i) {
            const auto&         texture = scene.colorTextures[i];
            const auto&         native  = nativeTextures[i];
            path::TextureRecord record;
            record.scaleOffset = glm::vec4(texture.scale, texture.offset);
            record.parameters  = glm::vec4(std::cos(texture.rotation), std::sin(texture.rotation), 0.f, 0.f);
            if (native.gpu) {
                record.image = {uint32_t(texels.size()), native.width, native.height,
                                textureSamplerFlags(texture, native.flags)};
                texels.resize(texels.size() + size_t(native.width) * native.height);
            } else {
                record.image = {uint32_t(texels.size()), texture.width, texture.height,
                                textureSamplerFlags(texture, repeatTextureU | repeatTextureV | linearTexture)};
                for (size_t pixel = 0; pixel < texture.pixels.size(); ++pixel) {
                    const auto rgb = texture.pixels[pixel];
                    texels.insert(
                        texels.end(),
                        {std::bit_cast<uint32_t>(rgb.x), std::bit_cast<uint32_t>(rgb.y), std::bit_cast<uint32_t>(rgb.z),
                         std::bit_cast<uint32_t>(texture.alpha.empty() ? 1.f : texture.alpha[pixel])});
                }
            }
            textureRecords.push_back(record);
        }
        if (texels.empty()) texels.resize(4, std::bit_cast<uint32_t>(1.f));
        if (textureRecords.empty()) textureRecords.emplace_back();
        for (const auto& material : scene.materials) {
            path::Material packed;
            packed.baseColor         = glm::vec4(material.baseColor, material.opacity);
            packed.emissionRoughness = glm::vec4(material.emission, material.roughness);
            packed.parameters =
                glm::vec4(material.metallic, material.alphaCutoff, float(material.alphaMode), material.normalScale);
            packed.textures = {material.baseColorTexture, material.metallicRoughnessTexture, material.emissionTexture,
                               material.normalTexture};
            materials.push_back(packed);
        }
        for (const auto& geometry : scene.geometry) {
            PathGeometryRecord record;
            record.firstTriangle = uint32_t(triangles.size());
            record.firstVertex   = uint32_t(vertices.size());
            record.alphaCoverage = scene.materials[geometry.material].alphaMode != PathAlphaMode::Opaque;
            std::vector<float> positions;
            for (size_t i = 0; i < geometry.positions.size(); ++i) {
                const auto p = geometry.positions[i];
                positions.insert(positions.end(), {p.x, p.y, p.z});
                path::Vertex vertex;
                vertex.position = glm::vec4(p, 1.f);
                if (!geometry.normals.empty()) vertex.normal = glm::vec4(geometry.normals[i], 0.f);
                if (!geometry.texcoords.empty()) vertex.texcoord = glm::vec4(geometry.texcoords[i], 0.f, 0.f);
                if (!geometry.tangents.empty()) {
                    vertex.tangent   = glm::vec4(geometry.tangents[i], 1.f);
                    vertex.bitangent = glm::vec4(geometry.bitangents[i], 0.f);
                }
                vertices.push_back(vertex);
            }
            const bool emissive =
                glm::any(glm::greaterThan(scene.materials[geometry.material].emission, glm::vec3(0.f)));
            for (size_t i = 0; i < geometry.indices.size(); i += 3) {
                path::Triangle triangle;
                triangle.indicesMaterial = {geometry.indices[i], geometry.indices[i + 1], geometry.indices[i + 2],
                                            geometry.material};
                if (emissive) {
                    triangle.emitter.x = uint32_t(i / 3);
                    record.emitters.push_back({geometry.positions[geometry.indices[i]],
                                               geometry.positions[geometry.indices[i + 1]],
                                               geometry.positions[geometry.indices[i + 2]],
                                               scene.materials[geometry.material].emission, uint32_t(i / 3)});
                }
                triangles.push_back(triangle);
            }
            auto mesh                 = std::make_unique<TriangleMeshRecord>();
            mesh->vertexCount         = uint32_t(geometry.positions.size());
            mesh->indexCount          = uint32_t(geometry.indices.size());
            const auto uploadGeometry = [&](DeviceAddressBuffer& target, const void* data,
                                            size_t bytes) -> Result<void> {
                if (auto r = target.allocate(*device_, bytes,
                                             vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                                 vk::BufferUsageFlagBits::eTransferDst);
                    !r.ok())
                    return r;
                return target.upload(uploadPool_, graphicsQueue_, data, bytes);
            };
            if (auto r = uploadGeometry(mesh->vertexBuffer, positions.data(), positions.size() * sizeof(float));
                !r.ok())
                return Result<PathSceneToken>::failure(r.status());
            if (auto r = uploadGeometry(mesh->indexBuffer, geometry.indices.data(), geometry.indices.size_bytes());
                !r.ok())
                return Result<PathSceneToken>::failure(r.status());
            if (auto r = buildBlas(*mesh); !r.ok()) return Result<PathSceneToken>::failure(r.status());
            candidate->meshes.push_back(std::move(mesh));
            candidate->geometry.push_back(std::move(record));
        }
        const auto upload = [&](size_t slot, const auto& values) -> Result<void> {
            const auto bytes  = values.size() * sizeof(values[0]);
            auto&      buffer = candidate->buffers[slot];
            if (auto r = buffer.allocate(
                    *device_, bytes, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
                !r.ok())
                return r;
            return buffer.upload(uploadPool_, graphicsQueue_, values.data(), bytes);
        };
        if (auto r = upload(0, vertices); !r.ok()) return Result<PathSceneToken>::failure(r.status());
        if (auto r = upload(1, triangles); !r.ok()) return Result<PathSceneToken>::failure(r.status());
        if (auto r = upload(3, materials); !r.ok()) return Result<PathSceneToken>::failure(r.status());
        if (auto r = upload(5, texels); !r.ok()) return Result<PathSceneToken>::failure(r.status());
        if (auto r = upload(6, textureRecords); !r.ok()) return Result<PathSceneToken>::failure(r.status());
        if (std::any_of(nativeTextures.begin(), nativeTextures.end(),
                        [](const auto& texture) { return texture.gpu; })) {
            graphics->waitForSharedGpuResources();
            vkb::executeImmediately(device_->instance, uploadPool_, graphicsQueue_, [&](vk::CommandBuffer command) {
                for (size_t i = 0; i < nativeTextures.size(); ++i) {
                    const auto& texture = nativeTextures[i];
                    if (!texture.gpu) continue;
                    vk::ImageMemoryBarrier barrier{};
                    barrier.srcAccessMask       = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
                    barrier.dstAccessMask       = vk::AccessFlagBits::eTransferRead;
                    barrier.oldLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
                    barrier.newLayout           = vk::ImageLayout::eTransferSrcOptimal;
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image                                             = texture.gpu->image.image();
                    barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, texture.gpu->mipLevels, 0, 1};
                    command.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                                            vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0, nullptr, 1,
                                            &barrier);
                    vk::BufferImageCopy copy{};
                    copy.bufferOffset     = vk::DeviceSize(textureRecords[i].image.x) * sizeof(uint32_t);
                    copy.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                    copy.imageExtent      = vk::Extent3D{texture.width, texture.height, 1};
                    command.copyImageToBuffer(texture.gpu->image.image(), vk::ImageLayout::eTransferSrcOptimal,
                                              candidate->buffers[5].buffer, copy);
                    barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
                    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                    std::swap(barrier.oldLayout, barrier.newLayout);
                    command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                            vk::PipelineStageFlagBits::eAllCommands, {}, 0, nullptr, 0, nullptr, 1,
                                            &barrier);
                }
                vk::BufferMemoryBarrier ready{};
                ready.srcAccessMask       = vk::AccessFlagBits::eTransferWrite;
                ready.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
                ready.srcQueueFamilyIndex = ready.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                ready.buffer                                          = candidate->buffers[5].buffer;
                ready.size                                            = VK_WHOLE_SIZE;
                command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                        vk::PipelineStageFlagBits::eRayTracingShaderKHR, {}, 0, nullptr, 1, &ready, 0,
                                        nullptr);
            });
        }
        if (auto r = buildPathInstances(scene.instances, *candidate, *candidate); !r.ok())
            return Result<PathSceneToken>::failure(r.status());
    } catch (const std::exception& error) {
        return Result<PathSceneToken>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Path scene: ") + error.what(), "graphics.raytracing"));
    }
    candidate->generation = ++pathGeneration_;
    // Scene uploads/builds completed before publication. Keep same-device output
    // images and sampled identities alive so HDR binding caches can reuse them.
    // The public output token still expires on full scene replacement.
    if (pathFrame_) {
        pathFrame_->scene = vk::AccelerationStructureKHR{};
        ++pathFrameGeneration_;
    }
    pathRecordedSequence_ = 0;
    pathSpareScene_.reset();
    pathScene_ = std::move(candidate);
    pathMeshSceneWords_.clear();
    pathSceneWords_     = std::move(newSceneWords);
    pathInstanceWords_  = std::move(newInstanceWords);
    pathGraphics_       = graphics;
    pathHistorySamples_ = 0;
    return Result<PathSceneToken>::success(PathSceneToken{pathGeneration_});
}

void VulkanRayTracing::reconcilePathRecording() {
    if (!pathRecordedSequence_ || !pathGraphics_ || pathRecordedSequence_ == pathGraphics_->recordingPresentSequence())
        return;
    if (pathRecordedSequence_ != pathGraphics_->submittedPresentSequence()) {
        pathHistorySamples_ = 0;
        if (pathFrame_) pathFrame_->initialized = false;
    }
    pathRecordedSequence_ = 0;
}

Result<PathFrameToken> VulkanRayTracing::preparePathFrame(PathSceneToken scene, uint32_t width, uint32_t height) {
    if (!pathScene_ || !pathGraphics_ || scene.generation != pathScene_->generation ||
        pathGraphics_->hasOpenPresentFrame() || !width || !height || width > INT32_MAX || height > INT32_MAX)
        return Result<PathFrameToken>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Prepare path output outside a frame with a valid scene and extent",
            "graphics.raytracing"));
    reconcilePathRecording();
    if (auto prepared = ensurePathFrame(width, height); !prepared.ok())
        return Result<PathFrameToken>::failure(prepared.status());
    return Result<PathFrameToken>::success(PathFrameToken{pathFrameGeneration_});
}

Result<Texture*> VulkanRayTracing::recordPathFrame(PathSceneToken scene, PathFrameToken output,
                                                   const PathTraceView& view) {
    EV_PROFILE_MODULE("graphics", "Path.RecordFrame");
    const auto invalid = [](const char* message) {
        return Result<Texture*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "graphics.raytracing"));
    };
    if (!pathGraphics_ || !pathScene_ || scene.generation != pathScene_->generation || !pathFrame_ ||
        output.generation != pathFrameGeneration_ || !pathGraphics_->hasOpenPresentFrame())
        return invalid("Record paths with prepared output in an open normal frame");
    const auto sequence = pathGraphics_->recordingPresentSequence();
    if (!sequence || sequence == pathRecordedSequence_)
        return invalid("Only one path batch may record per present frame");
    reconcilePathRecording();
    if (auto valid = validatePathView(view); !valid.ok()) return Result<Texture*>::failure(valid.status());
    std::array<vk::DescriptorBufferInfo, 7> buffers{};
    for (size_t i = 0; i < buffers.size(); ++i)
        buffers[i] = vk::DescriptorBufferInfo{pathScene_->buffers[i].buffer, 0, pathScene_->buffers[i].size};
    path::Constants constants;
    constants.invViewProj = view.invViewProj;
    constants.eye         = glm::vec4(view.eye, 0.f);
    constants.environment = glm::vec4(view.environment, 0.f);
    constants.sampling    = {view.firstSample, view.samples, view.maxBounces, view.seed};
    constants.counts      = {pathScene_->lightCount, view.historySamples, uint32_t(view.denoise), 0u};
    try {
        pathGraphics_->beginPathSceneOutput();
        auto recorded = recordPathDispatch(pathGraphics_->postSceneCommandBuffer(), pathScene_->tlas.handle, buffers,
                                           constants, view, pathFrame_->width, pathFrame_->height);
        if (!recorded.ok()) return Result<Texture*>::failure(recorded.status());
        std::array<vk::ImageMemoryBarrier, 2> sampled{};
        for (size_t i = 0; i < sampled.size(); ++i) {
            auto& barrier               = sampled[i];
            barrier.srcAccessMask       = vk::AccessFlagBits::eShaderWrite;
            barrier.dstAccessMask       = vk::AccessFlagBits::eShaderRead;
            barrier.oldLayout           = vk::ImageLayout::eGeneral;
            barrier.newLayout           = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                                             = pathFrame_->images[i == 0 ? 1 : 4].image();
            barrier.subresourceRange                                  = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        }
        pathGraphics_->postSceneCommandBuffer().pipelineBarrier(
            vk::PipelineStageFlagBits::eRayTracingShaderKHR | vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader, {}, {}, {},
            sampled);
        pathFrame_->sampledOutput = true;
    } catch (const std::exception& error) {
        pathHistorySamples_     = 0;
        pathFrame_->initialized = false;
        return Result<Texture*>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Record path frame: ") + error.what(), "graphics.raytracing"));
    }
    pathRecordedSequence_ = sequence;
    pathHistorySamples_   = view.historySamples + view.samples;
    pathPreviousView_     = view;
    auto* texture         = &pathFrame_->output->textures[view.denoise != PathDenoise::None ? 1 : 0];
    pathGraphics_->resolvePathSceneOutput(texture);
    return Result<Texture*>::success(texture);
}

Result<void> VulkanRayTracing::validatePathView(const PathTraceView& view) const {
    const auto invalid = [](const char* message) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "graphics.raytracing"));
    };
    if (!finiteMatrix(view.invViewProj) || !finiteVector(view.eye) || !finiteVector(view.environment) ||
        glm::any(glm::lessThan(view.environment, glm::vec3(0.f))) || view.samples == 0 || view.samples > 4096 ||
        view.maxBounces > 64 || view.historySamples > (1u << 24) - view.samples ||
        view.firstSample > UINT32_MAX - view.samples)
        return invalid("Invalid path camera or sampling settings");
    if (view.denoise != PathDenoise::None && view.denoise != PathDenoise::Spatial &&
        view.denoise != PathDenoise::Temporal)
        return invalid("Invalid path denoising mode");
    if (view.denoise == PathDenoise::Temporal &&
        (view.historySamples != 0 || glm::determinant(view.invViewProj) == 0.f ||
         !finiteMatrix(glm::inverse(view.invViewProj))))
        return invalid("Temporal paths need an invertible camera and zero explicit historySamples");
    const auto& previous = pathPreviousView_;
    if (view.historySamples && previous.denoise == PathDenoise::Temporal)
        return invalid("Temporal history cannot be used as exact sample accumulation");
    if (view.historySamples &&
        (view.historySamples != pathHistorySamples_ || view.invViewProj != previous.invViewProj ||
         view.eye != previous.eye || view.environment != previous.environment || view.seed != previous.seed ||
         view.maxBounces != previous.maxBounces || view.firstSample != previous.firstSample + previous.samples))
        return invalid("Path history does not match the previous completed sample batch");
    return Result<void>::success();
}

Result<void> VulkanRayTracing::renderPaths(PathSceneToken token, Canvas* dest, const PathTraceView& view) {
    const auto invalid = [](const char* message) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "graphics.raytracing"));
    };
    auto* canvas = dynamic_cast<eve::graphics::vulkan::OffscreenCanvas*>(dest);
    if (!pathGraphics_ || pathGraphics_->hasOpenPresentFrame())
        return invalid("Render synchronous path batches outside frame recording");
    if (!pathScene_ || token.generation != pathScene_->generation || !canvas ||
        canvas->owningGraphics() != pathGraphics_ || dest->getWidth() <= 0 || dest->getHeight() <= 0)
        return invalid("Invalid path scene token or destination");
    if (canvas->colorImage().format() != vk::Format::eR16G16B16A16Sfloat)
        return invalid("Path-tracing destination must use RGBA16F");
    reconcilePathRecording();
    if (auto valid = validatePathView(view); !valid.ok()) return valid;
    std::array<vk::DescriptorBufferInfo, 7> buffers{};
    for (size_t i = 0; i < buffers.size(); ++i)
        buffers[i] = vk::DescriptorBufferInfo{pathScene_->buffers[i].buffer, 0, pathScene_->buffers[i].size};
    path::Constants constants;
    constants.invViewProj = view.invViewProj;
    constants.eye         = glm::vec4(view.eye, 0.f);
    constants.environment = glm::vec4(view.environment, 0.f);
    constants.sampling    = {view.firstSample, view.samples, view.maxBounces, view.seed};
    constants.counts      = {pathScene_->lightCount, view.historySamples, uint32_t(view.denoise), 0u};
    eve::Status status    = eve::Status::success();
    try {
        vkb::executeImmediately(device_->instance, uploadPool_, graphicsQueue_, [&](vk::CommandBuffer command) {
            auto recorded = recordPathDispatch(command, pathScene_->tlas.handle, buffers, constants, view,
                                               uint32_t(dest->getWidth()), uint32_t(dest->getHeight()));
            if (!recorded.ok()) {
                status = recorded.status();
                return;
            }
            canvas->colorImage().setLayout(command, vk::ImageLayout::eTransferDstOptimal);
            vk::ImageCopy region{};
            region.srcSubresource    = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            region.dstSubresource    = region.srcSubresource;
            region.extent            = vk::Extent3D{uint32_t(dest->getWidth()), uint32_t(dest->getHeight()), 1};
            const size_t outputImage = view.denoise != PathDenoise::None ? 4 : 1;
            command.copyImage(pathFrame_->images[outputImage].image(), vk::ImageLayout::eGeneral,
                              canvas->colorImage().image(), vk::ImageLayout::eTransferDstOptimal, region);
            canvas->colorImage().setLayout(command, vk::ImageLayout::eShaderReadOnlyOptimal);
        });
    } catch (const std::exception& error) {
        pathHistorySamples_ = 0;
        if (pathFrame_) pathFrame_->initialized = false;
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Path dispatch: ") + error.what(), "graphics.raytracing"));
    }
    if (!status.isSuccess()) return Result<void>::failure(status);
    pathHistorySamples_ = view.historySamples + view.samples;
    pathPreviousView_   = view;
    return Result<void>::success();
}

}  // namespace eve::graphics::raytracing
