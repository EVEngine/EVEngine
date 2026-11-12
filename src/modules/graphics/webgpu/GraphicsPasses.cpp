#include "common/FrameCapture.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Batcher.h"
#include "graphics/ClusteredLight.h"
#include "graphics/CubemapPrefilter.h"
#include "graphics/GBuffer.h"
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
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/GraphicsPipelineSupport.h"
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

namespace eve::graphics::webgpu {
using namespace detail;

void Graphics::createSceneColorResources(int width, int height) {
    if (!device) return;
    // Keep the main WebGPU scene single-sampled. Dawn/Metal does not complete
    // the current MSAA resolve followed by synchronous scene readback chain.
    // Temporal AA remains available; Vulkan owns the multisampled main path.
    const uint32_t want = 1u;
    if (sceneColorWidth == width && sceneColorHeight == height && sceneColorSamples == want && !sceneColorSlots.empty())
        return;

    destroySceneColorResources();
    sceneColorWidth               = width;
    sceneColorHeight              = height;
    const uint32_t oldSamples     = sceneColorSamples;
    const bool     samplesChanged = oldSamples != want;
    sceneColorSamples             = want;
    sceneColorSlots.reserve(kFramesInFlight);
    for (int s = 0; s < int(kFramesInFlight); ++s) {
        sceneColorSlots.emplace_back();
        SceneColorSlot& slot = sceneColorSlots.back();
        slot.sampleCount     = sceneColorSamples;

        WGPUTextureDescriptor cd{};
        cd.label     = sv("eve_scene_color");
        cd.dimension = WGPUTextureDimension_2D;
        cd.size      = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        // Single-sample resolve target: composited to the swapchain and used
        // as the frame-readback source. The multisampled color lives in
        // msaaColor (created below) and is resolved into this texture.
        cd.sampleCount   = 1;
        cd.format        = sceneColorFormat;
        cd.mipLevelCount = 1;
        cd.usage       = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        slot.color     = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&cd));
        slot.colorView = slot.color.CreateView();

        if (sceneColorSamples > 1) {
            // Multisampled color target; the scene pass resolves it into the
            // single-sample `color` above for compositing / readback.
            WGPUTextureDescriptor mcd{};
            mcd.label         = sv("eve_scene_color_msaa");
            mcd.dimension     = WGPUTextureDimension_2D;
            mcd.size          = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
            mcd.sampleCount   = sceneColorSamples;
            mcd.format        = sceneColorFormat;
            mcd.mipLevelCount = 1;
            mcd.usage         = WGPUTextureUsage_RenderAttachment;
            slot.msaaColor    = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&mcd));
            slot.msaaView     = slot.msaaColor.CreateView();
        }

        WGPUTextureDescriptor dd{};
        dd.label         = sv("eve_scene_depth");
        dd.dimension     = WGPUTextureDimension_2D;
        dd.size          = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        dd.sampleCount   = sceneColorSamples;
        dd.format        = WGPUTextureFormat_Depth32Float;
        dd.mipLevelCount = 1;
        dd.usage         = WGPUTextureUsage_RenderAttachment;
        slot.depth       = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&dd));
        slot.depthView   = slot.depth.CreateView();

        slot.colorGpu.texture      = slot.color;
        slot.colorGpu.view         = slot.colorView;
        slot.colorGpu.width        = width;
        slot.colorGpu.height       = height;
        slot.colorGpu.sampler      = createLinearSampler(device);
        slot.colorGpu.samplerState = TextureSampler::linearMipmap();

        slot.colorTex.gpuHandle   = &slot.colorGpu;
        slot.colorTex.width       = width;
        slot.colorTex.height      = height;
        slot.colorTex.mipmapCount = 1;
    }
    sceneColorTexture = &sceneColorSlots[0].colorTex;
    // 3D pipelines that render into the scene target must match its sample
    // count. Invalidate only the affected cached variants; they are rebuilt
    // individually if a later draw actually needs them.
    if (samplesChanged) {
        mesh3dPipelines           = {};
        mesh3dPipeline            = {};
        mesh3dTransparentPipeline = {};
        mesh3dClusteredPipeline   = {};
        voxelRectPipeline         = {};
        // Deferred lighting is 1x-only; recreate when scene sample count changes.
        destroyDeferredLightingResources();
    }
    if (samplesChanged && gpuDrivenCullPipeline_) {
        // The forward indirect pipeline inherits the scene target sample
        // count. Recreate the GPU-driven layouts/pipelines lazily on the next
        // cull after an MSAA toggle.
        gpuDrivenCullPipeline_          = {};
        gpuDrivenRenderPipeline_        = {};
        gpuDrivenCanvasPipeline_        = {};
        gpuDrivenComputePipelineLayout_ = {};
        gpuDrivenRenderPipelineLayout_  = {};
        gpuDrivenComputeSetLayout_      = {};
        gpuDrivenRenderSetLayout_       = {};
        gpuDrivenComputeBindGroup_      = {};
        gpuDrivenRenderBindGroup_       = {};
    }
}

