#include "graphics/webgpu/Graphics.h"

#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include <stdexcept>
#include <string>

namespace eve::graphics::webgpu {
namespace {

WGPUStringView visLabel(const char *s) {
    WGPUStringView out{};
    out.data = s;
    out.length = WGPU_STRLEN;
    return out;
}

wgpu::ShaderModule visShader(wgpu::Device device, const std::string &source) {
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data = source.c_str();
    wgsl.code.length = source.size();
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl.chain;
    return device.CreateShaderModule(
        reinterpret_cast<const wgpu::ShaderModuleDescriptor *>(&desc));
}

struct VisibilityParams {
    uint32_t outputBase = 0;
    uint32_t instanceCount = 0;
    uint32_t indexType = 0;
    uint32_t pad = 0;
};
static_assert(sizeof(VisibilityParams) == 16);

constexpr const char *kVisibilityWgsl = R"wgsl(
struct Light3D { posRadius: vec4f, color: vec4f };
struct Frame {
    mvp: mat4x4f,
    model: mat4x4f,
    lightDir: vec4f,
    lightColor: vec4f,
    tint: vec4f,
    cameraPos: vec4f,
    ambient: vec4f,
    lights: array<Light3D, 8>,
    texBomb: vec4f,
    parallax: vec4f,
    surface: vec4f,
    view: mat4x4f,
    clipInfo: vec4f,
    cloud: vec4f,
    cloudWind: vec4f,
};
struct Params { outputBase: u32, instanceCount: u32, indexType: u32, pad: u32 };
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) uv: vec2f,
    @location(2) bary: vec3f,
    @location(3) worldPos: vec3f,
    @location(4) viewPos: vec3f,
    @location(5) @interpolate(flat) instanceId: u32,
    @location(6) @interpolate(flat) triBase: u32,
};
struct VisOut {
    @location(0) id: vec2u,
    @location(1) bary: vec2f,
};
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var albedoTex: texture_2d<f32>;
@group(0) @binding(7) var mainSampler: sampler;
@group(1) @binding(0) var<storage, read> visibleModels: array<mat4x4f>;
@group(1) @binding(1) var<storage, read> vertexWords: array<u32>;
@group(1) @binding(2) var<storage, read> indexWords: array<u32>;
@group(1) @binding(3) var<uniform> params: Params;

fn loadIndex(index: u32) -> u32 {
    if (params.indexType == 0u) {
        let packed = indexWords[index >> 1u];
        return select(packed & 0xffffu, packed >> 16u, (index & 1u) != 0u);
    }
    return indexWords[index];
}
fn loadVec3(vertex: u32, wordOffset: u32) -> vec3f {
    let base = vertex * 8u + wordOffset;
    return vec3f(bitcast<f32>(vertexWords[base]), bitcast<f32>(vertexWords[base + 1u]),
                 bitcast<f32>(vertexWords[base + 2u]));
}
fn inverse3x3(m: mat3x3f) -> mat3x3f {
    let a = m[0].x; let b = m[1].x; let c = m[2].x;
    let d = m[0].y; let e = m[1].y; let f = m[2].y;
    let g = m[0].z; let h = m[1].z; let i = m[2].z;
    let det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return mat3x3f(
        vec3f((e * i - f * h) / det, (f * g - d * i) / det, (d * h - e * g) / det),
        vec3f((c * h - b * i) / det, (a * i - c * g) / det, (b * g - a * h) / det),
        vec3f((b * f - c * e) / det, (c * d - a * f) / det, (a * e - b * d) / det));
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32,
           @builtin(instance_index) instanceIndex: u32) -> VSOut {
    let vertex = loadIndex(vertexIndex);
    let objectPos = loadVec3(vertex, 0u);
    let objectNormal = loadVec3(vertex, 3u);
    let uvBase = vertex * 8u + 6u;
    let globalInstance = params.outputBase + instanceIndex;
    let model = visibleModels[globalInstance];
    let world = model * vec4f(objectPos, 1.0);
    var out: VSOut;
    out.pos = frame.mvp * world;
    out.pos.y = -out.pos.y;
    out.worldPos = world.xyz;
    out.viewPos = (frame.view * world).xyz;
    out.worldNormal = normalize(transpose(inverse3x3(
        mat3x3f(model[0].xyz, model[1].xyz, model[2].xyz))) * objectNormal);
    out.uv = vec2f(bitcast<f32>(vertexWords[uvBase]), bitcast<f32>(vertexWords[uvBase + 1u]));
    let corner = vertexIndex % 3u;
    out.bary = vec3f(select(0.0, 1.0, corner == 0u),
                     select(0.0, 1.0, corner == 1u),
                     select(0.0, 1.0, corner == 2u));
    out.instanceId = globalInstance;
    out.triBase = (vertexIndex / 3u) * 3u;
    return out;
}

