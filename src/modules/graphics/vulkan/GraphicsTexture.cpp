// Vulkan backend implementation — texture creation, upload and reload.
//
// Split out of Graphics2D.cpp (pure move; shared helpers live in
// GraphicsInternal.h). Keep the include list tight: the embedded shader
// .inc arrays are unused here and would trip -Wunused-const-variable under
// strict-warning CI builds.

#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"
#include "graphics/vulkan/Canvas.h"

#include "common/Exception.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#if __has_include("graphics/shaders/reflection_probe_filter_comp_spv.inc")
#include "graphics/shaders/reflection_probe_filter_comp_spv.inc"
#define EVENGINE_HAS_REFLECTION_PROBE_FILTER_SPV 1
#endif


namespace eve::graphics::vulkan {

namespace {

template <class TextureImage>
void uploadTextureForAllShaderStages(vkb::Device &device, vk::CommandPool commandPool,
                                     vk::Queue graphicsQueue, TextureImage &image,
                                     uint32_t width, uint32_t height, uint32_t mipLevels,
                                     uint32_t layers, const std::vector<uint8_t> &bytes) {
    vkb::GenericBuffer staging(
        device, vk::BufferUsageFlagBits::eTransferSrc, vk::DeviceSize(bytes.size()),
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    staging.updateLocal(vkb::FrameSlot::gpuIdle(), bytes.data(), vk::DeviceSize(bytes.size()));

    vkb::executeImmediately(device.instance, commandPool, graphicsQueue,
                            [&](vk::CommandBuffer cb) {
                                vk::DeviceSize offset = 0;
                                for (uint32_t mip = 0; mip < mipLevels; ++mip) {
                                    const uint32_t mipWidth = std::max(width >> mip, 1u);
                                    const uint32_t mipHeight = std::max(height >> mip, 1u);
                                    for (uint32_t layer = 0; layer < layers; ++layer) {
                                        image.copy(cb, staging.buffer, mip, layer, mipWidth,
                                                   mipHeight, 1, uint32_t(offset));
                                        offset += vk::DeviceSize(mipWidth) * mipHeight * 4u;
                                    }
                                }
                                // VKBuilder's shader-read transition targets
                                // vertex shaders only. Perform the final image
                                // transition here so fragment and compute
                                // texture consumers are in its destination
                                // scope as well.
                                vk::ImageMemoryBarrier barrier{};
                                barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
                                barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                                barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
                                barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                barrier.image = image.image();
                                barrier.subresourceRange = {
                                    vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, layers};
                                cb.pipelineBarrier(
                                    vk::PipelineStageFlagBits::eTransfer,
                                    vk::PipelineStageFlagBits::eVertexShader |
                                        vk::PipelineStageFlagBits::eFragmentShader |
                                        vk::PipelineStageFlagBits::eComputeShader,
                                    {}, 0, nullptr, 0, nullptr, 1, &barrier);
                                image.setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
                            });
    staging.release();
}

}  // namespace

void Graphics::writeCombinedImageDescriptor(GpuTexture *gpu) {
    if (!gpu || !gpu->descriptorSet || !gpu->sampler) return;
    vk::ImageView view = gpu->imageView();
    if (!view) return;
    vkb::UnboundSet unbound = vkb::UnboundSet::reopenAfterIdle(gpu->descriptorSet);
    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpu->sampler, view))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpu->sampler, view))
        .update(device.instance);
    gpu->descriptorSet = std::move(unbound).publish();
}

Texture *Graphics::newTexture(int w, int h, const uint8_t *rgba, bool repeatU, bool repeatV) {
    TextureCreateInfo info;
    info.sampler.repeatU = repeatU;
    info.sampler.repeatV = repeatV;
    return newTexture(w, h, rgba, info);
}

Texture *Graphics::newTexture(int w, int h, const uint8_t *rgba, const TextureCreateInfo &rawInfo) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    ASSERT(rgba != nullptr);
    if (!initialized) throw Exception("newTexture: graphics not initialized");
    if (w <= 0 || h <= 0 || !rgba) throw Exception("newTexture: invalid args");

    TextureCreateInfo info = normalizeTextureInfo(rawInfo);
    const uint32_t mipLevels =
        info.generateMipmaps ? uint32_t(mipmapCountForSize(w, h)) : 1u;

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->isCube = false;
    gpu->mipLevels = mipLevels;
    gpu->samplerState = info.sampler;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h), mipLevels);

    std::vector<uint8_t> bytes =
        (mipLevels > 1) ? buildMipChain2D(rgba, uint32_t(w), uint32_t(h), mipLevels)
                        : std::vector<uint8_t>(rgba, rgba + size_t(w) * size_t(h) * 4);
    uploadTextureForAllShaderStages(device, uploadPool,
                                    device.getQueue(vkb::QueueType::graphics), gpu->image,
                                    uint32_t(w), uint32_t(h), mipLevels, 1, bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);

    gpu->descriptorSet = vkb::BoundSet{sets[0]};
    writeCombinedImageDescriptor(gpu.get());
    registerBindlessTexture2D(gpu.get());

    auto tex = std::make_unique<Texture>();
    tex->width = w;
    tex->height = h;
    tex->pixelWidth = w;
    tex->pixelHeight = h;
    tex->mipmapCount = int(mipLevels);
    tex->sampler = info.sampler;
    tex->gpuHandle = gpu.get();

    Texture *raw = tex.get();
    ownedTextures.push_back(std::move(tex));
    ownedGpuTextures.push_back(std::move(gpu));
    return raw;
}