void Graphics::destroySceneColorResources() {
    sceneColorSlots.clear();
    sceneColorTexture      = nullptr;
    sceneColorHistoryValid = false;
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
    const int wantSize = ShadowSchemeSettings::current().resolvedAtlasMapSize();
    if (shadowDepthArray) {
        if (shadowMapSize == wantSize) return;
        destroyShadowResources();
    }
    shadowMapSize = wantSize;

    // Depth array: CSM + local layers, renderable + sampleable.
    auto*                 gpu = new GpuTexture();
    WGPUTextureDescriptor td{};
    td.label         = sv("eve_shadow_depth");
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {static_cast<uint32_t>(shadowMapSize), static_cast<uint32_t>(shadowMapSize),
                        static_cast<uint32_t>(ShadowConfig::kTotalLayers)};
    td.sampleCount   = 1;
    td.format        = WGPUTextureFormat_Depth32Float;
    td.mipLevelCount = 1;
    td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc |
               WGPUTextureUsage_CopyDst;
    gpu->texture = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));

    WGPUTextureViewDescriptor avd{};
    avd.format          = WGPUTextureFormat_Depth32Float;
    avd.dimension       = WGPUTextureViewDimension_2DArray;
    avd.baseMipLevel    = 0;
    avd.mipLevelCount   = 1;
    avd.baseArrayLayer  = 0;
    avd.arrayLayerCount = 3;
    gpu->view           = gpu->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&avd));

    WGPUSamplerDescriptor sd{};
    sd.label         = sv("eve_shadow_sampler");
    sd.addressModeU  = WGPUAddressMode_ClampToEdge;
    sd.addressModeV  = WGPUAddressMode_ClampToEdge;
    sd.addressModeW  = WGPUAddressMode_ClampToEdge;
    sd.magFilter     = WGPUFilterMode_Linear;
    sd.minFilter     = WGPUFilterMode_Linear;
    sd.mipmapFilter  = WGPUMipmapFilterMode_Nearest;
    sd.compare       = WGPUCompareFunction_LessEqual;
    sd.maxAnisotropy = 1;
    gpu->sampler     = device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&sd));
    shadowDepthArray = gpu;
}

void Graphics::destroyShadowResources() {
    shadowDepthArray = nullptr;
    shadowMapSize    = 0;
}

void Graphics::createGbufferResources(int width, int height) {
    if (!device) return;
    if (!caps.supportsFiveTargetGBuffer())
        throw Exception("WebGPU: five-target GBuffer requires 40 color attachment bytes per sample");
    if (gbufferWidth == width && gbufferHeight == height && !gbufferSlots.empty()) return;
    destroyGbufferResources();
    gbufferWidth  = width;
    gbufferHeight = height;

    WGPUTextureDescriptor td{};
    td.label         = sv("eve_gbuffer_target");
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    td.sampleCount   = 1;
    td.format        = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;

    WGPUTextureDescriptor dd{};
    dd.label         = sv("eve_gbuffer_depth");
    dd.dimension     = WGPUTextureDimension_2D;
    dd.size          = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    dd.sampleCount   = 1;
    dd.format        = WGPUTextureFormat_Depth32Float;
    dd.mipLevelCount = 1;
    // TextureBinding so X-ray (and AO) can sample the scene depth in a later pass.
    dd.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;

    WGPUTextureDescriptor visIdDesc   = td;
    visIdDesc.label                   = sv("eve_visibility_id");
    visIdDesc.format                  = WGPUTextureFormat_RG32Uint;
    WGPUTextureDescriptor visBaryDesc = td;
    visBaryDesc.label                 = sv("eve_visibility_bary");
    visBaryDesc.format                = WGPUTextureFormat_RG16Float;

    gbufferSlots.reserve(kFramesInFlight);
    for (int s = 0; s < int(kFramesInFlight); ++s) {
        gbufferSlots.emplace_back();
        GbufferSlot& slot   = gbufferSlots.back();
        slot.normal         = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.normalView     = slot.normal.CreateView();
        slot.depthColor     = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.depthColorView = slot.depthColor.CreateView();
        slot.albedo         = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.albedoView     = slot.albedo.CreateView();
        slot.pbrParams      = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.pbrParamsView  = slot.pbrParams.CreateView();
        auto emissiveDesc   = td;
        emissiveDesc.format = WGPUTextureFormat_RGBA16Float;
        slot.emissive       = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&emissiveDesc));
        slot.emissiveView   = slot.emissive.CreateView();
        slot.depth          = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&dd));
        slot.depthView      = slot.depth.CreateView();
        slot.visID          = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&visIdDesc));
        slot.visIDView      = slot.visID.CreateView();
        slot.visBary        = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&visBaryDesc));
        slot.visBaryView    = slot.visBary.CreateView();

        slot.normalGpu.texture     = slot.normal;
        slot.normalGpu.view        = slot.normalView;
        slot.normalGpu.sampler     = createLinearSampler(device);
        slot.depthColorGpu.texture = slot.depthColor;
        slot.depthColorGpu.view    = slot.depthColorView;
        slot.depthColorGpu.sampler = createLinearSampler(device);
        slot.albedoGpu.texture     = slot.albedo;
        slot.albedoGpu.view        = slot.albedoView;
        slot.albedoGpu.sampler     = createLinearSampler(device);
        slot.pbrParamsGpu.texture  = slot.pbrParams;
        slot.pbrParamsGpu.view     = slot.pbrParamsView;
        slot.pbrParamsGpu.sampler  = createLinearSampler(device);
        slot.emissiveGpu.texture   = slot.emissive;
        slot.emissiveGpu.view      = slot.emissiveView;
        slot.emissiveGpu.sampler   = createLinearSampler(device);
        slot.depthGpu.texture      = slot.depth;
        slot.depthGpu.view         = slot.depthView;
        slot.depthGpu.sampler      = createLinearSampler(device);

        slot.normalTex.gpuHandle     = &slot.normalGpu;
        slot.normalTex.width         = width;
        slot.normalTex.height        = height;
        slot.depthColorTex.gpuHandle = &slot.depthColorGpu;
        slot.depthColorTex.width     = width;
        slot.depthColorTex.height    = height;
        slot.albedoTex.gpuHandle     = &slot.albedoGpu;
        slot.albedoTex.width         = width;
        slot.albedoTex.height        = height;
        slot.pbrParamsTex.gpuHandle  = &slot.pbrParamsGpu;
        slot.pbrParamsTex.width      = width;
        slot.pbrParamsTex.height     = height;
        slot.emissiveTex.gpuHandle   = &slot.emissiveGpu;
        slot.emissiveTex.width       = width;
        slot.emissiveTex.height      = height;
        slot.depthTex.gpuHandle      = &slot.depthGpu;
        slot.depthTex.width          = width;
        slot.depthTex.height         = height;
    }
}

