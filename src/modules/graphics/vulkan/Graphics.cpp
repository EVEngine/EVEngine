#define VKB_IMPL
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "common/Exception.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <memory>

#include "graphics/shaders/color_vert_spv.inc"
#include "graphics/shaders/color_frag_spv.inc"
#include "graphics/shaders/textured_vert_spv.inc"
#include "graphics/shaders/textured_frag_spv.inc"
#include "graphics/shaders/mesh3d_vert_spv.inc"
#include "graphics/shaders/mesh3d_frag_spv.inc"

#include <assimp/mesh.h>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics::vulkan {

Graphics::~Graphics() {
    if (!initialized) return;
    device->waitIdle();
    ownedCanvases.clear();
    ownedMeshes.clear();
    ownedGpuMeshes.clear();
    ownedTextures.clear();
    for (auto &g : ownedGpuTextures) {
        if (g->sampler) device->destroySampler(g->sampler);
    }
    ownedGpuTextures.clear();
    destroySwapchainResources();
    if (pipeline) device->destroyPipeline(pipeline);
    if (pipelineLayout) device->destroyPipelineLayout(pipelineLayout);
    if (texPipeline) device->destroyPipeline(texPipeline);
    if (texPipelineLayout) device->destroyPipelineLayout(texPipelineLayout);
    if (mesh3dPipeline) device->destroyPipeline(mesh3dPipeline);
    if (mesh3dPipelineLayout) device->destroyPipelineLayout(mesh3dPipelineLayout);
    mesh3dSetLayoutUnique.reset();
    mesh3dUboSlots.clear();
    if (offscreenSolidPipeline) device->destroyPipeline(offscreenSolidPipeline);
    if (offscreenTexPipeline) device->destroyPipeline(offscreenTexPipeline);
    if (offscreenRenderPass) device->destroyRenderPass(offscreenRenderPass);
    texSetLayoutUnique.reset();
    if (descriptorPool) device->destroyDescriptorPool(descriptorPool);
    if (uploadPool) device->destroyCommandPool(uploadPool);
    if (renderpass) device->destroyRenderPass(renderpass);
    if (surface) inst.instance.destroySurfaceKHR(surface);
}

void Graphics::initWithWindow(void *nativeWindow) {
    if (initialized) {
        // Window module may destroy/recreate the SDL window (tests, setWindowSettings).
        // The Vulkan surface is tied to the native window — rebuild it when the
        // handle changes, otherwise acquire/present use a destroyed surface.
        if (sdlWindow == nativeWindow) return;
        sdlWindow = nativeWindow;
        recreateSurfaceForResume();
        return;
    }
    sdlWindow = nativeWindow;
    auto *window = static_cast<SDL_Window *>(nativeWindow);
    ASSERT(window != nullptr);
    if (!window) throw Exception("Graphics::initWithWindow: null SDL_Window");

    unsigned int count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr))
        throw Exception("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

    std::vector<const char *> extNames(count);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &count, extNames.data()))
        throw Exception("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());

    vkb::InstanceBuilder builder;
    builder.require_api_version(1, 0).use_default_debug_messenger();
#if !defined(EVENGINE_IOS)
    // iOS bring-up skips validation layers (need embedded layer frameworks).
    builder.request_validation_layers();
#endif
    for (auto *name : extNames) builder.enable_extension(name);
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_IOS)
    // SDL already supplies surface extensions; avoid duplicating them via
    // InstanceBuilder's non-headless window path, and enable MoltenVK portability.
    builder.set_headless(true);
    // Portability enumeration comes from the Khronos loader. iOS links MoltenVK
    // directly, so the extension is absent there and asking for it aborts
    // instance creation.
    if (vkb::SystemInfo::query().is_extension_available(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        builder.enable_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        builder.add_flags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
    }
#endif
    inst = builder.build();

    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(inst.instance), &rawSurface))
        throw Exception("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
    surface = rawSurface;

    vkb::PhysicalDeviceSelector selector{inst};
    selector.set_surface(surface).set_minimum_version(1, 0);
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_IOS)
    selector.add_required_extension("VK_KHR_portability_subset");
#endif
    auto phys = selector.select();
    vkb::DeviceBuilder deviceBuilder{phys};
    device = deviceBuilder.build();

    uploadPool = device.createCommandPool();

    int pw = 0, ph = 0;
    SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
    int lw = 0, lh = 0;
    SDL_GetWindowSize(window, &lw, &lh);
    setViewportSize(lw, lh, pw, ph);

    createSwapchainAndPipeline();
    createTexturedPipeline();
    createMesh3DPipeline();
    initialized = true;
    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    whiteTexture = newTexture(1, 1, whitePixel);
}

void Graphics::destroySwapchainResources() {
    presentModel = vkb::Present{};
    depthImage = vkb::DepthStencilImage{};
}

