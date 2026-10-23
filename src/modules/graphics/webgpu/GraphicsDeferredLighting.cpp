// Hybrid clustered deferred lighting (Phase D): WebGPU parity with the Vulkan
// fullscreen pass. drawDeferredLighting() stages work; flushDeferredLighting()
// runs at the start of the scene-color pass after GBuffer has been written.
#include "graphics/ClusteredLight.h"
#include "graphics/Shadow.h"
#include "graphics/webgpu/BindGroupLayoutBuilder.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include <algorithm>
#include <cstring>

namespace eve::graphics::webgpu {
namespace {

struct DeferredLightingUBO {
    glm::mat4 invViewProj{1.f};
    glm::mat4 view{1.f};
    glm::vec4 lightDir{0.4f, 1.f, 0.3f, 0.f};
    glm::vec4 lightColor{1.f, 1.f, 1.f, 0.f};
    glm::vec4 cameraPos{0.f, 0.f, 3.f, 0.f};
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f};
    glm::vec4 gridInfo{16.f, 9.f, 24.f, 0.f};
    glm::vec4 clipInfo{0.1f, 100.f, 1.f, 1.f};
};
static_assert(sizeof(DeferredLightingUBO) == 224, "DeferredLightingUBO must match WGSL DeferredFrame");

}  // namespace

void Graphics::destroyDeferredLightingResources() {
    deferredLightingPipeline       = {};
    deferredLightingPipelineLayout = {};
    deferredLightingSetLayout      = {};
    deferredLightingNearestSampler = {};
    deferredLightingPending_       = false;
}

void Graphics::createDeferredLightingPipeline() {
    if (deferredLightingPipeline || !device) return;

    BindGroupLayoutBuilder b;
    b.buffer(0, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true, sizeof(DeferredLightingUBO));
    b.texture(1, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.texture(2, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.texture(3, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.texture(4, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.texture(5, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Float, wgpu::TextureViewDimension::e2D);
    b.texture(6, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2D);
    b.buffer(7, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::ReadOnlyStorage, false,
             sizeof(ClusteredLightGpu));
    b.buffer(8, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::ReadOnlyStorage, false,
             sizeof(ClusterTableEntry));
    b.buffer(9, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::ReadOnlyStorage, false, sizeof(uint32_t));
    b.buffer(10, wgpu::ShaderStage::Fragment, wgpu::BufferBindingType::Uniform, true, sizeof(ShadowUBO));
    b.texture(11, wgpu::ShaderStage::Fragment, wgpu::TextureSampleType::Depth, wgpu::TextureViewDimension::e2DArray);
    b.sampler(12, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Filtering);
    b.sampler(13, wgpu::ShaderStage::Fragment, wgpu::SamplerBindingType::Comparison);
    deferredLightingSetLayout = b.build(device, "eve_deferred_lighting");

    WGPUBindGroupLayout          layouts[1] = {deferredLightingSetLayout.Get()};
    WGPUPipelineLayoutDescriptor pl{};
    pl.label                = sv("eve_deferred_lighting_layout");
    pl.bindGroupLayoutCount = 1;
    pl.bindGroupLayouts     = layouts;
    deferredLightingPipelineLayout =
        device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&pl));

    wgpu::ShaderModule vertModule = makeWgslModule(device, kDeferredLightingVertWgsl);
    wgpu::ShaderModule fragModule = makeWgslModule(device, kDeferredLightingFragWgsl);

    WGPUColorTargetState target{};
    target.format    = sceneColorFormat;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs{};
    fs.module      = fragModule.Get();
    fs.entryPoint  = sv("fs_main");
    fs.targetCount = 1;
    fs.targets     = &target;

    WGPUDepthStencilState ds{};
    ds.format            = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    // Always pass; fragment writes frag_depth from GBuffer hwDepth so
    // transparent Forward+ can depth-test against deferred opaques.
    ds.depthCompare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor pd{};
    pd.label                      = sv("eve_deferred_lighting");
    pd.layout                     = deferredLightingPipelineLayout.Get();
    pd.vertex.module              = vertModule.Get();
    pd.vertex.entryPoint          = sv("vs_main");
    pd.vertex.bufferCount         = 0;
    pd.fragment                   = &fs;
    pd.depthStencil               = &ds;
    pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace        = WGPUFrontFace_CCW;
    pd.primitive.cullMode         = WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    // Hybrid compile() forces MSAA off; deferred lighting is 1x only.
    pd.multisample.count = 1;
    pd.multisample.mask  = 0xFFFFFFFFu;
    deferredLightingPipeline =
        device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));

    if (!deferredLightingNearestSampler) {
        WGPUSamplerDescriptor sd{};
        sd.label                       = sv("eve_deferred_nearest");
        sd.addressModeU                = WGPUAddressMode_ClampToEdge;
        sd.addressModeV                = WGPUAddressMode_ClampToEdge;
        sd.addressModeW                = WGPUAddressMode_ClampToEdge;
        sd.magFilter                   = WGPUFilterMode_Nearest;
        sd.minFilter                   = WGPUFilterMode_Nearest;
        sd.mipmapFilter                = WGPUMipmapFilterMode_Nearest;
        sd.maxAnisotropy               = 1.f;
        deferredLightingNearestSampler = device.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&sd));
    }
}