@fragment
fn fs_main(in: VSOut) -> VisOut {
    let base = textureSample(albedoTex, mainSampler, in.uv) * frame.tint;
    if (base.a < 0.5) { discard; }
    let nearZ = max(frame.clipInfo.x, 1e-4);
    let farZ = max(frame.clipInfo.y, nearZ + 1e-3);
    let linearDepth = clamp((max(-in.viewPos.z, 0.0) - nearZ) / (farZ - nearZ), 0.0, 1.0);
    var out: VisOut;
    out.id = vec2u(in.instanceId, in.triBase);
    out.bary = in.bary.xy;
    return out;
}
)wgsl";

constexpr const char *kResolveVertexWgsl = R"wgsl(
@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let x = f32((index << 1u) & 2u);
    let y = f32(index & 2u);
    return vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}
)wgsl";

std::string makeResolveFragment() {
    std::string source(kMesh3DFragWgsl);
    const std::string entry = "@fragment\nfn fs_main(in: FSIn) -> @location(0) vec4f {";
    const size_t pos = source.find(entry);
    if (pos == std::string::npos)
        throw std::runtime_error("WebGPU visibility resolve: mesh fragment entry not found");
    source.replace(pos, entry.size(), "fn shadeMesh(in: FSIn) -> vec4f {");
    source += R"wgsl(
struct ResolveParams { outputBase: u32, instanceCount: u32, indexType: u32, pad: u32 };
struct ResolveOut {
    @location(0) color: vec4f,
    @builtin(frag_depth) depth: f32,
};
@group(1) @binding(0) var visID: texture_2d<u32>;
@group(1) @binding(1) var visBary: texture_2d<f32>;
@group(1) @binding(2) var<storage, read> resolveModels: array<mat4x4f>;
@group(1) @binding(3) var<storage, read> resolveVertexWords: array<u32>;
@group(1) @binding(4) var<storage, read> resolveIndexWords: array<u32>;
@group(1) @binding(5) var<uniform> resolveParams: ResolveParams;

fn resolveIndex(index: u32) -> u32 {
    if (resolveParams.indexType == 0u) {
        let packed = resolveIndexWords[index >> 1u];
        return select(packed & 0xffffu, packed >> 16u, (index & 1u) != 0u);
    }
    return resolveIndexWords[index];
}
fn resolveVec3(vertex: u32, wordOffset: u32) -> vec3f {
    let base = vertex * 8u + wordOffset;
    return vec3f(bitcast<f32>(resolveVertexWords[base]),
                 bitcast<f32>(resolveVertexWords[base + 1u]),
                 bitcast<f32>(resolveVertexWords[base + 2u]));
}
fn resolveInverse3x3(m: mat3x3f) -> mat3x3f {
    let a = m[0].x; let b = m[1].x; let c = m[2].x;
    let d = m[0].y; let e = m[1].y; let f = m[2].y;
    let g = m[0].z; let h = m[1].z; let i = m[2].z;
    let det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return mat3x3f(
        vec3f((e * i - f * h) / det, (f * g - d * i) / det, (d * h - e * g) / det),
        vec3f((c * h - b * i) / det, (a * i - c * g) / det, (b * g - a * h) / det),
        vec3f((b * f - c * e) / det, (c * d - a * f) / det, (a * e - b * d) / det));
}

@fragment
fn resolve_main(@builtin(position) fragCoord: vec4f) -> ResolveOut {
    let pixel = vec2i(fragCoord.xy);
    let id = textureLoad(visID, pixel, 0).xy;
    if (id.x < resolveParams.outputBase ||
        id.x >= resolveParams.outputBase + resolveParams.instanceCount) { discard; }
    var bary = vec3f(textureLoad(visBary, pixel, 0).xy, 0.0);
    bary.z = 1.0 - bary.x - bary.y;
    bary = clamp(bary, vec3f(0.0), vec3f(1.0));
    bary /= max(bary.x + bary.y + bary.z, 1e-6);
    let i0 = resolveIndex(id.y);
    let i1 = resolveIndex(id.y + 1u);
    let i2 = resolveIndex(id.y + 2u);
    let objectPos = resolveVec3(i0, 0u) * bary.x + resolveVec3(i1, 0u) * bary.y +
                    resolveVec3(i2, 0u) * bary.z;
    let objectNormal = resolveVec3(i0, 3u) * bary.x + resolveVec3(i1, 3u) * bary.y +
                       resolveVec3(i2, 3u) * bary.z;
    let uv0base = i0 * 8u + 6u;
    let uv1base = i1 * 8u + 6u;
    let uv2base = i2 * 8u + 6u;
    let uv0 = vec2f(bitcast<f32>(resolveVertexWords[uv0base]),
                    bitcast<f32>(resolveVertexWords[uv0base + 1u]));
    let uv1 = vec2f(bitcast<f32>(resolveVertexWords[uv1base]),
                    bitcast<f32>(resolveVertexWords[uv1base + 1u]));
    let uv2 = vec2f(bitcast<f32>(resolveVertexWords[uv2base]),
                    bitcast<f32>(resolveVertexWords[uv2base + 1u]));
    let model = resolveModels[id.x];
    let world = model * vec4f(objectPos, 1.0);
    var input: FSIn;
    input.vNormal = normalize(transpose(resolveInverse3x3(
        mat3x3f(model[0].xyz, model[1].xyz, model[2].xyz))) * objectNormal);
    input.vUV = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    input.vTint = ubo.tint;
    input.vWorldPos = world.xyz;
    input.vCameraPos = ubo.cameraPos.xyz;
    input.vViewPos = (ubo.view * world).xyz;
    input.fragCoord = fragCoord;
    let clip = ubo.mvp * world;
    var out: ResolveOut;
    out.color = shadeMesh(input);
    out.depth = clip.z / max(clip.w, 1e-6);
    return out;
}
)wgsl";
    return source;
}