Texture *Graphics::newCubemap(int faceSize, const uint8_t *rgbaFaces) {
    // IBL shaders query the actual GGX-prefiltered mip count; generate it by default.
    return newCubemap(faceSize, rgbaFaces, TextureCreateInfo::withMipmaps(false));
}

Texture *Graphics::newCubemap(int faceSize, const uint8_t *rgbaFaces,
                              const TextureCreateInfo &rawInfo) {
    ASSERT(initialized);
    ASSERT_GT(faceSize, 0);
    ASSERT(rgbaFaces != nullptr);
    if (!initialized) throw Exception("newCubemap: graphics not initialized");
    if (faceSize <= 0 || !rgbaFaces) throw Exception("newCubemap: invalid args");

    TextureCreateInfo info = normalizeTextureInfo(rawInfo);
    info.sampler.repeatU = false;
    info.sampler.repeatV = false;
    info.sampler.repeatW = false;
    const uint32_t mipLevels =
        info.generateMipmaps ? uint32_t(mipmapCountForSize(faceSize, faceSize)) : 1u;

    const size_t faceBytes = size_t(faceSize) * size_t(faceSize) * 4u;
    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = faceSize;
    gpu->height = faceSize;
    gpu->isCube = true;
    gpu->mipLevels = mipLevels;
    gpu->samplerState = info.sampler;
    gpu->cubeImage = vkb::TextureImageCube(device, device.physical_device.memory_properties,
                                           uint32_t(faceSize), uint32_t(faceSize), mipLevels);

    std::vector<uint8_t> bytes =
        (mipLevels > 1) ? buildMipChainCube(rgbaFaces, uint32_t(faceSize), mipLevels)
                        : std::vector<uint8_t>(rgbaFaces, rgbaFaces + faceBytes * 6u);
    uploadTextureForAllShaderStages(device, uploadPool,
                                    device.getQueue(vkb::QueueType::graphics), gpu->cubeImage,
                                    uint32_t(faceSize), uint32_t(faceSize), mipLevels, 6, bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);
    // Cubemap sampled via mesh3d descriptor sets — no 2D texSetLayout binding required here.
    registerBindlessTextureCube(gpu.get());

    auto tex = std::make_unique<Texture>();
    tex->width = faceSize;
    tex->height = faceSize;
    tex->pixelWidth = faceSize;
    tex->pixelHeight = faceSize;
    tex->layers = 6;
    tex->mipmapCount = int(mipLevels);
    tex->sampler = info.sampler;
    tex->gpuHandle = gpu.get();

    Texture *raw = tex.get();
    ownedTextures.push_back(std::move(tex));
    ownedGpuTextures.push_back(std::move(gpu));
    return raw;
}

