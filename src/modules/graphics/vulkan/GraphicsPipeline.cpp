// Vulkan backend implementation — swapchain and pipeline creation.
//
// Re-split from the merged dev single-TU Graphics.cpp (pure move;
// dev changes preserved). Shared helpers live in GraphicsInternal.h.

#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/Light.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Outline.h"
#include "graphics/RenderControl.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

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
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <memory>


#include <assimp/mesh.h>
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/vector3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "graphics/shaders/color_vert_spv.inc"
#include "graphics/shaders/color_frag_spv.inc"
#include "graphics/shaders/textured_vert_spv.inc"
#include "graphics/shaders/textured_frag_spv.inc"
#include "graphics/shaders/mesh3d_vert_spv.inc"
#include "graphics/shaders/mesh3d_frag_spv.inc"
#include "graphics/shaders/mesh3d_gpudriven_vert_spv.inc"
#include "graphics/shaders/mesh3d_gpudriven_frag_spv.inc"
#include "graphics/shaders/resolve_vis_vert_spv.inc"
#include "graphics/shaders/resolve_vis_frag_spv.inc"
#include "graphics/shaders/mesh3d_clustered_vert_spv.inc"
#include "graphics/shaders/mesh3d_clustered_frag_spv.inc"
#include "graphics/shaders/mesh3d_shadow_vert_spv.inc"
#include "graphics/shaders/mesh3d_shadow_frag_spv.inc"
#include "graphics/shaders/mesh3d_shadow_alpha_vert_spv.inc"
#include "graphics/shaders/mesh3d_shadow_alpha_frag_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_vert_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_frag_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_alpha_frag_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_skin_vert_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_skin_frag_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_skin_alpha_frag_spv.inc"
#include "graphics/shaders/mesh3d_shadow_skin_vert_spv.inc"
#include "graphics/shaders/mesh3d_shadow_skin_alpha_frag_spv.inc"
#include "graphics/shaders/decal_box_vert_spv.inc"
#include "graphics/shaders/decal_box_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/lit2d_vert_spv.inc"
#include "graphics/shaders/lit2d_frag_spv.inc"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {

namespace {

vk::Pipeline createSolidColorPipeline(vkb::Device &device, const vkb::BuiltRenderPass &renderPass,
                                      vk::PipelineLayout layout,
                                      BlendMode mode = BlendMode::Opaque) {
    if (mode == BlendMode::Additive || mode == BlendMode::Premultiplied ||
        mode == BlendMode::Multiply) {
        // cbs/attachments must outlive build(); the builder must stay a single
        // expression — copying the builder into a named local leaves its
        // shader-stage pName pointers dangling into the temporary's storage.
        std::vector<vk::PipelineColorBlendAttachmentState> attachments(1,
                                                                       makeBlendAttachment(mode));
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.logicOpEnable = false;
        cbs.attachmentCount = 1;
        cbs.pAttachments = attachments.data();
        return device.createPipeline()
            .useClassicPipeline(embeddedSpirv(color_vert_spv), embeddedSpirv(color_frag_spv))
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<ColorVertex>()
                                     .addAttributeDescription<ColorVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                           vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
            .setColorBlending(cbs)
            .build(renderPass);
    }
    if (mode == BlendMode::Alpha) {
        return device.createPipeline()
            .useClassicPipeline(embeddedSpirv(color_vert_spv), embeddedSpirv(color_frag_spv))
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<ColorVertex>()
                                     .addAttributeDescription<ColorVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                           vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
            .setAlphaBlending(1)
            .build(renderPass);
    }
    // Opaque: keep the original single-expression chain (no explicit blend state).
    return device.createPipeline()
        .useClassicPipeline(embeddedSpirv(color_vert_spv), embeddedSpirv(color_frag_spv))
        .setPipelineLayout(layout)
        .setVertexInputState(vkb::VertexInputStateBuilder()
                                 .addInputBinding<ColorVertex>()
                                 .addAttributeDescription<ColorVertex>())
        .setDynamicStatesViewportScissor()
        .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                       vk::FrontFace::eCounterClockwise)
        .build(renderPass);
}
}  // namespace

// --- Swapchain and graphics pipelines -----------------------------------------

void Graphics::createSwapchainAndPipeline() {
    initGpuTiming();
    // Framebuffers / command buffers alias the current swapchain images; tear
    // them down before replacing the swapchain (destroy() waitIdles first).
    presentRecording = {};
    swapchainPass = {};
    presentModel.destroy();
    presentModel = vkb::Present{};

    vkb::SwapchainBuilder swapchainBuilder = device.createSwapchain();
    if (pixelWidth > 0 && pixelHeight > 0)
        swapchainBuilder.set_desired_extent(uint32_t(pixelWidth), uint32_t(pixelHeight));
    // Prefer UNORM so clear/draw Color floats match getPixel without sRGB encode.
    swapchainBuilder.set_desired_format(
        {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear});
    if (vsyncEnabled) {
        swapchainBuilder.set_desired_present_mode(vk::PresentModeKHR::eMailbox);
        swapchainBuilder.add_fallback_present_mode(vk::PresentModeKHR::eFifo);
    } else {
        swapchainBuilder.set_desired_present_mode(vk::PresentModeKHR::eImmediate);
        swapchainBuilder.add_fallback_present_mode(vk::PresentModeKHR::eMailbox);
        swapchainBuilder.add_fallback_present_mode(vk::PresentModeKHR::eFifo);
    }
    // Allow screen getPixel / newImageData readback after present.
    swapchainBuilder.add_image_usage_flags(vk::ImageUsageFlagBits::eTransferSrc);
    auto swapRet = swapchainBuilder.set_old_swapchain(swapchain).build();
    swapchain.destroy();
    swapchain = swapRet;

    depthImage = vkb::DepthStencilImage{device, swapchain.extent.width, swapchain.extent.height, depthFormat};

    if (!renderpass) {
        vkb::RenderPassBuilder rpBuilder{device};
        renderpass =
            rpBuilder.addPresentAttachment(swapchain.image_format, vk::AttachmentLoadOp::eClear)
                .addDepthAttachment(depthFormat, vk::AttachmentLoadOp::eClear,
                                    vk::AttachmentStoreOp::eDontCare)
                .addSubpass(vkb::SubpassBuilder()
                                .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                .setDepthStencilAttachment(
                                    1, vk::ImageLayout::eDepthStencilAttachmentOptimal))
                .addDependency(VK_SUBPASS_EXTERNAL, 0,
                               vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                   vk::PipelineStageFlagBits::eEarlyFragmentTests,
                               vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                   vk::PipelineStageFlagBits::eEarlyFragmentTests,
                               {},
                               vk::AccessFlagBits::eColorAttachmentRead |
                                   vk::AccessFlagBits::eColorAttachmentWrite |
                                   vk::AccessFlagBits::eDepthStencilAttachmentWrite)
                .build();
    }

    if (!pipeline) {
        pipelineLayout = createPipelineLayout(device);
        pipeline = createSolidColorPipeline(device, renderpass, pipelineLayout);
        solidAlphaPipeline = createSolidColorPipeline(device, renderpass, pipelineLayout,
                                                      BlendMode::Alpha);
        additiveSolidPipeline = createSolidColorPipeline(device, renderpass, pipelineLayout,
                                                         BlendMode::Additive);
        premultipliedSolidPipeline = createSolidColorPipeline(
            device, renderpass, pipelineLayout, BlendMode::Premultiplied);
        multiplySolidPipeline = createSolidColorPipeline(device, renderpass, pipelineLayout,
                                                         BlendMode::Multiply);
    }

    // Scene-pass pipelines are initially built for the swapchain render pass at
    // 1x; ensureScenePassPipelines rebuilds them only when the active scene pass
    // (handle or sample count) differs.
    scenePassPipelineTarget = vk::RenderPass(renderpass);
    scenePassPipelineSamples = vk::SampleCountFlagBits::e1;

    presentModel = device.createPresent(swapchain).build(renderpass, depthImage.imageView());
    // Multi-frame overlap: submit + present without waiting on this frame's
    // fence, so the CPU can build frame N+1 while the GPU still renders frame N.
    // Present caps frames_in_flight at 2 (independent of swapchain image count)
    // so present-wait semaphores are not reused while WSI still holds them.
    // Per-frame mutable GPU resources must be multi-buffered (see *FrameSlots).
    presentModel.synchronous_frames = false;
    ensurePresentCaptureHook();
    swapchainDirty = false;
}