void fillVisibilityFrame(Mesh3DUBO &ubo, Graphics &gfx, Material *material) {
    // Kept here only for fields independent of private Graphics state; the
    // caller fills camera, lighting, clip and effect state directly.
    ubo.model = glm::mat4(1.f);
    ubo.tint = glm::vec4(material->getTintR(), material->getTintG(), material->getTintB(),
                         material->getTintA());
    ubo.cameraPos.w = material->getRoughness();
    ubo.ambient.w = material->getMetallic();
    ubo.texBomb = glm::vec4(material->getTexCellBombScale(), material->getTexCellBombStrength(),
                            material->getTexCellBombRotation(), 0.f);
    ubo.parallax = glm::vec4(material->getParallaxScale(), material->getParallaxMinLayers(),
                             material->getParallaxMaxLayers(), 0.f);
    (void)gfx;
}

}  // namespace

bool Graphics::gpuDrivenResolveWanted() const {
    return gpuDrivenEnabled_ && renderControl_ && renderControl_->isEnabled("visResolve") &&
           sceneColorSamples == 1;
}

void Graphics::gpuDrivenRecordVisPass() {
    if (!gpuDrivenComputePending_ || gpuDrivenBuckets_.empty()) return;
    gpuDrivenVisPending_ = true;
    frameHad3DThisFrame = true;
    frameHad3D = true;
}

void Graphics::gpuDrivenResolve() {
    if (!gpuDrivenVisPending_) return;
    gpuDrivenResolvePending_ = true;
}

