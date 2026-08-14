#define VKB_IMPL
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/Light.h"
#include "graphics/AntiAliasing.h"

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
#include "graphics/shaders/mesh3d_gbuffer_vert_spv.inc"
#include "graphics/shaders/mesh3d_gbuffer_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/lit2d_vert_spv.inc"
#include "graphics/shaders/lit2d_frag_spv.inc"
#include "graphics/shaders/voxel_rect_vert_spv.inc"
#include "graphics/shaders/voxel_rect_frag_spv.inc"

#include <assimp/mesh.h>
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/vector3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace eve::graphics::vulkan {

namespace {

constexpr auto kHostVisibleCoherent = vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent;

template <typename T, size_t N>
std::vector<T> embeddedSpirv(const T (&words)[N]) {
    return {words, words + N};
}

template <typename Slots>
auto &currentSlot(Slots &slots, size_t slotCount, size_t slotIndex) {
    if (slots.size() != slotCount) slots.resize(slotCount);
    return slots[slotIndex];
}

template <typename FrameBuffers>
void releaseFrame2dBuffers(FrameBuffers &buffers) {
    buffers.solidBuf.release();
    for (auto &buffer : buffers.texBufs) buffer.release();
    buffers.texBufs.clear();
}

void setViewportAndScissor(vk::CommandBuffer cb, uint32_t width, uint32_t height) {
    const vk::Viewport viewport{0.f, 0.f, float(width), float(height), 0.f, 1.f};
    const vk::Rect2D scissor{{0, 0}, {width, height}};
    cb.setViewport(0, 1, &viewport);
    cb.setScissor(0, 1, &scissor);
}

vk::PipelineLayout createPipelineLayout(vkb::Device &device,
                                        vk::DescriptorSetLayout setLayout = {},
                                        const vk::PushConstantRange *pushConstant = nullptr) {
    vk::PipelineLayoutCreateInfo info{};
    if (setLayout) {
        info.setLayoutCount = 1;
        info.pSetLayouts = &setLayout;
    }
    if (pushConstant) {
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = pushConstant;
    }
    return device->createPipelineLayout(info);
}

vk::PushConstantRange pushConstantRange(vk::ShaderStageFlags stages, uint32_t size) {
    vk::PushConstantRange range{};
    range.stageFlags = stages;
    range.size = size;
    return range;
}

void destroySampler(vkb::Device &device, vk::Sampler &sampler) {
    if (!sampler) return;
    device->destroySampler(sampler);
    sampler = vk::Sampler{};
}

void destroyPipeline(vkb::Device &device, vk::Pipeline &pipeline) {
    if (!pipeline) return;
    device->destroyPipeline(pipeline);
    pipeline = vk::Pipeline{};
}

void destroyPipelineLayout(vkb::Device &device, vk::PipelineLayout &layout) {
    if (!layout) return;
    device->destroyPipelineLayout(layout);
    layout = vk::PipelineLayout{};
}

void drawIndexedMesh(vk::CommandBuffer cb, GpuMesh &mesh) {
    const vk::DeviceSize offset = 0;
    cb.bindVertexBuffers(0, 1, mesh.vertices, &offset);
    cb.bindIndexBuffer(mesh.indices.buffer, 0, vk::IndexType::eUint32);
    cb.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
}

std::unique_ptr<GpuMesh> uploadGpuMesh(vkb::Device &device, vkb::FrameSlot frame,
                                       const std::vector<MeshVertex> &vertices,
                                       const std::vector<uint32_t> &indices) {
    auto gpu = std::make_unique<GpuMesh>();
    gpu->vertices.allocate<MeshVertex>(frame, device, vertices);
    gpu->indices.allocate(frame, device, vk::BufferUsageFlagBits::eIndexBuffer,
                          indices.size() * sizeof(uint32_t), kHostVisibleCoherent);
    gpu->indices.updateLocal(frame, indices.data(), indices.size() * sizeof(uint32_t));
    gpu->indexCount = uint32_t(indices.size());
    return gpu;
}

std::unique_ptr<Mesh> makeMeshHandle(GpuMesh &gpu) {
    auto mesh = std::make_unique<Mesh>();
    mesh->indexCount = int(gpu.indexCount);
    mesh->gpuHandle = &gpu;
    return mesh;
}

vk::Pipeline createSolidColorPipeline(vkb::Device &device, const vkb::BuiltRenderPass &renderPass,
                                      vk::PipelineLayout layout) {
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

class ShaderModulePair {
public:
    ShaderModulePair(vkb::Device &device, const std::vector<uint32_t> &vert,
                     const std::vector<uint32_t> &frag)
        : device(device),
          vert(vkb::PipelineBuilder::createShaderModule(device.instance, vert)),
          frag(vkb::PipelineBuilder::createShaderModule(device.instance, frag)) {}

    ~ShaderModulePair() {
        device->destroyShaderModule(vert);
        device->destroyShaderModule(frag);
    }

    ShaderModulePair(const ShaderModulePair &) = delete;
    ShaderModulePair &operator=(const ShaderModulePair &) = delete;

    vkb::Device &device;
    vk::ShaderModule vert;
    vk::ShaderModule frag;
};

}  // namespace

// --- Backend lifecycle and frame orchestration --------------------------------

std::string Graphics::getBackendName() const { return "vulkan"; }

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
    inst = builder.build();

    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(inst.instance), &rawSurface))
        throw Exception("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
    surface = rawSurface;

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
    createVoxelRectPipeline();
    // Must be set before createShadowResources(): it clears the shadow cascade
    // layers by calling beginShadowPass()/endShadowPass(), and beginShadowPass()
    // asserts `initialized` is already true.
    initialized = true;
    createShadowResources();
    const uint8_t whitePixel[4] = {255, 255, 255, 255};
    whiteTexture = newTexture(1, 1, whitePixel);
}

void Graphics::onNativeWindowDestroyed() {
    if (!initialized) return;
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
    if (swapchainPass)
        return swapchainPass.commandBuffer();
    return presentRecording.commandBuffer();
}

