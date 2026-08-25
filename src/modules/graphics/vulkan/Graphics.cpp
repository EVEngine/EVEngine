// Vulkan backend implementation — backend lifecycle and frame orchestration.
//
// Re-split from the merged dev single-TU Graphics.cpp (pure move;
// dev changes preserved). Shared helpers live in GraphicsInternal.h.

#define VKB_IMPL
#include "graphics/vulkan/Graphics.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Light.h"
#include "graphics/Outline.h"
#include "graphics/RenderControl.h"
#include "graphics/vulkan/Canvas.h"

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

#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {

// --- Backend lifecycle and frame orchestration --------------------------------

std::string Graphics::getBackendName() const { return "vulkan"; }

Graphics::~Graphics() {
    if (!initialized) return;
    device->waitIdle();
    // Pipeline objects hold raw Shader* owned by ownedShaders. Drop them
    // first so their destructors do not see freed shader pointers.
    pipelineAA_.reset();
    pipelineAO_.reset();
    pipelineGI_.reset();
    renderControl_.reset();
    destroyGpuParticleResources();
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
        if (g->mesh3dXrayPipeline) device->destroyPipeline(g->mesh3dXrayPipeline);
        // pipelineLayout is shared; do not destroy per-shader
    }
    ownedGpuShaders.clear();
    ownedShaders.clear();
    destroySwapchainResources();
    if (pipeline) device->destroyPipeline(pipeline);
    if (solidAlphaPipeline) device->destroyPipeline(solidAlphaPipeline);
    if (additiveSolidPipeline) device->destroyPipeline(additiveSolidPipeline);
    if (premultipliedSolidPipeline) device->destroyPipeline(premultipliedSolidPipeline);
    if (multiplySolidPipeline) device->destroyPipeline(multiplySolidPipeline);
    if (pipelineLayout) device->destroyPipelineLayout(pipelineLayout);
    if (texPipeline) device->destroyPipeline(texPipeline);
    if (additiveTexPipeline) device->destroyPipeline(additiveTexPipeline);
    if (premultipliedTexPipeline) device->destroyPipeline(premultipliedTexPipeline);
    if (multiplyTexPipeline) device->destroyPipeline(multiplyTexPipeline);
    if (opaqueTexPipeline) device->destroyPipeline(opaqueTexPipeline);
    if (particleDistortionPipeline) device->destroyPipeline(particleDistortionPipeline);
    if (texPipelineLayout) device->destroyPipelineLayout(texPipelineLayout);
    if (shaderPipelineLayout) device->destroyPipelineLayout(shaderPipelineLayout);
    if (mesh3dPipeline) device->destroyPipeline(mesh3dPipeline);
    if (mesh3dTransparentPipeline) device->destroyPipeline(mesh3dTransparentPipeline);
    for (auto pipeline : mesh3dSurfacePipelines)
        if (pipeline) device->destroyPipeline(pipeline);
    destroyOffscreen3DResources();
    if (mesh3dPipelineLayout) device->destroyPipelineLayout(mesh3dPipelineLayout);
    if (mesh3dShaderPipelineLayout) device->destroyPipelineLayout(mesh3dShaderPipelineLayout);
    mesh3dSetLayoutUnique.reset();
    mesh3dFrameSlots.clear();
    destroyVoxelRectResources();
    if (mesh3dClusteredPipeline) device->destroyPipeline(mesh3dClusteredPipeline);
    if (mesh3dClusteredPipelineLayout) device->destroyPipelineLayout(mesh3dClusteredPipelineLayout);
    mesh3dClusteredSetLayoutUnique.reset();
    mesh3dClusteredFrameSlots.clear();
    destroyShadowResources();
    destroyGBufferResources();
    if (skinPassPipelineLayout) device->destroyPipelineLayout(skinPassPipelineLayout);
    skinPassPipelineLayout = nullptr;
    skinPassSetLayoutUnique.reset();
    skinPassSetLayout = nullptr;
    destroyDecalResources();
    destroySceneColorResources();
    destroyUiColorResources();
    auto destroyBuf = [&](vkb::GenericBuffer &b) { b.release(); };
    for (auto &st : clusteredStorages) {
        destroyBuf(st.lightsBuf);
        destroyBuf(st.tableBuf);
        destroyBuf(st.indicesBuf);
    }
    clusteredStorages.clear();
    if (lit2dPipeline) device->destroyPipeline(lit2dPipeline);
    if (offscreenLitPipeline) device->destroyPipeline(offscreenLitPipeline);
    if (lit2dPipelineLayout) device->destroyPipelineLayout(lit2dPipelineLayout);
    lit2dSetLayoutUnique.reset();
    for (auto &m : lit2dSets) m.clear();
    lit2dSets.clear();
    offscreenLit2dSets.clear();
    destroyBuf(offscreenLighting2dUbo);
    if (offscreenSolidPipeline) device->destroyPipeline(offscreenSolidPipeline);
    if (offscreenSolidAlphaPipeline) device->destroyPipeline(offscreenSolidAlphaPipeline);
    if (offscreenAdditiveSolidPipeline) device->destroyPipeline(offscreenAdditiveSolidPipeline);
    if (offscreenPremultipliedSolidPipeline)
        device->destroyPipeline(offscreenPremultipliedSolidPipeline);
    if (offscreenMultiplySolidPipeline) device->destroyPipeline(offscreenMultiplySolidPipeline);
    if (offscreenTexPipeline) device->destroyPipeline(offscreenTexPipeline);
    if (offscreenAdditiveTexPipeline) device->destroyPipeline(offscreenAdditiveTexPipeline);
    if (offscreenPremultipliedTexPipeline)
        device->destroyPipeline(offscreenPremultipliedTexPipeline);
    if (offscreenMultiplyTexPipeline) device->destroyPipeline(offscreenMultiplyTexPipeline);
    if (offscreenOpaqueTexPipeline) device->destroyPipeline(offscreenOpaqueTexPipeline);
    if (offscreenRenderPass) device->destroyRenderPass(offscreenRenderPass);
    texSetLayoutUnique.reset();
    if (descriptorPool) device->destroyDescriptorPool(descriptorPool);
    if (uploadPool) device->destroyCommandPool(uploadPool);
    if (renderpass) device->destroyRenderPass(renderpass);
    if (surface) inst.instance.destroySurfaceKHR(surface);
}