void Graphics::ensureGpuDrivenVisibilityResources() {
    if (gpuDrivenVisPipeline_) return;
    WGPUBindGroupLayoutEntry visEntries[4]{};
    for (uint32_t i = 0; i < 3; ++i) {
        visEntries[i].binding = i;
        visEntries[i].visibility = WGPUShaderStage_Vertex;
        visEntries[i].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        visEntries[i].buffer.minBindingSize = i == 0 ? sizeof(glm::mat4) : 4u;
    }
    visEntries[3].binding = 3;
    visEntries[3].visibility = WGPUShaderStage_Vertex;
    visEntries[3].buffer.type = WGPUBufferBindingType_Uniform;
    visEntries[3].buffer.hasDynamicOffset = true;
    visEntries[3].buffer.minBindingSize = sizeof(VisibilityParams);
    WGPUBindGroupLayoutDescriptor vbgl{};
    vbgl.label = visLabel("eve_visibility_bgl");
    vbgl.entryCount = 4;
    vbgl.entries = visEntries;
    gpuDrivenVisSetLayout_ = device.CreateBindGroupLayout(
        reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&vbgl));

    WGPUBindGroupLayoutEntry resolveEntries[6]{};
    resolveEntries[0].binding = 0;
    resolveEntries[0].visibility = WGPUShaderStage_Fragment;
    resolveEntries[0].texture.sampleType = WGPUTextureSampleType_Uint;
    resolveEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    resolveEntries[1].binding = 1;
    resolveEntries[1].visibility = WGPUShaderStage_Fragment;
    resolveEntries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    resolveEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    for (uint32_t i = 2; i < 5; ++i) {
        resolveEntries[i].binding = i;
        resolveEntries[i].visibility = WGPUShaderStage_Fragment;
        resolveEntries[i].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        resolveEntries[i].buffer.minBindingSize = i == 2 ? sizeof(glm::mat4) : 4u;
    }
    resolveEntries[5].binding = 5;
    resolveEntries[5].visibility = WGPUShaderStage_Fragment;
    resolveEntries[5].buffer.type = WGPUBufferBindingType_Uniform;
    resolveEntries[5].buffer.hasDynamicOffset = true;
    resolveEntries[5].buffer.minBindingSize = sizeof(VisibilityParams);
    WGPUBindGroupLayoutDescriptor rbgl{};
    rbgl.label = visLabel("eve_visibility_resolve_bgl");
    rbgl.entryCount = 6;
    rbgl.entries = resolveEntries;
    gpuDrivenResolveSetLayout_ = device.CreateBindGroupLayout(
        reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&rbgl));

    WGPUBindGroupLayout visLayouts[2] = {mesh3dSetLayout.Get(), gpuDrivenVisSetLayout_.Get()};
    WGPUPipelineLayoutDescriptor vpl{};
    vpl.label = visLabel("eve_visibility_layout");
    vpl.bindGroupLayoutCount = 2;
    vpl.bindGroupLayouts = visLayouts;
    gpuDrivenVisPipelineLayout_ = device.CreatePipelineLayout(
        reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&vpl));
    WGPUBindGroupLayout resolveLayouts[2] = {mesh3dSetLayout.Get(),
                                            gpuDrivenResolveSetLayout_.Get()};
    WGPUPipelineLayoutDescriptor rpl{};
    rpl.label = visLabel("eve_visibility_resolve_layout");
    rpl.bindGroupLayoutCount = 2;
    rpl.bindGroupLayouts = resolveLayouts;
    gpuDrivenResolvePipelineLayout_ = device.CreatePipelineLayout(
        reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&rpl));

    wgpu::ShaderModule vis = visShader(device, kVisibilityWgsl);
    WGPUColorTargetState targets[2]{};
    targets[0].format = WGPUTextureFormat_RG32Uint;
    targets[1].format = WGPUTextureFormat_RG16Float;
    for (auto &target : targets) target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs{};
    fs.module = vis.Get();
    fs.entryPoint = visLabel("fs_main");
    fs.targetCount = 2;
    fs.targets = targets;
    WGPUDepthStencilState depth{};
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipelineDescriptor vpd{};
    vpd.label = visLabel("eve_visibility");
    vpd.layout = gpuDrivenVisPipelineLayout_.Get();
    vpd.vertex.module = vis.Get();
    vpd.vertex.entryPoint = visLabel("vs_main");
    vpd.fragment = &fs;
    vpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    vpd.primitive.frontFace = WGPUFrontFace_CW;
    vpd.primitive.cullMode = WGPUCullMode_None;
    vpd.depthStencil = &depth;
    vpd.multisample.count = 1;
    vpd.multisample.mask = 0xFFFFFFFFu;
    gpuDrivenVisPipeline_ = device.CreateRenderPipeline(
        reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&vpd));

    wgpu::ShaderModule resolveVert = visShader(device, kResolveVertexWgsl);
    const std::string resolveSource = makeResolveFragment();
    wgpu::ShaderModule resolveFrag = visShader(device, resolveSource);
    WGPUColorTargetState resolveTarget{};
    resolveTarget.format = sceneColorFormat;
    resolveTarget.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState rfs{};
    rfs.module = resolveFrag.Get();
    rfs.entryPoint = visLabel("resolve_main");
    rfs.targetCount = 1;
    rfs.targets = &resolveTarget;
    depth.depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor rpd{};
    rpd.label = visLabel("eve_visibility_resolve");
    rpd.layout = gpuDrivenResolvePipelineLayout_.Get();
    rpd.vertex.module = resolveVert.Get();
    rpd.vertex.entryPoint = visLabel("vs_main");
    rpd.fragment = &rfs;
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode = WGPUCullMode_None;
    rpd.depthStencil = &depth;
    rpd.multisample.count = 1;
    rpd.multisample.mask = 0xFFFFFFFFu;
    gpuDrivenResolvePipeline_ = device.CreateRenderPipeline(
        reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&rpd));
}

