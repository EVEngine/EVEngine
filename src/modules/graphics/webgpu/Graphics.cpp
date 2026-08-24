#include "graphics/webgpu/Graphics.h"
#include "graphics/Batcher.h"
#include "graphics/ClusteredLight.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/webgpu/Canvas.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include "common/Exception.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/gtc/constants.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace eve::graphics::webgpu {

namespace {

/** Build a WGPUStringView from a C string (null-safe, length auto-computed). */
WGPUStringView sv(const char *s) {
    return WGPUStringView{s, s ? std::strlen(s) : 0};
}

// Forward declaration for the readback helper (defined in the Readback section).
bool copyTextureToCpu(wgpu::Instance &instance, wgpu::Device &device, wgpu::Queue &queue,
                      wgpu::Texture src, int width, int height, std::vector<uint8_t> &outRgba);

// A shared default sampler used when no texture is provided.
wgpu::Sampler createLinearSampler(wgpu::Device &dev) {
    WGPUSamplerDescriptor d{};
    d.label = sv("eve_linear");
    d.addressModeU = WGPUAddressMode_ClampToEdge;
    d.addressModeV = WGPUAddressMode_ClampToEdge;
    d.addressModeW = WGPUAddressMode_ClampToEdge;
    d.magFilter = WGPUFilterMode_Linear;
    d.minFilter = WGPUFilterMode_Linear;
    d.mipmapFilter = WGPUMipmapFilterMode_Linear;
    d.lodMinClamp = 0.f;
    d.lodMaxClamp = 1000.f;
    d.maxAnisotropy = 1;
    return dev.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&d));
}

int nextPOT(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

}  // namespace

Graphics::Graphics() {
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        uboArenas.emplace_back();
        vertexArenas.emplace_back();
    }
}

Graphics::~Graphics() = default;

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void Graphics::initHeadless(int width, int height) {
    if (deviceInitDone) {
        if (headless_) {
            setViewportSize(width, height, width, height);
            return;
        }
        throw Exception("Graphics::initHeadless: already initialized with a window");
    }
    if (width <= 0 || height <= 0) throw Exception("Graphics::initHeadless: invalid size");

    createInstanceAndAdapter();
    requestDevice();
    queue = device.GetQueue();
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

    createPipelineResources();
    createShadowResources();
    createDefaultTextures();
    setViewportSize(width, height, width, height);
    createSceneColorResources(width, height);
    headless_ = true;
    initialized = true;
    deviceInitDone = true;
}

void Graphics::initWithWindow(void *nativeWindow) {
    sdlWindow = nativeWindow;
    if (deviceInitDone) return;

    createInstanceAndAdapter();
    requestDevice();
    if (!device) throw Exception("WebGPU: device request failed");

    queue = device.GetQueue();

    // ---- Surface ----
    WGPUSurfaceDescriptor surfDesc{};
    surfDesc.label = sv("eve_surface");
#if defined(__EMSCRIPTEN__)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSel{};
    canvasSel.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSel.selector = sv("#canvas");
    surfDesc.nextInChain = &canvasSel.chain;
    surface = instance.CreateSurface(reinterpret_cast<const wgpu::SurfaceDescriptor*>(&surfDesc));
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
#else
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    if (!SDL_GetWindowWMInfo(static_cast<SDL_Window *>(sdlWindow), &wminfo))
        throw Exception("WebGPU: SDL_GetWindowWMInfo failed: %s", SDL_GetError());
#if defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND winChain{};
    winChain.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    winChain.hwnd = wminfo.info.win.window;
    winChain.hinstance = GetModuleHandle(nullptr);
    surfDesc.nextInChain = &winChain.chain;
    surface = instance.CreateSurface(reinterpret_cast<const wgpu::SurfaceDescriptor*>(&surfDesc));
#elif defined(__linux__)
    if (wminfo.subsystem == SDL_SYSWM_X11) {
        WGPUSurfaceSourceXlibWindow x11Chain{};
        x11Chain.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        x11Chain.display = wminfo.info.x11.display;
        x11Chain.window = wminfo.info.x11.window;
        surfDesc.nextInChain = &x11Chain.chain;
        surface = instance.CreateSurface(reinterpret_cast<const wgpu::SurfaceDescriptor*>(&surfDesc));
    } else if (wminfo.subsystem == SDL_SYSWM_WAYLAND) {
        WGPUSurfaceSourceWaylandSurface wlChain{};
        wlChain.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
        wlChain.display = wminfo.info.wl.display;
        wlChain.surface = wminfo.info.wl.surface;
        surfDesc.nextInChain = &wlChain.chain;
        surface = instance.CreateSurface(reinterpret_cast<const wgpu::SurfaceDescriptor*>(&surfDesc));
    } else {
        throw Exception("WebGPU: unsupported SDL window subsystem on Linux");
    }
#elif defined(__APPLE__)
    throw Exception("WebGPU: native macOS surface (Metal layer) not yet supported; "
                    "use the Emscripten browser build or the Vulkan backend on macOS");
#else
    throw Exception("WebGPU: unsupported native platform for surface creation");
#endif
    if (!surface) throw Exception("WebGPU: surface creation failed");
    // Preferred surface format comes from wgpuSurfaceGetCapabilities in the
    // current ABI (Surface::GetPreferredFormat was removed).
    WGPUSurfaceCapabilities caps{};
    if (wgpuSurfaceGetCapabilities(surface.Get(), adapter.Get(), &caps) == WGPUStatus_Success &&
        caps.formatCount > 0) {
        surfaceFormat = caps.formats[0];
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
#endif

    swapchainConfigured = false;
    // Bind group layouts / pipelines must exist before the default textures
    // (texture bind groups reference the 2D / mesh3d layouts), and the shadow
    // depth array must exist before mesh bind groups are built at flush time.
    createPipelineResources();
    createShadowResources();
    createDefaultTextures();
    initialized = true;
    deviceInitDone = true;
}

void Graphics::createInstanceAndAdapter() {
    instance = wgpu::CreateInstance();
    if (!instance) throw Exception("WebGPU: wgpuCreateInstance failed");

    WGPURequestAdapterOptions opts{};
    opts.compatibleSurface = surface.Get();
    opts.powerPreference = WGPUPowerPreference_HighPerformance;

    adapterReceived = false;
    WGPURequestAdapterCallbackInfo cbInfo{};
    cbInfo.nextInChain = nullptr;
    cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    cbInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView msg,
                         void *userdata1, void * /*userdata2*/) {
        auto *self = static_cast<Graphics *>(userdata1);
        if (status == WGPURequestAdapterStatus_Success && a) {
            self->adapter = wgpu::Adapter(a);
        } else {
            self->adapterError =
                msg.data ? std::string(msg.data, msg.length) : "unknown adapter error";
        }
        self->adapterReceived.store(true);
    };
    cbInfo.userdata1 = this;
    cbInfo.userdata2 = nullptr;

    wgpuInstanceRequestAdapter(instance.Get(), &opts, cbInfo);
    waitForAdapter();
    if (!adapter) {
        throw Exception("WebGPU: no adapter found (%s)", adapterError.c_str());
    }
}

void Graphics::requestDevice() {
    WGPUDeviceDescriptor devDesc{};
    devDesc.label = sv("eve_device");
    devDesc.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const *, WGPUErrorType type, WGPUStringView message, void *, void *) {
            std::fprintf(stderr, "[webgpu] uncaptured error type=%d: %.*s\n", int(type),
                         int(message.length), message.data ? message.data : "");
        };

    deviceReceived = false;
    WGPURequestDeviceCallbackInfo cbInfo{};
    cbInfo.nextInChain = nullptr;
    cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    cbInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView msg,
                         void *userdata1, void * /*userdata2*/) {
        auto *self = static_cast<Graphics *>(userdata1);
        if (status == WGPURequestDeviceStatus_Success && d) {
            self->device = wgpu::Device(d);
            // Default device limits support 8+ uniform/vertex/storage buffers.
            self->deviceReceived.store(true);
            self->deviceError.clear();
        } else {
            self->deviceError =
                msg.data ? std::string(msg.data, msg.length) : "unknown device error";
            self->deviceReceived.store(true);
        }
    };
    cbInfo.userdata1 = this;
    cbInfo.userdata2 = nullptr;

    wgpuAdapterRequestDevice(adapter.Get(), &devDesc, cbInfo);
    waitForDevice();
    if (!device) {
        throw Exception("WebGPU: device request failed (%s)", deviceError.c_str());
    }
}

void Graphics::waitForAdapter() {
    while (!adapterReceived.load()) {
#if defined(__EMSCRIPTEN__)
        // Yield to the browser event loop so the JS requestAdapter promise can
        // resolve and fire the callback, then flush it with ProcessEvents.
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(instance.Get());
    }
}

void Graphics::waitForDevice() {
    while (!deviceReceived.load()) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(instance.Get());
    }
}

void Graphics::setVSync(bool enabled) {
    if (vsyncEnabled == enabled) return;
    eve::graphics::Graphics::setVSync(enabled);
    markSwapchainDirty();
}

void Graphics::setViewportSize(int width, int height, int pixelwidth, int pixelheight) {
    // Mirror the Vulkan backend: keep the base-class viewport members in sync
    // so getWidth()/getPixelWidth() (used by RenderSystem3D for the GBuffer
    // size) return the real dimensions instead of 0.
    this->width = width;
    this->height = height;
    this->pixelWidth = pixelwidth;
    this->pixelHeight = pixelheight;
    logicalW = width;
    logicalH = height;
    pixelW = pixelwidth;
    pixelH = pixelheight;
    if (width <= 0 || height <= 0) return;
    if (initialized) configureSurface(width, height);
}

void Graphics::configureSurface(int width, int height) {
    if (!surface || !device || width <= 0 || height <= 0) return;
    WGPUSurfaceConfiguration cfg{};
    cfg.nextInChain = nullptr;
    cfg.device = device.Get();
    cfg.width = static_cast<uint32_t>(width);
    cfg.height = static_cast<uint32_t>(height);
    cfg.format = surfaceFormat;
    cfg.usage = WGPUTextureUsage_RenderAttachment;
    cfg.viewFormatCount = 0;
    cfg.viewFormats = nullptr;
    // emdawnwebgpu only accepts Fifo / Undefined present modes.
    cfg.presentMode = WGPUPresentMode_Fifo;
    // Auto (0) maps to an empty slot in emdawnwebgpu's JS CompositeAlphaMode
    // table �?`alphaMode: undefined`, which some browsers reject. Use Opaque,
    // which maps to the JS value 'opaque' used by the working replica.
    cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
    surface.Configure(reinterpret_cast<const wgpu::SurfaceConfiguration*>(&cfg));
    swapchainConfigured = true;
    // Recreate the offscreen targets at the new size.
    int w = pixelW > 0 ? pixelW : width;
    int h = pixelH > 0 ? pixelH : height;
    if (w > 0 && h > 0) {
        createSceneColorResources(w, h);
        createShadowResources();
    }
}

void Graphics::rebuildSwapchainIfNeeded() {
    if (!swapchainConfigured && surface && logicalW > 0 && logicalH > 0) {
        configureSurface(logicalW, logicalH);
    }
}

// ---------------------------------------------------------------------------
// Default / placeholder resources
// ---------------------------------------------------------------------------

void Graphics::createDefaultTextures() {
    uint8_t white[4] = {255, 255, 255, 255};
    whiteTexture = static_cast<GpuTexture *>(newTexture(1, 1, white, false, false)->gpuHandle);

    // Flat normal (0.5,0.5,1) in RGBA8 �?sampled as (0,0,1) normal.
    uint8_t flatNrm[4] = {128, 128, 255, 255};
    flatNormalTexture = static_cast<GpuTexture *>(newTexture(1, 1, flatNrm, false, false)->gpuHandle);
    flatNormalTexture3D = flatNormalTexture;

    uint8_t flatH[4] = {0, 0, 0, 255};
    flatHeightTexture3D = static_cast<GpuTexture *>(newTexture(1, 1, flatH, false, false)->gpuHandle);

    // 1x1 depth placeholder for the mesh3d scene-depth binding (9). Only X-ray
    // shaders sample it; every other mesh draw binds this unused depth view.
    {
        auto *gpu = new GpuTexture();
        WGPUTextureDescriptor td{};
        td.label = sv("eve_flat_depth");
        td.dimension = WGPUTextureDimension_2D;
        td.size = {1, 1, 1};
        td.sampleCount = 1;
        td.format = WGPUTextureFormat_Depth32Float;
        td.mipLevelCount = 1;
        td.usage = WGPUTextureUsage_TextureBinding;
        gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        WGPUTextureViewDescriptor vd{};
        vd.format = WGPUTextureFormat_Depth32Float;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.baseMipLevel = 0;
        vd.mipLevelCount = 1;
        vd.baseArrayLayer = 0;
        vd.arrayLayerCount = 1;
        gpu->view = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
        gpu->width = 1;
        gpu->height = 1;
        flatDepthTexture3D = gpu;
    }

    // 1x1x3 depth-array placeholder for the mesh3d shadow bindings (5/8).
    // sampleShadowPCF() early-outs (bias.y < 0.5) when shadows are disabled,
    // so the texture is never actually sampled.
    {
        auto *gpu = new GpuTexture();
        WGPUTextureDescriptor td{};
        td.label = sv("eve_default_shadow_depth");
        td.dimension = WGPUTextureDimension_2D;
        td.size = {1, 1, 3};
        td.sampleCount = 1;
        td.format = WGPUTextureFormat_Depth32Float;
        td.mipLevelCount = 1;
        td.usage = WGPUTextureUsage_TextureBinding;
        gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        WGPUTextureViewDescriptor vd{};
        vd.format = WGPUTextureFormat_Depth32Float;
        vd.dimension = WGPUTextureViewDimension_2DArray;
        vd.baseMipLevel = 0;
        vd.mipLevelCount = 1;
        vd.baseArrayLayer = 0;
        vd.arrayLayerCount = 3;
        gpu->view = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
        WGPUSamplerDescriptor sd{};
        sd.label = sv("eve_default_shadow_sampler");
        sd.addressModeU = WGPUAddressMode_ClampToEdge;
        sd.addressModeV = WGPUAddressMode_ClampToEdge;
        sd.addressModeW = WGPUAddressMode_ClampToEdge;
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.compare = WGPUCompareFunction_LessEqual;
        sd.maxAnisotropy = 1.f;
        gpu->sampler = device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&sd));
        defaultShadowTex = gpu;
    }

    // Default env cubemap is black so an unset environment (RenderSystem3D
    // still passes envIntensity=1.0 by default) does not wash the scene
    // toward white. Only an explicitly set envMap contributes IBL.
    uint8_t cubeFace[4] = {0, 0, 0, 0};
    uint8_t cubeData[24];
    for (int f = 0; f < 6; ++f) std::memcpy(cubeData + f * 4, cubeFace, 4);
    defaultEnvCubemap =
        static_cast<GpuTexture *>(newCubemap(1, cubeData)->gpuHandle);

    // Shared filtering sampler for WGSL bindings declared as plain `sampler`.
    TextureSampler def;
    mainSampler = makeSampler(def, 1);

    const uint8_t transparent[4] = {0, 0, 0, 0};
    const uint8_t decalNormal[4] = {128, 128, 255, 0};
    const uint8_t decalParams[4] = {0, 0, 0, 0};
    decalFlatAlbedo = newTexture(1, 1, transparent);
    decalFlatNormal = newTexture(1, 1, decalNormal);
    decalFlatParams = newTexture(1, 1, decalParams);
}

// ---------------------------------------------------------------------------
// Pipeline resource creation
// ---------------------------------------------------------------------------

void Graphics::createPipelineResources() {
    create2DPipelines();
    createMesh3DPipelines();
    createMesh3DClusteredPipeline();
    createShadowPipelines();
    createGbufferPipelines();
    createDecalPipeline();
    createVoxelPipelines();
}

// ---------------------------------------------------------------------------
// Bind group layouts
// ---------------------------------------------------------------------------

wgpu::BindGroupLayout Graphics::make2DBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[5]{};
    // 0: color texture
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[0].texture.multisampled = false;
    // 1: depth texture
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 2: color sampler
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
    // 3: depth sampler
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].sampler.type = WGPUSamplerBindingType_Filtering;
    // 4: Externals UBO (push-constant replacement, dynamic offset). Sized for
    //    the largest consumer: custom 2D shaders (128 B) and lit2D (Lighting2DUBO).
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[4].buffer.type = WGPUBufferBindingType_Uniform;
    entries[4].buffer.hasDynamicOffset = true;
    entries[4].buffer.minBindingSize =
        std::max<uint32_t>(Shader::kPushConstantBytes, uint32_t(sizeof(Lighting2DUBO)));

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_2d");
    desc.entryCount = 5;
    desc.entries = entries;
    return device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&desc));
}

wgpu::BindGroupLayout Graphics::makeMesh3DBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[15]{};
    // 0: Frame UBO (dynamic)
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = sizeof(Mesh3DUBO);
    // 1: albedo
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 2: normal
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 3: env cubemap
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].texture.sampleType = WGPUTextureSampleType_Float;
    entries[3].texture.viewDimension = WGPUTextureViewDimension_Cube;
    // 4: Shadow UBO (dynamic)
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Fragment;
    entries[4].buffer.type = WGPUBufferBindingType_Uniform;
    entries[4].buffer.hasDynamicOffset = true;
    entries[4].buffer.minBindingSize = sizeof(ShadowUBO);
    // 5: shadow depth array
    entries[5].binding = 5;
    entries[5].visibility = WGPUShaderStage_Fragment;
    entries[5].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[5].texture.viewDimension = WGPUTextureViewDimension_2DArray;
    // 6: height
    entries[6].binding = 6;
    entries[6].visibility = WGPUShaderStage_Fragment;
    entries[6].texture.sampleType = WGPUTextureSampleType_Float;
    entries[6].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 7: shared filtering sampler (WGSL `mainSamp`)
    entries[7].binding = 7;
    entries[7].visibility = WGPUShaderStage_Fragment;
    entries[7].sampler.type = WGPUSamplerBindingType_Filtering;
    // 8: shadow comparison sampler
    entries[8].binding = 8;
    entries[8].visibility = WGPUShaderStage_Fragment;
    entries[8].sampler.type = WGPUSamplerBindingType_Comparison;
    // 9: scene depth (G-buffer hwDepth; X-ray shaders sample it). Depth view,
    // sampled via textureSampleLevel with the shared mainSamp (binding 7).
    entries[9].binding = 9;
    entries[9].visibility = WGPUShaderStage_Fragment;
    entries[9].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[9].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 10: SSAO occlusion texture (white when AO is disabled)
    entries[10].binding = 10;
    entries[10].visibility = WGPUShaderStage_Fragment;
    entries[10].texture.sampleType = WGPUTextureSampleType_Float;
    entries[10].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 11: shared AO sampler
    entries[11].binding = 11;
    entries[11].visibility = WGPUShaderStage_Fragment;
    entries[11].sampler.type = WGPUSamplerBindingType_Filtering;
    for (uint32_t i = 12; i <= 14; ++i) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment;
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_mesh3d");
    desc.entryCount = 15;
    desc.entries = entries;
    return device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&desc));
}

wgpu::BindGroupLayout Graphics::makeShadowBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[3]{};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = 64;  // mat4 mvp
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_shadow");
    desc.entryCount = 3;
    desc.entries = entries;
    return device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&desc));
}

wgpu::BindGroupLayout Graphics::makeGbufferBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[3]{};
    // 0: Push UBO (dynamic; 128 bytes)
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = 128;
    // 1: albedo
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 2: sampler
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_gbuffer");
    desc.entryCount = 3;
    desc.entries = entries;
    return device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&desc));
}

wgpu::BindGroupLayout Graphics::makeDecalBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[7]{};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = 240;
    for (uint32_t i = 1; i <= 3; ++i) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment;
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Fragment;
    entries[4].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[4].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[5].binding = 5;
    entries[5].visibility = WGPUShaderStage_Fragment;
    entries[5].texture.sampleType = WGPUTextureSampleType_Float;
    entries[5].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[6].binding = 6;
    entries[6].visibility = WGPUShaderStage_Fragment;
    entries[6].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_decal");
    desc.entryCount = 7;
    desc.entries = entries;
    return device.CreateBindGroupLayout(
        reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&desc));
}

wgpu::BindGroupLayout Graphics::makeVoxelBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[3]{};
    // 0: PC UBO (dynamic)
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = 112;
    // 1: atlas texture
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 2: atlas sampler
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_voxel");
    desc.entryCount = 3;
    desc.entries = entries;
    return device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&desc));
}

wgpu::PipelineLayout Graphics::make2DPipelineLayout() {
    WGPUBindGroupLayout bgl = tex2DSetLayout.Get();
    WGPUPipelineLayoutDescriptor d{};
    d.label = sv("eve_2d_layout");
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts = &bgl;
    return device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&d));
}

wgpu::PipelineLayout Graphics::makeMesh3DPipelineLayout() {
    WGPUBindGroupLayout bgl = mesh3dSetLayout.Get();
    WGPUPipelineLayoutDescriptor d{};
    d.label = sv("eve_mesh3d_layout");
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts = &bgl;
    return device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&d));
}

wgpu::BindGroupLayout Graphics::makeMesh3DClusteredBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[18]{};
    // 0: Frame UBO (dynamic; clustered layout)
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = sizeof(Mesh3DClusteredUBO);
    // 1: albedo
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 2: normal map
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 3: env cubemap
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].texture.sampleType = WGPUTextureSampleType_Float;
    entries[3].texture.viewDimension = WGPUTextureViewDimension_Cube;
    // 4: shadow UBO (dynamic)
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Fragment;
    entries[4].buffer.type = WGPUBufferBindingType_Uniform;
    entries[4].buffer.hasDynamicOffset = true;
    entries[4].buffer.minBindingSize = sizeof(ShadowUBO);
    // 5: shadow depth array
    entries[5].binding = 5;
    entries[5].visibility = WGPUShaderStage_Fragment;
    entries[5].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[5].texture.viewDimension = WGPUTextureViewDimension_2DArray;
    // 6: height/parallax (unused fallback)
    entries[6].binding = 6;
    entries[6].visibility = WGPUShaderStage_Fragment;
    entries[6].texture.sampleType = WGPUTextureSampleType_Float;
    entries[6].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 7: shared filtering sampler
    entries[7].binding = 7;
    entries[7].visibility = WGPUShaderStage_Fragment;
    entries[7].sampler.type = WGPUSamplerBindingType_Filtering;
    // 8: shadow comparison sampler
    entries[8].binding = 8;
    entries[8].visibility = WGPUShaderStage_Fragment;
    entries[8].sampler.type = WGPUSamplerBindingType_Comparison;
    // 9: scene depth (G-buffer hwDepth; sampled by X-ray variants)
    entries[9].binding = 9;
    entries[9].visibility = WGPUShaderStage_Fragment;
    entries[9].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[9].texture.viewDimension = WGPUTextureViewDimension_2D;
    // 10..12: clustered-forward SSBOs
    entries[10].binding = 10;
    entries[10].visibility = WGPUShaderStage_Fragment;
    entries[10].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[10].buffer.minBindingSize = sizeof(ClusteredLightGpu);
    entries[11].binding = 11;
    entries[11].visibility = WGPUShaderStage_Fragment;
    entries[11].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[11].buffer.minBindingSize = sizeof(ClusterTableEntry);
    entries[12].binding = 12;
    entries[12].visibility = WGPUShaderStage_Fragment;
    entries[12].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[12].buffer.minBindingSize = sizeof(uint32_t);
    // 13/14: SSAO occlusion texture + sampler
    entries[13].binding = 13;
    entries[13].visibility = WGPUShaderStage_Fragment;
    entries[13].texture.sampleType = WGPUTextureSampleType_Float;
    entries[13].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[14].binding = 14;
    entries[14].visibility = WGPUShaderStage_Fragment;
    entries[14].sampler.type = WGPUSamplerBindingType_Filtering;
    for (uint32_t i = 15; i <= 17; ++i) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment;
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_mesh3d_clustered");
    desc.entryCount = 18;
    desc.entries = entries;
    return device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&desc));
}

