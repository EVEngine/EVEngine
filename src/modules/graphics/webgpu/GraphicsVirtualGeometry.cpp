#include "graphics/webgpu/Graphics.h"

#include "graphics/Material.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/matrix_access.hpp>

namespace eve::graphics::webgpu {
namespace {

WGPUStringView vgLabel(const char *s) {
    WGPUStringView out{};
    out.data = s;
    out.length = WGPU_STRLEN;
    return out;
}

wgpu::ShaderModule vgShader(wgpu::Device device, const char *source) {
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = vgLabel(source);
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl.chain;
    return device.CreateShaderModule(
        reinterpret_cast<const wgpu::ShaderModuleDescriptor *>(&desc));
}

struct VgCullParams {
    glm::mat4 viewProj{1.f};
    glm::vec4 planes[6]{};
    glm::mat4 model{1.f};
    glm::vec4 cameraPos{};
    glm::vec4 clipNearFar{};
    glm::vec4 hzbInfo{};
    glm::uvec4 counts{};
};
static_assert(sizeof(VgCullParams) == 288);

struct VgDrawParams {
    glm::mat4 mvp{1.f};
    glm::mat4 model{1.f};
    glm::vec4 clip{};
    glm::vec4 tint{1.f};
    glm::vec4 lightDir{};
    glm::vec4 lightColor{};
    glm::vec4 ambient{};
    glm::uvec4 ids{};
};
static_assert(sizeof(VgDrawParams) == 224);

constexpr const char *kVgCullWgsl = R"wgsl(
struct Cluster { u0: vec4u, u1: vec4u, u2: vec4u, u3: vec4u };
struct Params {
    viewProj: mat4x4f,
    planes: array<vec4f, 6>,
    model: mat4x4f,
    cameraPos: vec4f,
    clipNearFar: vec4f,
    hzbInfo: vec4f,
    counts: vec4u,
};
struct Command {
    vertexCount: u32,
    instanceCount: u32,
    firstVertex: u32,
    firstInstance: u32,
};
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read> clusters: array<Cluster>;
@group(0) @binding(2) var<storage, read_write> commands: array<Command>;
@group(0) @binding(3) var<storage, read> hzb: array<u32>;

fn remainsVisibleAgainstHzb(center: vec3f, radius: f32) -> bool {
    if (params.clipNearFar.w < 0.5) { return true; }
    let clip = params.viewProj * vec4f(center, 1.0);
    if (clip.w <= params.clipNearFar.x) { return true; }
    let ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z > 1.0) { return true; }
    let toEye = normalize(params.cameraPos.xyz - center);
    let frontClip = params.viewProj * vec4f(center + toEye * radius, 1.0);
    let frontZ = frontClip.z / max(frontClip.w, 1e-6);
    let viewDepth = max(length(center - params.cameraPos.xyz), 1e-4);
    let diameterPx = max(2.0 * radius * params.clipNearFar.z / viewDepth, 1.0);
    let mip = min(u32(ceil(log2(diameterPx))), u32(params.hzbInfo.x));
    let width = max(u32(params.hzbInfo.z) >> mip, 1u);
    let height = max(u32(params.hzbInfo.w) >> mip, 1u);
    let uv = vec2f(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    let p0 = clamp(vec2i(uv * vec2f(f32(width), f32(height))) - vec2i(1),
                   vec2i(0), vec2i(i32(width) - 1, i32(height) - 1));
    let p1 = min(p0 + vec2i(1), vec2i(i32(width) - 1, i32(height) - 1));
    let base = 16u + hzb[mip];
    let a = bitcast<f32>(hzb[base + u32(p0.y) * width + u32(p0.x)]);
    let b = bitcast<f32>(hzb[base + u32(p0.y) * width + u32(p1.x)]);
    let c = bitcast<f32>(hzb[base + u32(p1.y) * width + u32(p0.x)]);
    let d = bitcast<f32>(hzb[base + u32(p1.y) * width + u32(p1.x)]);
    let blocker = min(min(a, b), min(c, d));
    return blocker >= 0.9999 || frontZ <= blocker + 0.002;
}

@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let cid = gid.x;
    if (cid >= params.counts.x) { return; }
    let cluster = clusters[cid];
    let localCenter = vec3f(bitcast<f32>(cluster.u0.x), bitcast<f32>(cluster.u0.y),
                            bitcast<f32>(cluster.u0.z));
    let worldCenter = (params.model * vec4f(localCenter, 1.0)).xyz;
    let scale = max(length(params.model[0].xyz),
                    max(length(params.model[1].xyz), length(params.model[2].xyz)));
    let radius = bitcast<f32>(cluster.u0.w) * scale;
    var visible = true;
    for (var p = 0u; p < 6u; p = p + 1u) {
        let plane = params.planes[p];
        if (dot(plane.xyz, worldCenter) + plane.w < -radius) { visible = false; }
    }
    if (visible) { visible = remainsVisibleAgainstHzb(worldCenter, radius); }
    commands[cid].vertexCount = cluster.u1.y * 3u;
    commands[cid].instanceCount = select(0u, 1u, visible);
    commands[cid].firstVertex = 0u;
    commands[cid].firstInstance = 0u;
}
)wgsl";