void Graphics::recordGpuDrivenVisibility(wgpu::CommandEncoder encoder) {
    if (!gpuDrivenVisPending_ || gpuDrivenBuckets_.empty()) {
        recordGpuDrivenVgVisibility(encoder);
        return;
    }
    createGbufferResources(sceneColorWidth, sceneColorHeight);
    ensureGpuDrivenVisibilityResources();
    lastGbufferSlot = currentFrameSlot();
    GbufferSlot &slot = gbufferSlots[lastGbufferSlot];
    WGPURenderPassColorAttachment colors[2]{};
    const WGPUTextureView views[2] = {slot.visIDView.Get(), slot.visBaryView.Get()};
    for (uint32_t i = 0; i < 2; ++i) {
        colors[i].view = views[i];
        colors[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colors[i].loadOp = WGPULoadOp_Clear;
        colors[i].storeOp = WGPUStoreOp_Store;
        colors[i].clearValue = i == 0 ? WGPUColor{4294967295.0, 0.0, 0.0, 0.0}
                                      : WGPUColor{0.0, 0.0, 0.0, 0.0};
    }
    WGPURenderPassDepthStencilAttachment depth{};
    depth.view = slot.depthView.Get();
    depth.depthClearValue = 1.f;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.stencilLoadOp = WGPULoadOp_Undefined;
    depth.stencilStoreOp = WGPUStoreOp_Undefined;
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 2;
    rp.colorAttachments = colors;
    rp.depthStencilAttachment = &depth;
    wgpu::RenderPassEncoder pass =
        encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor *>(&rp));
    pass.SetPipeline(gpuDrivenVisPipeline_);

    auto &arena = currentUboArena();
    ensureUboArena(arena, arena.used + gpuDrivenBuckets_.size() * 2048);
    for (uint32_t i = 0; i < gpuDrivenBuckets_.size(); ++i) {
        const GpuDrivenBucket &bucket = gpuDrivenBuckets_[i];
        auto *gpu = static_cast<GpuMesh *>(bucket.mesh->gpuHandle);
        Material *material = bucket.material;
        Mesh3DUBO frame{};
        fillVisibilityFrame(frame, *this, material);
        frame.mvp = mesh3dViewProj;
        frame.view = mesh3dView;
        frame.cameraPos = glm::vec4(mesh3dCameraPos, material->getRoughness());
        frame.envProbeCenter = glm::vec4(mesh3dEnvProbeCenter, 1.f);
        frame.envProbeExtent = glm::vec4(mesh3dEnvProbeExtent, 0.f);
        for (int probeIndex = 0; probeIndex < ReflectionProbeUpload::kMaxProbes; ++probeIndex) {
            if (probeIndex >= mesh3dReflectionProbes.count) continue;
            const auto &probe = mesh3dReflectionProbes.probes[probeIndex];
            GpuTexture *gpuProbe = gpuForTexture(probe.cubemap);
            if (!gpuProbe || !gpuProbe->isCube) continue;
            frame.reflectionProbeCenter[probeIndex] = glm::vec4(probe.center, probe.intensity);
            frame.reflectionProbeExtent[probeIndex] =
                glm::vec4(probe.extent, probe.blendDistance);
        }
        frame.ambient = glm::vec4(glm::vec3(mesh3dLighting.ambient), material->getMetallic());
        frame.clipInfo = glm::vec4(mesh3dNear, mesh3dFar, 0.f, 0.f);
        const uint32_t frameOffset = arena.alloc(sizeof(Mesh3DUBO), 256);
        const uint32_t shadowOffset = arena.alloc(sizeof(ShadowUBO), 256);
        const uint32_t paramsOffset = arena.alloc(sizeof(VisibilityParams), 256);
        queue.WriteBuffer(arena.buffer, frameOffset, &frame, sizeof(frame));
        ShadowUBO shadow = mesh3dShadows.ubo;
        queue.WriteBuffer(arena.buffer, shadowOffset, &shadow, sizeof(shadow));
        VisibilityParams params{bucket.outputBase, bucket.inputCount,
                                gpu->indexFormat == wgpu::IndexFormat::Uint16 ? 0u : 1u, 0u};
        queue.WriteBuffer(arena.buffer, paramsOffset, &params, sizeof(params));

        GpuTexture *depthTex = mesh3dSceneDepthTexture ? gpuForTexture(mesh3dSceneDepthTexture)
                                                       : flatDepthTexture3D;
        wgpu::BindGroup frameGroup =
            makeMeshBindGroup(gpuForTexture(material->getAlbedoTexture()),
                              gpuForTexture(material->getNormalTexture()),
                              gpuForTexture(mesh3dEnvTexture),
                              gpuForTexture(material->getHeightTexture()), depthTex, nullptr,
                              frameOffset, shadowOffset, 0);
        const uint32_t frameOffsets[3] = {frameOffset, shadowOffset, 0};
        pass.SetBindGroup(0, frameGroup, 3, frameOffsets);
        WGPUBindGroupEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].buffer = gpuDrivenVisibleBuffer_.Get();
        entries[0].size = gpuDrivenVisibleCapacity_;
        entries[1].binding = 1;
        entries[1].buffer = gpu->vertexBuffer.Get();
        entries[1].size = gpu->vertexCapacity;
        entries[2].binding = 2;
        entries[2].buffer = gpu->indexBuffer.Get();
        entries[2].size = gpu->indexCapacity;
        entries[3].binding = 3;
        entries[3].buffer = arena.buffer.Get();
        entries[3].size = sizeof(VisibilityParams);
        WGPUBindGroupDescriptor bgd{};
        bgd.layout = gpuDrivenVisSetLayout_.Get();
        bgd.entryCount = 4;
        bgd.entries = entries;
        wgpu::BindGroup group = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&bgd));
        pass.SetBindGroup(1, group, 1, &paramsOffset);
        pass.DrawIndirect(gpuDrivenVisIndirectBuffer_, uint64_t(i) * 16u);
    }
    pass.End();
    gpuDrivenVisPending_ = false;
    recordGpuDrivenVgVisibility(encoder);
}

