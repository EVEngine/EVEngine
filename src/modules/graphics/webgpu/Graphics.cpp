#include "graphics/webgpu/Graphics.h"
#include "common/FrameCapture.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Batcher.h"
#include "graphics/ClusteredLight.h"
#include "graphics/CubemapPrefilter.h"
#include "graphics/GBuffer.h"
#include "graphics/GraphicsCapabilities.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/ReflectionProbeUpload.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/shaders/ReflectionProbeWgsl.h"
#include "graphics/webgpu/BindGroupLayoutBuilder.h"
#include "graphics/webgpu/Canvas.h"
#include "graphics/webgpu/InitFlow.h"
#include "graphics/webgpu/PipelineBuilder.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include "common/Exception.h"
#include "common/Resource.h"
#include "common/config.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"

// Header-only Assimp mesh types for newMeshFromAssimp. The Assimp library is
// not linked on Emscripten; only these POD headers are required here.
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/vector3.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/gtc/constants.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
// SDL's native X11 declarations define Success, which collides with WebGPU enums.
#ifdef Success
#undef Success
#endif
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "graphics/webgpu/GraphicsPipelineSupport.h"

namespace eve::graphics::webgpu {
using namespace detail;

Graphics::Graphics() {
    FrameCaptureCapabilities caps;
    caps.backend = "webgpu";
    frameCaptures().configure(std::move(caps));
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        uboArenas.emplace_back();
        vertexArenas.emplace_back();
    }
}

Graphics::~Graphics() {
    destroyDeferredLightingResources();
    retireResourceLifetime();
    detachGraphicsArtifactProvider(this);
}

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

    auto inst = InitFlow::createInstance();
    auto adp  = InitFlow::requestAdapter(std::move(inst), surface);
    auto dev  = InitFlow::requestDevice(std::move(adp));
    initHeadlessFromDevice(width, height, std::move(dev));
}

void Graphics::initHeadlessFromDevice(int width, int height, DeviceDone&& dev) {
    if (deviceInitDone || width <= 0 || height <= 0 || !dev.instance || !dev.adapter || !dev.device || !dev.queue)
        throw Exception("Graphics::initHeadlessFromDevice: invalid size, device stage, or initialization state");
    dev.caps.capture(dev.adapter, dev.device);

    instance      = std::move(dev.instance);
    adapter       = std::move(dev.adapter);
    device        = std::move(dev.device);
    queue         = std::move(dev.queue);
    caps          = std::move(dev.caps);
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

    createPipelineResources();
    createShadowResources();
    createDefaultTextures();
    setViewportSize(width, height, width, height);
    createSceneColorResources(width, height);
    headless_      = true;
    initialized    = true;
    deviceInitDone = true;
}