constexpr const char *kVgVisWgsl = R"wgsl(
struct Cluster { u0: vec4u, u1: vec4u, u2: vec4u, u3: vec4u };
struct Params {
    mvp: mat4x4f, model: mat4x4f, clip: vec4f, tint: vec4f,
    lightDir: vec4f, lightColor: vec4f, ambient: vec4f, ids: vec4u,
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) normal: vec3f,
    @location(1) bary: vec3f,
    @location(2) @interpolate(flat) triBase: u32,
};
struct FSOut {
    @location(0) id: vec2u,
    @location(1) bary: vec2f,
};
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read> positions: array<u32>;
@group(0) @binding(2) var<storage, read> triangles: array<u32>;
@group(0) @binding(3) var<storage, read> clusters: array<Cluster>;

fn position(index: u32) -> vec3f {
    let base = index * 3u;
    return vec3f(bitcast<f32>(positions[base]), bitcast<f32>(positions[base + 1u]),
                 bitcast<f32>(positions[base + 2u]));
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VSOut {
    let cluster = clusters[params.ids.y];
    let triBase = (cluster.u1.x + vertexIndex / 3u) * 3u;
    let p0 = position(triangles[triBase]);
    let p1 = position(triangles[triBase + 1u]);
    let p2 = position(triangles[triBase + 2u]);
    let w0 = params.model * vec4f(p0, 1.0);
    let w1 = params.model * vec4f(p1, 1.0);
    let w2 = params.model * vec4f(p2, 1.0);
    let corner = vertexIndex % 3u;
    let world = select(select(w2, w1, corner == 1u), w0, corner == 0u);
    var out: VSOut;
    out.pos = params.mvp * world;
    out.pos.y = -out.pos.y;
    out.normal = normalize(cross(w1.xyz - w0.xyz, w2.xyz - w0.xyz));
    out.bary = vec3f(select(0.0, 1.0, corner == 0u),
                     select(0.0, 1.0, corner == 1u),
                     select(0.0, 1.0, corner == 2u));
    out.triBase = triBase;
    return out;
}

@fragment
fn fs_main(in: VSOut) -> FSOut {
    var out: FSOut;
    out.id = vec2u(0x80000000u | params.ids.x, in.triBase);
    out.bary = in.bary.xy;
    return out;
}
)wgsl";

constexpr const char *kVgResolveWgsl = R"wgsl(
struct Params {
    mvp: mat4x4f, model: mat4x4f, clip: vec4f, tint: vec4f,
    lightDir: vec4f, lightColor: vec4f, ambient: vec4f, ids: vec4u,
};
struct Out { @location(0) color: vec4f, @builtin(frag_depth) depth: f32 };
@group(0) @binding(0) var visID: texture_2d<u32>;
@group(0) @binding(1) var visBary: texture_2d<f32>;
@group(0) @binding(2) var<storage, read> positions: array<u32>;
@group(0) @binding(3) var<storage, read> triangles: array<u32>;
@group(0) @binding(4) var<uniform> params: Params;