Texture *Graphics::newHDRCubemap(int faceSize) {
    if (!initialized || faceSize <= 0) return nullptr;
    const uint32_t size = static_cast<uint32_t>(faceSize);
    const uint32_t mipLevels = uint32_t(mipmapCountForSize(faceSize, faceSize));
    vk::ImageCreateInfo imageInfo{};
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
    imageInfo.extent = vk::Extent3D{size, size, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 6;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                      vk::ImageUsageFlagBits::eTransferSrc |
                      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    auto gpu = std::make_unique<GpuTexture>();
    gpu->rawCubeImage = device->createImageUnique(imageInfo);
    const vk::MemoryRequirements requirements =
        device->getImageMemoryRequirements(*gpu->rawCubeImage);
    uint32_t memoryType = UINT32_MAX;
    const auto &memoryProperties = device.physical_device.memory_properties;
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if ((requirements.memoryTypeBits & (1u << index)) != 0u &&
            (memoryProperties.memoryTypes[index].propertyFlags &
             vk::MemoryPropertyFlagBits::eDeviceLocal) != vk::MemoryPropertyFlags{}) {
            memoryType = index;
            break;
        }
    }
    if (memoryType == UINT32_MAX) return nullptr;
    gpu->rawCubeMemory = device->allocateMemoryUnique(
        vk::MemoryAllocateInfo{requirements.size, memoryType});
    device->bindImageMemory(*gpu->rawCubeImage, *gpu->rawCubeMemory, 0);

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = *gpu->rawCubeImage;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange =
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6};
    gpu->rawCubeView = device->createImageViewUnique(viewInfo);
    gpu->width = faceSize;
    gpu->height = faceSize;
    gpu->isCube = true;
    gpu->isHDR = true;
    gpu->mipLevels = mipLevels;
    gpu->samplerState = TextureSampler::linearMipmap();
    gpu->sampler = createVkSampler(gpu->samplerState, mipLevels);

    vkb::executeImmediately(device.instance, uploadPool,
                            device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer command) {
                                vk::ImageMemoryBarrier barrier{};
                                barrier.srcAccessMask = {};
                                barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                                barrier.oldLayout = vk::ImageLayout::eUndefined;
                                barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                barrier.image = *gpu->rawCubeImage;
                                barrier.subresourceRange = viewInfo.subresourceRange;
                                command.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                                        vk::PipelineStageFlagBits::eFragmentShader,
                                                        {}, 0, nullptr, 0, nullptr, 1, &barrier);
                            });
    registerBindlessTextureCube(gpu.get());

    auto texture = std::make_unique<Texture>();
    texture->width = faceSize;
    texture->height = faceSize;
    texture->pixelWidth = faceSize;
    texture->pixelHeight = faceSize;
    texture->layers = 6;
    texture->mipmapCount = int(mipLevels);
    texture->sampler = gpu->samplerState;
    texture->gpuHandle = gpu.get();
    Texture *raw = texture.get();
    ownedTextures.push_back(std::move(texture));
    ownedGpuTextures.push_back(std::move(gpu));
    return raw;
}

bool Graphics::copyHDRCanvasToCubemapFace(Canvas *source, Texture *cubemap, int face) {
    auto *canvas = dynamic_cast<OffscreenCanvas *>(source);
    if (!canvas || !canvas->isHDR() || !cubemap || !cubemap->gpuHandle || face < 0 || face >= 6)
        return false;
    auto *target = static_cast<GpuTexture *>(cubemap->gpuHandle);
    if (!target->isCube || !target->isHDR || !target->rawCubeImage ||
        canvas->getWidth() != target->width || canvas->getHeight() != target->height)
        return false;
    ensureOffscreen3DResources();
    lastOffscreen3DGpuDurationMs = 0.f;
    vkb::executeImmediately(device.instance, uploadPool,
                            device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer command) {
                                if (offscreen3DTimestampQueryPool) {
                                    command.resetQueryPool(offscreen3DTimestampQueryPool, 0, 2);
                                    command.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,
                                                           offscreen3DTimestampQueryPool, 0);
                                }
                                canvas->colorImage().setLayout(command,
                                                              vk::ImageLayout::eTransferSrcOptimal);
                                vk::ImageMemoryBarrier toCopy{};
                                toCopy.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                                toCopy.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
                                toCopy.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                                toCopy.newLayout = vk::ImageLayout::eTransferDstOptimal;
                                toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                toCopy.image = *target->rawCubeImage;
                                toCopy.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1,
                                                           uint32_t(face), 1};
                                command.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                                                        vk::PipelineStageFlagBits::eTransfer, {}, 0,
                                                        nullptr, 0, nullptr, 1, &toCopy);
                                vk::ImageCopy copy{};
                                copy.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                                copy.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0,
                                                       uint32_t(face), 1};
                                copy.extent = vk::Extent3D{uint32_t(target->width),
                                                           uint32_t(target->height), 1};
                                command.copyImage(canvas->colorImage().image(),
                                                  vk::ImageLayout::eTransferSrcOptimal,
                                                  *target->rawCubeImage,
                                                  vk::ImageLayout::eTransferDstOptimal, 1, &copy);
                                std::swap(toCopy.srcAccessMask, toCopy.dstAccessMask);
                                std::swap(toCopy.oldLayout, toCopy.newLayout);
                                command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                                        vk::PipelineStageFlagBits::eFragmentShader,
                                                        {}, 0, nullptr, 0, nullptr, 1, &toCopy);
                                canvas->colorImage().setLayout(
                                    command, vk::ImageLayout::eShaderReadOnlyOptimal);
                                if (offscreen3DTimestampQueryPool)
                                    command.writeTimestamp(
                                        vk::PipelineStageFlagBits::eBottomOfPipe,
                                        offscreen3DTimestampQueryPool, 1);
                            });
    if (offscreen3DTimestampQueryPool && offscreen3DTimestampPeriodNs > 0.f) {
        std::array<uint64_t, 2> ticks{};
        const vk::Result result = device->getQueryPoolResults(
            offscreen3DTimestampQueryPool, 0, uint32_t(ticks.size()), sizeof(ticks), ticks.data(),
            sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result == vk::Result::eSuccess && ticks[1] >= ticks[0])
            lastOffscreen3DGpuDurationMs =
                float(double(ticks[1] - ticks[0]) * double(offscreen3DTimestampPeriodNs) * 1.0e-6);
    }
    return true;
}