void Graphics::createInstanceAndDevice(const std::vector<const char *> &extNames, void *nativeWindow,
                                       vk::SurfaceKHR *surfaceOut) {
#if defined(EVENGINE_MACOSX)
    // Build the instance through the SAME Vulkan loader SDL loaded (the one
    // bundled by the SDK). vk-bootstrap would otherwise dlopen its own copy
    // (vulkan.hpp DynamicLoader or dlsym in load order) and
    // SDL_Vulkan_CreateSurface then cannot resolve the surface functions on
    // the vkb-created instance (its cached vkGetInstanceProcAddr belongs to a
    // different loader -> "extensions are not enabled").
    if (PFN_vkGetInstanceProcAddr sdlGpa =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr())) {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(sdlGpa);
        vkb::InstanceBuilder::loaded = true;
    }
#endif
    vkb::InstanceBuilder builder;
    builder.require_api_version(1, 0);
#if !defined(EVENGINE_IOS)
    // Khronos validation on every draw / descriptor update is typically 5–20×
    // slower. Opt in with EVENGINE_VULKAN_VALIDATION=1 (any value except "0").
    const char *vkVal = std::getenv("EVENGINE_VULKAN_VALIDATION");
    const bool wantValidation = vkVal && vkVal[0] != '\0' && vkVal[0] != '0';
    if (wantValidation) {
        builder.request_validation_layers();
        builder.use_default_debug_messenger();
        std::printf("EVEngine: Vulkan validation layers enabled (EVENGINE_VULKAN_VALIDATION)\n");
    }