vkb::FrameSlot Graphics::frameToken() const {
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
        for (const auto &d : shadowCascadeDraws[c]) {
            if (!d.mesh || !d.mesh->gpuHandle) continue;
            auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
            cb.pushConstants(shadowPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
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
    for (const auto &d : gbufferPassDraws) {
        if (!d.mesh || !d.mesh->gpuHandle) continue;
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

// --- Swapchain and graphics pipelines -----------------------------------------

void Graphics::createSwapchainAndPipeline() {
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
        {vk::DescriptorType::eStorageBuffer, 256},
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.maxSets = 4096;
    poolInfo.poolSizeCount = 3;
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

    createLit2DPipeline();
}

vk::Pipeline Graphics::createTexturedStylePipeline(const std::vector<uint32_t> &vert,
                                                   const std::vector<uint32_t> &frag,
                                                   const vkb::BuiltRenderPass &rp,
                                                   vk::PipelineLayout layout) {
    ShaderModulePair modules(device, vert, frag);
    return
        device.createPipeline()
            .useClassicPipeline(modules.vert, modules.frag)
            .setPipelineLayout(layout)
            .setVertexInputState(vkb::VertexInputStateBuilder()
                                     .addInputBinding<TexturedVertex>()
                                     .addAttributeDescription<TexturedVertex>())
            .setDynamicStatesViewportScissor()
            .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eNone,
                           vk::FrontFace::eCounterClockwise)
            .setDepthStencil(false, false)
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
            .buffer(0, vk::DescriptorType::eUniformBuffer,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 1)
            .image(1, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(2, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(3, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .buffer(4, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1)
            .image(5, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .image(6, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
            .createUnique(device.instance);
    mesh3dSetLayout = *mesh3dSetLayoutUnique;

    mesh3dPipelineLayout = createPipelineLayout(device, mesh3dSetLayout);
    const auto pcr =
        pushConstantRange(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                          Shader::kPushConstantBytes);
    mesh3dShaderPipelineLayout = createPipelineLayout(device, mesh3dSetLayout, &pcr);

    // UBO + descriptor sets are allocated lazily per draw (see mesh3dSetFor).
    mesh3dFrameSlots.clear();

    auto vert = embeddedSpirv(mesh3d_vert_spv);
    auto frag = embeddedSpirv(mesh3d_frag_spv);
    mesh3dPipeline =
        createMesh3DStylePipeline(vert, frag, mesh3dPipelineLayout, renderpass,
                                  vk::SampleCountFlagBits::e1);
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
            .image(9, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)
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
        slot.depthTex.gpuHandle = nullptr;
        if (slot.framebuffer) {
            device->destroyFramebuffer(slot.framebuffer);
            slot.framebuffer = vk::Framebuffer{};
        }
        destroySampler(device, slot.normalGpu.sampler);
        destroySampler(device, slot.depthColorGpu.sampler);
        destroySampler(device, slot.albedoGpu.sampler);
        destroySampler(device, slot.depthGpu.sampler);
    }
    gbufferSlots.clear();
    post2Sets.clear();
    destroyPipeline(device, gbufferPipeline);
    destroyPipelineLayout(device, gbufferPipelineLayout);
    if (gbufferRenderPass) {
        device->destroyRenderPass(gbufferRenderPass);
        gbufferRenderPass = {};
    }
    gbufferWidth = 0;
    gbufferHeight = 0;
    if (renderControl_) renderControl_->getGBuffer()->clear();
}

void Graphics::destroySceneColorResources() {
    sceneColorPassOpen = false;
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

void Graphics::createUiColorResources(int width, int height) {
    if (width <= 0 || height <= 0) return;
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
                    .addSampledColorAttachment(colorFmt, vk::AttachmentLoadOp::eClear, samples)
                    .addResolveColorAttachment(colorFmt)
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

    if (!texSetLayout || !descriptorPool) return;

    if (!uiColorSlots.empty() && uiColorWidth == width && uiColorHeight == height &&
        uiColorFormat == colorFmt && uiColorSamples == samples)
        return;
    destroyUiColorTargets();

    uiColorWidth = width;
    uiColorHeight = height;
    uiColorFormat = colorFmt;
    uiColorSamples = samples;
    const uint32_t w = uint32_t(width);
    const uint32_t h = uint32_t(height);
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
    uiColorSamples = vk::SampleCountFlagBits::e1;
}

void Graphics::destroyUiColorResources() {
    destroyUiColorTargets();
    if (uiRenderPass) {
        device->destroyRenderPass(uiRenderPass);
        uiRenderPass = {};
    }
}

void Graphics::queueUiResolve() {
    auto *slot = currentUiColorSlot();
    if (!slot || !slot->colorTex.gpuHandle) return;
    TexturedBatch resolve{&slot->colorTex, nullptr, nullptr, Batcher{}};
    resolve.batch.addTexturedRect(0.f, 0.f, float(uiColorWidth), float(uiColorHeight),
                                  Color(1.f, 1.f, 1.f, 1.f), 0.f, 0.f, 1.f, 1.f);
    texturedBatches.push_back(std::move(resolve));
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

namespace {

vk::Format pickGBufferColorFormat(vkb::Device &device) {
    (void)device;
    return vk::Format::eR8G8B8A8Unorm;
}

vk::SampleCountFlagBits sampleCountFlagFor(int samples) {
    if (samples >= 8) return vk::SampleCountFlagBits::e8;
    if (samples >= 4) return vk::SampleCountFlagBits::e4;
    if (samples >= 2) return vk::SampleCountFlagBits::e2;
    return vk::SampleCountFlagBits::e1;
}

}  // namespace

void Graphics::createGBufferResources(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (!gbufferSlots.empty() && gbufferWidth == width && gbufferHeight == height && gbufferPipeline)
        return;
    destroyGBufferResources();

    gbufferWidth = width;
    gbufferHeight = height;
    const uint32_t w = uint32_t(width);
    const uint32_t h = uint32_t(height);
    const vk::Format colorFmt = pickGBufferColorFormat(device);
    const vk::Format depthFmt = vk::Format::eD32Sfloat;

    gbufferSlots.resize(kAsyncResourceCopies);
    for (auto &slot : gbufferSlots) {
        slot.normal = device.createColorTarget(w, h, colorFmt);
        slot.depthColor = device.createColorTarget(w, h, colorFmt);
        slot.albedo = device.createColorTarget(w, h, colorFmt);
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
        makeSampleTex(slot.depthGpu, slot.depthTex, slot.depth.imageView());
    }
}

void Graphics::createSceneColorResources(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (!texSetLayout || !descriptorPool) return;
    const vk::Format colorFmt = swapchain.image_format;
    if (colorFmt == vk::Format::eUndefined) return;

    const bool featureMsaa = !renderControl_ || renderControl_->isEnabled("msaa");
    const int desired = featureMsaa ? msaaSamples : 0;
    if (desired != appliedMsaa) appliedMsaa = desired;
    const vk::SampleCountFlagBits samples = sampleCountFlagFor(clampMsaaSamples(appliedMsaa));

    if (!sceneColorSlots.empty() && sceneColorWidth == width && sceneColorHeight == height &&
        sceneColorFormat == colorFmt && sceneColorSamples == samples && sceneColorRenderPass)
        return;
    destroySceneColorResources();

    sceneColorWidth = width;
    sceneColorHeight = height;
    sceneColorFormat = colorFmt;
    sceneColorSamples = samples;
    const uint32_t w = uint32_t(width);
    const uint32_t h = uint32_t(height);
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
        sceneColorRenderPass =
            device.createRenderPass()
                .addSampledColorAttachment(colorFmt, vk::AttachmentLoadOp::eClear, samples)
                .addResolveColorAttachment(colorFmt)
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
    destroyPipeline(device, mesh3dClusteredPipeline);
    destroyPipeline(device, voxelRectPipeline);

    mesh3dPipeline = createMesh3DStylePipeline(embeddedSpirv(mesh3d_vert_spv),
                                               embeddedSpirv(mesh3d_frag_spv),
                                               mesh3dPipelineLayout, target, samples);
    mesh3dClusteredPipeline =
        createMesh3DStylePipeline(embeddedSpirv(mesh3d_clustered_vert_spv),
                                  embeddedSpirv(mesh3d_clustered_frag_spv),
                                  mesh3dClusteredPipelineLayout, target, samples);
    voxelRectPipeline = buildVoxelRectPipeline(target, samples);

    for (auto &g : ownedGpuShaders) {
        if (!g->isMesh3D) continue;
        destroyPipeline(device, g->mesh3dPipeline);
        if (!g->owner) continue;
        if (g->isHair3D) {
            g->mesh3dPipeline =
                createMesh3DHairPipeline(g->owner->vertexSpirv(), g->owner->fragmentSpirv(),
                                         g->pipelineLayout, target, samples);
        } else {
            g->mesh3dPipeline =
                createMesh3DStylePipeline(g->owner->vertexSpirv(), g->owner->fragmentSpirv(),
                                          g->pipelineLayout, target, samples);
        }
    }

    scenePassPipelineTarget = targetHandle;
    scenePassPipelineSamples = samples;
}

void Graphics::queueSceneColorResolve() {
    Texture *src = getSceneColorTexture();
    if (!src || !src->gpuHandle) return;
    AntiAliasing *aa = pipelineAntiAliasing();
    aa->setMode("fxaa");
    const bool doAA = !renderControl_ || renderControl_->isEnabled("aa");
    if (doAA) {
        aa->setQuality("medium");
    } else {
        aa->setFloat("edgeThreshold", 1.f);
        aa->setFloat("edgeThresholdMin", 1.f);
        aa->setFloat("subpix", 0.f);
    }
    aa->prepareSource(src);
    Shader *sh = aa->getShader();
    if (!sh) return;
    TexturedBatch resolve{src, nullptr, sh, Batcher{}};
    resolve.batch.addTexturedRect(0.f, 0.f, float(width), float(height), Color(1.f, 1.f, 1.f, 1.f),
                                  0.f, 0.f, 1.f, 1.f);
    texturedBatches.insert(texturedBatches.begin(), std::move(resolve));
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
    for (auto &slot : currentMesh3dClusteredFrameSlots().slots) slot.sets.clear();
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

vkb::BoundSet Graphics::mesh3dClusteredSetFor(GpuTexture *gpuTex, GpuTexture *normalTex,
                                              GpuTexture *envTex, GpuTexture *heightTex,
                                              Mesh3dClusteredFrameSlots &fslots,
                                              size_t uboSlot) {
    ASSERT(gpuTex != nullptr);
    ASSERT(normalTex != nullptr);
    ASSERT(envTex != nullptr);
    ASSERT(heightTex != nullptr);
    ASSERT(currentShadowArrayView());
    while (fslots.slots.size() <= uboSlot) {
        fslots.slots.emplace_back();
        auto &s = fslots.slots.back();
        s.ubo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(Mesh3DClusteredUBO),
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
        s.shadowUbo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(ShadowUBO),
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    auto &slot = fslots.slots[uboSlot];
    Mesh3dSetKey key{gpuTex, normalTex, envTex, heightTex};
    auto it = slot.sets.find(key);
    if (it != slot.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &mesh3dClusteredSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    auto &st = currentClusteredStorage();
    vkb::DescriptorSetUpdater updater(14, 14, 0);
    updater.beginDescriptorSet(unbound)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.ubo.buffer, 0, sizeof(Mesh3DClusteredUBO))
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
        .beginBuffers(7, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.shadowUbo.buffer, 0, sizeof(ShadowUBO))
        .beginImages(8, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(shadowSampler, currentShadowArrayView()))
        .beginImages(9, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(heightTex->sampler, heightTex->imageView()))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    slot.sets.emplace(key, bound);
    return bound;
}

vk::Pipeline Graphics::createMesh3DStylePipeline(const std::vector<uint32_t> &vert,
                                                 const std::vector<uint32_t> &frag,
                                                 vk::PipelineLayout layout,
                                                 const vkb::BuiltRenderPass &rp,
                                                 vk::SampleCountFlagBits samples) {
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    vk::Pipeline pipeline =
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
    return pipeline;
}

vk::Pipeline Graphics::createMesh3DHairPipeline(const std::vector<uint32_t> &vert,
                                                const std::vector<uint32_t> &frag,
                                                vk::PipelineLayout layout,
                                                const vkb::BuiltRenderPass &rp,
                                                vk::SampleCountFlagBits samples) {
    vk::ShaderModule vertModule = vkb::PipelineBuilder::createShaderModule(device.instance, vert);
    vk::ShaderModule fragModule = vkb::PipelineBuilder::createShaderModule(device.instance, frag);
    vk::Pipeline pipeline =
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
    return pipeline;
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

    // Textured offscreen pipeline mirrors createTexturedPipeline but with offscreen RP.
    auto tvert = embeddedSpirv(textured_vert_spv);
    auto tfrag = embeddedSpirv(textured_frag_spv);
    offscreenTexPipeline =
        createTexturedStylePipeline(tvert, tfrag, offscreenRenderPass, texPipelineLayout);

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

void Graphics::ensurePresentCaptureHook() {
    // Only install the hook when readback is requested. drawFrame() treats a
    // non-empty hook as "wait for this frame's fence" even when
    // synchronous_frames is false, which would serialize every frame and erase
    // the multi-frame overlap we rely on for async rendering.
    if (screenReadbackEnabled) {
        presentModel.after_render_before_present = [this](uint32_t imageIndex) {
            captureSwapchainImage(imageIndex);
        };
    } else {
        presentModel.after_render_before_present = nullptr;
    }
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

namespace {

uint32_t rgba8MipBytes(uint32_t width, uint32_t height) {
    // Matches VKBuilder GenericImage::upload packing for eR8G8B8A8Unorm.
    return 4u * width * height;
}

void appendBoxFilteredMip(std::vector<uint8_t> &out, const uint8_t *src, uint32_t srcW,
                          uint32_t srcH, uint32_t dstW, uint32_t dstH) {
    const size_t base = out.size();
    out.resize(base + size_t(rgba8MipBytes(dstW, dstH)));
    uint8_t *dst = out.data() + base;
    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t y0 = std::min(y * 2u, srcH - 1u);
        const uint32_t y1 = std::min(y0 + 1u, srcH - 1u);
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t x0 = std::min(x * 2u, srcW - 1u);
            const uint32_t x1 = std::min(x0 + 1u, srcW - 1u);
            uint32_t acc[4] = {0, 0, 0, 0};
            const uint32_t samples[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
            for (const auto &s : samples) {
                const size_t i = (size_t(s[1]) * srcW + s[0]) * 4u;
                acc[0] += src[i + 0];
                acc[1] += src[i + 1];
                acc[2] += src[i + 2];
                acc[3] += src[i + 3];
            }
            const size_t o = (size_t(y) * dstW + x) * 4u;
            dst[o + 0] = static_cast<uint8_t>((acc[0] + 2u) / 4u);
            dst[o + 1] = static_cast<uint8_t>((acc[1] + 2u) / 4u);
            dst[o + 2] = static_cast<uint8_t>((acc[2] + 2u) / 4u);
            dst[o + 3] = static_cast<uint8_t>((acc[3] + 2u) / 4u);
        }
    }
}

/** Packed mip chain for one 2D layer (base + downsampled levels). */
std::vector<uint8_t> buildMipChain2D(const uint8_t *rgba, uint32_t width, uint32_t height,
                                     uint32_t mipLevels) {
    std::vector<uint8_t> packed;
    packed.reserve(size_t(width) * size_t(height) * 4u * 2u);
    packed.insert(packed.end(), rgba, rgba + size_t(rgba8MipBytes(width, height)));

    uint32_t srcW = width;
    uint32_t srcH = height;
    size_t srcOffset = 0;
    for (uint32_t level = 1; level < mipLevels; ++level) {
        const uint32_t dstW = std::max(srcW >> 1, 1u);
        const uint32_t dstH = std::max(srcH >> 1, 1u);
        const uint8_t *src = packed.data() + srcOffset;
        srcOffset = packed.size();
        appendBoxFilteredMip(packed, src, srcW, srcH, dstW, dstH);
        srcW = dstW;
        srcH = dstH;
    }
    return packed;
}

/**
 * Cubemap packed as VKBuilder expects: for each mip, for each face (+X..-Z).
 * Input faces are contiguous full-res faces.
 */
std::vector<uint8_t> buildMipChainCube(const uint8_t *rgbaFaces, uint32_t faceSize,
                                       uint32_t mipLevels) {
    const uint32_t faceBytes = rgba8MipBytes(faceSize, faceSize);
    std::vector<std::vector<uint8_t>> faceChains(6);
    for (uint32_t f = 0; f < 6; ++f) {
        faceChains[f] = buildMipChain2D(rgbaFaces + size_t(f) * faceBytes, faceSize, faceSize,
                                        mipLevels);
    }

    std::vector<uint8_t> packed;
    uint32_t w = faceSize;
    uint32_t h = faceSize;
    size_t faceOffsets[6] = {0, 0, 0, 0, 0, 0};
    for (uint32_t level = 0; level < mipLevels; ++level) {
        const uint32_t levelBytes = rgba8MipBytes(w, h);
        for (uint32_t f = 0; f < 6; ++f) {
            const uint8_t *src = faceChains[f].data() + faceOffsets[f];
            packed.insert(packed.end(), src, src + levelBytes);
            faceOffsets[f] += levelBytes;
        }
        w = std::max(w >> 1, 1u);
        h = std::max(h >> 1, 1u);
    }
    return packed;
}

TextureCreateInfo normalizeTextureInfo(TextureCreateInfo info) {
    if (info.generateMipmaps && info.sampler.mipmap == MipmapMode::Disabled)
        info.sampler.mipmap = MipmapMode::Linear;
    if (info.sampler.maxAnisotropy < 1.f) info.sampler.maxAnisotropy = 1.f;
    return info;
}

}  // namespace

float Graphics::getMaxAnisotropy() const { return maxSamplerAnisotropy; }

vk::Sampler Graphics::createVkSampler(const TextureSampler &sampler, uint32_t mipLevels) const {
    auto wrapMode = [](bool repeat) {
        return repeat ? vk::SamplerAddressMode::eRepeat : vk::SamplerAddressMode::eClampToEdge;
    };
    auto toFilter = [](FilterMode m) {
        return m == FilterMode::Nearest ? vk::Filter::eNearest : vk::Filter::eLinear;
    };
    auto toMip = [](MipmapMode m) {
        return m == MipmapMode::Nearest ? vk::SamplerMipmapMode::eNearest
                                        : vk::SamplerMipmapMode::eLinear;
    };

    const bool useMips = sampler.mipmap != MipmapMode::Disabled && mipLevels > 1;
    float maxLod = useMips ? std::min(sampler.maxLod, float(mipLevels - 1)) : 0.f;
    if (maxLod < sampler.minLod) maxLod = sampler.minLod;

    float aniso = 1.f;
    bool enableAniso = false;
    if (sampler.maxAnisotropy > 1.f && maxSamplerAnisotropy > 1.f) {
        enableAniso = true;
        aniso = std::min(sampler.maxAnisotropy, maxSamplerAnisotropy);
    }

    vkb::SamplerBuilder sb;
    return sb.magFilter(toFilter(sampler.mag))
        .minFilter(toFilter(sampler.min))
        .mipmapMode(useMips ? toMip(sampler.mipmap) : vk::SamplerMipmapMode::eNearest)
        .addressModeU(wrapMode(sampler.repeatU))
        .addressModeV(wrapMode(sampler.repeatV))
        .addressModeW(wrapMode(sampler.repeatW))
        .mipLodBias(sampler.lodBias)
        .anisotropyEnable(enableAniso ? VK_TRUE : VK_FALSE)
        .maxAnisotropy(aniso)
        .minLod(useMips ? sampler.minLod : 0.f)
        .maxLod(maxLod)
        .build(device);
}

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
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);
    gpu->descriptorSet = vkb::BoundSet{sets[0]};
    writeCombinedImageDescriptor(gpu.get());

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
    // IBL shaders use textureLod(roughness * 5); generate mips by default.
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
    gpu->cubeImage.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);
    // Cubemap sampled via mesh3d descriptor sets — no 2D texSetLayout binding required here.

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

bool Graphics::replaceTexturePixels(Texture *tex, image::ImageData *data) {
    if (!tex || !data) return false;
    if (data->getFormat() != "RGBA8") return false;
    const int w = data->getWidth();
    const int h = data->getHeight();
    const auto *rgba = static_cast<const uint8_t *>(data->getData());
    if (w <= 0 || h <= 0 || !rgba) return false;

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
    gpu->image.upload(uploadPool, device.getQueue(vkb::QueueType::graphics), bytes);

    gpu->sampler = createVkSampler(info.sampler, mipLevels);

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);
    gpu->descriptorSet = vkb::BoundSet{sets[0]};
    writeCombinedImageDescriptor(gpu.get());

    void *oldHandle = tex->gpuHandle;
    for (auto &owned : ownedGpuTextures) {
        if (owned.get() != oldHandle) continue;
        // Destroying the old image/sampler while an in-flight frame still
        // samples it is a typical TDR. Drain first, then drop cached sets.
        waitForSharedGpuResources();
        if (owned->sampler) device->destroySampler(owned->sampler);
        owned = std::move(gpu);
        tex->gpuHandle = owned.get();
        tex->width = w;
        tex->height = h;
        tex->pixelWidth = w;
        tex->pixelHeight = h;
        tex->mipmapCount = int(mipLevels);
        tex->sampler = info.sampler;
        invalidateTextureBindings();
        return true;
    }

    // Texture not in owned list — attach as new ownership.
    tex->gpuHandle = gpu.get();
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
                                        const Color &color, bool rotatedUV) {
    if (!texture) {
        drawSolidRect(x, y, w, h, color);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().depth != nullptr ||
        texturedBatches.back().shader != shader) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1, rotatedUV);
}

void Graphics::drawTexturedRectShaderUVRotated(Texture *texture, Shader *shader, float cx, float cy,
                                               float w, float h, float degrees, float u0, float v0,
                                               float u1, float v1, const Color &color,
                                               bool rotatedUV) {
    if (!texture) {
        drawSolidRect(cx - w * 0.5f, cy - h * 0.5f, w, h, color);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().depth != nullptr ||
        texturedBatches.back().shader != shader) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRectRotated(cx, cy, w, h, degrees, color, u0, v0, u1, v1,
                                                        rotatedUV);
}

void Graphics::drawTexturedRectShaderDepth(Texture *color, Texture *depth, Shader *shader, float x,
                                           float y, float w, float h, const Color &tint) {
    if (!color) {
        drawSolidRect(x, y, w, h, tint);
        return;
    }
    if (!depth) {
        drawTexturedRectShader(color, shader, x, y, w, h, tint);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != color ||
        texturedBatches.back().depth != depth || texturedBatches.back().shader != shader) {
        texturedBatches.push_back(TexturedBatch{color, depth, shader, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, tint, 0.f, 0.f, 1.f, 1.f);
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

vkb::BoundSet Graphics::lit2dSetFor(GpuTexture *albedo, GpuTexture *normal, bool offscreen) {
    ASSERT(albedo != nullptr);
    ASSERT(normal != nullptr);
    auto &sets = offscreen ? offscreenLit2dSets : currentLit2dSets();
    vkb::GenericBuffer &ubo = offscreen ? offscreenLighting2dUbo : currentLighting2dUbo();
    LitSetKey key{albedo, normal};
    auto it = sets.find(key);
    if (it != sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &lit2dSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(albedo->sampler, albedo->image.imageView()))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(normal->sampler, normal->image.imageView()))
        .beginBuffers(2, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(ubo.buffer, 0, sizeof(Lighting2DUBO))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    sets.emplace(key, bound);
    return bound;
}

vkb::BoundSet Graphics::post2SetFor(GpuTexture *color, GpuTexture *depth) {
    if (!color || !color->sampler) return {};
    vk::ImageView colorView = color->imageView();
    if (!colorView) return {};
    if (!depth || !depth->sampler || !depth->imageView()) depth = color;
    LitSetKey key{color, depth};
    auto it = post2Sets.find(key);
    if (it != post2Sets.end()) return it->second;

    auto sets = vkb::DescriptorSetBuilder().layout(texSetLayout).build(device.instance, descriptorPool);
    vkb::UnboundSet unbound{sets[0]};
    vkb::DescriptorSetUpdater updater;
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(color->sampler, colorView))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(depth->sampler, depth->imageView()))
        .update(device.instance);
    vkb::BoundSet bound = std::move(unbound).publish();
    post2Sets.emplace(key, bound);
    return bound;
}

void Graphics::drawLitBatches(vk::CommandBuffer cb, int viewW, int viewH, vk::Pipeline pipeline,
                              std::vector<LitBatch> &batches,
                              std::vector<vkb::HostVertexBuffer> &texBufs, size_t &texBufIndex,
                              bool offscreen) {
    if (!pipeline || batches.empty() || !lit2dPipelineLayout) return;
    lighting2dFrame.meta.y = float(viewW);
    lighting2dFrame.meta.z = float(viewH);
    vkb::GenericBuffer &ubo = offscreen ? offscreenLighting2dUbo : currentLighting2dUbo();
    ubo.updateLocal(frameToken(), &lighting2dFrame, sizeof(Lighting2DUBO));

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

        if (texBufIndex >= texBufs.size()) texBufs.emplace_back();
        vkb::HostVertexBuffer &vb = texBufs[texBufIndex++];
        vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);

        vk::DescriptorSet set = lit2dSetFor(albedoGpu, normalGpu, offscreen);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, lit2dPipelineLayout, 0, 1, &set, 0,
                              nullptr);
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, vb, &offset);
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
    gpu->mesh3dPipeline = createMesh3DStylePipeline(vert, fragSpv, mesh3dShaderPipelineLayout,
                                                    activeScenePass(), activeSceneSamples());

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eMesh3D);
    sh->setSpirv(std::move(vert), fragSpv);
    sh->gpuHandle = gpu.get();
    gpu->owner = sh.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

Shader *Graphics::newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                       const std::vector<uint32_t> &fragSpv) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newHairShaderFromSpv: graphics not initialized");
    if (fragSpv.empty()) throw Exception("newHairShaderFromSpv: empty fragment SPIR-V");
    if (!mesh3dShaderPipelineLayout)
        throw Exception("newHairShaderFromSpv: mesh3d pipeline layout missing");

    std::vector<uint32_t> vert = vertSpv;
    if (vert.empty())
        vert.assign(mesh3d_hair_vert_spv, mesh3d_hair_vert_spv + mesh3d_hair_vert_spv_count);
    if (vert[0] != 0x07230203 || fragSpv[0] != 0x07230203)
        throw Exception("newHairShaderFromSpv: SPIR-V magic mismatch");

    auto gpu = std::make_unique<GpuShader>();
    gpu->isMesh3D = true;
    gpu->isHair3D = true;
    gpu->pipelineLayout = mesh3dShaderPipelineLayout;
    gpu->mesh3dPipeline = createMesh3DHairPipeline(vert, fragSpv, mesh3dShaderPipelineLayout,
                                                   activeScenePass(), activeSceneSamples());

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eMesh3D);
    sh->setSpirv(std::move(vert), fragSpv);
    sh->gpuHandle = gpu.get();
    gpu->owner = sh.get();

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

    // Offscreen color is a single shared image. An in-flight swapchain frame
    // may still be sampling it (draw canvas to screen last frame), so drain
    // those frames before transitioning it back to a color attachment.
    waitForSharedGpuResources();

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
                                canvas->colorImage().beginColorAttachment();
                                cb.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

                                setViewportAndScissor(cb, uint32_t(canvas->getWidth()),
                                                      uint32_t(canvas->getHeight()));

                                vkb::HostVertexBuffer &solidBuf = offscreenBuffers.solidBuf;
                                std::vector<vkb::HostVertexBuffer> &texBufs = offscreenBuffers.texBufs;
                                size_t texBufIndex = 0;

                                if (!solid.empty() && offscreenSolidPipeline) {
                                    Batcher ndc = solid;
                                    ndc.toNDC(canvas->getWidth(), canvas->getHeight());
                                    std::vector<ColorVertex> gpuVerts;
                                    gpuVerts.reserve(ndc.vertices().size());
                                    for (const auto &v : ndc.vertices())
                                        gpuVerts.push_back(ColorVertex{v.pos, v.color});
                                    solidBuf.allocate<ColorVertex>(frameToken(), device, gpuVerts);
                                    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenSolidPipeline);
                                    vk::DeviceSize offset = 0;
                                    cb.bindVertexBuffers(0, 1, solidBuf, &offset);
                                    cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                }

                                if (offscreenTexPipeline) {
                                    for (auto &tb : textured) {
                                        if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) continue;
                                        auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
                                        vk::DescriptorSet texSet = gpu->descriptorSet;
                                        if (tb.depth && tb.depth->gpuHandle) {
                                            auto *depthGpu = static_cast<GpuTexture *>(tb.depth->gpuHandle);
                                            if (vk::DescriptorSet combo = post2SetFor(gpu, depthGpu))
                                                texSet = combo;
                                        }
                                        Batcher ndc = tb.batch;
                                        ndc.toNDC(canvas->getWidth(), canvas->getHeight());
                                        std::vector<TexturedVertex> gpuVerts;
                                        gpuVerts.reserve(ndc.vertices().size());
                                        for (const auto &v : ndc.vertices())
                                            gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

                                        if (texBufIndex >= texBufs.size()) texBufs.emplace_back();
                                        vkb::HostVertexBuffer &vb = texBufs[texBufIndex++];
                                        vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);

                                        if (tb.shader && tb.shader->gpuHandle) {
                                            ensureShaderOffscreenPipeline(tb.shader);
                                            auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
                                            if (!gs->offscreenPipeline) continue;
                                            cb.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                                            gs->offscreenPipeline);
                                            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                                                  shaderPipelineLayout, 0, 1,
                                                                  &texSet, 0, nullptr);
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
                                                                  &texSet, 0, nullptr);
                                        }
                                        vk::DeviceSize offset = 0;
                                        cb.bindVertexBuffers(0, 1, vb, &offset);
                                        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
                                    }
                                }

                                if (offscreenLitPipeline)
                                    drawLitBatches(cb, canvas->getWidth(), canvas->getHeight(),
                                                   offscreenLitPipeline, lit, texBufs, texBufIndex,
                                                   true);

                                cb.endRenderPass();
                                canvas->colorImage().endSampledLayout();
                            });
}

