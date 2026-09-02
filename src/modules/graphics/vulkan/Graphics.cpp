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
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "common/BootWarmup.h"
#include "common/CrashLog.h"
#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "graphics/GraphicsCapabilities.h"
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

namespace {

bool wantVulkanValidation() {
#if defined(EVENGINE_IOS)
    return false;
#else
    const char *vkVal = std::getenv("EVENGINE_VULKAN_VALIDATION");
    return vkVal && vkVal[0] != '\0' && vkVal[0] != '0';
#endif
}

void disableImplicitLayersIfSafe() {
    if (wantVulkanValidation()) return;
    if (const char *existing = std::getenv("VK_LOADER_LAYERS_DISABLE")) {
        if (existing[0] != '\0') return;
    }
    if (const char *layers = std::getenv("VK_INSTANCE_LAYERS")) {
        if (layers[0] != '\0') return;
    }
#if defined(_WIN32)
    _putenv_s("VK_LOADER_LAYERS_DISABLE", "~implicit~");
#else
    setenv("VK_LOADER_LAYERS_DISABLE", "~implicit~", 0);
#endif
}

bool extensionAvailable(const std::vector<vk::ExtensionProperties> &props, const char *name) {
    for (const auto &p : props) {
        if (std::strcmp(p.extensionName, name) == 0) return true;
    }
    return false;
}

void addIfAvailable(std::vector<const char *> &exts, const std::vector<vk::ExtensionProperties> &props,
                    const char *name) {
    if (extensionAvailable(props, name)) exts.push_back(name);
}

std::vector<const char *> collectFastInstanceExtensions(const std::vector<vk::ExtensionProperties> &props,
                                                        vk::InstanceCreateFlags *flagsOut) {
    std::vector<const char *> exts;
    addIfAvailable(exts, props, "VK_KHR_surface");
#if defined(_WIN32)
    addIfAvailable(exts, props, "VK_KHR_win32_surface");
#elif defined(__ANDROID__)
    addIfAvailable(exts, props, "VK_KHR_android_surface");
#elif defined(__APPLE__)
    addIfAvailable(exts, props, "VK_EXT_metal_surface");
    if (extensionAvailable(props, "VK_KHR_portability_enumeration")) {
        exts.push_back("VK_KHR_portability_enumeration");
        if (flagsOut) *flagsOut |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }
#else
    addIfAvailable(exts, props, "VK_KHR_xcb_surface");
    addIfAvailable(exts, props, "VK_KHR_xlib_surface");
    addIfAvailable(exts, props, "VK_KHR_wayland_surface");
#endif
    return exts;
}

void initVulkanDispatcher() {
#if defined(EVENGINE_MACOSX)
    if (PFN_vkGetInstanceProcAddr sdlGpa =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr())) {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(sdlGpa);
        vkb::InstanceBuilder::loaded = true;
        return;
    }
#endif
    if (!vkb::InstanceBuilder::loaded) {
        vkb::InstanceBuilder::loadDefault();
        vkb::InstanceBuilder::loaded = true;
    }
}

vkb::Instance createInstanceFast() {
    initVulkanDispatcher();
    disableImplicitLayersIfSafe();
    // Enumerate instance extensions only. vk-bootstrap's SystemInfo::query() also
    // walks every implicit layer (and each layer's extensions), which is the
    // bulk of "instance + surface" on a Windows SDK install.
    const auto props = vk::enumerateInstanceExtensionProperties();
    vk::InstanceCreateFlags flags{};
    const auto exts = collectFastInstanceExtensions(props, &flags);

    if (wantVulkanValidation()) {
        vkb::InstanceBuilder builder;
        builder.require_api_version(1, 0);
        builder.request_validation_layers();
        builder.use_default_debug_messenger();
        for (auto *e : exts) builder.enable_extension(e);
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_IOS)
        builder.set_headless(true);
        builder.add_flags(flags);
#endif
        std::printf("EVEngine: Vulkan validation layers enabled (EVENGINE_VULKAN_VALIDATION)\n");
        return builder.build();
    }

    vk::ApplicationInfo app{};
    app.pApplicationName = "EVEngine";
    app.pEngineName = "EVEngine";
    app.apiVersion = VK_MAKE_VERSION(1, 0, 0);

    vk::InstanceCreateInfo ci{};
    ci.flags = flags;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    vkb::Instance inst;
    inst.instance = vk::createInstance(ci);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(inst.instance);
    return inst;
}