wgpu::PipelineLayout Graphics::makeMesh3DClusteredPipelineLayout() {
    WGPUBindGroupLayout bgl = mesh3dClusteredSetLayout.Get();
    WGPUPipelineLayoutDescriptor d{};
    d.label = sv("eve_mesh3d_clustered_layout");
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts = &bgl;
    return device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&d));
}

wgpu::PipelineLayout Graphics::makeShadowPipelineLayout() {
    WGPUBindGroupLayout bgl = shadowSetLayout.Get();
    WGPUPipelineLayoutDescriptor d{};
    d.label = sv("eve_shadow_layout");
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts = &bgl;
    return device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&d));
}

wgpu::PipelineLayout Graphics::makeGbufferPipelineLayout() {
    WGPUBindGroupLayout bgl = gbufferSetLayout.Get();
    WGPUPipelineLayoutDescriptor d{};
    d.label = sv("eve_gbuffer_layout");
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts = &bgl;
    return device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&d));
}

wgpu::PipelineLayout Graphics::makeDecalPipelineLayout() {
    WGPUBindGroupLayout bgl = decalSetLayout.Get();
    WGPUPipelineLayoutDescriptor d{};
    d.label = sv("eve_decal_layout");
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts = &bgl;
    return device.CreatePipelineLayout(
        reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&d));
}

wgpu::PipelineLayout Graphics::makeVoxelPipelineLayout() {
    WGPUBindGroupLayout bgl = voxelSetLayout.Get();
    WGPUPipelineLayoutDescriptor d{};
    d.label = sv("eve_voxel_layout");
    d.bindGroupLayoutCount = 1;
    d.bindGroupLayouts = &bgl;
    return device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&d));
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------

namespace {

// Returns a RAII-held module: the caller must keep it alive across
// CreateRenderPipeline. Returning a raw WGPUShaderModule would let the
// temporary wgpu::ShaderModule destructor Release() the handle, dropping it
// from emdawnwebgpu's jsObjects before the pipeline can reference it.
wgpu::ShaderModule makeWgslModule(wgpu::Device &dev, const char *wgsl) {
    WGPUShaderSourceWGSL wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = sv(wgsl);
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgslDesc.chain;
    return dev.CreateShaderModule(reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&desc));
}

// Builds a WGSL shader-module descriptor (the chained WGPUShaderSourceWGSL is
// static so its chain pointer stays valid). The returned descriptor is a value
// that must outlive the CreateShaderModule call.
WGPUShaderModuleDescriptor mdDesc(const std::string &code) {
    static WGPUShaderSourceWGSL wd{};
    wd.chain.sType = WGPUSType_ShaderSourceWGSL;
    wd.code = sv(code.c_str());
    WGPUShaderModuleDescriptor md{};
    md.nextInChain = &wd.chain;
    return md;
}

void fillVertexLayout(WGPUVertexBufferLayout &layout, uint64_t stride,
                      const WGPUVertexAttribute *attrs, uint32_t attrCount) {
    layout.arrayStride = stride;
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.attributeCount = attrCount;
    layout.attributes = attrs;
}

WGPUBlendState alphaBlend() {
    WGPUBlendState b{};
    b.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    b.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    b.color.operation = WGPUBlendOperation_Add;
    b.alpha.srcFactor = WGPUBlendFactor_One;
    b.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    b.alpha.operation = WGPUBlendOperation_Add;
    return b;
}

WGPUBlendState noBlend() {
    WGPUBlendState b{};
    b.color.srcFactor = WGPUBlendFactor_One;
    b.color.dstFactor = WGPUBlendFactor_Zero;
    b.color.operation = WGPUBlendOperation_Add;
    b.alpha.srcFactor = WGPUBlendFactor_One;
    b.alpha.dstFactor = WGPUBlendFactor_Zero;
    b.alpha.operation = WGPUBlendOperation_Add;
    return b;
}

WGPUBlendState additiveBlend() {
    WGPUBlendState b{};
    b.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    b.color.dstFactor = WGPUBlendFactor_One;
    b.color.operation = WGPUBlendOperation_Add;
    b.alpha.srcFactor = WGPUBlendFactor_One;
    b.alpha.dstFactor = WGPUBlendFactor_One;
    b.alpha.operation = WGPUBlendOperation_Add;
    return b;
}

WGPUBlendState premultipliedBlend() {
    WGPUBlendState b = alphaBlend();
    b.color.srcFactor = WGPUBlendFactor_One;
    return b;
}

WGPUBlendState multiplyBlend() {
    WGPUBlendState b = alphaBlend();
    b.color.srcFactor = WGPUBlendFactor_Dst;
    return b;
}

size_t meshPipelineIndex(BlendMode blend, bool depthWrite, bool doubleSided) {
    return size_t(blend) * 4u + (depthWrite ? 2u : 0u) + (doubleSided ? 1u : 0u);
}

WGPUBlendState blendState(BlendMode mode) {
    switch (mode) {
        case BlendMode::Additive:
            return additiveBlend();
        case BlendMode::Premultiplied:
            return premultipliedBlend();
        case BlendMode::Multiply:
            return multiplyBlend();
        case BlendMode::Opaque:
            return noBlend();
        case BlendMode::Alpha:
        default:
            return alphaBlend();
    }
}

}  // namespace

namespace {

wgpu::RenderPipeline make2DColorPipeline(wgpu::Device &dev, WGPUTextureFormat format,
                                         BlendMode mode) {
    WGPUVertexAttribute attrs[2] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x4;
    attrs[1].offset = 8;
    attrs[1].shaderLocation = 1;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 24, attrs, 2);

    WGPUColorTargetState target{};
    target.format = format;
    target.blend = nullptr;
    // Zero-init yields WGPUColorWriteMask_None, which silently discards every
    // fragment (the clear still shows because clearValue is unaffected by the
    // mask). emdawnwebgpu forwards this explicit 0 to the browser, unlike the
    // JS default of "all".
    target.writeMask = WGPUColorWriteMask_All;
    WGPUBlendState bs = alphaBlend();
    if (mode == BlendMode::Additive)
        bs = additiveBlend();
    else if (mode == BlendMode::Premultiplied)
        bs = premultipliedBlend();
    else if (mode == BlendMode::Multiply)
        bs = multiplyBlend();
    else if (mode == BlendMode::Opaque)
        bs = noBlend();
    if (mode != BlendMode::Opaque) target.blend = &bs;

    // The solid-color shader declares no bindings, so it must use an empty
    // pipeline layout. Reusing the textured 2D layout here would require a
    // bind group for every solid draw (WebGPU validation fails the whole
    // command buffer otherwise).
    WGPUPipelineLayoutDescriptor pld{};
    pld.label = sv("eve_2d_color_layout");
    pld.bindGroupLayoutCount = 0;
    pld.bindGroupLayouts = nullptr;
    wgpu::PipelineLayout emptyLayout = dev.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&pld));

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_color2d");
    // nullptr = auto (default) pipeline layout; the solid-color shader has no
    // bindings so the derived layout matches.
    pd.layout = nullptr;
    wgpu::ShaderModule vertModule = makeWgslModule(dev, kColorVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(dev, kColorFragWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    WGPUFragmentState fs{};
    fs.module = fragModule.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    // The WGSL vertex shader mirrors clip Y to match Vulkan, which flips
    // object-space CCW winding in framebuffer space.
    pd.primitive.frontFace = WGPUFrontFace_CW;
    pd.primitive.cullMode = WGPUCullMode_Back;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = nullptr;
    pd.multisample.count = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    return dev.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

wgpu::RenderPipeline make2DTexturedPipeline(wgpu::Device &dev, wgpu::PipelineLayout layout,
                                            WGPUTextureFormat format, BlendMode mode) {
    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x4;
    attrs[1].offset = 8;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x2;
    attrs[2].offset = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 32, attrs, 3);

    WGPUColorTargetState target{};
    target.format = format;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUBlendState bs = alphaBlend();
    if (mode == BlendMode::Additive)
        bs = additiveBlend();
    else if (mode == BlendMode::Premultiplied)
        bs = premultipliedBlend();
    else if (mode == BlendMode::Multiply)
        bs = multiplyBlend();
    else if (mode == BlendMode::Opaque)
        bs = noBlend();
    if (mode != BlendMode::Opaque) target.blend = &bs;

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_textured2d");
    pd.layout = layout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(dev, kTexturedVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(dev, kTexturedFragWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    WGPUFragmentState fs{};
    fs.module = fragModule.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = nullptr;
    pd.multisample.count = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    return dev.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

wgpu::RenderPipeline make2DLitPipeline(wgpu::Device &dev, wgpu::PipelineLayout layout,
                                       WGPUTextureFormat format, bool blend) {
    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x4;
    attrs[1].offset = 8;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x2;
    attrs[2].offset = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 32, attrs, 3);

    WGPUColorTargetState target{};
    target.format = format;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUBlendState bs = alphaBlend();
    if (blend) target.blend = &bs;

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_lit2d");
    pd.layout = layout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(dev, kTexturedVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(dev, kLit2DFragWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    WGPUFragmentState fs{};
    fs.module = fragModule.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = nullptr;
    pd.multisample.count = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    return dev.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

}  // namespace

void Graphics::create2DPipelines() {
    tex2DSetLayout = make2DBindGroupLayout();
    tex2DPipelineLayout = make2DPipelineLayout();

    colorPipeline = make2DColorPipeline(device, surfaceFormat, BlendMode::Alpha);
    texturedPipeline = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat,
                                              BlendMode::Alpha);
    colorAdditivePipeline = make2DColorPipeline(device, surfaceFormat, BlendMode::Additive);
    texturedAdditivePipeline = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat,
                                                      BlendMode::Additive);
    colorPremultipliedPipeline =
        make2DColorPipeline(device, surfaceFormat, BlendMode::Premultiplied);
    texturedPremultipliedPipeline = make2DTexturedPipeline(
        device, tex2DPipelineLayout, surfaceFormat, BlendMode::Premultiplied);
    colorMultiplyPipeline = make2DColorPipeline(device, surfaceFormat, BlendMode::Multiply);
    texturedMultiplyPipeline = make2DTexturedPipeline(
        device, tex2DPipelineLayout, surfaceFormat, BlendMode::Multiply);
    colorOpaquePipeline = make2DColorPipeline(device, surfaceFormat, BlendMode::Opaque);
    texturedOpaquePipeline = make2DTexturedPipeline(device, tex2DPipelineLayout, surfaceFormat,
                                                    BlendMode::Opaque);
    lit2dPipeline = make2DLitPipeline(device, tex2DPipelineLayout, surfaceFormat, true);

    offscreenColorPipeline = make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm,
                                                 BlendMode::Alpha);
    offscreenTexturedPipeline = make2DTexturedPipeline(device, tex2DPipelineLayout,
                                                       WGPUTextureFormat_RGBA8Unorm,
                                                       BlendMode::Alpha);
    offscreenColorAdditivePipeline = make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm,
                                                         BlendMode::Additive);
    offscreenTexturedAdditivePipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm,
                               BlendMode::Additive);
    offscreenColorPremultipliedPipeline = make2DColorPipeline(
        device, WGPUTextureFormat_RGBA8Unorm, BlendMode::Premultiplied);
    offscreenTexturedPremultipliedPipeline = make2DTexturedPipeline(
        device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm,
        BlendMode::Premultiplied);
    offscreenColorMultiplyPipeline = make2DColorPipeline(
        device, WGPUTextureFormat_RGBA8Unorm, BlendMode::Multiply);
    offscreenTexturedMultiplyPipeline = make2DTexturedPipeline(
        device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, BlendMode::Multiply);
    offscreenColorOpaquePipeline = make2DColorPipeline(device, WGPUTextureFormat_RGBA8Unorm,
                                                       BlendMode::Opaque);
    offscreenTexturedOpaquePipeline =
        make2DTexturedPipeline(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm,
                               BlendMode::Opaque);
    offscreenLitPipeline = make2DLitPipeline(device, tex2DPipelineLayout,
                                             WGPUTextureFormat_RGBA8Unorm, true);
}

void Graphics::createMesh3DPipelines() {
    mesh3dSetLayout = makeMesh3DBindGroupLayout();
    // Bind groups reference the layout; drop cached groups when it changes.
    clearMeshBindGroupCache();
    mesh3dPipelineLayout = makeMesh3DPipelineLayout();

    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x3;  // pos
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x3;  // normal
    attrs[1].offset = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x2;  // uv
    attrs[2].offset = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 32, attrs, 3);

    WGPUDepthStencilState ds{};
    ds.format = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilReadMask = 0;
    ds.stencilWriteMask = 0;

    WGPUColorTargetState target{};
    target.format = sceneColorFormat;
    target.blend = nullptr;
    target.writeMask = WGPUColorWriteMask_All;

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_mesh3d");
    pd.layout = mesh3dPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kMesh3DFragWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    WGPUFragmentState fs{};
    fs.module = fragModule.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = &ds;
    pd.multisample.count = sceneColorSamples;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    for (int blendValue = int(BlendMode::Alpha); blendValue <= int(BlendMode::Multiply);
         ++blendValue) {
        const BlendMode blend = BlendMode(blendValue);
        for (int depthValue = 0; depthValue < 2; ++depthValue) {
            for (int doubleValue = 0; doubleValue < 2; ++doubleValue) {
                const bool depthWrite = depthValue != 0;
                const bool doubleSided = doubleValue != 0;
                const size_t index = meshPipelineIndex(blend, depthWrite, doubleSided);
                WGPUBlendState blendDesc = blendState(blend);
                target.blend = blend == BlendMode::Opaque ? nullptr : &blendDesc;
                ds.depthWriteEnabled =
                    depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
                pd.primitive.cullMode = doubleSided ? WGPUCullMode_None : WGPUCullMode_Back;
                pd.multisample.count = sceneColorSamples;
                pd.label = sv("eve_mesh3d_surface");
                mesh3dPipelines[index] = device.CreateRenderPipeline(
                    reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
                pd.multisample.count = 1;
                pd.label = sv("eve_mesh3d_canvas_surface");
                mesh3dCanvasPipelines[index] = device.CreateRenderPipeline(
                    reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
            }
        }
    }
    mesh3dPipeline = mesh3dPipelines[meshPipelineIndex(BlendMode::Opaque, true, false)];
    mesh3dTransparentPipeline =
        mesh3dPipelines[meshPipelineIndex(BlendMode::Alpha, false, false)];
    mesh3dCanvasPipeline =
        mesh3dCanvasPipelines[meshPipelineIndex(BlendMode::Opaque, true, false)];
}

void Graphics::createMesh3DClusteredPipeline() {
    mesh3dClusteredSetLayout = makeMesh3DClusteredBindGroupLayout();
    mesh3dClusteredPipelineLayout = makeMesh3DClusteredPipelineLayout();

    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x3;  // pos
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x3;  // normal
    attrs[1].offset = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x2;  // uv
    attrs[2].offset = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 32, attrs, 3);

    WGPUDepthStencilState ds{};
    ds.format = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilReadMask = 0;
    ds.stencilWriteMask = 0;

    WGPUColorTargetState target{};
    target.format = sceneColorFormat;
    target.blend = nullptr;
    target.writeMask = WGPUColorWriteMask_All;

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_mesh3d_clustered");
    pd.layout = mesh3dClusteredPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DClusteredVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kMesh3DClusteredFragWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    WGPUFragmentState fs{};
    fs.module = fragModule.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CW;
    pd.primitive.cullMode = WGPUCullMode_Back;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = &ds;
    pd.multisample.count = sceneColorSamples;
    pd.multisample.mask = 0xFFFFFFFFu;
    mesh3dClusteredPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

void Graphics::createShadowPipelines() {
    shadowSetLayout = makeShadowBindGroupLayout();
    shadowPipelineLayout = makeShadowPipelineLayout();

    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x3;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x3;
    attrs[1].offset = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x2;
    attrs[2].offset = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 32, attrs, 3);

    WGPUDepthStencilState ds{};
    ds.format = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilReadMask = 0;
    ds.stencilWriteMask = 0;

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_shadow");
    pd.layout = shadowPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DShadowVertWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    pd.fragment = nullptr;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = &ds;
    pd.multisample.count = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    mesh3dShadowPipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));

    wgpu::ShaderModule alphaVertModule = makeWgslModule(device, kMesh3DShadowAlphaVertWgsl);
    wgpu::ShaderModule alphaFragModule = makeWgslModule(device, kMesh3DShadowAlphaFragWgsl);
    pd.label = sv("eve_shadow_alpha");
    pd.vertex.module = alphaVertModule.Get();
    WGPUFragmentState alphaFs{};
    alphaFs.module = alphaFragModule.Get();
    alphaFs.entryPoint = sv("fs_main");
    alphaFs.targetCount = 0;
    alphaFs.targets = nullptr;
    pd.fragment = &alphaFs;
    mesh3dShadowAlphaPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

void Graphics::createGbufferPipelines() {
    gbufferSetLayout = makeGbufferBindGroupLayout();
    gbufferPipelineLayout = makeGbufferPipelineLayout();

    WGPUVertexAttribute attrs[3] = {};
    attrs[0].format = WGPUVertexFormat_Float32x3;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x3;
    attrs[1].offset = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x2;
    attrs[2].offset = 24;
    attrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vb{};
    fillVertexLayout(vb, 32, attrs, 3);

    WGPUDepthStencilState ds{};
    ds.format = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilReadMask = 0;
    ds.stencilWriteMask = 0;

    WGPUColorTargetState targets[3] = {};
    for (int i = 0; i < 3; ++i) {
        targets[i].format = WGPUTextureFormat_RGBA8Unorm;
        targets[i].blend = nullptr;
        targets[i].writeMask = WGPUColorWriteMask_All;
    }

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_gbuffer");
    pd.layout = gbufferPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kMesh3DGbufferVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kMesh3DGbufferFragWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    WGPUFragmentState fs{};
    fs.module = fragModule.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 3;
    fs.targets = targets;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = &ds;
    pd.multisample.count = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    mesh3dGbufferPipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));

    wgpu::ShaderModule alphaFragModule = makeWgslModule(device, kMesh3DGbufferAlphaFragWgsl);
    pd.label = sv("eve_gbuffer_alpha");
    fs.module = alphaFragModule.Get();
    mesh3dGbufferAlphaPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

void Graphics::createDecalPipeline() {
    decalSetLayout = makeDecalBindGroupLayout();
    decalPipelineLayout = makeDecalPipelineLayout();
    wgpu::ShaderModule vert = makeWgslModule(device, kDecalVertWgsl);
    wgpu::ShaderModule frag = makeWgslModule(device, kDecalFragWgsl);
    WGPUBlendState blend = alphaBlend();
    WGPUColorTargetState targets[3]{};
    for (auto &target : targets) {
        target.format = WGPUTextureFormat_RGBA8Unorm;
        target.blend = &blend;
        target.writeMask = WGPUColorWriteMask_All;
    }
    WGPUFragmentState fs{};
    fs.module = frag.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 3;
    fs.targets = targets;
    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_decal");
    pd.layout = decalPipelineLayout.Get();
    pd.vertex.module = vert.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    // kMesh3DVertWgsl mirrors clip-space Y, so object-space CCW becomes
    // clockwise in framebuffer space (the Vulkan pipeline uses Clockwise).
    pd.primitive.frontFace = WGPUFrontFace_CW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.multisample.count = 1;
    pd.multisample.mask = 0xFFFFFFFFu;
    decalPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&pd));
}

void Graphics::createVoxelPipelines() {
    voxelSetLayout = makeVoxelBindGroupLayout();
    voxelPipelineLayout = makeVoxelPipelineLayout();

    WGPUVertexAttribute attrs[1] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;  // corner
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    WGPUVertexBufferLayout cornerVb{};
    cornerVb.arrayStride = 8;
    cornerVb.stepMode = WGPUVertexStepMode_Vertex;
    cornerVb.attributeCount = 1;
    cornerVb.attributes = attrs;

    WGPUVertexAttribute packedAttr{};
    packedAttr.format = WGPUVertexFormat_Uint32;  // packed rect word
    packedAttr.offset = 0;
    packedAttr.shaderLocation = 1;
    WGPUVertexBufferLayout packedVb{};
    packedVb.arrayStride = 4;
    packedVb.stepMode = WGPUVertexStepMode_Instance;
    packedVb.attributeCount = 1;
    packedVb.attributes = &packedAttr;

    WGPUVertexAttribute aoAttr{};
    aoAttr.format = WGPUVertexFormat_Uint32;  // 2 bits per corner
    aoAttr.offset = 0;
    aoAttr.shaderLocation = 2;
    WGPUVertexBufferLayout aoVb{};
    aoVb.arrayStride = 4;
    aoVb.stepMode = WGPUVertexStepMode_Instance;
    aoVb.attributeCount = 1;
    aoVb.attributes = &aoAttr;

    WGPUVertexBufferLayout vbs[3] = {cornerVb, packedVb, aoVb};

    WGPUDepthStencilState ds{};
    ds.format = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilReadMask = 0;
    ds.stencilWriteMask = 0;

    WGPUColorTargetState target{};
    target.format = sceneColorFormat;
    target.blend = nullptr;
    target.writeMask = WGPUColorWriteMask_All;

    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_voxel");
    pd.layout = voxelPipelineLayout.Get();
    wgpu::ShaderModule vertModule = makeWgslModule(device, kVoxelRectVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kVoxelRectFragWgsl);
    pd.vertex.module = vertModule.Get();
    pd.vertex.entryPoint = sv("vs_main");
    pd.vertex.bufferCount = 3;
    pd.vertex.buffers = vbs;
    WGPUFragmentState fs{};
    fs.module = fragModule.Get();
    fs.entryPoint = sv("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = &ds;
    pd.multisample.count = sceneColorSamples;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    voxelRectPipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));

    // Unit quad for instanced voxel faces (2 triangles, corner + packed uv slot).
    float quad[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_voxel_quad");
    bd.size = sizeof(quad);
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    bd.mappedAtCreation = false;
    voxelUnitQuadVerts = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    queue.WriteBuffer(voxelUnitQuadVerts, 0, quad, sizeof(quad));

    uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
    WGPUBufferDescriptor ibd{};
    ibd.label = sv("eve_voxel_quad_idx");
    ibd.size = sizeof(indices);
    ibd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
    ibd.mappedAtCreation = false;
    voxelUnitQuadIndices = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
    queue.WriteBuffer(voxelUnitQuadIndices, 0, indices, sizeof(indices));
}

// ---------------------------------------------------------------------------
// SSAO (screen-space ambient occlusion)
// ---------------------------------------------------------------------------

