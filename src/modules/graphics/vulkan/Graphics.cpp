// Vulkan backend implementation — backend lifecycle and frame orchestration.
//
// Split out of the original single 5100-line Graphics.cpp so several
// agents can work on backend concerns (lifecycle / pipelines / 2D / 3D /
// mesh) without touching the same translation unit. This file only
// defines members of vulkan::Graphics; see GraphicsInternal.h for the
// shared implementation helpers.

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
    if (pipelineLayout) device->destroyPipelineLayout(pipelineLayout);
    if (texPipeline) device->destroyPipeline(texPipeline);
    if (additiveTexPipeline) device->destroyPipeline(additiveTexPipeline);
    if (opaqueTexPipeline) device->destroyPipeline(opaqueTexPipeline);
    if (texPipelineLayout) device->destroyPipelineLayout(texPipelineLayout);
    if (shaderPipelineLayout) device->destroyPipelineLayout(shaderPipelineLayout);
    if (mesh3dPipeline) device->destroyPipeline(mesh3dPipeline);
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
    if (offscreenTexPipeline) device->destroyPipeline(offscreenTexPipeline);
    if (offscreenAdditiveTexPipeline) device->destroyPipeline(offscreenAdditiveTexPipeline);
    if (offscreenOpaqueTexPipeline) device->destroyPipeline(offscreenOpaqueTexPipeline);
    if (offscreenRenderPass) device->destroyRenderPass(offscreenRenderPass);
    texSetLayoutUnique.reset();
    if (descriptorPool) device->destroyDescriptorPool(descriptorPool);
    if (uploadPool) device->destroyCommandPool(uploadPool);
    if (renderpass) device->destroyRenderPass(renderpass);
    if (surface) inst.instance.destroySurfaceKHR(surface);
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
    {
        StartupStage stage("  vulkan: instance + surface");
        inst = builder.build();

        VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(inst.instance), &rawSurface))
            throw Exception("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        surface = rawSurface;
    }

    {
        StartupStage stage("  vulkan: physical device + device");
        auto selector = inst.selectPhysicalDevice();
        selector.set_surface(surface).set_minimum_version(1, 0);
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
        }
        vkb::DeviceBuilder deviceBuilder = phys.createDevice();
        device = deviceBuilder.build();
        maxSamplerAnisotropy = device.caps.maxSamplerAnisotropy;
    }

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
    depthImage = vkb::DepthStencilImage{};
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
        for (auto &slot : frame.slots) slot.sets.clear();
    for (auto &frame : mesh3dClusteredFrameSlots)
        for (auto &slot : frame.slots) slot.sets.clear();
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
}

void Graphics::recordPendingShadowPasses() {
    if (shadowPendingMask == 0 || !shadowPipeline || shadowMaps.empty()) {
        shadowPendingMask = 0;
        for (auto &d : shadowCascadeDraws) d.clear();
        return;
    }
    auto &slot = currentShadowMap();
    auto &cb = currentPresentCb();
    slot.image.beginDepthAttachment();
    const uint32_t size = uint32_t(ShadowConfig::kMapSize);
    vk::ClearValue clear{};
    clear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    for (int c = 0; c < ShadowConfig::kCascades; ++c) {
        if ((shadowPendingMask & (1u << c)) == 0) continue;
        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = shadowRenderPass;
        rpBegin.framebuffer = slot.framebuffers[c];
        rpBegin.renderArea = vk::Rect2D{{0, 0}, {size, size}};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clear;
        cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
        setViewportAndScissor(cb, size, size);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPipeline);
        bool alphaBound = false;
        for (const auto &d : shadowCascadeDraws[c]) {
            if (!d.mesh || !d.mesh->gpuHandle) continue;
            const bool wantAlpha = d.alphaTest && shadowAlphaPipeline;
            if (wantAlpha != alphaBound) {
                cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                wantAlpha ? shadowAlphaPipeline : shadowPipeline);
                alphaBound = wantAlpha;
            }
            auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
            if (wantAlpha) {
                Texture *alb = d.albedo ? d.albedo : whiteTexture;
                if (alb && alb->gpuHandle && texSetLayout) {
                    auto *gpuTex = static_cast<GpuTexture *>(alb->gpuHandle);
                    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          shadowAlphaPipelineLayout, 0, 1,
                                          gpuTex->descriptorSet.ptr(), 0, nullptr);
                }
            }
            cb.pushConstants(wantAlpha ? shadowAlphaPipelineLayout : shadowPipelineLayout,
                             vk::ShaderStageFlagBits::eVertex, 0,
                             sizeof(glm::mat4), &d.mvp);
            drawIndexedMesh(cb, *gpuMesh);
        }
        cb.endRenderPass();
        shadowCascadeDraws[c].clear();
    }
    slot.image.endSampledLayout();
    shadowPendingMask = 0;
}