bool Graphics::copyHDRCanvasesToCubemap(Canvas *const *sources, int faceCount,
                                        Texture *cubemap) {
    if (!sources || faceCount < 1 || faceCount > 6 || !cubemap || !cubemap->gpuHandle)
        return false;
    auto *target = static_cast<GpuTexture *>(cubemap->gpuHandle);
    if (!target->isCube || !target->isHDR || !target->rawCubeImage) return false;

    std::array<OffscreenCanvas *, 6> canvases{};
    for (int face = 0; face < faceCount; ++face) {
        auto *canvas = dynamic_cast<OffscreenCanvas *>(sources[face]);
        if (!canvas || !canvas->isHDR() || canvas->getWidth() != target->width ||
            canvas->getHeight() != target->height)
            return false;
        canvases[static_cast<size_t>(face)] = canvas;
    }

    ensureOffscreen3DResources();
    lastOffscreen3DGpuDurationMs = 0.f;
    vkb::executeImmediately(device.instance, uploadPool,
                            device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer command) {
                                if (offscreen3DTimestampQueryPool) {
                                    command.resetQueryPool(offscreen3DTimestampQueryPool, 0, 2);
                                    command.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,
                                                           offscreen3DTimestampQueryPool, 0);
                                }
                                for (int face = 0; face < faceCount; ++face) {
                                    auto *canvas = canvases[static_cast<size_t>(face)];
                                    canvas->colorImage().setLayout(
                                        command, vk::ImageLayout::eTransferSrcOptimal);

                                    vk::ImageMemoryBarrier toCopy{};
                                    toCopy.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                                    toCopy.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
                                    toCopy.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                                    toCopy.newLayout = vk::ImageLayout::eTransferDstOptimal;
                                    toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                    toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                    toCopy.image = *target->rawCubeImage;
                                    toCopy.subresourceRange =
                                        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,
                                                                  0, 1, uint32_t(face), 1);
                                    command.pipelineBarrier(
                                        vk::PipelineStageFlagBits::eFragmentShader,
                                        vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0,
                                        nullptr, 1, &toCopy);

                                    vk::ImageCopy copy{};
                                    copy.srcSubresource =
                                        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor,
                                                                   0, 0, 1);
                                    copy.dstSubresource =
                                        vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor,
                                                                   0, uint32_t(face), 1);
                                    copy.extent = vk::Extent3D(uint32_t(canvas->getWidth()),
                                                               uint32_t(canvas->getHeight()), 1);
                                    command.copyImage(canvas->colorImage().image(),
                                                      vk::ImageLayout::eTransferSrcOptimal,
                                                      *target->rawCubeImage,
                                                      vk::ImageLayout::eTransferDstOptimal, 1,
                                                      &copy);

                                    std::swap(toCopy.srcAccessMask, toCopy.dstAccessMask);
                                    std::swap(toCopy.oldLayout, toCopy.newLayout);
                                    command.pipelineBarrier(
                                        vk::PipelineStageFlagBits::eTransfer,
                                        vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr,
                                        0, nullptr, 1, &toCopy);
                                    canvas->colorImage().setLayout(
                                        command, vk::ImageLayout::eShaderReadOnlyOptimal);
                                }
                                if (offscreen3DTimestampQueryPool)
                                    command.writeTimestamp(
                                        vk::PipelineStageFlagBits::eBottomOfPipe,
                                        offscreen3DTimestampQueryPool, 1);
                            });
    if (offscreen3DTimestampQueryPool && offscreen3DTimestampPeriodNs > 0.f) {
        std::array<uint64_t, 2> ticks{};
        const vk::Result result = device->getQueryPoolResults(
            offscreen3DTimestampQueryPool, 0, uint32_t(ticks.size()), sizeof(ticks), ticks.data(),
            sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result == vk::Result::eSuccess && ticks[1] >= ticks[0])
            lastOffscreen3DGpuDurationMs =
                float(double(ticks[1] - ticks[0]) * double(offscreen3DTimestampPeriodNs) * 1.0e-6);
    }
    return true;
}