void Graphics::ensureAOResources(int width, int height) {
    if (!device || width <= 0 || height <= 0) return;
    if (!aoSetLayout) {
        WGPUBindGroupLayoutEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = 32;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType = WGPUTextureSampleType_Depth;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{};
        ld.label = sv("eve_ao");
        ld.entryCount = 3;
        ld.entries = entries;
        aoSetLayout =
            device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&ld));
        WGPUBindGroupLayout layouts[1] = {aoSetLayout.Get()};
        WGPUPipelineLayoutDescriptor pl{};
        pl.label = sv("eve_ao_layout");
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts = layouts;
        aoPipelineLayout =
            device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&pl));

        WGPUVertexAttribute attrs[3]{};
        attrs[0].format = WGPUVertexFormat_Float32x2;
        attrs[0].offset = 0;
        attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x4;
        attrs[1].offset = 8;
        attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Float32x2;
        attrs[2].offset = 24;
        attrs[2].shaderLocation = 2;
        WGPUVertexBufferLayout vb{};
        fillVertexLayout(vb, 32, attrs, 3);

        WGPUColorTargetState target{};
        target.format = WGPUTextureFormat_RGBA8Unorm;
        target.blend = nullptr;
        target.writeMask = WGPUColorWriteMask_All;

        WGPURenderPipelineDescriptor pd{};
        pd.label = sv("eve_ssao");
        pd.layout = aoPipelineLayout.Get();
        wgpu::ShaderModule vertModule = makeWgslModule(device, kTexturedVertWgsl);
        wgpu::ShaderModule fragModule = makeWgslModule(device, kSSAOFragWgsl);
        pd.vertex.module = vertModule.Get();
        pd.vertex.entryPoint = sv("vs_main");
        pd.vertex.bufferCount = 1;
        pd.vertex.buffers = &vb;
        WGPUFragmentState fs{};
        fs.module = fragModule.Get();
        fs.entryPoint = sv("fs_main");
        fs.targetCount = 1;
        fs.targets = &target;
        pd.fragment = &fs;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pd.primitive.frontFace = WGPUFrontFace_CCW;
        pd.primitive.cullMode = WGPUCullMode_None;
        pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
        pd.multisample.count = 1;
        pd.multisample.mask = 0xFFFFFFFFu;
        aoPipeline =
            device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
    }
    if (!aoUbo) {
        WGPUBufferDescriptor bd{};
        bd.label = sv("eve_ao_ubo");
        bd.size = 64;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
        bd.mappedAtCreation = false;
        aoUbo = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    }
    if (!aoTex[0]) {
        for (int i = 0; i < 2; ++i) {
            WGPUTextureDescriptor td{};
            td.label = sv("eve_ao");
            td.dimension = WGPUTextureDimension_2D;
            // Half-resolution AO: the bilinear upsample in the mesh pass blurs
            // the per-pixel sampling grain that otherwise reads as noise.
            td.size = {static_cast<uint32_t>((width + 1) / 2),
                       static_cast<uint32_t>((height + 1) / 2), 1};
            td.sampleCount = 1;
            td.format = WGPUTextureFormat_RGBA8Unorm;
            td.mipLevelCount = 1;
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
            aoTex[i] = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
            aoView[i] = aoTex[i].CreateView();
        }
        aoReady = true;
        // New AO binding — rebuild cached mesh bind groups.
        clearMeshBindGroupCache();
    }
    if (!fullscreenQuadReady) {
        float verts[32] = {
            -1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f,
             1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f,
             1.f,  1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
            -1.f,  1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f,
        };
        uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
        WGPUBufferDescriptor vbd{};
        vbd.label = sv("eve_fullscreen_vb");
        vbd.size = sizeof(verts);
        vbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
        fullscreenQuadVb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&vbd));
        queue.WriteBuffer(fullscreenQuadVb, 0, verts, sizeof(verts));
        WGPUBufferDescriptor ibd{};
        ibd.label = sv("eve_fullscreen_ib");
        ibd.size = sizeof(indices);
        ibd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
        fullscreenQuadIb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
        queue.WriteBuffer(fullscreenQuadIb, 0, indices, sizeof(indices));
        fullscreenQuadReady = true;
    }
}

wgpu::BindGroup Graphics::makeAOBindGroup(wgpu::TextureView depthView) {
    WGPUBindGroupEntry entries[3]{};
    entries[0].binding = 0;
    entries[0].buffer = aoUbo.Get();
    entries[0].size = 32;
    entries[1].binding = 1;
    entries[1].textureView = depthView.Get();
    entries[2].binding = 2;
    entries[2].sampler = mainSampler.Get();
    WGPUBindGroupDescriptor desc{};
    desc.label = sv("eve_ao_group");
    desc.layout = aoSetLayout.Get();
    desc.entryCount = 3;
    desc.entries = entries;
    return device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
}

// ---------------------------------------------------------------------------
// Arena helpers
// ---------------------------------------------------------------------------

uint32_t Graphics::UboArena::alloc(uint64_t size, uint64_t alignment) {
    uint64_t aligned = (used + alignment - 1) / alignment * alignment;
    used = aligned + size;
    return static_cast<uint32_t>(aligned);
}

uint64_t Graphics::VertexArena::alloc(uint64_t bytes) {
    uint64_t at = used;
    used += bytes;
    return at;
}

Graphics::UboArena &Graphics::currentUboArena() { return uboArenas[currentFrameSlot()]; }
Graphics::VertexArena &Graphics::currentVertexArena() { return vertexArenas[currentFrameSlot()]; }

void Graphics::ensureUboArena(UboArena &arena, uint64_t bytes) {
    if (arena.buffer && arena.capacity >= bytes) return;
    uint64_t size = arena.capacity;
    while (size < bytes) size = size ? size * 2 : (64u << 10);
    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_ubo_arena");
    bd.size = size;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    bd.mappedAtCreation = false;
    arena.buffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    arena.capacity = size;
    arena.used = 0;
}

void Graphics::ensureVertexArena(VertexArena &arena, uint64_t bytes) {
    if (arena.buffer && arena.capacity >= bytes) return;
    uint64_t size = arena.capacity;
    while (size < bytes) size = size ? size * 2 : (256u << 10);
    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_vertex_arena");
    bd.size = size;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    bd.mappedAtCreation = false;
    arena.buffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    arena.capacity = size;
    arena.used = 0;
}

// ---------------------------------------------------------------------------
// Sampler
// ---------------------------------------------------------------------------

wgpu::Sampler Graphics::makeSampler(const TextureSampler &s, uint32_t mipLevels) const {
    WGPUSamplerDescriptor d{};
    d.label = sv("eve_sampler");
    d.addressModeU = s.repeatU ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    d.addressModeV = s.repeatV ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    d.addressModeW = s.repeatW ? WGPUAddressMode_Repeat : WGPUAddressMode_ClampToEdge;
    d.magFilter = s.mag == FilterMode::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    d.minFilter = s.min == FilterMode::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    if (s.mipmap == MipmapMode::Disabled || mipLevels <= 1) {
        d.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        d.lodMinClamp = 0.f;
        d.lodMaxClamp = 0.f;
    } else {
        d.mipmapFilter =
            s.mipmap == MipmapMode::Nearest ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
        d.lodMinClamp = s.minLod;
        d.lodMaxClamp = std::min(std::max(s.maxLod, 0.f), float(mipLevels));
    }
    float aniso = s.maxAnisotropy > 1.f ? s.maxAnisotropy : 1.f;
    d.maxAnisotropy = std::min(std::max(aniso, 1.f), std::max(maxSamplerAnisotropy, 1.f));
    return device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&d));
}

// ---------------------------------------------------------------------------
// Texture creation
// ---------------------------------------------------------------------------

Texture *Graphics::newTexture(int width, int height, const uint8_t *rgba, bool repeatU,
                              bool repeatV) {
    TextureCreateInfo info;
    info.sampler.repeatU = repeatU;
    info.sampler.repeatV = repeatV;
    return newTexture(width, height, rgba, info);
}

Texture *Graphics::newTexture(image::ImageData *data) {
    if (!data) throw Exception("newTexture: null ImageData");
    TextureCreateInfo info;
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()), info);
}

Texture *Graphics::newTexture(image::ImageData *data, const TextureCreateInfo &info) {
    if (!data) throw Exception("newTexture: null ImageData");
    if (data->getFormat() != "RGBA8")
        throw Exception("newTexture: only RGBA8 supported");
    return newTexture(data->getWidth(), data->getHeight(),
                      static_cast<const uint8_t *>(data->getData()), info);
}

Texture *Graphics::newTexture(int width, int height, const uint8_t *rgba,
                              const TextureCreateInfo &info) {
    if (width <= 0 || height <= 0) throw Exception("newTexture: invalid size %dx%d", width, height);

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = width;
    gpu->height = height;
    gpu->samplerState = info.sampler;
    gpu->mipLevels = info.generateMipmaps ? uint32_t(mipmapCountForSize(width, height)) : 1u;

    WGPUTextureDescriptor td{};
    td.label = sv("eve_tex2d");
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = static_cast<uint32_t>(width);
    td.size.height = static_cast<uint32_t>(height);
    td.size.depthOrArrayLayers = 1;
    td.sampleCount = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = gpu->mipLevels;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
               WGPUTextureUsage_RenderAttachment;
    gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));

    uploadTexturePixelsMips(gpu.get(), rgba, width, height);

    WGPUTextureViewDescriptor vd{};
    vd.format = WGPUTextureFormat_RGBA8Unorm;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.baseMipLevel = 0;
    vd.mipLevelCount = gpu->mipLevels;
    vd.baseArrayLayer = 0;
    vd.arrayLayerCount = 1;
    gpu->view = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
    gpu->sampler = makeSampler(info.sampler, gpu->mipLevels);

    auto *tex = new Texture();
    tex->width = width;
    tex->height = height;
    tex->mipmapCount = int(gpu->mipLevels);
    tex->sampler = info.sampler;
    tex->gpuHandle = gpu.get();

    ownedGpuTextures.push_back(std::move(gpu));
    ownedTextures.push_back(std::unique_ptr<Texture>(tex));
    return tex;
}

void Graphics::uploadTexturePixels(GpuTexture *gt, const uint8_t *rgba, int w, int h,
                                   const TextureCreateInfo &info) {
    (void)info;
    uploadTexturePixelsMips(gt, rgba, w, h);
}

void Graphics::uploadTexturePixelsMips(GpuTexture *gt, const uint8_t *rgba, int w, int h) {
    if (!rgba) return;
    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = static_cast<uint32_t>(w * 4);
    layout.rowsPerImage = static_cast<uint32_t>(h);
    WGPUExtent3D extent{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};

    for (uint32_t m = 0; m < gt->mipLevels; ++m) {
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = gt->texture.Get();
        dst.mipLevel = m;
        dst.aspect = WGPUTextureAspect_All;
        queue.WriteTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&dst), rgba,
                           static_cast<uint64_t>(w) * h * 4,
                           reinterpret_cast<const wgpu::TexelCopyBufferLayout*>(&layout),
                           reinterpret_cast<const wgpu::Extent3D*>(&extent));
        if (m + 1 < gt->mipLevels) {
            // Box-filter downsample into the CPU buffer for the next level.
            std::vector<uint8_t> next((w / 2) * (h / 2) * 4);
            for (int y = 0; y < h / 2; ++y) {
                for (int x = 0; x < w / 2; ++x) {
                    for (int c = 0; c < 4; ++c) {
                        uint32_t acc = 0;
                        acc += rgba[((y * 2 + 0) * w + (x * 2 + 0)) * 4 + c];
                        acc += rgba[((y * 2 + 0) * w + (x * 2 + 1)) * 4 + c];
                        acc += rgba[((y * 2 + 1) * w + (x * 2 + 0)) * 4 + c];
                        acc += rgba[((y * 2 + 1) * w + (x * 2 + 1)) * 4 + c];
                        next[(y * (w / 2) + x) * 4 + c] = uint8_t(acc / 4);
                    }
                }
            }
            w /= 2;
            h /= 2;
            rgba = next.data();
            layout.bytesPerRow = static_cast<uint32_t>(w * 4);
            layout.rowsPerImage = static_cast<uint32_t>(h);
            extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        }
    }
}

Texture *Graphics::newCubemap(int faceSize, const uint8_t *rgbaFaces) {
    TextureCreateInfo info;
    return newCubemap(faceSize, rgbaFaces, info);
}

Texture *Graphics::newCubemap(int faceSize, const uint8_t *rgbaFaces,
                              const TextureCreateInfo &info) {
    if (faceSize <= 0 || !rgbaFaces)
        throw Exception("newCubemap: invalid size or null data");

    auto gpu = std::make_unique<GpuTexture>();
    gpu->width = faceSize;
    gpu->height = faceSize;
    gpu->samplerState = info.sampler;
    gpu->isCube = true;
    gpu->mipLevels = info.generateMipmaps ? uint32_t(mipmapCountForSize(faceSize, faceSize)) : 1u;

    WGPUTextureDescriptor td{};
    td.label = sv("eve_cubemap");
    td.dimension = WGPUTextureDimension_2D;
    td.size.width = static_cast<uint32_t>(faceSize);
    td.size.height = static_cast<uint32_t>(faceSize);
    td.size.depthOrArrayLayers = 6;
    td.sampleCount = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = gpu->mipLevels;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = static_cast<uint32_t>(faceSize * 4);
    layout.rowsPerImage = static_cast<uint32_t>(faceSize);
    WGPUExtent3D extent{static_cast<uint32_t>(faceSize), static_cast<uint32_t>(faceSize), 1};
    for (int f = 0; f < 6; ++f) {
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = gpu->texture.Get();
        dst.mipLevel = 0;
        dst.aspect = WGPUTextureAspect_All;
        dst.origin = {0, 0, static_cast<uint32_t>(f)};
        queue.WriteTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&dst),
                           rgbaFaces + f * faceSize * faceSize * 4,
                           static_cast<uint64_t>(faceSize) * faceSize * 4,
                           reinterpret_cast<const wgpu::TexelCopyBufferLayout*>(&layout),
                           reinterpret_cast<const wgpu::Extent3D*>(&extent));
    }

    WGPUTextureViewDescriptor vd{};
    vd.format = WGPUTextureFormat_RGBA8Unorm;
    vd.dimension = WGPUTextureViewDimension_Cube;
    vd.baseMipLevel = 0;
    vd.mipLevelCount = gpu->mipLevels;
    vd.baseArrayLayer = 0;
    vd.arrayLayerCount = 6;
    gpu->view = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
    gpu->sampler = makeSampler(info.sampler, gpu->mipLevels);

    auto *tex = new Texture();
    tex->width = faceSize;
    tex->height = faceSize;
    tex->mipmapCount = int(gpu->mipLevels);
    tex->sampler = info.sampler;
    tex->gpuHandle = gpu.get();

    ownedGpuTextures.push_back(std::move(gpu));
    ownedTextures.push_back(std::unique_ptr<Texture>(tex));
    return tex;
}

void Graphics::setTextureSampler(Texture *texture, const TextureSampler &sampler) {
    if (!texture) return;
    auto *gpu = gpuForTexture(texture);
    if (!gpu) return;
    gpu->samplerState = sampler;
    gpu->sampler = makeSampler(sampler, gpu->mipLevels);
    texture->sampler = sampler;
}

float Graphics::getMaxAnisotropy() const { return maxSamplerAnisotropy; }

Texture *Graphics::newTextureFromFile(const std::string &filename) {
    if (filename.empty()) throw Exception("newTextureFromFile: empty filename");
    auto it = texturesByPath.find(filename);
    if (it != texturesByPath.end()) {
        reloadTextureFromFile(filename);
        return it->second;
    }
    auto *imgMod = image::Image::create();
    eve::ref<image::ImageData> data(imgMod->newImageDataFromFile(filename));
    Texture *tex = newTexture(data.get());
    texturesByPath[filename] = tex;
    return tex;
}

bool Graphics::reloadTextureFromFile(const std::string &filename) {
    auto it = texturesByPath.find(filename);
    if (it == texturesByPath.end()) return false;

    image::ImageData *data = nullptr;
    try {
        auto *imgMod = image::Image::create();
        eve::ref<image::ImageData> cached(imgMod->newImageDataFromFile(filename));
        data = cached.get();
    } catch (...) {
        return false;
    }
    if (!data) return false;

    Texture *tex = it->second;
    return updateTexture(tex, data->getWidth(), data->getHeight(),
                         static_cast<const uint8_t *>(data->getData()));
}

bool Graphics::releaseTexture(Texture *texture) {
    if (!texture || !texture->gpuHandle) return false;
    // Renderer-owned fallback textures must never be released by callers.
    if (texture->gpuHandle == whiteTexture || texture->gpuHandle == flatNormalTexture ||
        texture->gpuHandle == flatNormalTexture3D ||
        texture->gpuHandle == defaultEnvCubemap)
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

    texture->gpuHandle = nullptr;
    ownedGpuTextures.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)texIt->release();
    ownedTextures.erase(texIt);
    return true;
}

bool Graphics::updateTexture(Texture *texture, int width, int height,
                             const uint8_t *rgba) {
    if (!texture || !rgba || width <= 0 || height <= 0) return false;
    auto *gpu = gpuForTexture(texture);
    if (!gpu || width != texture->width || height != texture->height) return false;
    const auto owned = std::find_if(ownedGpuTextures.begin(), ownedGpuTextures.end(),
                                    [gpu](const std::unique_ptr<GpuTexture> &candidate) {
                                        return candidate.get() == gpu;
                                    });
    if (owned == ownedGpuTextures.end() || gpu->isCube) return false;
    uploadTexturePixelsMips(gpu, rgba, width, height);
    return true;
}

GpuTexture *Graphics::gpuForTexture(Texture *t) const {
    return t ? static_cast<GpuTexture *>(t->gpuHandle) : nullptr;
}

GpuTexture *Graphics::gpuForTextureOrWhite(Texture *t) const {
    GpuTexture *g = gpuForTexture(t);
    return g ? g : whiteTexture;
}

wgpu::BindGroup Graphics::makeTex2DBindGroup(GpuTexture *color, GpuTexture *depth) {
    GpuTexture *c = color ? color : whiteTexture;
    GpuTexture *d = depth ? depth : c;
    WGPUBindGroupEntry entries[5]{};
    entries[0].binding = 0;
    entries[0].textureView = c->view.Get();
    entries[1].binding = 1;
    entries[1].textureView = d->view.Get();
    entries[2].binding = 2;
    entries[2].sampler = c->sampler.Get();
    entries[3].binding = 3;
    entries[3].sampler = d->sampler.Get();
    entries[4].binding = 4;
    entries[4].buffer = currentUboArena().buffer.Get();  // replaced per-draw via dynamic offset
    entries[4].size =
        std::max<uint32_t>(Shader::kPushConstantBytes, uint32_t(sizeof(Lighting2DUBO)));
    WGPUBindGroupDescriptor desc{};
    desc.label = sv("eve_tex2d_group");
    desc.layout = tex2DSetLayout.Get();
    desc.entryCount = 5;
    desc.entries = entries;
    return device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
}

wgpu::BindGroup Graphics::makeMeshBindGroup(GpuTexture *albedo, GpuTexture *normal, GpuTexture *env,
                                            GpuTexture *height, GpuTexture *depth,
                                            uint32_t frameUboOffset, uint32_t shadowUboOffset,
                                            uint32_t pushUboOffset) {
    GpuTexture *a = albedo ? albedo : whiteTexture;
    GpuTexture *n = normal ? normal : flatNormalTexture;
    GpuTexture *e = env ? env : defaultEnvCubemap;
    GpuTexture *h = height ? height : flatHeightTexture3D;
    GpuTexture *d = depth ? depth : flatDepthTexture3D;
    GpuTexture *shadow = shadowDepthArray ? shadowDepthArray : defaultShadowTex;
    wgpu::TextureView aoView_ =
        aoReady ? aoView[(aoWriteIndex + 1) % 2] : (whiteTexture ? whiteTexture->view : wgpu::TextureView());
    wgpu::TextureView decalAlbedoView = gpuForTextureOrWhite(decalFlatAlbedo)->view;
    wgpu::TextureView decalNormalView = gpuForTextureOrWhite(decalFlatNormal)->view;
    wgpu::TextureView decalParamsView = gpuForTextureOrWhite(decalFlatParams)->view;
    if (decalReady && lastDecalSlot < decalSlots.size()) {
        decalAlbedoView = decalSlots[lastDecalSlot].albedoView;
        decalNormalView = decalSlots[lastDecalSlot].normalView;
        decalParamsView = decalSlots[lastDecalSlot].paramsView;
    }

    MeshBindGroupKey key{reinterpret_cast<uintptr_t>(a->view.Get()),
                         reinterpret_cast<uintptr_t>(n->view.Get()),
                         reinterpret_cast<uintptr_t>(e->view.Get()),
                         reinterpret_cast<uintptr_t>(h->view.Get()),
                         reinterpret_cast<uintptr_t>(d->view.Get()),
                         reinterpret_cast<uintptr_t>(shadow->view.Get()),
                         reinterpret_cast<uintptr_t>(shadow->sampler.Get()),
                         reinterpret_cast<uintptr_t>(aoView_.Get()),
                         reinterpret_cast<uintptr_t>(decalAlbedoView.Get()),
                         reinterpret_cast<uintptr_t>(decalNormalView.Get()),
                         reinterpret_cast<uintptr_t>(decalParamsView.Get())};
    auto cached = meshBindGroupCache_.find(key);
    if (cached != meshBindGroupCache_.end()) return cached->second;
    if (meshBindGroupCache_.size() >= kMaxMeshBindGroupCache) meshBindGroupCache_.clear();

    WGPUBindGroupEntry entries[15]{};
    entries[0].binding = 0;
    entries[0].buffer = currentUboArena().buffer.Get();
    entries[0].size = sizeof(Mesh3DUBO);
    entries[1].binding = 1;
    entries[1].textureView = a->view.Get();
    entries[2].binding = 2;
    entries[2].textureView = n->view.Get();
    entries[3].binding = 3;
    entries[3].textureView = e->view.Get();
    entries[4].binding = 4;
    entries[4].buffer = currentUboArena().buffer.Get();
    entries[4].size = sizeof(ShadowUBO);
    entries[5].binding = 5;
    entries[5].textureView = shadow->view.Get();
    entries[6].binding = 6;
    entries[6].textureView = h->view.Get();
    entries[7].binding = 7;
    entries[7].sampler = mainSampler.Get();
    entries[8].binding = 8;
    entries[8].sampler = shadow->sampler.Get();
    entries[9].binding = 9;
    entries[9].textureView = d->view.Get();
    entries[10].binding = 10;
    entries[10].textureView = aoView_.Get();
    entries[11].binding = 11;
    entries[11].sampler = mainSampler.Get();
    entries[12].binding = 12;
    entries[12].textureView = decalAlbedoView.Get();
    entries[13].binding = 13;
    entries[13].textureView = decalNormalView.Get();
    entries[14].binding = 14;
    entries[14].textureView = decalParamsView.Get();

    (void)frameUboOffset;
    (void)shadowUboOffset;
    (void)pushUboOffset;
    WGPUBindGroupDescriptor desc{};
    desc.label = sv("eve_mesh_group");
    desc.layout = mesh3dSetLayout.Get();
    desc.entryCount = 15;
    desc.entries = entries;
    wgpu::BindGroup bg =
        device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
    meshBindGroupCache_.emplace(key, bg);
    return bg;
}

wgpu::BindGroup Graphics::makeMesh3DClusteredBindGroup(GpuTexture *albedo, GpuTexture *normal,
                                                       GpuTexture *env, GpuTexture *height,
                                                       GpuTexture *depth, wgpu::TextureView aoView,
                                                       uint32_t frameUboOffset,
                                                       uint32_t shadowUboOffset) {
    GpuTexture *a = albedo ? albedo : whiteTexture;
    GpuTexture *n = normal ? normal : flatNormalTexture3D;
    GpuTexture *e = env ? env : defaultEnvCubemap;
    GpuTexture *h = height ? height : flatHeightTexture3D;
    GpuTexture *d = depth ? depth : flatDepthTexture3D;
    GpuTexture *shadow = shadowDepthArray ? shadowDepthArray : defaultShadowTex;
    ClusteredStorage &st = clusteredStorage[currentFrameSlot()];
    wgpu::TextureView decalAlbedoView = gpuForTextureOrWhite(decalFlatAlbedo)->view;
    wgpu::TextureView decalNormalView = gpuForTextureOrWhite(decalFlatNormal)->view;
    wgpu::TextureView decalParamsView = gpuForTextureOrWhite(decalFlatParams)->view;
    if (decalReady && lastDecalSlot < decalSlots.size()) {
        decalAlbedoView = decalSlots[lastDecalSlot].albedoView;
        decalNormalView = decalSlots[lastDecalSlot].normalView;
        decalParamsView = decalSlots[lastDecalSlot].paramsView;
    }

    WGPUBindGroupEntry entries[18]{};
    entries[0].binding = 0;
    entries[0].buffer = currentUboArena().buffer.Get();
    entries[0].size = sizeof(Mesh3DClusteredUBO);
    entries[1].binding = 1;
    entries[1].textureView = a->view.Get();
    entries[2].binding = 2;
    entries[2].textureView = n->view.Get();
    entries[3].binding = 3;
    entries[3].textureView = e->view.Get();
    entries[4].binding = 4;
    entries[4].buffer = currentUboArena().buffer.Get();
    entries[4].size = sizeof(ShadowUBO);
    entries[5].binding = 5;
    entries[5].textureView = shadow->view.Get();
    entries[6].binding = 6;
    entries[6].textureView = h->view.Get();
    entries[7].binding = 7;
    entries[7].sampler = mainSampler.Get();
    entries[8].binding = 8;
    entries[8].sampler = shadow->sampler.Get();
    entries[9].binding = 9;
    entries[9].textureView = d->view.Get();
    entries[10].binding = 10;
    entries[10].buffer = st.lights.Get();
    entries[10].size = st.lightsCap;
    entries[11].binding = 11;
    entries[11].buffer = st.table.Get();
    entries[11].size = st.tableCap;
    entries[12].binding = 12;
    entries[12].buffer = st.indices.Get();
    entries[12].size = st.indicesCap;
    entries[13].binding = 13;
    entries[13].textureView = aoView.Get();
    entries[14].binding = 14;
    entries[14].sampler = mainSampler.Get();
    entries[15].binding = 15;
    entries[15].textureView = decalAlbedoView.Get();
    entries[16].binding = 16;
    entries[16].textureView = decalNormalView.Get();
    entries[17].binding = 17;
    entries[17].textureView = decalParamsView.Get();

    (void)frameUboOffset;
    (void)shadowUboOffset;
    WGPUBindGroupDescriptor desc{};
    desc.label = sv("eve_mesh3d_clustered_group");
    desc.layout = mesh3dClusteredSetLayout.Get();
    desc.entryCount = 18;
    desc.entries = entries;
    return device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
}