void Graphics::destroyGbufferResources() {
    gbufferSlots.clear();
    gbufferDepthValid_ = false;
}

void Graphics::createDecalResources(int width, int height) {
    if (!device) return;
    if (decalWidth == width && decalHeight == height && !decalSlots.empty()) return;
    destroyDecalResources();
    decalWidth  = width;
    decalHeight = height;
    WGPUTextureDescriptor td{};
    td.label         = sv("eve_decal_target");
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    td.sampleCount   = 1;
    td.format        = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    decalSlots.resize(kFramesInFlight);
    for (auto& slot : decalSlots) {
        slot.albedo     = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.albedoView = slot.albedo.CreateView();
        slot.normal     = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.normalView = slot.normal.CreateView();
        slot.params     = device.CreateTexture(reinterpret_cast<const wgpu::TextureDescriptor*>(&td));
        slot.paramsView = slot.params.CreateView();
    }
}

void Graphics::destroyDecalResources() {
    decalSlots.clear();
    decalWidth  = 0;
    decalHeight = 0;
}

// ---------------------------------------------------------------------------
// Flush helpers
// ---------------------------------------------------------------------------


void Graphics::flushMesh3D(wgpu::RenderPassEncoder pass, WGPUTextureFormat format, bool canvasTarget) {
    if (mesh3dDraws.empty()) return;

    if (mesh3dClusteredActive && !canvasTarget && !mesh3dClusteredPipeline) {
        for (const auto& d : mesh3dDraws) {
            const bool customShader = d.shader && d.shader->gpuHandle;
            if (!customShader && !d.doubleSided && d.surfaceMode != SurfaceMode::Transparent && d.mesh &&
                !d.mesh->hasGpuSkinning()) {
                createMesh3DClusteredPipeline();
                break;
            }
        }
    }

    auto& uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + mesh3dDraws.size() * 10240);

    // Pre-allocate per-draw UBO slots.
    for (auto& d : mesh3dDraws) {
        d.frameUboOffset     = uboArena.alloc(sizeof(Mesh3DUBO), 256);
        d.shadowUboOffset    = uboArena.alloc(sizeof(ShadowUBO), 256);
        d.clusteredUboOffset = 0;
        if (mesh3dClusteredActive && !canvasTarget && d.mesh && !d.mesh->hasGpuSkinning())
            d.clusteredUboOffset = uboArena.alloc(sizeof(Mesh3DClusteredUBO), 256);
        d.pushUboOffset = 0;
        if (d.shader && d.shader->pushConstantSize() > 0)
            d.pushUboOffset = uboArena.alloc(Shader::kPushConstantBytes, 256);
    }

    // Upload the per-draw frame UBO + shared shadow UBO.
    for (auto& d : mesh3dDraws) {
        if (mesh3dClusteredActive && !canvasTarget && mesh3dClusteredPipeline && d.clusteredUboOffset) {
            Mesh3DClusteredUBO cubo;
            cubo.mvp            = mesh3dViewProj * d.model;
            cubo.model          = d.model;
            cubo.view           = mesh3dView;
            cubo.lightDir       = glm::vec4(glm::vec3(mesh3dClustered.primaryDir), mesh3dClustered.primaryDir.w);
            cubo.lightColor     = glm::vec4(glm::vec3(mesh3dClustered.primaryColor), mesh3dEnvIntensity);
            cubo.tint           = d.tint;
            cubo.cameraPos      = glm::vec4(mesh3dCameraPos, mesh3dRoughness);
            cubo.ambient        = glm::vec4(glm::vec3(mesh3dClustered.ambient), mesh3dMetallic);
            cubo.gridInfo       = mesh3dClustered.gridInfo;
            cubo.clipInfo       = mesh3dClustered.clipInfo;
            cubo.texBomb        = glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
            cubo.parallax       = glm::vec4(mesh3dParallaxScale, mesh3dParallaxMin, mesh3dParallaxMax, 0.f);
            cubo.virtualTexture = d.virtualTexture;
            cubo.virtualAtlas   = d.virtualAtlas;
            float surfaceCode   = float(int(d.surfaceMode));
            if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "dither")
                surfaceCode = 3.f;
            else if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "coverage")
                surfaceCode = 4.f;
            const float aoStrength = (renderControl_ && renderControl_->isEnabled("ao")) ? mesh3dSsaoIntensity : 0.f;
            cubo.surface           = glm::vec4(surfaceCode, d.alphaCutoff, aoStrength, 0.f);
            cubo.envProbeCenter    = glm::vec4(mesh3dEnvProbeCenter, 1.f);
            cubo.envProbeExtent    = glm::vec4(mesh3dEnvProbeExtent, 0.f);
            for (int i = 0; i < ReflectionProbeUpload::kMaxProbes; ++i) {
                if (i >= mesh3dReflectionProbes.count) continue;
                const auto& probe = mesh3dReflectionProbes.probes[i];
                GpuTexture* gpu   = gpuForTexture(probe.cubemap);
                if (!gpu || !gpu->isCube) continue;
                cubo.reflectionProbeCenter[i] = glm::vec4(probe.center, probe.intensity);
                cubo.reflectionProbeExtent[i] = glm::vec4(probe.extent, probe.blendDistance);
            }
            queue.WriteBuffer(uboArena.buffer, d.clusteredUboOffset, &cubo, sizeof(cubo));
            continue;
        }
        Mesh3DUBO ubo;
        ubo.mvp        = mesh3dViewProj * d.model;
        ubo.model      = d.model;
        ubo.lightDir   = glm::vec4(glm::vec3(mesh3dLighting.lights[0].posRadius), float(mesh3dLighting.count));
        ubo.lightColor = mesh3dLighting.lights[0].color;
        ubo.tint       = d.tint;
        ubo.cameraPos  = glm::vec4(mesh3dCameraPos, mesh3dRoughness);
        ubo.ambient    = glm::vec4(glm::vec3(mesh3dLighting.ambient), mesh3dMetallic);
        for (int i = 0; i < Lighting3DPack::kMaxLights; ++i) ubo.lights[i] = mesh3dLighting.lights[i];
        for (size_t i = 0; i < mesh3dLighting.diffuseProbeSh.size(); ++i)
            ubo.diffuseProbeSh[i] = mesh3dLighting.diffuseProbeSh[i];
        ubo.diffuseProbeInfo.x = mesh3dLighting.diffuseProbeShEnabled ? 1.f : 0.f;
        for (size_t i = 0; i < mesh3dLighting.diffuseVolumePosition.size(); ++i) {
            ubo.diffuseVolumePosition[i] = mesh3dLighting.diffuseVolumePosition[i];
            ubo.diffuseVolumeExtent[i]   = mesh3dLighting.diffuseVolumeExtent[i];
        }
        for (size_t i = 0; i < mesh3dLighting.diffuseVolumeSh.size(); ++i)
            ubo.diffuseVolumeSh[i] = mesh3dLighting.diffuseVolumeSh[i];
        ubo.diffuseVolumeInfo.x = static_cast<float>(mesh3dLighting.diffuseVolumeProbeCount);
        ubo.diffuseVolumeInfo.y = mesh3dLighting.diffuseVolumeTrilinearCell ? 1.f : 0.f;
        ubo.lodFade             = d.lodFade;
        float surfaceCode       = float(int(d.surfaceMode));
        if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "dither")
            surfaceCode = 3.f;
        else if (d.surfaceMode == SurfaceMode::Masked && d.alphaTechnique == "coverage")
            surfaceCode = 4.f;
        ubo.texBomb            = glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
        ubo.parallax           = glm::vec4(mesh3dParallaxScale, mesh3dParallaxMin, mesh3dParallaxMax, d.alphaCutoff);
        ubo.virtualTexture     = d.virtualTexture;
        ubo.virtualAtlas       = d.virtualAtlas;
        const float aoStrength = (renderControl_ && renderControl_->isEnabled("ao")) ? mesh3dSsaoIntensity : 0.f;
        ubo.surface            = glm::vec4(surfaceCode, d.alphaCutoff, aoStrength, 0.f);
        ubo.view               = mesh3dView;
        ubo.clipInfo           = glm::vec4(mesh3dNear, mesh3dFar, 0.f, 0.f);
        ubo.cloud              = mesh3dCloud;
        ubo.cloudWind          = mesh3dCloudWind;
        ubo.envProbeCenter     = glm::vec4(mesh3dEnvProbeCenter, 1.f);
        ubo.envProbeExtent     = glm::vec4(mesh3dEnvProbeExtent, 0.f);
        for (int i = 0; i < ReflectionProbeUpload::kMaxProbes; ++i) {
            if (i >= mesh3dReflectionProbes.count) continue;
            const auto& probe = mesh3dReflectionProbes.probes[i];
            GpuTexture* gpu   = gpuForTexture(probe.cubemap);
            if (!gpu || !gpu->isCube) continue;
            ubo.reflectionProbeCenter[i] = glm::vec4(probe.center, probe.intensity);
            ubo.reflectionProbeExtent[i] = glm::vec4(probe.extent, probe.blendDistance);
        }
        ubo.lightColor.w = mesh3dEnvIntensity;
        if (d.mesh && d.mesh->hasGpuSkinning()) ubo.skinInfo.x = static_cast<float>(d.mesh->getSkinPaletteCount());
        ubo.skinInfo.y = static_cast<float>(d.skinInfluenceLimit);
        d.skinBuffer   = uploadSkinPalette(d.mesh);
        // X-ray params travel through the Frame UBO (no extra binding). Packed
        // in bindMeshUniforms("xray") order: colorR..G..B, bias, screenW, screenH,
        // rimPower, rimStrength, alpha.
        if (d.shader && d.shader->isXray() && d.shader->pushConstantSize() >= 9 * sizeof(float)) {
            const float* pc = d.shader->pushConstantData();
            ubo.texBomb     = glm::vec4(pc[0], pc[1], pc[2], pc[8]);  // color.xyz + alpha
            ubo.parallax    = glm::vec4(pc[3], pc[4], pc[5], pc[7]);  // bias, screenW, screenH, rimStrength
            ubo.clipInfo.z  = pc[6];                                  // rimPower
        }
        queue.WriteBuffer(uboArena.buffer, d.frameUboOffset, &ubo, sizeof(ubo));
        if (d.shader && d.shader->pushConstantSize() > 0)
            queue.WriteBuffer(uboArena.buffer, d.pushUboOffset, d.shader->pushConstantData(),
                              Shader::kPushConstantBytes);
    }

    // Capture receive-shadow per draw; RenderSystem3D changes it between queued meshes.
    for (auto& d : mesh3dDraws) {
        ShadowUBO shadowUbo = mesh3dShadows.ubo;
        if (!mesh3dShadows.active || !d.shadowReceive) shadowUbo.bias.y = 0.f;
        queue.WriteBuffer(uboArena.buffer, d.shadowUboOffset, &shadowUbo, sizeof(shadowUbo));
    }

    for (auto& d : mesh3dDraws) {
        auto* gpuMesh = static_cast<GpuMesh*>(d.mesh->gpuHandle);
        if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

        const bool      transparent = d.surfaceMode == SurfaceMode::Transparent;
        const BlendMode blend       = transparent ? d.surfaceBlend : BlendMode::Opaque;
        const bool      depthWrite  = !transparent || d.depthWrite;
        // Canvas targets are 1-sample; scene pipelines follow the active MSAA
        // count. Both sets preserve the per-draw material raster state.
        const bool customShader = d.shader && d.shader->gpuHandle;
        if (d.pbrSurface && !customShader && drawPbrMesh(pass, format, canvasTarget, d, *gpuMesh)) continue;
        wgpu::RenderPipeline pipe;
        if (d.shader && d.shader->gpuHandle) {
            auto* gs = static_cast<GpuShader*>(d.shader->gpuHandle);
            if (gs->isMesh3D && gs->mesh3dPipeline) {
                if (d.shader->isXray() && gs->mesh3dXrayPipeline)
                    pipe = gs->mesh3dXrayPipeline;
                else
                    pipe = gs->mesh3dPipeline;
            }
        }
        if (!pipe) pipe = getMesh3DPipeline(blend, depthWrite, d.doubleSided, canvasTarget);
        const bool useClustered = !canvasTarget && !customShader && !d.doubleSided && mesh3dClusteredActive &&
                                  d.surfaceMode != SurfaceMode::Transparent && mesh3dClusteredPipeline &&
                                  d.clusteredUboOffset && !d.mesh->hasGpuSkinning();
        if (useClustered) pipe = mesh3dClusteredPipeline;
        if (!pipe) continue;
        pass.SetPipeline(pipe);

        GpuTexture*     albedo = gpuForTexture(d.texture);
        GpuTexture*     normal = gpuForTexture(d.normalTexture);
        GpuTexture*     env    = gpuForTexture(mesh3dEnvTexture);
        GpuTexture*     height = gpuForTexture(d.heightTexture);
        GpuTexture*     depth  = mesh3dSceneDepthTexture ? gpuForTexture(mesh3dSceneDepthTexture) : flatDepthTexture3D;
        GpuTexture*     sceneColor = mesh3dSceneColorTexture ? gpuForTexture(mesh3dSceneColorTexture)
                                     : sceneColorHistoryValid && lastPresentSlot < sceneColorSlots.size()
                                         ? &sceneColorSlots[lastPresentSlot].colorGpu
                                         : whiteTexture;
        wgpu::BindGroup bg;
        uint32_t        offsets[3];
        if (useClustered) {
            wgpu::TextureView aoView_ =
                aoReady ? aoView[(aoWriteIndex + 1) % 2] : (whiteTexture ? whiteTexture->view : wgpu::TextureView());
            bg         = makeMesh3DClusteredBindGroup(albedo, normal, env, height, depth, aoView_, d.clusteredUboOffset,
                                                      d.shadowUboOffset);
            offsets[0] = d.clusteredUboOffset;
            offsets[1] = d.shadowUboOffset;
            pass.SetBindGroup(0, bg, 2, offsets);
        } else {
            bg = makeMeshBindGroup(albedo, normal, env, height, depth, sceneColor, d.frameUboOffset, d.shadowUboOffset,
                                   d.pushUboOffset, d.skinBuffer);
            offsets[0] = d.frameUboOffset;
            offsets[1] = d.shadowUboOffset;
            offsets[2] = d.pushUboOffset;
            pass.SetBindGroup(0, bg, 3, offsets);
        }

        if (gpuMesh->indexBuffer) {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, uint64_t(gpuMesh->vertexCount) * gpuMesh->vertexStride);
            const uint64_t indexBytes = gpuMesh->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
            pass.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat, 0,
                                uint64_t(gpuMesh->indexCount) * indexBytes);
            pass.DrawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
        } else {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, uint64_t(gpuMesh->vertexCount) * gpuMesh->vertexStride);
            pass.Draw(gpuMesh->vertexCount, 1, 0, 0);
        }
    }
    mesh3dDraws.clear();
}