bool Graphics::filterHDRReflectionCubemap(Texture *cubemap, int sampleCount) {
#ifndef EVENGINE_HAS_REFLECTION_PROBE_FILTER_SPV
    (void)cubemap;
    (void)sampleCount;
    return false;
#else
    if (!cubemap || !cubemap->gpuHandle) return false;
    auto *target = static_cast<GpuTexture *>(cubemap->gpuHandle);
    if (!target->isCube || !target->isHDR || !target->rawCubeImage || target->mipLevels < 2)
        return false;
    sampleCount = std::clamp(sampleCount, 8, 512);
    ensureOffscreen3DResources();
    lastOffscreen3DGpuDurationMs = 0.f;

    if (!reflectionProbeFilterPass.pipeline()) {
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 1,
                                           vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageImage, 1,
                                           vk::ShaderStageFlagBits::eCompute},
        };
        reflectionProbeFilterSetLayout = device->createDescriptorSetLayoutUnique(
            vk::DescriptorSetLayoutCreateInfo{{}, uint32_t(bindings.size()), bindings.data()});
        vk::DescriptorSetLayout layout = *reflectionProbeFilterSetLayout;
        vk::PushConstantRange push{vk::ShaderStageFlagBits::eCompute, 0, 16};
        reflectionProbeFilterPipelineLayout = device->createPipelineLayoutUnique(
            vk::PipelineLayoutCreateInfo{{}, 1, &layout, 1, &push});
        const std::vector<uint32_t> spv(reflection_probe_filter_comp_spv,
                                        reflection_probe_filter_comp_spv +
                                            reflection_probe_filter_comp_spv_count);
        if (!reflectionProbeFilterPass.create(device, *reflectionProbeFilterPipelineLayout, spv))
            return false;
    }

    vk::ImageViewCreateInfo sourceInfo{};
    sourceInfo.image = *target->rawCubeImage;
    sourceInfo.viewType = vk::ImageViewType::eCube;
    sourceInfo.format = vk::Format::eR16G16B16A16Sfloat;
    sourceInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6};
    vk::UniqueImageView sourceView = device->createImageViewUnique(sourceInfo);
    std::vector<vk::UniqueImageView> targetViews;
    targetViews.reserve(target->mipLevels - 1u);
    for (uint32_t mip = 1; mip < target->mipLevels; ++mip) {
        vk::ImageViewCreateInfo info{};
        info.image = *target->rawCubeImage;
        info.viewType = vk::ImageViewType::e2DArray;
        info.format = sourceInfo.format;
        info.subresourceRange = {vk::ImageAspectFlagBits::eColor, mip, 1, 0, 6};
        targetViews.push_back(device->createImageViewUnique(info));
    }

    std::array<vk::DescriptorPoolSize, 2> poolSizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler,
                               target->mipLevels - 1u},
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, target->mipLevels - 1u},
    };
    vk::UniqueDescriptorPool pool = device->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo{
        {}, target->mipLevels - 1u, uint32_t(poolSizes.size()), poolSizes.data()});
    std::vector<vk::DescriptorSetLayout> layouts(target->mipLevels - 1u,
                                                  *reflectionProbeFilterSetLayout);
    std::vector<vk::DescriptorSet> sets = device->allocateDescriptorSets(
        vk::DescriptorSetAllocateInfo{*pool, uint32_t(layouts.size()), layouts.data()});
    for (size_t index = 0; index < sets.size(); ++index) {
        vk::DescriptorImageInfo sourceImage{target->sampler, *sourceView,
                                             vk::ImageLayout::eShaderReadOnlyOptimal};
        vk::DescriptorImageInfo destinationImage{{}, *targetViews[index],
                                                  vk::ImageLayout::eGeneral};
        std::array<vk::WriteDescriptorSet, 2> writes{
            vk::WriteDescriptorSet{sets[index], 0, 0, 1,
                                   vk::DescriptorType::eCombinedImageSampler, &sourceImage},
            vk::WriteDescriptorSet{sets[index], 1, 0, 1,
                                   vk::DescriptorType::eStorageImage, &destinationImage},
        };
        device->updateDescriptorSets(uint32_t(writes.size()), writes.data(), 0, nullptr);
    }

    struct FilterPush {
        float roughness;
        uint32_t sampleCount;
        uint32_t diffuseMode;
        uint32_t targetSize;
    };
    vkb::executeImmediately(device.instance, uploadPool,
                            device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer command) {
                                if (offscreen3DTimestampQueryPool) {
                                    command.resetQueryPool(offscreen3DTimestampQueryPool, 0, 2);
                                    command.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,
                                                           offscreen3DTimestampQueryPool, 0);
                                }
                                for (uint32_t mip = 1; mip < target->mipLevels; ++mip) {
                                    vk::ImageMemoryBarrier toWrite{};
                                    toWrite.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                                    toWrite.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
                                    toWrite.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                                    toWrite.newLayout = vk::ImageLayout::eGeneral;
                                    toWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                    toWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                    toWrite.image = *target->rawCubeImage;
                                    toWrite.subresourceRange = {vk::ImageAspectFlagBits::eColor, mip,
                                                                1, 0, 6};
                                    command.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                                                            vk::PipelineStageFlagBits::eComputeShader,
                                                            {}, 0, nullptr, 0, nullptr, 1, &toWrite);
                                    const uint32_t size = std::max(uint32_t(target->width) >> mip, 1u);
                                    const FilterPush push{
                                        float(mip) / float(target->mipLevels - 1u),
                                        uint32_t(sampleCount),
                                        mip + 1u == target->mipLevels ? 1u : 0u, size};
                                    command.bindDescriptorSets(
                                        vk::PipelineBindPoint::eCompute,
                                        *reflectionProbeFilterPipelineLayout, 0, 1,
                                        &sets[static_cast<size_t>(mip - 1u)], 0, nullptr);
                                    command.pushConstants(*reflectionProbeFilterPipelineLayout,
                                                          vk::ShaderStageFlagBits::eCompute, 0,
                                                          sizeof(push), &push);
                                    reflectionProbeFilterPass.record(command, (size + 7u) / 8u,
                                                                     (size + 7u) / 8u, 6);
                                    std::swap(toWrite.srcAccessMask, toWrite.dstAccessMask);
                                    std::swap(toWrite.oldLayout, toWrite.newLayout);
                                    command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                                            vk::PipelineStageFlagBits::eFragmentShader,
                                                            {}, 0, nullptr, 0, nullptr, 1, &toWrite);
                                }
                                if (offscreen3DTimestampQueryPool)
                                    command.writeTimestamp(
                                        vk::PipelineStageFlagBits::eBottomOfPipe,
                                        offscreen3DTimestampQueryPool, 1);
                            });
    if (offscreen3DTimestampQueryPool && offscreen3DTimestampPeriodNs > 0.f) {
        std::array<uint64_t, 2> ticks{};
        const vk::Result result = device->getQueryPoolResults(
            offscreen3DTimestampQueryPool, 0, uint32_t(ticks.size()), sizeof(ticks), ticks.data(),
            sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        if (result == vk::Result::eSuccess && ticks[1] >= ticks[0])
            lastOffscreen3DGpuDurationMs =
                float(double(ticks[1] - ticks[0]) * double(offscreen3DTimestampPeriodNs) * 1.0e-6);
    }
    return true;
#endif
}