void Graphics::flushToSwapchain() {
    const bool continue3D = swapchainPassOpen;
    const bool hadScenePass = sceneColorPassOpen;

    if (hadScenePass) {
        endSceneColorRenderPass();
        queueSceneColorResolve();
    }

    if (!continue3D) {
        // 2D-only path: acquire the present CB and record deferred passes now,
        // so the UI overlay's dedicated MSAA pass can be recorded before the
        // swapchain pass begins.
        if (!beginPresentCommandBuffer()) {
            dropPendingOffscreenPasses();
            hasPendingClear = false;
            return;
        }
        recordPendingShadowPasses();
        recordPendingGBufferPass();
    }

    // Render the UI overlay (ImGui) into its own MSAA pass, resolved and
    // composited as the top-most fullscreen quad. Skipped only on the rare 3D
    // fallback path where the swapchain pass is already open from begin3DFrame.
    if (presentOverlayFn_ && !(continue3D && !hadScenePass)) {
        renderUiOverlayPass();
    }

    if (hadScenePass || !continue3D) {
        beginSwapchainColorPass();
        swapchainPassOpen = true;
    }

    Batcher solid = solidBatch;
    auto textured = std::move(texturedBatches);
    auto lit = std::move(litBatches);
    solidBatch.clear();
    texturedBatches.clear();
    litBatches.clear();

    auto &cb = currentPresentCb();
    setViewportAndScissor(cb, swapchain.extent.width, swapchain.extent.height);

    // Persistent per-frame-slot buffers (see currentFrame2DBuffers). Safe to
    // overwrite: acquireForFrame() already waited this slot's fence.
    auto &frameBufs = currentFrame2DBuffers();
    vkb::HostVertexBuffer &solidBuf = frameBufs.solidBuf;
    std::vector<vkb::HostVertexBuffer> &texBufs = frameBufs.texBufs;
    size_t texBufIndex = 0;

    if (!solid.empty() && pipeline) {
        Batcher ndc = solid;
        ndc.toNDC(width, height);
        std::vector<ColorVertex> gpuVerts;
        gpuVerts.reserve(ndc.vertices().size());
        for (const auto &v : ndc.vertices())
            gpuVerts.push_back(ColorVertex{v.pos, v.color});
        solidBuf.allocate<ColorVertex>(frameToken(), device, gpuVerts);
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        vk::DeviceSize offset = 0;
        cb.bindVertexBuffers(0, 1, solidBuf, &offset);
        cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
    }

    if (texPipeline) {
        for (auto &tb : textured) {
            if (tb.batch.empty() || !tb.texture || !tb.texture->gpuHandle) continue;
            auto *gpu = static_cast<GpuTexture *>(tb.texture->gpuHandle);
            vk::DescriptorSet texSet = gpu->descriptorSet;
            if (tb.depth && tb.depth->gpuHandle) {
                auto *depthGpu = static_cast<GpuTexture *>(tb.depth->gpuHandle);
                if (vk::DescriptorSet combo = post2SetFor(gpu, depthGpu)) texSet = combo;
            }
            Batcher ndc = tb.batch;
            ndc.toNDC(width, height);
            std::vector<TexturedVertex> gpuVerts;
            gpuVerts.reserve(ndc.vertices().size());
            for (const auto &v : ndc.vertices())
                gpuVerts.push_back(TexturedVertex{v.pos, v.color, v.uv});

            if (texBufIndex >= texBufs.size()) texBufs.emplace_back();
            vkb::HostVertexBuffer &vb = texBufs[texBufIndex++];
            vb.allocate<TexturedVertex>(frameToken(), device, gpuVerts);

            if (tb.shader && tb.shader->gpuHandle) {
                auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, gs->swapchainPipeline);
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, shaderPipelineLayout, 0, 1,
                                      &texSet, 0, nullptr);
                cb.pushConstants(shaderPipelineLayout,
                                 vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                                 Shader::kPushConstantBytes, tb.shader->pushConstantData());
            } else {
                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, texPipeline);
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, texPipelineLayout, 0, 1,
                                      &texSet, 0, nullptr);
            }
            vk::DeviceSize offset = 0;
            cb.bindVertexBuffers(0, 1, vb, &offset);
            cb.draw(uint32_t(gpuVerts.size()), 1, 0, 0);
        }
    }

    if (lit2dPipeline) drawLitBatches(cb, width, height, lit2dPipeline, lit, texBufs, texBufIndex,
                                      false);

    // Invalidate prior readback so a failed present cannot reuse a stale frame.
    if (screenReadbackEnabled) hasPresentedFrame = false;

    // Fallback path only: the swapchain pass was already open when this frame
    // began (3D MSAA scene pass unavailable), so draw the overlay directly.
    if (presentOverlayFn_ && continue3D && !hadScenePass) {
        VkCommandBuffer raw = static_cast<VkCommandBuffer>(cb);
        presentOverlayFn_(presentOverlayUser_, raw);
    }

    presentRecording = swapchainPass.endRenderPass();
    swapchainPass = {};
    presentRecording.end().submitAndPresent();
    presentRecording = {};
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
    recordPendingShadowPasses();
    recordPendingGBufferPass();

    // 3D pass clears with backgroundColor (setBackgroundColor), not a stale 2D clear.
    clearColor = backgroundColor;
    createSceneColorResources(int(swapchain.extent.width), int(swapchain.extent.height));
    if (beginSceneColorRenderPass()) {
        // Rebuild scene-pass pipelines to match the active scene pass (MSAA).
        // Must happen AFTER the pass opens: if the MSAA scene pass is unavailable
        // we fall back to the swapchain (e1) render pass below, and rasterizing
        // with an Nx pipeline into an e1 pass is UB that can hang the GPU (TDR).
        ensureScenePassPipelines(activeScenePass(), activeSceneSamples());
    } else {
        // Scene pass unavailable — draw 3D straight into the swapchain. Scene-pass
        // pipelines must be 1x / swapchain-compatible to avoid the samples mismatch.
        ensureScenePassPipelines(renderpass, vk::SampleCountFlagBits::e1);
        beginSwapchainColorPass();
    }

    auto &cb = currentPresentCb();
    setViewportAndScissor(cb, swapchain.extent.width, swapchain.extent.height);

    currentMesh3dFrameSlots().drawIndex = 0;
    currentMesh3dClusteredFrameSlots().drawIndex = 0;
    currentVoxelInstanceFrame().drawIndex = 0;
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