void Graphics::createTexturedPipeline() {
    if (texPipeline) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    texSetLayoutUnique = layoutBuilder
                             .image(0, vk::DescriptorType::eCombinedImageSampler,
                                    vk::ShaderStageFlagBits::eFragment, 1)
                             .image(1, vk::DescriptorType::eCombinedImageSampler,
                                    vk::ShaderStageFlagBits::eFragment, 1)
                             .createUnique(device.instance);
    texSetLayout = *texSetLayoutUnique;

    vk::DescriptorPoolSize poolSizes[] = {
        {vk::DescriptorType::eCombinedImageSampler, 8192},
        {vk::DescriptorType::eUniformBuffer, 2048},
        // Dynamic-offset UBOs (per-draw mesh3d ring) count against their own
        // pool size type; without this entry the first dynamic set allocation
        // fails with VK_ERROR_OUT_OF_POOL_MEMORY.
        {vk::DescriptorType::eUniformBufferDynamic, 4096},
        {vk::DescriptorType::eStorageBuffer, 256},
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 4096;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    descriptorPool = device->createDescriptorPool(poolInfo);

    texPipelineLayout = createPipelineLayout(device, texSetLayout);
    const auto pcr =
        pushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                          Shader::kPushConstantBytes);
    shaderPipelineLayout = createPipelineLayout(device, texSetLayout, &pcr);

    auto vert = embeddedSpirv(textured_vert_spv);
    auto frag = embeddedSpirv(textured_frag_spv);
    texPipeline = createTexturedStylePipeline(vert, frag, renderpass, texPipelineLayout);
    additiveTexPipeline = createTexturedStylePipeline(vert, frag, renderpass, texPipelineLayout,
                                                      BlendMode::Additive);
    premultipliedTexPipeline = createTexturedStylePipeline(
        vert, frag, renderpass, texPipelineLayout, BlendMode::Premultiplied);
    multiplyTexPipeline = createTexturedStylePipeline(vert, frag, renderpass, texPipelineLayout,
                                                      BlendMode::Multiply);
    opaqueTexPipeline = createTexturedStylePipeline(vert, frag, renderpass, texPipelineLayout,
                                                    BlendMode::Opaque);

    createLit2DPipeline();
}

vk::Pipeline Graphics::createTexturedStylePipeline(const std::vector<uint32_t> &vert,
                                                   const std::vector<uint32_t> &frag,
                                                   const vkb::BuiltRenderPass &rp,
                                                   vk::PipelineLayout layout, BlendMode mode,
                                                   vk::SampleCountFlagBits samples) {
    ShaderModulePair modules(device, vert, frag);
    if (mode == BlendMode::Additive || mode == BlendMode::Premultiplied ||
        mode == BlendMode::Multiply) {
        // cbs/attachments must outlive build(); keep the builder as a single
        // expression (see createSolidColorPipeline).
        std::vector<vk::PipelineColorBlendAttachmentState> attachments(1,
                                                                       makeBlendAttachment(mode));
        vk::PipelineColorBlendStateCreateInfo cbs{};
        cbs.logicOpEnable = false;
        cbs.attachmentCount = 1;
        cbs.pAttachments = attachments.data();
        return device.createPipeline()
            .useClassicPipeline(modules.vert, modules.frag)
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<TexturedVertex>()
                                     .addAttributeDescription<TexturedVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                           vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
            .setDepthStencil(false, false)
            .setMultisampler(false, samples)
            .setColorBlending(cbs)
            .build(rp);
    }
    if (mode == BlendMode::Opaque) {
        return device.createPipeline()
            .useClassicPipeline(modules.vert, modules.frag)
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<TexturedVertex>()
                                     .addAttributeDescription<TexturedVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                           vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
            .setDepthStencil(false, false)
            .setMultisampler(false, samples)
            .setColorBlending()
            .build(rp);
    }
    // Alpha (original behavior).
    return device.createPipeline()
        .useClassicPipeline(modules.vert, modules.frag)
        .setPipelineLayout(layout)
        .setVertexInputState(vkb::VertexInputStateBuilder()
                                 .addInputBinding<TexturedVertex>()
                                 .addAttributeDescription<TexturedVertex>())
        .setDynamicStatesViewportScissor()
        .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                       vk::FrontFace::eCounterClockwise)
        .setDepthStencil(false, false)
        .setMultisampler(false, samples)
        .setAlphaBlending(1)
        .build(rp);
}