// ---------------------------------------------------------------------------
// Mesh creation
// ---------------------------------------------------------------------------

Mesh *Graphics::newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                  int vertexCount, const uint32_t *indices, int indexCount) {
    if (vertexCount <= 0 || !posXYZ) throw Exception("newMeshFromArrays: invalid vertex data");
    if (indexCount < 0 || (indexCount > 0 && (!indices || indexCount % 3 != 0)))
        throw Exception("newMeshFromArrays: invalid index data");
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] >= uint32_t(vertexCount))
            throw Exception("newMeshFromArrays: index out of range");
    }

    std::vector<float> verts;
    verts.reserve(vertexCount * 8);
    for (int i = 0; i < vertexCount; ++i) {
        verts.push_back(posXYZ[i * 3 + 0]);
        verts.push_back(posXYZ[i * 3 + 1]);
        verts.push_back(posXYZ[i * 3 + 2]);
        if (nrmXYZ) {
            verts.push_back(nrmXYZ[i * 3 + 0]);
            verts.push_back(nrmXYZ[i * 3 + 1]);
            verts.push_back(nrmXYZ[i * 3 + 2]);
        } else {
            verts.push_back(0.f);
            verts.push_back(0.f);
            verts.push_back(1.f);
        }
        if (uvST) {
            verts.push_back(uvST[i * 2 + 0]);
            verts.push_back(uvST[i * 2 + 1]);
        } else {
            verts.push_back(0.f);
            verts.push_back(0.f);
        }
    }

    auto gpu = std::make_unique<GpuMesh>();
    gpu->vertexCount = uint32_t(vertexCount);
    gpu->indexCount = indexCount > 0 ? uint32_t(indexCount) : 0;
    gpu->vertexStride = 32;

    WGPUBufferDescriptor vbd{};
    vbd.label = sv("eve_mesh_vb");
    vbd.size = verts.size() * sizeof(float);
    vbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    vbd.mappedAtCreation = false;
    gpu->vertexBuffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&vbd));
    queue.WriteBuffer(gpu->vertexBuffer, 0, verts.data(), vbd.size);
    gpu->vertexCapacity = vbd.size;

    if (indexCount > 0) {
        // 16-bit index format halves index memory for meshes with <= 65535 verts.
        if (vertexCount <= 65535) {
            std::vector<uint16_t> idx16;
            idx16.reserve(indexCount);
            for (int i = 0; i < indexCount; ++i) idx16.push_back(uint16_t(indices[i]));
            WGPUBufferDescriptor ibd{};
            ibd.label = sv("eve_mesh_ib");
            if (idx16.size() % 2 != 0) idx16.push_back(0);  // WriteBuffer size must align to 4.
            ibd.size = uint64_t(idx16.size()) * sizeof(uint16_t);
            ibd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
            ibd.mappedAtCreation = false;
            gpu->indexBuffer =
                device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
            queue.WriteBuffer(gpu->indexBuffer, 0, idx16.data(), ibd.size);
            gpu->indexFormat = wgpu::IndexFormat::Uint16;
            gpu->indexCapacity = ibd.size;
        } else {
            WGPUBufferDescriptor ibd{};
            ibd.label = sv("eve_mesh_ib");
            ibd.size = uint64_t(indexCount) * sizeof(uint32_t);
            ibd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
            ibd.mappedAtCreation = false;
            gpu->indexBuffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
            queue.WriteBuffer(gpu->indexBuffer, 0, indices, ibd.size);
            gpu->indexFormat = wgpu::IndexFormat::Uint32;
            gpu->indexCapacity = ibd.size;
        }
    }

    auto *mesh       = new Mesh();
    mesh->indexCount = indexCount;
    mesh->gpuVertexCount = vertexCount;
    mesh->gpuHandle  = gpu.get();
    mesh->computeBounds(posXYZ, vertexCount);
    ownedGpuMeshes.push_back(std::move(gpu));
    ownedMeshes.push_back(std::unique_ptr<Mesh>(mesh));
    return mesh;
}

Mesh *Graphics::newMeshFromAssimp(const ::aiMesh &mesh) {
    aiMatrix4x4 ident;
    return newMeshFromAssimp(mesh, ident);
}

Mesh *Graphics::newMeshFromAssimp(const ::aiMesh &mesh, const aiMatrix4x4 &worldTransform) {
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    pos.reserve(mesh.mNumVertices * 3);
    nrm.reserve(mesh.mNumVertices * 3);
    uv.reserve(mesh.mNumVertices * 2);
    idx.reserve(mesh.mNumFaces * 3);

    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        aiVector3D p = worldTransform * mesh.mVertices[i];
        pos.push_back(p.x);
        pos.push_back(p.y);
        pos.push_back(p.z);
        if (mesh.mNormals) {
            aiMatrix3x3 rot(worldTransform);
            aiVector3D n = rot * mesh.mNormals[i];
            n.Normalize();
            nrm.push_back(n.x);
            nrm.push_back(n.y);
            nrm.push_back(n.z);
        } else {
            nrm.push_back(0.f);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
        }
        if (mesh.mTextureCoords[0]) {
            uv.push_back(mesh.mTextureCoords[0][i].x);
            uv.push_back(mesh.mTextureCoords[0][i].y);
        } else {
            uv.push_back(0.f);
            uv.push_back(0.f);
        }
    }
    for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
        for (unsigned v = 0; v < mesh.mFaces[f].mNumIndices; ++v)
            idx.push_back(mesh.mFaces[f].mIndices[v]);
    }
    Mesh *m = newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(mesh.mNumVertices), idx.data(),
                                int(idx.size()));
    if (m && mesh.mNumAnimMeshes > 0) {
        m->initMorphBase(int(mesh.mNumVertices), pos.data(), nrm.data(), uv.data());
        for (unsigned a = 0; a < mesh.mNumAnimMeshes; ++a) {
            const aiAnimMesh *am = mesh.mAnimMeshes[a];
            std::vector<float> absPos(am->mNumVertices * 3);
            for (unsigned i = 0; i < am->mNumVertices; ++i) {
                absPos[i * 3 + 0] = am->mVertices[i].x;
                absPos[i * 3 + 1] = am->mVertices[i].y;
                absPos[i * 3 + 2] = am->mVertices[i].z;
            }
            std::string name = am->mName.C_Str();
            if (name.empty()) name = "morph" + std::to_string(a);
            m->addMorphTargetAbsolute(name, absPos.data());
        }
    }
    return m;
}

bool Graphics::bakeMeshMorph(Mesh *mesh) {
    if (!mesh || !mesh->hasMorphData() || !mesh->isMorphDirty()) return false;
    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    if (!gpu || !gpu->vertexBuffer) return false;
    std::vector<float> pos, nrm;
    mesh->computeMorphedPositions(pos, nrm);
    if (pos.empty()) return false;
    mesh->computeBounds(pos.data(), mesh->getVertexCount());
    std::vector<float> verts;
    verts.reserve(mesh->getVertexCount() * 8);
    const auto &uvs = mesh->baseUv();
    for (int i = 0; i < mesh->getVertexCount(); ++i) {
        verts.push_back(pos[i * 3 + 0]);
        verts.push_back(pos[i * 3 + 1]);
        verts.push_back(pos[i * 3 + 2]);
        if (nrm.size() >= size_t((i + 1) * 3)) {
            verts.push_back(nrm[i * 3 + 0]);
            verts.push_back(nrm[i * 3 + 1]);
            verts.push_back(nrm[i * 3 + 2]);
        } else {
            verts.push_back(0.f);
            verts.push_back(0.f);
            verts.push_back(1.f);
        }
        if (uvs.size() >= size_t((i + 1) * 2)) {
            verts.push_back(uvs[i * 2 + 0]);
            verts.push_back(uvs[i * 2 + 1]);
        } else {
            verts.push_back(0.f);
            verts.push_back(0.f);
        }
    }
    queue.WriteBuffer(gpu->vertexBuffer, 0, verts.data(), verts.size() * sizeof(float));
    mesh->markMorphClean();
    return true;
}

bool Graphics::updateMeshVertices(Mesh *mesh, const float *posXYZ, const float *nrmXYZ,
                                  const float *uvST, int vertexCount, const uint32_t *indices,
                                  int indexCount) {
    if (!mesh || !mesh->gpuHandle || !posXYZ || vertexCount <= 0 || indexCount < 0) return false;
    if (indexCount > 0 && (!indices || indexCount % 3 != 0)) return false;
    for (int i = 0; i < indexCount; ++i) {
        if (indices[i] >= uint32_t(vertexCount)) return false;
    }
    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    const auto owned = std::find_if(ownedGpuMeshes.begin(), ownedGpuMeshes.end(),
                                    [gpu](const std::unique_ptr<GpuMesh> &candidate) {
                                        return candidate.get() == gpu;
                                    });
    if (owned == ownedGpuMeshes.end()) return false;

    std::vector<float> verts;
    verts.reserve(size_t(vertexCount) * 8u);
    for (int i = 0; i < vertexCount; ++i) {
        verts.insert(verts.end(), posXYZ + size_t(i) * 3u, posXYZ + size_t(i) * 3u + 3u);
        if (nrmXYZ)
            verts.insert(verts.end(), nrmXYZ + size_t(i) * 3u, nrmXYZ + size_t(i) * 3u + 3u);
        else
            verts.insert(verts.end(), {0.f, 0.f, 1.f});
        if (uvST)
            verts.insert(verts.end(), uvST + size_t(i) * 2u, uvST + size_t(i) * 2u + 2u);
        else
            verts.insert(verts.end(), {0.f, 0.f});
    }

    const uint64_t vertexBytes = verts.size() * sizeof(float);
    if (vertexBytes > gpu->vertexCapacity) {
        WGPUBufferDescriptor desc{};
        desc.label = sv("eve_mesh_dynamic_vb");
        desc.size = vertexBytes;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
        gpu->vertexBuffer =
            device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&desc));
        gpu->vertexCapacity = vertexBytes;
    }
    queue.WriteBuffer(gpu->vertexBuffer, 0, verts.data(), vertexBytes);
    gpu->vertexCount = uint32_t(vertexCount);

    if (indexCount > 0) {
        const wgpu::IndexFormat format =
            vertexCount <= 65535 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
        std::vector<uint16_t> idx16;
        const void *indexData = indices;
        uint64_t indexBytes = uint64_t(indexCount) * sizeof(uint32_t);
        if (format == wgpu::IndexFormat::Uint16) {
            idx16.reserve(size_t(indexCount) + 1u);
            for (int i = 0; i < indexCount; ++i) idx16.push_back(uint16_t(indices[i]));
            if (idx16.size() % 2 != 0) idx16.push_back(0);
            indexData = idx16.data();
            indexBytes = idx16.size() * sizeof(uint16_t);
        }
        if (format != gpu->indexFormat || indexBytes > gpu->indexCapacity) {
            WGPUBufferDescriptor desc{};
            desc.label = sv("eve_mesh_dynamic_ib");
            desc.size = indexBytes;
            desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
            gpu->indexBuffer =
                device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&desc));
            gpu->indexCapacity = indexBytes;
        }
        queue.WriteBuffer(gpu->indexBuffer, 0, indexData, indexBytes);
        gpu->indexFormat = format;
        gpu->indexCount = uint32_t(indexCount);
    }

    mesh->computeBounds(posXYZ, vertexCount);
    mesh->gpuVertexCount = int(gpu->vertexCount);
    mesh->indexCount = int(gpu->indexCount);
    return true;
}

bool Graphics::releaseMesh(Mesh *mesh) {
    if (!mesh || !mesh->gpuHandle) return false;

    auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
    auto gpuIt = std::find_if(ownedGpuMeshes.begin(), ownedGpuMeshes.end(),
                              [&](const std::unique_ptr<GpuMesh> &g) {
                                  return g.get() == gpu;
                              });
    if (gpuIt == ownedGpuMeshes.end()) return false;

    auto meshIt = std::find_if(ownedMeshes.begin(), ownedMeshes.end(),
                               [&](const std::unique_ptr<Mesh> &m) {
                                   return m.get() == mesh;
                               });
    if (meshIt == ownedMeshes.end()) return false;

    mesh->gpuHandle = nullptr;
    ownedGpuMeshes.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)meshIt->release();
    ownedMeshes.erase(meshIt);
    return true;
}

Mesh *Graphics::newMeshSphere(int slices, int stacks) {
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= stacks; ++y) {
        for (int x = 0; x <= slices; ++x) {
            float u = float(x) / float(slices);
            float v = float(y) / float(stacks);
            float theta = u * 2.f * glm::pi<float>();
            float phi = v * glm::pi<float>();
            float sx = std::sin(phi) * std::cos(theta);
            float sy = std::cos(phi);
            float sz = std::sin(phi) * std::sin(theta);
            pos.push_back(sx);
            pos.push_back(sy);
            pos.push_back(sz);
            nrm.push_back(sx);
            nrm.push_back(sy);
            nrm.push_back(sz);
            uv.push_back(u);
            uv.push_back(v);
        }
    }
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            int a = y * (slices + 1) + x;
            int b = a + slices + 1;
            idx.push_back(a);
            idx.push_back(b);
            idx.push_back(a + 1);
            idx.push_back(b);
            idx.push_back(b + 1);
            idx.push_back(a + 1);
        }
    }
    return newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(),
                             int(idx.size()));
}

Mesh *Graphics::newMeshCylinder(int slices, int stacks, bool caps) {
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int y = 0; y <= stacks; ++y) {
        for (int x = 0; x <= slices; ++x) {
            float u = float(x) / float(slices);
            float v = float(y) / float(stacks);
            float theta = u * 2.f * glm::pi<float>();
            pos.push_back(std::cos(theta));
            pos.push_back(v * 2.f - 1.f);
            pos.push_back(std::sin(theta));
            nrm.push_back(std::cos(theta));
            nrm.push_back(0.f);
            nrm.push_back(std::sin(theta));
            uv.push_back(u);
            uv.push_back(v);
        }
    }
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            int a = y * (slices + 1) + x;
            int b = a + slices + 1;
            idx.push_back(a);
            idx.push_back(b);
            idx.push_back(a + 1);
            idx.push_back(b);
            idx.push_back(b + 1);
            idx.push_back(a + 1);
        }
    }
    if (caps) {
        int base = int(pos.size() / 3);
        pos.push_back(0.f); pos.push_back(-1.f); pos.push_back(0.f);
        nrm.push_back(0.f); nrm.push_back(-1.f); nrm.push_back(0.f);
        uv.push_back(0.5f); uv.push_back(0.5f);
        for (int x = 0; x < slices; ++x) {
            float theta0 = float(x) / float(slices) * 2.f * glm::pi<float>();
            float theta1 = float(x + 1) / float(slices) * 2.f * glm::pi<float>();
            int i0 = int(pos.size() / 3);
            pos.push_back(std::cos(theta0)); pos.push_back(-1.f); pos.push_back(std::sin(theta0));
            nrm.push_back(0.f); nrm.push_back(-1.f); nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta0)); uv.push_back(0.5f + 0.5f * std::sin(theta0));
            int i1 = int(pos.size() / 3);
            pos.push_back(std::cos(theta1)); pos.push_back(-1.f); pos.push_back(std::sin(theta1));
            nrm.push_back(0.f); nrm.push_back(-1.f); nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta1)); uv.push_back(0.5f + 0.5f * std::sin(theta1));
            idx.push_back(base);
            idx.push_back(i1);
            idx.push_back(i0);
        }
        base = int(pos.size() / 3);
        pos.push_back(0.f); pos.push_back(1.f); pos.push_back(0.f);
        nrm.push_back(0.f); nrm.push_back(1.f); nrm.push_back(0.f);
        uv.push_back(0.5f); uv.push_back(0.5f);
        for (int x = 0; x < slices; ++x) {
            float theta0 = float(x) / float(slices) * 2.f * glm::pi<float>();
            float theta1 = float(x + 1) / float(slices) * 2.f * glm::pi<float>();
            int i0 = int(pos.size() / 3);
            pos.push_back(std::cos(theta0)); pos.push_back(1.f); pos.push_back(std::sin(theta0));
            nrm.push_back(0.f); nrm.push_back(1.f); nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta0)); uv.push_back(0.5f + 0.5f * std::sin(theta0));
            int i1 = int(pos.size() / 3);
            pos.push_back(std::cos(theta1)); pos.push_back(1.f); pos.push_back(std::sin(theta1));
            nrm.push_back(0.f); nrm.push_back(1.f); nrm.push_back(0.f);
            uv.push_back(0.5f + 0.5f * std::cos(theta1)); uv.push_back(0.5f + 0.5f * std::sin(theta1));
            idx.push_back(base);
            idx.push_back(i0);
            idx.push_back(i1);
        }
    }
    return newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(),
                             int(idx.size()));
}

// ---------------------------------------------------------------------------
// 2D drawing
// ---------------------------------------------------------------------------

void Graphics::clear2DBatches() {
    solidBatches.clear();
    texturedBatches.clear();
    litBatches.clear();
    overlaySpans.clear();
    sceneColorComposited = false;
}

void Graphics::noteSolidOverlay() {
    if (solidBatches.empty()) return;
    const uint32_t idx = uint32_t(solidBatches.size() - 1);
    const uint32_t n = uint32_t(solidBatches.back().batch.vertices().size());
    if (!overlaySpans.empty() && overlaySpans.back().kind == OverlayKind::Solid &&
        overlaySpans.back().index == idx) {
        overlaySpans.back().vertCount = n - overlaySpans.back().vertBegin;
        return;
    }
    const uint32_t begin = n >= 6u ? n - 6u : 0u;
    overlaySpans.push_back({OverlayKind::Solid, idx, begin, n - begin});
}

void Graphics::noteTexturedOverlay(Texture *tex) {
    if (tex && tex == getSceneColorTexture()) sceneColorComposited = true;
    const uint32_t idx = texturedBatches.empty() ? 0u : uint32_t(texturedBatches.size() - 1);
    if (!overlaySpans.empty() && overlaySpans.back().kind == OverlayKind::Textured &&
        overlaySpans.back().index == idx)
        return;
    overlaySpans.push_back({OverlayKind::Textured, idx, 0, 0});
}

void Graphics::drawSolidRect(float x, float y, float w, float h, const Color &color,
                             BlendMode blend) {
    auto it = std::find_if(solidBatches.begin(), solidBatches.end(),
                           [&](const SolidBatch &sb) { return sb.blend == blend; });
    if (it == solidBatches.end()) {
        solidBatches.push_back(SolidBatch{blend, Batcher{}});
        it = solidBatches.end() - 1;
    }
    it->batch.addRect(x, y, w, h, color);
    noteSolidOverlay();
}

void Graphics::drawSolidRectRotated(float cx, float cy, float w, float h, float degrees,
                                    const Color &color, BlendMode blend) {
    auto it = std::find_if(solidBatches.begin(), solidBatches.end(),
                           [&](const SolidBatch &sb) { return sb.blend == blend; });
    if (it == solidBatches.end()) {
        solidBatches.push_back(SolidBatch{blend, Batcher{}});
        it = solidBatches.end() - 1;
    }
    it->batch.addRectRotated(cx, cy, w, h, degrees, color);
    noteSolidOverlay();
}

void Graphics::drawTexturedRect(Texture *texture, float x, float y, float w, float h,
                                const Color &color) {
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
                                        const Color &color, bool rotatedUV, BlendMode blend) {
    if (!texture) {
        drawSolidRect(x, y, w, h, color, blend);
        return;
    }
    if (texturedBatches.empty() || texturedBatches.back().texture != texture ||
        texturedBatches.back().shader != shader || texturedBatches.back().depth != nullptr ||
        texturedBatches.back().blend != blend) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, blend, Batcher{}});
    }
    texturedBatches.back().batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1, rotatedUV);
    noteTexturedOverlay(texture);
}

void Graphics::drawTexturedRectShaderUVRotated(Texture *texture, Shader *shader, float cx, float cy,
                                               float w, float h, float degrees, float u0, float v0,
                                               float u1, float v1, const Color &color,
                                               bool rotatedUV, BlendMode blend) {
    if (!texture) {
        drawSolidRect(cx - w * 0.5f, cy - h * 0.5f, w, h, color, blend);
        return;
    }
    auto it = std::find_if(texturedBatches.begin(), texturedBatches.end(),
                           [&](const TexturedBatch &tb) {
                               return tb.texture == texture && tb.shader == shader &&
                                      tb.depth == nullptr && tb.blend == blend;
                           });
    if (it == texturedBatches.end()) {
        texturedBatches.push_back(TexturedBatch{texture, nullptr, shader, blend, Batcher{}});
        it = texturedBatches.end() - 1;
    }
    it->batch.addTexturedRectRotated(cx, cy, w, h, degrees, color, u0, v0, u1, v1, rotatedUV);
    noteTexturedOverlay(texture);
}

void Graphics::drawTexturedRectShaderDepth(Texture *color, Texture *depth, Shader *shader, float x,
                                           float y, float w, float h, const Color &tint) {
    if (!color) {
        drawSolidRect(x, y, w, h, tint);
        return;
    }
    auto it = std::find_if(texturedBatches.begin(), texturedBatches.end(),
                           [&](const TexturedBatch &tb) {
                               return tb.texture == color && tb.shader == shader &&
                                      tb.depth == depth;
                           });
    if (it == texturedBatches.end()) {
        texturedBatches.push_back(TexturedBatch{color, depth, shader, BlendMode::Alpha, Batcher{}});
        it = texturedBatches.end() - 1;
    }
    it->batch.addTexturedRect(x, y, w, h, tint, 0.f, 0.f, 1.f, 1.f, false);
    noteTexturedOverlay(color);
}

void Graphics::drawTexturedRectLitUV(Texture *albedo, Texture *normal, float x, float y, float w,
                                     float h, float u0, float v0, float u1, float v1,
                                     const Color &color) {
    if (!albedo) {
        drawSolidRect(x, y, w, h, color);
        return;
    }
    auto it = std::find_if(litBatches.begin(), litBatches.end(),
                           [&](const LitBatch &lb) {
                               return lb.albedo == albedo && lb.normal == normal;
                           });
    if (it == litBatches.end()) {
        litBatches.push_back(LitBatch{albedo, normal, Batcher{}});
        it = litBatches.end() - 1;
    }
    it->batch.addTexturedRect(x, y, w, h, color, u0, v0, u1, v1, false);
}

void Graphics::setLighting2D(const Lighting2DUBO &ubo) {
    // Uploaded on demand before flushing lit batches.
    lighting2dFrame = ubo;
}

