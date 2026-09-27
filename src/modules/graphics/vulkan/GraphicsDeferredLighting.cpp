// Hybrid clustered deferred lighting (Phase C): fullscreen pass that samples
// the Phase B GBuffer and evaluates core PBR with ClusteredLight + CSM.
#include "graphics/ClusteredLight.h"
#include "graphics/Shadow.h"
#include "graphics/shaders/deferred_lighting_frag_spv.inc"
#include "graphics/shaders/deferred_lighting_vert_spv.inc"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

#include <algorithm>
#include <cstring>

namespace eve::graphics::vulkan {
namespace {

std::vector<uint32_t> embeddedSpirv(const uint32_t* words, size_t count) {
    return std::vector<uint32_t>(words, words + count);
}

}  // namespace

void Graphics::destroyDeferredLightingResources() {
    destroyPipeline(device, deferredLightingPipeline);
    destroyPipelineLayout(device, deferredLightingPipelineLayout);
    deferredLightingSetLayout = vk::DescriptorSetLayout{};
    deferredLightingSetLayoutUnique.reset();
    deferredLightingSet = vk::DescriptorSet{};
    deferredLightingUbo.release();
    if (deferredLightingSampler) {
        device->destroySampler(deferredLightingSampler);
        deferredLightingSampler = nullptr;
    }
}

void Graphics::createDeferredLightingPipeline() {
    if (deferredLightingPipeline || !sceneColorRenderPass) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    deferredLightingSetLayoutUnique =
        layoutBuilder.buffer(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(4, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(5, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(6, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(7, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(8, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(9, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(10, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .image(11, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    deferredLightingSetLayout      = *deferredLightingSetLayoutUnique;
    deferredLightingPipelineLayout = createPipelineLayout(device, deferredLightingSetLayout);

    auto             vert       = embeddedSpirv(deferred_lighting_vert_spv, deferred_lighting_vert_spv_count);
    auto             frag       = embeddedSpirv(deferred_lighting_frag_spv, deferred_lighting_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    deferredLightingPipeline    = device.createPipeline()
                                      .useClassicPipeline(vertModule, fragModule)
                                      .setPipelineLayout(deferredLightingPipelineLayout)
                                      .setDynamicStatesViewportScissor()
                                      .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                                     vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise)
                                      .setMultisampler(false, sceneColorSamples)
                                      // Always pass; fragment writes gl_FragDepth from GBuffer hwDepth so
                                      // transparent Forward+ can depth-test against deferred opaques.
                                      .setDepthStencil(true, true, vk::CompareOp::eAlways)
                                      .setColorAttachmentCount(1)
                                      .build(sceneColorRenderPass);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);

    if (!deferredLightingUbo.buffer) {
        deferredLightingUbo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer,
                                     sizeof(DeferredLightingUBO), kHostVisibleCoherent);
    }
    if (!deferredLightingSampler) {
        vkb::SamplerBuilder sb;
        deferredLightingSampler = sb.nearestClamp().build(device);
    }
    if (!deferredLightingSet && descriptorPool) {
        vk::DescriptorSetAllocateInfo alloc{};
        alloc.descriptorPool     = descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &deferredLightingSetLayout;
        deferredLightingSet      = device->allocateDescriptorSets(alloc).front();
    }
}

void Graphics::drawDeferredLighting() {
    if (!sceneColorPassOpen || !sceneColorRenderPass) return;
    auto* gslot = currentGBufferSlot();
    if (!gslot || !gslot->normalGpu.sampler) return;
    createDeferredLightingPipeline();
    if (!deferredLightingPipeline || !deferredLightingSet) return;

    if (!shadowPipeline) createShadowResources();
    vk::ImageView shadowView = currentShadowArrayView();
    if (!shadowView || !shadowSampler) return;

    // Ensure clustered storage exists even with an empty/inactive upload so
    // the SSBO bindings stay valid. Hybrid always samples the cluster tables.
    if (!mesh3dClustered.active) {
        std::vector<ClusteredLightGpu> dirs;
        if (glm::length(glm::vec3(mesh3dFrameUbo.lightDir)) > 1e-4f) {
            ClusteredLightGpu d{};
            d.posRadius = glm::vec4(glm::normalize(glm::vec3(mesh3dFrameUbo.lightDir)), 0.f);
            d.color     = glm::vec4(glm::vec3(mesh3dFrameUbo.lightColor), 1.f);
            dirs.push_back(d);
        }
        ClusteredLightingUpload empty = buildClusteredLighting(
            {}, dirs, mesh3dFrameUbo.view, mesh3dFrameUbo.clipInfo.x, mesh3dFrameUbo.clipInfo.y,
            std::max(1, sceneColorWidth), std::max(1, sceneColorHeight), /*fovYRad*/ 1.f, mesh3dFrameUbo.ambient);
        empty.active = true;
        setMesh3DClusteredLighting(empty);
    } else {
        uploadClusteredLighting(mesh3dClustered);
    }
    auto& storage = currentClusteredStorage();
    if (!storage.lightsBuf.buffer || !storage.tableBuf.buffer || !storage.indicesBuf.buffer) return;

    DeferredLightingUBO ubo{};
    ubo.invViewProj = glm::inverse(mesh3dFrameUbo.mvp);
    ubo.view        = mesh3dFrameUbo.view;
    ubo.lightDir    = mesh3dFrameUbo.lightDir;
    // Mesh3DUBO packs lightCount in lightDir.w; deferred_lighting.frag uses w as
    // the primary-enable flag. Re-encode enable from the direction vector.
    ubo.lightDir.w = glm::length(glm::vec3(mesh3dFrameUbo.lightDir)) > 1e-4f ? 1.f : 0.f;
    ubo.lightColor = mesh3dFrameUbo.lightColor;
    ubo.cameraPos  = glm::vec4(glm::vec3(mesh3dFrameUbo.cameraPos), 0.f);
    ubo.ambient    = mesh3dFrameUbo.ambient;
    if (mesh3dClustered.active) {
        ubo.gridInfo   = mesh3dClustered.gridInfo;
        ubo.clipInfo   = mesh3dClustered.clipInfo;
        ubo.ambient    = mesh3dClustered.ambient;
        ubo.lightDir   = mesh3dClustered.primaryDir;
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dClustered.primaryColor), 0.f);
        ubo.view       = mesh3dClustered.view;
    }
    ubo.clipInfo.z = float(std::max(1, sceneColorWidth));
    ubo.clipInfo.w = float(std::max(1, sceneColorHeight));
    deferredLightingUbo.updateLocal(frameToken(), &ubo, sizeof(ubo));

    // Shadow UBO: stage the latest CSM upload into the clustered shadow ring.
    ensureMesh3dStrides();
    auto& cfslots = currentMesh3dClusteredFrameSlots();
    ensureMesh3dClusteredRing(cfslots);
    const uint32_t shadowOffset = 0;
    std::memcpy(static_cast<uint8_t*>(cfslots.shadowRing.map()) + shadowOffset, &mesh3dShadows.ubo, sizeof(ShadowUBO));

    auto writeImage = [&](uint32_t binding, vk::ImageView view, vk::Sampler sampler,
                          vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal) {
        vk::DescriptorImageInfo img{};
        img.sampler     = sampler;
        img.imageView   = view;
        img.imageLayout = layout;
        vk::WriteDescriptorSet w{};
        w.dstSet          = deferredLightingSet;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eCombinedImageSampler;
        w.pImageInfo      = &img;
        device->updateDescriptorSets(w, {});
    };
    auto writeBuffer = [&](uint32_t binding, vk::Buffer buffer, vk::DeviceSize size, vk::DescriptorType type) {
        vk::DescriptorBufferInfo buf{};
        buf.buffer = buffer;
        buf.offset = 0;
        buf.range  = size;
        vk::WriteDescriptorSet w{};
        w.dstSet          = deferredLightingSet;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = type;
        w.pBufferInfo     = &buf;
        device->updateDescriptorSets(w, {});
    };

    writeBuffer(0, deferredLightingUbo.buffer, sizeof(DeferredLightingUBO), vk::DescriptorType::eUniformBuffer);
    writeImage(1, gslot->normal.imageView(), deferredLightingSampler);
    writeImage(2, gslot->depthColor.imageView(), deferredLightingSampler);
    writeImage(3, gslot->albedo.imageView(), deferredLightingSampler);
    writeImage(4, gslot->pbrParams.imageView(), deferredLightingSampler);
    writeImage(5, gslot->emissive.imageView(), deferredLightingSampler);
    writeImage(6, gslot->depth.imageView(), deferredLightingSampler);
    writeBuffer(7, storage.lightsBuf.buffer, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    writeBuffer(8, storage.tableBuf.buffer, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    writeBuffer(9, storage.indicesBuf.buffer, VK_WHOLE_SIZE, vk::DescriptorType::eStorageBuffer);
    {
        vk::DescriptorBufferInfo buf{};
        buf.buffer = cfslots.shadowRing.buffer;
        buf.offset = shadowOffset;
        buf.range  = sizeof(ShadowUBO);
        vk::WriteDescriptorSet w{};
        w.dstSet          = deferredLightingSet;
        w.dstBinding      = 10;
        w.descriptorCount = 1;
        w.descriptorType  = vk::DescriptorType::eUniformBuffer;
        w.pBufferInfo     = &buf;
        device->updateDescriptorSets(w, {});
    }
    writeImage(11, shadowView, vk::Sampler(shadowSampler), currentShadowMap().image.currentLayout());

    auto& cb = currentPresentCb();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, deferredLightingPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, deferredLightingPipelineLayout, 0, 1, &deferredLightingSet,
                          0, nullptr);
    setViewportAndScissor(cb, uint32_t(sceneColorWidth), uint32_t(sceneColorHeight));
    cb.draw(3, 1, 0, 0);
}

}  // namespace eve::graphics::vulkan