void Graphics::createLit2DPipeline() {
    if (lit2dPipeline) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    lit2dSetLayoutUnique =
        layoutBuilder
            .image(0, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(2, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    lit2dSetLayout = *lit2dSetLayoutUnique;

    lit2dPipelineLayout = createPipelineLayout(device, lit2dSetLayout);

    // Offscreen (synchronous) lit-2D path owns a dedicated UBO. The swapchain
    // path's per-frame UBOs are allocated lazily in currentLighting2dUbo().
    offscreenLighting2dUbo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer,
                                    sizeof(Lighting2DUBO),
                                    vk::MemoryPropertyFlagBits::eHostVisible |
                                        vk::MemoryPropertyFlagBits::eHostCoherent);

    auto vert = embeddedSpirv(lit2d_vert_spv);
    auto frag = embeddedSpirv(lit2d_frag_spv);
    lit2dPipeline = createTexturedStylePipeline(vert, frag, renderpass, lit2dPipelineLayout);
}

void Graphics::createMesh3DPipeline() {
    if (mesh3dPipeline) return;

    // Right-handed view, Y-up world. Projection uses perspectiveVulkanRH_ZO (Y flip for
    // Vulkan NDC), so frontFace is Clockwise while mesh winding stays CCW in object space.
    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    mesh3dSetLayoutUnique =
        layoutBuilder
            .buffer(0, vk::DescriptorType::eUniformBufferDynamic,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(4, vk::DescriptorType::eUniformBufferDynamic, vk::ShaderStageFlagBits::eFragment, 1)
            .image(5, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(6, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(7, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(8, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(9, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(10, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    mesh3dSetLayout = *mesh3dSetLayoutUnique;

    vkb::DescriptorSetLayoutBuilder skinLayoutBuilder;
    skinPassSetLayoutUnique =
        skinLayoutBuilder
            .buffer(0, vk::DescriptorType::eUniformBufferDynamic,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler,
                   vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    skinPassSetLayout = *skinPassSetLayoutUnique;
    skinPassPipelineLayout = createPipelineLayout(device, skinPassSetLayout);

    mesh3dPipelineLayout = createPipelineLayout(device, mesh3dSetLayout);
    const auto pcr =
        pushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                          Shader::kPushConstantBytes);
    mesh3dShaderPipelineLayout = createPipelineLayout(device, mesh3dSetLayout, &pcr);

    // Per-draw UBOs live in a per-frame-slot ring; descriptor sets are cached
    // per texture combination (see mesh3dSetFor / ensureMesh3dRing).
    mesh3dFrameSlots.clear();

    auto vert = embeddedSpirv(mesh3d_vert_spv);
    auto frag = embeddedSpirv(mesh3d_frag_spv);
    mesh3dPipeline =
        createMesh3DStylePipeline(vert, frag, mesh3dPipelineLayout, renderpass,
                                  vk::SampleCountFlagBits::e1);
    mesh3dTransparentPipeline =
        createMesh3DHairPipeline(vert, frag, mesh3dPipelineLayout, renderpass,
                                 vk::SampleCountFlagBits::e1);
}

void Graphics::createMesh3DClusteredPipeline() {
    if (mesh3dClusteredPipeline) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    mesh3dClusteredSetLayoutUnique =
        layoutBuilder
            .buffer(0, vk::DescriptorType::eUniformBufferDynamic,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(4, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(5, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(6, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(7, vk::DescriptorType::eUniformBufferDynamic, vk::ShaderStageFlagBits::eFragment, 1)
            .image(8, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(9, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(10, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(11, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(12, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    mesh3dClusteredSetLayout = *mesh3dClusteredSetLayoutUnique;

    mesh3dClusteredPipelineLayout = createPipelineLayout(device, mesh3dClusteredSetLayout);

    mesh3dClusteredFrameSlots.clear();

    auto vert = embeddedSpirv(mesh3d_clustered_vert_spv);
    auto frag = embeddedSpirv(mesh3d_clustered_frag_spv);
    mesh3dClusteredPipeline =
        createMesh3DStylePipeline(vert, frag, mesh3dClusteredPipelineLayout, renderpass,
                                  vk::SampleCountFlagBits::e1);
}

void Graphics::destroyShadowResources() {
    destroyPipeline(device, shadowPipeline);
    destroyPipelineLayout(device, shadowPipelineLayout);
    destroyPipeline(device, shadowAlphaPipeline);
    destroyPipeline(device, shadowSkinPipeline);
    destroyPipeline(device, shadowSkinAlphaPipeline);
    destroyPipelineLayout(device, shadowAlphaPipelineLayout);
    for (auto &slot : shadowMaps) {
        for (int i = 0; i < ShadowConfig::kCascades; ++i) {
            if (slot.framebuffers[i]) {
                device->destroyFramebuffer(slot.framebuffers[i]);
                slot.framebuffers[i] = vk::Framebuffer{};
            }
        }
    }
    shadowMaps.clear();
    if (shadowSampler) {
        device->destroySampler(shadowSampler);
        shadowSampler = {};
    }
    if (shadowRenderPass) {
        device->destroyRenderPass(shadowRenderPass);
        shadowRenderPass = {};
    }
    shadowPassCascade = -1;
    shadowPassDraws.clear();
    shadowPendingMask = 0;
    for (auto &d : shadowCascadeDraws) d.clear();
}

void Graphics::destroyGBufferResources() {
    gbufferPassActive = false;
    gbufferPending = false;
    gbufferPassDraws.clear();
    for (auto &slot : gbufferSlots) {
        slot.normalTex.gpuHandle = nullptr;
        slot.depthColorTex.gpuHandle = nullptr;
        slot.albedoTex.gpuHandle = nullptr;
        slot.visIDTex.gpuHandle = nullptr;
        slot.visBaryTex.gpuHandle = nullptr;
        slot.depthTex.gpuHandle = nullptr;
        if (slot.framebuffer) {
            device->destroyFramebuffer(slot.framebuffer);
            slot.framebuffer = vk::Framebuffer{};
        }
        if (slot.visFramebuffer) {
            device->destroyFramebuffer(slot.visFramebuffer);
            slot.visFramebuffer = vk::Framebuffer{};
        }
        destroySampler(device, slot.normalGpu.sampler);
        destroySampler(device, slot.depthColorGpu.sampler);
        destroySampler(device, slot.albedoGpu.sampler);
        destroySampler(device, slot.visIDGpu.sampler);
        destroySampler(device, slot.visBaryGpu.sampler);
        destroySampler(device, slot.depthGpu.sampler);
    }
    gbufferSlots.clear();
    post2Sets.clear();
    destroyPipeline(device, gbufferPipeline);
    destroyPipeline(device, gbufferAlphaPipeline);
    destroyPipeline(device, gbufferSkinPipeline);
    destroyPipeline(device, gbufferSkinAlphaPipeline);
    destroyPipeline(device, gbufferVisPipeline);
    destroyPipeline(device, gbufferVgVisPipeline);
    destroyPipelineLayout(device, gbufferPipelineLayout);
    if (gbufferRenderPass) {
        device->destroyRenderPass(gbufferRenderPass);
        gbufferRenderPass = {};
    }
    if (gbufferVisRenderPass) {
        device->destroyRenderPass(gbufferVisRenderPass);
        gbufferVisRenderPass = {};
    }
    gbufferWidth = 0;
    gbufferHeight = 0;
    if (renderControl_) renderControl_->getGBuffer()->clear();
}

void Graphics::destroySceneColorResources() {
    sceneColorPassOpen = false;
    if (device.instance) device->waitIdle();
    for (auto &slot : sceneColorSlots) {
        slot.colorTex.gpuHandle = nullptr;
        if (slot.framebuffer) {
            device->destroyFramebuffer(slot.framebuffer);
            slot.framebuffer = vk::Framebuffer{};
        }
        destroySampler(device, slot.colorGpu.sampler);
    }
    sceneColorSlots.clear();
    post2Sets.clear();
    if (sceneColorRenderPass) {
        device->destroyRenderPass(sceneColorRenderPass);
        sceneColorRenderPass = {};
    }
    sceneColorWidth = 0;
    sceneColorHeight = 0;
    sceneColorFormat = vk::Format::eUndefined;
    sceneColorSamples = vk::SampleCountFlagBits::e1;
}

void Graphics::createUiColorResources(int uiW, int uiH) {
    if (uiW <= 0 || uiH <= 0) return;
    const vk::Format colorFmt = swapchain.image_format;
    if (colorFmt == vk::Format::eUndefined) return;

    const vk::SampleCountFlags supported =
        device.physical_device.properties.limits.framebufferColorSampleCounts;
    const vk::SampleCountFlagBits samples =
        (supported & vk::SampleCountFlagBits::e4) ? vk::SampleCountFlagBits::e4
                                                  : vk::SampleCountFlagBits::e1;

    // The render pass is size-independent and must remain stable for ImGui's
    // lifetime: ImGui_ImplVulkan_Init builds its pipeline against it. Create it
    // once, before any early-returns below.
    if (!uiRenderPass) {
        if (samples != vk::SampleCountFlagBits::e1) {
            uiRenderPass =
                device.createRenderPass()
                    .addColorAttachment(colorFmt, vk::AttachmentLoadOp::eClear,
                                        vk::AttachmentStoreOp::eDontCare, samples)
                    .addResolveColorAttachment(colorFmt, vk::AttachmentLoadOp::eDontCare)
                    .addSubpass(vkb::SubpassBuilder()
                                    .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                    .addResolveAttachment(1, vk::ImageLayout::eColorAttachmentOptimal))
                    .addExternalShaderReadDependencies()
                    .build();
        } else {
            uiRenderPass =
                device.createRenderPass()
                    .addSampledColorAttachment(colorFmt)
                    .addSubpass(vkb::SubpassBuilder()
                                    .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal))
                    .addExternalShaderReadDependencies()
                    .build();
        }
    }
    // Keep sample count in sync with the render pass even if target creation
    // returns early (ImGui builds its pipeline from getUiMsaaSamples()).
    uiColorSamples = samples;

    if (!texSetLayout || !descriptorPool) return;
    if (!uiTexturePipeline) {
        const auto vert = embeddedSpirv(textured_vert_spv);
        const auto frag = embeddedSpirv(textured_frag_spv);
        uiTexturePipeline = createTexturedStylePipeline(vert, frag, uiRenderPass,
                                                        texPipelineLayout, BlendMode::Alpha,
                                                        uiColorSamples);
        uiTextureOpaquePipeline = createTexturedStylePipeline(
            vert, frag, uiRenderPass, texPipelineLayout, BlendMode::Opaque, uiColorSamples);
    }

    if (!uiColorSlots.empty() && uiColorWidth == uiW && uiColorHeight == uiH &&
        uiColorFormat == colorFmt && uiColorSamples == samples)
        return;
    destroyUiColorTargets();

    uiColorWidth = uiW;
    uiColorHeight = uiH;
    uiColorFormat = colorFmt;
    uiColorSamples = samples;
    const uint32_t w = uint32_t(uiW);
    const uint32_t h = uint32_t(uiH);
    const bool msaa = samples != vk::SampleCountFlagBits::e1;

    uiColorSlots.resize(kAsyncResourceCopies);
    for (auto &slot : uiColorSlots) {
        slot.color = device.createColorTarget(w, h, colorFmt);
        if (msaa) slot.msaaColor = device.createColorTarget(w, h, colorFmt, samples);
    }
    auto uiPass = uiRenderPass;

    for (auto &slot : uiColorSlots) {
        if (msaa) {
            slot.framebuffer = uiPass.createFramebuffer(
                device, w, h, {slot.msaaColor.asAttachment(), slot.color.asAttachment()});
        } else {
            slot.framebuffer = uiPass.createFramebuffer(device, w, h, {slot.color.asAttachment()});
        }
    }

    auto makeSampleTex = [&](GpuTexture &gpu, Texture &tex, vk::ImageView view) {
        vkb::SamplerBuilder sb;
        gpu.sampler = sb.magFilter(vk::Filter::eLinear)
                          .minFilter(vk::Filter::eLinear)
                          .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                          .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                          .build(device);
        auto sets = vkb::DescriptorSetBuilder()
                        .layout(texSetLayout)
                        .build(device.instance, descriptorPool);
        gpu.descriptorSet = vkb::BoundSet{sets[0]};
        gpu.width = width;
        gpu.height = height;
        gpu.viewOverride = view;
        writeCombinedImageDescriptor(&gpu);
        tex.width = width;
        tex.height = height;
        tex.pixelWidth = width;
        tex.pixelHeight = height;
        tex.gpuHandle = &gpu;
    };
    for (auto &slot : uiColorSlots)
        makeSampleTex(slot.colorGpu, slot.colorTex, slot.color.imageView());
}

void Graphics::destroyUiColorTargets() {
    if (device.instance) device->waitIdle();
    for (auto &slot : uiColorSlots) {
        slot.colorTex.gpuHandle = nullptr;
        if (slot.framebuffer) {
            device->destroyFramebuffer(slot.framebuffer);
            slot.framebuffer = vk::Framebuffer{};
        }
        destroySampler(device, slot.colorGpu.sampler);
    }
    uiColorSlots.clear();
    uiColorWidth = 0;
    uiColorHeight = 0;
    uiColorFormat = vk::Format::eUndefined;
    // Keep uiColorSamples matching uiRenderPass; ImGui's pipeline is bound to it.
}

void Graphics::destroyUiColorResources() {
    destroyUiColorTargets();
    if (uiTexturePipeline) {
        device->destroyPipeline(uiTexturePipeline);
        uiTexturePipeline = nullptr;
    }
    if (uiTextureOpaquePipeline) {
        device->destroyPipeline(uiTextureOpaquePipeline);
        uiTextureOpaquePipeline = nullptr;
    }
    if (uiRenderPass) {
        device->destroyRenderPass(uiRenderPass);
        uiRenderPass = {};
    }
}

void Graphics::queueUiResolve() {
    auto *slot = currentUiColorSlot();
    if (!slot || !slot->colorTex.gpuHandle) return;
    TexturedBatch resolve{&slot->colorTex, nullptr, nullptr, BlendMode::Alpha, Batcher{}};
    resolve.batch.addTexturedRect(0.f, 0.f, float(uiColorWidth), float(uiColorHeight),
                                  Color(1.f, 1.f, 1.f, 1.f), 0.f, 0.f, 1.f, 1.f);
    pendingUiResolve = std::move(resolve);
}

bool Graphics::renderUiOverlayPass() {
    if (!presentOverlayFn_) return false;
    createUiColorResources(int(swapchain.extent.width), int(swapchain.extent.height));
    auto *slot = currentUiColorSlot();
    if (!slot || !uiRenderPass || !slot->framebuffer) return false;

    auto &cb = currentPresentCb();
    const bool msaa = uiColorSamples != vk::SampleCountFlagBits::e1;
    std::array<vk::ClearValue, 2> clears{};
    clears[0].color = vk::ClearColorValue(std::array<float, 4>{0.f, 0.f, 0.f, 0.f});
    clears[1].color = vk::ClearColorValue(std::array<float, 4>{0.f, 0.f, 0.f, 0.f});
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = uiRenderPass;
    rpBegin.framebuffer = slot->framebuffer;
    rpBegin.renderArea = vk::Rect2D{{0, 0}, {uint32_t(uiColorWidth), uint32_t(uiColorHeight)}};
    rpBegin.clearValueCount = msaa ? 2 : 1;
    rpBegin.pClearValues = clears.data();

    slot->color.beginColorAttachment();
    if (msaa) slot->msaaColor.beginColorAttachment();
    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

    VkCommandBuffer raw = static_cast<VkCommandBuffer>(cb);
    presentOverlayFn_(presentOverlayUser_, raw);

    cb.endRenderPass();
    slot->color.endSampledLayout();
    queueUiResolve();
    return true;
}


void Graphics::createGBufferResources(int gbufW, int gbufH) {
    if (gbufW <= 0 || gbufH <= 0) return;
    if (!gbufferSlots.empty() && gbufferWidth == gbufW && gbufferHeight == gbufH && gbufferPipeline)
        return;
    destroyGBufferResources();

    gbufferWidth = gbufW;
    gbufferHeight = gbufH;
    const uint32_t w = uint32_t(gbufW);
    const uint32_t h = uint32_t(gbufH);
    const vk::Format colorFmt = pickGBufferColorFormat(device);
    const vk::Format depthFmt = vk::Format::eD32Sfloat;
    const vk::Format visIDFmt = vk::Format::eR32G32Uint;
    const vk::Format visBaryFmt = vk::Format::eR16G16Sfloat;

    gbufferSlots.resize(kAsyncResourceCopies);
    for (auto &slot : gbufferSlots) {
        slot.normal = device.createColorTarget(w, h, colorFmt);
        slot.depthColor = device.createColorTarget(w, h, colorFmt);
        slot.albedo = device.createColorTarget(w, h, colorFmt);
        slot.visID = device.createColorTarget(w, h, visIDFmt);
        slot.visBary = device.createColorTarget(w, h, visBaryFmt);
        slot.depth = device.createDepthTarget(w, h, depthFmt, true);
    }

    auto gbufferPass =
        device.createRenderPass()
            .addSampledColorAttachment(colorFmt)
            .addSampledColorAttachment(colorFmt)
            .addSampledColorAttachment(colorFmt)
            .addSampledDepthAttachment(depthFmt)
            .addSubpass(vkb::SubpassBuilder()
                            .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(1, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(2, vk::ImageLayout::eColorAttachmentOptimal)
                            .setDepthStencilAttachment(
                                3, vk::ImageLayout::eDepthStencilAttachmentOptimal))
            .addExternalShaderReadDependencies()
            .build();
    gbufferRenderPass = gbufferPass;

    for (auto &slot : gbufferSlots) {
        slot.framebuffer = gbufferPass.createFramebuffer(
            device, w, h,
            {slot.normal.asAttachment(), slot.depthColor.asAttachment(), slot.albedo.asAttachment(),
             slot.depth.asAttachment()});
    }

    auto layoutBuilder = device.createPipelineLayout();
    if (texSetLayout) layoutBuilder.set(texSetLayout);
    gbufferPipelineLayout =
        layoutBuilder
            .push<GBufferPush>(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
            .build();

    std::vector<uint32_t> vert(mesh3d_gbuffer_vert_spv,
                               mesh3d_gbuffer_vert_spv + mesh3d_gbuffer_vert_spv_count);
    std::vector<uint32_t> frag(mesh3d_gbuffer_frag_spv,
                               mesh3d_gbuffer_frag_spv + mesh3d_gbuffer_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    gbufferPipeline = device.createPipeline()
                          .useClassicPipeline(vertModule, fragModule)
                          .setPipelineLayout(gbufferPipelineLayout)
                          .setVertexInputState(vkb::VertexInputStateBuilder()
                                                   .addInputBinding<MeshVertex>()
                                                   .addAttributeDescription<MeshVertex>())
                          .setDynamicStatesViewportScissor()
                          .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                         vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise)
                          .build(gbufferPass);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);

    // Alpha-cutout variant for billboard/card geometry (sprite-stack slices):
    // same layout/push constants, fragment discards transparent texels.
    vk::ShaderModule alphaVertModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    std::vector<uint32_t> alphaFrag(mesh3d_gbuffer_alpha_frag_spv,
                                    mesh3d_gbuffer_alpha_frag_spv +
                                        mesh3d_gbuffer_alpha_frag_spv_count);
    vk::ShaderModule alphaFragModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, alphaFrag);
    gbufferAlphaPipeline = device.createPipeline()
                               .useClassicPipeline(alphaVertModule, alphaFragModule)
                               .setPipelineLayout(gbufferPipelineLayout)
                               .setVertexInputState(vkb::VertexInputStateBuilder()
                                                        .addInputBinding<MeshVertex>()
                                                        .addAttributeDescription<MeshVertex>())
                               .setDynamicStatesViewportScissor()
                               .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                              vk::CullModeFlagBits::eNone,
                                              vk::FrontFace::eClockwise)
                               .build(gbufferPass);
    device->destroyShaderModule(alphaVertModule);
    device->destroyShaderModule(alphaFragModule);

    auto skinVert = embeddedSpirv(mesh3d_gbuffer_skin_vert_spv);
    auto skinFrag = embeddedSpirv(mesh3d_gbuffer_skin_frag_spv);
    gbufferSkinPipeline = device.createPipeline()
                              .useClassicPipeline(skinVert, skinFrag)
                              .setPipelineLayout(skinPassPipelineLayout)
                              .setVertexInputState(vkb::VertexInputStateBuilder()
                                                       .addInputBinding<MeshVertex>()
                                                       .addAttributeDescription<MeshVertex>())
                              .setDynamicStatesViewportScissor()
                              .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                             vk::CullModeFlagBits::eNone,
                                             vk::FrontFace::eClockwise)
                              .build(gbufferPass);
    auto skinAlphaFrag = embeddedSpirv(mesh3d_gbuffer_skin_alpha_frag_spv);
    gbufferSkinAlphaPipeline = device.createPipeline()
                                   .useClassicPipeline(skinVert, skinAlphaFrag)
                                   .setPipelineLayout(skinPassPipelineLayout)
                                   .setVertexInputState(vkb::VertexInputStateBuilder()
                                                            .addInputBinding<MeshVertex>()
                                                            .addAttributeDescription<MeshVertex>())
                                   .setDynamicStatesViewportScissor()
                                   .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                                  vk::CullModeFlagBits::eNone,
                                                  vk::FrontFace::eClockwise)
                                   .build(gbufferPass);

    auto makeSampleTex = [&](GpuTexture &gpu, Texture &tex, vk::ImageView view) {
        vkb::SamplerBuilder sb;
        gpu.sampler = sb.nearestClamp().build(device);
        auto sets = vkb::DescriptorSetBuilder()
                        .layout(texSetLayout)
                        .build(device.instance, descriptorPool);
        gpu.descriptorSet = vkb::BoundSet{sets[0]};
        gpu.width = width;
        gpu.height = height;
        gpu.viewOverride = view;
        writeCombinedImageDescriptor(&gpu);
        tex.width = width;
        tex.height = height;
        tex.pixelWidth = width;
        tex.pixelHeight = height;
        tex.gpuHandle = &gpu;
    };
    for (auto &slot : gbufferSlots) {
        makeSampleTex(slot.normalGpu, slot.normalTex, slot.normal.imageView());
        makeSampleTex(slot.depthColorGpu, slot.depthColorTex, slot.depthColor.imageView());
        makeSampleTex(slot.albedoGpu, slot.albedoTex, slot.albedo.imageView());
        makeSampleTex(slot.visIDGpu, slot.visIDTex, slot.visID.imageView());
        makeSampleTex(slot.visBaryGpu, slot.visBaryTex, slot.visBary.imageView());
        makeSampleTex(slot.depthGpu, slot.depthTex, slot.depth.imageView());
    }
    createGpuDrivenVisResources(width, height);
}

void Graphics::ensureDecalUnitBox() {
    if (decalUnitBox) return;
    // Unit cube [-0.5, 0.5]^3, one 4-vertex face per side with its own
    // normal + UVs so the decal projection samples a clean square per face.
    const float h = 0.5f;
    const std::vector<glm::vec3> pos = {
        // +Z
        {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h},
        // -Z
        {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h},
        // +X
        {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h},
        // -X
        {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h},
        // +Y
        {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h},
        // -Y
        {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h},
    };
    const std::vector<glm::vec3> nrm = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
        {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
        {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0},
        {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
        {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0},
    };
    const std::vector<glm::vec2> uv = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
        {0, 0}, {1, 0}, {1, 1}, {0, 1},
    };
    const std::vector<uint32_t> idx = {
        0, 1, 2, 0, 2, 3,       4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,    12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };
    std::vector<float> posF, nrmF, uvF;
    posF.reserve(pos.size() * 3);
    nrmF.reserve(nrm.size() * 3);
    uvF.reserve(uv.size() * 2);
    for (const auto &p : pos) posF.insert(posF.end(), {p.x, p.y, p.z});
    for (const auto &n : nrm) nrmF.insert(nrmF.end(), {n.x, n.y, n.z});
    for (const auto &t : uv) uvF.insert(uvF.end(), {t.x, t.y});
    decalUnitBox =
        newMeshFromArrays(posF.data(), nrmF.data(), uvF.data(), int(pos.size()), idx.data(),
                          int(idx.size()));
}

void Graphics::createDecalResources(int decalW, int decalH) {
    if (decalW <= 0 || decalH <= 0) return;
    if (!decalSlots.empty() && decalWidth == decalW && decalHeight == decalH && decalPipeline)
        return;
    destroyDecalResources();

    decalWidth = decalW;
    decalHeight = decalH;
    const uint32_t w = uint32_t(decalW);
    const uint32_t h = uint32_t(decalH);
    const vk::Format colorFmt = pickGBufferColorFormat(device);  // RGBA8

    decalSlots.resize(kAsyncResourceCopies);
    for (auto &slot : decalSlots) {
        slot.albedo = device.createColorTarget(w, h, colorFmt);
        slot.normal = device.createColorTarget(w, h, colorFmt);
        slot.params = device.createColorTarget(w, h, colorFmt);
        slot.cameraUbo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer,
                                sizeof(DecalCameraUBO), kHostVisibleCoherent);
        slot.instanceBuf.allocate(
            frameToken(), device, vk::BufferUsageFlagBits::eStorageBuffer,
            vk::DeviceSize(kMaxDecalInstances) * sizeof(DecalInstanceData), kHostVisibleCoherent);
    }

    auto decalPass =
        device.createRenderPass()
            .addSampledColorAttachment(colorFmt)
            .addSampledColorAttachment(colorFmt)
            .addSampledColorAttachment(colorFmt)
            .addSubpass(vkb::SubpassBuilder()
                            .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(1, vk::ImageLayout::eColorAttachmentOptimal)
                            .addAttachmentRef(2, vk::ImageLayout::eColorAttachmentOptimal))
            .addExternalShaderReadDependencies()
            .build();
    decalRenderPass = decalPass;

    for (auto &slot : decalSlots) {
        slot.framebuffer = decalPass.createFramebuffer(
            device, w, h,
            {slot.albedo.asAttachment(), slot.normal.asAttachment(), slot.params.asAttachment()});
    }

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    decalSetLayoutUnique =
        layoutBuilder
            .image(0, vk::DescriptorType::eCombinedImageSampler,
                   vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler,
                   vk::ShaderStageFlagBits::eFragment, 1)
            .image(2, vk::DescriptorType::eCombinedImageSampler,
                   vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler,
                   vk::ShaderStageFlagBits::eFragment, 1)
            .image(4, vk::DescriptorType::eCombinedImageSampler,
                   vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(5, vk::DescriptorType::eUniformBufferDynamic,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(6, vk::DescriptorType::eStorageBuffer,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    decalSetLayout = *decalSetLayoutUnique;
    decalPipelineLayout = createPipelineLayout(device, decalSetLayout);

    std::vector<uint32_t> vert(decal_box_vert_spv, decal_box_vert_spv + decal_box_vert_spv_count);
    std::vector<uint32_t> frag(decal_box_frag_spv, decal_box_frag_spv + decal_box_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);

    // Alpha-over compositing for the three layer targets (non-premultiplied
    // decal output; the shader writes rgb + coverage in alpha).
    decalPipeline = device.createPipeline()
                        .useClassicPipeline(vertModule, fragModule)
                        .setPipelineLayout(decalPipelineLayout)
                        .setVertexInputState(vkb::VertexInputStateBuilder()
                                                 .addInputBinding<MeshVertex>()
                                                 .addAttributeDescription<MeshVertex>())
                        .setDynamicStatesViewportScissor()
                        .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                       vk::CullModeFlagBits::eNone, vk::FrontFace::eClockwise)
                        .setMultisampler(false, vk::SampleCountFlagBits::e1)
                        .setDepthStencil(false, false, vk::CompareOp::eAlways)
                        .setAlphaBlending(3)
                        .build(decalPass);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);

    auto makeSampleTex = [&](GpuTexture &gpu, Texture &tex, vk::ImageView view) {
        vkb::SamplerBuilder sb;
        gpu.sampler = sb.nearestClamp().build(device);
        auto sets = vkb::DescriptorSetBuilder()
                        .layout(texSetLayout)
                        .build(device.instance, descriptorPool);
        gpu.descriptorSet = vkb::BoundSet{sets[0]};
        gpu.width = decalW;
        gpu.height = decalH;
        gpu.viewOverride = view;
        writeCombinedImageDescriptor(&gpu);
        tex.width = decalW;
        tex.height = decalH;
        tex.pixelWidth = decalW;
        tex.pixelHeight = decalH;
        tex.gpuHandle = &gpu;
    };
    for (auto &slot : decalSlots) {
        makeSampleTex(slot.albedoGpu, slot.albedoTex, slot.albedo.imageView());
        makeSampleTex(slot.normalGpu, slot.normalTex, slot.normal.imageView());
        makeSampleTex(slot.paramsGpu, slot.paramsTex, slot.params.imageView());
    }
}

void Graphics::destroyDecalResources() {
    decalPassActive = false;
    decalPending = false;
    decalPassDraws.clear();
    for (auto &slot : decalSlots) {
        slot.albedoTex.gpuHandle = nullptr;
        slot.normalTex.gpuHandle = nullptr;
        slot.paramsTex.gpuHandle = nullptr;
        if (slot.framebuffer) {
            device->destroyFramebuffer(slot.framebuffer);
            slot.framebuffer = vk::Framebuffer{};
        }
        destroySampler(device, slot.albedoGpu.sampler);
        destroySampler(device, slot.normalGpu.sampler);
        destroySampler(device, slot.paramsGpu.sampler);
        slot.cameraUbo.release();
        slot.instanceBuf.release();
        slot.sets.clear();
    }
    decalSlots.clear();
    destroyPipeline(device, decalPipeline);
    destroyPipelineLayout(device, decalPipelineLayout);
    if (decalSetLayoutUnique) decalSetLayoutUnique.reset();
    if (decalRenderPass) {
        device->destroyRenderPass(decalRenderPass);
        decalRenderPass = {};
    }
    decalWidth = 0;
    decalHeight = 0;
}

vkb::BoundSet Graphics::decalSetFor(DecalSlot &slot, GpuTexture *albedo, GpuTexture *normal,
                                    GpuTexture *params, GpuTexture *depth, GpuTexture *gbNormal) {
    ASSERT(albedo != nullptr);
    ASSERT(normal != nullptr);
    ASSERT(params != nullptr);
    ASSERT(depth != nullptr);
    ASSERT(gbNormal != nullptr);
    DecalSetKey key{albedo, normal, params, depth, gbNormal};
    auto it = slot.sets.find(key);
    if (it != slot.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &decalSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    vkb::DescriptorSetUpdater updater(8, 8, 0);
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(albedo->sampler, albedo->imageView()))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(normal->sampler, normal->imageView()))
        .beginImages(2, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(params->sampler, params->imageView()))
        .beginImages(3, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(depth->sampler, depth->imageView()))
        .beginImages(4, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gbNormal->sampler, gbNormal->imageView()))
        .beginBuffers(5, 0, vk::DescriptorType::eUniformBufferDynamic)
        .buffer(slot.cameraUbo.buffer, 0, slot.cameraUbo.size)
        .beginBuffers(6, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(slot.instanceBuf.buffer, 0, slot.instanceBuf.size)
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    slot.sets.emplace(key, bound);
    return bound;
}

void Graphics::createSceneColorResources(int sceneW, int sceneH) {
    if (sceneW <= 0 || sceneH <= 0) return;
    if (!texSetLayout || !descriptorPool) return;
    const vk::Format colorFmt = swapchain.image_format;
    if (colorFmt == vk::Format::eUndefined) return;

    const bool featureMsaa = !renderControl_ || renderControl_->isEnabled("msaa");
    const int desired = featureMsaa ? msaaSamples : 0;
    if (desired != appliedMsaa) appliedMsaa = desired;
    const vk::SampleCountFlagBits samples = sampleCountFlagFor(clampMsaaSamples(appliedMsaa));

    if (!sceneColorSlots.empty() && sceneColorWidth == sceneW && sceneColorHeight == sceneH &&
        sceneColorFormat == colorFmt && sceneColorSamples == samples && sceneColorRenderPass)
        return;
    destroySceneColorResources();

    sceneColorWidth = sceneW;
    sceneColorHeight = sceneH;
    sceneColorFormat = colorFmt;
    sceneColorSamples = samples;
    const uint32_t w = uint32_t(sceneW);
    const uint32_t h = uint32_t(sceneH);
    const vk::Format depthFmt = depthFormat;
    const bool msaa = samples != vk::SampleCountFlagBits::e1;

    sceneColorSlots.resize(kAsyncResourceCopies);
    for (auto &slot : sceneColorSlots) {
        slot.color = device.createColorTarget(w, h, colorFmt);
        if (msaa) {
            slot.msaaColor = device.createColorTarget(w, h, colorFmt, samples);
            slot.depth = device.createDepthTarget(w, h, depthFmt, false, samples);
        } else {
            slot.depth = device.createDepthTarget(w, h, depthFmt, false);
        }
    }

    if (msaa) {
        // MSAA color is transient (DONT_CARE store). Sampling the 1x resolve.
        // addSampledColorAttachment on the MSAA target makes MoltenVK treat it
        // as a shader-readable texture2d and segfaults on macOS.
        sceneColorRenderPass =
            device.createRenderPass()
                .addColorAttachment(colorFmt, vk::AttachmentLoadOp::eClear,
                                    vk::AttachmentStoreOp::eDontCare, samples)
                .addResolveColorAttachment(colorFmt, vk::AttachmentLoadOp::eDontCare)
                .addDepthAttachment(depthFmt, vk::AttachmentLoadOp::eClear,
                                    vk::AttachmentStoreOp::eDontCare, samples)
                .addSubpass(vkb::SubpassBuilder()
                                .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                .addResolveAttachment(1, vk::ImageLayout::eColorAttachmentOptimal)
                                .setDepthStencilAttachment(
                                    2, vk::ImageLayout::eDepthStencilAttachmentOptimal))
                .addExternalShaderReadDependencies()
                .build();
    } else {
        sceneColorRenderPass =
            device.createRenderPass()
                .addSampledColorAttachment(colorFmt)
                .addDepthAttachment(depthFmt, vk::AttachmentLoadOp::eClear,
                                    vk::AttachmentStoreOp::eDontCare)
                .addSubpass(vkb::SubpassBuilder()
                                .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                .setDepthStencilAttachment(
                                    1, vk::ImageLayout::eDepthStencilAttachmentOptimal))
                .addExternalShaderReadDependencies()
                .build();
    }
    auto scenePass = sceneColorRenderPass;

    for (auto &slot : sceneColorSlots) {
        if (msaa) {
            slot.framebuffer = scenePass.createFramebuffer(
                device, w, h,
                {slot.msaaColor.asAttachment(), slot.color.asAttachment(), slot.depth.asAttachment()});
        } else {
            slot.framebuffer = scenePass.createFramebuffer(
                device, w, h, {slot.color.asAttachment(), slot.depth.asAttachment()});
        }
    }

    auto makeSampleTex = [&](GpuTexture &gpu, Texture &tex, vk::ImageView view) {
        vkb::SamplerBuilder sb;
        gpu.sampler = sb.magFilter(vk::Filter::eLinear)
                          .minFilter(vk::Filter::eLinear)
                          .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                          .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                          .build(device);
        auto sets = vkb::DescriptorSetBuilder()
                        .layout(texSetLayout)
                        .build(device.instance, descriptorPool);
        gpu.descriptorSet = vkb::BoundSet{sets[0]};
        gpu.width = width;
        gpu.height = height;
        gpu.viewOverride = view;
        writeCombinedImageDescriptor(&gpu);
        tex.width = width;
        tex.height = height;
        tex.pixelWidth = width;
        tex.pixelHeight = height;
        tex.gpuHandle = &gpu;
    };
    for (auto &slot : sceneColorSlots) makeSampleTex(slot.colorGpu, slot.colorTex, slot.color.imageView());
}

bool Graphics::beginSceneColorRenderPass() {
    auto *slot = currentSceneColorSlot();
    if (!slot || !sceneColorRenderPass || !slot->framebuffer) return false;
    auto &cb = currentPresentCb();
    const bool msaa = sceneColorSamples != vk::SampleCountFlagBits::e1;
    // A=1 marks sky / far plane so SSGI skips uncleared pixels.
    if (msaa) {
        std::array<vk::ClearValue, 3> clears{};
        clears[0].color = vk::ClearColorValue(
            std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, 1.f});
        clears[1].color = vk::ClearColorValue(
            std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, 1.f});
        clears[2].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = sceneColorRenderPass;
        rpBegin.framebuffer = slot->framebuffer;
        rpBegin.renderArea =
            vk::Rect2D{{0, 0}, {uint32_t(sceneColorWidth), uint32_t(sceneColorHeight)}};
        rpBegin.clearValueCount = uint32_t(clears.size());
        rpBegin.pClearValues = clears.data();
        slot->msaaColor.beginColorAttachment();
        slot->color.beginColorAttachment();
        slot->depth.beginDepthAttachment();
        cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    } else {
        std::array<vk::ClearValue, 2> clears{};
        clears[0].color = vk::ClearColorValue(
            std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, 1.f});
        clears[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = sceneColorRenderPass;
        rpBegin.framebuffer = slot->framebuffer;
        rpBegin.renderArea =
            vk::Rect2D{{0, 0}, {uint32_t(sceneColorWidth), uint32_t(sceneColorHeight)}};
        rpBegin.clearValueCount = uint32_t(clears.size());
        rpBegin.pClearValues = clears.data();
        slot->color.beginColorAttachment();
        slot->depth.beginDepthAttachment();
        cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    }
    sceneColorPassOpen = true;
    return true;
}

void Graphics::endSceneColorRenderPass() {
    if (!sceneColorPassOpen) return;
    auto &cb = currentPresentCb();
    cb.endRenderPass();
    sceneColorPassOpen = false;
    if (auto *slot = currentSceneColorSlot())
        slot->color.endSampledLayout();
}

int Graphics::clampMsaaSamples(int requested) const {
    if (requested <= 1) return 0;
    vk::SampleCountFlags supported =
        device.physical_device.properties.limits.framebufferColorSampleCounts &
        device.physical_device.properties.limits.framebufferDepthSampleCounts;
    const int candidates[3] = {8, 4, 2};
    for (int c : candidates) {
        if (requested >= c && (supported & sampleCountFlagFor(c))) return c;
    }
    for (int c : candidates) {
        if (supported & sampleCountFlagFor(c)) return c;
    }
    return 0;
}

void Graphics::ensureScenePassPipelines(const vkb::BuiltRenderPass &target,
                                        vk::SampleCountFlagBits samples) {
    const vk::RenderPass targetHandle = vk::RenderPass(target);
    if (targetHandle == scenePassPipelineTarget && samples == scenePassPipelineSamples) return;
    device->waitIdle();

    destroyPipeline(device, mesh3dPipeline);
    destroyPipeline(device, mesh3dTransparentPipeline);
    destroyPipeline(device, mesh3dClusteredPipeline);
    destroyPipeline(device, mesh3dGpuDrivenPipeline);
    destroyPipeline(device, resolveVisPipeline);
    destroyPipeline(device, voxelRectPipeline);

    mesh3dPipeline = createMesh3DStylePipeline(embeddedSpirv(mesh3d_vert_spv),
                                               embeddedSpirv(mesh3d_frag_spv),
                                               mesh3dPipelineLayout, target, samples);
    mesh3dTransparentPipeline =
        createMesh3DHairPipeline(embeddedSpirv(mesh3d_vert_spv),
                                 embeddedSpirv(mesh3d_frag_spv), mesh3dPipelineLayout,
                                 target, samples);
    if (mesh3dGpuDrivenPipelineLayout) {
        mesh3dGpuDrivenPipeline =
            createMesh3DStylePipeline(embeddedSpirv(mesh3d_gpudriven_vert_spv),
                                      embeddedSpirv(mesh3d_gpudriven_frag_spv),
                                      mesh3dGpuDrivenPipelineLayout, target, samples);
        createResolveVisPipeline(target, samples);
    }
    mesh3dClusteredPipeline =
        createMesh3DStylePipeline(embeddedSpirv(mesh3d_clustered_vert_spv),
                                  embeddedSpirv(mesh3d_clustered_frag_spv),
                                  mesh3dClusteredPipelineLayout, target, samples);
    voxelRectPipeline = buildVoxelRectPipeline(target, samples);

    for (auto &g : ownedGpuShaders) {
        if (!g->isMesh3D) continue;
        destroyPipeline(device, g->mesh3dPipeline);
        destroyPipeline(device, g->mesh3dXrayPipeline);
        g->mesh3dXrayPipeline = nullptr;
        if (!g->owner) continue;
        if (g->isHair3D) {
            g->mesh3dPipeline =
                createMesh3DHairPipeline(g->owner->vertexSpirv(), g->owner->fragmentSpirv(),
                                         g->pipelineLayout, target, samples);
        } else {
            g->mesh3dPipeline =
                createMesh3DStylePipeline(g->owner->vertexSpirv(), g->owner->fragmentSpirv(),
                                          g->pipelineLayout, target, samples);
            g->mesh3dXrayPipeline =
                createMesh3DXrayPipeline(g->owner->vertexSpirv(), g->owner->fragmentSpirv(),
                                         g->pipelineLayout, target, samples);
        }
    }

    scenePassPipelineTarget = targetHandle;
    scenePassPipelineSamples = samples;
}

void Graphics::queueSceneColorResolve() {
    Texture *src = getSceneColorTexture();
    if (!src || !src->gpuHandle) return;
    Shader *sh = prepareSceneColorResolveShader(src);
    if (!sh) return;
    if (sceneColorComposited) return;
    TexturedBatch resolve{src, nullptr, sh, BlendMode::Alpha, Batcher{}};
    resolve.batch.addTexturedRect(0.f, 0.f, float(width), float(height), Color(1.f, 1.f, 1.f, 1.f),
                                  0.f, 0.f, 1.f, 1.f);
    pendingSceneResolve = std::move(resolve);
}

void Graphics::createShadowResources() {
    if (!shadowMaps.empty()) return;

    const uint32_t size = uint32_t(ShadowConfig::kMapSize);
    const uint32_t layers = uint32_t(ShadowConfig::kCascades);

    shadowMaps.resize(kAsyncResourceCopies);
    for (auto &slot : shadowMaps)
        slot.image = device.createDepthArray(size, size, layers, vk::Format::eD32Sfloat);

    vkb::SamplerBuilder sb;
    // Hardware PCF: linear filtering + depth compare on the D32 shadow map.
    // The compare result of each filtered depth sample is blended by the driver,
    // giving a soft penumbra instead of the hard 1-texel boundary.
    shadowSampler = sb.linearClamp().compareEnable(VK_TRUE).compareOp(vk::CompareOp::eLess).buildDepthPcf(device);

    auto shadowPass =
        device.createRenderPass()
            .addSampledDepthAttachment(vk::Format::eD32Sfloat)
            .addSubpass(vkb::SubpassBuilder().setDepthStencilAttachment(
                0, vk::ImageLayout::eDepthStencilAttachmentOptimal))
            .addExternalShaderReadDependencies()
            .build();
    shadowRenderPass = shadowPass;

    for (auto &slot : shadowMaps) {
        for (uint32_t i = 0; i < layers; ++i) {
            slot.framebuffers[i] = shadowPass.createFramebuffer(
                device, size, size, {slot.image.layerAttachment(i)});
        }
    }

    shadowPipelineLayout =
        device.createPipelineLayout()
            .push<glm::mat4>(vk::ShaderStageFlagBits::eVertex)
            .build();

    std::vector<uint32_t> vert(mesh3d_shadow_vert_spv,
                               mesh3d_shadow_vert_spv + mesh3d_shadow_vert_spv_count);
    std::vector<uint32_t> frag(mesh3d_shadow_frag_spv,
                               mesh3d_shadow_frag_spv + mesh3d_shadow_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    shadowPipeline =
        device.createPipeline()
            .useClassicPipeline(vertModule, fragModule)
            .setPipelineLayout(shadowPipelineLayout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<MeshVertex>()
                                     .addAttributeDescription<MeshVertex>())
            .setDynamicStatesViewportScissor()
            // No cull: Cornell-style one-sided interiors keep writing when the
            // ceiling/walls are back-facing the sun. Closest depth still wins
            // on closed meshes, so floors are not punched through.
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eClockwise)
            .setDepthBias(0.0f, 0.5f)
            .build(shadowPass);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);

    // Alpha-cutout variant for billboard/card shadow casters (sprite-stack
    // slices): same transform push constant, fragment samples the albedo and
    // discards transparent texels so shadows follow the silhouette.
    shadowAlphaPipelineLayout = device.createPipelineLayout()
                                    .set(texSetLayout)
                                    .push<glm::mat4>(vk::ShaderStageFlagBits::eVertex)
                                    .build();
    std::vector<uint32_t> alphaVert(mesh3d_shadow_alpha_vert_spv,
                                    mesh3d_shadow_alpha_vert_spv +
                                        mesh3d_shadow_alpha_vert_spv_count);
    std::vector<uint32_t> alphaFrag(mesh3d_shadow_alpha_frag_spv,
                                    mesh3d_shadow_alpha_frag_spv +
                                        mesh3d_shadow_alpha_frag_spv_count);
    vk::ShaderModule alphaVertModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, alphaVert);
    vk::ShaderModule alphaFragModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, alphaFrag);
    shadowAlphaPipeline =
        device.createPipeline()
            .useClassicPipeline(alphaVertModule, alphaFragModule)
            .setPipelineLayout(shadowAlphaPipelineLayout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<MeshVertex>()
                                     .addAttributeDescription<MeshVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eClockwise)
            .setDepthBias(0.0f, 0.5f)
            .build(shadowPass);
    device->destroyShaderModule(alphaVertModule);
    device->destroyShaderModule(alphaFragModule);

    auto skinVert = embeddedSpirv(mesh3d_shadow_skin_vert_spv);
    shadowSkinPipeline = device.createPipeline()
                             .useClassicPipeline(skinVert, frag)
                             .setPipelineLayout(skinPassPipelineLayout)
                             .setVertexInputState(vkb::VertexInputStateBuilder()
                                                      .addInputBinding<MeshVertex>()
                                                      .addAttributeDescription<MeshVertex>())
                             .setDynamicStatesViewportScissor()
                             .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                            vk::CullModeFlagBits::eNone,
                                            vk::FrontFace::eClockwise)
                             .setDepthBias(0.0f, 0.5f)
                             .build(shadowPass);
    auto skinAlphaFrag = embeddedSpirv(mesh3d_shadow_skin_alpha_frag_spv);
    shadowSkinAlphaPipeline = device.createPipeline()
                                  .useClassicPipeline(skinVert, skinAlphaFrag)
                                  .setPipelineLayout(skinPassPipelineLayout)
                                  .setVertexInputState(vkb::VertexInputStateBuilder()
                                                           .addInputBinding<MeshVertex>()
                                                           .addAttributeDescription<MeshVertex>())
                                  .setDynamicStatesViewportScissor()
                                  .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                                 vk::CullModeFlagBits::eNone,
                                                 vk::FrontFace::eClockwise)
                                  .setDepthBias(0.0f, 0.5f)
                                  .build(shadowPass);

    // Clear every ping-pong copy so sampling before the first real shadow pass
    // sees SHADER_READ_ONLY rather than UNDEFINED.
    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                vk::ClearValue clear{};
                                clear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
                                for (auto &slot : shadowMaps) {
                                    slot.image.beginDepthAttachment();
                                    for (int c = 0; c < ShadowConfig::kCascades; ++c) {
                                        vk::RenderPassBeginInfo rpBegin{};
                                        rpBegin.renderPass = shadowRenderPass;
                                        rpBegin.framebuffer = slot.framebuffers[c];
                                        rpBegin.renderArea = vk::Rect2D{{0, 0}, {size, size}};
                                        rpBegin.clearValueCount = 1;
                                        rpBegin.pClearValues = &clear;
                                        cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
                                        cb.endRenderPass();
                                    }
                                    slot.image.endSampledLayout();
                                }
                            });
}

void Graphics::ensureClusteredBuffers(size_t lightsBytes, size_t tableBytes, size_t indicesBytes) {
    auto &st = currentClusteredStorage();
    auto ensure = [&](vkb::GenericBuffer &buf, size_t &cap, size_t need) {
        if (need == 0) need = 4;
        if (cap >= need && buf.buffer) return;
        buf.release();
        // Grow with some slack.
        size_t alloc = std::max(need, cap ? cap * 2 : need);
        buf.allocate(frameToken(), device, vk::BufferUsageFlagBits::eStorageBuffer, vk::DeviceSize(alloc),
                     vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent);
        cap = alloc;
    };
    ensure(st.lightsBuf, st.lightsCap, lightsBytes);
    ensure(st.tableBuf, st.tableCap, tableBytes);
    ensure(st.indicesBuf, st.indicesCap, indicesBytes);
    // Reallocated handles invalidate this frame's cached descriptor sets; drop
    // them so mesh3dClusteredSetFor rebinds against the new buffers.
    currentMesh3dClusteredFrameSlots().sets.clear();
}

void Graphics::uploadClusteredLighting(const ClusteredLightingUpload &upload) {
    const size_t lightsBytes =
        std::max(size_t(1), upload.lights.size()) * sizeof(ClusteredLightGpu);
    const size_t tableBytes =
        std::max(size_t(1), upload.clusterTable.size()) * sizeof(ClusterTableEntry);
    const size_t indicesBytes =
        std::max(size_t(1), upload.lightIndices.size()) * sizeof(uint32_t);
    ensureClusteredBuffers(lightsBytes, tableBytes, indicesBytes);

    auto &st = currentClusteredStorage();
    if (!upload.lights.empty())
        st.lightsBuf.updateLocal(frameToken(), upload.lights.data(),
                                 upload.lights.size() * sizeof(ClusteredLightGpu));
    else {
        ClusteredLightGpu zero{};
        st.lightsBuf.updateLocal(frameToken(), &zero, sizeof(zero));
    }
    st.tableBuf.updateLocal(frameToken(), upload.clusterTable.data(),
                            upload.clusterTable.size() * sizeof(ClusterTableEntry));
    st.indicesBuf.updateLocal(frameToken(), upload.lightIndices.data(),
                              upload.lightIndices.size() * sizeof(uint32_t));
}

void Graphics::setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) {
    mesh3dClusteredActive = upload.active;
    mesh3dClustered = upload;
    if (upload.active) uploadClusteredLighting(upload);
}

void Graphics::setMesh3DClusteredActive(bool active) {
    mesh3dClusteredActive = active;
}

void Graphics::ensureMesh3dStrides() {
    if (mesh3dUboStride != 0) return;
    const uint32_t align = std::max(
        1u, uint32_t(device.physical_device.properties.limits.minUniformBufferOffsetAlignment));
    mesh3dUboStride = alignUpValue(uint32_t(sizeof(Mesh3DUBO)), align);
    shadowUboStride = alignUpValue(uint32_t(sizeof(ShadowUBO)), align);
    mesh3dClusteredUboStride = alignUpValue(uint32_t(sizeof(Mesh3DClusteredUBO)), align);
}

void Graphics::ensureMesh3dRing(Mesh3dFrameSlots &fslots) {
    ensureMesh3dStrides();
    const size_t want = std::max<size_t>(2048, fslots.lastDrawCount + 512);
    if (fslots.uboRing.buffer && fslots.capacity >= want) return;
    const size_t cap = std::max(want, fslots.capacity * 2);
    // Only called at frame start (before any draw of this frame is recorded),
    // so releasing the old ring is safe: no in-flight reader of this slot.
    fslots.uboRing.release();
    fslots.uboRing.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer,
                            vk::DeviceSize(cap) * mesh3dUboStride, kHostVisibleCoherent);
    fslots.shadowRing.release();
    fslots.shadowRing.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer,
                               vk::DeviceSize(cap) * shadowUboStride, kHostVisibleCoherent);
    fslots.capacity = cap;
    fslots.sets.clear();  // cached sets reference the old rings
    fslots.skinSets.clear();
}

