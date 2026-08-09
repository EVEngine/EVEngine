#define VKB_IMPL
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/Light.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

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
#include "graphics/shaders/mesh3d_clustered_vert_spv.inc"
#include "graphics/shaders/mesh3d_clustered_frag_spv.inc"
#include "graphics/shaders/mesh3d_shadow_vert_spv.inc"
#include "graphics/shaders/mesh3d_shadow_frag_spv.inc"
#include "graphics/shaders/lit2d_vert_spv.inc"
#include "graphics/shaders/lit2d_frag_spv.inc"

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
    texturesByPath.clear();
    for (auto &g : ownedGpuShaders) {
        if (g->swapchainPipeline) device->destroyPipeline(g->swapchainPipeline);
        if (g->offscreenPipeline) device->destroyPipeline(g->offscreenPipeline);
        if (g->mesh3dPipeline) device->destroyPipeline(g->mesh3dPipeline);
        // pipelineLayout is shared; do not destroy per-shader
    }
    ownedGpuShaders.clear();
    ownedShaders.clear();
    destroySwapchainResources();
    if (pipeline) device->destroyPipeline(pipeline);
    if (pipelineLayout) device->destroyPipelineLayout(pipelineLayout);
    if (texPipeline) device->destroyPipeline(texPipeline);
    if (texPipelineLayout) device->destroyPipelineLayout(texPipelineLayout);
    if (shaderPipelineLayout) device->destroyPipelineLayout(shaderPipelineLayout);
    if (mesh3dPipeline) device->destroyPipeline(mesh3dPipeline);
    if (mesh3dPipelineLayout) device->destroyPipelineLayout(mesh3dPipelineLayout);
    if (mesh3dShaderPipelineLayout) device->destroyPipelineLayout(mesh3dShaderPipelineLayout);
    mesh3dSetLayoutUnique.reset();
    mesh3dUboSlots.clear();
    if (mesh3dClusteredPipeline) device->destroyPipeline(mesh3dClusteredPipeline);
    if (mesh3dClusteredPipelineLayout) device->destroyPipelineLayout(mesh3dClusteredPipelineLayout);
    mesh3dClusteredSetLayoutUnique.reset();
    mesh3dClusteredUboSlots.clear();
    destroyShadowResources();
    auto destroyBuf = [&](vkb::GenericBuffer &b) {
        if (b.buffer) {
            device->destroyBuffer(b.buffer, device.allocation_callbacks);
            device->freeMemory(b.memory, device.allocation_callbacks);
            b.buffer = vk::Buffer{};
            b.memory = vk::DeviceMemory{};
        }
    };
    destroyBuf(clusteredLightsBuf);
    destroyBuf(clusteredTableBuf);
    destroyBuf(clusteredIndicesBuf);
    clusteredLightsCap = clusteredTableCap = clusteredIndicesCap = 0;
    if (lit2dPipeline) device->destroyPipeline(lit2dPipeline);
    if (offscreenLitPipeline) device->destroyPipeline(offscreenLitPipeline);
    if (lit2dPipelineLayout) device->destroyPipelineLayout(lit2dPipelineLayout);
    lit2dSetLayoutUnique.reset();
    lit2dSets.clear();
    if (lighting2dUbo.buffer) {
        device->destroyBuffer(lighting2dUbo.buffer, device.allocation_callbacks);
        device->freeMemory(lighting2dUbo.memory, device.allocation_callbacks);
        lighting2dUbo.buffer = vk::Buffer{};
        lighting2dUbo.memory = vk::DeviceMemory{};
    }
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
    createMesh3DClusteredPipeline();
    // Must be set before createShadowResources(): it clears the shadow cascade
    // layers by calling beginShadowPass()/endShadowPass(), and beginShadowPass()
    // asserts `initialized` is already true.
    initialized = true;
    createShadowResources();
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
        {vk::DescriptorType::eCombinedImageSampler, 1536},
        {vk::DescriptorType::eUniformBuffer, 512},
        {vk::DescriptorType::eStorageBuffer, 128},
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 1024;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    descriptorPool = device->createDescriptorPool(poolInfo);

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &texSetLayout;
    texPipelineLayout = device->createPipelineLayout(plInfo);

    vk::PushConstantRange pcr{};
    pcr.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pcr.offset = 0;
    pcr.size = Shader::kPushConstantBytes;
    vk::PipelineLayoutCreateInfo shaderPlInfo{};
    shaderPlInfo.setLayoutCount = 1;
    shaderPlInfo.pSetLayouts = &texSetLayout;
    shaderPlInfo.pushConstantRangeCount = 1;
    shaderPlInfo.pPushConstantRanges = &pcr;
    shaderPipelineLayout = device->createPipelineLayout(shaderPlInfo);

    std::vector<uint32_t> vert(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    std::vector<uint32_t> frag(textured_frag_spv, textured_frag_spv + textured_frag_spv_count);
    texPipeline = createTexturedStylePipeline(vert, frag, renderpass, texPipelineLayout);

    createLit2DPipeline();
}