void Graphics::destroyVoxelRectResources() {
    for (auto &frame : voxelInstanceFrames) {
        for (auto &slot : frame.slots) {
            slot.buffer.release();
            slot.capacityBytes = 0;
        }
        frame.slots.clear();
        frame.drawIndex = 0;
    }
    voxelInstanceFrames.clear();
    voxelRectSets.clear();
    auto destroyBuf = [&](vkb::GenericBuffer &b) { b.release(); };
    if (voxelUnitQuadReady) {
        destroyBuf(voxelUnitQuadVerts);
        destroyBuf(voxelUnitQuadIndices);
        voxelUnitQuadReady = false;
    }
    if (voxelRectPipeline) {
        device->destroyPipeline(voxelRectPipeline);
        voxelRectPipeline = vk::Pipeline{};
    }
    if (voxelRectPipelineLayout) {
        device->destroyPipelineLayout(voxelRectPipelineLayout);
        voxelRectPipelineLayout = vk::PipelineLayout{};
    }
    voxelRectSetLayoutUnique.reset();
    voxelRectSetLayout = vk::DescriptorSetLayout{};
}

void Graphics::createVoxelRectPipeline() {
    if (voxelRectPipeline) return;

    if (!voxelRectPipelineLayout) {
        vkb::DescriptorSetLayoutBuilder layoutBuilder;
        voxelRectSetLayoutUnique =
            layoutBuilder
                .image(0, vk::DescriptorType::eCombinedImageSampler,
                       vk::ShaderStageFlagBits::eFragment, 1)
                .createUnique(device.instance);
        voxelRectSetLayout = *voxelRectSetLayoutUnique;

        const auto pcr = pushConstantRange(vk::ShaderStageFlagBits::eVertex, sizeof(VoxelRectPC));
        voxelRectPipelineLayout = createPipelineLayout(device, voxelRectSetLayout, &pcr);
    }

    voxelRectPipeline = buildVoxelRectPipeline(renderpass, vk::SampleCountFlagBits::e1);
    ensureVoxelUnitQuad();
}