fn position(index: u32) -> vec3f {
    let base = index * 3u;
    return vec3f(bitcast<f32>(positions[base]), bitcast<f32>(positions[base + 1u]),
                 bitcast<f32>(positions[base + 2u]));
}

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let x = f32((index << 1u) & 2u);
    let y = f32(index & 2u);
    return vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) fragPos: vec4f) -> Out {
    let coord = vec2i(fragPos.xy);
    let id = textureLoad(visID, coord, 0);
    if (id.x != (0x80000000u | params.ids.x)) { discard; }
    let baryXY = textureLoad(visBary, coord, 0).xy;
    let bary = vec3f(baryXY, 1.0 - baryXY.x - baryXY.y);
    let p0 = position(triangles[id.y]);
    let p1 = position(triangles[id.y + 1u]);
    let p2 = position(triangles[id.y + 2u]);
    let w0 = params.model * vec4f(p0, 1.0);
    let w1 = params.model * vec4f(p1, 1.0);
    let w2 = params.model * vec4f(p2, 1.0);
    let world = w0 * bary.x + w1 * bary.y + w2 * bary.z;
    var normal = normalize(cross(w1.xyz - w0.xyz, w2.xyz - w0.xyz));
    if (dot(normal, -params.lightDir.xyz) < 0.0) { normal = -normal; }
    let diffuse = max(dot(normal, normalize(-params.lightDir.xyz)), 0.0);
    let lit = params.ambient.rgb + params.lightColor.rgb * diffuse;
    var out: Out;
    out.color = vec4f(params.tint.rgb * lit, params.tint.a);
    let clip = params.mvp * world;
    out.depth = clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
    return out;
}
)wgsl";

wgpu::Buffer uploadBuffer(wgpu::Device device, wgpu::Queue queue, const char *name,
                          const void *data, uint64_t bytes, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc{};
    desc.label = vgLabel(name);
    desc.size = (bytes + 3u) & ~uint64_t(3u);
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    wgpu::Buffer buffer =
        device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&desc));
    queue.WriteBuffer(buffer, 0, data, bytes);
    return buffer;
}

}  // namespace

uint32_t Graphics::gpuDrivenVgUpload(const GpuVgAssetUpload &asset) {
    if (!asset.positions || asset.vertexCount <= 0 || !asset.triangles ||
        asset.triangleCount <= 0 || !asset.clusters || asset.clusterCount <= 0)
        return kInvalidGpuDrivenSlot;
    GpuDrivenVgAsset out{};
    out.vertexCount = uint32_t(asset.vertexCount);
    out.triangleCount = uint32_t(asset.triangleCount);
    out.clusterCount = uint32_t(asset.clusterCount);
    out.positions = uploadBuffer(device, queue, "eve_vg_positions", asset.positions,
                                 uint64_t(asset.vertexCount) * 3u * sizeof(float),
                                 WGPUBufferUsage_Storage);
    out.triangles = uploadBuffer(device, queue, "eve_vg_triangles", asset.triangles,
                                 uint64_t(asset.triangleCount) * sizeof(uint32_t),
                                 WGPUBufferUsage_Storage);
    out.clusters = uploadBuffer(device, queue, "eve_vg_clusters", asset.clusters,
                                uint64_t(asset.clusterCount) * sizeof(GpuVgCluster),
                                WGPUBufferUsage_Storage);
    WGPUBufferDescriptor indirect{};
    indirect.label = vgLabel("eve_vg_indirect");
    indirect.size = uint64_t(asset.clusterCount) * 16u;
    indirect.usage =
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect;
    out.indirect =
        device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&indirect));
    WGPUBufferDescriptor params{};
    params.label = vgLabel("eve_vg_cull_params");
    params.size = sizeof(VgCullParams);
    params.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    out.params = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&params));
    gpuDrivenVgAssets_.push_back(std::move(out));
    return uint32_t(gpuDrivenVgAssets_.size() - 1u);
}

uint32_t Graphics::gpuDrivenVgAssetId(Mesh *mesh) const {
    const auto it = gpuDrivenVgMeshIds_.find(mesh);
    return it == gpuDrivenVgMeshIds_.end() ? kInvalidGpuDrivenSlot : it->second;
}

bool Graphics::gpuDrivenVgAttachToMesh(Mesh *mesh, uint32_t vgAssetId) {
    if (!mesh || vgAssetId >= gpuDrivenVgAssets_.size()) return false;
    gpuDrivenVgAssets_[vgAssetId].mesh = mesh;
    gpuDrivenVgMeshIds_[mesh] = vgAssetId;
    return true;
}