bool Graphics::rebuildSwapchainIfNeeded() {
    const bool wantRecreate =
        surfaceNeedsRecreate.load() || swapchainDirty || presentModel.needs_recreate;
    if (!wantRecreate) return true;
    if (!isRenderSurfaceStable()) return false;

    if (surfaceNeedsRecreate.exchange(false))
        recreateSurfaceForResume();
    if (presentModel.needs_recreate) {
        presentModel.needs_recreate = false;
        swapchainDirty = true;
    }
    if (swapchainDirty) {
        device->waitIdle();
        createSwapchainAndPipeline();
    }
    return true;
}

bool Graphics::beginPresentCommandBuffer() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!rebuildSwapchainIfNeeded()) return false;
        presentModel.begin();
        if (presentModel.has_acquired_image) return true;
        // Acquire failed without beginning the CB (see Present::begin). Rebuild once.
        if (presentModel.needs_recreate) {
            presentModel.needs_recreate = false;
            swapchainDirty = true;
            continue;
        }
        return false;
    }
    return false;
}

bool Graphics::isRenderSurfaceReady() const {
#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS)
    auto *window = static_cast<SDL_Window *>(sdlWindow);
    if (!window) return false;
    // During background/resume and orientation changes the native window is
    // torn down and rebuilt; SDL reports a zero drawable size until it settles.
    int pw = 0, ph = 0;
    SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
    return pw > 0 && ph > 0;
#else
    return true;
#endif
}

bool Graphics::isRenderSurfaceStable() {
#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS)
    auto *window = static_cast<SDL_Window *>(sdlWindow);
    if (!window) return false;
    int pw = 0, ph = 0;
    SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
    if (pw <= 0 || ph <= 0) {
        surfaceStableFrames = 0;
        return false;
    }
    if (pw != pendingSurfaceW || ph != pendingSurfaceH) {
        // Size still changing (mid-rotation / Split View); wait for it to settle.
        pendingSurfaceW = pw;
        pendingSurfaceH = ph;
        surfaceStableFrames = 1;
        return false;
    }
    // Require a couple of consecutive identical polls before rebuilding.
    if (surfaceStableFrames < 3) {
        ++surfaceStableFrames;
        return false;
    }
    return true;
#else
    return true;
#endif
}

void Graphics::recreateSurfaceForResume() {
    // Mobile platforms destroy the native window when backgrounded, which
    // invalidates the VkSurfaceKHR. On resume SDL creates a fresh native
    // window, so we must rebuild the surface (and the swapchain that depends on
    // it) before presenting again. Runs on the render thread.
    if (!initialized) return;
    auto *window = static_cast<SDL_Window *>(sdlWindow);
    if (!window) return;

    device->waitIdle();

    // Tear down surface-dependent resources first.
    destroySwapchainResources();
    swapchain.destroy();
    swapchain = vkb::Swapchain{};
    if (surface) {
        inst.instance.destroySurfaceKHR(surface);
        surface = VK_NULL_HANDLE;
    }

    // Create a new surface from the freshly created native window.
    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(inst.instance), &rawSurface))
        throw Exception("resume: SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
    surface = rawSurface;
    device.surface = surface;

    // Refresh drawable size (orientation / Split View may have changed).
    int pw = 0, ph = 0, lw = 0, lh = 0;
    SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
    SDL_GetWindowSize(window, &lw, &lh);
    if (pw > 0 && ph > 0)
        setViewportSize(lw, lh, pw, ph);

    swapchainDirty = true;
}

void Graphics::createSwapchainAndPipeline() {
    vkb::SwapchainBuilder swapchainBuilder{device};
    if (pixelWidth > 0 && pixelHeight > 0)
        swapchainBuilder.set_desired_extent(uint32_t(pixelWidth), uint32_t(pixelHeight));
    // Prefer UNORM so clear/draw Color floats match getPixel without sRGB encode.
    swapchainBuilder.set_desired_format(
        {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear});
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
        std::vector<uint32_t> vert(color_vert_spv, color_vert_spv + color_vert_spv_count);
        std::vector<uint32_t> frag(color_frag_spv, color_frag_spv + color_frag_spv_count);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        pipelineLayout = device->createPipelineLayout(layoutInfo);

        vkb::PipelineBuilder pipelineBuilder{device, swapchain};
        pipeline = pipelineBuilder.useClassicPipeline(vert, frag)
                       .setVertexInputState(vkb::VertexInputStateBuilder()
                                                .addInputBinding<ColorVertex>()
                                                .addAttributeDescription<ColorVertex>())
                       .setDynamicStatesViewportScissor()
                       .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f,
                                      vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise)
                       .build(renderpass);
    }

    vkb::PresentBuilder presentBuilder{device, swapchain};
    presentModel = presentBuilder.build(renderpass, depthImage.imageView());
    ensurePresentCaptureHook();
    swapchainDirty = false;
}

