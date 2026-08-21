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
    wgpuSurfaceCapabilitiesFreeMembers(&caps);
#endif

    swapchainConfigured = false;
    // Bind group layouts / pipelines must exist before the default textures
    // (texture bind groups reference the 2D / mesh3d layouts), and the shadow
    // depth array must exist before mesh bind groups are built at flush time.
    createPipelineResources();
    createShadowResources();
    createDefaultTextures();
    initialized = true;
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

    // 1x1 white cubemap.
    uint8_t cubeFace[4] = {255, 255, 255, 255};
    uint8_t cubeData[24];
    for (int f = 0; f < 6; ++f) std::memcpy(cubeData + f * 4, cubeFace, 4);
    defaultEnvCubemap =
        static_cast<GpuTexture *>(newCubemap(1, cubeData)->gpuHandle);

    // Shared filtering sampler for WGSL bindings declared as plain `sampler`.
    TextureSampler def;
    mainSampler = makeSampler(def, 1);
}

// ---------------------------------------------------------------------------
// Pipeline resource creation
// ---------------------------------------------------------------------------

void Graphics::createPipelineResources() {
    create2DPipelines();
    createMesh3DPipelines();
    createShadowPipelines();
    createGbufferPipelines();
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
    WGPUBindGroupLayoutEntry entries[10]{};
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

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_mesh3d");
    desc.entryCount = 10;
    desc.entries = entries;
    return device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&desc));
}

wgpu::BindGroupLayout Graphics::makeShadowBindGroupLayout() {
    WGPUBindGroupLayoutEntry entries[1]{};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = 64;  // mat4 mvp

    WGPUBindGroupLayoutDescriptor desc{};
    desc.label = sv("eve_shadow");
    desc.entryCount = 1;
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
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pd.depthStencil = &ds;
    pd.multisample.count = 1;
    // Zero-init would leave mask=0, which discards every fragment
    // (sampleMask=0). The WebGPU default is 0xFFFFFFFF (all samples).
    pd.multisample.mask = 0xFFFFFFFFu;
    mesh3dPipeline = device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
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
}

void Graphics::createVoxelPipelines() {
    voxelSetLayout = makeVoxelBindGroupLayout();
    voxelPipelineLayout = makeVoxelPipelineLayout();

    WGPUVertexAttribute attrs[2] = {};
    attrs[0].format = WGPUVertexFormat_Float32x2;  // corner
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Uint32;  // packed
    attrs[1].offset = 8;
    attrs[1].shaderLocation = 1;
    WGPUVertexBufferLayout cornerVb{};
    fillVertexLayout(cornerVb, 12, attrs, 2);
    WGPUVertexBufferLayout instanceVb{};
    instanceVb.arrayStride = 4;
    instanceVb.stepMode = WGPUVertexStepMode_Instance;
    instanceVb.attributeCount = 0;
    instanceVb.attributes = nullptr;
    WGPUVertexBufferLayout vbs[2] = {cornerVb, instanceVb};

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
    pd.vertex.bufferCount = 2;
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
    pd.multisample.count = 1;
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
    auto *gpu = gpuForTexture(tex);
    if (gpu && data->getWidth() == tex->width && data->getHeight() == tex->height) {
        uploadTexturePixelsMips(gpu, static_cast<const uint8_t *>(data->getData()), tex->width,
                                tex->height);
    }
    return true;
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

    WGPUBindGroupEntry entries[10]{};
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
    entries[5].textureView = (shadowDepthArray ? shadowDepthArray : defaultShadowTex)->view.Get();
    entries[6].binding = 6;
    entries[6].textureView = h->view.Get();
    entries[7].binding = 7;
    entries[7].sampler = mainSampler.Get();
    entries[8].binding = 8;
    entries[8].sampler = (shadowDepthArray ? shadowDepthArray : defaultShadowTex)->sampler.Get();
    entries[9].binding = 9;
    entries[9].textureView = d->view.Get();

    (void)frameUboOffset;
    (void)shadowUboOffset;
    (void)pushUboOffset;
    WGPUBindGroupDescriptor desc{};
    desc.label = sv("eve_mesh_group");
    desc.layout = mesh3dSetLayout.Get();
    desc.entryCount = 10;
    desc.entries = entries;
    return device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&desc));
}

// ---------------------------------------------------------------------------
// Mesh creation
// ---------------------------------------------------------------------------