bool Graphics::gpuDrivenVgSetInstance(uint32_t vgAssetId, const glm::mat4 &model,
                                      uint32_t materialId) {
    if (vgAssetId >= gpuDrivenVgAssets_.size() || materialId >= gpuDrivenMaterials_.size())
        return false;
    auto &asset = gpuDrivenVgAssets_[vgAssetId];
    asset.model = model;
    asset.materialId = materialId;
    asset.active = true;
    gpuDrivenVgComputePending_ = true;
    return true;
}

void Graphics::gpuDrivenVgComputeSection(const glm::mat4 &viewProj, const glm::vec3 &eye,
                                         float fovYDeg, float nearZ, float farZ) {
    if (gbufferSlots.empty() && sceneColorWidth > 0 && sceneColorHeight > 0)
        createGbufferResources(sceneColorWidth, sceneColorHeight);
    if (!gbufferSlots.empty()) ensureGpuDrivenResources(1, 1);
    gpuDrivenVgViewProj_ = viewProj;
    gpuDrivenVgCameraPos_ = eye;
    gpuDrivenVgFovYDeg_ = fovYDeg;
    gpuDrivenVgNear_ = nearZ;
    gpuDrivenVgFar_ = farZ;
    gpuDrivenVgPlanes_[0] = glm::row(viewProj, 3) + glm::row(viewProj, 0);
    gpuDrivenVgPlanes_[1] = glm::row(viewProj, 3) - glm::row(viewProj, 0);
    gpuDrivenVgPlanes_[2] = glm::row(viewProj, 3) + glm::row(viewProj, 1);
    gpuDrivenVgPlanes_[3] = glm::row(viewProj, 3) - glm::row(viewProj, 1);
    gpuDrivenVgPlanes_[4] = glm::row(viewProj, 2);
    gpuDrivenVgPlanes_[5] = glm::row(viewProj, 3) - glm::row(viewProj, 2);
    for (glm::vec4 &plane : gpuDrivenVgPlanes_) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1e-6f) plane /= length;
    }
    gpuDrivenVgComputePending_ = true;
    gpuDrivenVgVisPending_ = true;
    gpuDrivenVgResolvePending_ = true;
    frameHad3DThisFrame = true;
    frameHad3D = true;
}