void Graphics::recordDirectionalShadowPasses(wgpu::CommandEncoder encoder) {
    // 1. Shadow passes (CSM cascade layers).
    if (shadowDepthArray) {
        // Every cascade is sampled by receivers, including cascades with no
        // caster draw this frame. Clear all layers while shadows are active so
        // an empty cascade deterministically means fully lit (depth 1) instead
        // of retaining undefined or previous-frame depth.
        if (mesh3dShadows.active) {
            for (int c = 0; c < ShadowConfig::kCascades; ++c) {
                WGPUTextureViewDescriptor lvd{};
                lvd.format          = WGPUTextureFormat_Depth32Float;
                lvd.dimension       = WGPUTextureViewDimension_2D;
                lvd.baseMipLevel    = 0;
                lvd.mipLevelCount   = 1;
                lvd.baseArrayLayer  = static_cast<uint32_t>(c);
                lvd.arrayLayerCount = 1;
                wgpu::TextureView layerView =
                    shadowDepthArray->texture.CreateView(reinterpret_cast<const wgpu::TextureViewDescriptor*>(&lvd));
                WGPURenderPassDepthStencilAttachment ds{};
                ds.view              = layerView.Get();
                ds.depthClearValue   = 1.f;
                ds.depthLoadOp       = WGPULoadOp_Clear;
                ds.depthStoreOp      = WGPUStoreOp_Store;
                ds.stencilClearValue = 0;
                ds.stencilLoadOp     = WGPULoadOp_Undefined;
                ds.stencilStoreOp    = WGPUStoreOp_Undefined;
                WGPURenderPassDescriptor rp{};
                rp.depthStencilAttachment = &ds;
                wgpu::RenderPassEncoder pass =
                    encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
                flushShadowPass(pass, c);
                pass.End();
            }
        }
    }
}