void Graphics::createTexturedPipeline() {
    if (texPipeline) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    texSetLayoutUnique = layoutBuilder
                             .image(0, vk::DescriptorType::eCombinedImageSampler,
                                    vk::ShaderStageFlagBits::eFragment, 1)
                             .createUnique(device.instance);
    texSetLayout = *texSetLayoutUnique;

    vk::DescriptorPoolSize poolSizes[] = {
        {vk::DescriptorType::eCombinedImageSampler, 128},
        {vk::DescriptorType::eUniformBuffer, 64},
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 160;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    descriptorPool = device->createDescriptorPool(poolInfo);

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &texSetLayout;
    texPipelineLayout = device->createPipelineLayout(plInfo);

    std::vector<uint32_t> vert(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    std::vector<uint32_t> frag(textured_frag_spv, textured_frag_spv + textured_frag_spv_count);
    vk::ShaderModule vertModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule =
        vkb::PipelineBuilder::createShaderModule(device.instance, frag);

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto binding = TexturedVertex::getBindingDescription(0);
    auto attrs = TexturedVertex::getAttributeDescription(0);
    vk::PipelineVertexInputStateCreateInfo vi{};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = uint32_t(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    vk::PipelineInputAssemblyStateCreateInfo ia{};
    ia.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo vp{};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rs{};
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone;
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable = false;
    ds.depthWriteEnable = false;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blendAtt.blendEnable = true;
    blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.colorBlendOp = vk::BlendOp::eAdd;
    blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.alphaBlendOp = vk::BlendOp::eAdd;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAtt;

    vk::DynamicState dynStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    vk::GraphicsPipelineCreateInfo pci{};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = texPipelineLayout;
    pci.renderPass = renderpass;
    pci.subpass = 0;

    auto result = device->createGraphicsPipeline(nullptr, pci);
    if (result.result != vk::Result::eSuccess)
        throw Exception("failed to create textured pipeline");
    texPipeline = result.value;

    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
}

void Graphics::createMesh3DPipeline() {
    if (mesh3dPipeline) return;

    // Right-handed view, Y-up; Vulkan clip via glm::perspectiveRH_ZO. Front face CCW, cull back.
    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    mesh3dSetLayoutUnique =
        layoutBuilder
            .buffer(0, vk::DescriptorType::eUniformBuffer,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    mesh3dSetLayout = *mesh3dSetLayoutUnique;

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &mesh3dSetLayout;
    mesh3dPipelineLayout = device->createPipelineLayout(plInfo);

    // UBO + descriptor sets are allocated lazily per draw (see mesh3dSetFor).
    mesh3dUboSlots.clear();
    mesh3dDrawIndex = 0;

    std::vector<uint32_t> vert(mesh3d_vert_spv, mesh3d_vert_spv + mesh3d_vert_spv_count);
    std::vector<uint32_t> frag(mesh3d_frag_spv, mesh3d_frag_spv + mesh3d_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto binding = MeshVertex::getBindingDescription(0);
    auto attrs = MeshVertex::getAttributeDescription(0);
    vk::PipelineVertexInputStateCreateInfo vi{};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = uint32_t(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    vk::PipelineInputAssemblyStateCreateInfo ia{};
    ia.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo vp{};
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rs{};
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eBack;
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable = true;
    ds.depthWriteEnable = true;
    ds.depthCompareOp = vk::CompareOp::eLess;

    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blendAtt.blendEnable = false;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAtt;

    vk::DynamicState dynStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    vk::GraphicsPipelineCreateInfo pci{};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = mesh3dPipelineLayout;
    pci.renderPass = renderpass;
    pci.subpass = 0;

    auto result = device->createGraphicsPipeline(nullptr, pci);
    if (result.result != vk::Result::eSuccess)
        throw Exception("failed to create mesh3d pipeline");
    mesh3dPipeline = result.value;

    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
}

void Graphics::ensureOffscreenPipelines() {
    if (offscreenRenderPass) return;

    vkb::RenderPassBuilder rpBuilder{device};
    vk::AttachmentDescription ad{};
    ad.format = vk::Format::eR8G8B8A8Unorm;
    ad.samples = vk::SampleCountFlagBits::e1;
    ad.loadOp = vk::AttachmentLoadOp::eClear;
    ad.storeOp = vk::AttachmentStoreOp::eStore;
    ad.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    ad.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    ad.initialLayout = vk::ImageLayout::eUndefined;
    ad.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    offscreenRenderPass =
        rpBuilder.addAttachment(ad)
            .addSubpass(vkb::SubpassBuilder().addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal))
            .addDependency(VK_SUBPASS_EXTERNAL, 0)
            .build();

    std::vector<uint32_t> vert(color_vert_spv, color_vert_spv + color_vert_spv_count);
    std::vector<uint32_t> frag(color_frag_spv, color_frag_spv + color_frag_spv_count);
    vkb::PipelineBuilder solidBuilder{device, swapchain};
    offscreenSolidPipeline =
        solidBuilder.useClassicPipeline(vert, frag)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<ColorVertex>()
                                     .addAttributeDescription<ColorVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eCounterClockwise)
            .build(offscreenRenderPass);

    // Textured offscreen pipeline mirrors createTexturedPipeline but with offscreen RP.
    std::vector<uint32_t> tvert(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    std::vector<uint32_t> tfrag(textured_frag_spv, textured_frag_spv + textured_frag_spv_count);
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, tvert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, tfrag);

    vk::PipelineShaderStageCreateInfo stages[2]{};
    stages[0].stage = vk::ShaderStageFlagBits::eVertex;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].stage = vk::ShaderStageFlagBits::eFragment;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    auto binding = TexturedVertex::getBindingDescription(0);
    auto attrs = TexturedVertex::getAttributeDescription(0);
    vk::PipelineVertexInputStateCreateInfo vi{};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = uint32_t(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    vk::PipelineInputAssemblyStateCreateInfo ia{};
    ia.topology = vk::PrimitiveTopology::eTriangleList;
    vk::PipelineViewportStateCreateInfo vp{};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    vk::PipelineRasterizationStateCreateInfo rs{};
    rs.polygonMode = vk::PolygonMode::eFill;
    rs.cullMode = vk::CullModeFlagBits::eNone;
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0f;
    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;
    vk::PipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    blendAtt.blendEnable = true;
    blendAtt.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAtt.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.colorBlendOp = vk::BlendOp::eAdd;
    blendAtt.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAtt.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAtt.alphaBlendOp = vk::BlendOp::eAdd;
    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAtt;
    vk::DynamicState dynStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dyn{};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    vk::GraphicsPipelineCreateInfo pci{};
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &blend;
    pci.pDynamicState = &dyn;
    pci.layout = texPipelineLayout;
    pci.renderPass = offscreenRenderPass;
    pci.subpass = 0;
    auto result = device->createGraphicsPipeline(nullptr, pci);
    if (result.result != vk::Result::eSuccess)
        throw Exception("failed to create offscreen textured pipeline");
    offscreenTexPipeline = result.value;
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
}

Texture *Graphics::getTexture() { return nullptr; }

void Graphics::ensurePresentCaptureHook() {
    presentModel.after_render_before_present = [this](uint32_t imageIndex) {
        if (!screenReadbackEnabled) return;
        captureSwapchainImage(imageIndex);
    };
}

void Graphics::captureSwapchainImage(uint32_t imageIndex) {
    if (pixelWidth <= 0 || pixelHeight <= 0) return;

    const vk::Format fmt = swapchain.image_format;
    const bool bgra = (fmt == vk::Format::eB8G8R8A8Unorm || fmt == vk::Format::eB8G8R8A8Srgb);
    const bool rgba = (fmt == vk::Format::eR8G8B8A8Unorm || fmt == vk::Format::eR8G8B8A8Srgb);
    if (!bgra && !rgba)
        throw Exception("Graphics::captureSwapchainImage: unsupported swapchain format");

    auto &images = swapchain.get_images();
    if (imageIndex >= images.size())
        throw Exception("Graphics::captureSwapchainImage: invalid image index");

    const vk::DeviceSize byteSize =
        vk::DeviceSize(pixelWidth) * vk::DeviceSize(pixelHeight) * 4;
    vkb::GenericBuffer staging(device, vk::BufferUsageFlagBits::eTransferDst, byteSize,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent);

    const vk::Image image = images[imageIndex];
    // Called after submit waitIdle, before present — image still acquired, layout PresentSrcKHR.
    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                vk::ImageMemoryBarrier toTransfer{};
                                toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                                toTransfer.oldLayout = vk::ImageLayout::ePresentSrcKHR;
                                toTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
                                toTransfer.image = image;
                                toTransfer.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
                                toTransfer.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
                                toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
                                cb.pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe,
                                                   vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0,
                                                   nullptr, 1, &toTransfer);

                                vk::BufferImageCopy region{};
                                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                                region.imageExtent =
                                    vk::Extent3D{uint32_t(pixelWidth), uint32_t(pixelHeight), 1};
                                cb.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal,
                                                     staging.buffer, region);

                                vk::ImageMemoryBarrier toPresent = toTransfer;
                                toPresent.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
                                toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
                                toPresent.srcAccessMask = vk::AccessFlagBits::eTransferRead;
                                toPresent.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
                                cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                                   vk::PipelineStageFlagBits::eBottomOfPipe, {}, 0, nullptr, 0,
                                                   nullptr, 1, &toPresent);
                            });

    lastFrameRgba.resize(size_t(byteSize));
    void *mapped = device->mapMemory(staging.memory, 0, byteSize);
    const size_t rowBytes = size_t(pixelWidth) * 4;
    auto *src = static_cast<const uint8_t *>(mapped);
    for (int y = 0; y < pixelHeight; ++y) {
        const uint8_t *srcRow = src + size_t(y) * rowBytes;
        uint8_t *dstRow = lastFrameRgba.data() + size_t(pixelHeight - 1 - y) * rowBytes;
        if (bgra) {
            for (int x = 0; x < pixelWidth; ++x) {
                const uint8_t *s = srcRow + size_t(x) * 4;
                uint8_t *d = dstRow + size_t(x) * 4;
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
            }
        } else {
            std::memcpy(dstRow, srcRow, rowBytes);
        }
    }
    device->unmapMemory(staging.memory);
    staging.release();
    hasPresentedFrame = true;
}