vk::Pipeline Graphics::buildVoxelRectPipeline(const vkb::BuiltRenderPass &rp,
                                              vk::SampleCountFlagBits samples) {
    auto vert = embeddedSpirv(voxel_rect_vert_spv);
    auto frag = embeddedSpirv(voxel_rect_frag_spv);
    ShaderModulePair modules(device, vert, frag);

    vk::VertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(glm::vec2);
    bindings[0].inputRate = vk::VertexInputRate::eVertex;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(uint32_t);
    bindings[1].inputRate = vk::VertexInputRate::eInstance;

    vk::VertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = vk::Format::eR32G32Sfloat;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 1;
    attrs[1].format = vk::Format::eR32Uint;
    attrs[1].offset = 0;

    vk::PipelineVertexInputStateCreateInfo vi{};
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    // Unit-quad indices 0-2-1 / 0-3-2 are object-space CCW for outward faces.
    // perspectiveVulkanRH_ZO flips clip Y, so those faces are CCW in the
    // framebuffer — CounterClockwise frontFace keeps them. Clockwise + Back
    // (the mesh-pipeline convention) culls every submitted face after the Y
    // flip; CPU 6-dir cull already dropped the true back faces, so the frame
    // collapses to a 1px silhouette.
    return device.createPipeline()
        .useClassicPipeline(modules.vert, modules.frag)
        .setPipelineLayout(voxelRectPipelineLayout)
        .setVertexInputState(vi)
        .setDynamicStatesViewportScissor()
        .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eBack,
                       vk::FrontFace::eCounterClockwise)
        .setMultisampler(false, samples)
        .setDepthStencil(true, true, vk::CompareOp::eLess)
        .build(rp);
}