Texture *Graphics::newTexture(image::ImageData *data) {
    ASSERT(data != nullptr);
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw Exception("newTexture: only RGBA8 ImageData supported for now");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()));
}

Texture *Graphics::newTexture(image::ImageData *data, const TextureCreateInfo &info) {
    ASSERT(data != nullptr);
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw Exception("newTexture: only RGBA8 ImageData supported for now");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()), info);
}


void Graphics::setTextureSampler(Texture *texture, const TextureSampler &sampler) {
    if (!texture || !texture->gpuHandle || !initialized) return;
    for (auto &owned : ownedGpuTextures) {
        if (owned.get() != texture->gpuHandle) continue;
        // In-flight frames may still be sampling the old sampler / descriptor.
        waitForSharedGpuResources();
        if (owned->sampler) device->destroySampler(owned->sampler);
        owned->samplerState = sampler;
        owned->sampler = createVkSampler(sampler, owned->mipLevels);
        texture->sampler = sampler;
        if (!owned->isCube) writeCombinedImageDescriptor(owned.get());
        invalidateTextureBindings();
        return;
    }
}

bool Graphics::releaseTexture(Texture *texture) {
    if (!texture || !texture->gpuHandle) return false;
    // Renderer-owned fallback textures must never be released by callers.
    if (texture == whiteTexture || texture == flatNormalTexture ||
        texture == flatNormalTexture3D || texture == defaultEnvCubemap)
        return false;

    auto *gpu = static_cast<GpuTexture *>(texture->gpuHandle);
    auto gpuIt = std::find_if(ownedGpuTextures.begin(), ownedGpuTextures.end(),
                              [&](const std::unique_ptr<GpuTexture> &g) {
                                  return g.get() == gpu;
                              });
    if (gpuIt == ownedGpuTextures.end()) return false;

    auto texIt = std::find_if(ownedTextures.begin(), ownedTextures.end(),
                              [&](const std::unique_ptr<Texture> &t) {
                                  return t.get() == texture;
                              });
    if (texIt == ownedTextures.end()) return false;

    // Path-cached textures must leave the hot-reload cache once released.
    for (auto it = texturesByPath.begin(); it != texturesByPath.end();) {
        if (it->second == texture)
            it = texturesByPath.erase(it);
        else
            ++it;
    }

    // In-flight frames may still sample the image / sampler; drain first.
    waitForSharedGpuResources();
    unregisterBindlessTexture(gpu);
    if ((*gpuIt)->sampler) device->destroySampler((*gpuIt)->sampler);
    texture->gpuHandle = nullptr;
    ownedGpuTextures.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)texIt->release();
    ownedTextures.erase(texIt);
    return true;
}

