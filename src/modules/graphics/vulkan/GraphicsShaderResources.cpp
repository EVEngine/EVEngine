#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include "graphics/ShaderResourceValidation.h"
#include "graphics/shaders/mesh3d_vert_spv.inc"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/ShaderResourceReload.h"

namespace eve::graphics::vulkan {
namespace {
struct ResourceImage {
    vk::UniqueDeviceMemory    memory;
    vk::UniqueImage           image;
    vk::UniqueImageView       view;
    vk::UniqueSampler         sampler;
    ShaderImageInput          shape;
    std::weak_ptr<const void> contentOwner;
};
vk::Format imageFormat(ShaderImageFormat value) {
    switch (value) {
        case ShaderImageFormat::R8: return vk::Format::eR8Unorm;
        case ShaderImageFormat::RG8: return vk::Format::eR8G8Unorm;
        case ShaderImageFormat::R16: return vk::Format::eR16Unorm;
        case ShaderImageFormat::RGBA8: return vk::Format::eR8G8B8A8Unorm;
        case ShaderImageFormat::BGRA8: return vk::Format::eB8G8R8A8Unorm;
        case ShaderImageFormat::RGBA8Srgb: return vk::Format::eR8G8B8A8Srgb;
        case ShaderImageFormat::BGRA8Srgb: return vk::Format::eB8G8R8A8Srgb;
        case ShaderImageFormat::BC1: return vk::Format::eBc1RgbaUnormBlock;
        case ShaderImageFormat::BC1Srgb: return vk::Format::eBc1RgbaSrgbBlock;
        case ShaderImageFormat::BC3: return vk::Format::eBc3UnormBlock;
        case ShaderImageFormat::BC3Srgb: return vk::Format::eBc3SrgbBlock;
        case ShaderImageFormat::BC7: return vk::Format::eBc7UnormBlock;
        case ShaderImageFormat::BC7Srgb: return vk::Format::eBc7SrgbBlock;
    }
    return vk::Format::eUndefined;
}
Result<void> failure(std::string message, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), "shader.resources"));
}
}  // namespace

struct MeshShaderResources {
    std::vector<std::shared_ptr<ResourceImage>> images;
    std::unique_ptr<vkb::GenericBuffer>         constants;
    std::vector<ShaderImageInput>               shapes;  // Byte spans are cleared; only reload ABI metadata remains.
    std::size_t                                 constantSize = 0;
    std::vector<std::byte>                      constantBytes;
    std::unique_ptr<vkb::GenericBuffer>         instances;
    std::vector<std::byte>                      instanceBytes;
    vk::UniqueDescriptorSetLayout               setLayout;
    vk::UniqueDescriptorPool                    pool;
    vk::UniquePipelineLayout                    pipelineLayout;
};
void MeshShaderResourcesDeleter::operator()(MeshShaderResources* resources) const noexcept { delete resources; }
bool isMeshResourceImageShared(const GpuShader& first, std::uint32_t firstSlot, const GpuShader& second,
                               std::uint32_t secondSlot) {
    return first.resources && second.resources && firstSlot < first.resources->images.size() &&
           secondSlot < second.resources->images.size() &&
           first.resources->images[firstSlot] == second.resources->images[secondSlot];
}

Result<void> validateMeshResourceShaderReload(const GpuShader& shader, std::span<const uint32_t> vertex,
                                              std::span<const uint32_t> fragment) {
    if (!shader.resources) return Result<void>::success();
    return detail::validateResourceShaderStages(
        vertex, fragment,
        ShaderResourceInputs{shader.resources->shapes, shader.resources->constantBytes,
                             shader.resources->instanceBytes});
}

std::uint32_t meshResourceInstanceCount(const GpuShader& shader) {
    return shader.resources ? static_cast<std::uint32_t>(shader.resources->instanceBytes.size() / 64) : 0;
}