void Graphics::ensureGpuDrivenVgResources() {
    if (gpuDrivenVgCullPipeline_) return;
    WGPUBindGroupLayoutEntry compute[4]{};
    compute[0].binding = 0;
    compute[0].visibility = WGPUShaderStage_Compute;
    compute[0].buffer.type = WGPUBufferBindingType_Uniform;
    compute[0].buffer.minBindingSize = sizeof(VgCullParams);
    for (uint32_t i = 1; i < 3; ++i) {
        compute[i].binding = i;
        compute[i].visibility = WGPUShaderStage_Compute;
        compute[i].buffer.type = i == 1 ? WGPUBufferBindingType_ReadOnlyStorage
                                         : WGPUBufferBindingType_Storage;
        compute[i].buffer.minBindingSize = i == 1 ? sizeof(GpuVgCluster) : 16u;
    }
    compute[3].binding = 3;
    compute[3].visibility = WGPUShaderStage_Compute;
    compute[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    WGPUBindGroupLayoutDescriptor cbgl{};
    cbgl.entryCount = 4;
    cbgl.entries = compute;
    gpuDrivenVgComputeSetLayout_ = device.CreateBindGroupLayout(
        reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&cbgl));
    WGPUBindGroupLayout rawCompute = gpuDrivenVgComputeSetLayout_.Get();
    WGPUPipelineLayoutDescriptor cpl{};
    cpl.bindGroupLayoutCount = 1;
    cpl.bindGroupLayouts = &rawCompute;
    gpuDrivenVgComputePipelineLayout_ = device.CreatePipelineLayout(
        reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&cpl));
    wgpu::ShaderModule cull = vgShader(device, kVgCullWgsl);
    WGPUComputePipelineDescriptor cpd{};
    cpd.layout = gpuDrivenVgComputePipelineLayout_.Get();
    cpd.compute.module = cull.Get();
    cpd.compute.entryPoint = vgLabel("cs_main");
    gpuDrivenVgCullPipeline_ = device.CreateComputePipeline(
        reinterpret_cast<const wgpu::ComputePipelineDescriptor *>(&cpd));

    WGPUBindGroupLayoutEntry vis[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        vis[i].binding = i;
        vis[i].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        vis[i].buffer.type = i == 0 ? WGPUBufferBindingType_Uniform
                                    : WGPUBufferBindingType_ReadOnlyStorage;
        vis[i].buffer.minBindingSize =
            i == 0 ? sizeof(VgDrawParams) : (i == 3 ? sizeof(GpuVgCluster) : 4u);
    }
    WGPUBindGroupLayoutDescriptor vbgl{};
    vbgl.entryCount = 4;
    vbgl.entries = vis;
    gpuDrivenVgVisSetLayout_ = device.CreateBindGroupLayout(
        reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&vbgl));
    WGPUBindGroupLayout rawVis = gpuDrivenVgVisSetLayout_.Get();
    WGPUPipelineLayoutDescriptor vpl{};
    vpl.bindGroupLayoutCount = 1;
    vpl.bindGroupLayouts = &rawVis;
    gpuDrivenVgVisPipelineLayout_ = device.CreatePipelineLayout(
        reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&vpl));

    wgpu::ShaderModule visModule = vgShader(device, kVgVisWgsl);
    WGPUColorTargetState targets[2]{};
    targets[0].format = WGPUTextureFormat_RG32Uint;
    targets[1].format = WGPUTextureFormat_RG16Float;
    for (auto &target : targets) target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs{};
    fs.module = visModule.Get();
    fs.entryPoint = vgLabel("fs_main");
    fs.targetCount = 2;
    fs.targets = targets;
    WGPUDepthStencilState depth{};
    depth.format = WGPUTextureFormat_Depth32Float;
    depth.depthWriteEnabled = WGPUOptionalBool_True;
    depth.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipelineDescriptor vpd{};
    vpd.layout = gpuDrivenVgVisPipelineLayout_.Get();
    vpd.vertex.module = visModule.Get();
    vpd.vertex.entryPoint = vgLabel("vs_main");
    vpd.fragment = &fs;
    vpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    vpd.primitive.cullMode = WGPUCullMode_None;
    vpd.depthStencil = &depth;
    vpd.multisample.count = 1;
    vpd.multisample.mask = 0xffffffffu;
    gpuDrivenVgVisPipeline_ = device.CreateRenderPipeline(
        reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&vpd));

    WGPUBindGroupLayoutEntry resolve[5]{};
    resolve[0].binding = 0;
    resolve[0].visibility = WGPUShaderStage_Fragment;
    resolve[0].texture.sampleType = WGPUTextureSampleType_Uint;
    resolve[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    resolve[1] = resolve[0];
    resolve[1].binding = 1;
    resolve[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    for (uint32_t i = 2; i < 5; ++i) {
        resolve[i].binding = i;
        resolve[i].visibility = WGPUShaderStage_Fragment;
        resolve[i].buffer.type = i == 4 ? WGPUBufferBindingType_Uniform
                                        : WGPUBufferBindingType_ReadOnlyStorage;
        resolve[i].buffer.minBindingSize = i == 4 ? sizeof(VgDrawParams) : 4u;
    }
    WGPUBindGroupLayoutDescriptor rbgl{};
    rbgl.entryCount = 5;
    rbgl.entries = resolve;
    gpuDrivenVgResolveSetLayout_ = device.CreateBindGroupLayout(
        reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&rbgl));
    WGPUBindGroupLayout rawResolve = gpuDrivenVgResolveSetLayout_.Get();
    WGPUPipelineLayoutDescriptor rpl{};
    rpl.bindGroupLayoutCount = 1;
    rpl.bindGroupLayouts = &rawResolve;
    gpuDrivenVgResolvePipelineLayout_ = device.CreatePipelineLayout(
        reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&rpl));
    wgpu::ShaderModule resolveModule = vgShader(device, kVgResolveWgsl);
    WGPUColorTargetState color{};
    color.format = sceneColorFormat;
    color.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState rfs{};
    rfs.module = resolveModule.Get();
    rfs.entryPoint = vgLabel("fs_main");
    rfs.targetCount = 1;
    rfs.targets = &color;
    WGPUDepthStencilState resolveDepth = depth;
    resolveDepth.depthCompare = WGPUCompareFunction_Always;
    WGPURenderPipelineDescriptor rpd{};
    rpd.layout = gpuDrivenVgResolvePipelineLayout_.Get();
    rpd.vertex.module = resolveModule.Get();
    rpd.vertex.entryPoint = vgLabel("vs_main");
    rpd.fragment = &rfs;
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.depthStencil = &resolveDepth;
    rpd.multisample.count = 1;
    rpd.multisample.mask = 0xffffffffu;
    gpuDrivenVgResolvePipeline_ = device.CreateRenderPipeline(
        reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&rpd));
}