void Graphics::flushShadowPass(wgpu::RenderPassEncoder pass, int cascade) {
    auto& uboArena = currentUboArena();
    if (cascade < 0 || cascade >= ShadowConfig::kTotalLayers) return;
    if (!mesh3dShadowPipeline) createShadowPipelines();
    // Adaptive CSM: render into a top-left viewport matching the cascade's
    // allocated map size; shaders remap UVs via cascadeUvScale.
    float viewport = float(shadowMapSize > 0 ? shadowMapSize : ShadowConfig::kMapSize);
    if (cascade >= 0 && cascade < ShadowConfig::kCascades && mesh3dShadows.active) {
        viewport = float(std::max(1, mesh3dShadows.cascadeMapSizes[cascade]));
    }
    pass.SetViewport(0.f, 0.f, viewport, viewport, 0.f, 1.f);
    pass.SetScissorRect(0, 0, uint32_t(viewport), uint32_t(viewport));
    ensureUboArena(uboArena, uboArena.used + shadowCascadeDraws[cascade].size() * 256);
    for (auto& d : shadowCascadeDraws[cascade]) {
        auto* gpuMesh = static_cast<GpuMesh*>(d.mesh->gpuHandle);
        if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

        pass.SetPipeline(d.alphaTest
                             ? (d.doubleSided ? mesh3dShadowAlphaPipeline : mesh3dShadowAlphaSingleSidedPipeline)
                             : (d.doubleSided ? mesh3dShadowPipeline : mesh3dShadowSingleSidedPipeline));

        SkinPassUBO ubo;
        ubo.mvp  = d.mvp;
        ubo.clip = d.lodFade;
        if (d.mesh->hasGpuSkinning()) ubo.skinInfo.x = static_cast<float>(d.mesh->getSkinPaletteCount());
        ubo.skinInfo.y        = static_cast<float>(d.skinInfluenceLimit);
        const auto skinBuffer = uploadSkinPalette(d.mesh);
        uint32_t   offset     = uboArena.alloc(sizeof(SkinPassUBO), 256);
        queue.WriteBuffer(uboArena.buffer, offset, &ubo, sizeof(ubo));

        GpuTexture*        albedo = gpuForTextureOrWhite(d.albedo);
        WGPUBindGroupEntry entries[4]{};
        entries[0].binding     = 0;
        entries[0].buffer      = uboArena.buffer.Get();
        entries[0].size        = sizeof(SkinPassUBO);
        entries[1].binding     = 1;
        entries[1].textureView = albedo->view.Get();
        entries[2].binding     = 2;
        entries[2].sampler     = albedo->sampler.Get();
        entries[3].binding     = 3;
        entries[3].buffer      = skinBuffer.Get();
        entries[3].size        = skinBuffer.GetSize();
        WGPUBindGroupDescriptor bgd{};
        bgd.layout                 = shadowSetLayout.Get();
        bgd.entryCount             = 4;
        bgd.entries                = entries;
        wgpu::BindGroup bg         = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&bgd));
        uint32_t        offsets[1] = {offset};
        pass.SetBindGroup(0, bg, 1, offsets);

        if (gpuMesh->indexBuffer) {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, uint64_t(gpuMesh->vertexCount) * gpuMesh->vertexStride);
            const uint64_t indexBytes = gpuMesh->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
            pass.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat, 0,
                                uint64_t(gpuMesh->indexCount) * indexBytes);
            pass.DrawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
        } else {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, uint64_t(gpuMesh->vertexCount) * gpuMesh->vertexStride);
            pass.Draw(gpuMesh->vertexCount, 1, 0, 0);
        }
    }
    shadowCascadeDraws[cascade].clear();
}