struct InstanceWarmup {
    std::mutex mu;
    std::future<vkb::Instance> future;
    bool started = false;
    bool consumed = false;
};

InstanceWarmup &instanceWarmup() {
    static InstanceWarmup state;
    return state;
}

void startVulkanInstanceWarmupImpl() {
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_IOS)
    // MoltenVK / SDL loader affinity is main-thread on Apple.
    return;
#else
    auto &w = instanceWarmup();
    std::lock_guard<std::mutex> lock(w.mu);
    if (w.started) return;
    w.started = true;
    w.future = std::async(std::launch::async, [] {
        StartupStage stage("  vulkan: instance (warmup thread)");
        return createInstanceFast();
    });
#endif
}

vkb::Instance consumeWarmedInstance() {
#if defined(EVENGINE_MACOSX) || defined(EVENGINE_IOS)
    StartupStage stage("  vulkan: instance");
    return createInstanceFast();
#else
    startVulkanInstanceWarmupImpl();
    std::future<vkb::Instance> fut;
    {
        auto &w = instanceWarmup();
        std::lock_guard<std::mutex> lock(w.mu);
        if (w.consumed) {
            throw Exception("Vulkan instance warmup already consumed");
        }
        w.consumed = true;
        fut = std::move(w.future);
    }
    StartupStage waitStage("  vulkan: wait instance warmup");
    return fut.get();
#endif
}

void discardWarmedInstance() noexcept {
#if !defined(EVENGINE_MACOSX) && !defined(EVENGINE_IOS)
    std::future<vkb::Instance> fut;
    {
        auto &w = instanceWarmup();
        std::lock_guard<std::mutex> lock(w.mu);
        if (!w.started || w.consumed) return;
        w.consumed = true;
        fut = std::move(w.future);
    }
    try {
        vkb::Instance warmed = fut.get();
        if (static_cast<VkInstance>(warmed.instance) != VK_NULL_HANDLE) warmed.destroy();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[vulkan] instance warmup cleanup failed: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "[vulkan] instance warmup cleanup failed with an unknown error\n");
    }
#endif
}

struct RegisterVulkanWarmup {
    RegisterVulkanWarmup() {
        eve::boot::registerVulkanInstanceWarmup(&startVulkanInstanceWarmupImpl);
    }
} gRegisterVulkanWarmup;

}  // namespace

// --- Backend lifecycle and frame orchestration --------------------------------

#if defined(VKB_ENABLE_VMA)
VmaAllocatorOwner::~VmaAllocatorOwner() {
    if (allocator_) vmaDestroyAllocator(allocator_);
}

void VmaAllocatorOwner::create(const vkb::Instance &instance,
                               const vkb::PhysicalDevice &physicalDevice,
                               vkb::Device &device) {
    VmaAllocatorCreateInfo createInfo{};
    createInfo.instance = static_cast<VkInstance>(instance.instance);
    createInfo.physicalDevice = static_cast<VkPhysicalDevice>(physicalDevice.instance);
    createInfo.device = static_cast<VkDevice>(device.instance);
    VmaVulkanFunctions vulkanFunctions{};
    // The headless backend does not ask SDL to load Vulkan, so its loader
    // accessor may legitimately be null. Reuse the dispatcher that created
    // this instance; it is also the portable path on Android.
    vulkanFunctions.vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
    if (!vulkanFunctions.vkGetInstanceProcAddr) {
        throw Exception("VMA could not resolve vkGetInstanceProcAddr from the Vulkan dispatcher");
    }
    vulkanFunctions.vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vulkanFunctions.vkGetInstanceProcAddr(createInfo.instance, "vkGetDeviceProcAddr"));
    if (!vulkanFunctions.vkGetDeviceProcAddr) {
        throw Exception("VMA could not resolve vkGetDeviceProcAddr");
    }
    createInfo.pVulkanFunctions = &vulkanFunctions;
    // InstanceBuilder requests Vulkan 1.0. VMA requires this value to describe
    // the application's instance contract, not the physical device maximum.
    createInfo.vulkanApiVersion = VK_API_VERSION_1_0;
    const VkResult result = vmaCreateAllocator(&createInfo, &allocator_);
    if (result != VK_SUCCESS) throw Exception("vmaCreateAllocator failed: %d", int(result));
    device.attachVmaAllocator(allocator_);
}
#endif