void Graphics::flush2D(wgpu::RenderPassEncoder pass, int viewW, int viewH,
                       WGPUTextureFormat format) {
    auto spans = std::move(overlaySpans);
    const bool offscreen = uint32_t(format) != uint32_t(surfaceFormat);

    struct SolidUpload {
        BlendMode blend = BlendMode::Alpha;
        uint64_t offset = 0;
        uint64_t bytes = 0;
    };
    std::vector<SolidUpload> solidUploads;
    solidUploads.reserve(solidBatches.size());
    for (const auto &sb : solidBatches) {
        if (sb.batch.empty()) {
            solidUploads.push_back(SolidUpload{sb.blend, 0, 0});
            continue;
        }
        Batcher ndc = sb.batch;
        ndc.toNDC(viewW, viewH);
        auto verts = ndc.vertices();
        // Batcher::toNDC targets Vulkan's Y-down NDC ((-1,-1)=top-left), but
        // WebGPU's clip space is Y-up ((-1,-1)=bottom-left). Flip Y so logical
        // top-left maps to the swapchain's top-left.
        for (auto &v : verts) v.pos.y = -v.pos.y;
        std::vector<float> data;
        data.reserve(verts.size() * 6);
        for (const auto &v : verts) {
            data.push_back(v.pos.x);
            data.push_back(v.pos.y);
            data.push_back(v.color.r);
            data.push_back(v.color.g);
            data.push_back(v.color.b);
            data.push_back(v.color.a);
        }
        const uint64_t bytes = data.size() * sizeof(float);
        auto &arena = currentVertexArena();
        ensureVertexArena(arena, arena.used + bytes);
        const uint64_t offset = arena.alloc(bytes);
        queue.WriteBuffer(arena.buffer, offset, data.data(), bytes);
        solidUploads.push_back(SolidUpload{sb.blend, offset, bytes});
    }

    auto solidPipe = [&](BlendMode mode) -> wgpu::RenderPipeline {
        switch (mode) {
            case BlendMode::Additive:
                return offscreen ? offscreenColorAdditivePipeline : colorAdditivePipeline;
            case BlendMode::Premultiplied:
                return offscreen ? offscreenColorPremultipliedPipeline
                                 : colorPremultipliedPipeline;
            case BlendMode::Multiply:
                return offscreen ? offscreenColorMultiplyPipeline : colorMultiplyPipeline;
            case BlendMode::Opaque:
                return offscreen ? offscreenColorOpaquePipeline : colorOpaquePipeline;
            case BlendMode::Alpha:
            default:
                return offscreen ? offscreenColorPipeline : colorPipeline;
        }
    };

    auto drawSolid = [&](uint32_t batchIndex, uint32_t first, uint32_t count) {
        if (batchIndex >= solidUploads.size()) return;
        const SolidUpload &u = solidUploads[batchIndex];
        if (u.bytes == 0 || count == 0) return;
        pass.SetPipeline(solidPipe(u.blend));
        pass.SetVertexBuffer(0, currentVertexArena().buffer, u.offset, u.bytes);
        pass.Draw(count, 1, first, 0);
    };

    if (!spans.empty()) {
        for (const auto &sp : spans) {
            if (sp.kind == OverlayKind::Solid)
                drawSolid(sp.index, sp.vertBegin, sp.vertCount);
            else if (sp.kind == OverlayKind::Textured && sp.index < texturedBatches.size())
                drawTexturedBatch(pass, texturedBatches[sp.index], viewW, viewH, format, offscreen);
            else if (sp.kind == OverlayKind::Lit && sp.index < litBatches.size())
                drawLitBatch(pass, litBatches[sp.index], viewW, viewH, format);
        }
    } else {
        for (size_t i = 0; i < solidUploads.size(); ++i) {
            if (solidUploads[i].bytes > 0)
                drawSolid(uint32_t(i), 0, uint32_t(solidBatches[i].batch.vertices().size()));
        }
        for (auto &tb : texturedBatches) {
            if (tb.batch.empty()) continue;
            drawTexturedBatch(pass, tb, viewW, viewH, format, offscreen);
        }
        for (auto &lb : litBatches) {
            if (lb.batch.empty()) continue;
            drawLitBatch(pass, lb, viewW, viewH, format);
        }
    }
    clear2DBatches();
}

void Graphics::drawTexturedBatch(wgpu::RenderPassEncoder pass, TexturedBatch &tb, int viewW,
                                 int viewH, WGPUTextureFormat format, bool offscreen) {
    Batcher ndc = tb.batch;
    ndc.toNDC(viewW, viewH);
    auto verts = ndc.vertices();
    for (auto &v : verts) v.pos.y = -v.pos.y;
    if (verts.empty()) return;

    std::vector<float> data;
    data.reserve(verts.size() * 8);
    for (const auto &v : verts) {
        data.push_back(v.pos.x);
        data.push_back(v.pos.y);
        data.push_back(v.color.r);
        data.push_back(v.color.g);
        data.push_back(v.color.b);
        data.push_back(v.color.a);
        data.push_back(v.uv.x);
        data.push_back(v.uv.y);
    }
    uint64_t bytes = data.size() * sizeof(float);
    auto &arena = currentVertexArena();
    ensureVertexArena(arena, arena.used + bytes);
    uint64_t vtxOffset = arena.alloc(bytes);
    queue.WriteBuffer(arena.buffer, vtxOffset, data.data(), bytes);

    GpuTexture *gpu = gpuForTexture(tb.texture);
    GpuTexture *depthGpu = gpuForTexture(tb.depth);
    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + 256);

    // Push-constant (Externals) block for custom shaders.
    uint32_t pushOffset = 0;
    if (tb.shader && tb.shader->pushConstantSize() > 0) {
        pushOffset = uboArena.alloc(Shader::kPushConstantBytes, 256);
        queue.WriteBuffer(uboArena.buffer, pushOffset, tb.shader->pushConstantData(),
                          Shader::kPushConstantBytes);
    }

    wgpu::BindGroup bg = makeTex2DBindGroup(gpu, depthGpu);
    uint32_t offsets[1] = {pushOffset};

    wgpu::RenderPipeline pipe;
    if (tb.shader && tb.shader->gpuHandle) {
        auto *gs = static_cast<GpuShader *>(tb.shader->gpuHandle);
        pipe = offscreen ? gs->offscreenPipeline : gs->swapchainPipeline;
    } else {
        switch (tb.blend) {
            case BlendMode::Additive:
                pipe = offscreen ? offscreenTexturedAdditivePipeline : texturedAdditivePipeline;
                break;
            case BlendMode::Premultiplied:
                pipe = offscreen ? offscreenTexturedPremultipliedPipeline
                                 : texturedPremultipliedPipeline;
                break;
            case BlendMode::Multiply:
                pipe = offscreen ? offscreenTexturedMultiplyPipeline
                                 : texturedMultiplyPipeline;
                break;
            case BlendMode::Opaque:
                pipe = offscreen ? offscreenTexturedOpaquePipeline : texturedOpaquePipeline;
                break;
            case BlendMode::Alpha:
            default:
                pipe = offscreen ? offscreenTexturedPipeline : texturedPipeline;
                break;
        }
    }
    if (!pipe) return;
    pass.SetPipeline(pipe);
    pass.SetBindGroup(0, bg, 1, offsets);
    pass.SetVertexBuffer(0, arena.buffer, vtxOffset, bytes);
    pass.Draw(static_cast<uint32_t>(verts.size()), 1, 0, 0);
}

void Graphics::drawLitBatch(wgpu::RenderPassEncoder pass, LitBatch &lb, int viewW, int viewH,
                            WGPUTextureFormat format) {
    Batcher ndc = lb.batch;
    ndc.toNDC(viewW, viewH);
    auto verts = ndc.vertices();
    for (auto &v : verts) v.pos.y = -v.pos.y;
    if (verts.empty()) return;

    std::vector<float> data;
    data.reserve(verts.size() * 8);
    for (const auto &v : verts) {
        data.push_back(v.pos.x);
        data.push_back(v.pos.y);
        data.push_back(v.color.r);
        data.push_back(v.color.g);
        data.push_back(v.color.b);
        data.push_back(v.color.a);
        data.push_back(v.uv.x);
        data.push_back(v.uv.y);
    }
    uint64_t bytes = data.size() * sizeof(float);
    auto &arena = currentVertexArena();
    ensureVertexArena(arena, arena.used + bytes);
    uint64_t vtxOffset = arena.alloc(bytes);
    queue.WriteBuffer(arena.buffer, vtxOffset, data.data(), bytes);

    GpuTexture *albedoGpu = gpuForTextureOrWhite(lb.albedo);
    GpuTexture *normalGpu = gpuForTextureOrWhite(lb.normal);

    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + 512);
    uint32_t uboOffset = uboArena.alloc(sizeof(Lighting2DUBO), 256);
    queue.WriteBuffer(uboArena.buffer, uboOffset, &lighting2dFrame, sizeof(Lighting2DUBO));

    wgpu::BindGroup bg = makeTex2DBindGroup(albedoGpu, normalGpu);
    uint32_t offsets[1] = {uboOffset};
    wgpu::RenderPipeline pipe =
        uint32_t(format) == uint32_t(surfaceFormat) ? lit2dPipeline : offscreenLitPipeline;
    if (!pipe) return;
    pass.SetPipeline(pipe);
    pass.SetBindGroup(0, bg, 1, offsets);
    pass.SetVertexBuffer(0, arena.buffer, vtxOffset, bytes);
    pass.Draw(static_cast<uint32_t>(verts.size()), 1, 0, 0);
}

// ---------------------------------------------------------------------------
// 3D rendering
// ---------------------------------------------------------------------------

void Graphics::begin3DFrame() {
    if (!initialized || !device) return;
    frame3DStarted = true;
    frameHad3DThisFrame = false;
    // Match the desktop (Vulkan) backend: had3DThisFrame() must return true
    // once a 3D frame begins, or RenderSystem3D::render bails out before any
    // mesh is drawn.
    frameHad3D = true;
    sceneColorPassOpen = false;
    mesh3dDraws.clear();
    shadowPassDraws.clear();
    for (int c = 0; c < ShadowConfig::kCascades; ++c) shadowCascadeDraws[c].clear();
    voxelDraws.clear();
    if (surfaceNeedsRecreate.load()) {
        surfaceNeedsRecreate.store(false);
        markSwapchainDirty();
    }
    rebuildSwapchainIfNeeded();
}

void Graphics::begin3DFrameToCanvas(Canvas *canvas) {
    if (!canvas) throw Exception("begin3DFrameToCanvas: null canvas");
    if (!device) throw Exception("begin3DFrameToCanvas: device not initialized");
    active3DCanvas = static_cast<OffscreenCanvas *>(canvas);
    begin3DFrame();
}
void Graphics::end3DFrameToCanvas() {
    OffscreenCanvas *canvas = active3DCanvas;
    if (!canvas || !device) return;
    auto &uboArena = currentUboArena();
    uboArena.reset();
    ensureUboArena(uboArena, 4096);
    auto &vertexArena = currentVertexArena();
    vertexArena.reset();

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    WGPURenderPassColorAttachment color{};
    color.view = canvas->colorView.Get();
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};
    WGPURenderPassDepthStencilAttachment depth{};
    depth.view = canvas->depthView.Get();
    depth.depthClearValue = 1.f;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.stencilLoadOp = WGPULoadOp_Undefined;
    depth.stencilStoreOp = WGPUStoreOp_Undefined;
    WGPURenderPassDescriptor descriptor{};
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    descriptor.depthStencilAttachment = &depth;
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(
        reinterpret_cast<const wgpu::RenderPassDescriptor *>(&descriptor));
    flushMesh3D(pass, WGPUTextureFormat_RGBA8Unorm, true);
    pass.End();
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
    canvas->clearRequested = false;
    active3DCanvas = nullptr;
    frame3DStarted = false;
    frameHad3DThisFrame = false;
    frameHad3D = false;
}

void Graphics::setMesh3DViewProj(const glm::mat4 &viewProj) { mesh3dViewProj = viewProj; }
void Graphics::setMesh3DView(const glm::mat4 &view) { mesh3dView = view; }
void Graphics::setMesh3DClip(float nearZ, float farZ) {
    mesh3dNear = nearZ;
    mesh3dFar = farZ;
}

Texture *Graphics::getSceneColorTexture() { return sceneColorTexture; }

void Graphics::drawMesh(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint) {
    drawMeshShader(mesh, model, texture, tint, nullptr);
}

void Graphics::drawMeshShader(Mesh *mesh, const glm::mat4 &model, Texture *texture, const Color &tint,
                              Shader *shader) {
    if (!mesh || !mesh->gpuHandle) return;
    frameHad3DThisFrame = true;
    frameHad3D = true;
    Mesh3dDraw d;
    d.mesh = mesh;
    d.texture = texture;
    d.model = model;
    d.tint = tint;
    d.shader = shader;
    d.surfaceMode = mesh3dSurfaceMode;
    d.surfaceBlend = mesh3dSurfaceBlend;
    d.depthWrite = mesh3dSurfaceDepthWrite;
    d.doubleSided = mesh3dSurfaceDoubleSided;
    d.alphaCutoff = mesh3dAlphaCutoff;
    d.alphaTechnique = mesh3dAlphaTechnique;
    mesh3dDraws.push_back(d);
}

void Graphics::setMesh3DNormalTexture(Texture *normal) { mesh3dNormalTexture = normal; }
void Graphics::setMesh3DHeightTexture(Texture *height) { mesh3dHeightTexture = height; }
void Graphics::setMesh3DSceneDepth(Texture *depth) {
    // Custom SPIR-V mesh shaders (incl. xray) are unsupported on the WebGPU
    // backend (newMeshShaderFromSpv throws); store the handle for API parity so
    // callers work unchanged and a future WGSL xray path can sample it.
    mesh3dSceneDepthTexture = depth;
}
void Graphics::setMesh3DMaterial(float metallic, float roughness) {
    mesh3dMetallic = metallic;
    mesh3dRoughness = roughness;
}
void Graphics::setMesh3DSurface(SurfaceMode mode, BlendMode blend, bool depthWrite,
                                bool doubleSided, float alphaCutoff,
                                const std::string &alphaTechnique) {
    mesh3dSurfaceMode = mode;
    mesh3dSurfaceBlend = blend;
    mesh3dSurfaceDepthWrite = depthWrite;
    mesh3dSurfaceDoubleSided = doubleSided;
    mesh3dAlphaCutoff = std::clamp(alphaCutoff, 0.f, 1.f);
    mesh3dAlphaTechnique = alphaTechnique;
}
void Graphics::setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount) {
    mesh3dTexBombScale = cellScale;
    mesh3dTexBombStrength = strength;
    mesh3dTexBombRot = rotAmount;
}
void Graphics::setMesh3DParallax(float scale, float minLayers, float maxLayers) {
    mesh3dParallaxScale = scale;
    mesh3dParallaxMin = minLayers;
    mesh3dParallaxMax = maxLayers;
}
void Graphics::setMesh3DLighting(const Lighting3DPack &pack) { mesh3dLighting = pack; }
void Graphics::setCloudShadows(float strength, float worldCell, float time, float windSpeed,
                               float windAngle, float coverage, float detail) {
    mesh3dCloud     = glm::vec4(std::clamp(strength, 0.f, 1.f), std::max(worldCell, 1e-4f), time, 0.f);
    mesh3dCloudWind = glm::vec4(std::cos(windAngle) * windSpeed, std::sin(windAngle) * windSpeed,
                                std::clamp(coverage, 0.f, 1.f), std::clamp(detail, 0.f, 1.f));
}
void Graphics::setMesh3DClusteredLighting(const ClusteredLightingUpload &upload) {
    mesh3dClustered       = upload;
    mesh3dClusteredActive = upload.active;
    if (upload.active) uploadClusteredLighting(upload);
}
void Graphics::setMesh3DClusteredActive(bool active) { mesh3dClusteredActive = active; }
void Graphics::setMesh3DSSAO(float intensity) {
    mesh3dSsaoIntensity = std::clamp(intensity, 0.f, 1.f);
}

void Graphics::uploadClusteredLighting(const ClusteredLightingUpload &upload) {
    if (!device) return;
    ClusteredStorage &st = clusteredStorage[currentFrameSlot()];
    auto ensure = [&](wgpu::Buffer &buf, uint64_t &cap, uint64_t need) {
        if (need == 0) need = 4;
        if (buf && cap >= need) return;
        WGPUBufferDescriptor bd{};
        bd.label = sv("eve_clustered_ssbo");
        bd.size = std::max(need, cap ? cap * 2 : need);
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
        bd.mappedAtCreation = false;
        buf = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
        cap = bd.size;
    };
    const uint64_t lightsBytes =
        std::max<uint64_t>(1, upload.lights.size()) * sizeof(ClusteredLightGpu);
    const uint64_t tableBytes =
        std::max<uint64_t>(1, upload.clusterTable.size()) * sizeof(ClusterTableEntry);
    const uint64_t indicesBytes =
        std::max<uint64_t>(1, upload.lightIndices.size()) * sizeof(uint32_t);
    ensure(st.lights, st.lightsCap, lightsBytes);
    ensure(st.table, st.tableCap, tableBytes);
    ensure(st.indices, st.indicesCap, indicesBytes);

    ClusteredLightGpu zero{};
    if (!upload.lights.empty())
        queue.WriteBuffer(st.lights, 0, upload.lights.data(), lightsBytes);
    else
        queue.WriteBuffer(st.lights, 0, &zero, sizeof(zero));
    if (!upload.clusterTable.empty())
        queue.WriteBuffer(st.table, 0, upload.clusterTable.data(), tableBytes);
    if (!upload.lightIndices.empty())
        queue.WriteBuffer(st.indices, 0, upload.lightIndices.data(), indicesBytes);
}
void Graphics::setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) {
    mesh3dLighting.lights[0].posRadius = glm::vec4(dir, 0.f);
    mesh3dLighting.lights[0].color     = glm::vec4(color, 1.f);
}
void Graphics::setMesh3DCameraPos(const glm::vec3 &eye) { mesh3dCameraPos = eye; }
void Graphics::setMesh3DEnv(Texture *cube, float intensity) {
    mesh3dEnvTexture   = cube;
    mesh3dEnvIntensity = intensity;
}
void Graphics::setMesh3DShadows(const ShadowUpload &upload) { mesh3dShadows = upload; }
void Graphics::setMesh3DShadowReceive(bool receive) { mesh3dShadowReceive = receive; }

void Graphics::beginShadowPass(int cascadeIndex) {
    shadowPassCascade = cascadeIndex;
    shadowPassDraws.clear();
}

void Graphics::drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) {
    if (!mesh || !mesh->gpuHandle) return;
    ShadowDraw d;
    d.mesh = mesh;
    d.mvp = lightMVP;
    shadowPassDraws.push_back(d);
}

void Graphics::drawMeshShadowAlpha(Mesh *mesh, const glm::mat4 &lightMVP, Texture *albedo) {
    if (!mesh || !mesh->gpuHandle) return;
    ShadowDraw d;
    d.mesh = mesh;
    d.albedo = albedo;
    d.mvp = lightMVP;
    d.alphaTest = true;
    shadowPassDraws.push_back(d);
}

void Graphics::endShadowPass() {
    if (shadowPassCascade < 0 || shadowPassCascade >= ShadowConfig::kCascades) {
        shadowPassCascade = -1;
        shadowPassDraws.clear();
        return;
    }
    shadowCascadeDraws[shadowPassCascade] = shadowPassDraws;
    shadowPassDraws.clear();
    shadowPassCascade = -1;
}

// ---------------------------------------------------------------------------
// GBuffer pass
// ---------------------------------------------------------------------------

void Graphics::beginGBufferPass(int width, int height) {
    if (!device) return;
    createGbufferResources(width, height);
    gbufferPassActive = true;
    gbufferPassPending = true;
    gbufferPassDraws.clear();
}

void Graphics::drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model, float nearZ,
                               float farZ, Texture *albedo, float tintR, float tintG, float tintB) {
    if (!mesh || !mesh->gpuHandle) return;
    GbufferDraw d;
    d.mesh = mesh;
    d.albedo = albedo;
    d.mvp = mvp;
    d.model = model;
    d.nearZ = nearZ;
    d.farZ = farZ;
    d.tint = glm::vec4(tintR, tintG, tintB, 1.f);
    gbufferPassDraws.push_back(d);
}

void Graphics::drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                    float nearZ, float farZ, Texture *albedo, float tintR,
                                    float tintG, float tintB) {
    if (!mesh || !mesh->gpuHandle) return;
    GbufferDraw d;
    d.mesh = mesh;
    d.albedo = albedo;
    d.mvp = mvp;
    d.model = model;
    d.nearZ = nearZ;
    d.farZ = farZ;
    d.tint = glm::vec4(tintR, tintG, tintB, 1.f);
    d.alphaTest = true;
    gbufferPassDraws.push_back(d);
}

void Graphics::endGBufferPass() {
    gbufferPassActive = false;
    // Expose the G-buffer textures (depth/normal/albedo/hwDepth) to RenderControl
    // so post passes (AO, X-ray scene depth) can sample them this frame.
    if (renderControl_ && !gbufferSlots.empty()) {
        GbufferSlot &slot = gbufferSlots[currentFrameSlot()];
        renderControl_->getGBuffer()->setTargets(gbufferWidth, gbufferHeight, &slot.depthColorTex,
                                                 &slot.normalTex, &slot.albedoTex, &slot.depthTex);
    }
}

void Graphics::beginDecalPass(int width, int height) {
    if (!device || width <= 0 || height <= 0) return;
    createDecalResources(width, height);
    if (!decalFlatAlbedo) {
        const uint8_t transparent[4] = {0, 0, 0, 0};
        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        const uint8_t neutralParams[4] = {128, 128, 0, 255};
        decalFlatAlbedo = newTexture(1, 1, transparent);
        decalFlatNormal = newTexture(1, 1, flatNormal);
        decalFlatParams = newTexture(1, 1, neutralParams);
    }
    decalPassActive = true;
    decalPassPending = false;
    decalPassDraws.clear();
}

void Graphics::setDecalCamera(const glm::mat4 &viewProj, float nearZ, float farZ) {
    (void)nearZ;
    (void)farZ;
    decalViewProj = viewProj;
}

void Graphics::drawDecal(const glm::mat4 &model, Texture *albedo, Texture *normal,
                         Texture *params, const float uvRect[4], float fade,
                         float normalStrength, float roughnessStrength, float metalStrength,
                         float emissiveStrength, int blendMode) {
    if (!decalPassActive) return;
    DecalDraw draw;
    draw.model = model;
    draw.albedo = albedo ? albedo : decalFlatAlbedo;
    draw.normal = normal ? normal : decalFlatNormal;
    draw.params = params ? params : decalFlatParams;
    if (uvRect) draw.uvRect = glm::vec4(uvRect[0], uvRect[1], uvRect[2], uvRect[3]);
    draw.fadeParams = glm::vec4(fade, normalStrength, roughnessStrength, metalStrength);
    draw.extraParams = glm::vec4(emissiveStrength, float(blendMode == 1), 0.f, 0.f);
    decalPassDraws.push_back(draw);
}

void Graphics::endDecalPass() {
    if (!decalPassActive) return;
    decalPassActive = false;
    decalPassPending = true;
}

// ---------------------------------------------------------------------------
// Voxel
// ---------------------------------------------------------------------------

void Graphics::drawVoxelFaceInstances(const uint32_t *packed, int count, float originX,
                                      float originY, float originZ, const std::string &faceDir,
                                      Texture *atlas, int tilesPerRow, const uint32_t *ao) {
    if (!device || count <= 0 || !packed) return;
    if (!voxelUnitQuadVerts) return;
    frameHad3DThisFrame = true;
    frameHad3D = true;

    int face = 5;
    if (faceDir == "posX" || faceDir == "+x") face = 0;
    else if (faceDir == "negX" || faceDir == "-x") face = 1;
    else if (faceDir == "posY" || faceDir == "+y") face = 2;
    else if (faceDir == "negY" || faceDir == "-y") face = 3;
    else if (faceDir == "posZ" || faceDir == "+z") face = 4;

    VoxelDraw d;
    d.count = uint32_t(count);
    d.atlas = gpuForTextureOrWhite(atlas);
    d.viewProj = mesh3dViewProj;
    d.chunkOrigin = glm::vec4(originX, originY, originZ, float(face));
    d.atlasInfo = glm::vec4(float(tilesPerRow), 0.f, 0.f, 0.f);
    d.tint = glm::vec4(1.f);
    d.instanceBufferOffset = 0;
    d.pushUboOffset = 0;

    // Upload packed instances + the parallel AO word (2 bits per corner) into
    // the per-frame instance arenas.
    auto &arena = voxelInstanceArena;
    auto &aoArena = voxelAoArena;
    auto ensureArena = [&](VertexArena &a) {
        if (!a.buffer) {
            WGPUBufferDescriptor bd{};
            bd.label = sv("eve_voxel_instances");
            bd.size = 1u << 20;
            bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
            bd.mappedAtCreation = false;
            a.buffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
            a.capacity = 1u << 20;
        }
        uint64_t need = uint64_t(count) * 4;
        if (a.used + need > a.capacity) {
            uint64_t cap = a.capacity * 2;
            WGPUBufferDescriptor bd{};
            bd.label = sv("eve_voxel_instances");
            bd.size = cap;
            bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
            bd.mappedAtCreation = false;
            a.buffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
            a.capacity = cap;
            a.used = 0;
        }
    };
    ensureArena(arena);
    ensureArena(aoArena);
    const uint32_t defaultAO = 0xFFu;  // all four corners AO=3 (full bright)
    std::vector<uint32_t> aoDefaults;
    if (!ao) {
        aoDefaults.assign(size_t(count), defaultAO);
        ao = aoDefaults.data();
    }
    const uint64_t need = uint64_t(count) * 4;
    d.instanceBufferOffset = static_cast<uint32_t>(arena.used);
    queue.WriteBuffer(arena.buffer, arena.used, packed, need);
    arena.used += need;
    d.aoBufferOffset = static_cast<uint32_t>(aoArena.used);
    queue.WriteBuffer(aoArena.buffer, aoArena.used, ao, need);
    aoArena.used += need;
    voxelDraws.push_back(d);
}