void Graphics::initWithWindow(void* nativeWindow) {
    sdlWindow = nativeWindow;
    if (deviceInitDone) return;

    auto inst = InitFlow::createInstance();

    // ---- Surface ----
    WGPUSurfaceDescriptor surfDesc{};
    surfDesc.label = sv("eve_surface");
#if defined(__EMSCRIPTEN__)
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSel{};
    canvasSel.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasSel.selector    = sv("#canvas");
    surfDesc.nextInChain  = &canvasSel.chain;
    surface               = inst.instance.CreateSurface(reinterpret_cast<const wgpu::SurfaceDescriptor*>(&surfDesc));
    surfaceFormat         = WGPUTextureFormat_BGRA8Unorm;
#else
    wgpu::SurfaceDescriptor nativeSurfDesc{};
    nativeSurfDesc.label = "eve_surface";
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    if (!SDL_GetWindowWMInfo(static_cast<SDL_Window*>(sdlWindow), &wminfo))
        throw Exception("WebGPU: SDL_GetWindowWMInfo failed: %s", SDL_GetError());
#if defined(_WIN32)
    wgpu::SurfaceSourceWindowsHWND winChain{};
    winChain.hwnd              = wminfo.info.win.window;
    winChain.hinstance         = GetModuleHandle(nullptr);
    nativeSurfDesc.nextInChain = &winChain;
    surface                    = inst.instance.CreateSurface(&nativeSurfDesc);
#elif defined(__linux__)
    if (wminfo.subsystem == SDL_SYSWM_X11) {
        wgpu::SurfaceSourceXlibWindow x11Chain{};
        x11Chain.display           = wminfo.info.x11.display;
        x11Chain.window            = wminfo.info.x11.window;
        nativeSurfDesc.nextInChain = &x11Chain;
        surface                    = inst.instance.CreateSurface(&nativeSurfDesc);
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
    } else if (wminfo.subsystem == SDL_SYSWM_WAYLAND) {
        wgpu::SurfaceSourceWaylandSurface wlChain{};
        wlChain.display            = wminfo.info.wl.display;
        wlChain.surface            = wminfo.info.wl.surface;
        nativeSurfDesc.nextInChain = &wlChain;
        surface                    = inst.instance.CreateSurface(&nativeSurfDesc);
#endif
    } else {
        throw Exception("WebGPU: unsupported SDL window subsystem on Linux");
    }
#elif defined(__APPLE__)
    // SDL exposes an NSWindow, while Dawn requires its content view's
    // CAMetalLayer. Runtime messaging keeps this source portable C++.
    using SendId            = id (*)(id, SEL);
    using SendVoidId        = void (*)(id, SEL, id);
    using SendVoidBool      = void (*)(id, SEL, BOOL);
    using SendIsKind        = BOOL (*)(id, SEL, Class);
    const auto sendId       = reinterpret_cast<SendId>(objc_msgSend);
    const auto sendVoidId   = reinterpret_cast<SendVoidId>(objc_msgSend);
    const auto sendVoidBool = reinterpret_cast<SendVoidBool>(objc_msgSend);
    const auto sendIsKind   = reinterpret_cast<SendIsKind>(objc_msgSend);
    id         window       = reinterpret_cast<id>(wminfo.info.cocoa.window);
    id         view         = sendId(window, sel_registerName("contentView"));
    if (view == nil) throw Exception("WebGPU: SDL Cocoa window has no content view");
    sendVoidBool(view, sel_registerName("setWantsLayer:"), YES);
    id    layer           = sendId(view, sel_registerName("layer"));
    Class metalLayerClass = objc_getClass("CAMetalLayer");
    if (metalLayerClass == Nil) throw Exception("WebGPU: CAMetalLayer class unavailable");
    if (layer == nil || !sendIsKind(layer, sel_registerName("isKindOfClass:"), metalLayerClass)) {
        layer = sendId(reinterpret_cast<id>(metalLayerClass), sel_registerName("layer"));
        sendVoidId(view, sel_registerName("setLayer:"), layer);
    }
    if (layer == nil) throw Exception("WebGPU: failed to create CAMetalLayer");
    WGPUSurfaceSourceMetalLayer metalChain{};
    metalChain.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metalChain.layer       = layer;
    surfDesc.nextInChain   = &metalChain.chain;
    surface                = inst.instance.CreateSurface(reinterpret_cast<const wgpu::SurfaceDescriptor*>(&surfDesc));
#else
    throw Exception("WebGPU: unsupported native platform for surface creation");
#endif
#endif
    if (!surface) throw Exception("WebGPU: surface creation failed");
    auto adp = InitFlow::requestAdapter(std::move(inst), surface);
    auto dev = InitFlow::requestDevice(std::move(adp));

    instance = std::move(dev.instance);
    adapter  = std::move(dev.adapter);
    device   = std::move(dev.device);
    queue    = std::move(dev.queue);
    caps     = std::move(dev.caps);
    // Preferred surface format comes from wgpuSurfaceGetCapabilities in the
    // current ABI (Surface::GetPreferredFormat was removed).
    WGPUSurfaceCapabilities caps{};
    if (wgpuSurfaceGetCapabilities(surface.Get(), adapter.Get(), &caps) == WGPUStatus_Success && caps.formatCount > 0) {
        // Match Vulkan's display-space convention: the scene tone-map pass
        // writes sRGB-encoded values to UNORM, then UI is composited without
        // an additional hardware transfer function. Fall back only when the
        // platform does not expose either common UNORM surface format.
        surfaceFormat = caps.formats[0];
#if !defined(__EMSCRIPTEN__)
        for (size_t i = 0; i < caps.formatCount; ++i) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
                surfaceFormat = caps.formats[i];
                break;
            }
            if (caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
                surfaceFormat = caps.formats[i];
            }
        }