Result<void> Graphics::replaceMeshShaderResources(Shader& shader, const std::vector<uint32_t>& vertSpv,
                                                  const std::vector<uint32_t>& fragSpv,
                                                  const ShaderResourceInputs&  inputs) {
    auto found = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const auto& gpu) { return gpu->owner == &shader; });
    if (!initialized || found == ownedGpuShaders.end() || !(*found)->isMesh3D || (*found)->isHair3D ||
        shader.isXray() || swapchainPassOpen || offscreen3DPassOpen)
        return failure("Expected an owned, ordinary mesh shader outside frame submission");
    if (inputs.images.empty() || inputs.images.size() > 16 || inputs.constants.size() > 65536 ||
        inputs.constants.size() % 16 != 0)
        return failure("Expected 1-16 images and at most 64 KiB of aligned uniform bytes");
    std::set<uint32_t>                          bindings;
    std::vector<std::vector<ShaderImageRegion>> regions;
    const auto&                                 limits        = device.physical_device.properties.limits;
    auto                                        instanceCount = shaderInstanceMatrixCount(inputs.instanceMatrices);
    if (!instanceCount) return Result<void>::failure(instanceCount.status());
    if (inputs.instanceMatrices.size() > limits.maxStorageBufferRange ||
        (!inputs.instanceMatrices.empty() &&
         (limits.maxPerStageDescriptorStorageBuffers < 2 || limits.maxDescriptorSetStorageBuffers < 2)))
        return failure("Instance matrix buffer exceeds device limits", DiagnosticCode::Unsupported);
    if (inputs.images.size() + 13 > limits.maxPerStageDescriptorSamplers ||
        inputs.images.size() + 13 > limits.maxPerStageDescriptorSampledImages ||
        inputs.constants.size() > limits.maxUniformBufferRange)
        return failure("Resource program exceeds device descriptor or uniform limits", DiagnosticCode::Unsupported);
    for (const auto& image : inputs.images) {
        if (!bindings.insert(image.binding).second) return failure("Duplicate resource image binding");
        auto layout = shaderImageRegions(image);
        if (!layout) return Result<void>::failure(layout.status());
        if (image.width > limits.maxImageDimension2D || image.height > limits.maxImageDimension2D ||
            image.layers > limits.maxImageArrayLayers)
            return failure("Resource image exceeds device dimensions", DiagnosticCode::Unsupported);
        const auto flags = device.physical_device->getFormatProperties(imageFormat(image.format)).optimalTilingFeatures;
        const auto required = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
        if ((flags & required) != required ||
            ((image.sampler.min == FilterMode::Linear || image.sampler.mag == FilterMode::Linear ||
              image.sampler.mipmap == MipmapMode::Linear) &&
             !(flags & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)))
            return failure("Device cannot sample the requested image format/filter", DiagnosticCode::Unsupported);
        regions.push_back(std::move(layout).takeValue());
    }
    auto admitted = detail::validateResourceShaderStages(vertSpv, fragSpv, inputs);
    if (!admitted) return admitted;
    auto candidate      = std::make_unique<GpuShader>();
    candidate->isMesh3D = true;
    candidate->owner    = &shader;
    candidate->resources.reset(new MeshShaderResources);
    auto&                 resources = *candidate->resources;
    std::vector<uint32_t> vertex    = vertSpv;
    std::vector<uint32_t> fragment  = fragSpv;
    if (vertex.empty()) vertex.assign(mesh3d_vert_spv, mesh3d_vert_spv + mesh3d_vert_spv_count);
    try {
        const auto stages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        std::vector<vk::DescriptorSetLayoutBinding> layout;
        for (const auto& image : inputs.images)
            layout.emplace_back(image.binding, vk::DescriptorType::eCombinedImageSampler, 1, stages);
        if (!inputs.constants.empty()) layout.emplace_back(32, vk::DescriptorType::eUniformBuffer, 1, stages);
        if (!inputs.instanceMatrices.empty())
            layout.emplace_back(33, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex);
        resources.setLayout = device->createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo{{}, layout});
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eCombinedImageSampler, uint32_t(inputs.images.size())}};
        if (!inputs.constants.empty()) poolSizes.emplace_back(vk::DescriptorType::eUniformBuffer, 1);
        if (!inputs.instanceMatrices.empty()) poolSizes.emplace_back(vk::DescriptorType::eStorageBuffer, 1);
        resources.pool       = device->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo{{}, 1, poolSizes});
        const auto setLayout = *resources.setLayout;
        candidate->resourceSet =
            device->allocateDescriptorSets(vk::DescriptorSetAllocateInfo{*resources.pool, 1, &setLayout})[0];
        const vk::DescriptorSetLayout layouts[] = {mesh3dSetLayout, setLayout};
        const vk::PushConstantRange   push{stages, 0, Shader::kPushConstantBytes};
        resources.pipelineLayout =
            device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo{{}, 2, layouts, 1, &push});
        candidate->pipelineLayout = *resources.pipelineLayout;
        resources.images.reserve(inputs.images.size());
        std::vector<std::unique_ptr<vkb::GenericBuffer>>    pendingStaging;
        std::vector<std::function<void(vk::CommandBuffer)>> uploadCommands;
        size_t                                              pendingBytes = 0;
        auto                                                flushUploads = [&] {
            if (uploadCommands.empty()) return;
            vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                                                                                   [&](vk::CommandBuffer command) {
                                        for (auto& record : uploadCommands) record(command);
                                    });
            uploadCommands.clear();
            pendingStaging.clear();
            pendingBytes = 0;
        };
        for (size_t index = 0; index < inputs.images.size(); ++index) {
            const auto&                    input = inputs.images[index];
            std::shared_ptr<ResourceImage> output;
            if (input.contentOwner) {
                for (const auto& prior : ownedGpuShaders) {
                    if (!prior->resources) continue;
                    for (const auto& image : prior->resources->images) {
                        auto        owner = image->contentOwner.lock();
                        const auto& shape = image->shape;
                        const auto& a     = shape.sampler;
                        const auto& b     = input.sampler;
                        if (owner && !owner.owner_before(input.contentOwner) &&
                            !input.contentOwner.owner_before(owner) && shape.format == input.format &&
                            shape.dimension == input.dimension && shape.width == input.width &&
                            shape.height == input.height && shape.layers == input.layers &&
                            shape.mipLevels == input.mipLevels && a.min == b.min && a.mag == b.mag &&
                            a.mipmap == b.mipmap && a.repeatU == b.repeatU && a.repeatV == b.repeatV &&
                            a.repeatW == b.repeatW && a.maxAnisotropy == b.maxAnisotropy && a.lodBias == b.lodBias &&
                            a.minLod == b.minLod && a.maxLod == b.maxLod) {
                            output = image;
                            break;
                        }
                    }
                    if (output) break;
                }
            }
            if (!output) {
                output              = std::make_shared<ResourceImage>();
                output->shape       = input;
                output->shape.bytes = {};
                output->shape.contentOwner.reset();
                output->contentOwner = input.contentOwner;
                vk::ImageCreateInfo info{};
                info.imageType   = vk::ImageType::e2D;
                info.format      = imageFormat(input.format);
                info.extent      = vk::Extent3D{input.width, input.height, 1};
                info.mipLevels   = input.mipLevels;
                info.arrayLayers = input.layers;
                info.samples     = vk::SampleCountFlagBits::e1;
                info.tiling      = vk::ImageTiling::eOptimal;
                info.usage       = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
                if (input.dimension == ShaderImageDimension::Cube)
                    info.flags = vk::ImageCreateFlagBits::eCubeCompatible;
                output->image      = device->createImageUnique(info);
                const auto  memory = device->getImageMemoryRequirements(*output->image);
                uint32_t    type   = UINT32_MAX;
                const auto& props  = device.physical_device.memory_properties;
                for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
                    if ((memory.memoryTypeBits & (1u << i)) &&
                        (props.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
                        type = i;
                        break;
                    }
                if (type == UINT32_MAX) throw std::runtime_error("No device-local image memory type");
                output->memory = device->allocateMemoryUnique(vk::MemoryAllocateInfo{memory.size, type});
                device->bindImageMemory(*output->image, *output->memory, 0);
                const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, input.mipLevels, 0,
                                                      input.layers};
                const auto viewType = input.dimension == ShaderImageDimension::Cube      ? vk::ImageViewType::eCube
                                      : input.dimension == ShaderImageDimension::Array2D ? vk::ImageViewType::e2DArray
                                                                                         : vk::ImageViewType::e2D;
                output->view        = device->createImageViewUnique(
                    vk::ImageViewCreateInfo{{}, *output->image, viewType, info.format, {}, range});
                // A dedicated immutable sampler; use the same normalization as ordinary textures.
                output->sampler = vk::UniqueSampler(createVkSampler(input.sampler, input.mipLevels), device.instance);
                size_t                           stagingSize = 0;
                std::vector<vk::BufferImageCopy> copies;
                for (const auto& region : regions[index]) {
                    const auto offset = (stagingSize + 15) & ~size_t(15);
                    stagingSize       = offset + region.size;
                    copies.emplace_back(
                        offset, 0, 0,
                        vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, region.mip, region.layer, 1},
                        vk::Offset3D{}, vk::Extent3D{region.width, region.height, 1});
                }
                auto staging = std::make_unique<vkb::GenericBuffer>(
                    device, vk::BufferUsageFlagBits::eTransferSrc, stagingSize,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                // Copy directly into the new, unmapped upload allocation. Only the
                // recorded regions are read by the GPU; alignment gaps need no initialization.
                auto* mapped = static_cast<std::byte*>(staging->map());
                for (size_t regionIndex = 0; regionIndex < regions[index].size(); ++regionIndex) {
                    const auto& region = regions[index][regionIndex];
                    std::memcpy(mapped + copies[regionIndex].bufferOffset, input.bytes.data() + region.offset,
                                region.size);
                }
                staging->unmap();
                const auto buffer = staging->buffer;
                pendingBytes += stagingSize;
                pendingStaging.push_back(std::move(staging));
                uploadCommands.push_back(
                    [output, range, buffer, copies = std::move(copies)](vk::CommandBuffer command) {
                        vk::ImageMemoryBarrier barrier{{},
                                                       vk::AccessFlagBits::eTransferWrite,
                                                       vk::ImageLayout::eUndefined,
                                                       vk::ImageLayout::eTransferDstOptimal,
                                                       VK_QUEUE_FAMILY_IGNORED,
                                                       VK_QUEUE_FAMILY_IGNORED,
                                                       *output->image,
                                                       range};
                        command.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                                vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);
                        command.copyBufferToImage(buffer, *output->image, vk::ImageLayout::eTransferDstOptimal, copies);
                        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
                        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                        barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
                        barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
                        command.pipelineBarrier(
                            vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader, {},
                            {}, {}, barrier);
                    });
                if (pendingBytes >= 64ull * 1024 * 1024) flushUploads();
            }
            const vk::DescriptorImageInfo descriptor{*output->sampler, *output->view,
                                                     vk::ImageLayout::eShaderReadOnlyOptimal};
            device->updateDescriptorSets(vk::WriteDescriptorSet{candidate->resourceSet, input.binding, 0, 1,
                                                                vk::DescriptorType::eCombinedImageSampler, &descriptor},
                                         {});
            resources.images.push_back(std::move(output));
            resources.shapes.push_back(input);
            resources.shapes.back().bytes = {};
            resources.shapes.back().contentOwner.reset();
        }
        flushUploads();
        if (!inputs.constants.empty()) {
            resources.constants = std::make_unique<vkb::GenericBuffer>(
                device, vk::BufferUsageFlagBits::eUniformBuffer, inputs.constants.size(),
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            resources.constants->updateLocal(vkb::FrameSlot::gpuIdle(), inputs.constants.data(),
                                             inputs.constants.size());
            resources.constantSize = inputs.constants.size();
            resources.constantBytes.assign(inputs.constants.begin(), inputs.constants.end());
            const vk::DescriptorBufferInfo descriptor{resources.constants->buffer, 0, inputs.constants.size()};
            device->updateDescriptorSets(
                vk::WriteDescriptorSet{candidate->resourceSet, 32, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr,
                                       &descriptor},
                {});
        }
        if (!inputs.instanceMatrices.empty()) {
            resources.instances = std::make_unique<vkb::GenericBuffer>(
                device, vk::BufferUsageFlagBits::eStorageBuffer, inputs.instanceMatrices.size(),
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            resources.instances->updateLocal(vkb::FrameSlot::gpuIdle(), inputs.instanceMatrices.data(),
                                             inputs.instanceMatrices.size());
            resources.instanceBytes.assign(inputs.instanceMatrices.begin(), inputs.instanceMatrices.end());
            const vk::DescriptorBufferInfo descriptor{resources.instances->buffer, 0, inputs.instanceMatrices.size()};
            device->updateDescriptorSets(
                vk::WriteDescriptorSet{candidate->resourceSet, 33, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr,
                                       &descriptor},
                {});
        }
        candidate->mesh3dPipeline = createMesh3DStylePipeline(
            vertex, fragSpv, candidate->pipelineLayout, activeScenePass(), activeSceneSamples(), shader.meshBlend,
            shader.meshDepthWrite, shader.meshDoubleSided, shader.meshRasterState());
        candidate->mesh3dOffscreenPipeline = createMesh3DStylePipeline(
            vertex, fragSpv, candidate->pipelineLayout, offscreen3DRenderPass, vk::SampleCountFlagBits::e1,
            shader.meshBlend, shader.meshDepthWrite, shader.meshDoubleSided, shader.meshRasterState());
        candidate->mesh3dHdrOffscreenPipeline = createMesh3DStylePipeline(
            vertex, fragSpv, candidate->pipelineLayout, hdrOffscreen3DRenderPass, vk::SampleCountFlagBits::e1,
            shader.meshBlend, shader.meshDepthWrite, shader.meshDoubleSided, shader.meshRasterState());
        waitForSharedGpuResources();
    } catch (const std::exception& error) {
        for (auto pipeline :
             {candidate->mesh3dPipeline, candidate->mesh3dOffscreenPipeline, candidate->mesh3dHdrOffscreenPipeline})
            if (pipeline) device->destroyPipeline(pipeline);
        return failure(error.what(), DiagnosticCode::Failed);
    }
    auto& old = **found;
    for (auto pipeline :
         {old.mesh3dPipeline, old.mesh3dXrayPipeline, old.mesh3dOffscreenPipeline, old.mesh3dHdrOffscreenPipeline})
        if (pipeline) device->destroyPipeline(pipeline);
    old              = std::move(*candidate);
    shader.gpuHandle = &old;
    shader.setSpirv(std::move(vertex), std::move(fragment));
    lastMesh3dPipeline = nullptr;
    return Result<void>::success();
}
}  // namespace eve::graphics::vulkan