// ---------------------------------------------------------------------------
// Scene color / shadow / gbuffer resources
// ---------------------------------------------------------------------------

void Graphics::createSceneColorResources(int width, int height) {
    if (!device) return;
    if (sceneColorWidth == width && sceneColorHeight == height && !sceneColorSlots.empty()) return;

    destroySceneColorResources();
    sceneColorWidth = width;
    sceneColorHeight = height;
    // WebGPU only supports 1x and 4x multisampling; clamp anything >= 2 to 4.
    const uint32_t want = msaaSamples >= 2 ? 4u : 1u;
    const uint32_t oldSamples = sceneColorSamples;
    const bool samplesChanged = oldSamples != want;
    sceneColorSamples = want;
    sceneColorSlots.reserve(kFramesInFlight);
    for (int s = 0; s < int(kFramesInFlight); ++s) {
        sceneColorSlots.emplace_back();
        SceneColorSlot &slot = sceneColorSlots.back();
        slot.sampleCount = sceneColorSamples;

        WGPUTextureDescriptor cd{};
        cd.label = sv("eve_scene_color");
        cd.dimension = WGPUTextureDimension_2D;
        cd.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        // Single-sample resolve target: composited to the swapchain and used
        // as the frame-readback source. The multisampled color lives in
        // msaaColor (created below) and is resolved into this texture.
        cd.sampleCount = 1;
        cd.format = sceneColorFormat;
        cd.mipLevelCount = 1;
        cd.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
                   WGPUTextureUsage_CopySrc;
        slot.color = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&cd));
        slot.colorView = slot.color.CreateView();

        if (sceneColorSamples > 1) {
            // Multisampled color target; the scene pass resolves it into the
            // single-sample `color` above for compositing / readback.
            WGPUTextureDescriptor mcd{};
            mcd.label = sv("eve_scene_color_msaa");
            mcd.dimension = WGPUTextureDimension_2D;
            mcd.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
            mcd.sampleCount = sceneColorSamples;
            mcd.format = sceneColorFormat;
            mcd.mipLevelCount = 1;
            mcd.usage = WGPUTextureUsage_RenderAttachment;
            slot.msaaColor =
                device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&mcd));
            slot.msaaView = slot.msaaColor.CreateView();
        }

        WGPUTextureDescriptor dd{};
        dd.label = sv("eve_scene_depth");
        dd.dimension = WGPUTextureDimension_2D;
        dd.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        dd.sampleCount = sceneColorSamples;
        dd.format = WGPUTextureFormat_Depth32Float;
        dd.mipLevelCount = 1;
        dd.usage = WGPUTextureUsage_RenderAttachment;
        slot.depth = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&dd));
        slot.depthView = slot.depth.CreateView();

        slot.colorGpu.texture = slot.color;
        slot.colorGpu.view = slot.colorView;
        slot.colorGpu.width = width;
        slot.colorGpu.height = height;
        slot.colorGpu.sampler = createLinearSampler(device);
        slot.colorGpu.samplerState = TextureSampler::linearMipmap();

        slot.colorTex.gpuHandle = &slot.colorGpu;
        slot.colorTex.width = width;
        slot.colorTex.height = height;
        slot.colorTex.mipmapCount = 1;

    }
    sceneColorTexture = &sceneColorSlots[0].colorTex;
    // 3D pipelines that render into the scene target must match its sample
    // count; rebuild them when the count changes (first configure defaults to
    // 1, then the engine's msaaSamples (default 4) takes effect).
    if (samplesChanged && mesh3dPipeline) {
        createMesh3DPipelines();
        createVoxelPipelines();
    }
}

void Graphics::destroySceneColorResources() {
    sceneColorSlots.clear();
    sceneColorTexture = nullptr;
}

void Graphics::setMsaaSamples(int samples) {
    msaaSamples = samples > 0 ? samples : 0;
    if (!initialized || sceneColorWidth <= 0 || sceneColorHeight <= 0) return;
    // Force the offscreen targets (and the 3D pipelines that bind them) to
    // rebuild at the new sample count.
    destroySceneColorResources();
    createSceneColorResources(sceneColorWidth, sceneColorHeight);
}

void Graphics::createShadowResources() {
    if (!device) return;
    if (shadowDepthArray) return;
    shadowMapSize = ShadowConfig::kMapSize;

    // Depth array: 3 cascade layers, renderable + sampleable.
    auto *gpu = new GpuTexture();
    WGPUTextureDescriptor td{};
    td.label = sv("eve_shadow_depth");
    td.dimension = WGPUTextureDimension_2D;
    td.size = {static_cast<uint32_t>(shadowMapSize), static_cast<uint32_t>(shadowMapSize), 3};
    td.sampleCount = 1;
    td.format = WGPUTextureFormat_Depth32Float;
    td.mipLevelCount = 1;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
               WGPUTextureUsage_CopySrc;
    gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));

    WGPUTextureViewDescriptor avd{};
    avd.format = WGPUTextureFormat_Depth32Float;
    avd.dimension = WGPUTextureViewDimension_2DArray;
    avd.baseMipLevel = 0;
    avd.mipLevelCount = 1;
    avd.baseArrayLayer = 0;
    avd.arrayLayerCount = 3;
    gpu->view = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&avd));

    WGPUSamplerDescriptor sd{};
    sd.label = sv("eve_shadow_sampler");
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.compare = WGPUCompareFunction_LessEqual;
    sd.maxAnisotropy = 1.f;
    gpu->sampler = device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&sd));
    shadowDepthArray = gpu;
}

void Graphics::destroyShadowResources() {
    shadowDepthArray = nullptr;
}

void Graphics::createGbufferResources(int width, int height) {
    if (!device) return;
    if (gbufferWidth == width && gbufferHeight == height && !gbufferSlots.empty()) return;
    destroyGbufferResources();
    gbufferWidth = width;
    gbufferHeight = height;

    WGPUTextureDescriptor td{};
    td.label = sv("eve_gbuffer_target");
    td.dimension = WGPUTextureDimension_2D;
    td.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    td.sampleCount = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
               WGPUTextureUsage_CopySrc;

    WGPUTextureDescriptor dd{};
    dd.label = sv("eve_gbuffer_depth");
    dd.dimension = WGPUTextureDimension_2D;
    dd.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    dd.sampleCount = 1;
    dd.format = WGPUTextureFormat_Depth32Float;
    dd.mipLevelCount = 1;
    // TextureBinding so X-ray (and AO) can sample the scene depth in a later pass.
    dd.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;

    gbufferSlots.reserve(kFramesInFlight);
    for (int s = 0; s < int(kFramesInFlight); ++s) {
        gbufferSlots.emplace_back();
        GbufferSlot &slot = gbufferSlots.back();
        slot.normal = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.normalView = slot.normal.CreateView();
        slot.depthColor = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.depthColorView = slot.depthColor.CreateView();
        slot.albedo = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.albedoView = slot.albedo.CreateView();
        slot.depth = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&dd));
        slot.depthView = slot.depth.CreateView();

        slot.normalGpu.texture = slot.normal;
        slot.normalGpu.view = slot.normalView;
        slot.normalGpu.sampler = createLinearSampler(device);
        slot.depthColorGpu.texture = slot.depthColor;
        slot.depthColorGpu.view = slot.depthColorView;
        slot.depthColorGpu.sampler = createLinearSampler(device);
        slot.albedoGpu.texture = slot.albedo;
        slot.albedoGpu.view = slot.albedoView;
        slot.albedoGpu.sampler = createLinearSampler(device);
        slot.depthGpu.texture = slot.depth;
        slot.depthGpu.view = slot.depthView;
        slot.depthGpu.sampler = createLinearSampler(device);

        slot.normalTex.gpuHandle = &slot.normalGpu;
        slot.normalTex.width = width;
        slot.normalTex.height = height;
        slot.depthColorTex.gpuHandle = &slot.depthColorGpu;
        slot.depthColorTex.width = width;
        slot.depthColorTex.height = height;
        slot.albedoTex.gpuHandle = &slot.albedoGpu;
        slot.albedoTex.width = width;
        slot.albedoTex.height = height;
        slot.depthTex.gpuHandle = &slot.depthGpu;
        slot.depthTex.width = width;
        slot.depthTex.height = height;

    }
}

void Graphics::destroyGbufferResources() { gbufferSlots.clear(); }

void Graphics::createDecalResources(int width, int height) {
    if (!device) return;
    if (decalWidth == width && decalHeight == height && !decalSlots.empty()) return;
    destroyDecalResources();
    decalWidth = width;
    decalHeight = height;
    WGPUTextureDescriptor td{};
    td.label = sv("eve_decal_target");
    td.dimension = WGPUTextureDimension_2D;
    td.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    td.sampleCount = 1;
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
               WGPUTextureUsage_CopySrc;
    decalSlots.resize(kFramesInFlight);
    for (auto &slot : decalSlots) {
        slot.albedo = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor *>(&td));
        slot.albedoView = slot.albedo.CreateView();
        slot.normal = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor *>(&td));
        slot.normalView = slot.normal.CreateView();
        slot.params = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor *>(&td));
        slot.paramsView = slot.params.CreateView();
    }
}

void Graphics::destroyDecalResources() {
    decalSlots.clear();
    decalWidth = 0;
    decalHeight = 0;
}

// ---------------------------------------------------------------------------
// Flush helpers
// ---------------------------------------------------------------------------

void Graphics::flushMesh3D(wgpu::RenderPassEncoder pass, WGPUTextureFormat format,
                           bool canvasTarget) {
    if (mesh3dDraws.empty()) return;

    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + mesh3dDraws.size() * 2048);
    auto &vtxArena = currentVertexArena();

    // Pre-allocate per-draw UBO slots.
    for (auto &d : mesh3dDraws) {
        d.frameUboOffset = uboArena.alloc(sizeof(Mesh3DUBO), 256);
        d.shadowUboOffset = uboArena.alloc(sizeof(ShadowUBO), 256);
        d.clusteredUboOffset = 0;
        if (mesh3dClusteredActive && !canvasTarget)
            d.clusteredUboOffset = uboArena.alloc(sizeof(Mesh3DClusteredUBO), 256);
        d.pushUboOffset = 0;
        if (d.shader && d.shader->pushConstantSize() > 0)
            d.pushUboOffset = uboArena.alloc(Shader::kPushConstantBytes, 256);
    }

    // Upload the per-draw frame UBO + shared shadow UBO.
    for (auto &d : mesh3dDraws) {
        if (mesh3dClusteredActive && !canvasTarget && mesh3dClusteredPipeline && d.clusteredUboOffset) {
            Mesh3DClusteredUBO cubo;
            cubo.mvp = mesh3dViewProj * d.model;
            cubo.model = d.model;
            cubo.view = mesh3dView;
            cubo.lightDir = glm::vec4(glm::vec3(mesh3dClustered.primaryDir),
                                      mesh3dClustered.primaryDir.w);
            cubo.lightColor = glm::vec4(glm::vec3(mesh3dClustered.primaryColor), mesh3dEnvIntensity);
            cubo.tint = d.tint;
            cubo.cameraPos = glm::vec4(mesh3dCameraPos, mesh3dRoughness);
            cubo.ambient = glm::vec4(glm::vec3(mesh3dClustered.ambient), mesh3dMetallic);
            cubo.gridInfo = mesh3dClustered.gridInfo;
            cubo.clipInfo = mesh3dClustered.clipInfo;
            cubo.texBomb =
                glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
            cubo.parallax = glm::vec4(mesh3dParallaxScale, mesh3dParallaxMin, mesh3dParallaxMax, 0.f);
            float surfaceCode = float(int(d.surfaceMode));
            if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "dither")
                surfaceCode = 3.f;
            else if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "coverage")
                surfaceCode = 4.f;
            const float aoStrength =
                (renderControl_ && renderControl_->isEnabled("ao")) ? mesh3dSsaoIntensity : 0.f;
            cubo.surface = glm::vec4(surfaceCode, d.alphaCutoff, aoStrength, 0.f);
            queue.WriteBuffer(uboArena.buffer, d.clusteredUboOffset, &cubo, sizeof(cubo));
            continue;
        }
        Mesh3DUBO ubo;
        ubo.mvp = mesh3dViewProj * d.model;
        ubo.model = d.model;
        ubo.lightDir = glm::vec4(glm::vec3(mesh3dLighting.lights[0].posRadius), float(mesh3dLighting.count));
        ubo.lightColor = mesh3dLighting.lights[0].color;
        ubo.tint = d.tint;
        ubo.cameraPos = glm::vec4(mesh3dCameraPos, mesh3dRoughness);
        ubo.ambient = glm::vec4(glm::vec3(mesh3dLighting.ambient), mesh3dMetallic);
        for (int i = 0; i < Lighting3DPack::kMaxLights; ++i)
            ubo.lights[i] = mesh3dLighting.lights[i];
        float surfaceCode = float(int(d.surfaceMode));
        if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "dither")
            surfaceCode = 3.f;
        else if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "coverage")
            surfaceCode = 4.f;
        ubo.texBomb =
            glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
        ubo.parallax =
            glm::vec4(mesh3dParallaxScale, mesh3dParallaxMin, mesh3dParallaxMax, d.alphaCutoff);
        const float aoStrength =
            (renderControl_ && renderControl_->isEnabled("ao")) ? mesh3dSsaoIntensity : 0.f;
        ubo.surface = glm::vec4(surfaceCode, d.alphaCutoff, aoStrength, 0.f);
        ubo.view = mesh3dView;
        ubo.clipInfo = glm::vec4(mesh3dNear, mesh3dFar, 0.f, 0.f);
        ubo.cloud = mesh3dCloud;
        ubo.cloudWind = mesh3dCloudWind;
        ubo.lightColor.w = mesh3dEnvIntensity;
        // X-ray params travel through the Frame UBO (no extra binding). Packed
        // in bindMeshUniforms("xray") order: colorR..G..B, bias, screenW, screenH,
        // rimPower, rimStrength, alpha.
        if (d.shader && d.shader->isXray() && d.shader->pushConstantSize() >= 9 * sizeof(float)) {
            const float *pc = d.shader->pushConstantData();
            ubo.texBomb = glm::vec4(pc[0], pc[1], pc[2], pc[8]);    // color.xyz + alpha
            ubo.parallax = glm::vec4(pc[3], pc[4], pc[5], pc[7]);   // bias, screenW, screenH, rimStrength
            ubo.clipInfo.z = pc[6];                                 // rimPower
        }
        queue.WriteBuffer(uboArena.buffer, d.frameUboOffset, &ubo, sizeof(ubo));
    }

    // Shared shadow UBO written once.
    ShadowUBO shadowUbo = mesh3dShadows.ubo;
    if (!mesh3dShadows.active || !mesh3dShadowReceive) shadowUbo.bias.y = 0.f;
    for (auto &d : mesh3dDraws)
        queue.WriteBuffer(uboArena.buffer, d.shadowUboOffset, &shadowUbo, sizeof(shadowUbo));

    for (auto &d : mesh3dDraws) {
        auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
        if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

        const bool transparent = d.surfaceMode == SurfaceMode::Transparent;
        const BlendMode blend = transparent ? d.surfaceBlend : BlendMode::Opaque;
        const bool depthWrite = !transparent || d.depthWrite;
        const size_t pipelineIndex = meshPipelineIndex(blend, depthWrite, d.doubleSided);
        // Canvas targets are 1-sample; scene pipelines follow the active MSAA
        // count. Both sets preserve the per-draw material raster state.
        wgpu::RenderPipeline pipe = canvasTarget ? mesh3dCanvasPipelines[pipelineIndex]
                                                  : mesh3dPipelines[pipelineIndex];
        const bool customShader = d.shader && d.shader->gpuHandle;
        if (d.shader && d.shader->gpuHandle) {
            auto *gs = static_cast<GpuShader *>(d.shader->gpuHandle);
            if (gs->isMesh3D && gs->mesh3dPipeline) {
                if (d.shader->isXray() && gs->mesh3dXrayPipeline)
                    pipe = gs->mesh3dXrayPipeline;
                else
                    pipe = gs->mesh3dPipeline;
            }
        }
        const bool useClustered = !canvasTarget && !customShader && !d.doubleSided &&
                                  mesh3dClusteredActive &&
                                  d.surfaceMode != SurfaceMode::Transparent &&
                                  mesh3dClusteredPipeline && d.clusteredUboOffset;
        if (useClustered) pipe = mesh3dClusteredPipeline;
        if (!pipe) continue;
        pass.SetPipeline(pipe);

        GpuTexture *albedo = gpuForTexture(d.texture);
        GpuTexture *normal = gpuForTexture(mesh3dNormalTexture);
        GpuTexture *env = gpuForTexture(mesh3dEnvTexture);
        GpuTexture *height = gpuForTexture(mesh3dHeightTexture);
        GpuTexture *depth = mesh3dSceneDepthTexture ? gpuForTexture(mesh3dSceneDepthTexture)
                                                    : flatDepthTexture3D;
        wgpu::BindGroup bg;
        uint32_t offsets[2];
        if (useClustered) {
            wgpu::TextureView aoView_ =
                aoReady ? aoView[(aoWriteIndex + 1) % 2]
                        : (whiteTexture ? whiteTexture->view : wgpu::TextureView());
            bg = makeMesh3DClusteredBindGroup(albedo, normal, env, height, depth, aoView_,
                                              d.clusteredUboOffset, d.shadowUboOffset);
            offsets[0] = d.clusteredUboOffset;
            offsets[1] = d.shadowUboOffset;
        } else {
            bg = makeMeshBindGroup(albedo, normal, env, height, depth,
                                   d.frameUboOffset, d.shadowUboOffset, d.pushUboOffset);
            offsets[0] = d.frameUboOffset;
            offsets[1] = d.shadowUboOffset;
        }
        pass.SetBindGroup(0, bg, 2, offsets);

        if (gpuMesh->indexBuffer) {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, gpuMesh->vertexCount * 32);
            const uint64_t indexBytes = gpuMesh->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
            pass.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat, 0,
                                uint64_t(gpuMesh->indexCount) * indexBytes);
            pass.DrawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
        } else {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, gpuMesh->vertexCount * 32);
            pass.Draw(gpuMesh->vertexCount, 1, 0, 0);
        }
    }
    mesh3dDraws.clear();
}

void Graphics::flushShadowPass(wgpu::RenderPassEncoder pass) {
    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + 4096);
    for (int c = 0; c < ShadowConfig::kCascades; ++c) {
        if (shadowCascadeDraws[c].empty()) continue;
        for (auto &d : shadowCascadeDraws[c]) {
            auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
            if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

            pass.SetPipeline(d.alphaTest ? mesh3dShadowAlphaPipeline : mesh3dShadowPipeline);

            uint32_t offset = uboArena.alloc(256, 256);
            queue.WriteBuffer(uboArena.buffer, offset, &d.mvp, sizeof(glm::mat4));

            GpuTexture *albedo = gpuForTextureOrWhite(d.albedo);
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer = uboArena.buffer.Get();
            entries[0].size = 64;
            entries[1].binding = 1;
            entries[1].textureView = albedo->view.Get();
            entries[2].binding = 2;
            entries[2].sampler = albedo->sampler.Get();
            WGPUBindGroupDescriptor bgd{};
            bgd.layout = shadowSetLayout.Get();
            bgd.entryCount = 3;
            bgd.entries = entries;
            wgpu::BindGroup bg = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&bgd));
            uint32_t offsets[1] = {offset};
            pass.SetBindGroup(0, bg, 1, offsets);

            if (gpuMesh->indexBuffer) {
                pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, gpuMesh->vertexCount * 32);
                const uint64_t indexBytes = gpuMesh->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
                pass.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat, 0,
                                    uint64_t(gpuMesh->indexCount) * indexBytes);
                pass.DrawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
            } else {
                pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, gpuMesh->vertexCount * 32);
                pass.Draw(gpuMesh->vertexCount, 1, 0, 0);
            }
        }
        shadowCascadeDraws[c].clear();
    }
}

void Graphics::flushGbufferPass(wgpu::RenderPassEncoder pass) {
    if (gbufferPassDraws.empty() || gbufferSlots.empty()) return;
    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + gbufferPassDraws.size() * 512);
    for (auto &d : gbufferPassDraws) {
        auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
        if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

        pass.SetPipeline(d.alphaTest ? mesh3dGbufferAlphaPipeline : mesh3dGbufferPipeline);

        struct GbufferPush {
            glm::mat4 mvp;
            glm::vec4 modelR0;
            glm::vec4 modelR1;
            glm::vec4 modelR2;
            glm::vec4 clip;
        } push;
        static_assert(sizeof(GbufferPush) == 128);
        push.mvp = d.mvp;
        push.modelR0 = glm::vec4(d.model[0][0], d.model[1][0], d.model[2][0], d.model[3][0]);
        push.modelR1 = glm::vec4(d.model[0][1], d.model[1][1], d.model[2][1], d.model[3][1]);
        push.modelR2 = glm::vec4(d.model[0][2], d.model[1][2], d.model[2][2], d.model[3][2]);
        auto u8 = [](float value) {
            return uint32_t(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
        };
        const uint32_t packedTint = u8(d.tint.r) | (u8(d.tint.g) << 8) | (u8(d.tint.b) << 16) |
                                    (255u << 24);
        push.clip = glm::vec4(d.nearZ, d.farZ, glm::uintBitsToFloat(packedTint), 0.f);

        uint32_t offset = uboArena.alloc(256, 256);
        queue.WriteBuffer(uboArena.buffer, offset, &push, sizeof(push));

        GpuTexture *albedo = gpuForTextureOrWhite(d.albedo);
        WGPUBindGroupEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].buffer = uboArena.buffer.Get();
        entries[0].size = 128;
        entries[1].binding = 1;
        entries[1].textureView = albedo->view.Get();
        entries[2].binding = 2;
        entries[2].sampler = albedo->sampler.Get();
        WGPUBindGroupDescriptor bgd{};
        bgd.layout = gbufferSetLayout.Get();
        bgd.entryCount = 3;
        bgd.entries = entries;
        wgpu::BindGroup bg = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&bgd));
        uint32_t offsets[1] = {offset};
        pass.SetBindGroup(0, bg, 1, offsets);

        if (gpuMesh->indexBuffer) {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, gpuMesh->vertexCount * 32);
            const uint64_t indexBytes = gpuMesh->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
            pass.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat, 0,
                                uint64_t(gpuMesh->indexCount) * indexBytes);
            pass.DrawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
        } else {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, gpuMesh->vertexCount * 32);
            pass.Draw(gpuMesh->vertexCount, 1, 0, 0);
        }
    }
    gbufferPassDraws.clear();
    gbufferPassPending = false;
}