#endif
    for (auto *extName : extNames) builder.enable_extension(extName);
    // No window / no surface: create a truly headless instance so the device
    // selector does not demand a presentable queue family or a VkSurfaceKHR.
    if (nativeWindow == nullptr) builder.set_headless(true);
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
    {
        StartupStage stage("  vulkan: instance + surface");
        inst = builder.build();

        if (nativeWindow != nullptr && surfaceOut != nullptr) {
            auto *window = static_cast<SDL_Window *>(nativeWindow);
            VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
            if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(inst.instance), &rawSurface))
                throw Exception("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
            surface = rawSurface;
            *surfaceOut = rawSurface;
        }
    }

    {
        StartupStage stage("  vulkan: physical device + device");
        auto selector = inst.selectPhysicalDevice();
        selector.set_minimum_version(1, 0);
        if (surface) {
            selector.set_surface(surface);
        } else {
            // Headless: pick a device without requiring a presentable surface.
            // VKBuilder's instance.headless flag is not propagated from
            // InstanceBuilder, so defer_surface_initialization() is the
            // supported way to select a device with no VkSurfaceKHR.
            selector.defer_surface_initialization();
        }
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_IOS)
        selector.add_required_extension("VK_KHR_portability_subset");
#endif
        auto phys = selector.select();
        {
            const vk::PhysicalDeviceFeatures supported = phys->getFeatures();
            if (supported.samplerAnisotropy) {
                phys.features.samplerAnisotropy = VK_TRUE;
                maxSamplerAnisotropy = phys.properties.limits.maxSamplerAnisotropy;
                if (maxSamplerAnisotropy < 1.f) maxSamplerAnisotropy = 1.f;
            } else {
                maxSamplerAnisotropy = 1.f;
            }
            // Probe Vulkan 1.2 GPU-driven capabilities (bindless tables + GPU
            // cull chain). The vendored headers omit `computeShader` / draw
            // features from VkPhysicalDeviceFeatures; compute is core and
            // universal, so treat it as present.
            gpuDrivenCaps_ = GpuDrivenCaps{};
            vk::PhysicalDeviceVulkan12Features vk12{};
            vk::PhysicalDeviceFeatures2 features2{};
            vk12.sType = vk::StructureType::ePhysicalDeviceVulkan12Features;
            features2.sType = vk::StructureType::ePhysicalDeviceFeatures2;
            features2.pNext = &vk12;
            phys->getFeatures2(&features2);
            gpuDrivenCaps_.api12 = phys.properties.apiVersion >= VK_API_VERSION_1_2;
            gpuDrivenCaps_.computeShader = true;
            gpuDrivenCaps_.multiDrawIndirect = supported.multiDrawIndirect == VK_TRUE;
            gpuDrivenCaps_.shaderSampledImageArrayDynamicIndexing =
                supported.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
            gpuDrivenCaps_.drawIndirectCount = vk12.drawIndirectCount == VK_TRUE;
            gpuDrivenCaps_.descriptorIndexing = vk12.descriptorIndexing == VK_TRUE;
            gpuDrivenCaps_.samplerArrayCapacity =
                phys.properties.limits.maxPerStageDescriptorSamplers >= kMaxBindlessTextures;
            if (gpuDrivenCaps_.gpuDrivenAvailable()) {
                phys.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
                phys.features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
                phys.features.multiDrawIndirect = VK_TRUE;
            }
        }
        vkb::DeviceBuilder deviceBuilder = phys.createDevice();
        // Vulkan 1.2 feature: vkCmdDrawIndirectCount (VG cluster draws).
        vk::PhysicalDeviceVulkan12Features vk12Enable{};
        vk12Enable.sType = vk::StructureType::ePhysicalDeviceVulkan12Features;
        if (gpuDrivenCaps_.drawIndirectCount) vk12Enable.drawIndirectCount = VK_TRUE;
        deviceBuilder.add_pNext(&vk12Enable);
        device = deviceBuilder.build();
        maxSamplerAnisotropy = device.caps.maxSamplerAnisotropy;
    }
}