std::string Graphics::getBackendName() const { return "vulkan"; }

Graphics::Graphics() = default;

Graphics::~Graphics() {
    detachGraphicsArtifactProvider(this);
    if (!initialized) {
        // The command-line boot path may start warmup before Graphics exists.
        // Join it if initialization never consumed the warmed instance.
        discardWarmedInstance();
        if (static_cast<VkInstance>(inst.instance) != VK_NULL_HANDLE) inst.destroy();
        return;
    }
    device->waitIdle();
    if (gpuQueryPool_) device->destroyQueryPool(gpuQueryPool_);
    gpuQueryPool_ = nullptr;
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
        if (g->swapchainOpaquePipeline) device->destroyPipeline(g->swapchainOpaquePipeline);
        if (g->offscreenOpaquePipeline) device->destroyPipeline(g->offscreenOpaquePipeline);
        if (g->mesh3dPipeline) device->destroyPipeline(g->mesh3dPipeline);
        if (g->mesh3dXrayPipeline) device->destroyPipeline(g->mesh3dXrayPipeline);
        if (g->mesh3dOffscreenPipeline)
            device->destroyPipeline(g->mesh3dOffscreenPipeline);
        if (g->mesh3dHdrOffscreenPipeline)
            device->destroyPipeline(g->mesh3dHdrOffscreenPipeline);
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
    if (sceneTonemapPipeline) device->destroyPipeline(sceneTonemapPipeline);
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
    if (hdrOffscreenTexPipeline) device->destroyPipeline(hdrOffscreenTexPipeline);
    if (hdrOffscreenOpaqueTexPipeline)
        device->destroyPipeline(hdrOffscreenOpaqueTexPipeline);
    if (hdrOffscreenRenderPass) device->destroyRenderPass(hdrOffscreenRenderPass);
    texSetLayoutUnique.reset();
    if (descriptorPool) device->destroyDescriptorPool(descriptorPool);
    if (uploadPool) device->destroyCommandPool(uploadPool);
    if (renderpass) device->destroyRenderPass(renderpass);
    if (surface) inst.instance.destroySurfaceKHR(surface);
}

void Graphics::createInstanceAndDevice(const std::vector<const char *> &extNames, void *nativeWindow,
                                       vk::SurfaceKHR *surfaceOut) {
    (void)extNames;
    if (static_cast<VkInstance>(inst.instance) == VK_NULL_HANDLE)
        inst = consumeWarmedInstance();

    if (nativeWindow != nullptr && surfaceOut != nullptr) {
        StartupStage stage("  vulkan: surface");
        auto *window = static_cast<SDL_Window *>(nativeWindow);
        VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(inst.instance), &rawSurface))
            throw Exception("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
        surface = rawSurface;
        *surfaceOut = rawSurface;
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
            // Record the GPU identity into the crash/error log before any Vulkan
            // work, so a device/driver crash can be tied to the exact GPU+driver.
            const auto &pp = phys.properties;
            const char *vendor = "unknown";
            switch (pp.vendorID) {
                case 0x10DE: vendor = "NVIDIA"; break;
                case 0x1002: vendor = "AMD"; break;
                case 0x8086: vendor = "Intel"; break;
                case 0x13B5: vendor = "ARM"; break;
                case 0x5143: vendor = "Qualcomm"; break;
                default: break;
            }
            char vendorHex[16], deviceHex[16], driverHex[16];
            std::snprintf(vendorHex, sizeof(vendorHex), "%04X", pp.vendorID);
            std::snprintf(deviceHex, sizeof(deviceHex), "%04X", pp.deviceID);
            std::snprintf(driverHex, sizeof(driverHex), "%08X", pp.driverVersion);
            const uint32_t maj = VK_API_VERSION_MAJOR(pp.apiVersion);
            const uint32_t min = VK_API_VERSION_MINOR(pp.apiVersion);
            const uint32_t pat = VK_API_VERSION_PATCH(pp.apiVersion);
            eve::recordLogEvent(
                "info",
                std::string("gpu: ") + vendor + " '" + std::string(pp.deviceName.data()) +
                    "' vendorId=0x" + vendorHex + " deviceId=0x" + deviceHex + " api=" +
                    std::to_string(maj) + "." + std::to_string(min) + "." + std::to_string(pat) +
                    " driver=0x" + driverHex + (nativeWindow ? "" : " [headless]"));
        }
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
#if defined(VKB_ENABLE_VMA)
        vmaAllocatorOwner_.create(inst, phys, device);