Mesh *Graphics::newMeshFromArrays(const float *posXYZ, const float *nrmXYZ, const float *uvST,
                                  int vertexCount, const uint32_t *indices, int indexCount) {
    if (vertexCount <= 0 || !posXYZ) throw Exception("newMeshFromArrays: invalid vertex data");

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

    if (indexCount > 0) {
        // 16-bit index format halves index memory for meshes with <= 65535 verts.
        if (vertexCount <= 65535) {
            std::vector<uint16_t> idx16;
            idx16.reserve(indexCount);
            for (int i = 0; i < indexCount; ++i) idx16.push_back(uint16_t(indices[i]));
            WGPUBufferDescriptor ibd{};
            ibd.label = sv("eve_mesh_ib");
            ibd.size = uint64_t(indexCount) * sizeof(uint16_t);
            ibd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
            ibd.mappedAtCreation = false;
            gpu->indexBuffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
            queue.WriteBuffer(gpu->indexBuffer, 0, idx16.data(), ibd.size);
            gpu->indexFormat = wgpu::IndexFormat::Uint16;
        } else {
            WGPUBufferDescriptor ibd{};
            ibd.label = sv("eve_mesh_ib");
            ibd.size = uint64_t(indexCount) * sizeof(uint32_t);
            ibd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
            ibd.mappedAtCreation = false;
            gpu->indexBuffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
            queue.WriteBuffer(gpu->indexBuffer, 0, indices, ibd.size);
            gpu->indexFormat = wgpu::IndexFormat::Uint32;
        }
    }

    auto *mesh       = new Mesh();
    mesh->indexCount = indexCount;
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
    // WebGPU backend keeps mesh buffers immutable; rebuild via newMeshFromArrays.
    (void)mesh;
    (void)posXYZ;
    (void)nrmXYZ;
    (void)uvST;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    return false;
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

void Graphics::begin3DFrameToCanvas(Canvas *) {
    throw eve::Exception("begin3DFrameToCanvas: not supported on the webgpu backend");
}
void Graphics::end3DFrameToCanvas() {}

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
}
void Graphics::setMesh3DClusteredActive(bool active) { mesh3dClusteredActive = active; }
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
    // WebGPU shadow pipeline has no alpha-cutout variant; fall back to the
    // regular solid-quad shadow so callers keep working.
    drawMeshShadow(mesh, lightMVP);
    (void)albedo;
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
    // WebGPU gbuffer pipeline has no alpha-cutout variant; fall back to the
    // regular fill (full quad depth) so callers keep working.
    drawMeshGBuffer(mesh, mvp, model, nearZ, farZ, albedo, tintR, tintG, tintB);
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

// ---------------------------------------------------------------------------
// Voxel
// ---------------------------------------------------------------------------

void Graphics::drawVoxelFaceInstances(const uint32_t *packed, int count, float originX,
                                      float originY, float originZ, const std::string &faceDir,
                                      Texture *atlas, int tilesPerRow, const uint32_t *ao) {
    if (!device || count <= 0 || !packed) return;
    if (!voxelUnitQuadVerts) return;
    // TODO(webgpu): bake ao (2 bits per corner) into the WGSL voxel shader.
    (void)ao;
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

    // Upload packed instances into the voxel instance arena (per frame slot).
    auto &arena = voxelInstanceArena;
    if (!arena.buffer) {
        WGPUBufferDescriptor bd{};
        bd.label = sv("eve_voxel_instances");
        bd.size = 1u << 20;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
        bd.mappedAtCreation = false;
        arena.buffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
        arena.capacity = 1u << 20;
    }
    uint64_t need = uint64_t(count) * 4;
    if (arena.used + need > arena.capacity) {
        uint64_t cap = arena.capacity * 2;
        WGPUBufferDescriptor bd{};
        bd.label = sv("eve_voxel_instances");
        bd.size = cap;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
        bd.mappedAtCreation = false;
        arena.buffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
        arena.capacity = cap;
        arena.used = 0;
    }
    d.instanceBufferOffset = static_cast<uint32_t>(arena.used);
    queue.WriteBuffer(arena.buffer, arena.used, packed, need);
    arena.used += need;
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
    sceneColorSamples = 1;
    sceneColorSlots.reserve(kFramesInFlight);
    for (int s = 0; s < int(kFramesInFlight); ++s) {
        sceneColorSlots.emplace_back();
        SceneColorSlot &slot = sceneColorSlots.back();
        slot.sampleCount = sceneColorSamples;

        WGPUTextureDescriptor cd{};
        cd.label = sv("eve_scene_color");
        cd.dimension = WGPUTextureDimension_2D;
        cd.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        cd.sampleCount = sceneColorSamples;
        cd.format = sceneColorFormat;
        cd.mipLevelCount = 1;
        cd.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
                   WGPUTextureUsage_CopySrc;
        slot.color = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&cd));
        slot.colorView = slot.color.CreateView();

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
}