void Graphics::flushGbufferPass(wgpu::RenderPassEncoder pass) {
    if (gbufferPassDraws.empty() || gbufferSlots.empty()) return;
    if (!mesh3dGbufferPipeline) createGbufferPipelines();
    auto& uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + gbufferPassDraws.size() * 4096);
    for (auto& d : gbufferPassDraws) {
        auto* gpuMesh = static_cast<GpuMesh*>(d.mesh->gpuHandle);
        if (!gpuMesh || !gpuMesh->vertexBuffer) continue;

        if (d.pbrDraw) {
            drawPbrMesh(pass, WGPUTextureFormat_RGBA8Unorm, true, *d.pbrDraw, *gpuMesh);
            continue;
        }
        pass.SetPipeline(d.alphaTest ? mesh3dGbufferAlphaPipeline : mesh3dGbufferPipeline);

        SkinPassUBO ubo;
        ubo.mvp   = d.mvp;
        ubo.model = d.model;
        auto u6   = [](float value) { return uint32_t(std::lround(std::clamp(value, 0.f, 1.f) * 63.f)); };
        // Phase B: tint RGB6 | rough7 | metal7 — bit-cast (exceeds float's 24-bit exact ints).
        const uint32_t rough7 = uint32_t(std::lround(std::clamp(d.roughness, 0.f, 1.f) * 127.f));
        const uint32_t metal7 = uint32_t(std::lround(std::clamp(d.metallic, 0.f, 1.f) * 127.f));
        const uint32_t packedTint =
            u6(d.tint.r) | (u6(d.tint.g) << 6) | (u6(d.tint.b) << 12) | (rough7 << 18) | (metal7 << 25);
        auto motion12 = [](float value) {
            return uint32_t(std::lround(std::clamp(value, -1.f, 1.f) * 2047.f)) + 2047u;
        };
        const uint32_t packedMotion = motion12(d.motion.x) | (motion12(d.motion.y) << 12);
        ubo.clip                    = glm::vec4(d.nearZ, d.farZ, std::bit_cast<float>(packedTint), float(packedMotion));
        if (d.mesh->hasGpuSkinning()) ubo.skinInfo.x = static_cast<float>(d.mesh->getSkinPaletteCount());
        ubo.skinInfo.y        = static_cast<float>(d.skinInfluenceLimit);
        const auto skinBuffer = uploadSkinPalette(d.mesh);

        uint32_t offset = uboArena.alloc(sizeof(SkinPassUBO), 256);
        queue.WriteBuffer(uboArena.buffer, offset, &ubo, sizeof(ubo));

        GpuTexture*        albedo = gpuForTextureOrWhite(d.albedo);
        WGPUBindGroupEntry entries[4]{};
        entries[0].binding     = 0;
        entries[0].buffer      = uboArena.buffer.Get();
        entries[0].size        = sizeof(SkinPassUBO);
        entries[1].binding     = 1;
        entries[1].textureView = albedo->view.Get();
        entries[2].binding     = 2;
        entries[2].sampler     = albedo->sampler.Get();
        entries[3].binding     = 3;
        entries[3].buffer      = skinBuffer.Get();
        entries[3].size        = skinBuffer.GetSize();
        WGPUBindGroupDescriptor bgd{};
        bgd.layout                 = gbufferSetLayout.Get();
        bgd.entryCount             = 4;
        bgd.entries                = entries;
        wgpu::BindGroup bg         = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&bgd));
        uint32_t        offsets[1] = {offset};
        pass.SetBindGroup(0, bg, 1, offsets);

        if (gpuMesh->indexBuffer) {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, uint64_t(gpuMesh->vertexCount) * gpuMesh->vertexStride);
            const uint64_t indexBytes = gpuMesh->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
            pass.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat, 0,
                                uint64_t(gpuMesh->indexCount) * indexBytes);
            pass.DrawIndexed(gpuMesh->indexCount, 1, 0, 0, 0);
        } else {
            pass.SetVertexBuffer(0, gpuMesh->vertexBuffer, 0, uint64_t(gpuMesh->vertexCount) * gpuMesh->vertexStride);
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
    if (!decalPipeline) createDecalPipeline();
    auto& uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + decalPassDraws.size() * 512);
    GbufferSlot& gbuffer = gbufferSlots[lastGbufferSlot];
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
        glm::vec4 surfaceParams;
        glm::vec4 texel;
    };
    static_assert(sizeof(DecalUniforms) == 256);
    for (const auto& draw : decalPassDraws) {
        DecalUniforms uniforms{};
        uniforms.invViewProj   = glm::inverse(decalViewProj);
        uniforms.invModel      = glm::inverse(draw.model);
        uniforms.modelR0       = draw.model[0];
        uniforms.modelR1       = draw.model[1];
        uniforms.modelR2       = draw.model[2];
        uniforms.uvRect        = draw.uvRect;
        uniforms.fadeParams    = draw.fadeParams;
        uniforms.extraParams   = draw.extraParams;
        uniforms.surfaceParams = draw.surfaceParams;
        uniforms.texel         = glm::vec4(1.f / float(decalWidth), 1.f / float(decalHeight), 0.f, 0.f);
        uint32_t offset        = uboArena.alloc(256, 256);
        queue.WriteBuffer(uboArena.buffer, offset, &uniforms, sizeof(uniforms));
        GpuTexture*        albedo = gpuForTextureOrWhite(draw.albedo);
        GpuTexture*        normal = gpuForTextureOrWhite(draw.normal);
        GpuTexture*        params = gpuForTextureOrWhite(draw.params);
        WGPUBindGroupEntry entries[7]{};
        entries[0].binding     = 0;
        entries[0].buffer      = uboArena.buffer.Get();
        entries[0].size        = sizeof(uniforms);
        entries[1].binding     = 1;
        entries[1].textureView = albedo->view.Get();
        entries[2].binding     = 2;
        entries[2].textureView = normal->view.Get();
        entries[3].binding     = 3;
        entries[3].textureView = params->view.Get();
        entries[4].binding     = 4;
        entries[4].textureView = gbuffer.depthView.Get();
        entries[5].binding     = 5;
        entries[5].textureView = gbuffer.normalView.Get();
        entries[6].binding     = 6;
        entries[6].sampler     = mainSampler.Get();
        WGPUBindGroupDescriptor descriptor{};
        descriptor.layout     = decalSetLayout.Get();
        descriptor.entryCount = 7;
        descriptor.entries    = entries;
        wgpu::BindGroup group = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&descriptor));
        uint32_t        offsets[1] = {offset};
        pass.SetBindGroup(0, group, 1, offsets);
        pass.Draw(3, 1, 0, 0);
    }
    decalPassDraws.clear();
    decalPassPending = false;
    decalReady       = true;
    clearMeshBindGroupCache();
}