image::ImageData *Graphics::newImageData() {
    if (!hasPresentedFrame || lastFrameRgba.empty())
        throw Exception("Graphics::newImageData: no presented frame");
    auto *img = new image::ImageData(pixelWidth, pixelHeight, "RGBA8");
    std::memcpy(img->getData(), lastFrameRgba.data(), lastFrameRgba.size());
    return img;
}

Color Graphics::getPixel(int x, int y) {
    if (!hasPresentedFrame || lastFrameRgba.empty())
        throw Exception("Graphics::getPixel: no presented frame");
    if (x < 0 || y < 0 || x >= width || y >= height)
        throw Exception("Graphics::getPixel: out of bounds (%d,%d)", x, y);

    const int pxX = (width > 0) ? int((int64_t(x) * pixelWidth) / width) : x;
    const int pxY = (height > 0) ? int((int64_t(y) * pixelHeight) / height) : y;
    const int cx = std::min(std::max(pxX, 0), pixelWidth - 1);
    const int cy = std::min(std::max(pxY, 0), pixelHeight - 1);
    const size_t i = (size_t(cy) * size_t(pixelWidth) + size_t(cx)) * 4;
    float r = lastFrameRgba[i + 0] / 255.f;
    float g = lastFrameRgba[i + 1] / 255.f;
    float b = lastFrameRgba[i + 2] / 255.f;
    float a = lastFrameRgba[i + 3] / 255.f;
    // If surface ended up sRGB, convert encoded bytes back to linear Color space.
    const vk::Format fmt = swapchain.image_format;
    if (fmt == vk::Format::eB8G8R8A8Srgb || fmt == vk::Format::eR8G8B8A8Srgb) {
        auto toLinear = [](float u) {
            return (u <= 0.04045f) ? (u / 12.92f) : std::pow((u + 0.055f) / 1.055f, 2.4f);
        };
        r = toLinear(r);
        g = toLinear(g);
        b = toLinear(b);
    }
    return Color(r, g, b, a);
}