void Graphics::ensureVoxelUnitQuad() {
    if (voxelUnitQuadReady) return;
    const float corners[8] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    voxelUnitQuadVerts.allocate(frameToken(), device, vk::BufferUsageFlagBits::eVertexBuffer, sizeof(corners),
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);
    voxelUnitQuadVerts.updateLocal(frameToken(), corners, sizeof(corners));
    const uint32_t indices[6] = {0, 2, 1, 0, 3, 2};
    voxelUnitQuadIndices.allocate(frameToken(), device, vk::BufferUsageFlagBits::eIndexBuffer, sizeof(indices),
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent);
    voxelUnitQuadIndices.updateLocal(frameToken(), indices, sizeof(indices));
    voxelUnitQuadReady = true;
}

vkb::BoundSet Graphics::voxelRectSetFor(GpuTexture *gpuTex) {
    ASSERT(gpuTex != nullptr);
    auto it = voxelRectSets.find(gpuTex);
    if (it != voxelRectSets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &voxelRectSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    vkb::DescriptorSetUpdater updater(4, 4, 0);
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpuTex->sampler, gpuTex->imageView()))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    voxelRectSets.emplace(gpuTex, bound);
    return bound;
}

void Graphics::drawVoxelFaceInstances(const uint32_t *packed, int count, float originX,
                                      float originY, float originZ, const std::string &faceDir,
                                      Texture *atlas, int tilesPerRow) {
    ASSERT(initialized);
    if (!initialized) throw Exception("drawVoxelFaceInstances: graphics not initialized");
    if (!swapchainPassOpen) throw Exception("drawVoxelFaceInstances: call begin3DFrame first");
    if (!voxelRectPipeline) createVoxelRectPipeline();
    if (!packed || count <= 0) return;

    int face = -1;
    if (faceDir == "posX" || faceDir == "+x")
        face = 0;
    else if (faceDir == "negX" || faceDir == "-x")
        face = 1;
    else if (faceDir == "posY" || faceDir == "+y")
        face = 2;
    else if (faceDir == "negY" || faceDir == "-y")
        face = 3;
    else if (faceDir == "posZ" || faceDir == "+z")
        face = 4;
    else if (faceDir == "negZ" || faceDir == "-z")
        face = 5;
    else
        throw Exception("drawVoxelFaceInstances: unknown faceDir '%s'", faceDir.c_str());

    Texture *tex = atlas ? atlas : whiteTexture;
    if (!tex || !tex->gpuHandle) throw Exception("drawVoxelFaceInstances: missing texture");
    auto *gpuTex = static_cast<GpuTexture *>(tex->gpuHandle);

    ensureVoxelUnitQuad();

    const vk::DeviceSize bytes = vk::DeviceSize(count) * sizeof(uint32_t);
    auto &vframe = currentVoxelInstanceFrame();
    const size_t slotIndex = vframe.drawIndex++;
    while (vframe.slots.size() <= slotIndex) vframe.slots.emplace_back();
    auto &slot = vframe.slots[slotIndex];
    if (slot.capacityBytes < size_t(bytes) || !slot.buffer.buffer) {
        const size_t alloc = std::max(size_t(bytes), slot.capacityBytes ? slot.capacityBytes * 2 : size_t(bytes));
        slot.buffer.allocate(frameToken(), device, vk::BufferUsageFlagBits::eVertexBuffer, vk::DeviceSize(alloc),
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent);
        slot.capacityBytes = alloc;
    }
    slot.buffer.updateLocal(frameToken(), packed, size_t(bytes));

    VoxelRectPC pc{};
    pc.viewProj = mesh3dFrameUbo.mvp;
    pc.chunkOrigin = glm::vec4(originX, originY, originZ, float(face));
    pc.atlasInfo = glm::vec4(float(std::max(1, tilesPerRow)), 0.f, 0.f, 0.f);
    pc.tint = mesh3dFrameUbo.tint;

    auto &cb = currentPresentCb();
    vk::DescriptorSet set = voxelRectSetFor(gpuTex);
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, voxelRectPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, voxelRectPipelineLayout, 0, 1, &set, 0,
                          nullptr);
    cb.pushConstants(voxelRectPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VoxelRectPC),
                     &pc);

    vk::Buffer vbufs[2] = {voxelUnitQuadVerts.buffer, slot.buffer.buffer};
    vk::DeviceSize offsets[2] = {0, 0};
    cb.bindVertexBuffers(0, 2, vbufs, offsets);
    cb.bindIndexBuffer(voxelUnitQuadIndices.buffer, 0, vk::IndexType::eUint32);
    cb.drawIndexed(6, uint32_t(count), 0, 0, 0);
}

void Graphics::setMesh3DNormalTexture(Texture *normal) { mesh3dNormalTexture = normal; }

void Graphics::setMesh3DHeightTexture(Texture *height) { mesh3dHeightTexture = height; }

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
    shadowCascadeDraws[cascade] = std::move(shadowPassDraws);
    shadowPassDraws.clear();
    shadowPassCascade = -1;
    shadowPendingMask |= (1u << cascade);
    // GPU work is recorded into the swapchain command buffer in
    // beginSwapchainRenderPass() so it shares the frame's submit and uses the
    // ping-pong copy for this frame slot.
}

void Graphics::beginGBufferPass(int width, int height) {
    ASSERT(initialized);
    if (!initialized) throw Exception("beginGBufferPass: graphics not initialized");
    if (width <= 0 || height <= 0) throw Exception("beginGBufferPass: invalid size");
    if (!texSetLayout || !descriptorPool)
        throw Exception("beginGBufferPass: textured descriptor layout not ready");
    createGBufferResources(width, height);
    gbufferPassActive = true;
    gbufferPassDraws.clear();
}

void Graphics::drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model, float nearZ,
                               float farZ, Texture *albedo, float tintR, float tintG, float tintB) {
    if (!gbufferPassActive) throw Exception("drawMeshGBuffer: call beginGBufferPass first");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMeshGBuffer: null mesh");
    GBufferDraw d{};
    d.mesh = mesh;
    d.albedo = albedo;
    d.push.mvp = mvp;
    // Pack model rows (column-major glm → row vectors with translation in .w).
    d.push.modelR0 = glm::vec4(model[0][0], model[1][0], model[2][0], model[3][0]);
    d.push.modelR1 = glm::vec4(model[0][1], model[1][1], model[2][1], model[3][1]);
    d.push.modelR2 = glm::vec4(model[0][2], model[1][2], model[2][2], model[3][2]);
    auto u8 = [](float x) -> uint32_t {
        return uint32_t(std::lround(std::clamp(x, 0.f, 1.f) * 255.f));
    };
    const uint32_t packedTint = u8(tintR) | (u8(tintG) << 8) | (u8(tintB) << 16) | (255u << 24);
    d.push.clip = glm::vec4(nearZ, farZ, glm::uintBitsToFloat(packedTint), 0.f);
    gbufferPassDraws.push_back(d);
}

void Graphics::endGBufferPass() {
    if (!gbufferPassActive) throw Exception("endGBufferPass: no active gbuffer pass");
    gbufferPassActive = false;

    auto *slot = currentGBufferSlot();
    if (!gbufferPipeline || !gbufferRenderPass || !slot || !slot->framebuffer) {
        gbufferPassDraws.clear();
        gbufferPending = false;
        if (renderControl_) renderControl_->getGBuffer()->clear();
        return;
    }

    gbufferPending = true;
    if (renderControl_) {
        Texture *albedo = &slot->albedoTex;
        renderControl_->getGBuffer()->setTargets(gbufferWidth, gbufferHeight, &slot->depthColorTex,
                                                 &slot->normalTex, albedo, &slot->depthTex);
    }
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

void Graphics::setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount) {
    mesh3dTexBombScale = cellScale > 1e-3f ? cellScale : 1e-3f;
    mesh3dTexBombStrength = strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength);
    mesh3dTexBombRot = rotAmount < 0.f ? 0.f : (rotAmount > 1.f ? 1.f : rotAmount);
    mesh3dFrameUbo.texBomb =
        glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
}