bool Graphics::replaceTexturePixels(Texture *tex, image::ImageData *data) {
    if (!tex || !data) return false;
    if (data->getFormat() != "RGBA8") return false;
    return replaceTexturePixelsRGBA(tex, data->getWidth(), data->getHeight(),
                                    static_cast<const uint8_t *>(data->getData()));
}

bool Graphics::updateTexture(Texture *tex, int width, int height, const uint8_t *rgba) {
    if (!tex || width <= 0 || height <= 0 || !rgba) return false;
    if (width != tex->width || height != tex->height) return false;
    return replaceTexturePixelsRGBA(tex, width, height, rgba);
}

eve::Result<void> Graphics::updateTextureRegion(Texture *tex, int x, int y, int width,
                                                int height,
                                                std::span<const std::uint8_t> rgba,
                                                std::size_t bytesPerRow) {
    const TextureRegionUpload upload{x, y, width, height, rgba, bytesPerRow};
    return updateTextureRegions(tex, std::span<const TextureRegionUpload>(&upload, 1));
}

eve::Result<void> Graphics::updateTextureRegions(
    Texture *tex, std::span<const TextureRegionUpload> regions) {
    if (!tex)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "texture must be non-null",
            "graphics.updateTextureRegions.texture"));
    if (tex->mipmapCount != 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "partial updates require a single-mip texture",
            "graphics.updateTextureRegions.mipmaps"));

    auto *gpu = static_cast<GpuTexture *>(tex->gpuHandle);
    const auto owned = std::find_if(ownedGpuTextures.begin(), ownedGpuTextures.end(),
                                    [gpu](const std::unique_ptr<GpuTexture> &candidate) {
                                        return candidate.get() == gpu;
                                    });
    if (!gpu || owned == ownedGpuTextures.end() || gpu->isCube)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "texture is not an owned 2D texture",
            "graphics.updateTextureRegions.texture"));
    if (regions.empty()) return eve::Result<void>::success();

    std::vector<std::size_t> packedOffsets;
    packedOffsets.reserve(regions.size());
    std::size_t packedSize = 0;
    for (const TextureRegionUpload &region : regions) {
        if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
            region.x > tex->width - region.width || region.y > tex->height - region.height)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "invalid texture region",
                "graphics.updateTextureRegions.region"));
        const std::size_t packedRow = std::size_t(region.width) * 4U;
        const std::size_t stride = region.bytesPerRow == 0 ? packedRow : region.bytesPerRow;
        const std::size_t requiredBytes = stride * std::size_t(region.height - 1) + packedRow;
        if (stride < packedRow || region.rgba.size() < requiredBytes)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "source bytes do not cover the texture region",
                "graphics.updateTextureRegions.bytes"));
        packedOffsets.push_back(packedSize);
        packedSize += packedRow * std::size_t(region.height);
    }

    std::vector<std::uint8_t> packed(packedSize);
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const TextureRegionUpload &region = regions[index];
        const std::size_t packedRow = std::size_t(region.width) * 4U;
        const std::size_t stride = region.bytesPerRow == 0 ? packedRow : region.bytesPerRow;
        for (int row = 0; row < region.height; ++row)
            std::copy_n(region.rgba.data() + std::size_t(row) * stride, packedRow,
                        packed.data() + packedOffsets[index] + std::size_t(row) * packedRow);
    }
    vkb::GenericBuffer staging(
        device, vk::BufferUsageFlagBits::eTransferSrc, vk::DeviceSize(packed.size()),
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    staging.updateLocal(vkb::FrameSlot::gpuIdle(), packed.data(), vk::DeviceSize(packed.size()));

    waitForSharedGpuResources();
    vkb::executeImmediately(device.instance, uploadPool,
                            device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
        gpu->image.setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
        std::vector<vk::BufferImageCopy> copies;
        copies.reserve(regions.size());
        for (std::size_t index = 0; index < regions.size(); ++index) {
            const TextureRegionUpload &source = regions[index];
            vk::BufferImageCopy copy{};
            copy.bufferOffset = packedOffsets[index];
            copy.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            copy.imageOffset = vk::Offset3D{source.x, source.y, 0};
            copy.imageExtent = vk::Extent3D{std::uint32_t(source.width),
                                            std::uint32_t(source.height), 1};
            copies.push_back(copy);
        }
        cb.copyBufferToImage(staging.buffer, gpu->image.image(),
                             vk::ImageLayout::eTransferDstOptimal, copies);

        vk::ImageMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = gpu->image.image();
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                           vk::PipelineStageFlagBits::eVertexShader |
                               vk::PipelineStageFlagBits::eFragmentShader |
                               vk::PipelineStageFlagBits::eComputeShader,
                           {}, 0, nullptr, 0, nullptr, 1, &barrier);
        gpu->image.setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    });
    staging.release();
    return eve::Result<void>::success();
}