void Graphics::recordPendingGBufferPass() {
    if (!gbufferPending) return;
    gbufferPending = false;
    auto *slot = currentGBufferSlot();
    if (!slot || !gbufferPipeline || !gbufferRenderPass || !slot->framebuffer) {
        gbufferPassDraws.clear();
        return;
    }
    auto &cb = currentPresentCb();
    const uint32_t w = uint32_t(gbufferWidth);
    const uint32_t h = uint32_t(gbufferHeight);
    std::array<vk::ClearValue, 4> clears{};
    clears[0].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
    clears[1].color = vk::ClearColorValue(std::array<float, 4>{1, 1, 1, 1});
    clears[2].color = vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 0});
    clears[3].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = gbufferRenderPass;
    rpBegin.framebuffer = slot->framebuffer;
    rpBegin.renderArea = vk::Rect2D{{0, 0}, {w, h}};
    rpBegin.clearValueCount = uint32_t(clears.size());
    rpBegin.pClearValues = clears.data();
    slot->normal.beginColorAttachment();
    slot->depthColor.beginColorAttachment();
    slot->albedo.beginColorAttachment();
    slot->depth.beginDepthAttachment();
    cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    setViewportAndScissor(cb, w, h);
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gbufferPipeline);
    bool alphaBound = false;
    for (const auto &d : gbufferPassDraws) {
        if (!d.mesh || !d.mesh->gpuHandle) continue;
        const bool wantAlpha = d.alphaTest && gbufferAlphaPipeline;
        if (wantAlpha != alphaBound) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                            wantAlpha ? gbufferAlphaPipeline : gbufferPipeline);
            alphaBound = wantAlpha;
        }
        auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
        Texture *alb = d.albedo ? d.albedo : whiteTexture;
        if (alb && alb->gpuHandle && texSetLayout) {
            auto *gpuTex = static_cast<GpuTexture *>(alb->gpuHandle);
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, gbufferPipelineLayout, 0, 1,
                                  gpuTex->descriptorSet.ptr(), 0, nullptr);
        }
        cb.pushConstants(gbufferPipelineLayout,
                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                         sizeof(GBufferPush), &d.push);
        drawIndexedMesh(cb, *gpuMesh);
    }
    cb.endRenderPass();
    gbufferPassDraws.clear();
    if (slot) {
        slot->normal.endSampledLayout();
        slot->depthColor.endSampledLayout();
        slot->albedo.endSampledLayout();
        slot->depth.endSampledLayout();
    }
}

bool Graphics::beginSwapchainRenderPass() {
    if (!beginPresentCommandBuffer()) {
        dropPendingOffscreenPasses();
        return false;
    }
    recordPendingShadowPasses();
    recordPendingGBufferPass();
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
    if (!isActive()) return;
    if (isCanvasActive()) throw Exception("present: cannot present while a Canvas is active");
    if (!isRenderSurfaceReady()) {
        // Native window is mid-(re)creation / rotation; drop this frame's work
        // instead of touching an invalid swapchain (which crashes the driver).
        if (swapchainPassOpen) abortOpen3DFrame();
        else clear2DBatches();
        hasPendingClear = false;
        return;
    }
    flushBatch();
}

void Graphics::draw(eve::graphics::Graphics *, const glm::mat4 &) const {}
void Graphics::draw(Canvas *, const glm::mat4 &) const {}

}  // namespace eve::graphics::vulkan