void Graphics::initHeadless(int width, int height) {
    StartupStage initStage("graphics: initHeadless (total)");
    if (initialized) {
        if (headless_) {
            // Re-entrant: test cases share the process-wide Graphics singleton.
            // Update the logical viewport and keep the existing device.
            setViewportSize(width, height, width, height);
            return;
        }
        throw Exception("Graphics::initHeadless: already initialized with a window");
    }
    if (width <= 0 || height <= 0) throw Exception("Graphics::initHeadless: invalid size");

    // No window, no surface extensions, no swapchain: a bare Vulkan device that
    // renders into offscreen canvases and reads pixels back on the CPU.
    createInstanceAndDevice({}, nullptr, nullptr);

    uploadPool = device.createCommandPool();
    setViewportSize(width, height, width, height);
    headless_ = true;

    {
        StartupStage stage("  vulkan: headless renderpass + pipelines");
        // Plain color+depth render pass mirroring the swapchain pass format so
        // the shared pipeline builders can reuse it; nothing is ever presented.
        vkb::RenderPassBuilder rpBuilder{device};
        renderpass =
            rpBuilder.addPresentAttachment(vk::Format::eB8G8R8A8Unorm, vk::AttachmentLoadOp::eClear)
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
        depthImage = vkb::DepthStencilImage{device, uint32_t(width), uint32_t(height), depthFormat};
        pipelineLayout = createPipelineLayout(device);
        createTexturedPipeline();
        createMesh3DPipeline();
        createMesh3DClusteredPipeline();
        createVoxelRectPipeline();
    }

    // Must be set before createShadowResources(): it clears the shadow cascade
    // layers by calling beginShadowPass()/endShadowPass(), and beginShadowPass()
    // asserts `initialized` is already true.
    initialized = true;
    {
        StartupStage stage("  vulkan: shadow resources (3x2048^2 maps + pipelines)");
        createShadowResources();
    }
    {
        StartupStage stage("  vulkan: white texture");
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        whiteTexture = newTexture(1, 1, whitePixel);
        const std::vector<uint8_t> cubePx(6 * 4, 255);
        defaultBindlessCube = newCubemap(1, cubePx.data());
    }
    {
        StartupStage stage("  vulkan: gpu-driven bindless set + pipeline");
        initGpuDrivenResources();
    }
}