#endif
        maxSamplerAnisotropy = device.caps.maxSamplerAnisotropy;
        eve::recordLogEvent("info",
            "gpu: logical device created (gpuDriven=" +
            std::string(gpuDrivenCaps_.gpuDrivenAvailable() ? "on" : "off") +
#if defined(VKB_ENABLE_VMA)
            ", allocator=VMA" +
#else
            ", allocator=native" +
#endif
            ", maxAniso=" + std::to_string(maxSamplerAnisotropy) + ")");
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
    eve::recordLogEvent("info", "gfx: graphics initialized (headless)");
}

void Graphics::initWithWindow(void *nativeWindow) {
    eve::cap::provide<eve::service::IGpuTimer>(this);
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
        eve::recordLogEvent("info", "gfx: swapchain + solid pipelines created");
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
    eve::recordLogEvent("info", "gfx: graphics initialized (window)");
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
        if (presentRecording) {
            writeGpuTimestampBegin();
            return true;
        }
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

// ---- GPU frame timing ------------------------------------------------------

void Graphics::initGpuTiming() {
    if (gpuQueryPool_) return;
    try {
        timestampPeriod_ = device.physical_device.properties.limits.timestampPeriod;
        vk::QueryPoolCreateInfo ci;
        ci.queryType = vk::QueryType::eTimestamp;
        ci.queryCount = 2;
        gpuQueryPool_ = device->createQueryPool(ci);
        gpuTimingReady_ = true;
    } catch (...) {
        gpuTimingReady_ = false;
        gpuQueryPool_ = nullptr;
    }
}

void Graphics::writeGpuTimestampBegin() {
    if (!gpuTimingReady_ || !presentRecording) return;
    vk::CommandBuffer cb = presentRecording.commandBuffer();
    cb.resetQueryPool(gpuQueryPool_, 0, 2);
    cb.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, gpuQueryPool_, 0);
}

void Graphics::writeGpuTimestampEnd() {
    if (!gpuTimingReady_ || !presentRecording) return;
    vk::CommandBuffer cb = presentRecording.commandBuffer();
    cb.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, gpuQueryPool_, 1);
}

void Graphics::readGpuFrameTiming() {
    if (!gpuTimingReady_ || !gpuQueryPool_) return;
    // drawFrame() waits on the in-flight fence, so results are valid here.
    std::array<uint64_t, 2> ts{};
    const auto r = device->getQueryPoolResults(
        gpuQueryPool_, 0, 2, sizeof(ts), ts.data(), sizeof(uint64_t),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    if (r == vk::Result::eSuccess && ts[1] > ts[0])
        gpuFrameMs_ = float(double(ts[1] - ts[0]) * double(timestampPeriod_) / 1'000'000.0);
    else
        gpuFrameMs_ = 0.f;
}

bool Graphics::gpuTimingAvailable() const { return gpuTimingReady_ && gpuFrameMs_ > 0.f; }

float Graphics::gpuFrameMs() const { return gpuFrameMs_; }

}  // namespace eve::graphics::vulkan