#endif
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    swapchainConfigured = false;
    // Bind group layouts / pipelines must exist before the default textures
    // (texture bind groups reference the 2D / mesh3d layouts), and the shadow
    // depth array must exist before mesh bind groups are built at flush time.
    createPipelineResources();
    createShadowResources();
    createDefaultTextures();
    initialized    = true;
    deviceInitDone = true;
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
    this->width       = width;
    this->height      = height;
    this->pixelWidth  = pixelwidth;
    this->pixelHeight = pixelheight;
    logicalW          = width;
    logicalH          = height;
    pixelW            = pixelwidth;
    pixelH            = pixelheight;
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
#if defined(__EMSCRIPTEN__)
    // GPUCanvasConfiguration accepts COPY_SRC for canvas textures. Unlike native
    // surfaces, the browser API has no surface-usage capability query; the C API
    // adapter's synthesized capabilities need not populate the usages member.
    surfaceCanCopySrc = true;
#else
    WGPUSurfaceCapabilities caps{};
    surfaceCanCopySrc = wgpuSurfaceGetCapabilities(surface.Get(), adapter.Get(), &caps) == WGPUStatus_Success &&
                        (caps.usages & WGPUTextureUsage_CopySrc) != 0;
    wgpuSurfaceCapabilitiesFreeMembers(caps);
#endif
    cfg.usage           = WGPUTextureUsage_RenderAttachment | (surfaceCanCopySrc ? WGPUTextureUsage_CopySrc : 0);
    cfg.viewFormatCount = 0;
    cfg.viewFormats     = nullptr;
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
    whiteTexture     = static_cast<GpuTexture*>(newTexture(1, 1, white, false, false)->gpuHandle);

    // Flat normal (0.5,0.5,1) in RGBA8 �?sampled as (0,0,1) normal.
    uint8_t flatNrm[4]  = {128, 128, 255, 255};
    flatNormalTexture   = static_cast<GpuTexture*>(newTexture(1, 1, flatNrm, false, false)->gpuHandle);
    flatNormalTexture3D = flatNormalTexture;

    uint8_t flatH[4]    = {0, 0, 0, 255};
    flatHeightTexture3D = static_cast<GpuTexture*>(newTexture(1, 1, flatH, false, false)->gpuHandle);

    // 1x1 depth placeholder for the mesh3d scene-depth binding (9). Only X-ray
    // shaders sample it; every other mesh draw binds this unused depth view.
    {
        auto*                 gpu = new GpuTexture();
        WGPUTextureDescriptor td{};
        td.label         = sv("eve_flat_depth");
        td.dimension     = WGPUTextureDimension_2D;
        td.size          = {1, 1, 1};
        td.sampleCount   = 1;
        td.format        = WGPUTextureFormat_Depth32Float;
        td.mipLevelCount = 1;
        td.usage         = WGPUTextureUsage_TextureBinding;
        gpu->texture     = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        WGPUTextureViewDescriptor vd{};
        vd.format          = WGPUTextureFormat_Depth32Float;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.baseMipLevel    = 0;
        vd.mipLevelCount   = 1;
        vd.baseArrayLayer  = 0;
        vd.arrayLayerCount = 1;
        gpu->view          = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
        gpu->width         = 1;
        gpu->height        = 1;
        flatDepthTexture3D = gpu;
    }

    // 1x1xN depth-array placeholder for the mesh3d shadow bindings (5/8).
    // sampleShadowPCF() early-outs (bias.y < 0.5) when shadows are disabled,
    // so the texture is never actually sampled.
    {
        auto*                 gpu = new GpuTexture();
        WGPUTextureDescriptor td{};
        td.label         = sv("eve_default_shadow_depth");
        td.dimension     = WGPUTextureDimension_2D;
        td.size          = {1, 1, static_cast<uint32_t>(ShadowConfig::kTotalLayers)};
        td.sampleCount   = 1;
        td.format        = WGPUTextureFormat_Depth32Float;
        td.mipLevelCount = 1;
        td.usage         = WGPUTextureUsage_TextureBinding;
        gpu->texture     = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        WGPUTextureViewDescriptor vd{};
        vd.format          = WGPUTextureFormat_Depth32Float;
        vd.dimension       = WGPUTextureViewDimension_2DArray;
        vd.baseMipLevel    = 0;
        vd.mipLevelCount   = 1;
        vd.baseArrayLayer  = 0;
        vd.arrayLayerCount = static_cast<uint32_t>(ShadowConfig::kTotalLayers);
        gpu->view          = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&vd));
        WGPUSamplerDescriptor sd{};
        sd.label         = sv("eve_default_shadow_sampler");
        sd.addressModeU  = WGPUAddressMode_ClampToEdge;
        sd.addressModeV  = WGPUAddressMode_ClampToEdge;
        sd.addressModeW  = WGPUAddressMode_ClampToEdge;
        sd.magFilter     = WGPUFilterMode_Linear;
        sd.minFilter     = WGPUFilterMode_Linear;
        sd.mipmapFilter  = WGPUMipmapFilterMode_Nearest;
        sd.compare       = WGPUCompareFunction_LessEqual;
        sd.maxAnisotropy = 1;
        gpu->sampler     = device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&sd));
        defaultShadowTex = gpu;
    }

    // Default env cubemap is black so an unset environment (RenderSystem3D
    // still passes envIntensity=1.0 by default) does not wash the scene
    // toward white. Only an explicitly set envMap contributes IBL.
    uint8_t cubeFace[4] = {0, 0, 0, 0};
    uint8_t cubeData[24];
    for (int f = 0; f < 6; ++f) std::memcpy(cubeData + f * 4, cubeFace, 4);
    defaultEnvCubemap = static_cast<GpuTexture*>(newCubemap(1, cubeData)->gpuHandle);

    // Shared filtering sampler for WGSL bindings declared as plain `sampler`.
    TextureSampler def;
    mainSampler = makeSampler(def, 1);

    const uint8_t transparent[4] = {0, 0, 0, 0};
    const uint8_t decalNormal[4] = {128, 128, 255, 0};
    const uint8_t decalParams[4] = {0, 0, 0, 0};
    decalFlatAlbedo              = newTexture(1, 1, transparent);
    decalFlatNormal              = newTexture(1, 1, decalNormal);
    decalFlatParams              = newTexture(1, 1, decalParams);
}