void Graphics::drawDeferredLighting() {
    if (!device || !initialized) return;
    if (gbufferSlots.empty() || gbufferWidth <= 0 || gbufferHeight <= 0) return;
    deferredLightingPending_ = true;
}

void Graphics::ensureDeferredLightingClusteredUpload() {
    if (mesh3dClustered.active) {
        uploadClusteredLighting(mesh3dClustered);
        return;
    }
    std::vector<ClusteredLightGpu> dirs;
    const glm::vec3                dir = glm::vec3(mesh3dLighting.lights[0].posRadius);
    if (glm::length(dir) > 1e-4f) {
        ClusteredLightGpu d{};
        d.posRadius = glm::vec4(glm::normalize(dir), 0.f);
        d.color     = mesh3dLighting.lights[0].color;
        dirs.push_back(d);
    }
    ClusteredLightingUpload empty =
        buildClusteredLighting({}, dirs, mesh3dView, mesh3dNear, mesh3dFar, std::max(1, sceneColorWidth),
                               std::max(1, sceneColorHeight), /*fovYRad*/ 1.f, mesh3dLighting.ambient);
    empty.active = true;
    setMesh3DClusteredLighting(empty);
}

void Graphics::flushDeferredLighting(wgpu::RenderPassEncoder pass) {
    if (!deferredLightingPending_) return;
    deferredLightingPending_ = false;
    if (!pass || !device || gbufferSlots.empty()) return;
    if (sceneColorSamples != 1) return;

    createShadowResources();
    createDeferredLightingPipeline();
    if (!deferredLightingPipeline || !shadowDepthArray) return;

    ensureDeferredLightingClusteredUpload();
    ClusteredStorage& storage = clusteredStorage[currentFrameSlot()];
    if (!storage.lights || !storage.table || !storage.indices) return;

    GbufferSlot& slot = gbufferSlots[lastGbufferSlot < gbufferSlots.size() ? lastGbufferSlot : currentFrameSlot()];
    if (!slot.normalView || !slot.depthView) return;

    DeferredLightingUBO ubo{};
    ubo.invViewProj = glm::inverse(mesh3dViewProj);
    ubo.view        = mesh3dView;
    ubo.cameraPos   = glm::vec4(mesh3dCameraPos, 0.f);
    ubo.ambient     = mesh3dLighting.ambient;
    {
        const glm::vec3 dir = glm::vec3(mesh3dLighting.lights[0].posRadius);
        ubo.lightDir        = glm::vec4(dir, glm::length(dir) > 1e-4f ? 1.f : 0.f);
        ubo.lightColor      = mesh3dLighting.lights[0].color;
    }
    if (mesh3dClustered.active) {
        ubo.gridInfo   = mesh3dClustered.gridInfo;
        ubo.clipInfo   = mesh3dClustered.clipInfo;
        ubo.ambient    = mesh3dClustered.ambient;
        ubo.lightDir   = mesh3dClustered.primaryDir;
        ubo.lightColor = glm::vec4(glm::vec3(mesh3dClustered.primaryColor), 0.f);
        ubo.view       = mesh3dClustered.view;
    }
    ubo.clipInfo.z = float(std::max(1, sceneColorWidth > 0 ? sceneColorWidth : gbufferWidth));
    ubo.clipInfo.w = float(std::max(1, sceneColorHeight > 0 ? sceneColorHeight : gbufferHeight));

    auto& uboArena = currentUboArena();
    ensureUboArena(uboArena, uboArena.used + 512);
    const uint32_t frameOffset  = uboArena.alloc(sizeof(DeferredLightingUBO), 256);
    const uint32_t shadowOffset = uboArena.alloc(sizeof(ShadowUBO), 256);
    queue.WriteBuffer(uboArena.buffer, frameOffset, &ubo, sizeof(ubo));
    ShadowUBO shadowUbo = mesh3dShadows.ubo;
    if (!mesh3dShadows.active) shadowUbo.bias.y = 0.f;
    queue.WriteBuffer(uboArena.buffer, shadowOffset, &shadowUbo, sizeof(shadowUbo));

    WGPUBindGroupEntry entries[14]{};
    entries[0].binding = 0;
    entries[0].buffer  = uboArena.buffer.Get();
    entries[0].offset  = frameOffset;
    entries[0].size    = sizeof(DeferredLightingUBO);

    entries[1].binding     = 1;
    entries[1].textureView = slot.normalView.Get();
    entries[2].binding     = 2;
    entries[2].textureView = slot.depthColorView.Get();
    entries[3].binding     = 3;
    entries[3].textureView = slot.albedoView.Get();
    entries[4].binding     = 4;
    entries[4].textureView = slot.pbrParamsView.Get();
    entries[5].binding     = 5;
    entries[5].textureView = slot.emissiveView.Get();
    entries[6].binding     = 6;
    entries[6].textureView = slot.depthView.Get();

    entries[7].binding = 7;
    entries[7].buffer  = storage.lights.Get();
    entries[7].size    = storage.lightsCap;
    entries[8].binding = 8;
    entries[8].buffer  = storage.table.Get();
    entries[8].size    = storage.tableCap;
    entries[9].binding = 9;
    entries[9].buffer  = storage.indices.Get();
    entries[9].size    = storage.indicesCap;

    entries[10].binding = 10;
    entries[10].buffer  = uboArena.buffer.Get();
    entries[10].offset  = shadowOffset;
    entries[10].size    = sizeof(ShadowUBO);

    entries[11].binding     = 11;
    entries[11].textureView = shadowDepthArray->view.Get();
    entries[12].binding     = 12;
    entries[12].sampler     = deferredLightingNearestSampler.Get();
    entries[13].binding     = 13;
    entries[13].sampler     = shadowDepthArray->sampler.Get();

    WGPUBindGroupDescriptor bgd{};
    bgd.label          = sv("eve_deferred_lighting_bg");
    bgd.layout         = deferredLightingSetLayout.Get();
    bgd.entryCount     = 14;
    bgd.entries        = entries;
    wgpu::BindGroup bg = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&bgd));

    const uint32_t offsets[2] = {frameOffset, shadowOffset};
    pass.SetPipeline(deferredLightingPipeline);
    pass.SetBindGroup(0, bg, 2, offsets);
    pass.Draw(3, 1, 0, 0);
}

}  // namespace eve::graphics::webgpu