void Graphics::recordGpuDrivenVgCompute(wgpu::CommandEncoder encoder) {
    if (!gpuDrivenVgComputePending_) return;
    ensureGpuDrivenVgResources();
    gpuDrivenVgVisibleDiagnostic_ = 0;
    for (auto &asset : gpuDrivenVgAssets_) {
        if (!asset.active) continue;
        VgCullParams params{};
        params.viewProj = gpuDrivenVgViewProj_;
        std::copy(std::begin(gpuDrivenVgPlanes_), std::end(gpuDrivenVgPlanes_), params.planes);
        params.model = asset.model;
        params.cameraPos = glm::vec4(gpuDrivenVgCameraPos_, 1.f);
        const float projScaleY = float(gpuDrivenHzbHeight_) /
                                 (2.f * std::tan(glm::radians(gpuDrivenVgFovYDeg_) * 0.5f));
        params.clipNearFar = glm::vec4(gpuDrivenVgNear_, gpuDrivenVgFar_, projScaleY,
                                       gbufferDepthValid_ ? 1.f : 0.f);
        params.hzbInfo = glm::vec4(float(gpuDrivenHzbOffsets_.size() - 1u), 0.f,
                                   float(gpuDrivenHzbWidth_), float(gpuDrivenHzbHeight_));
        params.counts.x = asset.clusterCount;
        queue.WriteBuffer(asset.params, 0, &params, sizeof(params));
        WGPUBindGroupEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].buffer = asset.params.Get();
        entries[0].size = sizeof(params);
        entries[1].binding = 1;
        entries[1].buffer = asset.clusters.Get();
        entries[1].size = uint64_t(asset.clusterCount) * sizeof(GpuVgCluster);
        entries[2].binding = 2;
        entries[2].buffer = asset.indirect.Get();
        entries[2].size = uint64_t(asset.clusterCount) * 16u;
        entries[3].binding = 3;
        entries[3].buffer = gpuDrivenHzbBuffer_.Get();
        entries[3].size = gpuDrivenHzbCapacity_;
        WGPUBindGroupDescriptor bgd{};
        bgd.layout = gpuDrivenVgComputeSetLayout_.Get();
        bgd.entryCount = 4;
        bgd.entries = entries;
        wgpu::BindGroup group = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&bgd));
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(gpuDrivenVgCullPipeline_);
        pass.SetBindGroup(0, group, 0, nullptr);
        pass.DispatchWorkgroups((asset.clusterCount + 63u) / 64u, 1, 1);
        pass.End();
        gpuDrivenVgVisibleDiagnostic_ += asset.clusterCount;
    }
    gpuDrivenVgComputePending_ = false;
}