vk::Pipeline Graphics::createTexturedStylePipeline(const std::vector<uint32_t> &vert,
                                                   const std::vector<uint32_t> &frag,
                                                   vk::RenderPass rp, vk::PipelineLayout layout) {
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
    pci.layout = layout;
    pci.renderPass = rp;
    pci.subpass = 0;

    auto result = device->createGraphicsPipeline(nullptr, pci);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
    if (result.result != vk::Result::eSuccess)
        throw Exception("failed to create textured-style pipeline");
    return result.value;
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

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &lit2dSetLayout;
    lit2dPipelineLayout = device->createPipelineLayout(plInfo);

    lighting2dUbo.allocate(device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(Lighting2DUBO),
                           vk::MemoryPropertyFlagBits::eHostVisible |
                               vk::MemoryPropertyFlagBits::eHostCoherent);

    std::vector<uint32_t> vert(lit2d_vert_spv, lit2d_vert_spv + lit2d_vert_spv_count);
    std::vector<uint32_t> frag(lit2d_frag_spv, lit2d_frag_spv + lit2d_frag_spv_count);
    lit2dPipeline = createTexturedStylePipeline(vert, frag, renderpass, lit2dPipelineLayout);
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
            .image(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(4, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .image(5, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    mesh3dSetLayout = *mesh3dSetLayoutUnique;

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &mesh3dSetLayout;
    mesh3dPipelineLayout = device->createPipelineLayout(plInfo);

    vk::PushConstantRange pcr{};
    pcr.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pcr.offset = 0;
    pcr.size = Shader::kPushConstantBytes;
    vk::PipelineLayoutCreateInfo shaderPl{};
    shaderPl.setLayoutCount = 1;
    shaderPl.pSetLayouts = &mesh3dSetLayout;
    shaderPl.pushConstantRangeCount = 1;
    shaderPl.pPushConstantRanges = &pcr;
    mesh3dShaderPipelineLayout = device->createPipelineLayout(shaderPl);

    // UBO + descriptor sets are allocated lazily per draw (see mesh3dSetFor).
    mesh3dUboSlots.clear();
    mesh3dDrawIndex = 0;

    std::vector<uint32_t> vert(mesh3d_vert_spv, mesh3d_vert_spv + mesh3d_vert_spv_count);
    std::vector<uint32_t> frag(mesh3d_frag_spv, mesh3d_frag_spv + mesh3d_frag_spv_count);
    mesh3dPipeline = createMesh3DStylePipeline(vert, frag, mesh3dPipelineLayout);
}

void Graphics::createMesh3DClusteredPipeline() {
    if (mesh3dClusteredPipeline) return;

    vkb::DescriptorSetLayoutBuilder layoutBuilder;
    mesh3dClusteredSetLayoutUnique =
        layoutBuilder
            .buffer(0, vk::DescriptorType::eUniformBuffer,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(4, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(5, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(6, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(7, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .image(8, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    mesh3dClusteredSetLayout = *mesh3dClusteredSetLayoutUnique;

    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &mesh3dClusteredSetLayout;
    mesh3dClusteredPipelineLayout = device->createPipelineLayout(plInfo);

    mesh3dClusteredUboSlots.clear();
    mesh3dClusteredDrawIndex = 0;

    std::vector<uint32_t> vert(mesh3d_clustered_vert_spv,
                               mesh3d_clustered_vert_spv + mesh3d_clustered_vert_spv_count);
    std::vector<uint32_t> frag(mesh3d_clustered_frag_spv,
                               mesh3d_clustered_frag_spv + mesh3d_clustered_frag_spv_count);
    mesh3dClusteredPipeline = createMesh3DStylePipeline(vert, frag, mesh3dClusteredPipelineLayout);
}

void Graphics::destroyShadowResources() {
    if (shadowPipeline) {
        device->destroyPipeline(shadowPipeline);
        shadowPipeline = vk::Pipeline{};
    }
    if (shadowPipelineLayout) {
        device->destroyPipelineLayout(shadowPipelineLayout);
        shadowPipelineLayout = vk::PipelineLayout{};
    }
    for (int i = 0; i < ShadowConfig::kCascades; ++i) {
        if (shadowFramebuffers[i]) {
            device->destroyFramebuffer(shadowFramebuffers[i]);
            shadowFramebuffers[i] = vk::Framebuffer{};
        }
        if (shadowLayerViews[i]) {
            device->destroyImageView(shadowLayerViews[i]);
            shadowLayerViews[i] = vk::ImageView{};
        }
    }
    if (shadowArrayView) {
        device->destroyImageView(shadowArrayView);
        shadowArrayView = vk::ImageView{};
    }
    if (shadowSampler) {
        device->destroySampler(shadowSampler);
        shadowSampler = vk::Sampler{};
    }
    if (shadowImage) {
        device->destroyImage(shadowImage);
        shadowImage = vk::Image{};
    }
    if (shadowMemory) {
        device->freeMemory(shadowMemory);
        shadowMemory = vk::DeviceMemory{};
    }
    if (shadowRenderPass) {
        device->destroyRenderPass(shadowRenderPass);
        shadowRenderPass = vk::RenderPass{};
    }
    shadowPassCascade = -1;
    shadowPassDraws.clear();
}

void Graphics::createShadowResources() {
    if (shadowImage) return;

    const uint32_t size = uint32_t(ShadowConfig::kMapSize);
    const uint32_t layers = uint32_t(ShadowConfig::kCascades);

    vk::ImageCreateInfo imgInfo{};
    imgInfo.imageType = vk::ImageType::e2D;
    imgInfo.format = vk::Format::eD32Sfloat;
    imgInfo.extent = vk::Extent3D{size, size, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = layers;
    imgInfo.samples = vk::SampleCountFlagBits::e1;
    imgInfo.tiling = vk::ImageTiling::eOptimal;
    imgInfo.usage =
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    imgInfo.initialLayout = vk::ImageLayout::eUndefined;
    shadowImage = device->createImage(imgInfo);

    auto memReq = device->getImageMemoryRequirements(shadowImage);
    vk::MemoryAllocateInfo mai{};
    mai.allocationSize = memReq.size;
    mai.memoryTypeIndex = device.physical_device.findMemoryTypeIndex(
        memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    shadowMemory = device->allocateMemory(mai);
    device->bindImageMemory(shadowImage, shadowMemory, 0);

    vk::ImageViewCreateInfo arrayViewInfo{};
    arrayViewInfo.image = shadowImage;
    arrayViewInfo.viewType = vk::ImageViewType::e2DArray;
    arrayViewInfo.format = vk::Format::eD32Sfloat;
    arrayViewInfo.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, layers};
    shadowArrayView = device->createImageView(arrayViewInfo);

    for (uint32_t i = 0; i < layers; ++i) {
        vk::ImageViewCreateInfo layerInfo{};
        layerInfo.image = shadowImage;
        layerInfo.viewType = vk::ImageViewType::e2D;
        layerInfo.format = vk::Format::eD32Sfloat;
        layerInfo.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, i, 1};
        shadowLayerViews[i] = device->createImageView(layerInfo);
    }

    vkb::SamplerBuilder sb;
    shadowSampler = sb.magFilter(vk::Filter::eLinear)
                        .minFilter(vk::Filter::eLinear)
                        .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                        .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                        .addressModeW(vk::SamplerAddressMode::eClampToEdge)
                        .build(device.instance);

    vk::AttachmentDescription depthAtt{};
    depthAtt.format = vk::Format::eD32Sfloat;
    depthAtt.samples = vk::SampleCountFlagBits::e1;
    depthAtt.loadOp = vk::AttachmentLoadOp::eClear;
    depthAtt.storeOp = vk::AttachmentStoreOp::eStore;
    depthAtt.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAtt.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAtt.initialLayout = vk::ImageLayout::eUndefined;
    depthAtt.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::AttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass{};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.pDepthStencilAttachment = &depthRef;

    vk::SubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    deps[0].dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    deps[0].srcAccessMask = vk::AccessFlagBits::eShaderRead;
    deps[0].dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = vk::PipelineStageFlagBits::eLateFragmentTests;
    deps[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
    deps[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    deps[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

    vk::RenderPassCreateInfo rpInfo{};
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &depthAtt;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 2;
    rpInfo.pDependencies = deps;
    shadowRenderPass = device->createRenderPass(rpInfo);

    for (uint32_t i = 0; i < layers; ++i) {
        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = shadowRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &shadowLayerViews[i];
        fbInfo.width = size;
        fbInfo.height = size;
        fbInfo.layers = 1;
        shadowFramebuffers[i] = device->createFramebuffer(fbInfo);
    }

    vk::PushConstantRange pcr{};
    pcr.stageFlags = vk::ShaderStageFlagBits::eVertex;
    pcr.offset = 0;
    pcr.size = sizeof(glm::mat4);
    vk::PipelineLayoutCreateInfo plInfo{};
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pcr;
    shadowPipelineLayout = device->createPipelineLayout(plInfo);

    std::vector<uint32_t> vert(mesh3d_shadow_vert_spv,
                               mesh3d_shadow_vert_spv + mesh3d_shadow_vert_spv_count);
    std::vector<uint32_t> frag(mesh3d_shadow_frag_spv,
                               mesh3d_shadow_frag_spv + mesh3d_shadow_frag_spv_count);
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
    rs.cullMode = vk::CullModeFlagBits::eFront;  // reduce shadow acne
    rs.frontFace = vk::FrontFace::eCounterClockwise;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = VK_TRUE;
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor = 1.75f;

    vk::PipelineMultisampleStateCreateInfo ms{};
    ms.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineDepthStencilStateCreateInfo ds{};
    ds.depthTestEnable = true;
    ds.depthWriteEnable = true;
    ds.depthCompareOp = vk::CompareOp::eLess;

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.attachmentCount = 0;

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
    pci.layout = shadowPipelineLayout;
    pci.renderPass = shadowRenderPass;
    pci.subpass = 0;
    shadowPipeline = device->createGraphicsPipeline(vk::PipelineCache{}, pci).value;

    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);

    // Clear all cascade layers so the array is in SHADER_READ_ONLY before first sample.
    for (int c = 0; c < ShadowConfig::kCascades; ++c) {
        beginShadowPass(c);
        endShadowPass();
    }
}

void Graphics::ensureClusteredBuffers(size_t lightsBytes, size_t tableBytes, size_t indicesBytes) {
    auto ensure = [&](vkb::GenericBuffer &buf, size_t &cap, size_t need) {
        if (need == 0) need = 4;
        if (cap >= need && buf.buffer) return;
        if (buf.buffer) {
            device->destroyBuffer(buf.buffer, device.allocation_callbacks);
            device->freeMemory(buf.memory, device.allocation_callbacks);
            buf.buffer = vk::Buffer{};
            buf.memory = vk::DeviceMemory{};
        }
        // Grow with some slack.
        size_t alloc = std::max(need, cap ? cap * 2 : need);
        buf.allocate(device, vk::BufferUsageFlagBits::eStorageBuffer, vk::DeviceSize(alloc),
                     vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent);
        cap = alloc;
        // Descriptor sets cache buffer handles — clear so they rebind.
        mesh3dClusteredUboSlots.clear();
        mesh3dClusteredDrawIndex = 0;
    };
    ensure(clusteredLightsBuf, clusteredLightsCap, lightsBytes);
    ensure(clusteredTableBuf, clusteredTableCap, tableBytes);
    ensure(clusteredIndicesBuf, clusteredIndicesCap, indicesBytes);
}

void Graphics::uploadClusteredLighting(const ClusteredLightingUpload &upload) {
    const size_t lightsBytes =
        std::max(size_t(1), upload.lights.size()) * sizeof(ClusteredLightGpu);
    const size_t tableBytes =
        std::max(size_t(1), upload.clusterTable.size()) * sizeof(ClusterTableEntry);
    const size_t indicesBytes =
        std::max(size_t(1), upload.lightIndices.size()) * sizeof(uint32_t);
    ensureClusteredBuffers(lightsBytes, tableBytes, indicesBytes);

    if (!upload.lights.empty())
        clusteredLightsBuf.updateLocal(upload.lights.data(),
                                       upload.lights.size() * sizeof(ClusteredLightGpu));
    else {
        ClusteredLightGpu zero{};
        clusteredLightsBuf.updateLocal(&zero, sizeof(zero));
    }
    clusteredTableBuf.updateLocal(upload.clusterTable.data(),
                                  upload.clusterTable.size() * sizeof(ClusterTableEntry));
    clusteredIndicesBuf.updateLocal(upload.lightIndices.data(),
                                    upload.lightIndices.size() * sizeof(uint32_t));
}

void Graphics::setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) {
    mesh3dClusteredActive = upload.active;
    mesh3dClustered = upload;
    if (upload.active) uploadClusteredLighting(upload);
}

vk::DescriptorSet Graphics::mesh3dClusteredSetFor(GpuTexture *gpuTex, GpuTexture *normalTex,
                                                  GpuTexture *envTex, size_t uboSlot) {
    ASSERT(gpuTex != nullptr);
    ASSERT(normalTex != nullptr);
    ASSERT(envTex != nullptr);
    ASSERT(shadowArrayView);
    while (mesh3dClusteredUboSlots.size() <= uboSlot) {
        mesh3dClusteredUboSlots.emplace_back();
        auto &s = mesh3dClusteredUboSlots.back();
        s.ubo.allocate(device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(Mesh3DClusteredUBO),
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
        s.shadowUbo.allocate(device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(ShadowUBO),
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    auto &slot = mesh3dClusteredUboSlots[uboSlot];
    Mesh3dSetKey key{gpuTex, normalTex, envTex};
    auto it = slot.sets.find(key);
    if (it != slot.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &mesh3dClusteredSetLayout;
    vk::DescriptorSet set = device->allocateDescriptorSets(alloc).front();

    vkb::DescriptorSetUpdater updater(12, 12, 0);
    updater.beginDescriptorSet(set)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.ubo.buffer, 0, sizeof(Mesh3DClusteredUBO))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(gpuTex->sampler, gpuTex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginImages(2, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(normalTex->sampler, normalTex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginImages(3, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(envTex->sampler, envTex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginBuffers(4, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(clusteredLightsBuf.buffer, 0, vk::DeviceSize(clusteredLightsCap))
        .beginBuffers(5, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(clusteredTableBuf.buffer, 0, vk::DeviceSize(clusteredTableCap))
        .beginBuffers(6, 0, vk::DescriptorType::eStorageBuffer)
        .buffer(clusteredIndicesBuf.buffer, 0, vk::DeviceSize(clusteredIndicesCap))
        .beginBuffers(7, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.shadowUbo.buffer, 0, sizeof(ShadowUBO))
        .beginImages(8, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(shadowSampler, shadowArrayView, vk::ImageLayout::eShaderReadOnlyOptimal)
        .update(device.instance);

    slot.sets.emplace(key, set);
    return set;
}

vk::Pipeline Graphics::createMesh3DStylePipeline(const std::vector<uint32_t> &vert,
                                                 const std::vector<uint32_t> &frag,
                                                 vk::PipelineLayout layout) {
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
    pci.layout = layout;
    pci.renderPass = renderpass;
    pci.subpass = 0;

    auto result = device->createGraphicsPipeline(nullptr, pci);
    device->destroyShaderModule(vertModule);
    device->destroyShaderModule(fragModule);
    if (result.result != vk::Result::eSuccess)
        throw Exception("failed to create mesh3d-style pipeline");
    return result.value;
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
    offscreenTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout);

    if (lit2dPipelineLayout) {
        std::vector<uint32_t> lvert(lit2d_vert_spv, lit2d_vert_spv + lit2d_vert_spv_count);
        std::vector<uint32_t> lfrag(lit2d_frag_spv, lit2d_frag_spv + lit2d_frag_spv_count);
        offscreenLitPipeline =
            createTexturedStylePipeline(lvert, lfrag, offscreenRenderPass, lit2dPipelineLayout);
    }

    // Lazily create offscreen pipelines for any custom shaders already loaded.
    for (auto &sh : ownedShaders) ensureShaderOffscreenPipeline(sh.get());
}

void Graphics::ensureShaderOffscreenPipeline(Shader *shader) {
    if (!shader || !shader->gpuHandle || !offscreenRenderPass) return;
    auto *gpu = static_cast<GpuShader *>(shader->gpuHandle);
    if (gpu->offscreenPipeline) return;
    gpu->offscreenPipeline = createTexturedStylePipeline(shader->vertexSpirv(), shader->fragmentSpirv(),
                                                         offscreenRenderPass, shaderPipelineLayout);
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
    // FB row 0 is logical top (Batcher maps y=0 → Vulkan NDC -1). No Y flip.
    for (int y = 0; y < pixelHeight; ++y) {
        const uint8_t *srcRow = src + size_t(y) * rowBytes;
        uint8_t *dstRow = lastFrameRgba.data() + size_t(y) * rowBytes;
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
    litBatches.clear();
    if (auto *oc = dynamic_cast<OffscreenCanvas *>(activeCanvas)) {
        oc->clear(clearColor, std::nullopt, std::nullopt);
    }
}

void Graphics::drawSolidRect(float x, float y, float w, float h, const Color &color) {
    solidBatch.addRect(x, y, w, h, color);
}

Texture *Graphics::newTexture(int w, int h, const uint8_t *rgba, bool repeatU, bool repeatV) {
    ASSERT(initialized);
    ASSERT_GT(w, 0);
    ASSERT_GT(h, 0);
    ASSERT(rgba != nullptr);
    if (!initialized) throw Exception("newTexture: graphics not initialized");
    if (w <= 0 || h <= 0 || !rgba) throw Exception("newTexture: invalid args");

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->isCube = false;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h));
    std::vector<uint8_t> bytes(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    auto wrapMode = [](bool repeat) {
        return repeat ? vk::SamplerAddressMode::eRepeat : vk::SamplerAddressMode::eClampToEdge;
    };
    vkb::SamplerBuilder sb;
    gpu->sampler = sb.magFilter(vk::Filter::eLinear)
                       .minFilter(vk::Filter::eLinear)
                       .addressModeU(wrapMode(repeatU))
                       .addressModeV(wrapMode(repeatV))
                       .build(device.instance);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);
    gpu->descriptorSet = sets[0];

    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(gpu->descriptorSet)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(gpu->sampler, gpu->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
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

Texture *Graphics::newCubemap(int faceSize, const uint8_t *rgbaFaces) {
    ASSERT(initialized);
    ASSERT_GT(faceSize, 0);
    ASSERT(rgbaFaces != nullptr);
    if (!initialized) throw Exception("newCubemap: graphics not initialized");
    if (faceSize <= 0 || !rgbaFaces) throw Exception("newCubemap: invalid args");

    const size_t faceBytes = size_t(faceSize) * size_t(faceSize) * 4u;
    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = faceSize;
    gpu->height = faceSize;
    gpu->isCube = true;
    gpu->cubeImage = vkb::TextureImageCube(device, device.physical_device.memory_properties,
                                           uint32_t(faceSize), uint32_t(faceSize));
    std::vector<uint8_t> bytes(rgbaFaces, rgbaFaces + faceBytes * 6u);
    gpu->cubeImage.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    vkb::SamplerBuilder sb;
    gpu->sampler = sb.magFilter(vk::Filter::eLinear)
                       .minFilter(vk::Filter::eLinear)
                       .mipmapMode(vk::SamplerMipmapMode::eLinear)
                       .addressModeU(vk::SamplerAddressMode::eClampToEdge)
                       .addressModeV(vk::SamplerAddressMode::eClampToEdge)
                       .addressModeW(vk::SamplerAddressMode::eClampToEdge)
                       .minLod(0.f)
                       .maxLod(1.f)
                       .build(device.instance);

    auto tex = std::make_unique<Texture>();
    tex->width = faceSize;
    tex->height = faceSize;
    tex->pixelWidth = faceSize;
    tex->pixelHeight = faceSize;
    tex->layers = 6;
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

namespace {

std::string normalizeTexPath(std::string path) {
    for (char &c : path) {
        if (c == '\\') c = '/';
    }
    while (path.size() >= 2 && path[0] == '.' && path[1] == '/') path.erase(0, 2);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

}  // namespace

bool Graphics::replaceTexturePixels(Texture *tex, image::ImageData *data) {
    if (!tex || !data) return false;
    if (data->getFormat() != "RGBA8") return false;
    const int w = data->getWidth();
    const int h = data->getHeight();
    const auto *rgba = static_cast<const uint8_t *>(data->getData());
    if (w <= 0 || h <= 0 || !rgba) return false;

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = w;
    gpu->height = h;
    gpu->image = vkb::TextureImage2D(device, uint32_t(w), uint32_t(h));
    std::vector<uint8_t> bytes(rgba, rgba + size_t(w) * size_t(h) * 4);
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    vkb::SamplerBuilder sb;
    gpu->sampler = sb.magFilter(vk::Filter::eLinear)
                       .minFilter(vk::Filter::eLinear)
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

    void *oldHandle = tex->gpuHandle;
    for (auto &owned : ownedGpuTextures) {
        if (owned.get() != oldHandle) continue;
        if (owned->sampler) device->destroySampler(owned->sampler);
        owned = std::move(gpu);
        tex->gpuHandle = owned.get();
        tex->width = w;
        tex->height = h;
        tex->pixelWidth = w;
        tex->pixelHeight = h;
        return true;
    }

    // Texture not in owned list — attach as new ownership.
    tex->gpuHandle = gpu.get();
    tex->width = w;
    tex->height = h;
    tex->pixelWidth = w;
    tex->pixelHeight = h;
    ownedGpuTextures.push_back(std::move(gpu));
    return true;
}

Texture *Graphics::newTextureFromFile(const std::string &filename) {
    ASSERT(!filename.empty());
    if (filename.empty()) throw Exception("newTextureFromFile: empty filename");

    const std::string key = normalizeTexPath(filename);
    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fileData(fs->read(filename));
    if (!fileData) throw Exception("newTextureFromFile: failed to read '%s'", filename.c_str());

    auto *imgMod = image::Image::create();
    std::unique_ptr<image::ImageData> data(imgMod->newImageData(fileData.get()));

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

    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fileData;
    try {
        fileData.reset(fs->read(filename));
    } catch (...) {
        return false;
    }
    if (!fileData) return false;

    auto *imgMod = image::Image::create();
    std::unique_ptr<image::ImageData> data(imgMod->newImageData(fileData.get()));
    return replaceTexturePixels(it->second, data.get());
}

void Graphics::drawTexturedRect(Texture *texture, float x, float y, float w, float h, const Color &color) {
    drawTexturedRectUV(texture, x, y, w, h, 0.f, 0.f, 1.f, 1.f, color);
}

void Graphics::drawTexturedRectShader(Texture *texture, Shader *shader, float x, float y, float w,
                                      float h, const Color &color) {
    drawTexturedRectShaderUV(texture, shader, x, y, w, h, 0.f, 0.f, 1.f, 1.f, color);
}

void Graphics::drawTexturedRectUV(Texture *texture, float x, float y, float w, float h, float u0,
                                  float v0, float u1, float v1, const Color &color) {
    drawTexturedRectShaderUV(texture, currentShader, x, y, w, h, u0, v0, u1, v1, color);
}

void Graphics::drawTexturedRectShaderUV(Texture *texture, Shader *shader, float x, float y, float w,
                                        float h, float u0, float v0, float u1, float v1,
                                        const Color &color) {
    if (!texture) {
        drawSolidRect(x, y, w, h, color);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().shader != shader) {
        texturedBatches.push_back(TexturedBatch{texture, shader, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1);
}

void Graphics::setLighting2D(const Lighting2DUBO &ubo) { lighting2dFrame = ubo; }

void Graphics::ensureFlatNormalTexture() {
    if (flatNormalTexture) return;
    const uint8_t px[4] = {128, 128, 255, 255};  // flat normal pointing +Z
    flatNormalTexture = newTexture(1, 1, px);
}

void Graphics::drawTexturedRectLitUV(Texture *albedo, Texture *normal, float x, float y, float w,
                                     float h, float u0, float v0, float u1, float v1,
                                     const Color &color) {
    if (!albedo) {
        drawSolidRect(x, y, w, h, color);
        return;
    }
    ensureFlatNormalTexture();
    if (!normal) normal = flatNormalTexture;
    if (litBatches.empty() || litBatches.back().albedo != albedo ||
        litBatches.back().normal != normal) {
        litBatches.push_back(LitBatch{albedo, normal, Batcher{}});
    }
    litBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1);
}

vk::DescriptorSet Graphics::lit2dSetFor(GpuTexture *albedo, GpuTexture *normal) {
    ASSERT(albedo != nullptr);
    ASSERT(normal != nullptr);
    LitSetKey key{albedo, normal};
    auto it = lit2dSets.find(key);
    if (it != lit2dSets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &lit2dSetLayout;
    vk::DescriptorSet set = device->allocateDescriptorSets(alloc).front();

    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(set)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(albedo->sampler, albedo->image.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(normal->sampler, normal->image.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginBuffers(2, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(lighting2dUbo.buffer, 0, sizeof(Lighting2DUBO))
        .update(device.instance);

    lit2dSets.emplace(key, set);
    return set;
}

void Graphics::drawLitBatches(vk::CommandBuffer cb, int viewW, int viewH, vk::Pipeline pipeline,
                              std::vector<LitBatch> &batches,
                              std::vector<vkb::HostVertexBuffer> &texBufs) {
    if (!pipeline || batches.empty() || !lit2dPipelineLayout) return;
    lighting2dFrame.meta.y = float(viewW);
    lighting2dFrame.meta.z = float(viewH);
    lighting2dUbo.updateLocal(&lighting2dFrame, sizeof(Lighting2DUBO));

    for (auto &lb : batches) {
        if (lb.batch.empty() || !lb.albedo || !lb.albedo->gpuHandle) continue;
        ensureFlatNormalTexture();
        Texture *ntex = lb.normal ? lb.normal : flatNormalTexture;
        if (!ntex || !ntex->gpuHandle) continue;
        auto *albedoGpu = static_cast<GpuTexture *>(lb.albedo->gpuHandle);
        auto *normalGpu = static_cast<GpuTexture *>(ntex->gpuHandle);

        Batcher ndc = lb.batch;
        ndc.toNDC(viewW, viewH);
        std::vector<TexturedVertex> gpuVerts;
        gpuVerts.reserve(ndc.vertices().size());
        for (const auto &v : ndc.vertices())
            gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});
        texBufs.emplace_back();
        texBufs.back().allocate<TexturedVertex>(device, gpuVerts);

        vk::DescriptorSet set = lit2dSetFor(albedoGpu, normalGpu);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, lit2dPipelineLayout, 0, 1, &set, 0,
                              nullptr);
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, texBufs.back(), &offset);
        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
    }
}

namespace {

std::vector<uint32_t> loadSpirvBytes(const void *data, size_t size) {
    if (!data || size < 4 || (size % 4) != 0)
        throw Exception("SPIR-V: invalid size %zu", size);
    const auto *words = static_cast<const uint32_t *>(data);
    if (words[0] != 0x07230203)
        throw Exception("SPIR-V: bad magic (expected 0x07230203)");
    return std::vector<uint32_t>(words, words + size / 4);
}

std::vector<uint32_t> readSpirvFile(const std::string &path) {
    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fd(fs->read(path));
    if (!fd) throw Exception("newShaderFromSpvFile: failed to read '%s'", path.c_str());
    return loadSpirvBytes(fd->getData(), fd->getSize());
}

std::vector<uint32_t> compileGlslWithGlslc(const std::string &source, const char *stage) {
    if (source.empty()) throw Exception("newShader: empty %s GLSL", stage);
#if defined(_WIN32)
    (void)source;
    (void)stage;
    throw Exception("newShader: GLSL compile via glslc is not supported on Windows; "
                    "use newShaderFromSpv / newShaderFromSpvFile");
#else
    char inPath[] = "/tmp/eve_shader_XXXXXX";
    int fd = mkstemp(inPath);
    if (fd < 0) throw Exception("newShader: mkstemp failed");
    std::string outPath = std::string(inPath) + ".spv";
    {
        ssize_t n = write(fd, source.data(), source.size());
        close(fd);
        if (n < 0 || size_t(n) != source.size()) {
            unlink(inPath);
            throw Exception("newShader: failed to write temp GLSL");
        }
    }

    std::string cmd = std::string("glslc -fshader-stage=") + stage + " \"" + inPath + "\" -o \"" +
                      outPath + "\" 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    std::string err;
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) err += buf;
        int status = pclose(pipe);
        unlink(inPath);
        if (status != 0) {
            unlink(outPath.c_str());
            throw Exception("newShader: glslc failed for %s:\n%s", stage, err.c_str());
        }
    } else {
        unlink(inPath);
        throw Exception("newShader: glslc not available (popen failed)");
    }

    FILE *f = fopen(outPath.c_str(), "rb");
    if (!f) {
        unlink(outPath.c_str());
        throw Exception("newShader: failed to open compiled SPIR-V");
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz > 0 ? sz : 0));
    if (sz > 0 && fread(bytes.data(), 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
        fclose(f);
        unlink(outPath.c_str());
        throw Exception("newShader: failed to read compiled SPIR-V");
    }
    fclose(f);
    unlink(outPath.c_str());
    return loadSpirvBytes(bytes.data(), bytes.size());
#endif
}

}  // namespace

Shader *Graphics::newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                   const std::vector<uint32_t> &fragSpv) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newShaderFromSpv: graphics not initialized");
    if (fragSpv.empty()) throw Exception("newShaderFromSpv: empty fragment SPIR-V");

    std::vector<uint32_t> vert = vertSpv;
    if (vert.empty())
        vert.assign(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
    if (vert[0] != 0x07230203 || fragSpv[0] != 0x07230203)
        throw Exception("newShaderFromSpv: SPIR-V magic mismatch");

    auto gpu = std::make_unique<GpuShader>();
    gpu->pipelineLayout = shaderPipelineLayout;
    gpu->swapchainPipeline =
        createTexturedStylePipeline(vert, fragSpv, renderpass, shaderPipelineLayout);
    if (offscreenRenderPass) {
        gpu->offscreenPipeline =
            createTexturedStylePipeline(vert, fragSpv, offscreenRenderPass, shaderPipelineLayout);
    }

    auto sh = std::make_unique<Shader>();
    sh->setSpirv(std::move(vert), fragSpv);
    sh->gpuHandle = gpu.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

Shader *Graphics::newShaderFromSpvFile(const std::string &vertPath, const std::string &fragPath) {
    if (fragPath.empty()) throw Exception("newShaderFromSpvFile: empty fragPath");
    std::vector<uint32_t> vert;
    if (!vertPath.empty()) vert = readSpirvFile(vertPath);
    auto frag = readSpirvFile(fragPath);
    return newShaderFromSpv(vert, frag);
}

Shader *Graphics::newShader(const std::string &vertGlsl, const std::string &fragGlsl) {
    if (fragGlsl.empty()) throw Exception("newShader: empty fragment GLSL");
    std::vector<uint32_t> vert;
    if (!vertGlsl.empty()) vert = compileGlslWithGlslc(vertGlsl, "vert");
    auto frag = compileGlslWithGlslc(fragGlsl, "frag");
    return newShaderFromSpv(vert, frag);
}

Shader *Graphics::newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                       const std::vector<uint32_t> &fragSpv) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshShaderFromSpv: graphics not initialized");
    if (fragSpv.empty()) throw Exception("newMeshShaderFromSpv: empty fragment SPIR-V");
    if (!mesh3dShaderPipelineLayout)
        throw Exception("newMeshShaderFromSpv: mesh3d pipeline layout missing");

    std::vector<uint32_t> vert = vertSpv;
    if (vert.empty())
        vert.assign(mesh3d_vert_spv, mesh3d_vert_spv + mesh3d_vert_spv_count);
    if (vert[0] != 0x07230203 || fragSpv[0] != 0x07230203)
        throw Exception("newMeshShaderFromSpv: SPIR-V magic mismatch");

    auto gpu = std::make_unique<GpuShader>();
    gpu->isMesh3D = true;
    gpu->pipelineLayout = mesh3dShaderPipelineLayout;
    gpu->mesh3dPipeline =
        createMesh3DStylePipeline(vert, fragSpv, mesh3dShaderPipelineLayout);

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eMesh3D);
    sh->setSpirv(std::move(vert), fragSpv);
    sh->gpuHandle = gpu.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

Shader *Graphics::newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) {
    if (fragGlsl.empty()) throw Exception("newMeshShader: empty fragment GLSL");
    std::vector<uint32_t> vert;
    if (!vertGlsl.empty()) vert = compileGlslWithGlslc(vertGlsl, "vert");
    auto frag = compileGlslWithGlslc(fragGlsl, "frag");
    return newMeshShaderFromSpv(vert, frag);
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
    auto lit = std::move(litBatches);
    solidBatch.clear();
    texturedBatches.clear();
    litBatches.clear();

    const Color cc = canvas->pendingClearColor();
    const bool needClear = canvas->takePendingClear();
    if (solid.empty() && textured.empty() && lit.empty() && !needClear) return;

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

                                        if (tb.shader && tb.shader->gpuHandle) {
                                            ensureShaderOffscreenPipeline(tb.shader);
                                            auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
                                            if (!gs->offscreenPipeline) continue;
                                            cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                            gs->offscreenPipeline);
                                            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                                  shaderPipelineLayout, 0, 1,
                                                                  &gpu->descriptorSet, 0, nullptr);
                                            cb.pushConstants(shaderPipelineLayout,
                                                             vk::ShaderStageFlagBits::eVertex |
                                                                 vk::ShaderStageFlagBits::eFragment,
                                                             0, Shader::kPushConstantBytes,
                                                             tb.shader->pushConstantData());
                                        } else {
                                            cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                            offscreenTexPipeline);
                                            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                                  texPipelineLayout, 0, 1,
                                                                  &gpu->descriptorSet, 0, nullptr);
                                        }
                                        vk::DeviceSize offset = 0;
                                        cb.bindVertexBuffers(0, 1, texBufs.back(), &offset);
                                        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                    }
                                }

                                if (offscreenLitPipeline)
                                    drawLitBatches(cb, canvas->getWidth(), canvas->getHeight(),
                                                   offscreenLitPipeline, lit, texBufs);

                                cb.endRenderPass();
                                canvas->colorImage().setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
                            });
}

void Graphics::flushToSwapchain() {
    Batcher solid = solidBatch;
    auto textured = std::move(texturedBatches);
    auto lit = std::move(litBatches);
    solidBatch.clear();
    texturedBatches.clear();
    litBatches.clear();

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

            if (tb.shader && tb.shader->gpuHandle) {
                auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gs->swapchainPipeline);
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shaderPipelineLayout, 0, 1,
                                      &gpu->descriptorSet, 0, nullptr);
                cb.pushConstants(shaderPipelineLayout,
                                 vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                                 Shader::kPushConstantBytes, tb.shader->pushConstantData());
            } else {
                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, texPipeline);
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1,
                                      &gpu->descriptorSet, 0, nullptr);
            }
            vk::DeviceSize offset = 0;
            cb.bindVertexBuffers(0, 1, texBufs.back(), &offset);
            cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
        }
    }

    if (lit2dPipeline) drawLitBatches(cb, width, height, lit2dPipeline, lit, texBufs);

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
    // Soft-fail like flushToSwapchain: on Android/iOS the surface may still be
    // settling after orientation change; throwing would abort the whole script.
    if (!beginPresentCommandBuffer())
        return;

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
    mesh3dClusteredDrawIndex = 0;
    swapchainPassOpen = true;
    frameHad3D = true;
    hasPendingClear = false;
}

void Graphics::setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) {
    glm::vec3 d = glm::normalize(dir);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    mesh3dFrameUbo.lightDir = glm::vec4(d, mesh3dFrameUbo.lightDir.w);
    // Preserve .w (envIntensity) — filled per-draw in drawMeshShader.
    mesh3dFrameUbo.lightColor = glm::vec4(color, mesh3dFrameUbo.lightColor.w);
}

void Graphics::setMesh3DCameraPos(const glm::vec3 &eye) {
    mesh3dFrameUbo.cameraPos = glm::vec4(eye, mesh3dFrameUbo.cameraPos.w);
}

void Graphics::setMesh3DNormalTexture(Texture *normal) { mesh3dNormalTexture = normal; }

void Graphics::setMesh3DEnv(Texture *cube, float intensity) {
    mesh3dEnvTexture = cube;
    mesh3dEnvIntensity = intensity < 0.f ? 0.f : intensity;
    if (!cube) mesh3dEnvIntensity = 0.f;
}

void Graphics::setMesh3DShadows(const ShadowUpload &upload) { mesh3dShadows = upload; }

void Graphics::setMesh3DShadowReceive(bool receive) { mesh3dShadowReceive = receive; }

void Graphics::beginShadowPass(int cascadeIndex) {
    ASSERT(initialized);
    if (!shadowPipeline) createShadowResources();
    if (cascadeIndex < 0 || cascadeIndex >= ShadowConfig::kCascades) {
        throw Exception("beginShadowPass: cascadeIndex out of range");
    }
    shadowPassCascade = cascadeIndex;
    shadowPassDraws.clear();
}

void Graphics::drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) {
    if (shadowPassCascade < 0) throw Exception("drawMeshShadow: call beginShadowPass first");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMeshShadow: null mesh");
    shadowPassDraws.push_back(ShadowDraw{mesh, lightMVP});
}

void Graphics::endShadowPass() {
    if (shadowPassCascade < 0) throw Exception("endShadowPass: no active shadow pass");
    if (!shadowPipeline || !shadowRenderPass) {
        shadowPassCascade = -1;
        shadowPassDraws.clear();
        return;
    }
    const int cascade = shadowPassCascade;
    const auto draws = std::move(shadowPassDraws);
    shadowPassDraws.clear();
    shadowPassCascade = -1;

    const uint32_t size = uint32_t(ShadowConfig::kMapSize);
    vkb::executeImmediately(device.instance, uploadPool, device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                vk::ClearValue clear{};
                                clear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
                                vk::RenderPassBeginInfo rpBegin{};
                                rpBegin.renderPass = shadowRenderPass;
                                rpBegin.framebuffer = shadowFramebuffers[cascade];
                                rpBegin.renderArea = vk::Rect2D{{0, 0}, {size, size}};
                                rpBegin.clearValueCount = 1;
                                rpBegin.pClearValues = &clear;
                                cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

                                vk::Viewport vp{0.f, 0.f, float(size), float(size), 0.f, 1.f};
                                vk::Rect2D scissor{{0, 0}, {size, size}};
                                cb.setViewport(0, 1, &vp);
                                cb.setScissor(0, 1, &scissor);
                                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPipeline);

                                for (const auto &d : draws) {
                                    auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
                                    cb.pushConstants(shadowPipelineLayout, vk::ShaderStageFlagBits::eVertex,
                                                     0, sizeof(glm::mat4), &d.mvp);
                                    vk::DeviceSize offset = 0;
                                    cb.bindVertexBuffers(0, 1, gpuMesh->vertices, &offset);
                                    cb.bindIndexBuffer(gpuMesh->indices.buffer, 0, vk::IndexType::eUint32);
                                    cb.drawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
                                }
                                cb.endRenderPass();
                            });
}

void Graphics::ensureDefaultEnvCubemap() {
    if (defaultEnvCubemap) return;
    const uint8_t black[4] = {0, 0, 0, 255};
    std::vector<uint8_t> faces(6u * 4u);
    for (int i = 0; i < 6; ++i) {
        faces[size_t(i) * 4u + 0] = black[0];
        faces[size_t(i) * 4u + 1] = black[1];
        faces[size_t(i) * 4u + 2] = black[2];
        faces[size_t(i) * 4u + 3] = black[3];
    }
    defaultEnvCubemap = newCubemap(1, faces.data());
}

void Graphics::setMesh3DMaterial(float metallic, float roughness) {
    mesh3dMetallic = metallic;
    mesh3dRoughness = roughness;
    mesh3dFrameUbo.ambient.w = metallic;
    mesh3dFrameUbo.cameraPos.w = roughness;
}

void Graphics::setMesh3DLighting(const Lighting3DPack &pack) {
    mesh3dLighting = pack;
    mesh3dFrameUbo.ambient = glm::vec4(glm::vec3(pack.ambient), mesh3dMetallic);
    mesh3dFrameUbo.lightDir.w = float(pack.count);
    if (pack.count > 0) {
        mesh3dFrameUbo.lightDir =
            glm::vec4(glm::vec3(pack.lights[0].posRadius), float(pack.count));
        mesh3dFrameUbo.lightColor = glm::vec4(glm::vec3(pack.lights[0].color), 1.f);
    }
}

void Graphics::ensureFlatNormalTexture3D() {
    if (flatNormalTexture3D) return;
    const uint8_t px[4] = {128, 128, 255, 255};
    flatNormalTexture3D = newTexture(1, 1, px);
}

vk::DescriptorSet Graphics::mesh3dSetFor(GpuTexture *gpuTex, GpuTexture *normalTex, GpuTexture *envTex,
                                         size_t uboSlot) {
    ASSERT(gpuTex != nullptr);
    ASSERT(normalTex != nullptr);
    ASSERT(envTex != nullptr);
    ASSERT(shadowArrayView);
    while (mesh3dUboSlots.size() <= uboSlot) {
        mesh3dUboSlots.emplace_back();
        auto &s = mesh3dUboSlots.back();
        s.ubo.allocate(device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(Mesh3DUBO),
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
        s.shadowUbo.allocate(device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(ShadowUBO),
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    auto &slot = mesh3dUboSlots[uboSlot];
    Mesh3dSetKey key{gpuTex, normalTex, envTex};
    auto it = slot.sets.find(key);
    if (it != slot.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &mesh3dSetLayout;
    vk::DescriptorSet set = device->allocateDescriptorSets(alloc).front();

    vkb::DescriptorSetUpdater updater(10, 10, 0);
    updater.beginDescriptorSet(set)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.ubo.buffer, 0, sizeof(Mesh3DUBO))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(gpuTex->sampler, gpuTex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginImages(2, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(normalTex->sampler, normalTex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginImages(3, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(envTex->sampler, envTex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal)
        .beginBuffers(4, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.shadowUbo.buffer, 0, sizeof(ShadowUBO))
        .beginImages(5, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(shadowSampler, shadowArrayView, vk::ImageLayout::eShaderReadOnlyOptimal)
        .update(device.instance);

    slot.sets.emplace(key, set);
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

Mesh *Graphics::newMeshSphere(int slices, int stacks) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshSphere: graphics not initialized");
    if (slices < 3) slices = 3;
    if (stacks < 2) stacks = 2;
    if (slices > 256) slices = 256;
    if (stacks > 128) stacks = 128;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.f;

    // Layout (stride = slices+1, seam column duplicated for continuous U):
    //   row 0:          north pole verts (same pos, unique U)
    //   row 1..stacks-1: latitude rings
    //   row stacks:     south pole verts
    // One pole vertex per longitude avoids a single-fan UV singularity and
    // keeps every triangle non-degenerate.
    const int stride = slices + 1;
    const int rows = stacks + 1;  // includes both pole rows
    std::vector<MeshVertex> verts;
    verts.reserve(size_t(stride) * size_t(rows));

    auto pushVert = [&](float px, float py, float pz, float u, float v) {
        MeshVertex vert{};
        vert.pos = {px, py, pz};
        vert.normal = {px, py, pz};  // unit sphere
        vert.uv = {u, v};
        verts.push_back(vert);
    };

    for (int y = 0; y <= stacks; ++y) {
        const float fv = float(y) / float(stacks);
        const float phi = fv * kPi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            // Exact poles: collapse ring to a point but keep per-slice UVs.
            if (y == 0)
                pushVert(0.f, 1.f, 0.f, u, 0.f);
            else if (y == stacks)
                pushVert(0.f, -1.f, 0.f, u, 1.f);
            else
                pushVert(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta), u, fv);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(size_t(slices) * size_t(stacks) * 6u);

    // Quads between consecutive rows. Winding must be consistent for outward
    // faces (pipeline: frontFace CCW, cull back). Use the same winding that
    // closed the south pole in-game: rowA[x], rowB[x], rowA[x+1] /
    // rowA[x+1], rowB[x], rowB[x+1] — derived from ring→next with
    // (i0,i2,i1)+(i1,i2,i3) which equals (lon,lat)->(lon,lat+1)->(lon+1,lat).
    for (int y = 0; y < stacks; ++y) {
        const uint32_t row0 = uint32_t(y * stride);
        const uint32_t row1 = uint32_t((y + 1) * stride);
        for (int x = 0; x < slices; ++x) {
            const uint32_t i0 = row0 + uint32_t(x);
            const uint32_t i1 = row0 + uint32_t(x + 1);
            const uint32_t i2 = row1 + uint32_t(x);
            const uint32_t i3 = row1 + uint32_t(x + 1);
            // Outward for RH Y-up (verified against south-cap fix): i0,i2,i1 + i1,i2,i3
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

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

Mesh *Graphics::newMeshCylinder(int slices, int stacks, bool caps) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshCylinder: graphics not initialized");
    if (slices < 3) slices = 3;
    if (stacks < 1) stacks = 1;
    if (slices > 256) slices = 256;
    if (stacks > 128) stacks = 128;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.f;
    constexpr float kRadius = 1.f;
    constexpr float kHalfH = 1.f;  // height 2, Y from -1..1

    const int stride = slices + 1;  // duplicated seam for continuous U
    const int sideRows = stacks + 1;
    std::vector<MeshVertex> verts;
    verts.reserve(size_t(stride) * size_t(sideRows) + size_t(caps ? 2 * (slices + 2) : 0));

    auto pushVert = [&](float px, float py, float pz, float nx, float ny, float nz, float u,
                        float v) {
        MeshVertex vert{};
        vert.pos = {px, py, pz};
        vert.normal = {nx, ny, nz};
        vert.uv = {u, v};
        verts.push_back(vert);
    };

    // Side wall: outward normals in XZ.
    for (int y = 0; y <= stacks; ++y) {
        const float fv = float(y) / float(stacks);
        const float py = kHalfH - fv * (2.f * kHalfH);
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            const float cx = std::cos(theta);
            const float sz = std::sin(theta);
            pushVert(kRadius * cx, py, kRadius * sz, cx, 0.f, sz, u, fv);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(size_t(slices) * size_t(stacks) * 6u +
                    size_t(caps ? slices * 2 * 3 : 0));

    for (int y = 0; y < stacks; ++y) {
        const uint32_t row0 = uint32_t(y * stride);
        const uint32_t row1 = uint32_t((y + 1) * stride);
        for (int x = 0; x < slices; ++x) {
            const uint32_t i0 = row0 + uint32_t(x);
            const uint32_t i1 = row0 + uint32_t(x + 1);
            const uint32_t i2 = row1 + uint32_t(x);
            const uint32_t i3 = row1 + uint32_t(x + 1);
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    if (caps) {
        // Top cap (y = +1, normal +Y) — fan from center.
        const uint32_t topCenter = uint32_t(verts.size());
        pushVert(0.f, kHalfH, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f);
        const uint32_t topRing = uint32_t(verts.size());
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            const float cx = std::cos(theta);
            const float sz = std::sin(theta);
            pushVert(kRadius * cx, kHalfH, kRadius * sz, 0.f, 1.f, 0.f, 0.5f + 0.5f * cx,
                     0.5f + 0.5f * sz);
        }
        for (int x = 0; x < slices; ++x) {
            indices.push_back(topCenter);
            indices.push_back(topRing + uint32_t(x));
            indices.push_back(topRing + uint32_t(x + 1));
        }

        // Bottom cap (y = -1, normal -Y).
        const uint32_t botCenter = uint32_t(verts.size());
        pushVert(0.f, -kHalfH, 0.f, 0.f, -1.f, 0.f, 0.5f, 0.5f);
        const uint32_t botRing = uint32_t(verts.size());
        for (int x = 0; x <= slices; ++x) {
            const float u = float(x) / float(slices);
            const float theta = u * kTwoPi;
            const float cx = std::cos(theta);
            const float sz = std::sin(theta);
            pushVert(kRadius * cx, -kHalfH, kRadius * sz, 0.f, -1.f, 0.f, 0.5f + 0.5f * cx,
                     0.5f + 0.5f * sz);
        }
        for (int x = 0; x < slices; ++x) {
            // CW when viewed from below so outward (-Y) faces are CCW from outside.
            indices.push_back(botCenter);
            indices.push_back(botRing + uint32_t(x + 1));
            indices.push_back(botRing + uint32_t(x));
        }
    }

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
    drawMeshShader(mesh, model, texture, tint, nullptr);
}

void Graphics::drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                              Shader *shader) {
    ASSERT(initialized);
    ASSERT(mesh != nullptr);
    if (!initialized) throw Exception("drawMesh: graphics not initialized");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMesh: null mesh");
    if (!swapchainPassOpen) throw Exception("drawMesh: call begin3DFrame first");
    if (!mesh3dPipeline) throw Exception("drawMesh: mesh3d pipeline missing");

    if (shader) {
        if (shader->getKind() != Shader::Kind::eMesh3D)
            throw Exception("drawMesh: shader is not a Mesh3D shader (use newMeshShader*)");
        if (!shader->gpuHandle) throw Exception("drawMesh: shader has no GPU pipeline");
    }

    auto *gpuMesh = static_cast<GpuMesh *>(mesh->gpuHandle);
    Texture *tex = texture ? texture : whiteTexture;
    if (!tex || !tex->gpuHandle) throw Exception("drawMesh: missing texture");
    auto *gpuTex = static_cast<GpuTexture *>(tex->gpuHandle);

    ensureFlatNormalTexture3D();
    Texture *ntex = mesh3dNormalTexture ? mesh3dNormalTexture : flatNormalTexture3D;
    if (!ntex || !ntex->gpuHandle) throw Exception("drawMesh: missing normal texture");
    auto *gpuNormal = static_cast<GpuTexture *>(ntex->gpuHandle);

    ensureDefaultEnvCubemap();
    Texture *envTex = mesh3dEnvTexture ? mesh3dEnvTexture : defaultEnvCubemap;
    if (!envTex || !envTex->gpuHandle) throw Exception("drawMesh: missing env cubemap");
    auto *gpuEnv = static_cast<GpuTexture *>(envTex->gpuHandle);
    if (!gpuEnv->isCube) throw Exception("drawMesh: env texture is not a cubemap");
    const float envIntensity = (mesh3dEnvTexture && mesh3dEnvIntensity > 0.f) ? mesh3dEnvIntensity : 0.f;

    const bool useClustered = mesh3dClusteredActive && !shader && mesh3dClusteredPipeline;
    auto &cb = presentModel.getCurrentCommandBuffer();

    auto makeShadowUbo = [&]() {
        ShadowUBO s = mesh3dShadows.ubo;
        if (!mesh3dShadows.active) {
            s.bias.y = 0.f;
            s.splits.w = 0.f;
        }
        s.bias.z = mesh3dShadowReceive ? 1.f : 0.f;
        return s;
    };

    if (useClustered) {
        Mesh3DClusteredUBO ubo{};
        ubo.model = model;
        ubo.mvp = mesh3dFrameUbo.mvp * model;
        ubo.view = mesh3dClustered.view;
        ubo.lightDir = mesh3dClustered.primaryDir;
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dClustered.primaryColor), envIntensity);
        ubo.tint = glm::vec4(tint.r, tint.g, tint.b, tint.a);
        ubo.cameraPos = glm::vec4(glm::vec3(mesh3dFrameUbo.cameraPos), mesh3dRoughness);
        ubo.ambient = glm::vec4(glm::vec3(mesh3dClustered.ambient), mesh3dMetallic);
        ubo.gridInfo = mesh3dClustered.gridInfo;
        ubo.clipInfo = mesh3dClustered.clipInfo;

        const size_t slot = mesh3dClusteredDrawIndex++;
        vk::DescriptorSet set = mesh3dClusteredSetFor(gpuTex, gpuNormal, gpuEnv, slot);
        mesh3dClusteredUboSlots[slot].ubo.updateLocal(ubo);
        mesh3dClusteredUboSlots[slot].shadowUbo.updateLocal(makeShadowUbo());

        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dClusteredPipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dClusteredPipelineLayout, 0, 1,
                              &set, 0, nullptr);
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, gpuMesh->vertices, &offset);
        cb.bindIndexBuffer(gpuMesh->indices.buffer, 0, vk::IndexType::eUint32);
        cb.drawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
        return;
    }

    Mesh3DUBO ubo = mesh3dFrameUbo;
    ubo.model = model;
    ubo.mvp = mesh3dFrameUbo.mvp * model;
    ubo.tint = glm::vec4(tint.r, tint.g, tint.b, tint.a);
    ubo.ambient = glm::vec4(glm::vec3(mesh3dLighting.ambient), mesh3dMetallic);
    const int lightCount = std::max(0, std::min(mesh3dLighting.count, Lighting3DPack::kMaxLights));
    ubo.lightDir.w = float(lightCount);
    ubo.cameraPos.w = mesh3dRoughness;
    ubo.lightColor.w = envIntensity;
    for (int i = 0; i < lightCount; ++i) ubo.lights[i] = mesh3dLighting.lights[i];
    if (lightCount > 0) {
        ubo.lightDir = glm::vec4(glm::vec3(mesh3dLighting.lights[0].posRadius), float(lightCount));
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dLighting.lights[0].color), envIntensity);
    }

    const size_t slot = mesh3dDrawIndex++;
    vk::DescriptorSet set = mesh3dSetFor(gpuTex, gpuNormal, gpuEnv, slot);
    mesh3dUboSlots[slot].ubo.updateLocal(ubo);
    mesh3dUboSlots[slot].shadowUbo.updateLocal(makeShadowUbo());

    if (shader) {
        auto *gs = static_cast<GpuShader *>(shader->gpuHandle);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gs->mesh3dPipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dShaderPipelineLayout, 0, 1, &set,
                              0, nullptr);
        cb.pushConstants(mesh3dShaderPipelineLayout,
                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                         Shader::kPushConstantBytes, shader->pushConstantData());
    } else {
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dPipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dPipelineLayout, 0, 1, &set, 0,
                              nullptr);
    }
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
        litBatches.clear();
        hasPendingClear = false;
        return;
    }
    flushBatch();
}

void Graphics::draw(eve::graphics::Graphics *, const glm::mat4 &) const {}
void Graphics::draw(Canvas *, const glm::mat4 &) const {}

}  // namespace eve::graphics::vulkan