void Graphics::ensureMesh3dClusteredRing(Mesh3dClusteredFrameSlots &fslots) {
    ensureMesh3dStrides();
    const size_t want = std::max<size_t>(2048, fslots.lastDrawCount + 512);
    if (fslots.uboRing.buffer && fslots.capacity >= want) return;
    const size_t cap = std::max(want, fslots.capacity * 2);
    fslots.uboRing.release();
    fslots.uboRing.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer,
                            vk::DeviceSize(cap) * mesh3dClusteredUboStride, kHostVisibleCoherent);
    fslots.shadowRing.release();
    fslots.shadowRing.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer,
                               vk::DeviceSize(cap) * shadowUboStride, kHostVisibleCoherent);
    fslots.capacity = cap;
    fslots.sets.clear();
}

vkb::BoundSet Graphics::mesh3dClusteredSetFor(GpuTexture *gpuTex, GpuTexture *normalTex,
                                              GpuTexture *envTex, GpuTexture *heightTex,
                                              GpuTexture *decalAlbedo, GpuTexture *decalNormal,
                                              GpuTexture *decalParams,
                                              Mesh3dClusteredFrameSlots &fslots) {
    ASSERT(gpuTex != nullptr);
    ASSERT(normalTex != nullptr);
    ASSERT(envTex != nullptr);
    ASSERT(heightTex != nullptr);
    ASSERT(decalAlbedo != nullptr);
    ASSERT(decalNormal != nullptr);
    ASSERT(decalParams != nullptr);
    ASSERT(currentShadowArrayView());
    ASSERT(fslots.uboRing.buffer);
    ASSERT(fslots.shadowRing.buffer);

    Mesh3dSetKey key{gpuTex, normalTex, envTex, heightTex, nullptr,
                     decalAlbedo, decalNormal, decalParams};
    auto it = fslots.sets.find(key);
    if (it != fslots.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &mesh3dClusteredSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    auto &st = currentClusteredStorage();
    vkb::DescriptorSetUpdater updater(14, 14, 0);
    updater.beginDescriptorSet(unbound)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBufferDynamic)
        .buffer(fslots.uboRing.buffer, 0, fslots.uboRing.size)
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpuTex->sampler, gpuTex->imageView()))
        .beginImages(2, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(normalTex->sampler, normalTex->imageView()))
        .beginImages(3, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(envTex->sampler, envTex->imageView()))
        .beginBuffers(4, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(st.lightsBuf.buffer, 0, vk::DeviceSize(st.lightsCap))
        .beginBuffers(5, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(st.tableBuf.buffer, 0, vk::DeviceSize(st.tableCap))
        .beginBuffers(6, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(st.indicesBuf.buffer, 0, vk::DeviceSize(st.indicesCap))
        .beginBuffers(7, 0, vk::DescriptorType::eUniformBufferDynamic)
        .buffer(fslots.shadowRing.buffer, 0, fslots.shadowRing.size)
        .beginImages(8, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(shadowSampler, currentShadowArrayView()))
        .beginImages(9, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(heightTex->sampler, heightTex->imageView()))
        .beginImages(10, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(decalAlbedo->sampler, decalAlbedo->imageView()))
        .beginImages(11, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(decalNormal->sampler, decalNormal->imageView()))
        .beginImages(12, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(decalParams->sampler, decalParams->imageView()))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    fslots.sets.emplace(key, bound);
    return bound;
}

vk::Pipeline Graphics::createMesh3DStylePipeline(const std::vector<uint32_t> &vert,
                                                 const std::vector<uint32_t> &frag,
                                                 vk::PipelineLayout layout,
                                                 const vkb::BuiltRenderPass &rp,
                                                 vk::SampleCountFlagBits samples) {
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    vk::Pipeline pipe =
        device.createPipeline()
            .useClassicPipeline(vertModule, fragModule)
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<MeshVertex>()
                                     .addAttributeDescription<MeshVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eClockwise)
            .setMultisampler(false, samples)
            .setDepthStencil(true, true, vk::CompareOp::eLess)
            .setColorAttachmentCount(1)
            .build(rp);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
    return pipe;
}

vk::Pipeline Graphics::createMesh3DHairPipeline(const std::vector<uint32_t> &vert,
                                                const std::vector<uint32_t> &frag,
                                                vk::PipelineLayout layout,
                                                const vkb::BuiltRenderPass &rp,
                                                vk::SampleCountFlagBits samples) {
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    vk::Pipeline pipe =
        device.createPipeline()
            .useClassicPipeline(vertModule, fragModule)
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<MeshVertex>()
                                     .addAttributeDescription<MeshVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eClockwise)
            .setDepthBias(-1.5f, -1.0f)
            .setMultisampler(false, samples)
            .setDepthStencil(true, false, vk::CompareOp::eLess)
            .setAlphaBlending(1)
            .build(rp);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
    return pipe;
}