bool Graphics::flushGBufferPassInto(wgpu::CommandEncoder& encoder, bool runAo) {
    if (!gbufferPassPending || gbufferSlots.empty()) return false;
    lastGbufferSlot                    = currentFrameSlot();
    GbufferSlot&                  slot = gbufferSlots[lastGbufferSlot];
    WGPURenderPassColorAttachment colorAtts[5]{};
    for (int i = 0; i < 5; ++i) {
        colorAtts[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAtts[i].loadOp     = WGPULoadOp_Clear;
        colorAtts[i].storeOp    = WGPUStoreOp_Store;
        colorAtts[i].clearValue = {0.f, 0.f, 0.f, 1.f};
    }
    // Match Vulkan GBuffer clears: depthColor=1, pbrParams=(0,1,1,1).
    colorAtts[1].clearValue = {1.f, 1.f, 1.f, 1.f};
    colorAtts[3].clearValue = {0.f, 1.f, 1.f, 1.f};
    colorAtts[0].view       = slot.normalView.Get();
    colorAtts[1].view       = slot.depthColorView.Get();
    colorAtts[2].view       = slot.albedoView.Get();
    colorAtts[3].view       = slot.pbrParamsView.Get();
    colorAtts[4].view       = slot.emissiveView.Get();
    WGPURenderPassDepthStencilAttachment ds{};
    ds.view              = slot.depthView.Get();
    ds.depthClearValue   = 1.f;
    ds.depthLoadOp       = WGPULoadOp_Clear;
    ds.depthStoreOp      = WGPUStoreOp_Store;
    ds.stencilClearValue = 0;
    ds.stencilLoadOp     = WGPULoadOp_Undefined;
    ds.stencilStoreOp    = WGPUStoreOp_Undefined;
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount      = 5;
    rp.colorAttachments          = colorAtts;
    rp.depthStencilAttachment    = &ds;
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&rp));
    flushGbufferPass(pass);
    pass.End();
    gbufferPassPending = false;
    gbufferDepthValid_ = true;

    if (runAo) {
        ensureAOResources(sceneColorWidth, sceneColorHeight);
        if (aoPipeline && aoTex[0]) {
            pushValidationScope();
            GbufferSlot& gslot = gbufferSlots[currentFrameSlot()];
            struct AOUbo {
                glm::vec4 params;  // radius, power, nearZ, farZ
                float     intensity;
                float     invScale;  // AO target size / depth size
                float     pad;
            } aou;
            aou.params    = glm::vec4(0.05f, 1.1f, mesh3dNear, mesh3dFar);
            aou.intensity = 1.0f;
            aou.invScale  = 0.5f;
            aou.pad       = 0.f;
            queue.WriteBuffer(aoUbo, 0, &aou, sizeof(aou));
            wgpu::BindGroup aoBg = makeAOBindGroup(gslot.depthView);

            WGPURenderPassColorAttachment colorAtt{};
            colorAtt.view       = aoView[aoWriteIndex].Get();
            colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAtt.loadOp     = WGPULoadOp_Clear;
            colorAtt.storeOp    = WGPUStoreOp_Store;
            colorAtt.clearValue = {1.f, 1.f, 1.f, 1.f};
            WGPURenderPassDescriptor aoRp{};
            aoRp.colorAttachmentCount = 1;
            aoRp.colorAttachments     = &colorAtt;
            wgpu::RenderPassEncoder apass =
                encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&aoRp));
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
    return true;
}