void Graphics::setMesh3DParallax(float scale, float minLayers, float maxLayers) {
    mesh3dParallaxScale = scale < 0.f ? 0.f : (scale > 0.25f ? 0.25f : scale);
    float minL = minLayers < 1.f ? 1.f : minLayers;
    float maxL = maxLayers < minL ? minL : maxLayers;
    if (maxL > 64.f) maxL = 64.f;
    mesh3dParallaxMinLayers = minL;
    mesh3dParallaxMaxLayers = maxL;
    mesh3dFrameUbo.parallax =
        glm::vec4(mesh3dParallaxScale, mesh3dParallaxMinLayers, mesh3dParallaxMaxLayers, 0.f);
}

void Graphics::setMesh3DLighting(const Lighting3DPack &pack) {
    mesh3dLighting = pack;
    mesh3dFrameUbo.ambient = glm::vec4(glm::vec3(pack.ambient), mesh3dMetallic);
    const int n = std::max(0, std::min(pack.count, Lighting3DPack::kMaxLights));
    mesh3dFrameUbo.lightDir.w = float(n);
    int dirI = -1;
    for (int i = 0; i < n; ++i) {
        if (pack.lights[i].posRadius.w <= 0.f) {
            dirI = i;
            break;
        }
    }
    if (dirI >= 0) {
        glm::vec3 d(pack.lights[dirI].posRadius);
        if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
        else d = glm::normalize(d);
        mesh3dFrameUbo.lightDir = glm::vec4(d, float(n));
        mesh3dFrameUbo.lightColor =
            glm::vec4(glm::vec3(pack.lights[dirI].color), mesh3dFrameUbo.lightColor.w);
    } else {
        // No directional: zero the legacy primary slot. mesh3d.frag always shades it.
        mesh3dFrameUbo.lightDir = glm::vec4(0.f, 1.f, 0.f, float(n));
        mesh3dFrameUbo.lightColor =
            glm::vec4(0.f, 0.f, 0.f, mesh3dFrameUbo.lightColor.w);
    }
}

void Graphics::ensureFlatNormalTexture3D() {
    if (flatNormalTexture3D) return;
    const uint8_t px[4] = {128, 128, 255, 255};
    flatNormalTexture3D = newTexture(1, 1, px);
}

void Graphics::ensureFlatHeightTexture3D() {
    if (flatHeightTexture3D) return;
    // Mid-gray height: no relief when scale>0 without a real height map.
    const uint8_t px[4] = {128, 128, 128, 255};
    flatHeightTexture3D = newTexture(1, 1, px);
}

vkb::BoundSet Graphics::mesh3dSetFor(GpuTexture *gpuTex, GpuTexture *normalTex, GpuTexture *envTex,
                                     GpuTexture *heightTex, Mesh3dFrameSlots &fslots,
                                     size_t uboSlot) {
    ASSERT(gpuTex != nullptr);
    ASSERT(normalTex != nullptr);
    ASSERT(envTex != nullptr);
    ASSERT(heightTex != nullptr);
    ASSERT(currentShadowArrayView());
    while (fslots.slots.size() <= uboSlot) {
        fslots.slots.emplace_back();
        auto &s = fslots.slots.back();
        s.ubo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(Mesh3DUBO),
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
        s.shadowUbo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(ShadowUBO),
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    auto &slot = fslots.slots[uboSlot];
    Mesh3dSetKey key{gpuTex, normalTex, envTex, heightTex};
    auto it = slot.sets.find(key);
    if (it != slot.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &mesh3dSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    vkb::DescriptorSetUpdater updater(12, 12, 0);
    updater.beginDescriptorSet(unbound)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.ubo.buffer, 0, sizeof(Mesh3DUBO))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpuTex->sampler, gpuTex->imageView()))
        .beginImages(2, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(normalTex->sampler, normalTex->imageView()))
        .beginImages(3, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(envTex->sampler, envTex->imageView()))
        .beginBuffers(4, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.shadowUbo.buffer, 0, sizeof(ShadowUBO))
        .beginImages(5, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(shadowSampler, currentShadowArrayView()))
        .beginImages(6, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(heightTex->sampler, heightTex->imageView()))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    slot.sets.emplace(key, bound);
    return bound;
}

void Graphics::setMesh3DViewProj(const glm::mat4 &viewProj) {
    mesh3dFrameUbo.mvp = viewProj;
}

void Graphics::setMesh3DView(const glm::mat4 &view) {
    mesh3dFrameUbo.view = view;
}

void Graphics::setMesh3DClip(float nearZ, float farZ) {
    const float n = nearZ > 1e-4f ? nearZ : 0.1f;
    const float f = farZ > n ? farZ : n + 1.f;
    mesh3dFrameUbo.clipInfo = glm::vec4(n, f, mesh3dFrameUbo.clipInfo.z, mesh3dFrameUbo.clipInfo.w);
    mesh3dClustered.clipInfo.x = n;
    mesh3dClustered.clipInfo.y = f;
}

// --- Mesh creation and drawing ------------------------------------------------

Mesh *Graphics::newMeshFromAssimp(const ::aiMesh &mesh) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshFromAssimp: graphics not initialized");
    if (mesh.mNumVertices == 0 || mesh.mNumFaces == 0)
        throw Exception("newMeshFromAssimp: empty mesh");

    std::vector<MeshVertex> verts;
    verts.reserve(mesh.mNumVertices);
    std::vector<float> basePos;
    std::vector<float> baseNrm;
    std::vector<float> baseUv;
    basePos.reserve(mesh.mNumVertices * 3);
    baseNrm.reserve(mesh.mNumVertices * 3);
    baseUv.reserve(mesh.mNumVertices * 2);
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
        basePos.push_back(v.pos.x);
        basePos.push_back(v.pos.y);
        basePos.push_back(v.pos.z);
        baseNrm.push_back(v.normal.x);
        baseNrm.push_back(v.normal.y);
        baseNrm.push_back(v.normal.z);
        baseUv.push_back(v.uv.x);
        baseUv.push_back(v.uv.y);
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

    auto gpu = uploadGpuMesh(device, frameToken(), verts, indices);
    auto handle = makeMeshHandle(*gpu);
    handle->initMorphBase(int(mesh.mNumVertices), basePos.data(), baseNrm.data(), baseUv.data());
    // Assimp morph targets (VRM / glTF blend shapes often land here).
    for (unsigned m = 0; m < mesh.mNumAnimMeshes; ++m) {
        const aiAnimMesh *am = mesh.mAnimMeshes[m];
        if (!am || !am->mVertices || am->mNumVertices != mesh.mNumVertices) continue;
        std::string name = am->mName.length ? am->mName.C_Str() : ("morph" + std::to_string(m));
        std::vector<float> absPos(size_t(am->mNumVertices) * 3u);
        for (unsigned i = 0; i < am->mNumVertices; ++i) {
            absPos[size_t(i) * 3u + 0] = am->mVertices[i].x;
            absPos[size_t(i) * 3u + 1] = am->mVertices[i].y;
            absPos[size_t(i) * 3u + 2] = am->mVertices[i].z;
        }
        handle->addMorphTargetAbsolute(name, absPos.data());
    }
    handle->markMorphClean();
    Mesh *raw = handle.get();
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::move(handle));
    return raw;
}