Canvas *Graphics::newCanvas(int w, int h) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    if (!initialized) throw Exception("newCanvas: graphics not initialized");
    if (w <= 0 || h <= 0) throw Exception("newCanvas: invalid size");
    ensureOffscreenPipelines();
    auto c = std::make_unique<OffscreenCanvas>(this, w, h);
    Canvas *raw = c.get();
    ownedCanvases.push_back(std::move(c));
    return raw;
}

void Graphics::setCanvas(Canvas *canvas) {
    Canvas *next = canvas;
    if (next == static_cast<Canvas *>(this)) next = nullptr;
    if (next == activeCanvas) return;
    if (!solidBatch.empty() || !texturedBatches.empty()) flushBatch();
    activeCanvas = next;
}

bool Graphics::isCanvasActive() const {
    return activeCanvas != nullptr;
}

Canvas *Graphics::getCanvas() const {
    return activeCanvas ? activeCanvas : const_cast<Graphics *>(this);
}

void Graphics::setViewportSize(int newW, int newH, int newPw, int newPh) {
    bool changed = (newW != width) || (newH != height) || (newPw != pixelWidth) || (newPh != pixelHeight);
    width = newW;
    height = newH;
    pixelWidth = newPw;
    pixelHeight = newPh;
    if (initialized && changed) swapchainDirty = true;
}

void Graphics::clear(std::optional<Color> color, std::optional<int>, std::optional<double>) {
    // Keep 3D framebuffer contents when composing 2D on top of an open 3D pass.
    if (frameHad3D && activeCanvas == nullptr) return;
    clearColor = color.value_or(backgroundColor);
    hasPendingClear = true;
    solidBatch.clear();
    texturedBatches.clear();
    if (auto *oc = dynamic_cast<OffscreenCanvas *>(activeCanvas)) {
        oc->clear(clearColor, std::nullopt, std::nullopt);
    }
}

void Graphics::drawSolidRect(float x, float y, float w, float h, const Color &color) {
    solidBatch.addRect(x, y, w, h, color);
}

Texture *Graphics::newTexture(int w, int h, const uint8_t *rgba) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    ASSERT(rgba != nullptr);
    if (!initialized) throw Exception("newTexture: graphics not initialized");
    if (w <= 0 || h <= 0 || !rgba) throw Exception("newTexture: invalid args");

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h));
    std::vector<uint8_t> bytes(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    vkb::SamplerBuilder sb;
    gpu->sampler = sb.magFilter(vk::Filter::eNearest)
                       .minFilter(vk::Filter::eNearest)
                       .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                       .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                       .build(device.instance);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);
    gpu->descriptorSet = sets[0];

    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(gpu->descriptorSet)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(gpu->sampler, gpu->image.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .update(device.instance);

    auto tex = std::make_unique<Texture>();
    tex->width = w;
    tex->height = h;
    tex->pixelWidth = w;
    tex->pixelHeight = h;
    tex->gpuHandle = gpu.get();

    Texture *raw = tex.get();
    ownedTextures.push_back(std::move(tex));
    ownedGpuTextures.push_back(std::move(gpu));
    return raw;
}