vk::Pipeline Graphics::createMesh3DXrayPipeline(const std::vector<uint32_t> &vert,
                                                const std::vector<uint32_t> &frag,
                                                vk::PipelineLayout layout,
                                                const vkb::BuiltRenderPass &rp,
                                                vk::SampleCountFlagBits samples) {
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    vk::Pipeline pipe =
        device.createPipeline()
            .useClassicPipeline(vertModule, fragModule)
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<MeshVertex>()
                                     .addAttributeDescription<MeshVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eClockwise)
            .setMultisampler(false, samples)
            // Depth test/write off: the shader discards visible fragments itself
            // (sampling G-buffer scene depth), so occluded ones can paint over walls.
            .setDepthStencil(false, false, vk::CompareOp::eLess)
            .setAlphaBlending(1)
            .setColorAttachmentCount(1)
            .build(rp);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
    return pipe;
}

void Graphics::ensureOffscreenPipelines() {
    if (offscreenRenderPass) return;

    vkb::RenderPassBuilder rpBuilder = device.createRenderPass();
    offscreenRenderPass =
        rpBuilder.addSampledColorAttachment(vk::Format::eR8G8B8A8Unorm)
            .addSubpass(vkb::SubpassBuilder().addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal))
            .addDependency(VK_SUBPASS_EXTERNAL, 0)
            .build();

    offscreenSolidPipeline =
        createSolidColorPipeline(device, offscreenRenderPass, pipelineLayout);
    offscreenSolidAlphaPipeline =
        createSolidColorPipeline(device, offscreenRenderPass, pipelineLayout, BlendMode::Alpha);
    offscreenAdditiveSolidPipeline =
        createSolidColorPipeline(device, offscreenRenderPass, pipelineLayout, BlendMode::Additive);
    offscreenPremultipliedSolidPipeline = createSolidColorPipeline(
        device, offscreenRenderPass, pipelineLayout, BlendMode::Premultiplied);
    offscreenMultiplySolidPipeline = createSolidColorPipeline(
        device, offscreenRenderPass, pipelineLayout, BlendMode::Multiply);

    // Textured offscreen pipeline mirrors createTexturedPipeline but with offscreen RP.
    auto tvert = embeddedSpirv(textured_vert_spv);
    auto tfrag = embeddedSpirv(textured_frag_spv);
    offscreenTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout);
    offscreenAdditiveTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout,
                                    BlendMode::Additive);
    offscreenPremultipliedTexPipeline = createTexturedStylePipeline(
        tvert, tfrag, offscreenRenderPass, texPipelineLayout, BlendMode::Premultiplied);
    offscreenMultiplyTexPipeline = createTexturedStylePipeline(
        tvert, tfrag, offscreenRenderPass, texPipelineLayout, BlendMode::Multiply);
    offscreenOpaqueTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout,
                                    BlendMode::Opaque);

    if (lit2dPipelineLayout) {
        auto lvert = embeddedSpirv(lit2d_vert_spv);
        auto lfrag = embeddedSpirv(lit2d_frag_spv);
        offscreenLitPipeline =
            createTexturedStylePipeline(lvert, lfrag, offscreenRenderPass, lit2dPipelineLayout);
    }

    // Lazily create offscreen pipelines for any custom shaders already loaded.
    for (auto &sh : ownedShaders) ensureShaderOffscreenPipeline(sh.get());
}

void Graphics::ensureShaderOffscreenPipeline(Shader *shader) {
    if (!shader || !shader->gpuHandle || !offscreenRenderPass) return;
    // Mesh3D SPIR-V / layout is incompatible with the 2D textured offscreen pass.
    if (shader->getKind() == Shader::Kind::eMesh3D) return;
    auto *gpu = static_cast<GpuShader *>(shader->gpuHandle);
    if (gpu->isMesh3D || gpu->offscreenPipeline) return;
    gpu->offscreenPipeline = createTexturedStylePipeline(shader->vertexSpirv(), shader->fragmentSpirv(),
                                                         offscreenRenderPass, shaderPipelineLayout);
}

Texture *Graphics::getTexture() { return nullptr; }


}  // namespace eve::graphics::vulkan