void Graphics::flushDecalPass(wgpu::RenderPassEncoder pass) {
    if (decalPassDraws.empty() || gbufferSlots.empty()) {
        decalPassPending = false;
        return;
    }
    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + decalPassDraws.size() * 512);
    GbufferSlot &gbuffer = gbufferSlots[lastGbufferSlot];
    pass.SetPipeline(decalPipeline);
    struct DecalUniforms {
        glm::mat4 invViewProj;
        glm::mat4 invModel;
        glm::vec4 modelR0;
        glm::vec4 modelR1;
        glm::vec4 modelR2;
        glm::vec4 uvRect;
        glm::vec4 fadeParams;
        glm::vec4 extraParams;
        glm::vec4 texel;
    };
    static_assert(sizeof(DecalUniforms) == 240);
    for (const auto &draw : decalPassDraws) {
        DecalUniforms uniforms{};
        uniforms.invViewProj = glm::inverse(decalViewProj);
        uniforms.invModel = glm::inverse(draw.model);
        uniforms.modelR0 = draw.model[0];
        uniforms.modelR1 = draw.model[1];
        uniforms.modelR2 = draw.model[2];
        uniforms.uvRect = draw.uvRect;
        uniforms.fadeParams = draw.fadeParams;
        uniforms.extraParams = draw.extraParams;
        uniforms.texel = glm::vec4(1.f / float(decalWidth), 1.f / float(decalHeight), 0.f, 0.f);
        uint32_t offset = uboArena.alloc(256, 256);
        queue.WriteBuffer(uboArena.buffer, offset, &uniforms, sizeof(uniforms));
        GpuTexture *albedo = gpuForTextureOrWhite(draw.albedo);
        GpuTexture *normal = gpuForTextureOrWhite(draw.normal);
        GpuTexture *params = gpuForTextureOrWhite(draw.params);
        WGPUBindGroupEntry entries[7]{};
        entries[0].binding = 0;
        entries[0].buffer = uboArena.buffer.Get();
        entries[0].size = sizeof(uniforms);
        entries[1].binding = 1;
        entries[1].textureView = albedo->view.Get();
        entries[2].binding = 2;
        entries[2].textureView = normal->view.Get();
        entries[3].binding = 3;
        entries[3].textureView = params->view.Get();
        entries[4].binding = 4;
        entries[4].textureView = gbuffer.depthView.Get();
        entries[5].binding = 5;
        entries[5].textureView = gbuffer.normalView.Get();
        entries[6].binding = 6;
        entries[6].sampler = mainSampler.Get();
        WGPUBindGroupDescriptor descriptor{};
        descriptor.layout = decalSetLayout.Get();
        descriptor.entryCount = 7;
        descriptor.entries = entries;
        wgpu::BindGroup group = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&descriptor));
        uint32_t offsets[1] = {offset};
        pass.SetBindGroup(0, group, 1, offsets);
        pass.Draw(3, 1, 0, 0);
    }
    decalPassDraws.clear();
    decalPassPending = false;
    decalReady = true;
    clearMeshBindGroupCache();
}

void Graphics::submitPendingDeferredPasses() {
    if ((!gbufferPassPending || gbufferSlots.empty()) &&
        (!decalPassPending || decalSlots.empty()))
        return;
    auto &uboArena = currentUboArena();
    uboArena.reset();
    ensureUboArena(uboArena, 4096 + (gbufferPassDraws.size() + decalPassDraws.size()) * 512);
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    if (gbufferPassPending && !gbufferSlots.empty()) {
        lastGbufferSlot = currentFrameSlot();
        GbufferSlot &slot = gbufferSlots[lastGbufferSlot];
        WGPURenderPassColorAttachment colors[3]{};
        for (auto &color : colors) {
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = WGPUStoreOp_Store;
            color.clearValue = {0.f, 0.f, 0.f, 0.f};
        }
        colors[1].clearValue = {1.f, 1.f, 1.f, 1.f};
        colors[0].view = slot.normalView.Get();
        colors[1].view = slot.depthColorView.Get();
        colors[2].view = slot.albedoView.Get();
        WGPURenderPassDepthStencilAttachment depth{};
        depth.view = slot.depthView.Get();
        depth.depthClearValue = 1.f;
        depth.depthLoadOp = WGPULoadOp_Clear;
        depth.depthStoreOp = WGPUStoreOp_Store;
        depth.stencilLoadOp = WGPULoadOp_Undefined;
        depth.stencilStoreOp = WGPUStoreOp_Undefined;
        WGPURenderPassDescriptor descriptor{};
        descriptor.colorAttachmentCount = 3;
        descriptor.colorAttachments = colors;
        descriptor.depthStencilAttachment = &depth;
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(
            reinterpret_cast<const wgpu::RenderPassDescriptor *>(&descriptor));
        flushGbufferPass(pass);
        pass.End();
    }
    if (decalPassPending && !decalSlots.empty() && !gbufferSlots.empty()) {
        lastDecalSlot = currentFrameSlot();
        DecalSlot &slot = decalSlots[lastDecalSlot];
        WGPURenderPassColorAttachment colors[3]{};
        for (auto &color : colors) {
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = WGPUStoreOp_Store;
            color.clearValue = {0.f, 0.f, 0.f, 0.f};
        }
        colors[0].view = slot.albedoView.Get();
        colors[1].view = slot.normalView.Get();
        colors[2].view = slot.paramsView.Get();
        WGPURenderPassDescriptor descriptor{};
        descriptor.colorAttachmentCount = 3;
        descriptor.colorAttachments = colors;
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(
            reinterpret_cast<const wgpu::RenderPassDescriptor *>(&descriptor));
        flushDecalPass(pass);
        pass.End();
    }
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
}

void Graphics::flushVoxelDraws(wgpu::RenderPassEncoder pass, WGPUTextureFormat format) {
    if (voxelDraws.empty() || !voxelRectPipeline) return;
    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + voxelDraws.size() * 512);
    pass.SetPipeline(voxelRectPipeline);

    for (auto &d : voxelDraws) {
        uint32_t offset = uboArena.alloc(256, 256);
        struct VoxelPC {
            glm::mat4 viewProj;
            glm::vec4 chunkOrigin;
            glm::vec4 atlasInfo;
            glm::vec4 tint;
        } pc;
        pc.viewProj = d.viewProj;
        pc.chunkOrigin = d.chunkOrigin;
        pc.atlasInfo = d.atlasInfo;
        pc.tint = d.tint;
        queue.WriteBuffer(uboArena.buffer, offset, &pc, sizeof(pc));

        WGPUBindGroupEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].buffer = uboArena.buffer.Get();
        entries[0].size = sizeof(pc);
        entries[1].binding = 1;
        entries[1].textureView = d.atlas->view.Get();
        entries[2].binding = 2;
        entries[2].sampler = d.atlas->sampler.Get();
        WGPUBindGroupDescriptor bgd{};
        bgd.layout = voxelSetLayout.Get();
        bgd.entryCount = 3;
        bgd.entries = entries;
        wgpu::BindGroup bg = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&bgd));
        uint32_t offsets[1] = {offset};
        pass.SetBindGroup(0, bg, 1, offsets);

        pass.SetVertexBuffer(0, voxelUnitQuadVerts, 0, 32);
        pass.SetVertexBuffer(1, voxelInstanceArena.buffer, d.instanceBufferOffset,
                             uint64_t(d.count) * 4);
        pass.SetVertexBuffer(2, voxelAoArena.buffer, d.aoBufferOffset,
                             uint64_t(d.count) * 4);
        pass.SetIndexBuffer(voxelUnitQuadIndices, wgpu::IndexFormat::Uint32, 0, 24);
        pass.DrawIndexed(6, d.count, 0, 0, 0);
    }
    voxelDraws.clear();
}

// ---------------------------------------------------------------------------
// Present
// ---------------------------------------------------------------------------

bool Graphics::acquireSurfaceTexture(wgpu::TextureView &view, wgpu::Texture &texture) {
    if (!surface || !device || !swapchainConfigured) return false;
    wgpu::SurfaceTexture surfTex{};
    surface.GetCurrentTexture(&surfTex);
    if (!surfTex.texture) return false;
    texture = surfTex.texture;
    view = texture.CreateView();
    return bool(view);
}

void Graphics::pushValidationScope() {
#ifdef EVENGINE_WEBGPU
    if (device) device.PushErrorScope(wgpu::ErrorFilter::Validation);
#endif
}

void Graphics::popValidationScope() {
#ifdef EVENGINE_WEBGPU
    if (!device) return;
    device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
        [](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
            (void)status;
            if (message.data && type != wgpu::ErrorType::NoError) {
#if defined(__EMSCRIPTEN__)
                EM_ASM({ console.log("[GPU_ERR] type=" + $0 + " msg=" + UTF8ToString($1)); },
                       (int)type, message.data);
#else
                std::fprintf(stderr, "[GPU_ERR] type=%d msg=%.*s\n", static_cast<int>(type),
                             static_cast<int>(message.length), message.data);
#endif
            }
        });
#endif
}

struct Graphics::PendingReadback {
    std::string path;
    int width = 0;
    int height = 0;
    uint64_t bytesPerRow = 0;
    wgpu::Buffer dst;
    bool mapped = false;
    bool done = false;
    bool ok = false;
};

void Graphics::present() {
    if (!device || !surface || !swapchainConfigured) return;
    pumpReadback();
    rebuildSwapchainIfNeeded();
    if (!swapchainConfigured) return;
    const bool aoActive = renderControl_ && renderControl_->isEnabled("ao");

    wgpu::TextureView surfaceView;
    wgpu::Texture surfaceTex;
    if (!acquireSurfaceTexture(surfaceView, surfaceTex)) {
        return;
    }

    auto &uboArena = currentUboArena();
    uboArena.reset();
    auto &vtxArena = currentVertexArena();
    vtxArena.reset();
    voxelInstanceArena.used = 0;
    voxelAoArena.used = 0;
    ensureUboArena(uboArena, 4096);
    ensureVertexArena(vtxArena, 4096);
    // Match the desktop (Vulkan) flow: the 3D/swapchain pass clears with the
    // background set by setBackgroundColor unless the script called clear().
    if (!hasPendingClear) clearColor = backgroundColor;
    hasPendingClear = false;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

    // 1. Shadow passes (CSM cascade layers).
    if (shadowDepthArray) {
        bool anyShadow = false;
        for (int c = 0; c < ShadowConfig::kCascades; ++c)
            if (!shadowCascadeDraws[c].empty()) anyShadow = true;
        if (anyShadow) {
            for (int c = 0; c < ShadowConfig::kCascades; ++c) {
                if (shadowCascadeDraws[c].empty()) continue;
                WGPUTextureViewDescriptor lvd{};
                lvd.format = WGPUTextureFormat_Depth32Float;
                lvd.dimension = WGPUTextureViewDimension_2D;
                lvd.baseMipLevel = 0;
                lvd.mipLevelCount = 1;
                lvd.baseArrayLayer = static_cast<uint32_t>(c);
                lvd.arrayLayerCount = 1;
                wgpu::TextureView layerView = shadowDepthArray->texture.CreateView(
                    reinterpret_cast<const wgpu::TextureViewDescriptor*>(&lvd));
                WGPURenderPassDepthStencilAttachment ds{};
                ds.view = layerView.Get();
                ds.depthClearValue = 1.f;
                ds.depthLoadOp = WGPULoadOp_Clear;
                ds.depthStoreOp = WGPUStoreOp_Store;
                ds.stencilClearValue = 0;
                ds.stencilLoadOp = WGPULoadOp_Undefined;
                ds.stencilStoreOp = WGPUStoreOp_Undefined;
                WGPURenderPassDescriptor rp{};
                rp.depthStencilAttachment = &ds;
                wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
                flushShadowPass(pass);
                pass.End();
            }
        }
    }

    // 2. Scene color pass (3D) into the offscreen target.
    wgpu::TextureView sceneView;
    wgpu::Texture sceneTex;
    if (frameHad3DThisFrame) {
        if (active3DCanvas) {
            // 3D into an offscreen canvas: render the mesh pass into the
            // canvas color/depth (RGBA8Unorm, matches the mesh3d pipelines).
            OffscreenCanvas *oc = active3DCanvas;
            WGPURenderPassColorAttachment colorAtt{};
            colorAtt.view = oc->colorView.Get();
            colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAtt.loadOp = WGPULoadOp_Clear;
            colorAtt.storeOp = WGPUStoreOp_Store;
            colorAtt.clearValue = {clearColor.r, clearColor.g, clearColor.b, 1.f};
            WGPURenderPassDepthStencilAttachment ds{};
            ds.view = oc->depthView.Get();
            ds.depthClearValue = 1.f;
            ds.depthLoadOp = WGPULoadOp_Clear;
            ds.depthStoreOp = WGPUStoreOp_Store;
            ds.stencilClearValue = 0;
            ds.stencilLoadOp = WGPULoadOp_Undefined;
            ds.stencilStoreOp = WGPUStoreOp_Undefined;
            WGPURenderPassDescriptor rp{};
            rp.colorAttachmentCount = 1;
            rp.colorAttachments = &colorAtt;
            rp.depthStencilAttachment = &ds;
            wgpu::RenderPassEncoder pass =
                encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
            // Voxel draws are not expected on the canvas path (voxel module is
            // trimmed from the web build), and its pipeline follows the scene
            // sample count which would mismatch the 1x canvas attachment.
            flushMesh3D(pass, WGPUTextureFormat_RGBA8Unorm, /*canvasTarget*/ true);
            pass.End();
            oc->clearRequested = false;
            // The script draws the canvas texture explicitly (2D path); it is
            // not composited into the swapchain.
            lastReadbackTex = oc->color;
            lastReadbackW = oc->getWidth();
            lastReadbackH = oc->getHeight();
        } else if (!sceneColorSlots.empty()) {
            SceneColorSlot &slot = sceneColorSlots[currentFrameSlot()];
            WGPURenderPassColorAttachment colorAtt{};
            colorAtt.view = slot.sampleCount > 1 ? slot.msaaView.Get() : slot.colorView.Get();
            colorAtt.resolveTarget = slot.sampleCount > 1 ? slot.colorView.Get() : nullptr;
            colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAtt.loadOp = WGPULoadOp_Clear;
            // When resolving to a single-sample target, the multisampled
            // attachment must be discarded (WebGPU spec).
            colorAtt.storeOp =
                slot.sampleCount > 1 ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
            colorAtt.clearValue = {clearColor.r, clearColor.g, clearColor.b, 1.f};
            WGPURenderPassDepthStencilAttachment ds{};
            ds.view = slot.depthView.Get();
            ds.depthClearValue = 1.f;
            ds.depthLoadOp = WGPULoadOp_Clear;
            ds.depthStoreOp = WGPUStoreOp_Store;
            ds.stencilClearValue = 0;
            ds.stencilLoadOp = WGPULoadOp_Undefined;
            ds.stencilStoreOp = WGPUStoreOp_Undefined;
            WGPURenderPassDescriptor rp{};
            rp.colorAttachmentCount = 1;
            rp.colorAttachments = &colorAtt;
            rp.depthStencilAttachment = &ds;
            wgpu::RenderPassEncoder pass =
                encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
            flushVoxelDraws(pass, sceneColorFormat);
            flushMesh3D(pass, sceneColorFormat);
            pass.End();
            lastPresentSlot = currentFrameSlot();
            lastReadbackTex = slot.color;
            lastReadbackW = sceneColorWidth;
            lastReadbackH = sceneColorHeight;
            sceneView = slot.colorView;
            sceneTex = slot.color;
        }
    }

    // 3. GBuffer pass.
    if (gbufferPassPending && !gbufferSlots.empty()) {
        lastGbufferSlot = currentFrameSlot();
        GbufferSlot &slot = gbufferSlots[lastGbufferSlot];
        WGPURenderPassColorAttachment colorAtts[3]{};
        for (int i = 0; i < 3; ++i) {
            colorAtts[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAtts[i].loadOp = WGPULoadOp_Clear;
            colorAtts[i].storeOp = WGPUStoreOp_Store;
            colorAtts[i].clearValue = {0.f, 0.f, 0.f, 1.f};
        }
        colorAtts[0].view = slot.normalView.Get();
        colorAtts[1].view = slot.depthColorView.Get();
        colorAtts[2].view = slot.albedoView.Get();
        WGPURenderPassDepthStencilAttachment ds{};
        ds.view = slot.depthView.Get();
        ds.depthClearValue = 1.f;
        ds.depthLoadOp = WGPULoadOp_Clear;
        ds.depthStoreOp = WGPUStoreOp_Store;
        ds.stencilClearValue = 0;
        ds.stencilLoadOp = WGPULoadOp_Undefined;
        ds.stencilStoreOp = WGPUStoreOp_Undefined;
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = 3;
        rp.colorAttachments = colorAtts;
        rp.depthStencilAttachment = &ds;
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
        flushGbufferPass(pass);
        pass.End();

        // 3b. SSAO pass: derive a screen-space occlusion texture from the
        // G-buffer linear depth; the forward mesh pass samples the previous
        // slot (one frame of latency).
        if (aoActive) {
            ensureAOResources(sceneColorWidth, sceneColorHeight);
            if (aoPipeline && aoTex[0]) {
                pushValidationScope();
                GbufferSlot &gslot = gbufferSlots[currentFrameSlot()];
                struct AOUbo {
                    glm::vec4 params;  // radius, power, nearZ, farZ
                    float intensity;
                    float invScale;    // AO target size / depth size
                    float pad;
                } aou;
                // Near/far are passed so the shader can linearize the
                // hardware depth into world units for the occlusion delta.
                aou.params = glm::vec4(0.05f, 1.1f, mesh3dNear, mesh3dFar);
                aou.intensity = 1.0f;
                aou.invScale = 0.5f;
                aou.pad = 0.f;
                queue.WriteBuffer(aoUbo, 0, &aou, sizeof(aou));
                wgpu::BindGroup aoBg = makeAOBindGroup(gslot.depthView);

                WGPURenderPassColorAttachment colorAtt{};
                colorAtt.view = aoView[aoWriteIndex].Get();
                colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAtt.loadOp = WGPULoadOp_Clear;
                colorAtt.storeOp = WGPUStoreOp_Store;
                colorAtt.clearValue = {1.f, 1.f, 1.f, 1.f};
                WGPURenderPassDescriptor rp{};
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &colorAtt;
                wgpu::RenderPassEncoder apass =
                    encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
                apass.SetPipeline(aoPipeline);
                apass.SetBindGroup(0, aoBg, 0, nullptr);
                apass.SetVertexBuffer(0, fullscreenQuadVb, 0, 4 * 32);
                apass.SetIndexBuffer(fullscreenQuadIb, wgpu::IndexFormat::Uint32, 0, 24);
                apass.DrawIndexed(6, 1, 0, 0, 0);
                apass.End();
                popValidationScope();
                aoWriteIndex ^= 1;
            }
        }
    }

    // 3c. Screen-space decals consume the freshly written GBuffer depth and
    // world normal, then write three transparent layer attachments.
    if (decalPassPending && !decalSlots.empty() && !gbufferSlots.empty()) {
        lastDecalSlot = currentFrameSlot();
        DecalSlot &slot = decalSlots[lastDecalSlot];
        WGPURenderPassColorAttachment colorAtts[3]{};
        for (auto &attachment : colorAtts) {
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            attachment.clearValue = {0.f, 0.f, 0.f, 0.f};
        }
        colorAtts[0].view = slot.albedoView.Get();
        colorAtts[1].view = slot.normalView.Get();
        colorAtts[2].view = slot.paramsView.Get();
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = 3;
        rp.colorAttachments = colorAtts;
        wgpu::RenderPassEncoder pass =
            encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor *>(&rp));
        flushDecalPass(pass);
        pass.End();
    }

    // 4. Active canvas: flush 2D batches into the offscreen target instead.
    if (activeCanvas) {
        auto *oc = static_cast<OffscreenCanvas *>(activeCanvas);
        flush2DToCanvas(oc);
    }

    // 5. Swapchain pass: composite scene color, draw 2D, run the overlay.
    {
        WGPURenderPassColorAttachment colorAtt{};
        colorAtt.view = surfaceView.Get();
        colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAtt.loadOp = WGPULoadOp_Clear;
        colorAtt.storeOp = WGPUStoreOp_Store;
        colorAtt.clearValue = {clearColor.r, clearColor.g, clearColor.b, 1.f};
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &colorAtt;
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));

        if (sceneView && !sceneColorComposited) {
            if (!fullscreenQuadReady) {
                // Textured vertex: pos(2) + color(4) + uv(2) = 32 bytes.
                float verts[32] = {
                    -1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f,
                     1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f,
                     1.f,  1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
                    -1.f,  1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 1.f,
                };
                uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
                WGPUBufferDescriptor vbd{};
                vbd.label = sv("eve_fullscreen_vb");
                vbd.size = sizeof(verts);
                vbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
                fullscreenQuadVb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&vbd));
                queue.WriteBuffer(fullscreenQuadVb, 0, verts, sizeof(verts));
                WGPUBufferDescriptor ibd{};
                ibd.label = sv("eve_fullscreen_ib");
                ibd.size = sizeof(indices);
                ibd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
                fullscreenQuadIb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
                queue.WriteBuffer(fullscreenQuadIb, 0, indices, sizeof(indices));
                fullscreenQuadReady = true;
            }

            GpuTexture sceneGpu;
            sceneGpu.texture = sceneTex;
            sceneGpu.view = sceneView;
            sceneGpu.sampler = createLinearSampler(device);
            wgpu::BindGroup bg = makeTex2DBindGroup(&sceneGpu, nullptr);
            uint32_t offsets[1] = {0};
            pass.SetPipeline(texturedPipeline);
            pass.SetBindGroup(0, bg, 1, offsets);
            pass.SetVertexBuffer(0, fullscreenQuadVb, 0, 4 * 32);
            pass.SetIndexBuffer(fullscreenQuadIb, wgpu::IndexFormat::Uint32, 0, 24);
            pass.DrawIndexed(6, 1, 0, 0, 0);
        }

        if (!activeCanvas) {
            flush2D(pass, pixelW > 0 ? pixelW : logicalW, pixelH > 0 ? pixelH : logicalH,
                    surfaceFormat);
        }

        if (presentOverlayFn_) {
            WGPURenderPassEncoder cPass = pass.Get();
            presentOverlayFn_(presentOverlayUser_, &cPass);
        }
        pass.End();
    }

    wgpu::CommandBuffer cmd = encoder.Finish();
    queue.Submit(1, &cmd);
    // emdawnwebgpu implements the GPU on the JS main thread: queued commands
    // (draws, writes) only execute when ProcessEvents drives the work queue.
    // Without this, render-pass draws are silently never executed (only the
    // swapchain clear, handled by the browser present, is visible).
#ifdef __EMSCRIPTEN__
    if (instance) instance.ProcessEvents();
#endif
    // emdawnwebgpu does not implement wgpuSurfacePresent (it aborts); the
    // browser presents the canvas automatically once the command buffer is
    // submitted from within a requestAnimationFrame callback. On Emscripten
    // the engine frame is driven by emscripten_set_main_loop (rAF), so the
    // Squirrel main loop no longer blocks and no sleep is needed here.
#if !defined(__EMSCRIPTEN__)
    surface.Present();
#endif

    frameIndex++;
    frame3DStarted = false;
    frameHad3DThisFrame = false;
    frameHad3D = false;
    sceneColorPassOpen = false;
    active3DCanvas = nullptr;
    gbufferPassPending = false;
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

Canvas *Graphics::newCanvas(int width, int height) {
    auto *c = new OffscreenCanvas(this, width, height);
    ownedCanvases.push_back(std::unique_ptr<eve::graphics::Canvas>(c));
    return c;
}

void Graphics::setCanvas(Canvas *canvas) {
    if (activeCanvas && canvas != activeCanvas) {
        flush2DToCanvas(static_cast<OffscreenCanvas *>(activeCanvas));
    }
    activeCanvas = canvas;
}

bool Graphics::isCanvasActive() const { return activeCanvas != nullptr; }
Canvas *Graphics::getCanvas() const { return activeCanvas; }