Mesh *Graphics::newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshFromAssimp: graphics not initialized");
    if (mesh.mNumVertices == 0 || mesh.mNumFaces == 0)
        throw Exception("newMeshFromAssimp: empty mesh");

    std::vector<aiVector3D> positions(mesh.mNumVertices);
    std::vector<aiVector3D> normals(mesh.mNumVertices);
    aiMatrix3x3 nmat(worldTransform);
    const float ndet = nmat.Determinant();
    if (std::fabs(ndet) > 1e-8f) {
        nmat.Inverse();
        nmat.Transpose();
    }
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        positions[i] = worldTransform * mesh.mVertices[i];
        if (mesh.HasNormals()) {
            normals[i] = nmat * mesh.mNormals[i];
            normals[i].Normalize();
        } else {
            normals[i] = aiVector3D(0.f, 1.f, 0.f);
        }
    }

    // Negative determinant mirrors the mesh: winding flips while inverse-transpose
    // keeps normals consistent, so back-face cull would hide the visible side.
    const bool flipWinding = worldTransform.Determinant() < 0.f;
    std::vector<aiFace> flippedFaces;
    std::vector<unsigned> flippedIdx;
    if (flipWinding) {
        flippedFaces.resize(mesh.mNumFaces);
        flippedIdx.resize(size_t(mesh.mNumFaces) * 3u);
        for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
            const aiFace &src = mesh.mFaces[f];
            aiFace &dst = flippedFaces[f];
            dst.mNumIndices = src.mNumIndices;
            if (src.mNumIndices == 3 && src.mIndices) {
                unsigned *idx = flippedIdx.data() + size_t(f) * 3u;
                idx[0] = src.mIndices[0];
                idx[1] = src.mIndices[2];
                idx[2] = src.mIndices[1];
                dst.mIndices = idx;
            } else {
                dst.mIndices = src.mIndices;
            }
        }
    }

    // Non-owning view — do not let aiMesh destructor free borrowed pointers.
    aiMesh tmp;
    std::memset(&tmp, 0, sizeof(tmp));
    tmp.mPrimitiveTypes = mesh.mPrimitiveTypes;
    tmp.mNumVertices = mesh.mNumVertices;
    tmp.mVertices = positions.data();
    tmp.mNormals = normals.data();
    tmp.mNumFaces = mesh.mNumFaces;
    tmp.mFaces = flipWinding ? flippedFaces.data() : mesh.mFaces;
    tmp.mMaterialIndex = mesh.mMaterialIndex;
    tmp.mNumAnimMeshes = mesh.mNumAnimMeshes;
    tmp.mAnimMeshes = mesh.mAnimMeshes;
    if (mesh.HasTextureCoords(0)) {
        tmp.mTextureCoords[0] = mesh.mTextureCoords[0];
        tmp.mNumUVComponents[0] = mesh.mNumUVComponents[0];
    }
    Mesh *out = newMeshFromAssimp(tmp);
    tmp.mVertices = nullptr;
    tmp.mNormals = nullptr;
    tmp.mFaces = nullptr;
    tmp.mTextureCoords[0] = nullptr;
    tmp.mAnimMeshes = nullptr;
    return out;
}

Mesh *Graphics::newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                  int vertexCount, const uint32_t *indices, int indexCount) {
    ASSERT(initialized);
    if (!initialized) throw Exception("newMeshFromArrays: graphics not initialized");
    if (!posXYZ || vertexCount <= 0) throw Exception("newMeshFromArrays: empty positions");
    if (!indices || indexCount < 3) throw Exception("newMeshFromArrays: empty indices");
    if (indexCount % 3 != 0) throw Exception("newMeshFromArrays: indexCount must be multiple of 3");

    std::vector<MeshVertex> verts(static_cast<size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        MeshVertex &v = verts[static_cast<size_t>(i)];
        v.pos = {posXYZ[size_t(i) * 3u], posXYZ[size_t(i) * 3u + 1u], posXYZ[size_t(i) * 3u + 2u]};
        if (nrmXYZ)
            v.normal = {nrmXYZ[size_t(i) * 3u], nrmXYZ[size_t(i) * 3u + 1u],
                        nrmXYZ[size_t(i) * 3u + 2u]};
        else
            v.normal = {0.f, 1.f, 0.f};
        if (uvST)
            v.uv = {uvST[size_t(i) * 2u], uvST[size_t(i) * 2u + 1u]};
        else
            v.uv = {0.f, 0.f};
    }

    std::vector<uint32_t> idx(indices, indices + indexCount);
    for (uint32_t id : idx) {
        if (int(id) >= vertexCount) throw Exception("newMeshFromArrays: index out of range");
    }

    auto gpu = uploadGpuMesh(device, frameToken(), verts, idx);
    auto handle = makeMeshHandle(*gpu);
    Mesh *raw = handle.get();
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::move(handle));
    return raw;
}

bool Graphics::bakeMeshMorph(Mesh *mesh) {
    if (!mesh || !mesh->gpuHandle || !mesh->hasMorphData() || !mesh->isMorphDirty()) return false;
    if (!initialized) return false;

    std::vector<float> pos;
    std::vector<float> nrm;
    mesh->computeMorphedPositions(pos, nrm);
    const int vc = mesh->getVertexCount();
    if (vc <= 0 || int(pos.size()) < vc * 3) return false;

    std::vector<MeshVertex> verts(static_cast<size_t>(vc));
    const auto &uv = mesh->baseUv();
    for (int i = 0; i < vc; ++i) {
        MeshVertex &v = verts[static_cast<size_t>(i)];
        v.pos = {pos[size_t(i) * 3u + 0], pos[size_t(i) * 3u + 1], pos[size_t(i) * 3u + 2]};
        if (int(nrm.size()) >= (i + 1) * 3)
            v.normal = {nrm[size_t(i) * 3u + 0], nrm[size_t(i) * 3u + 1], nrm[size_t(i) * 3u + 2]};
        else
            v.normal = {0.f, 1.f, 0.f};
        if (int(uv.size()) >= (i + 1) * 2)
            v.uv = {uv[size_t(i) * 2u + 0], uv[size_t(i) * 2u + 1]};
        else
            v.uv = {0.f, 0.f};
    }

    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    // Host-visible VBO is shared across frames. Overwriting it while an
    // in-flight draw still reads the previous morph is a GPU page-fault / TDR.
    waitForSharedGpuResources();
    gpu->vertices.updateLocal(vkb::FrameSlot::gpuIdle(), verts);
    mesh->markMorphClean();
    return true;
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
    // faces (object-space CCW; mesh pipelines use Clockwise frontFace after the
    // Vulkan Y flip in perspectiveVulkanRH_ZO). Use the same winding that
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

    auto gpu = uploadGpuMesh(device, frameToken(), verts, indices);
    auto handle = makeMeshHandle(*gpu);
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

    auto gpu = uploadGpuMesh(device, frameToken(), verts, indices);
    auto handle = makeMeshHandle(*gpu);
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

    ensureFlatHeightTexture3D();
    Texture *htex = mesh3dHeightTexture ? mesh3dHeightTexture : flatHeightTexture3D;
    if (!htex || !htex->gpuHandle) throw Exception("drawMesh: missing height texture");
    auto *gpuHeight = static_cast<GpuTexture *>(htex->gpuHandle);

    ensureDefaultEnvCubemap();
    Texture *envTex = mesh3dEnvTexture ? mesh3dEnvTexture : defaultEnvCubemap;
    if (!envTex || !envTex->gpuHandle) throw Exception("drawMesh: missing env cubemap");
    auto *gpuEnv = static_cast<GpuTexture *>(envTex->gpuHandle);
    if (!gpuEnv->isCube) throw Exception("drawMesh: env texture is not a cubemap");
    const float envIntensity = (mesh3dEnvTexture && mesh3dEnvIntensity > 0.f) ? mesh3dEnvIntensity : 0.f;

    const bool useClustered = mesh3dClusteredActive && !shader && mesh3dClusteredPipeline;
    auto &cb = currentPresentCb();

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
        ubo.texBomb = glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
        ubo.parallax =
            glm::vec4(mesh3dParallaxScale, mesh3dParallaxMinLayers, mesh3dParallaxMaxLayers, 0.f);

        auto &cfslots = currentMesh3dClusteredFrameSlots();
        const size_t slot = cfslots.drawIndex++;
        vk::DescriptorSet set =
            mesh3dClusteredSetFor(gpuTex, gpuNormal, gpuEnv, gpuHeight, cfslots, slot);
        cfslots.slots[slot].ubo.updateLocal(frameToken(), ubo);
        cfslots.slots[slot].shadowUbo.updateLocal(frameToken(), makeShadowUbo());

        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, mesh3dClusteredPipeline);
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, mesh3dClusteredPipelineLayout, 0, 1,
                              &set, 0, nullptr);
        drawIndexedMesh(cb, *gpuMesh);
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
    ubo.texBomb = glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
    ubo.parallax =
        glm::vec4(mesh3dParallaxScale, mesh3dParallaxMinLayers, mesh3dParallaxMaxLayers, 0.f);
    for (int i = 0; i < lightCount; ++i) ubo.lights[i] = mesh3dLighting.lights[i];
    int dirI = -1;
    for (int i = 0; i < lightCount; ++i) {
        if (mesh3dLighting.lights[i].posRadius.w <= 0.f) {
            dirI = i;
            break;
        }
    }
    if (dirI >= 0) {
        glm::vec3 d(mesh3dLighting.lights[dirI].posRadius);
        if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
        else d = glm::normalize(d);
        ubo.lightDir = glm::vec4(d, float(lightCount));
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dLighting.lights[dirI].color), envIntensity);
    } else {
        ubo.lightDir = glm::vec4(0.f, 1.f, 0.f, float(lightCount));
        ubo.lightColor = glm::vec4(0.f, 0.f, 0.f, envIntensity);
    }

    auto &fslots = currentMesh3dFrameSlots();
    const size_t slot = fslots.drawIndex++;
    vk::DescriptorSet set = mesh3dSetFor(gpuTex, gpuNormal, gpuEnv, gpuHeight, fslots, slot);
    fslots.slots[slot].ubo.updateLocal(frameToken(), ubo);
    fslots.slots[slot].shadowUbo.updateLocal(frameToken(), makeShadowUbo());

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
    drawIndexedMesh(cb, *gpuMesh);
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