void Graphics::destroySceneColorResources() {
    sceneColorSlots.clear();
    sceneColorTexture = nullptr;
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
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
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
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;

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

// ---------------------------------------------------------------------------
// Flush helpers
// ---------------------------------------------------------------------------

void Graphics::flushMesh3D(wgpu::RenderPassEncoder pass, WGPUTextureFormat format) {
    if (mesh3dDraws.empty()) return;

    auto &uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + mesh3dDraws.size() * 2048);
    auto &vtxArena = currentVertexArena();

    // Pre-allocate per-draw UBO slots.
    for (auto &d : mesh3dDraws) {
        d.frameUboOffset = uboArena.alloc(sizeof(Mesh3DUBO), 256);
        d.shadowUboOffset = uboArena.alloc(sizeof(ShadowUBO), 256);
        d.pushUboOffset = 0;
        if (d.shader && d.shader->pushConstantSize() > 0)
            d.pushUboOffset = uboArena.alloc(Shader::kPushConstantBytes, 256);
    }

    // Upload the per-draw frame UBO + shared shadow UBO.
    for (auto &d : mesh3dDraws) {
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
        ubo.texBomb = glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
        ubo.parallax = glm::vec4(mesh3dParallaxScale, mesh3dParallaxMin, mesh3dParallaxMax, 0.f);
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

        wgpu::RenderPipeline pipe = mesh3dPipeline;
        if (d.shader && d.shader->gpuHandle) {
            auto *gs = static_cast<GpuShader *>(d.shader->gpuHandle);
            if (gs->isMesh3D && gs->mesh3dPipeline) {
                if (d.shader->isXray() && gs->mesh3dXrayPipeline)
                    pipe = gs->mesh3dXrayPipeline;
                else
                    pipe = gs->mesh3dPipeline;
            }
        }
        if (!pipe) continue;
        pass.SetPipeline(pipe);

        GpuTexture *albedo = gpuForTexture(d.texture);
        GpuTexture *normal = gpuForTexture(mesh3dNormalTexture);
        GpuTexture *env = gpuForTexture(mesh3dEnvTexture);
        GpuTexture *height = gpuForTexture(mesh3dHeightTexture);
        GpuTexture *depth = mesh3dSceneDepthTexture ? gpuForTexture(mesh3dSceneDepthTexture)
                                                    : flatDepthTexture3D;
        wgpu::BindGroup bg = makeMeshBindGroup(albedo, normal, env, height, depth,
                                               d.frameUboOffset, d.shadowUboOffset,
                                               d.pushUboOffset);
        uint32_t offsets[2] = {d.frameUboOffset, d.shadowUboOffset};
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
    pass.SetPipeline(mesh3dShadowPipeline);

    for (int c = 0; c < ShadowConfig::kCascades; ++c) {
        if (shadowCascadeDraws[c].empty()) continue;
        for (auto &d : shadowCascadeDraws[c]) {
            auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
            if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

            uint32_t offset = uboArena.alloc(256, 256);
            queue.WriteBuffer(uboArena.buffer, offset, &d.mvp, sizeof(glm::mat4));

            WGPUBindGroupEntry entry{};
            entry.binding = 0;
            entry.buffer = uboArena.buffer.Get();
            entry.size = 64;
            WGPUBindGroupDescriptor bgd{};
            bgd.layout = shadowSetLayout.Get();
            bgd.entryCount = 1;
            bgd.entries = &entry;
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
    pass.SetPipeline(mesh3dGbufferPipeline);

    for (auto &d : gbufferPassDraws) {
        auto *gpuMesh = static_cast<GpuMesh *>(d.mesh->gpuHandle);
        if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

        struct GbufferPush {
            glm::mat4 mvp;
            glm::mat4 model;
            glm::vec4 clip;
        } push;
        push.mvp = d.mvp;
        push.model = d.model;
        push.clip = glm::vec4(d.nearZ, d.farZ, 0.f, 0.f);

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
        [](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, const char *message) {
            if (message && type != wgpu::ErrorType::NoError) {
                EM_ASM({ console.log("[GPU_ERR] type=" + $0 + " msg=" + UTF8ToString($1)); },
                       (int)type, message);
            }
        });
#endif
}

void Graphics::present() {
    if (!device || !surface || !swapchainConfigured) return;
    pumpReadback();
    rebuildSwapchainIfNeeded();
    if (!swapchainConfigured) return;

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
    if (frameHad3DThisFrame && !sceneColorSlots.empty()) {
        SceneColorSlot &slot = sceneColorSlots[currentFrameSlot()];
        WGPURenderPassColorAttachment colorAtt{};
        colorAtt.view = slot.colorView.Get();
        colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAtt.loadOp = WGPULoadOp_Clear;
        colorAtt.storeOp = WGPUStoreOp_Store;
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
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
        flushVoxelDraws(pass, sceneColorFormat);
        flushMesh3D(pass, sceneColorFormat);
        pass.End();
        sceneView = slot.colorView;
        sceneTex = slot.color;
    }

    // 3. GBuffer pass.
    if (gbufferPassPending && !gbufferSlots.empty()) {
        GbufferSlot &slot = gbufferSlots[currentFrameSlot()];
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
    clearColor = color.value_or(backgroundColor);
    hasPendingClear = true;
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
    wgpu::Texture src = canvas ? canvas->color : (sceneColorSlots.empty() ? nullptr : sceneColorSlots[0].color);
    if (!src || !copyTextureToCpu(instance, device, queue, src, w, h, rgba)) return clearColor;
    const uint8_t *p = rgba.data() + (size_t(y) * w + x) * 4;
    return Color(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f);
}

image::ImageData *Graphics::newImageDataImpl(OffscreenCanvas *canvas) {
    int w = canvas ? canvas->getWidth() : (sceneColorWidth > 0 ? sceneColorWidth : 1);
    int h = canvas ? canvas->getHeight() : (sceneColorHeight > 0 ? sceneColorHeight : 1);
    auto *img = new image::ImageData(w, h, "RGBA8");
    std::vector<uint8_t> rgba;
    wgpu::Texture src = canvas ? canvas->color : (sceneColorSlots.empty() ? nullptr : sceneColorSlots[0].color);
    if (src && copyTextureToCpu(instance, device, queue, src, w, h, rgba)) {
        std::memcpy(img->getData(), rgba.data(), rgba.size());
    }
    return img;
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

bool Graphics::beginFrameReadback(const std::string &path) {
    // A finished readback can be replaced; only refuse while one is in flight.
    if ((pendingReadback_ && !pendingReadback_->done) || !device || sceneColorSlots.empty())
        return false;
    const uint32_t slot = currentFrameSlot();
    if (slot >= sceneColorSlots.size()) return false;
    wgpu::Texture src = sceneColorSlots[slot].color;
    if (!src) return false;
    const int w = sceneColorWidth > 0 ? sceneColorWidth : 1;
    const int h = sceneColorHeight > 0 ? sceneColorHeight : 1;

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

    if (!vert.empty()) {
        WGPUShaderModuleDescriptor md = mdDesc(vert);
        wgpu::ShaderModule vm = dev.CreateShaderModule(
            reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        pd.vertex.module = vm.Get();
        pd.vertex.entryPoint = sv("vs_main");
    } else {
        // Default textured vertex shader.
        WGPUShaderModuleDescriptor md = mdDesc(kTexturedVertWgsl);
        wgpu::ShaderModule vm = dev.CreateShaderModule(
            reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        pd.vertex.module = vm.Get();
        pd.vertex.entryPoint = sv("vs_main");
    }

    if (!frag.empty()) {
        WGPUFragmentState fs{};
        WGPUShaderModuleDescriptor md = mdDesc(frag);
        wgpu::ShaderModule fm = dev.CreateShaderModule(
            reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        fs.module = fm.Get();
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
                              /*shadow*/ false, /*gbuffer*/ false, /*sampleCount*/ 1);
    // X-ray variant: depth test/write off + alpha blend so occluded silhouettes
    // paint over the building (the shader discards visible fragments itself).
    gpu->mesh3dXrayPipeline =
        buildPipelineFromWgsl(device, mesh3dPipelineLayout, sceneColorFormat, vert, fragWgsl,
                              /*depth*/ false, /*blend*/ true, /*mesh3d*/ true, /*hair*/ false,
                              /*shadow*/ false, /*gbuffer*/ false, /*sampleCount*/ 1);

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