void Graphics::flushGpuDrivenResolve(wgpu::RenderPassEncoder pass) {
    if (!gpuDrivenResolvePending_ || gpuDrivenBuckets_.empty() || gbufferSlots.empty()) {
        flushGpuDrivenVgResolve(pass);
        return;
    }
    GbufferSlot &slot = gbufferSlots[lastGbufferSlot];
    auto &arena = currentUboArena();
    ensureUboArena(arena, arena.used + gpuDrivenBuckets_.size() * 2048);
    pass.SetPipeline(gpuDrivenResolvePipeline_);
    for (const GpuDrivenBucket &bucket : gpuDrivenBuckets_) {
        auto *gpu = static_cast<GpuMesh *>(bucket.mesh->gpuHandle);
        Material *material = bucket.material;
        Mesh3DUBO frame{};
        fillVisibilityFrame(frame, *this, material);
        frame.mvp = mesh3dViewProj;
        frame.view = mesh3dView;
        frame.lightDir = glm::vec4(glm::vec3(mesh3dLighting.lights[0].posRadius),
                                   float(mesh3dLighting.count));
        frame.lightColor = mesh3dLighting.lights[0].color;
        frame.lightColor.w = mesh3dEnvIntensity;
        frame.cameraPos = glm::vec4(mesh3dCameraPos, material->getRoughness());
        frame.envProbeCenter = glm::vec4(mesh3dEnvProbeCenter, 1.f);
        frame.envProbeExtent = glm::vec4(mesh3dEnvProbeExtent, 0.f);
        for (int probeIndex = 0; probeIndex < ReflectionProbeUpload::kMaxProbes; ++probeIndex) {
            if (probeIndex >= mesh3dReflectionProbes.count) continue;
            const auto &probe = mesh3dReflectionProbes.probes[probeIndex];
            GpuTexture *gpuProbe = gpuForTexture(probe.cubemap);
            if (!gpuProbe || !gpuProbe->isCube) continue;
            frame.reflectionProbeCenter[probeIndex] = glm::vec4(probe.center, probe.intensity);
            frame.reflectionProbeExtent[probeIndex] =
                glm::vec4(probe.extent, probe.blendDistance);
        }
        frame.ambient = glm::vec4(glm::vec3(mesh3dLighting.ambient), material->getMetallic());
        for (int light = 0; light < Lighting3DPack::kMaxLights; ++light)
            frame.lights[light] = mesh3dLighting.lights[light];
        frame.surface.z = renderControl_ && renderControl_->isEnabled("ao")
                              ? mesh3dSsaoIntensity
                              : 0.f;
        frame.clipInfo = glm::vec4(mesh3dNear, mesh3dFar, 0.f, 0.f);
        frame.cloud = mesh3dCloud;
        frame.cloudWind = mesh3dCloudWind;
        const uint32_t frameOffset = arena.alloc(sizeof(Mesh3DUBO), 256);
        const uint32_t shadowOffset = arena.alloc(sizeof(ShadowUBO), 256);
        const uint32_t paramsOffset = arena.alloc(sizeof(VisibilityParams), 256);
        queue.WriteBuffer(arena.buffer, frameOffset, &frame, sizeof(frame));
        ShadowUBO shadow = mesh3dShadows.ubo;
        if (!mesh3dShadows.active || !material->getReceiveShadow()) shadow.bias.y = 0.f;
        queue.WriteBuffer(arena.buffer, shadowOffset, &shadow, sizeof(shadow));
        VisibilityParams params{bucket.outputBase, bucket.inputCount,
                                gpu->indexFormat == wgpu::IndexFormat::Uint16 ? 0u : 1u, 0u};
        queue.WriteBuffer(arena.buffer, paramsOffset, &params, sizeof(params));

        GpuTexture *depthTex = mesh3dSceneDepthTexture ? gpuForTexture(mesh3dSceneDepthTexture)
                                                       : flatDepthTexture3D;
        wgpu::BindGroup frameGroup =
            makeMeshBindGroup(gpuForTexture(material->getAlbedoTexture()),
                              gpuForTexture(material->getNormalTexture()),
                              gpuForTexture(mesh3dEnvTexture),
                              gpuForTexture(material->getHeightTexture()), depthTex, nullptr,
                              frameOffset, shadowOffset, 0);
        const uint32_t frameOffsets[3] = {frameOffset, shadowOffset, 0};
        pass.SetBindGroup(0, frameGroup, 3, frameOffsets);
        WGPUBindGroupEntry entries[6]{};
        entries[0].binding = 0;
        entries[0].textureView = slot.visIDView.Get();
        entries[1].binding = 1;
        entries[1].textureView = slot.visBaryView.Get();
        entries[2].binding = 2;
        entries[2].buffer = gpuDrivenVisibleBuffer_.Get();
        entries[2].size = gpuDrivenVisibleCapacity_;
        entries[3].binding = 3;
        entries[3].buffer = gpu->vertexBuffer.Get();
        entries[3].size = gpu->vertexCapacity;
        entries[4].binding = 4;
        entries[4].buffer = gpu->indexBuffer.Get();
        entries[4].size = gpu->indexCapacity;
        entries[5].binding = 5;
        entries[5].buffer = arena.buffer.Get();
        entries[5].size = sizeof(VisibilityParams);
        WGPUBindGroupDescriptor bgd{};
        bgd.layout = gpuDrivenResolveSetLayout_.Get();
        bgd.entryCount = 6;
        bgd.entries = entries;
        wgpu::BindGroup group = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&bgd));
        pass.SetBindGroup(1, group, 1, &paramsOffset);
        pass.Draw(3, 1, 0, 0);
    }
    gpuDrivenResolvePending_ = false;
    gpuDrivenBuckets_.clear();
    flushGpuDrivenVgResolve(pass);
}

}  // namespace eve::graphics::webgpu