Texture *Graphics::newTexture(image::ImageData *data) {
    ASSERT(data != nullptr);
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw Exception("newTexture: only RGBA8 ImageData supported for now");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()));
}

Texture *Graphics::newTextureFromFile(const std::string &filename) {
    ASSERT(!filename.empty());
    if (filename.empty()) throw Exception("newTextureFromFile: empty filename");

    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fileData(fs->read(filename));
    if (!fileData) throw Exception("newTextureFromFile: failed to read '%s'", filename.c_str());

    auto *imgMod = image::Image::create();
    std::unique_ptr<image::ImageData> data(imgMod->newImageData(fileData.get()));
    return newTexture(data.get());
}

void Graphics::drawTexturedRect(Texture *texture, float x, float y, float w, float h, const Color &color) {
    if (!texture) {
        drawSolidRect(x, y, w, h, color);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture) {
        texturedBatches.push_back(TexturedBatch{texture, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, color, 0, 0, 1, 1);
}

void Graphics::flushBatch() {
    if (!initialized) return;
    if (isCanvasActive()) {
        auto *oc = dynamic_cast<OffscreenCanvas *>(activeCanvas);
        if (!oc) throw Exception("flushBatch: active canvas is not an OffscreenCanvas");
        flushToOffscreen(oc);
    } else {
        flushToSwapchain();
    }
}

void Graphics::flushToOffscreen(OffscreenCanvas *canvas) {
    Batcher solid = solidBatch;
    auto textured = std::move(texturedBatches);
    solidBatch.clear();
    texturedBatches.clear();

    const Color cc = canvas->pendingClearColor();
    const bool needClear = canvas->takePendingClear();
    if (solid.empty() && textured.empty() && !needClear) return;

    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                canvas->colorImage().setLayout(cb, vk::ImageLayout::eColorAttachmentOptimal);

                                vk::ClearValue cv{
                                    vk::ClearColorValue(std::array<float, 4>{cc.r, cc.g, cc.b, cc.a})};
                                vk::RenderPassBeginInfo rpBegin{};
                                rpBegin.renderPass = offscreenRenderPass;
                                rpBegin.framebuffer = canvas->framebuffer();
                                rpBegin.renderArea.extent =
                                    vk::Extent2D{uint32_t(canvas->getWidth()), uint32_t(canvas->getHeight())};
                                rpBegin.clearValueCount = 1;
                                rpBegin.pClearValues = &cv;
                                cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

                                vk::Viewport vp{0.f, 0.f, float(canvas->getWidth()), float(canvas->getHeight()),
                                                0.f, 1.f};
                                vk::Rect2D scissor{{0, 0},
                                                   {uint32_t(canvas->getWidth()), uint32_t(canvas->getHeight())}};
                                cb.setViewport(0, 1, &vp);
                                cb.setScissor(0, 1, &scissor);

                                vkb::HostVertexBuffer solidBuf;
                                std::vector<vkb::HostVertexBuffer> texBufs;
                                texBufs.reserve(textured.size());

                                if (!solid.empty() && offscreenSolidPipeline) {
                                    Batcher ndc = solid;
                                    ndc.toNDC(canvas->getWidth(), canvas->getHeight());
                                    std::vector<ColorVertex> gpuVerts;
                                    gpuVerts.reserve(ndc.vertices().size());
                                    for (const auto &v : ndc.vertices())
                                        gpuVerts.push_back(ColorVertex{v.pos, v.color});
                                    solidBuf.allocate<ColorVertex>(device, gpuVerts);
                                    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenSolidPipeline);
                                    vk::DeviceSize offset = 0;
                                    cb.bindVertexBuffers(0, 1, solidBuf, &offset);
                                    cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                }

                                if (offscreenTexPipeline) {
                                    for (auto &tb : textured) {
                                        if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) continue;
                                        auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
                                        Batcher ndc = tb.batch;
                                        ndc.toNDC(canvas->getWidth(), canvas->getHeight());
                                        std::vector<TexturedVertex> gpuVerts;
                                        gpuVerts.reserve(ndc.vertices().size());
                                        for (const auto &v : ndc.vertices())
                                            gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});
                                        texBufs.emplace_back();
                                        texBufs.back().allocate<TexturedVertex>(device, gpuVerts);
                                        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenTexPipeline);
                                        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1,
                                                              &gpu->descriptorSet, 0, nullptr);
                                        vk::DeviceSize offset = 0;
                                        cb.bindVertexBuffers(0, 1, texBufs.back(), &offset);
                                        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                    }
                                }

                                cb.endRenderPass();
                                canvas->colorImage().setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
                            });
}