uint32_t Graphics::debugGpuDrivenVgGpuVisibleCount() {
#if defined(__EMSCRIPTEN__)
    return gpuDrivenVgVisibleDiagnostic_;
#else
    uint64_t totalBytes = 0;
    for (const auto &asset : gpuDrivenVgAssets_)
        totalBytes += uint64_t(asset.clusterCount) * 16u;
    if (totalBytes == 0) return 0;
    WGPUBufferDescriptor bd{};
    bd.label = vgLabel("eve_vg_debug_readback");
    bd.size = totalBytes;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    wgpu::Buffer dst =
        device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    uint64_t offset = 0;
    for (const auto &asset : gpuDrivenVgAssets_) {
        const uint64_t bytes = uint64_t(asset.clusterCount) * 16u;
        encoder.CopyBufferToBuffer(asset.indirect, 0, dst, offset, bytes);
        offset += bytes;
    }
    wgpu::CommandBuffer command = encoder.Finish();
    queue.Submit(1, &command);
    struct MapState {
        bool ok = false;
    } state;
    WGPUBufferMapCallbackInfo callback{};
    callback.mode = WGPUCallbackMode_WaitAnyOnly;
    callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void *userdata1, void *) {
        static_cast<MapState *>(userdata1)->ok = status == WGPUMapAsyncStatus_Success;
    };
    callback.userdata1 = &state;
    WGPUFuture future = wgpuBufferMapAsync(dst.Get(), WGPUMapMode_Read, 0, totalBytes, callback);
    WGPUFutureWaitInfo wait{};
    wait.future = future;
    (void)wgpuInstanceWaitAny(instance.Get(), 1, &wait, UINT64_MAX);
    if (!state.ok) return 0;
    const auto *words = static_cast<const uint32_t *>(dst.GetConstMappedRange(0, totalBytes));
    uint32_t visible = 0;
    if (words) {
        for (uint64_t commandOffset = 0; commandOffset < totalBytes / sizeof(uint32_t);
             commandOffset += 4u)
            visible += words[commandOffset + 1u];
    }
    dst.Unmap();
    return visible;
#endif
}