bool Graphics::replaceTexturePixelsRGBA(Texture *tex, int w, int h, const uint8_t *rgba) {
    if (!tex || !rgba || w <= 0 || h <= 0 || !initialized) return false;

    TextureCreateInfo info;
    info.sampler = tex->sampler;
    info.generateMipmaps = tex->mipmapCount > 1;
    info = normalizeTextureInfo(info);
    const uint32_t mipLevels =
        info.generateMipmaps ? uint32_t(mipmapCountForSize(w, h)) : 1u;

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->isCube = false;
    gpu->mipLevels = mipLevels;
    gpu->samplerState = info.sampler;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h), mipLevels);
    std::vector<uint8_t> bytes =
        (mipLevels > 1) ? buildMipChain2D(rgba, uint32_t(w), uint32_t(h), mipLevels)
                        : std::vector<uint8_t>(rgba, rgba + size_t(w) * size_t(h) * 4);
    uploadTextureForAllShaderStages(device, uploadPool,
                                    device.getQueue(vkb::QueueType::graphics), gpu->image,
                                    uint32_t(w), uint32_t(h), mipLevels, 1, bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);

    gpu->descriptorSet = vkb::BoundSet{sets[0]};
    writeCombinedImageDescriptor(gpu.get());
    registerBindlessTexture2D(gpu.get());

    void *oldHandle = tex->gpuHandle;
    for (auto &owned : ownedGpuTextures) {
        if (owned.get() != oldHandle) continue;
        // Destroying the old image/sampler while an in-flight frame still
        // samples it is a typical TDR. Drain first, then drop cached sets.
        waitForSharedGpuResources();
        unregisterBindlessTexture(static_cast<GpuTexture *>(oldHandle));
        if (owned->sampler) device->destroySampler(owned->sampler);
        owned = std::move(gpu);
        tex->gpuHandle = owned.get();
        tex->width = w;
        tex->height = h;
        tex->pixelWidth = w;
        tex->pixelHeight = h;
        tex->mipmapCount = int(mipLevels);
        tex->sampler = info.sampler;
        registerBindlessTexture2D(owned.get());
        invalidateTextureBindings();
        return true;
    }

    // Texture not in owned list — attach as new ownership.
    tex->gpuHandle = gpu.get();
    registerBindlessTexture2D(static_cast<GpuTexture *>(tex->gpuHandle));
    tex->width = w;
    tex->height = h;
    tex->pixelWidth = w;
    tex->pixelHeight = h;
    tex->mipmapCount = int(mipLevels);
    tex->sampler = info.sampler;
    ownedGpuTextures.push_back(std::move(gpu));
    return true;
}

Texture *Graphics::newTextureFromFile(const std::string &filename) {
    ASSERT(!filename.empty());
    if (filename.empty()) throw Exception("newTextureFromFile: empty filename");

    const std::string key = normalizeTexPath(filename);
    auto *imgMod = image::Image::create();
    eve::ref<image::ImageData> data(imgMod->newImageDataFromFile(filename));

    auto it = texturesByPath.find(key);
    if (it != texturesByPath.end() && it->second) {
        if (!replaceTexturePixels(it->second, data.get()))
            throw Exception("newTextureFromFile: reload failed '%s'", filename.c_str());
        return it->second;
    }

    Texture *tex = newTexture(data.get());
    texturesByPath[key] = tex;
    return tex;
}

bool Graphics::reloadTextureFromFile(const std::string &filename) {
    if (filename.empty()) return false;
    const std::string key = normalizeTexPath(filename);
    auto it = texturesByPath.find(key);
    if (it == texturesByPath.end() || !it->second) return false;

    image::ImageData *data = nullptr;
    try {
        auto *imgMod = image::Image::create();
        eve::ref<image::ImageData> cached(imgMod->newImageDataFromFile(filename));
        data = cached.get();
    } catch (...) {
        return false;
    }
    if (!data) return false;
    return replaceTexturePixels(it->second, data);
}

}  // namespace eve::graphics::vulkan