void Graphics::flushToSwapchain() {
    Batcher solid = solidBatch;
    auto textured = std::move(texturedBatches);
    solidBatch.clear();
    texturedBatches.clear();

    const bool continue3D = swapchainPassOpen;
    if (!continue3D) {
        if (!beginPresentCommandBuffer()) {
            hasPendingClear = false;
            return;
        }
        std::array<vk::ClearValue, 2> clears{};
        clears[0].color = vk::ClearColorValue(
            std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
        clears[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        presentModel.beginRenderPass(renderpass, clears.data(), uint32_t(clears.size()));
        swapchainPassOpen = true;
    }

    auto &cb = presentModel.getCurrentCommandBuffer();
    vk::Viewport vp{0.f, 0.f, float(swapchain.extent.width), float(swapchain.extent.height), 0.f, 1.f};
    vk::Rect2D scissor{{0, 0}, swapchain.extent};
    cb.setViewport(0, 1, &vp);
    cb.setScissor(0, 1, &scissor);

    // Keep HostVertexBuffers alive until after GPU wait in drawFrame().
    vkb::HostVertexBuffer solidBuf;
    std::vector<vkb::HostVertexBuffer> texBufs;
    texBufs.reserve(textured.size());

    if (!solid.empty() && pipeline) {
        Batcher ndc = solid;
        ndc.toNDC(width, height);
        std::vector<ColorVertex> gpuVerts;
        gpuVerts.reserve(ndc.vertices().size());
        for (const auto &v : ndc.vertices())
            gpuVerts.push_back(ColorVertex{v.pos, v.color});
        solidBuf.allocate<ColorVertex>(device, gpuVerts);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, solidBuf, &offset);
        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
    }

    if (texPipeline) {
        for (auto &tb : textured) {
            if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) continue;
            auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
            Batcher ndc = tb.batch;
            ndc.toNDC(width, height);
            std::vector<TexturedVertex> gpuVerts;
            gpuVerts.reserve(ndc.vertices().size());
            for (const auto &v : ndc.vertices())
                gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

            texBufs.emplace_back();
            texBufs.back().allocate<TexturedVertex>(device, gpuVerts);
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, texPipeline);
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1,
                                  &gpu->descriptorSet, 0, nullptr);
            vk::DeviceSize offset = 0;
            cb.bindVertexBuffers(0, 1, texBufs.back(), &offset);
            cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
        }
    }

    // Invalidate prior readback so a failed present cannot reuse a stale frame.
    if (screenReadbackEnabled) hasPresentedFrame = false;

    if (presentOverlayFn_) {
        VkCommandBuffer raw = static_cast<VkCommandBuffer>(cb);
        presentOverlayFn_(presentOverlayUser_, raw);
    }

    presentModel.endRenderPass();
    presentModel.end();
    presentModel.drawFrame();
    // hasPresentedFrame set by capture hook during drawFrame
    hasPendingClear = false;
    swapchainPassOpen = false;
    frameHad3D = false;
}