void Graphics::recordGpuDrivenVgVisibility(wgpu::CommandEncoder encoder) {
    if (!gpuDrivenVgVisPending_) return;
    createGbufferResources(sceneColorWidth, sceneColorHeight);
    ensureGpuDrivenVgResources();
    const bool loadExisting = gpuDrivenResolvePending_ && !gpuDrivenBuckets_.empty();
    lastGbufferSlot = currentFrameSlot();
    GbufferSlot &slot = gbufferSlots[lastGbufferSlot];
    WGPURenderPassColorAttachment colors[2]{};
    const WGPUTextureView views[2] = {slot.visIDView.Get(), slot.visBaryView.Get()};
    for (uint32_t i = 0; i < 2; ++i) {
        colors[i].view = views[i];
        colors[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colors[i].loadOp = loadExisting ? WGPULoadOp_Load : WGPULoadOp_Clear;
        colors[i].storeOp = WGPUStoreOp_Store;
        colors[i].clearValue = i == 0 ? WGPUColor{4294967295.0, 0.0, 0.0, 0.0}
                                      : WGPUColor{0.0, 0.0, 0.0, 0.0};
    }
    WGPURenderPassDepthStencilAttachment depth{};
    depth.view = slot.depthView.Get();
    depth.depthClearValue = 1.f;
    depth.depthLoadOp = loadExisting ? WGPULoadOp_Load : WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;
    depth.stencilLoadOp = WGPULoadOp_Undefined;
    depth.stencilStoreOp = WGPUStoreOp_Undefined;
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 2;
    rp.colorAttachments = colors;
    rp.depthStencilAttachment = &depth;
    wgpu::RenderPassEncoder pass =
        encoder.BeginRenderPass(reinterpret_cast<const wgpu::RenderPassDescriptor *>(&rp));
    pass.SetPipeline(gpuDrivenVgVisPipeline_);
    gpuDrivenVgLastIndirectDrawCount_ = 0;
    auto &arena = currentUboArena();
    uint64_t drawCount = 0;
    for (const auto &asset : gpuDrivenVgAssets_)
        if (asset.active) drawCount += asset.clusterCount;
    ensureUboArena(arena, arena.used + drawCount * 256u);
    for (uint32_t assetId = 0; assetId < gpuDrivenVgAssets_.size(); ++assetId) {
        auto &asset = gpuDrivenVgAssets_[assetId];
        if (!asset.active || asset.materialId >= gpuDrivenMaterials_.size()) continue;
        Material *material = gpuDrivenMaterials_[asset.materialId];
        for (uint32_t cluster = 0; cluster < asset.clusterCount; ++cluster) {
            VgDrawParams params{};
            params.mvp = gpuDrivenVgViewProj_;
            params.model = asset.model;
            params.clip = glm::vec4(mesh3dNear, mesh3dFar, 0.f, 0.f);
            params.tint = glm::vec4(material->getTintR(), material->getTintG(),
                                    material->getTintB(), material->getTintA());
            params.ids = glm::uvec4(assetId, cluster, 0u, 0u);
            const uint32_t offset = arena.alloc(sizeof(params), 256);
            queue.WriteBuffer(arena.buffer, offset, &params, sizeof(params));
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer = arena.buffer.Get();
            entries[0].offset = offset;
            entries[0].size = sizeof(params);
            entries[1].binding = 1;
            entries[1].buffer = asset.positions.Get();
            entries[1].size = uint64_t(asset.vertexCount) * 3u * sizeof(float);
            entries[2].binding = 2;
            entries[2].buffer = asset.triangles.Get();
            entries[2].size = uint64_t(asset.triangleCount) * sizeof(uint32_t);
            entries[3].binding = 3;
            entries[3].buffer = asset.clusters.Get();
            entries[3].size = uint64_t(asset.clusterCount) * sizeof(GpuVgCluster);
            WGPUBindGroupDescriptor bgd{};
            bgd.layout = gpuDrivenVgVisSetLayout_.Get();
            bgd.entryCount = 4;
            bgd.entries = entries;
            wgpu::BindGroup group = device.CreateBindGroup(
                reinterpret_cast<const wgpu::BindGroupDescriptor *>(&bgd));
            pass.SetBindGroup(0, group, 0, nullptr);
            pass.DrawIndirect(asset.indirect, uint64_t(cluster) * 16u);
            ++gpuDrivenVgLastIndirectDrawCount_;
        }
    }
    pass.End();
    gpuDrivenVgVisPending_ = false;
}

void Graphics::flushGpuDrivenVgResolve(wgpu::RenderPassEncoder pass) {
    if (!gpuDrivenVgResolvePending_ || gbufferSlots.empty()) return;
    ensureGpuDrivenVgResources();
    GbufferSlot &slot = gbufferSlots[lastGbufferSlot];
    pass.SetPipeline(gpuDrivenVgResolvePipeline_);
    auto &arena = currentUboArena();
    ensureUboArena(arena, arena.used + gpuDrivenVgAssets_.size() * 256u);
    for (uint32_t assetId = 0; assetId < gpuDrivenVgAssets_.size(); ++assetId) {
        auto &asset = gpuDrivenVgAssets_[assetId];
        if (!asset.active || asset.materialId >= gpuDrivenMaterials_.size()) continue;
        Material *material = gpuDrivenMaterials_[asset.materialId];
        VgDrawParams params{};
        params.mvp = gpuDrivenVgViewProj_;
        params.model = asset.model;
        params.tint = glm::vec4(material->getTintR(), material->getTintG(),
                                material->getTintB(), material->getTintA());
        params.lightDir = glm::vec4(glm::vec3(mesh3dLighting.lights[0].posRadius), 0.f);
        params.lightColor = mesh3dLighting.lights[0].color;
        params.ambient = mesh3dLighting.ambient;
        params.ids.x = assetId;
        const uint32_t offset = arena.alloc(sizeof(params), 256);
        queue.WriteBuffer(arena.buffer, offset, &params, sizeof(params));
        WGPUBindGroupEntry entries[5]{};
        entries[0].binding = 0;
        entries[0].textureView = slot.visIDView.Get();
        entries[1].binding = 1;
        entries[1].textureView = slot.visBaryView.Get();
        entries[2].binding = 2;
        entries[2].buffer = asset.positions.Get();
        entries[2].size = uint64_t(asset.vertexCount) * 3u * sizeof(float);
        entries[3].binding = 3;
        entries[3].buffer = asset.triangles.Get();
        entries[3].size = uint64_t(asset.triangleCount) * sizeof(uint32_t);
        entries[4].binding = 4;
        entries[4].buffer = arena.buffer.Get();
        entries[4].offset = offset;
        entries[4].size = sizeof(params);
        WGPUBindGroupDescriptor bgd{};
        bgd.layout = gpuDrivenVgResolveSetLayout_.Get();
        bgd.entryCount = 5;
        bgd.entries = entries;
        wgpu::BindGroup group = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&bgd));
        pass.SetBindGroup(0, group, 0, nullptr);
        pass.Draw(3, 1, 0, 0);
        asset.active = false;
    }
    gpuDrivenVgResolvePending_ = false;
}

}  // namespace eve::graphics::webgpu