void Graphics::initWithWindow(void *nativeWindow) {
    StartupStage initStage("graphics: initWithWindow (total)");
    if (initialized) {
        // Window module may destroy/recreate the SDL window (tests, setWindowSettings).
        // The Vulkan surface is tied to the native window. SDL can hand back the
        // same pointer for a freshly recreated window, so pointer identity alone
        // can't tell whether the surface is still valid: Window::close() drops it
        // via onNativeWindowDestroyed(), and a missing surface must be rebuilt too.
        if (sdlWindow == nativeWindow && surface) return;
        sdlWindow = nativeWindow;
        recreateSurfaceForResume();
        // Restore the initWithWindow contract (device + swapchain valid on
        // return): rebuild the swapchain synchronously. On desktop this runs
        // immediately; on Android/iOS isRenderSurfaceStable() defers it until
        // the native surface settles, leaving swapchainDirty set (same as before).
        rebuildSwapchainIfNeeded();
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

    createInstanceAndDevice(extNames, nativeWindow, &surface);

    uploadPool = device.createCommandPool();

    int pw = 0, ph = 0;
    SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
    int lw = 0, lh = 0;
    SDL_GetWindowSize(window, &lw, &lh);
    setViewportSize(lw, lh, pw, ph);

    {
        StartupStage stage("  vulkan: swapchain + solid pipelines");
        createSwapchainAndPipeline();
    }
    {
        StartupStage stage("  vulkan: textured/lit2d pipelines");
        createTexturedPipeline();
    }
    {
        StartupStage stage("  vulkan: mesh3d pipeline");
        createMesh3DPipeline();
    }
    {
        StartupStage stage("  vulkan: clustered pipeline");
        createMesh3DClusteredPipeline();
    }
    {
        StartupStage stage("  vulkan: voxel pipeline");
        createVoxelRectPipeline();
    }
    // Must be set before createShadowResources(): it clears the shadow cascade
    // layers by calling beginShadowPass()/endShadowPass(), and beginShadowPass()
    // asserts `initialized` is already true.
    initialized = true;
    {
        StartupStage stage("  vulkan: shadow resources (3x2048^2 maps + pipelines)");
        createShadowResources();
    }
    {
        StartupStage stage("  vulkan: white texture");
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        whiteTexture = newTexture(1, 1, whitePixel);
        const std::vector<uint8_t> cubePx(6 * 4, 255);
        defaultBindlessCube = newCubemap(1, cubePx.data());
    }
    {
        StartupStage stage("  vulkan: gpu-driven bindless set + pipeline");
        initGpuDrivenResources();
    }
}

void Graphics::onNativeWindowDestroyed() {
    if (!initialized) return;
    if (swapchainPassOpen || sceneColorPassOpen) abortOpen3DFrame();
    // Fire registered window-destroyed callbacks (UI backend tears down its
    // ImGui context here) before the device/surface resources are released.
    for (auto &cb : windowDestroyedCallbacks_) cb.first(cb.second);
    windowDestroyedCallbacks_.clear();
    clearPresentOverlay();

    device->waitIdle();
    destroySwapchainResources();
    swapchain.destroy();
    swapchain = vkb::Swapchain{};
    if (surface) {
        inst.instance.destroySurfaceKHR(surface);
        surface = VK_NULL_HANDLE;
    }
    sdlWindow = nullptr;
    swapchainDirty = true;
}

void Graphics::destroySwapchainResources() {
    presentRecording = {};
    swapchainPass = {};
    presentModel.destroy();
    presentModel = vkb::Present{};
    destroyGpuDrivenCullResources();
    depthImage = vkb::DepthStencilImage{};
    destroyReadbackResources();
    // Release reused per-frame vertex buffers. Callers hold a device-wide
    // waitIdle before this runs.
    for (auto &fb : frame2dBuffers) releaseFrame2dBuffers(fb);
    frame2dBuffers.clear();
    releaseFrame2dBuffers(offscreenBuffers);
    // Per-frame lit-2D UBOs are keyed to the in-flight slot count; release them
    // here (slot count may change across recreation).
    for (auto &ubo : lighting2dUboSlots) ubo.release();
    lighting2dUboSlots.clear();
    // Descriptor sets cached against the released UBO handles must be dropped;
    // they live in the shared descriptor pool, so only the cache is cleared.
    lit2dSets.clear();
}

uint32_t Graphics::frameSlotCount() const {
    uint32_t n = presentModel.frames_in_flight;
    if (n == 0) n = swapchain.image_count;
    return n ? n : 1u;
}

size_t Graphics::currentFrameSlot() const {
    const uint32_t n = frameSlotCount();
    size_t slot = presentRecording ? presentRecording.slot().index : swapchain.current_frame;
    return (n > 0 && slot >= n) ? 0 : slot;
}

vk::CommandBuffer &Graphics::currentPresentCb() {
    if (offscreen3DPassOpen && offscreen3DCB)
        return offscreen3DCB;
    if (swapchainPass)
        return swapchainPass.commandBuffer();
    return presentRecording.commandBuffer();
}

vkb::FrameSlot Graphics::frameToken() const {
    if (offscreen3DPassOpen) return vkb::FrameSlot{0};
    if (presentRecording) return presentRecording.slot();
    return vkb::FrameSlot::gpuIdle();
}

void Graphics::waitForSharedGpuResources() {
    presentModel.waitForAllFrames();
}

void Graphics::invalidateTextureBindings() {
    for (auto &frame : mesh3dFrameSlots)
        frame.sets.clear();
    for (auto &frame : mesh3dClusteredFrameSlots)
        frame.sets.clear();
    for (auto &m : lit2dSets) m.clear();
    offscreenLit2dSets.clear();
    post2Sets.clear();
    voxelRectSets.clear();
}

Graphics::Frame2DBuffers &Graphics::currentFrame2DBuffers() {
    return currentSlot(frame2dBuffers, frameSlotCount(), currentFrameSlot());
}

Graphics::Mesh3dFrameSlots &Graphics::currentMesh3dFrameSlots() {
    return currentSlot(mesh3dFrameSlots, frameSlotCount(), currentFrameSlot());
}

Graphics::Mesh3dClusteredFrameSlots &Graphics::currentMesh3dClusteredFrameSlots() {
    return currentSlot(mesh3dClusteredFrameSlots, frameSlotCount(), currentFrameSlot());
}

vkb::GenericBuffer &Graphics::currentLighting2dUbo() {
    auto &ubo = currentSlot(lighting2dUboSlots, frameSlotCount(), currentFrameSlot());
    // allocate() reuses the slot's buffer when it already fits (same usage/
    // memflags, fixed sizeof(Lighting2DUBO)), so this is a no-op after first use.
    ubo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(Lighting2DUBO),
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent);
    return ubo;
}