void Graphics::begin3DFrame() {
    ASSERT(initialized);
    if (!initialized) throw Exception("begin3DFrame: graphics not initialized");
    if (isCanvasActive()) throw Exception("begin3DFrame: cannot start 3D while a Canvas is active");
    if (swapchainPassOpen) throw Exception("begin3DFrame: swapchain pass already open");
    if (!beginPresentCommandBuffer())
        throw Exception("begin3DFrame: failed to acquire swapchain image");

    std::array<vk::ClearValue, 2> clears{};
    clears[0].color = vk::ClearColorValue(
        std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
    clears[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    presentModel.beginRenderPass(renderpass, clears.data(), uint32_t(clears.size()));

    auto &cb = presentModel.getCurrentCommandBuffer();
    vk::Viewport vp{0.f, 0.f, float(swapchain.extent.width), float(swapchain.extent.height), 0.f, 1.f};
    vk::Rect2D scissor{{0, 0}, swapchain.extent};
    cb.setViewport(0, 1, &vp);
    cb.setScissor(0, 1, &scissor);

    mesh3dDrawIndex = 0;
    swapchainPassOpen = true;
    frameHad3D = true;
    hasPendingClear = false;
}

void Graphics::setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) {
    glm::vec3 d = glm::normalize(dir);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    mesh3dFrameUbo.lightDir = glm::vec4(d, 0.f);
    mesh3dFrameUbo.lightColor = glm::vec4(color, 1.f);
}

vk::DescriptorSet Graphics::mesh3dSetFor(GpuTexture *gpuTex, size_t uboSlot) {
    ASSERT(gpuTex != nullptr);
    while (mesh3dUboSlots.size() <= uboSlot) {
        mesh3dUboSlots.emplace_back();
        mesh3dUboSlots.back().ubo.allocate(device, vk::BufferUsageFlagBits::eUniformBuffer,
                                           sizeof(Mesh3DUBO),
                                           vk::MemoryPropertyFlagBits::eHostVisible |
                                               vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    auto &slot = mesh3dUboSlots[uboSlot];
    auto it = slot.sets.find(gpuTex);
    if (it != slot.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &mesh3dSetLayout;
    vk::DescriptorSet set = device->allocateDescriptorSets(alloc).front();

    // Fresh set — never bound to a command buffer yet, so Update is safe.
    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(set)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.ubo.buffer, 0, sizeof(Mesh3DUBO))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(gpuTex->sampler, gpuTex->image.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .update(device.instance);

    slot.sets.emplace(gpuTex, set);
    return set;
}

void Graphics::setMesh3DViewProj(const glm::mat4 &viewProj) {
    mesh3dFrameUbo.mvp = viewProj;
}

Mesh *Graphics::newMeshFromAssimp(const ::aiMesh &mesh) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshFromAssimp: graphics not initialized");
    if (mesh.mNumVertices == 0 || mesh.mNumFaces == 0)
        throw Exception("newMeshFromAssimp: empty mesh");

    std::vector<MeshVertex> verts;
    verts.reserve(mesh.mNumVertices);
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        MeshVertex v{};
        v.pos = {mesh.mVertices[i].x, mesh.mVertices[i].y, mesh.mVertices[i].z};
        if (mesh.HasNormals())
            v.normal = {mesh.mNormals[i].x, mesh.mNormals[i].y, mesh.mNormals[i].z};
        else
            v.normal = {0.f, 1.f, 0.f};
        if (mesh.HasTextureCoords(0))
            v.uv = {mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y};
        else
            v.uv = {0.f, 0.f};
        verts.push_back(v);
    }

    std::vector<uint32_t> indices;
    indices.reserve(mesh.mNumFaces * 3);
    for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
        const aiFace &face = mesh.mFaces[f];
        if (face.mNumIndices != 3) continue;
        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }
    if (indices.empty()) throw Exception("newMeshFromAssimp: no triangle faces");

    auto gpu = std::make_unique<GpuMesh>();
    gpu->vertices.allocate<MeshVertex>(device, verts);
    gpu->indices.allocate(device, vk::BufferUsageFlagBits::eIndexBuffer,
                          indices.size() * sizeof(uint32_t),
                          vk::MemoryPropertyFlagBits::eHostVisible |
                              vk::MemoryPropertyFlagBits::eHostCoherent);
    gpu->indices.updateLocal(indices.data(), indices.size() * sizeof(uint32_t));
    gpu->indexCount = uint32_t(indices.size());

    auto handle = std::make_unique<Mesh>();
    handle->indexCount = int(gpu->indexCount);
    handle->gpuHandle = gpu.get();
    Mesh *raw = handle.get();
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::move(handle));
    return raw;
}

void Graphics::drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) {
    ASSERT(initialized);
    ASSERT(mesh != nullptr);
    if (!initialized) throw Exception("drawMesh: graphics not initialized");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMesh: null mesh");
    if (!swapchainPassOpen) throw Exception("drawMesh: call begin3DFrame first");
    if (!mesh3dPipeline) throw Exception("drawMesh: mesh3d pipeline missing");

    auto *gpuMesh = static_cast<GpuMesh *>(mesh->gpuHandle);
    Texture *tex = texture ? texture : whiteTexture;
    if (!tex || !tex->gpuHandle) throw Exception("drawMesh: missing texture");
    auto *gpuTex = static_cast<GpuTexture *>(tex->gpuHandle);

    // Caller supplies view*proj via model? No — store viewProj in mesh3dFrameUbo.mvp temporarily.
    // drawMesh receives model; mvp = viewProj * model where viewProj is in mesh3dFrameUbo.mvp
    // before multiply. RenderSystem3D sets mesh3dFrameUbo.mvp = viewProj first via setMesh3DViewProj.
    Mesh3DUBO ubo = mesh3dFrameUbo;
    ubo.model = model;
    ubo.mvp = mesh3dFrameUbo.mvp * model;
    ubo.tint = glm::vec4(tint.r, tint.g, tint.b, tint.a);

    const size_t slot = mesh3dDrawIndex++;
    vk::DescriptorSet set = mesh3dSetFor(gpuTex, slot);
    mesh3dUboSlots[slot].ubo.updateLocal(ubo);

    auto &cb = presentModel.getCurrentCommandBuffer();
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dPipelineLayout, 0, 1, &set, 0,
                          nullptr);
    vk::DeviceSize offset = 0;
    cb.bindVertexBuffers(0, 1, gpuMesh->vertices, &offset);
    cb.bindIndexBuffer(gpuMesh->indices.buffer, 0, vk::IndexType::eUint32);
    cb.drawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
}

void Graphics::present() {
    if (!initialized) return;
    if (!isActive()) return;
    if (isCanvasActive()) throw Exception("present: cannot present while a Canvas is active");
    if (!isRenderSurfaceReady()) {
        // Native window is mid-(re)creation / rotation; drop this frame's work
        // instead of touching an invalid swapchain (which crashes the driver).
        solidBatch.clear();
        texturedBatches.clear();
        hasPendingClear = false;
        return;
    }
    flushBatch();
}

void Graphics::draw(eve::graphics::Graphics *, const glm::mat4 &) const {}
void Graphics::draw(Canvas *, const glm::mat4 &) const {}

}  // namespace eve::graphics::vulkan