// ---------------------------------------------------------------------------
// Pipeline resource creation
// ---------------------------------------------------------------------------

bool Graphics::acquireSurfaceTexture(wgpu::TextureView& view, wgpu::Texture& texture) {
    if (!surface || !device || !swapchainConfigured) return false;
    wgpu::SurfaceTexture surfTex{};
    surface.GetCurrentTexture(&surfTex);
    if (!surfTex.texture) return false;
    texture = surfTex.texture;
    view    = texture.CreateView();
    return bool(view);
}

void Graphics::pushValidationScope() {
    // Browser validation is reported by the device uncaptured-error callback.
    // Emdawnwebgpu scope callbacks can report a dropped instance every frame.
#if defined(EVENGINE_WEBGPU) && !defined(__EMSCRIPTEN__)
    if (device) device.PushErrorScope(wgpu::ErrorFilter::Validation);
#endif
}

void Graphics::popValidationScope() {
#if defined(EVENGINE_WEBGPU) && !defined(__EMSCRIPTEN__)
    if (!device) return;
    device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
                         [](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView message) {
                             (void)status;
                             if (message.data && type != wgpu::ErrorType::NoError) {
                                 std::fprintf(stderr, "[webgpu] validation error type=%d: %.*s\n", int(type),
                                              static_cast<int>(message.length), message.data);
                             }
                         });
#endif
}