void Graphics::flush2DToCanvas(OffscreenCanvas *canvas) {
    if (!canvas || !canvas->getTexture()) return;
    auto &uboArena = currentUboArena();
    uboArena.reset();
    auto &vtxArena = currentVertexArena();
    vtxArena.reset();

    wgpu::CommandEncoder enc = device.CreateCommandEncoder();
    WGPURenderPassColorAttachment ca{};
    ca.view = canvas->colorView.Get();
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = canvas->clearRequested ? WGPULoadOp_Clear : WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {canvas->clearColor.r, canvas->clearColor.g, canvas->clearColor.b,
                     canvas->clearColor.a};
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    wgpu::RenderPassEncoder pass = enc.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
    flush2D(pass, canvas->getWidth(), canvas->getHeight(), WGPUTextureFormat_RGBA8Unorm);
    pass.End();
    wgpu::CommandBuffer cmd = enc.Finish();
    queue.Submit(1, &cmd);
    canvas->clearRequested = false;
}

Texture *Graphics::getTexture() {
    if (activeCanvas) return activeCanvas->getTexture();
    return sceneColorTexture;
}

void Graphics::draw(eve::graphics::Graphics *gfx, const glm::mat4 &matrix) const {
    (void)gfx;
    (void)matrix;
}

void Graphics::draw(Canvas *C, const glm::mat4 &matrix) const {
    (void)C;
    (void)matrix;
}

void Graphics::clear(std::optional<Color> color, std::optional<int> /*stencil*/,
                     std::optional<double> /*depth*/) {
    if (frameHad3D && activeCanvas == nullptr) return;
    clearColor = color.value_or(backgroundColor);
    hasPendingClear = true;
    clear2DBatches();
    if (auto *canvas = dynamic_cast<OffscreenCanvas *>(activeCanvas)) {
        canvas->clear(clearColor, std::nullopt, std::nullopt);
    }
}

Color Graphics::getPixel(int x, int y) {
    if (activeCanvas) return getPixelImpl(static_cast<OffscreenCanvas *>(activeCanvas), x, y);
    if (!sceneColorSlots.empty()) {
        return getPixelImpl(nullptr, x, y);
    }
    return clearColor;
}

image::ImageData *Graphics::newImageData() {
    if (activeCanvas) return newImageDataImpl(static_cast<OffscreenCanvas *>(activeCanvas));
    if (!sceneColorSlots.empty()) return newImageDataImpl(nullptr);
    return new image::ImageData(1, 1, "RGBA8");
}

// ---------------------------------------------------------------------------
// Readback
// ---------------------------------------------------------------------------

namespace {

bool copyTextureToCpu(wgpu::Instance &instance, wgpu::Device &device, wgpu::Queue &queue,
                      wgpu::Texture src, int width, int height, std::vector<uint8_t> &outRgba) {
    if (!src) return false;
    uint64_t bytesPerRow = static_cast<uint64_t>(width * 4);
    bytesPerRow = (bytesPerRow + 255) / 256 * 256;  // copy alignment 256
    uint64_t size = bytesPerRow * height;

    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_readback");
    bd.size = size;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.mappedAtCreation = false;
    wgpu::Buffer dst = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));

    wgpu::CommandEncoder enc = device.CreateCommandEncoder();
    WGPUTexelCopyTextureInfo from{};
    from.texture = src.Get();
    from.mipLevel = 0;
    from.aspect = WGPUTextureAspect_All;
    from.origin = {0, 0, 0};
    WGPUTexelCopyBufferInfo to{};
    to.buffer = dst.Get();
    to.layout.offset = 0;
    to.layout.bytesPerRow = static_cast<uint32_t>(bytesPerRow);
    to.layout.rowsPerImage = static_cast<uint32_t>(height);
    WGPUExtent3D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    enc.CopyTextureToBuffer(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&from),
                            reinterpret_cast<const wgpu::TexelCopyBufferInfo*>(&to),
                            reinterpret_cast<const wgpu::Extent3D*>(&extent));
    wgpu::CommandBuffer cmd = enc.Finish();
    queue.Submit(1, &cmd);

    bool mapped = false;
    WGPUBufferMapCallbackInfo cbInfo{};
    cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    cbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView /*message*/, void *userdata1,
                         void * /*userdata2*/) {
        bool *ok = static_cast<bool *>(userdata1);
        *ok = (status == WGPUMapAsyncStatus_Success);
    };
    cbInfo.userdata1 = &mapped;
    wgpuBufferMapAsync(dst.Get(), WGPUMapMode_Read, 0, size, cbInfo);

    int guard = 0;
    while (!mapped && guard < 2000) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(instance.Get());
        ++guard;
    }
    if (!mapped) return false;

    const uint8_t *data = static_cast<const uint8_t *>(dst.GetConstMappedRange(0, size));
    if (!data) return false;
    outRgba.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        std::memcpy(outRgba.data() + size_t(y) * width * 4, data + size_t(y) * bytesPerRow,
                    size_t(width) * 4);
    dst.Unmap();
    return true;
}

}  // namespace

Color Graphics::getPixelImpl(OffscreenCanvas *canvas, int x, int y) {
    int w = canvas ? canvas->getWidth() : (sceneColorWidth > 0 ? sceneColorWidth : 1);
    int h = canvas ? canvas->getHeight() : (sceneColorHeight > 0 ? sceneColorHeight : 1);
    if (x < 0 || y < 0 || x >= w || y >= h) return Color(0.f, 0.f, 0.f, 0.f);
    std::vector<uint8_t> rgba;
    wgpu::Texture src = canvas ? canvas->color
                               : (sceneColorSlots.empty() ? nullptr
                                                          : sceneColorSlots[lastPresentSlot].color);
    if (!src || !copyTextureToCpu(instance, device, queue, src, w, h, rgba)) return clearColor;
    const uint8_t *p = rgba.data() + (size_t(y) * w + x) * 4;
    return Color(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f);
}

image::ImageData *Graphics::newImageDataImpl(OffscreenCanvas *canvas) {
    int w = canvas ? canvas->getWidth() : (sceneColorWidth > 0 ? sceneColorWidth : 1);
    int h = canvas ? canvas->getHeight() : (sceneColorHeight > 0 ? sceneColorHeight : 1);
    auto *img = new image::ImageData(w, h, "RGBA8");
    std::vector<uint8_t> rgba;
    wgpu::Texture src = canvas ? canvas->color
                               : (sceneColorSlots.empty() ? nullptr
                                                          : sceneColorSlots[lastPresentSlot].color);
    if (src && copyTextureToCpu(instance, device, queue, src, w, h, rgba)) {
        std::memcpy(img->getData(), rgba.data(), rgba.size());
    }
    return img;
}

image::ImageData *Graphics::readGBufferToImageData(const std::string &attachment) {
    submitPendingDeferredPasses();
    if (!device || gbufferSlots.empty() || gbufferWidth <= 0 || gbufferHeight <= 0) return nullptr;
    GbufferSlot &slot = gbufferSlots[std::min<size_t>(lastGbufferSlot, gbufferSlots.size() - 1)];
    wgpu::Texture src;
    if (attachment == "depth")
        src = slot.depthColor;
    else if (attachment == "normal")
        src = slot.normal;
    else if (attachment == "albedo")
        src = slot.albedo;
    else
        return nullptr;

    std::vector<uint8_t> rgba;
    if (!copyTextureToCpu(instance, device, queue, src, gbufferWidth, gbufferHeight, rgba))
        return nullptr;
    auto *image = new image::ImageData(gbufferWidth, gbufferHeight, "RGBA8");
    std::memcpy(image->getData(), rgba.data(), rgba.size());
    return image;
}

image::ImageData *Graphics::readDecalLayerToImageData(const std::string &attachment) {
    submitPendingDeferredPasses();
    if (!device || decalSlots.empty() || decalWidth <= 0 || decalHeight <= 0) return nullptr;
    DecalSlot &slot = decalSlots[std::min<size_t>(lastDecalSlot, decalSlots.size() - 1)];
    wgpu::Texture src;
    if (attachment == "albedo")
        src = slot.albedo;
    else if (attachment == "normal")
        src = slot.normal;
    else if (attachment == "params")
        src = slot.params;
    else
        return nullptr;
    std::vector<uint8_t> rgba;
    if (!copyTextureToCpu(instance, device, queue, src, decalWidth, decalHeight, rgba))
        return nullptr;
    auto *image = new image::ImageData(decalWidth, decalHeight, "RGBA8");
    std::memcpy(image->getData(), rgba.data(), rgba.size());
    return image;
}

// ---------------------------------------------------------------------------
// Async frame readback (browser)
// ---------------------------------------------------------------------------

namespace {

bool encodeRgbaToPng(const std::string &path, int width, int height,
                     const std::vector<uint8_t> &rgba) {
    try {
        image::ImageData img(width, height, "RGBA8");
        std::memcpy(img.getData(), rgba.data(), rgba.size());
        std::unique_ptr<eve::filesystem::FileData> png(
            img.encode(medialoader::FormatHandler::ENCODED_PNG, path.c_str(), false));
        if (!png) return false;
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out.write(static_cast<const char *>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
        return out.good();
    } catch (...) {
        return false;
    }
}

}  // namespace

bool Graphics::beginFrameReadback(const std::string &path) {
    // A finished readback can be replaced; only refuse while one is in flight.
    if ((pendingReadback_ && !pendingReadback_->done) || !device) return false;
    wgpu::Texture src = lastReadbackTex;
    int w = lastReadbackW;
    int h = lastReadbackH;
    if (!src || w <= 0 || h <= 0) {
        // Fall back to the scene color slot before anything was rendered.
        if (sceneColorSlots.empty()) return false;
        const uint32_t slot = lastPresentSlot;
        if (slot >= sceneColorSlots.size()) return false;
        src = sceneColorSlots[slot].color;
        w = sceneColorWidth;
        h = sceneColorHeight;
        if (!src || w <= 0 || h <= 0) return false;
    }

    uint64_t bytesPerRow = static_cast<uint64_t>(w * 4);
    bytesPerRow = (bytesPerRow + 255) / 256 * 256;
    const uint64_t size = bytesPerRow * static_cast<uint64_t>(h);

    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_readback");
    bd.size = size;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.mappedAtCreation = false;
    wgpu::Buffer dst = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    if (!dst) return false;

    // Copy the resolved scene color into the readback buffer, then map
    // asynchronously (two-phase: the pump waits for the map callback on the
    // browser event loop without ASYNCIFY sleeps).
    wgpu::CommandEncoder enc = device.CreateCommandEncoder();
    WGPUTexelCopyTextureInfo from{};
    from.texture = src.Get();
    from.mipLevel = 0;
    from.aspect = WGPUTextureAspect_All;
    from.origin = {0, 0, 0};
    WGPUTexelCopyBufferInfo to{};
    to.buffer = dst.Get();
    to.layout.offset = 0;
    to.layout.bytesPerRow = static_cast<uint32_t>(bytesPerRow);
    to.layout.rowsPerImage = static_cast<uint32_t>(h);
    WGPUExtent3D extent{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    enc.CopyTextureToBuffer(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&from),
                            reinterpret_cast<const wgpu::TexelCopyBufferInfo*>(&to),
                            reinterpret_cast<const wgpu::Extent3D*>(&extent));
    wgpu::CommandBuffer cmd = enc.Finish();
    queue.Submit(1, &cmd);

    auto pr = std::make_unique<PendingReadback>();
    pr->path = path;
    pr->width = w;
    pr->height = h;
    pr->bytesPerRow = bytesPerRow;
    pr->dst = dst;

    WGPUBufferMapCallbackInfo cbInfo{};
    cbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    cbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView /*message*/, void *userdata1,
                         void * /*userdata2*/) {
        auto *p = static_cast<PendingReadback *>(userdata1);
        p->mapped = (status == WGPUMapAsyncStatus_Success);
    };
    cbInfo.userdata1 = pr.get();
    wgpuBufferMapAsync(dst.Get(), WGPUMapMode_Read, 0, size, cbInfo);

    pendingReadback_ = std::move(pr);
    return true;
}

int Graphics::frameReadbackStatus() const {
    if (!pendingReadback_) return 0;
    if (!pendingReadback_->done) return 1;
    return pendingReadback_->ok ? 2 : 3;
}

void Graphics::pumpReadback() {
    if (!pendingReadback_ || pendingReadback_->done) return;
    auto &pr = *pendingReadback_;
    if (!pr.mapped) {
        // The map callback is delivered on the browser event loop between
        // frames (emdawnwebgpu callUserCallback), so no ASYNCIFY sleep is
        // needed here — this runs from present() on the main loop.
#if defined(__EMSCRIPTEN__)
        wgpuInstanceProcessEvents(instance.Get());
#endif
        return;
    }
    const uint8_t *data = static_cast<const uint8_t *>(
        pr.dst.GetConstMappedRange(0, pr.bytesPerRow * static_cast<uint64_t>(pr.height)));
    std::vector<uint8_t> rgba(static_cast<size_t>(pr.width) * pr.height * 4);
    if (data) {
        for (int y = 0; y < pr.height; ++y)
            std::memcpy(rgba.data() + size_t(y) * pr.width * 4,
                        data + size_t(y) * pr.bytesPerRow, size_t(pr.width) * 4);
    }
    pr.dst.Unmap();
    pr.ok = data && encodeRgbaToPng(pr.path, pr.width, pr.height, rgba);
    pr.done = true;
}

// ---------------------------------------------------------------------------
// Shader creation
// ---------------------------------------------------------------------------

namespace {

wgpu::RenderPipeline buildPipelineFromWgsl(wgpu::Device &dev, wgpu::PipelineLayout layout,
                                           WGPUTextureFormat format, const std::string &vert,
                                           const std::string &frag, bool depth, bool blend,
                                           bool mesh3d, bool hair, bool shadow, bool gbuffer,
                                           uint32_t sampleCount) {
    WGPURenderPipelineDescriptor pd{};
    pd.label = sv("eve_custom_shader");
    pd.layout = layout.Get();

    WGPUVertexAttribute attrs[3]{};
    WGPUVertexBufferLayout vb{};
    if (mesh3d || shadow || gbuffer) {
        attrs[0].format = WGPUVertexFormat_Float32x3;
        attrs[0].offset = 0;
        attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x3;
        attrs[1].offset = 12;
        attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Float32x2;
        attrs[2].offset = 24;
        attrs[2].shaderLocation = 2;
        vb.arrayStride = 32;
        vb.stepMode = WGPUVertexStepMode_Vertex;
        vb.attributeCount = 3;
        vb.attributes = attrs;
    } else {
        attrs[0].format = WGPUVertexFormat_Float32x2;
        attrs[0].offset = 0;
        attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x4;
        attrs[1].offset = 8;
        attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Float32x2;
        attrs[2].offset = 24;
        attrs[2].shaderLocation = 2;
        vb.arrayStride = 32;
        vb.stepMode = WGPUVertexStepMode_Vertex;
        vb.attributeCount = 3;
        vb.attributes = attrs;
    }
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;

    // Keep both modules alive until CreateRenderPipeline has consumed the
    // descriptor. Storing only their raw handles in pd and destroying the
    // wrappers at the end of these branches leaves dangling handles for Dawn.
    wgpu::ShaderModule vertexModule;
    wgpu::ShaderModule fragmentModule;
    if (!vert.empty()) {
        WGPUShaderModuleDescriptor md = mdDesc(vert);
        vertexModule = dev.CreateShaderModule(
            reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        pd.vertex.module = vertexModule.Get();
        pd.vertex.entryPoint = sv("vs_main");
    } else {
        // Default textured vertex shader.
        WGPUShaderModuleDescriptor md = mdDesc(kTexturedVertWgsl);
        vertexModule = dev.CreateShaderModule(
            reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        pd.vertex.module = vertexModule.Get();
        pd.vertex.entryPoint = sv("vs_main");
    }

    if (!frag.empty()) {
        WGPUFragmentState fs{};
        WGPUShaderModuleDescriptor md = mdDesc(frag);
        fragmentModule = dev.CreateShaderModule(
            reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        fs.module = fragmentModule.Get();
        fs.entryPoint = sv("fs_main");
        fs.targetCount = 1;
        WGPUColorTargetState target{};
        target.format = format;
        target.writeMask = WGPUColorWriteMask_All;
        if (blend) {
            static WGPUBlendState bs = alphaBlend();
            target.blend = &bs;
        }
        fs.targets = &target;
        pd.fragment = &fs;
    } else {
        pd.fragment = nullptr;
    }

    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = hair ? WGPUCullMode_None : WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;

    if (depth && !shadow) {
        static WGPUDepthStencilState ds{};
        ds.format = WGPUTextureFormat_Depth32Float;
        ds.depthWriteEnabled = WGPUOptionalBool_True;
        ds.depthCompare = WGPUCompareFunction_Less;
        pd.depthStencil = &ds;
    }
    pd.multisample.count = sampleCount ? sampleCount : 1;
    // mask=0 (zero-init) silently discards all fragments; use all-samples.
    pd.multisample.mask = 0xFFFFFFFFu;
    return dev.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

}  // namespace

Shader *Graphics::newShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                   const std::vector<uint32_t> &fragSpv) {
    if (!device) throw Exception("newShaderFromSpv: device not initialized");
    // Native Dawn can consume Vulkan SPIR-V directly; the browser build cannot
    // (browsers only accept WGSL). The WebGPU backend therefore requires WGSL
    // for custom shaders; SPIR-V input is rejected with a clear message.
    throw Exception("newShaderFromSpv: SPIR-V custom shaders are not supported on the "
                    "WebGPU backend (browsers accept WGSL only). Recompile the shader "
                    "with the WebGPU toolchain (glslc+tint) and use newShaderFromWgsl.");
}

Shader *Graphics::newShaderFromSpvFile(const std::string &vertPath, const std::string &fragPath) {
    throw Exception("newShaderFromSpvFile: SPIR-V custom shaders are not supported on the "
                    "WebGPU backend. Use WGSL shaders instead.");
}

bool Graphics::releaseShader(Shader *shader) {
    if (!shader || !shader->gpuHandle) return false;

    auto *gpu = static_cast<GpuShader *>(shader->gpuHandle);
    auto gpuIt = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const std::unique_ptr<GpuShader> &g) {
                                  return g.get() == gpu;
                              });
    if (gpuIt == ownedGpuShaders.end()) return false;

    auto shIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                             [&](const std::unique_ptr<Shader> &s) {
                                 return s.get() == shader;
                             });
    if (shIt == ownedShaders.end()) return false;

    shader->gpuHandle = nullptr;
    ownedGpuShaders.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)shIt->release();
    ownedShaders.erase(shIt);
    return true;
}

Shader *Graphics::newShader(const std::string &vertGlsl, const std::string &fragGlsl) {
    (void)vertGlsl;
    (void)fragGlsl;
    throw Exception("newShader: runtime GLSL compilation is not available on the WebGPU "
                    "backend (browser WGSL only). Ship pre-compiled WGSL shaders.");
}

Shader *Graphics::newMeshShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                       const std::vector<uint32_t> &fragSpv) {
    (void)vertSpv;
    (void)fragSpv;
    throw Exception("newMeshShaderFromSpv: SPIR-V custom mesh shaders are not supported on the "
                    "WebGPU backend. Use WGSL shaders instead.");
}

Shader *Graphics::newMeshShaderFromWgsl(const std::string &vertWgsl, const std::string &fragWgsl) {
    if (!device) throw Exception("newMeshShaderFromWgsl: device not initialized");
    if (fragWgsl.empty()) throw Exception("newMeshShaderFromWgsl: empty fragment WGSL");
    if (!mesh3dSetLayout) throw Exception("newMeshShaderFromWgsl: mesh3d layout missing");

    std::string vert = vertWgsl.empty() ? kMesh3DVertWgsl : vertWgsl;

    auto gpu = std::make_unique<GpuShader>();
    gpu->isMesh3D = true;
    gpu->wgslVert = vert;
    gpu->wgslFrag = fragWgsl;
    gpu->mesh3dPipeline =
        buildPipelineFromWgsl(device, mesh3dPipelineLayout, sceneColorFormat, vert, fragWgsl,
                              /*depth*/ true, /*blend*/ false, /*mesh3d*/ true, /*hair*/ false,
                              /*shadow*/ false, /*gbuffer*/ false,
                              /*sampleCount*/ sceneColorSamples);
    // X-ray variant: depth test/write off + alpha blend so occluded silhouettes
    // paint over the building (the shader discards visible fragments itself).
    gpu->mesh3dXrayPipeline =
        buildPipelineFromWgsl(device, mesh3dPipelineLayout, sceneColorFormat, vert, fragWgsl,
                              /*depth*/ false, /*blend*/ true, /*mesh3d*/ true, /*hair*/ false,
                              /*shadow*/ false, /*gbuffer*/ false,
                              /*sampleCount*/ sceneColorSamples);

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eMesh3D);
    sh->gpuHandle = gpu.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

Shader *Graphics::newMeshShader(const std::string &vertGlsl, const std::string &fragGlsl) {
    (void)vertGlsl;
    (void)fragGlsl;
    throw Exception("newMeshShader: runtime GLSL compilation is not available on the WebGPU "
                    "backend. Ship pre-compiled WGSL shaders.");
}

Shader *Graphics::newShaderFromWgsl(const std::string &vertWgsl,
                                    const std::string &fragWgsl) {
    if (!device) throw Exception("newShaderFromWgsl: device not initialized");
    if (fragWgsl.empty()) throw Exception("newShaderFromWgsl: empty fragment WGSL");
    if (!tex2DPipelineLayout) throw Exception("newShaderFromWgsl: 2D pipeline layout missing");

    std::string vert = vertWgsl.empty() ? kTexturedVertWgsl : vertWgsl;

    auto gpu = std::make_unique<GpuShader>();
    gpu->isMesh3D = false;
    gpu->wgslVert = vert;
    gpu->wgslFrag = fragWgsl;
    // 2D custom shader: alpha blend, no depth; one pipeline per target format
    // (swapchain surface vs RGBA8Unorm offscreen canvas).
    gpu->swapchainPipeline = createPipelineForShader(gpu.get(), wgpu::TextureFormat(surfaceFormat),
                                                     /*depth*/ false, /*mesh3d*/ false,
                                                     /*hair*/ false, /*shadow*/ false,
                                                     /*gbuffer*/ false, tex2DPipelineLayout);
    gpu->offscreenPipeline =
        createPipelineForShader(gpu.get(), wgpu::TextureFormat::RGBA8Unorm,
                                /*depth*/ false, /*mesh3d*/ false,
                                /*hair*/ false, /*shadow*/ false,
                                /*gbuffer*/ false, tex2DPipelineLayout);

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eSprite2D);
    sh->gpuHandle = gpu.get();

    Shader *raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

wgpu::RenderPipeline Graphics::createPipelineForShader(GpuShader *gs, wgpu::TextureFormat format,
                                                       bool depth, bool mesh3d, bool hair,
                                                       bool shadow, bool gbuffer,
                                                       wgpu::PipelineLayout layout) {
    if (!gs || gs->wgslFrag.empty()) return {};
    // Custom 2D/mesh shaders blend only when the caller asks (x-ray / 2D UI);
    // opaque meshes and shadow/gbuffer variants keep write-through targets.
    const bool blend = !depth;
    return buildPipelineFromWgsl(device, layout, WGPUTextureFormat(format), gs->wgslVert,
                                 gs->wgslFrag, depth, blend, mesh3d, hair, shadow, gbuffer,
                                 sceneColorSamples);
}

Shader *Graphics::newHairShaderFromSpv(const std::vector<uint32_t> &vertSpv,
                                       const std::vector<uint32_t> &fragSpv) {
    (void)vertSpv;
    (void)fragSpv;
    throw Exception("newHairShaderFromSpv: SPIR-V custom hair shaders are not supported on the "
                    "WebGPU backend. Use WGSL shaders instead.");
}

}  // namespace eve::graphics::webgpu

namespace eve::graphics {
// Font support lives in the font module, which is not part of the WebGPU/WASM
// build. Fail loudly instead of leaving the interface unsatisfied at link time.
Font *Graphics::newFont(font::FontData *, std::string) {
    throw Exception(
        "newFont: fonts are not supported on the WebGPU backend (font module is not "
        "part of the WASM build)");
}

void Graphics::print(const std::string &, float, float, const Color &, float) {
    throw Exception(
        "print: fonts are not supported on the WebGPU backend (font module is not part "
        "of the WASM build)");
}
}  // namespace eve::graphics