std::unordered_map<Graphics::LitSetKey, vkb::BoundSet, Graphics::LitSetKeyHash> &
Graphics::currentLit2dSets() {
    return currentSlot(lit2dSets, frameSlotCount(), currentFrameSlot());
}

Graphics::ClusteredStorage &Graphics::currentClusteredStorage() {
    return currentSlot(clusteredStorages, frameSlotCount(), currentFrameSlot());
}

Graphics::VoxelInstanceFrame &Graphics::currentVoxelInstanceFrame() {
    return currentSlot(voxelInstanceFrames, frameSlotCount(), currentFrameSlot());
}

Graphics::ShadowMapSlot &Graphics::currentShadowMap() {
    ASSERT(!shadowMaps.empty());
    return shadowMaps[currentFrameSlot() % shadowMaps.size()];
}

vk::ImageView Graphics::currentShadowArrayView() {
    if (shadowMaps.empty()) return vk::ImageView{};
    return currentShadowMap().image.arrayView();
}

Graphics::GBufferSlot *Graphics::currentGBufferSlot() {
    if (gbufferSlots.empty()) return nullptr;
    return &gbufferSlots[currentFrameSlot() % gbufferSlots.size()];
}

Texture* Graphics::getSceneLinearDepthTexture() {
    auto* slot = currentGBufferSlot();
    return slot && gbufferPending ? &slot->depthColorTex : nullptr;
}

Graphics::DecalSlot *Graphics::currentDecalSlot() {
    if (decalSlots.empty()) return nullptr;
    return &decalSlots[currentFrameSlot() % decalSlots.size()];
}

Graphics::SceneColorSlot *Graphics::currentSceneColorSlot() {
    if (sceneColorSlots.empty()) return nullptr;
    return &sceneColorSlots[currentFrameSlot() % sceneColorSlots.size()];
}

Texture *Graphics::getSceneColorTexture() {
    auto *slot = currentSceneColorSlot();
    if (!slot || !slot->colorTex.gpuHandle) return nullptr;
    return &slot->colorTex;
}

Graphics::UiColorSlot *Graphics::currentUiColorSlot() {
    if (uiColorSlots.empty()) return nullptr;
    return &uiColorSlots[currentFrameSlot() % uiColorSlots.size()];
}

void Graphics::dropPendingOffscreenPasses() {
    shadowPendingMask = 0;
    for (auto &d : shadowCascadeDraws) d.clear();
    gbufferPending = false;
    gbufferPassDraws.clear();
    decalPending = false;
    decalPassDraws.clear();
}

bool Graphics::beginSwapchainRenderPass() {
    if (!beginPresentCommandBuffer()) {
        dropPendingOffscreenPasses();
        return false;
    }
    recordDeferredFrameGraph();
    beginSwapchainColorPass();
    return true;
}

void Graphics::beginSwapchainColorPass() {
    std::array<vk::ClearValue, 2> clears{};
    clears[0].color = vk::ClearColorValue(
        std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, clearColor.a});
    clears[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    swapchainPass = presentRecording.beginRenderPass(renderpass, clears.data(), uint32_t(clears.size()));
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
        presentRecording = presentModel.begin();
        if (presentRecording) return true;
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

void Graphics::present() {
    if (!initialized) return;
    if (headless_) return;
    if (!isActive()) return;
    if (isCanvasActive()) throw Exception("present: cannot present while a Canvas is active");
    if (!isRenderSurfaceReady()) {
        // Native window is mid-(re)creation / rotation; drop this frame's work
        // instead of touching an invalid swapchain (which crashes the driver).
        if (swapchainPassOpen)
            abortOpen3DFrame();
        else
            clear2DBatches();
        hasPendingClear = false;
        return;
    }
    flushBatch();
}

void Graphics::draw(eve::graphics::Graphics *, const glm::mat4 &) const {}
void Graphics::draw(Canvas *, const glm::mat4 &) const {}


}  // namespace eve::graphics::vulkan