void Graphics::present() {
    ensureFileTexturesReady();
    applyPendingResourceChanges();
    if (headless_) return;
    if (!isActive()) return;
    if (isCanvasActive()) throw Exception("present: cannot present while a Canvas is active");
    if (!device || !surface || !swapchainConfigured) return;
    pumpReadback();
    rebuildSwapchainIfNeeded();
    if (!swapchainConfigured) return;
    // RenderControl can toggle MSAA without resizing the window. Re-evaluate
    // the offscreen sample count every frame, matching the Vulkan backend.
    if (sceneColorWidth > 0 && sceneColorHeight > 0) createSceneColorResources(sceneColorWidth, sceneColorHeight);
    const bool aoActive = renderControl_ && renderControl_->isEnabled("ao");

    wgpu::TextureView surfaceView;
    wgpu::Texture     surfaceTex;
    if (!acquireSurfaceTexture(surfaceView, surfaceTex)) {
        return;
    }

    auto& uboArena = currentUboArena();
    uboArena.reset();
    auto& vtxArena = currentVertexArena();
    vtxArena.reset();
    voxelInstanceArena.used = 0;
    voxelAoArena.used       = 0;
    ensureUboArena(uboArena, 4096);
    ensureVertexArena(vtxArena, 4096);
    // Match the desktop (Vulkan) flow: the 3D/swapchain pass clears with the
    // background set by setBackgroundColor unless the script called clear().
    if (!hasPendingClear) clearColor = backgroundColor;
    hasPendingClear = false;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    // Compute compaction writes the visible-model table and indexed-indirect
    // commands consumed by the scene render pass below. Ending the compute
    // pass establishes the required WebGPU storage-to-indirect dependency.
    recordGpuDrivenCompute(encoder);

    recordDirectionalShadowPasses(encoder);

    // Stage-3 visibility pass consumes the compute-compacted instance table
    // before the scene pass resolves material shading.
    recordGpuDrivenVisibility(encoder);

    // Hybrid: GBuffer must be written before deferred lighting samples it into
    // scene color. Forward+ keeps the historical order (GBuffer after scene).
    const bool wantDeferredLighting = deferredLightingPending_;
    if (wantDeferredLighting) flushGBufferPassInto(encoder, aoActive);

    // 2. Scene color pass (3D) into the offscreen target.
    wgpu::TextureView sceneView;
    wgpu::Texture     sceneTex;
    if (frameHad3DThisFrame) {
        if (active3DCanvas) {
            // 3D into an offscreen canvas: render the mesh pass into the
            // canvas color/depth (RGBA8Unorm, matches the mesh3d pipelines).
            OffscreenCanvas*              oc = active3DCanvas;
            WGPURenderPassColorAttachment colorAtt{};
            colorAtt.view       = oc->colorView.Get();
            colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAtt.loadOp     = WGPULoadOp_Clear;
            colorAtt.storeOp    = WGPUStoreOp_Store;
            colorAtt.clearValue = {clearColor.r, clearColor.g, clearColor.b, 1.f};
            WGPURenderPassDepthStencilAttachment ds{};
            ds.view              = oc->depthView.Get();
            ds.depthClearValue   = 1.f;
            ds.depthLoadOp       = WGPULoadOp_Clear;
            ds.depthStoreOp      = WGPUStoreOp_Store;
            ds.stencilClearValue = 0;
            ds.stencilLoadOp     = WGPULoadOp_Undefined;
            ds.stencilStoreOp    = WGPUStoreOp_Undefined;
            WGPURenderPassDescriptor rp{};
            rp.colorAttachmentCount   = 1;
            rp.colorAttachments       = &colorAtt;
            rp.depthStencilAttachment = &ds;
            wgpu::RenderPassEncoder pass =
                encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
            // Voxel draws are not expected on the canvas path (voxel module is
            // trimmed from the web build), and its pipeline follows the scene
            // sample count which would mismatch the 1x canvas attachment.
            flushMesh3D(pass, WGPUTextureFormat_RGBA8Unorm, /*canvasTarget*/ true);
            flushGpuDrivenDraws(pass, /*canvasTarget*/ true);
            flushPrimitive3D(pass, WGPUTextureFormat_RGBA8Unorm, 1);
            pass.End();
            oc->clearRequested = false;
            // The script draws the canvas texture explicitly (2D path); it is
            // not composited into the swapchain.
            lastReadbackTex = oc->color;
            lastReadbackW   = oc->getWidth();
            lastReadbackH   = oc->getHeight();
        } else if (!sceneColorSlots.empty()) {
            SceneColorSlot&         slot = sceneColorSlots[currentFrameSlot()];
            std::vector<Mesh3dDraw> refractiveDraws;
            const bool              splitSceneColor = mesh3dSceneColorCaptureIndex.has_value() &&
                                         *mesh3dSceneColorCaptureIndex <= mesh3dDraws.size() &&
                                         sceneColorSlots.size() > 1 && slot.sampleCount == 1;
            if (splitSceneColor) {
                auto split = mesh3dDraws.begin() + static_cast<std::ptrdiff_t>(*mesh3dSceneColorCaptureIndex);
                refractiveDraws.assign(std::make_move_iterator(split), std::make_move_iterator(mesh3dDraws.end()));
                mesh3dDraws.erase(split, mesh3dDraws.end());
            }
            WGPURenderPassColorAttachment colorAtt{};
            colorAtt.view          = slot.sampleCount > 1 ? slot.msaaView.Get() : slot.colorView.Get();
            colorAtt.resolveTarget = slot.sampleCount > 1 ? slot.colorView.Get() : nullptr;
            colorAtt.depthSlice    = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAtt.loadOp        = WGPULoadOp_Clear;
            // When resolving to a single-sample target, the multisampled
            // attachment must be discarded (WebGPU spec).
            colorAtt.storeOp    = slot.sampleCount > 1 ? WGPUStoreOp_Discard : WGPUStoreOp_Store;
            colorAtt.clearValue = {clearColor.r, clearColor.g, clearColor.b, 1.f};
            WGPURenderPassDepthStencilAttachment ds{};
            ds.view              = slot.depthView.Get();
            ds.depthClearValue   = 1.f;
            ds.depthLoadOp       = WGPULoadOp_Clear;
            ds.depthStoreOp      = WGPUStoreOp_Store;
            ds.stencilClearValue = 0;
            ds.stencilLoadOp     = WGPULoadOp_Undefined;
            ds.stencilStoreOp    = WGPUStoreOp_Undefined;
            WGPURenderPassDescriptor rp{};
            rp.colorAttachmentCount   = 1;
            rp.colorAttachments       = &colorAtt;
            rp.depthStencilAttachment = &ds;
            wgpu::RenderPassEncoder pass =
                encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
            // Phase D Hybrid: clustered deferred lighting fills opaque scene
            // color + depth before transparent Forward+ mesh draws.
            if (wantDeferredLighting) flushDeferredLighting(pass);
            flushVoxelDraws(pass, sceneColorFormat);
            flushGpuDrivenResolve(pass);
            flushMesh3D(pass, sceneColorFormat);
            flushGpuDrivenDraws(pass, /*canvasTarget*/ false);
            flushPrimitive3D(pass, sceneColorFormat, slot.sampleCount);
            pass.End();
            if (splitSceneColor) {
                const uint32_t snapshotIndex =
                    (currentFrameSlot() + 1u) % static_cast<uint32_t>(sceneColorSlots.size());
                SceneColorSlot&          snapshot = sceneColorSlots[snapshotIndex];
                WGPUTexelCopyTextureInfo source{};
                source.texture  = slot.color.Get();
                source.mipLevel = 0;
                source.origin   = {0, 0, 0};
                source.aspect   = WGPUTextureAspect_All;
                WGPUTexelCopyTextureInfo destination{};
                destination.texture  = snapshot.color.Get();
                destination.mipLevel = 0;
                destination.origin   = {0, 0, 0};
                destination.aspect   = WGPUTextureAspect_All;
                WGPUExtent3D extent{static_cast<uint32_t>(sceneColorWidth), static_cast<uint32_t>(sceneColorHeight), 1};
                encoder.CopyTextureToTexture(reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&source),
                                             reinterpret_cast<const wgpu::TexelCopyTextureInfo*>(&destination),
                                             reinterpret_cast<const wgpu::Extent3D*>(&extent));

                lastPresentSlot        = snapshotIndex;
                sceneColorHistoryValid = true;
                mesh3dDraws            = std::move(refractiveDraws);
                colorAtt.resolveTarget = nullptr;
                colorAtt.loadOp        = WGPULoadOp_Load;
                colorAtt.storeOp       = WGPUStoreOp_Store;
                ds.depthLoadOp         = WGPULoadOp_Load;
                wgpu::RenderPassEncoder transparentPass =
                    encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
                flushMesh3D(transparentPass, sceneColorFormat);
                transparentPass.End();
            }
            mesh3dSceneColorCaptureIndex.reset();
            lastPresentSlot        = currentFrameSlot();
            sceneColorHistoryValid = true;
            lastReadbackTex        = slot.color;
            lastReadbackW          = sceneColorWidth;
            lastReadbackH          = sceneColorHeight;
            sceneView              = slot.colorView;
            sceneTex               = slot.color;
        }
    }

    // 3. GBuffer pass (Forward+ / post-only path). Hybrid already flushed above.
    if (!wantDeferredLighting) flushGBufferPassInto(encoder, aoActive);

    // 3c. Screen-space decals consume the freshly written GBuffer depth and
    // world normal, then write three transparent layer attachments.
    if (decalPassPending && !decalSlots.empty() && !gbufferSlots.empty()) {
        lastDecalSlot                      = currentFrameSlot();
        DecalSlot&                    slot = decalSlots[lastDecalSlot];
        WGPURenderPassColorAttachment colorAtts[3]{};
        for (auto& attachment : colorAtts) {
            attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            attachment.loadOp     = WGPULoadOp_Clear;
            attachment.storeOp    = WGPUStoreOp_Store;
            attachment.clearValue = {0.f, 0.f, 0.f, 0.f};
        }
        colorAtts[0].view = slot.albedoView.Get();
        colorAtts[1].view = slot.normalView.Get();
        colorAtts[2].view = slot.paramsView.Get();
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = 3;
        rp.colorAttachments     = colorAtts;
        wgpu::RenderPassEncoder pass =
            encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
        flushDecalPass(pass);
        pass.End();
    }

    // 4. Active canvas: flush 2D batches into the offscreen target instead.
    if (activeCanvas) {
        auto* oc = static_cast<OffscreenCanvas*>(activeCanvas);
        flush2DToCanvas(oc);
    }

    // 5. Swapchain pass: composite scene color, draw 2D, run the overlay.
    {
        WGPURenderPassColorAttachment colorAtt{};
        colorAtt.view       = surfaceView.Get();
        colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAtt.loadOp     = WGPULoadOp_Clear;
        colorAtt.storeOp    = WGPUStoreOp_Store;
        colorAtt.clearValue = {clearColor.r, clearColor.g, clearColor.b, 1.f};
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments     = &colorAtt;
        wgpu::RenderPassEncoder pass =
            encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));

        if (sceneView && !sceneColorComposited) {
            if (!fullscreenQuadReady) {
                // Textured vertex: pos(2) + color(4) + uv(2) = 32 bytes.
                float verts[32] = {
                    -1.f, -1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f,  -1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f,
                    1.f,  1.f,  1.f, 1.f, 1.f, 1.f, 1.f, 1.f, -1.f, 1.f,  1.f, 1.f, 1.f, 1.f, 0.f, 1.f,
                };
                uint32_t             indices[6] = {0, 1, 2, 2, 3, 0};
                WGPUBufferDescriptor vbd{};
                vbd.label        = sv("eve_fullscreen_vb");
                vbd.size         = sizeof(verts);
                vbd.usage        = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
                fullscreenQuadVb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&vbd));
                queue.WriteBuffer(fullscreenQuadVb, 0, verts, sizeof(verts));
                WGPUBufferDescriptor ibd{};
                ibd.label        = sv("eve_fullscreen_ib");
                ibd.size         = sizeof(indices);
                ibd.usage        = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index;
                fullscreenQuadIb = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&ibd));
                queue.WriteBuffer(fullscreenQuadIb, 0, indices, sizeof(indices));
                fullscreenQuadReady = true;
            }

            GpuTexture sceneGpu;
            sceneGpu.texture           = sceneTex;
            sceneGpu.view              = sceneView;
            sceneGpu.sampler           = createLinearSampler(device);
            wgpu::BindGroup bg         = makeTex2DBindGroup(&sceneGpu, nullptr);
            uint32_t        offsets[1] = {0};
            pass.SetPipeline(get2DTexturedPipeline(BlendMode::Alpha, false));
            pass.SetBindGroup(0, bg, 1, offsets);
            pass.SetVertexBuffer(0, fullscreenQuadVb, 0, 4 * 32);
            pass.SetIndexBuffer(fullscreenQuadIb, wgpu::IndexFormat::Uint32, 0, 24);
            pass.DrawIndexed(6, 1, 0, 0, 0);
        }

        if (!activeCanvas) {
            flush2D(pass, pixelW > 0 ? pixelW : logicalW, pixelH > 0 ? pixelH : logicalH, surfaceFormat);
        }

        if (presentOverlayFn_ && presentOverlayFn_(presentOverlayUser_, nullptr)) {
            WGPURenderPassEncoder cPass = pass.Get();
            presentOverlayFn_(presentOverlayUser_, &cPass);
        }
        pass.End();
    }

    if (screenReadbackEnabled && surfaceCanCopySrc) recordPresentedReadback(encoder, surfaceTex);
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
    frame3DStarted      = false;
    frameHad3DThisFrame = false;
    frameHad3D          = false;
    sceneColorPassOpen  = false;
    active3DCanvas      = nullptr;
    gbufferPassPending  = false;
    deferredLightingPending_ = false;
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------


}  // namespace eve::graphics::webgpu