void Graphics::submitPendingDeferredPasses() {
    if ((!gbufferPassPending || gbufferSlots.empty()) && (!decalPassPending || decalSlots.empty())) return;
    auto& uboArena = currentUboArena();
    uboArena.reset();
    ensureUboArena(uboArena, 4096 + (gbufferPassDraws.size() + decalPassDraws.size()) * 512);
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    if (gbufferPassPending && !gbufferSlots.empty()) {
        lastGbufferSlot                    = currentFrameSlot();
        GbufferSlot&                  slot = gbufferSlots[lastGbufferSlot];
        WGPURenderPassColorAttachment colors[5]{};
        for (auto& color : colors) {
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp     = WGPULoadOp_Clear;
            color.storeOp    = WGPUStoreOp_Store;
            color.clearValue = {0.f, 0.f, 0.f, 0.f};
        }
        colors[1].clearValue = {1.f, 1.f, 1.f, 1.f};
        colors[3].clearValue = {0.f, 1.f, 1.f, 1.f};  // metal=0, rough/ao/spec=1
        colors[0].view       = slot.normalView.Get();
        colors[1].view       = slot.depthColorView.Get();
        colors[2].view       = slot.albedoView.Get();
        colors[3].view       = slot.pbrParamsView.Get();
        colors[4].view       = slot.emissiveView.Get();
        WGPURenderPassDepthStencilAttachment depth{};
        depth.view            = slot.depthView.Get();
        depth.depthClearValue = 1.f;
        depth.depthLoadOp     = WGPULoadOp_Clear;
        depth.depthStoreOp    = WGPUStoreOp_Store;
        depth.stencilLoadOp   = WGPULoadOp_Undefined;
        depth.stencilStoreOp  = WGPUStoreOp_Undefined;
        WGPURenderPassDescriptor descriptor{};
        descriptor.colorAttachmentCount   = 5;
        descriptor.colorAttachments       = colors;
        descriptor.depthStencilAttachment = &depth;
        wgpu::RenderPassEncoder pass =
            encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&descriptor));
        flushGbufferPass(pass);
        pass.End();
        gbufferDepthValid_ = true;
    }
    if (decalPassPending && !decalSlots.empty() && !gbufferSlots.empty()) {
        lastDecalSlot                      = currentFrameSlot();
        DecalSlot&                    slot = decalSlots[lastDecalSlot];
        WGPURenderPassColorAttachment colors[3]{};
        for (auto& color : colors) {
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp     = WGPULoadOp_Clear;
            color.storeOp    = WGPUStoreOp_Store;
            color.clearValue = {0.f, 0.f, 0.f, 0.f};
        }
        colors[0].view = slot.albedoView.Get();
        colors[1].view = slot.normalView.Get();
        colors[2].view = slot.paramsView.Get();
        WGPURenderPassDescriptor descriptor{};
        descriptor.colorAttachmentCount = 3;
        descriptor.colorAttachments     = colors;
        wgpu::RenderPassEncoder pass =
            encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor*>(&descriptor));
        flushDecalPass(pass);
        pass.End();
    }
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
}


// ---------------------------------------------------------------------------
// Present
// ---------------------------------------------------------------------------


}  // namespace eve::graphics::webgpu
