#include "graphics/webgpu/Graphics.h"

#include "graphics/Material.h"
#include "graphics/RenderControl.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>

#include <glm/gtc/matrix_access.hpp>

namespace eve::graphics::webgpu {
namespace {

WGPUStringView label(const char *s) {
    WGPUStringView out{};
    out.data = s;
    out.length = WGPU_STRLEN;
    return out;
}

wgpu::ShaderModule shaderModule(wgpu::Device device, const char *source) {
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = label(source);
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl.chain;
    return device.CreateShaderModule(
        reinterpret_cast<const wgpu::ShaderModuleDescriptor *>(&desc));
}

wgpu::ShaderModule shaderModule(wgpu::Device device, const std::string &source) {
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data   = source.data();
    wgsl.code.length = source.size();
    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl.chain;
    return device.CreateShaderModule(reinterpret_cast<const wgpu::ShaderModuleDescriptor *>(&desc));
}

struct CullInput {
    glm::mat4 model{1.f};
    glm::vec4 bounds{0.f};
    uint32_t bucket = 0;
    uint32_t outputBase = 0;
    uint32_t pad[2]{};
};
static_assert(sizeof(CullInput) == 96);

struct CullParams {
    glm::mat4 viewProj{1.f};
    glm::vec4 planes[6]{};
    glm::vec4 cameraPos{};
    glm::vec4 screen{};
    glm::vec4 clipNearFar{};
    glm::vec4 hzbInfo{};
    glm::uvec4 counts{};
};
static_assert(sizeof(CullParams) == 240);

struct VisIndirectCommand {
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(VisIndirectCommand) == 16);

struct HzbBuildParams {
    glm::uvec4 info{};  // mip, width, height, previous-mip word offset
    glm::uvec4 source{};  // previous width, previous height
};
static_assert(sizeof(HzbBuildParams) == 32);

constexpr uint32_t kMaxHzbMips = 16;
constexpr uint32_t kHzbHeaderWords = 16;

constexpr const char *kHzbBuildWgsl = R"wgsl(
struct BuildParams { info: vec4u, source: vec4u };
@group(0) @binding(0) var<uniform> params: BuildParams;
@group(0) @binding(1) var sourceDepth: texture_depth_2d;
@group(0) @binding(2) var<storage, read_write> hzb: array<u32>;

fn readHzb(base: u32, width: u32, height: u32, p: vec2i) -> f32 {
    let q = clamp(p, vec2i(0), vec2i(i32(width) - 1, i32(height) - 1));
    return bitcast<f32>(hzb[base + u32(q.y) * width + u32(q.x)]);
}

@compute @workgroup_size(8, 8)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let mip = params.info.x;
    let width = params.info.y;
    let height = params.info.z;
    if (gid.x >= width || gid.y >= height) { return; }
    var depth: f32;
    if (mip == 0u) {
        depth = textureLoad(sourceDepth, vec2i(gid.xy), 0);
    } else {
        let previousWidth = params.source.x;
        let previousHeight = params.source.y;
        let previousBase = 16u + params.info.w;
        let p = vec2i(gid.xy * 2u);
        depth = max(readHzb(previousBase, previousWidth, previousHeight, p),
                max(readHzb(previousBase, previousWidth, previousHeight, p + vec2i(1, 0)),
                max(readHzb(previousBase, previousWidth, previousHeight, p + vec2i(0, 1)),
                    readHzb(previousBase, previousWidth, previousHeight, p + vec2i(1, 1)))));
    }
    let outputBase = 16u + hzb[mip];
    hzb[outputBase + gid.y * width + gid.x] = bitcast<u32>(depth);
}
)wgsl";

constexpr const char *kCullWgsl = R"wgsl(
struct CullInput {
    model: mat4x4f,
    bounds: vec4f,
    bucket: u32,
    outputBase: u32,
    pad1: u32,
    pad2: u32,
};
struct CullParams {
    viewProj: mat4x4f,
    planes: array<vec4f, 6>,
    cameraPos: vec4f,
    screen: vec4f,
    clipNearFar: vec4f,
    hzbInfo: vec4f,
    counts: vec4u,
};
struct IndirectCommand {
    indexCount: u32,
    instanceCount: atomic<u32>,
    firstIndex: u32,
    baseVertex: u32,
    firstInstance: u32,
};
struct VisIndirectCommand {
    vertexCount: u32,
    instanceCount: atomic<u32>,
    firstVertex: u32,
    firstInstance: u32,
};
@group(0) @binding(0) var<uniform> params: CullParams;
@group(0) @binding(1) var<storage, read> inputs: array<CullInput>;
@group(0) @binding(2) var<storage, read_write> visibleModels: array<mat4x4f>;
@group(0) @binding(3) var<storage, read_write> commands: array<IndirectCommand>;
@group(0) @binding(4) var<storage, read_write> visCommands: array<VisIndirectCommand>;
@group(0) @binding(5) var<storage, read> hzb: array<u32>;

fn remainsVisibleAgainstDepth(center: vec3f, radius: f32) -> bool {
    if (params.clipNearFar.w < 0.5) { return true; }
    let clip = params.viewProj * vec4f(center, 1.0);
    if (clip.w <= params.clipNearFar.x) { return true; }
    let ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z > 1.0) { return true; }

    let toEye = normalize(params.cameraPos.xyz - center);
    let frontClip = params.viewProj * vec4f(center + toEye * radius, 1.0);
    let frontZ = frontClip.z / max(frontClip.w, 1e-6);
    let viewDepth = max(length(center - params.cameraPos.xyz), 1e-4);
    let radiusPx = radius * params.clipNearFar.z / viewDepth;
    let diameterPx = max(radiusPx * 2.0, 1.0);
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
    // All conservative samples must contain geometry closer than the sphere.
    // The epsilon prevents the object's own previous-frame depth from culling it.
    return blocker >= 0.9999 || frontZ <= blocker + 0.002;
}

@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let index = gid.x;
    if (index >= params.counts.x) { return; }
    let input = inputs[index];
    let center = (input.model * vec4f(input.bounds.xyz, 1.0)).xyz;
    let scale = max(length(input.model[0].xyz),
                    max(length(input.model[1].xyz), length(input.model[2].xyz)));
    let radius = input.bounds.w * scale;
    for (var p = 0u; p < 6u; p = p + 1u) {
        let plane = params.planes[p];
        if (dot(plane.xyz, center) + plane.w < -radius) { return; }
    }
    if (!remainsVisibleAgainstDepth(center, radius)) { return; }
    let local = atomicAdd(&commands[input.bucket].instanceCount, 1u);
    atomicAdd(&visCommands[input.bucket].instanceCount, 1u);
    visibleModels[input.outputBase + local] = input.model;
}
)wgsl";

constexpr const char *kGpuDrivenVertWgsl = R"wgsl(
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
struct VSIn {
    @location(0) pos: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) vNormal: vec3f,
    @location(1) vUV: vec2f,
    @location(2) vTint: vec4f,
    @location(3) vWorldPos: vec3f,
    @location(4) vCameraPos: vec3f,
    @location(5) vViewPos: vec3f,
};
@group(0) @binding(0) var<uniform> ubo: Frame;
@group(1) @binding(0) var<storage, read> visibleModels: array<mat4x4f>;

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
fn vs_main(in: VSIn, @builtin(instance_index) instanceIndex: u32) -> VSOut {
    let model = visibleModels[instanceIndex];
    let world = model * vec4f(in.pos, 1.0);
    var out: VSOut;
    out.pos = ubo.mvp * world;
    out.pos.y = -out.pos.y;
    out.vWorldPos = world.xyz;
    out.vViewPos = (ubo.view * world).xyz;
    let normalMatrix = transpose(inverse3x3(mat3x3f(model[0].xyz, model[1].xyz,
                                                    model[2].xyz)));
    out.vNormal = normalize(normalMatrix * in.normal);
    out.vUV = in.uv;
    out.vTint = ubo.tint;
    out.vCameraPos = ubo.cameraPos.xyz;
    return out;
}
)wgsl";

uint64_t grownCapacity(uint64_t current, uint64_t required) {
    uint64_t value = std::max<uint64_t>(current, 256);
    while (value < required) value *= 2;
    return value;
}

}  // namespace

uint32_t Graphics::gpuDrivenMeshRecord(Mesh *mesh) {
    if (!mesh || !mesh->gpuHandle) return kInvalidGpuDrivenSlot;
    auto found = gpuDrivenMeshIds_.find(mesh);
    if (found != gpuDrivenMeshIds_.end()) return found->second;
    const uint32_t id = static_cast<uint32_t>(gpuDrivenMeshes_.size());
    gpuDrivenMeshes_.push_back(mesh);
    gpuDrivenMeshIds_.emplace(mesh, id);
    return id;
}

uint32_t Graphics::gpuDrivenMaterialRecord(Material *material) {
    if (!gpuDrivenMaterialUsable(material)) return kInvalidGpuDrivenSlot;
    auto found = gpuDrivenMaterialIds_.find(material);
    if (found != gpuDrivenMaterialIds_.end()) return found->second;
    const uint32_t id = static_cast<uint32_t>(gpuDrivenMaterials_.size());
    gpuDrivenMaterials_.push_back(material);
    gpuDrivenMaterialIds_.emplace(material, id);
    return id;
}

bool Graphics::gpuDrivenMaterialUsable(Material *material) {
    if (!material ||
        material->virtualTextureMode() == MaterialVirtualTextureMode::AtlasPageTable)
        return false;
    if (!material || material->surfaceMode() != SurfaceMode::Opaque ||
        material->effectiveShader() != nullptr || material->isTransparentHair())
        return false;
    return material->getShadingModel() == "pbr" && material->getReceiveLight();
}

bool Graphics::gpuDrivenSubmitOpaque(const GpuInstance *instances, uint32_t instanceCount) {
    if (!gpuDrivenEnabled_ || !instances || instanceCount == 0) return false;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        const GpuInstance &instance = instances[i];
        if (instance.meshId >= gpuDrivenMeshes_.size() ||
            instance.materialId >= gpuDrivenMaterials_.size())
            return false;
        Mesh *mesh = gpuDrivenMeshes_[instance.meshId];
        Material *material = gpuDrivenMaterials_[instance.materialId];
        if (!mesh || !material || !gpuDrivenMaterialUsable(material)) return false;
        material->bind(*this);
        drawMeshShader(mesh, instance.model, material->getAlbedoTexture(),
                       Color(material->getTintR(), material->getTintG(), material->getTintB(),
                             material->getTintA()),
                       nullptr);
    }
    return true;
}

GpuResidentSubmitStatus Graphics::gpuDrivenSubmitResident(const GpuResidentInstanceBatch &batch) {
    if (!gpuDrivenEnabled_ || !device || gpuDrivenComputePending_ || gpuDrivenDrawPending_)
        return GpuResidentSubmitStatus::ResourceUnavailable;
    if (batch.buffer.backend != GpuResidentBackend::WebGpu) return GpuResidentSubmitStatus::BackendMismatch;
    if (batch.buffer.nativeHandle == 0 || batch.buffer.offsetBytes % kGpuResidentStorageOffsetAlignment != 0 ||
        batch.buffer.strideBytes != sizeof(GpuInstance) || !batch.buckets || batch.bucketCount == 0 ||
        batch.instanceCount == 0)
        return GpuResidentSubmitStatus::InvalidArgument;
    const uint64_t required = batch.buffer.offsetBytes + uint64_t(batch.instanceCount) * sizeof(GpuInstance);
    if (required < batch.buffer.offsetBytes || required > batch.buffer.sizeBytes)
        return GpuResidentSubmitStatus::InvalidArgument;

    std::vector<GpuIndirectCommand> commands(batch.bucketCount);
    std::vector<GpuDrivenBucket>    buckets(batch.bucketCount);
    uint64_t                        coveredInstances = 0;
    for (uint32_t i = 0; i < batch.bucketCount; ++i) {
        const GpuResidentInstanceBucket &bucket = batch.buckets[i];
        const uint64_t                   end    = uint64_t(bucket.firstInstance) + bucket.instanceCount;
        if (bucket.instanceCount == 0 || bucket.firstInstance != coveredInstances || end > batch.instanceCount ||
            bucket.meshId >= gpuDrivenMeshes_.size() || bucket.materialId >= gpuDrivenMaterials_.size())
            return GpuResidentSubmitStatus::InvalidArgument;
        Mesh     *mesh     = gpuDrivenMeshes_[bucket.meshId];
        Material *material = gpuDrivenMaterials_[bucket.materialId];
        if (!mesh || !mesh->gpuHandle || !gpuDrivenMaterialUsable(material))
            return GpuResidentSubmitStatus::InvalidArgument;
        auto *gpu        = static_cast<GpuMesh *>(mesh->gpuHandle);
        commands[i]      = {gpu->indexCount, bucket.instanceCount, 0, 0, bucket.firstInstance};
        buckets[i]       = {mesh, material, bucket.firstInstance, bucket.instanceCount};
        coveredInstances = end;
    }
    if (coveredInstances != batch.instanceCount) return GpuResidentSubmitStatus::InvalidArgument;

    ensureGpuDrivenResources(batch.instanceCount, batch.bucketCount);
    if (!gpuDrivenResidentRenderPipeline_ || !gpuDrivenIndirectBuffer_)
        return GpuResidentSubmitStatus::ResourceUnavailable;
    queue.WriteBuffer(gpuDrivenIndirectBuffer_, 0, commands.data(), commands.size() * sizeof(GpuIndirectCommand));
    WGPUBuffer rawBuffer{};
    static_assert(sizeof(rawBuffer) <= sizeof(batch.buffer.nativeHandle));
    std::memcpy(&rawBuffer, &batch.buffer.nativeHandle, sizeof(rawBuffer));
    if (!rawBuffer) return GpuResidentSubmitStatus::ResourceUnavailable;
    gpuDrivenBuckets_             = std::move(buckets);
    gpuDrivenResidentBuffer_      = rawBuffer;
    gpuDrivenResidentOffset_      = batch.buffer.offsetBytes;
    gpuDrivenResidentSize_        = uint64_t(batch.instanceCount) * sizeof(GpuInstance);
    gpuDrivenResidentDrawPending_ = true;
    gpuDrivenDrawPending_         = true;
    frameHad3DThisFrame           = true;
    frameHad3D                    = true;
    return GpuResidentSubmitStatus::Submitted;
}

void Graphics::ensureGpuDrivenResources(uint32_t instanceCount, uint32_t bucketCount) {
    if (!gpuDrivenCullPipeline_) {
        WGPUBindGroupLayoutEntry computeEntries[6]{};
        computeEntries[0].binding = 0;
        computeEntries[0].visibility = WGPUShaderStage_Compute;
        computeEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        computeEntries[0].buffer.minBindingSize = sizeof(CullParams);
        for (uint32_t i = 1; i < 5; ++i) {
            computeEntries[i].binding = i;
            computeEntries[i].visibility = WGPUShaderStage_Compute;
            computeEntries[i].buffer.type = i == 1 ? WGPUBufferBindingType_ReadOnlyStorage
                                                    : WGPUBufferBindingType_Storage;
        }
        computeEntries[1].buffer.minBindingSize = sizeof(CullInput);
        computeEntries[2].buffer.minBindingSize = sizeof(glm::mat4);
        computeEntries[3].buffer.minBindingSize = sizeof(uint32_t) * 5;
        computeEntries[4].buffer.minBindingSize = sizeof(VisIndirectCommand);
        WGPUBindGroupLayoutDescriptor cbgl{};
        cbgl.label = label("eve_gpu_driven_compute_bgl");
        computeEntries[5].binding = 5;
        computeEntries[5].visibility = WGPUShaderStage_Compute;
        computeEntries[5].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        cbgl.entryCount = 6;
        cbgl.entries = computeEntries;
        gpuDrivenComputeSetLayout_ = device.CreateBindGroupLayout(
            reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&cbgl));

        WGPUBindGroupLayoutEntry renderEntry{};
        renderEntry.binding = 0;
        renderEntry.visibility = WGPUShaderStage_Vertex;
        renderEntry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        renderEntry.buffer.minBindingSize = sizeof(glm::mat4);
        WGPUBindGroupLayoutDescriptor rbgl{};
        rbgl.label = label("eve_gpu_driven_render_bgl");
        rbgl.entryCount = 1;
        rbgl.entries = &renderEntry;
        gpuDrivenRenderSetLayout_ = device.CreateBindGroupLayout(
            reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&rbgl));

        WGPUBindGroupLayout computeLayout = gpuDrivenComputeSetLayout_.Get();
        WGPUPipelineLayoutDescriptor cpl{};
        cpl.label = label("eve_gpu_driven_compute_layout");
        cpl.bindGroupLayoutCount = 1;
        cpl.bindGroupLayouts = &computeLayout;
        gpuDrivenComputePipelineLayout_ = device.CreatePipelineLayout(
            reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&cpl));

        WGPUBindGroupLayout renderLayouts[2] = {mesh3dSetLayout.Get(),
                                                gpuDrivenRenderSetLayout_.Get()};
        WGPUPipelineLayoutDescriptor rpl{};
        rpl.label = label("eve_gpu_driven_render_layout");
        rpl.bindGroupLayoutCount = 2;
        rpl.bindGroupLayouts = renderLayouts;
        gpuDrivenRenderPipelineLayout_ = device.CreatePipelineLayout(
            reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&rpl));

        wgpu::ShaderModule cullModule = shaderModule(device, kCullWgsl);
        WGPUComputePipelineDescriptor cpd{};
        cpd.label = label("eve_gpu_driven_cull");
        cpd.layout = gpuDrivenComputePipelineLayout_.Get();
        cpd.compute.module = cullModule.Get();
        cpd.compute.entryPoint = label("cs_main");
        gpuDrivenCullPipeline_ = device.CreateComputePipeline(
            reinterpret_cast<const wgpu::ComputePipelineDescriptor *>(&cpd));

        WGPUBindGroupLayoutEntry hzbEntries[3]{};
        hzbEntries[0].binding = 0;
        hzbEntries[0].visibility = WGPUShaderStage_Compute;
        hzbEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        hzbEntries[0].buffer.hasDynamicOffset = true;
        hzbEntries[0].buffer.minBindingSize = sizeof(HzbBuildParams);
        hzbEntries[1].binding = 1;
        hzbEntries[1].visibility = WGPUShaderStage_Compute;
        hzbEntries[1].texture.sampleType = WGPUTextureSampleType_Depth;
        hzbEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        hzbEntries[2].binding = 2;
        hzbEntries[2].visibility = WGPUShaderStage_Compute;
        hzbEntries[2].buffer.type = WGPUBufferBindingType_Storage;
        WGPUBindGroupLayoutDescriptor hzbBgl{};
        hzbBgl.label = label("eve_gpu_driven_hzb_bgl");
        hzbBgl.entryCount = 3;
        hzbBgl.entries = hzbEntries;
        gpuDrivenHzbSetLayout_ = device.CreateBindGroupLayout(
            reinterpret_cast<const wgpu::BindGroupLayoutDescriptor *>(&hzbBgl));
        WGPUBindGroupLayout hzbLayout = gpuDrivenHzbSetLayout_.Get();
        WGPUPipelineLayoutDescriptor hzbPl{};
        hzbPl.bindGroupLayoutCount = 1;
        hzbPl.bindGroupLayouts = &hzbLayout;
        gpuDrivenHzbPipelineLayout_ = device.CreatePipelineLayout(
            reinterpret_cast<const wgpu::PipelineLayoutDescriptor *>(&hzbPl));
        wgpu::ShaderModule hzbModule = shaderModule(device, kHzbBuildWgsl);
        WGPUComputePipelineDescriptor hzbPd{};
        hzbPd.label = label("eve_gpu_driven_hzb_build");
        hzbPd.layout = gpuDrivenHzbPipelineLayout_.Get();
        hzbPd.compute.module = hzbModule.Get();
        hzbPd.compute.entryPoint = label("cs_main");
        gpuDrivenHzbPipeline_ = device.CreateComputePipeline(
            reinterpret_cast<const wgpu::ComputePipelineDescriptor *>(&hzbPd));

        WGPUVertexAttribute attrs[3]{};
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
        vb.arrayStride = 32;
        vb.stepMode = WGPUVertexStepMode_Vertex;
        vb.attributeCount = 3;
        vb.attributes = attrs;
        WGPUDepthStencilState depth{};
        depth.format = WGPUTextureFormat_Depth32Float;
        depth.depthWriteEnabled = WGPUOptionalBool_True;
        depth.depthCompare = WGPUCompareFunction_Less;
        WGPUColorTargetState target{};
        target.format = sceneColorFormat;
        target.writeMask = WGPUColorWriteMask_All;
        wgpu::ShaderModule vertModule = shaderModule(device, kGpuDrivenVertWgsl);
        std::string        residentVertSource = kGpuDrivenVertWgsl;
        const std::string  modelsDecl = "@group(1) @binding(0) var<storage, read> visibleModels: array<mat4x4f>;";
        const std::string  residentDecl =
            "struct ResidentInstance { model: mat4x4f, meshId: u32, materialId: u32, "
            "flags: u32, lodGroupId: u32 };\n"
            "@group(1) @binding(0) var<storage, read> residentInstances: "
            "array<ResidentInstance>;";
        residentVertSource.replace(residentVertSource.find(modelsDecl), modelsDecl.size(), residentDecl);
        const std::string modelRead = "let model = visibleModels[instanceIndex];";
        residentVertSource.replace(residentVertSource.find(modelRead), modelRead.size(),
                                   "let model = residentInstances[instanceIndex].model;");
        wgpu::ShaderModule residentVertModule = shaderModule(device, residentVertSource);
        wgpu::ShaderModule fragModule = shaderModule(device, kMesh3DFragWgsl);
        WGPUFragmentState fs{};
        fs.module = fragModule.Get();
        fs.entryPoint = label("fs_main");
        fs.targetCount = 1;
        fs.targets = &target;
        WGPURenderPipelineDescriptor rpd{};
        rpd.label = label("eve_gpu_driven_render");
        rpd.layout = gpuDrivenRenderPipelineLayout_.Get();
        rpd.vertex.module = vertModule.Get();
        rpd.vertex.entryPoint = label("vs_main");
        rpd.vertex.bufferCount = 1;
        rpd.vertex.buffers = &vb;
        rpd.fragment = &fs;
        rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        rpd.primitive.frontFace = WGPUFrontFace_CW;
        rpd.primitive.cullMode = WGPUCullMode_None;
        rpd.depthStencil = &depth;
        rpd.multisample.count = sceneColorSamples;
        rpd.multisample.mask = 0xFFFFFFFFu;
        gpuDrivenRenderPipeline_ = device.CreateRenderPipeline(
            reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&rpd));
        rpd.label         = label("eve_gpu_driven_resident_render");
        rpd.vertex.module = residentVertModule.Get();
        gpuDrivenResidentRenderPipeline_ =
            device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&rpd));
        rpd.label = label("eve_gpu_driven_canvas");
        rpd.vertex.module        = vertModule.Get();
        rpd.multisample.count = 1;
        gpuDrivenCanvasPipeline_ = device.CreateRenderPipeline(
            reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&rpd));
        rpd.label         = label("eve_gpu_driven_resident_canvas");
        rpd.vertex.module = residentVertModule.Get();
        gpuDrivenResidentCanvasPipeline_ =
            device.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor *>(&rpd));

        WGPUBufferDescriptor pbd{};
        pbd.label = label("eve_gpu_driven_params");
        pbd.size = sizeof(CullParams);
        pbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
        gpuDrivenParamsBuffer_ = device.CreateBuffer(
            reinterpret_cast<const wgpu::BufferDescriptor *>(&pbd));
        pbd.label = label("eve_gpu_driven_hzb_params");
        pbd.size = kMaxHzbMips * 256u;
        gpuDrivenHzbParamsBuffer_ = device.CreateBuffer(
            reinterpret_cast<const wgpu::BufferDescriptor *>(&pbd));
    }

    bool recreateGroups = false;
    const uint32_t hzbWidth = std::max(gbufferWidth, 1);
    const uint32_t hzbHeight = std::max(gbufferHeight, 1);
    if (!gpuDrivenHzbBuffer_ || gpuDrivenHzbWidth_ != hzbWidth ||
        gpuDrivenHzbHeight_ != hzbHeight) {
        gpuDrivenHzbWidth_ = hzbWidth;
        gpuDrivenHzbHeight_ = hzbHeight;
        gpuDrivenHzbOffsets_.clear();
        uint32_t words = kHzbHeaderWords;
        for (uint32_t w = hzbWidth, h = hzbHeight;
             gpuDrivenHzbOffsets_.size() < kMaxHzbMips; w = std::max(w >> 1u, 1u),
                      h = std::max(h >> 1u, 1u)) {
            gpuDrivenHzbOffsets_.push_back(words - kHzbHeaderWords);
            words += w * h;
            if (w == 1 && h == 1) break;
        }
        WGPUBufferDescriptor hzbBd{};
        hzbBd.label = label("eve_gpu_driven_hzb");
        hzbBd.size = uint64_t(words) * sizeof(uint32_t);
        gpuDrivenHzbCapacity_ = hzbBd.size;
        hzbBd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
        gpuDrivenHzbBuffer_ = device.CreateBuffer(
            reinterpret_cast<const wgpu::BufferDescriptor *>(&hzbBd));
        queue.WriteBuffer(gpuDrivenHzbBuffer_, 0, gpuDrivenHzbOffsets_.data(),
                          gpuDrivenHzbOffsets_.size() * sizeof(uint32_t));
        recreateGroups = true;
    }

    const uint64_t inputBytes = uint64_t(instanceCount) * sizeof(CullInput);
    if (!gpuDrivenInputBuffer_ || gpuDrivenInputCapacity_ < inputBytes) {
        gpuDrivenInputCapacity_ = grownCapacity(gpuDrivenInputCapacity_, inputBytes);
        WGPUBufferDescriptor bd{};
        bd.label = label("eve_gpu_driven_inputs");
        bd.size = gpuDrivenInputCapacity_;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
        gpuDrivenInputBuffer_ =
            device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
        recreateGroups = true;
    }
    const uint64_t visibleBytes = uint64_t(instanceCount) * sizeof(glm::mat4);
    if (!gpuDrivenVisibleBuffer_ || gpuDrivenVisibleCapacity_ < visibleBytes) {
        gpuDrivenVisibleCapacity_ = grownCapacity(gpuDrivenVisibleCapacity_, visibleBytes);
        WGPUBufferDescriptor bd{};
        bd.label = label("eve_gpu_driven_visible");
        bd.size = gpuDrivenVisibleCapacity_;
        bd.usage = WGPUBufferUsage_Storage;
        gpuDrivenVisibleBuffer_ =
            device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
        recreateGroups = true;
    }
    const uint64_t indirectBytes = uint64_t(bucketCount) * sizeof(GpuIndirectCommand);
    if (!gpuDrivenIndirectBuffer_ || gpuDrivenIndirectCapacity_ < indirectBytes) {
        gpuDrivenIndirectCapacity_ = grownCapacity(gpuDrivenIndirectCapacity_, indirectBytes);
        WGPUBufferDescriptor bd{};
        bd.label = label("eve_gpu_driven_indirect");
        bd.size = gpuDrivenIndirectCapacity_;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage |
                   WGPUBufferUsage_Indirect;
        gpuDrivenIndirectBuffer_ =
            device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
        recreateGroups = true;
    }
    const uint64_t visIndirectBytes = uint64_t(bucketCount) * sizeof(VisIndirectCommand);
    if (!gpuDrivenVisIndirectBuffer_ || gpuDrivenVisIndirectCapacity_ < visIndirectBytes) {
        gpuDrivenVisIndirectCapacity_ =
            grownCapacity(gpuDrivenVisIndirectCapacity_, visIndirectBytes);
        WGPUBufferDescriptor bd{};
        bd.label = label("eve_gpu_driven_vis_indirect");
        bd.size = gpuDrivenVisIndirectCapacity_;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect;
        gpuDrivenVisIndirectBuffer_ =
            device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
        recreateGroups = true;
    }
    // The previous-frame depth view rotates with the frame slot, so refresh
    // the compute group even when the storage-buffer capacities are unchanged.
    recreateGroups = true;
    if (recreateGroups || !gpuDrivenComputeBindGroup_) {
        WGPUBindGroupEntry entries[6]{};
        entries[0].binding = 0;
        entries[0].buffer = gpuDrivenParamsBuffer_.Get();
        entries[0].size = sizeof(CullParams);
        entries[1].binding = 1;
        entries[1].buffer = gpuDrivenInputBuffer_.Get();
        entries[1].size = gpuDrivenInputCapacity_;
        entries[2].binding = 2;
        entries[2].buffer = gpuDrivenVisibleBuffer_.Get();
        entries[2].size = gpuDrivenVisibleCapacity_;
        entries[3].binding = 3;
        entries[3].buffer = gpuDrivenIndirectBuffer_.Get();
        entries[3].size = gpuDrivenIndirectCapacity_;
        entries[4].binding = 4;
        entries[4].buffer = gpuDrivenVisIndirectBuffer_.Get();
        entries[4].size = gpuDrivenVisIndirectCapacity_;
        entries[5].binding = 5;
        const uint32_t depthSlot = gbufferDepthValid_ ? lastGbufferSlot : currentFrameSlot();
        entries[5].buffer = gpuDrivenHzbBuffer_.Get();
        entries[5].size = gpuDrivenHzbCapacity_;
        WGPUBindGroupDescriptor bgd{};
        bgd.label = label("eve_gpu_driven_compute_bg");
        bgd.layout = gpuDrivenComputeSetLayout_.Get();
        bgd.entryCount = 6;
        bgd.entries = entries;
        gpuDrivenComputeBindGroup_ = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&bgd));

        WGPUBindGroupEntry renderEntry{};
        renderEntry.binding = 0;
        renderEntry.buffer = gpuDrivenVisibleBuffer_.Get();
        renderEntry.size = gpuDrivenVisibleCapacity_;
        WGPUBindGroupDescriptor rbgd{};
        rbgd.label = label("eve_gpu_driven_render_bg");
        rbgd.layout = gpuDrivenRenderSetLayout_.Get();
        rbgd.entryCount = 1;
        rbgd.entries = &renderEntry;
        gpuDrivenRenderBindGroup_ = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&rbgd));

        WGPUBindGroupEntry hzbEntries[3]{};
        hzbEntries[0].binding = 0;
        hzbEntries[0].buffer = gpuDrivenHzbParamsBuffer_.Get();
        hzbEntries[0].size = sizeof(HzbBuildParams);
        hzbEntries[1].binding = 1;
        hzbEntries[1].textureView = gbufferSlots[depthSlot].depthView.Get();
        hzbEntries[2].binding = 2;
        hzbEntries[2].buffer = gpuDrivenHzbBuffer_.Get();
        hzbEntries[2].size = gpuDrivenHzbCapacity_;
        WGPUBindGroupDescriptor hzbBg{};
        hzbBg.label = label("eve_gpu_driven_hzb_bg");
        hzbBg.layout = gpuDrivenHzbSetLayout_.Get();
        hzbBg.entryCount = 3;
        hzbBg.entries = hzbEntries;
        gpuDrivenHzbBindGroup_ = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&hzbBg));
    }
}

bool Graphics::gpuDrivenCullBegin(const GpuInstance *instances, uint32_t instanceCount) {
    if (!gpuDrivenEnabled_ || !instances || instanceCount == 0) return false;
    for (uint32_t i = 0; i < instanceCount; ++i) {
        if (instances[i].meshId >= gpuDrivenMeshes_.size()) return false;
        auto *gpu = static_cast<GpuMesh *>(gpuDrivenMeshes_[instances[i].meshId]->gpuHandle);
        if (!gpu || !gpu->indexBuffer) return false;
    }
    gpuDrivenPending_.assign(instances, instances + instanceCount);
    gpuDrivenVisible_.clear();
    gpuDrivenBuckets_.clear();
    gpuDrivenComputePending_ = false;
    gpuDrivenDrawPending_ = false;
    return true;
}

void Graphics::gpuDrivenCullEmit(const glm::mat4 &viewProj, const glm::vec3 &eye, float fovYDeg,
                                 float nearZ, float farZ) {
    if (sceneColorWidth > 0 && sceneColorHeight > 0)
        createSceneColorResources(sceneColorWidth, sceneColorHeight);
    if (gbufferSlots.empty() && sceneColorWidth > 0 && sceneColorHeight > 0)
        createGbufferResources(sceneColorWidth, sceneColorHeight);
    CullParams params{};
    params.viewProj = viewProj;
    params.planes[0] = glm::row(viewProj, 3) + glm::row(viewProj, 0);
    params.planes[1] = glm::row(viewProj, 3) - glm::row(viewProj, 0);
    params.planes[2] = glm::row(viewProj, 3) + glm::row(viewProj, 1);
    params.planes[3] = glm::row(viewProj, 3) - glm::row(viewProj, 1);
    params.planes[4] = glm::row(viewProj, 2);
    params.planes[5] = glm::row(viewProj, 3) - glm::row(viewProj, 2);
    for (glm::vec4 &plane : params.planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1e-6f) plane /= length;
    }
    params.cameraPos = glm::vec4(eye, 1.f);
    params.screen = glm::vec4(float(gbufferWidth), float(gbufferHeight),
                              gbufferWidth > 0 ? 1.f / float(gbufferWidth) : 0.f,
                              gbufferHeight > 0 ? 1.f / float(gbufferHeight) : 0.f);
    const float projScaleY = float(gbufferHeight) /
                             (2.f * std::tan(glm::radians(fovYDeg) * 0.5f));
    params.clipNearFar = glm::vec4(nearZ, farZ, projScaleY,
                                   gbufferDepthValid_ ? 1.f : 0.f);
    params.hzbInfo = glm::vec4(float(gpuDrivenHzbOffsets_.size() - 1u), 0.f,
                               float(gpuDrivenHzbWidth_), float(gpuDrivenHzbHeight_));
    uint32_t mipWidth = gpuDrivenHzbWidth_;
    uint32_t mipHeight = gpuDrivenHzbHeight_;
    uint32_t previousWidth = mipWidth;
    uint32_t previousHeight = mipHeight;
    for (uint32_t mip = 0; mip < gpuDrivenHzbOffsets_.size(); ++mip) {
        HzbBuildParams build{};
        build.info = glm::uvec4(mip, mipWidth, mipHeight,
                                mip == 0 ? 0u : gpuDrivenHzbOffsets_[mip - 1]);
        build.source = glm::uvec4(previousWidth, previousHeight, 0u, 0u);
        queue.WriteBuffer(gpuDrivenHzbParamsBuffer_, uint64_t(mip) * 256u, &build,
                          sizeof(build));
        previousWidth = mipWidth;
        previousHeight = mipHeight;
        mipWidth = std::max(mipWidth >> 1u, 1u);
        mipHeight = std::max(mipHeight >> 1u, 1u);
    }

    using BucketKey = std::pair<uint32_t, uint32_t>;
    std::map<BucketKey, std::vector<const GpuInstance *>> grouped;
    for (const GpuInstance &instance : gpuDrivenPending_)
        grouped[{instance.meshId, instance.materialId}].push_back(&instance);

    std::vector<CullInput> inputs;
    std::vector<GpuIndirectCommand> commands;
    std::vector<VisIndirectCommand> visCommands;
    inputs.reserve(gpuDrivenPending_.size());
    commands.reserve(grouped.size());
    visCommands.reserve(grouped.size());
    uint32_t outputBase = 0;
    for (const auto &[key, bucketInstances] : grouped) {
        Mesh *mesh = gpuDrivenMeshes_[key.first];
        Material *material = gpuDrivenMaterials_[key.second];
        auto *gpu = static_cast<GpuMesh *>(mesh->gpuHandle);
        const uint32_t bucketIndex = static_cast<uint32_t>(gpuDrivenBuckets_.size());
        gpuDrivenBuckets_.push_back(
            {mesh, material, outputBase, static_cast<uint32_t>(bucketInstances.size())});
        GpuIndirectCommand command{};
        command.indexCount = gpu->indexCount;
        // Keep firstInstance zero: it is optional in WebGPU and requires the
        // IndirectFirstInstance feature on adapters that expose it. The
        // bucket's compacted model range is selected through the storage
        // binding offset when drawing.
        command.firstInstance = 0;
        commands.push_back(command);
        VisIndirectCommand visCommand{};
        visCommand.vertexCount = gpu->indexCount;
        visCommand.firstInstance = 0;
        visCommands.push_back(visCommand);
        for (const GpuInstance *instance : bucketInstances) {
            CullInput input{};
            input.model = instance->model;
            input.bounds = mesh->hasBounds()
                               ? glm::vec4(mesh->boundsCx, mesh->boundsCy, mesh->boundsCz,
                                           mesh->boundsRadius)
                               : glm::vec4(0.f, 0.f, 0.f, 1e20f);
            input.bucket = bucketIndex;
            input.outputBase = outputBase;
            inputs.push_back(input);

            const glm::vec3 center = glm::vec3(
                input.model * glm::vec4(glm::vec3(input.bounds), 1.f));
            const float scale = std::max({glm::length(glm::vec3(input.model[0])),
                                          glm::length(glm::vec3(input.model[1])),
                                          glm::length(glm::vec3(input.model[2]))});
            bool visible = true;
            for (const glm::vec4 &plane : params.planes) {
                if (glm::dot(glm::vec3(plane), center) + plane.w < -input.bounds.w * scale) {
                    visible = false;
                    break;
                }
            }
            if (visible) gpuDrivenVisible_.push_back(*instance);
        }
        // Storage-buffer binding offsets are aligned to WebGPU's common
        // 256-byte limit (four mat4 values), so every bucket can bind its
        // compacted range without relying on indirect firstInstance.
        outputBase +=
            (static_cast<uint32_t>(bucketInstances.size()) + 3u) & ~uint32_t(3u);
    }

    ensureGpuDrivenResources(outputBase,
                             static_cast<uint32_t>(commands.size()));
    params.counts.x = static_cast<uint32_t>(inputs.size());
    queue.WriteBuffer(gpuDrivenParamsBuffer_, 0, &params, sizeof(params));
    queue.WriteBuffer(gpuDrivenInputBuffer_, 0, inputs.data(), inputs.size() * sizeof(CullInput));
    queue.WriteBuffer(gpuDrivenIndirectBuffer_, 0, commands.data(),
                      commands.size() * sizeof(GpuIndirectCommand));
    queue.WriteBuffer(gpuDrivenVisIndirectBuffer_, 0, visCommands.data(),
                      visCommands.size() * sizeof(VisIndirectCommand));
    gpuDrivenDispatchCount_ = static_cast<uint32_t>(inputs.size());
    gpuDrivenLastBucketCount_ = static_cast<uint32_t>(commands.size());
    gpuDrivenComputePending_ = !inputs.empty() && !commands.empty();
}

uint32_t Graphics::debugGpuDrivenGpuVisibleCount() {
#if defined(__EMSCRIPTEN__)
    return static_cast<uint32_t>(gpuDrivenVisible_.size());
#else
    if (!gpuDrivenIndirectBuffer_ || gpuDrivenLastBucketCount_ == 0) return 0;
    const uint64_t size = uint64_t(gpuDrivenLastBucketCount_) * sizeof(GpuIndirectCommand);
    WGPUBufferDescriptor bd{};
    bd.label = label("eve_gpu_driven_debug_readback");
    bd.size = size;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    wgpu::Buffer dst =
        device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor *>(&bd));
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(gpuDrivenIndirectBuffer_, 0, dst, 0, size);
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
    WGPUFuture future = wgpuBufferMapAsync(dst.Get(), WGPUMapMode_Read, 0, size, callback);
    WGPUFutureWaitInfo wait{};
    wait.future = future;
    (void)wgpuInstanceWaitAny(instance.Get(), 1, &wait, UINT64_MAX);
    if (!state.ok) return 0;
    const auto *commands =
        static_cast<const GpuIndirectCommand *>(dst.GetConstMappedRange(0, size));
    uint32_t visible = 0;
    if (commands) {
        for (uint32_t i = 0; i < gpuDrivenLastBucketCount_; ++i)
            visible += commands[i].instanceCount;
    }
    dst.Unmap();
    return visible;
#endif
}

void Graphics::gpuDrivenDrawOpaque() {
    if (!gpuDrivenComputePending_ || gpuDrivenBuckets_.empty()) return;
    frameHad3DThisFrame = true;
    frameHad3D = true;
    gpuDrivenDrawPending_ = true;
    gpuDrivenPending_.clear();
}

void Graphics::recordGpuDrivenCompute(wgpu::CommandEncoder encoder) {
    if ((gpuDrivenComputePending_ || gpuDrivenVgComputePending_) && gbufferDepthValid_ &&
        gpuDrivenHzbPipeline_ && gpuDrivenHzbBindGroup_) {
        uint32_t width = gpuDrivenHzbWidth_;
        uint32_t height = gpuDrivenHzbHeight_;
        for (uint32_t mip = 0; mip < gpuDrivenHzbOffsets_.size(); ++mip) {
            wgpu::ComputePassEncoder hzbPass = encoder.BeginComputePass();
            hzbPass.SetPipeline(gpuDrivenHzbPipeline_);
            const uint32_t dynamicOffset = mip * 256u;
            hzbPass.SetBindGroup(0, gpuDrivenHzbBindGroup_, 1, &dynamicOffset);
            hzbPass.DispatchWorkgroups((width + 7u) / 8u, (height + 7u) / 8u, 1);
            hzbPass.End();
            width = std::max(width >> 1u, 1u);
            height = std::max(height >> 1u, 1u);
        }
    }
    if (gpuDrivenComputePending_ && gpuDrivenCullPipeline_ && gpuDrivenComputeBindGroup_) {
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(gpuDrivenCullPipeline_);
        pass.SetBindGroup(0, gpuDrivenComputeBindGroup_, 0, nullptr);
        pass.DispatchWorkgroups((gpuDrivenDispatchCount_ + 63u) / 64u, 1, 1);
        pass.End();
        gpuDrivenComputePending_ = false;
    }
    recordGpuDrivenVgCompute(encoder);
}

void Graphics::flushGpuDrivenDraws(wgpu::RenderPassEncoder pass, bool canvasTarget) {
    if (!gpuDrivenDrawPending_ ||
        (gpuDrivenResidentDrawPending_ ? !gpuDrivenResidentBuffer_ : !gpuDrivenVisibleBuffer_))
        return;
    auto &arena = currentUboArena();
    ensureUboArena(arena, arena.used + gpuDrivenBuckets_.size() * 2048);
    pass.SetPipeline(gpuDrivenResidentDrawPending_
                         ? (canvasTarget ? gpuDrivenResidentCanvasPipeline_ : gpuDrivenResidentRenderPipeline_)
                         : (canvasTarget ? gpuDrivenCanvasPipeline_ : gpuDrivenRenderPipeline_));
    gpuDrivenLastIndirectDrawCount_ = 0;

    for (uint32_t i = 0; i < gpuDrivenBuckets_.size(); ++i) {
        const GpuDrivenBucket &bucket = gpuDrivenBuckets_[i];
        auto *gpu = static_cast<GpuMesh *>(bucket.mesh->gpuHandle);
        Material *material = bucket.material;
        if (!gpu || !gpu->vertexBuffer || !gpu->indexBuffer || !material) continue;

        Mesh3DUBO ubo{};
        ubo.mvp = mesh3dViewProj;
        ubo.model = glm::mat4(1.f);
        ubo.lightDir = glm::vec4(glm::vec3(mesh3dLighting.lights[0].posRadius),
                                 float(mesh3dLighting.count));
        ubo.lightColor = mesh3dLighting.lights[0].color;
        ubo.lightColor.w = mesh3dEnvIntensity;
        ubo.tint = glm::vec4(material->getTintR(), material->getTintG(), material->getTintB(),
                             material->getTintA());
        ubo.cameraPos = glm::vec4(mesh3dCameraPos, material->getRoughness());
        ubo.ambient = glm::vec4(glm::vec3(mesh3dLighting.ambient), material->getMetallic());
        for (int light = 0; light < Lighting3DPack::kMaxLights; ++light)
            ubo.lights[light] = mesh3dLighting.lights[light];
        ubo.texBomb = glm::vec4(material->getTexCellBombScale(),
                                material->getTexCellBombStrength(),
                                material->getTexCellBombRotation(), 0.f);
        ubo.parallax = glm::vec4(material->getParallaxScale(), material->getParallaxMinLayers(),
                                 material->getParallaxMaxLayers(), 0.f);
        const float ao = renderControl_ && renderControl_->isEnabled("ao")
                             ? mesh3dSsaoIntensity
                             : 0.f;
        ubo.surface = glm::vec4(0.f, material->getAlphaCutoff(), ao, 0.f);
        ubo.view = mesh3dView;
        ubo.clipInfo = glm::vec4(mesh3dNear, mesh3dFar, 0.f, 0.f);
        ubo.cloud = mesh3dCloud;
        ubo.cloudWind = mesh3dCloudWind;
        ubo.envProbeCenter = glm::vec4(mesh3dEnvProbeCenter, 1.f);
        ubo.envProbeExtent = glm::vec4(mesh3dEnvProbeExtent, 0.f);
        for (int probeIndex = 0; probeIndex < ReflectionProbeUpload::kMaxProbes; ++probeIndex) {
            if (probeIndex >= mesh3dReflectionProbes.count) continue;
            const auto &probe = mesh3dReflectionProbes.probes[probeIndex];
            GpuTexture *gpuProbe = gpuForTexture(probe.cubemap);
            if (!gpuProbe || !gpuProbe->isCube) continue;
            ubo.reflectionProbeCenter[probeIndex] = glm::vec4(probe.center, probe.intensity);
            ubo.reflectionProbeExtent[probeIndex] =
                glm::vec4(probe.extent, probe.blendDistance);
        }

        const uint32_t frameOffset = arena.alloc(sizeof(Mesh3DUBO), 256);
        const uint32_t shadowOffset = arena.alloc(sizeof(ShadowUBO), 256);
        queue.WriteBuffer(arena.buffer, frameOffset, &ubo, sizeof(ubo));
        ShadowUBO shadow = mesh3dShadows.ubo;
        if (!mesh3dShadows.active || !material->getReceiveShadow()) shadow.bias.y = 0.f;
        queue.WriteBuffer(arena.buffer, shadowOffset, &shadow, sizeof(shadow));

        GpuTexture *depth = mesh3dSceneDepthTexture ? gpuForTexture(mesh3dSceneDepthTexture)
                                                    : flatDepthTexture3D;
        wgpu::BindGroup bindGroup =
            makeMeshBindGroup(gpuForTexture(material->getAlbedoTexture()),
                              gpuForTexture(material->getNormalTexture()),
                              gpuForTexture(mesh3dEnvTexture),
                              gpuForTexture(material->getHeightTexture()), depth, frameOffset,
                              shadowOffset, 0);
        const uint32_t offsets[3] = {frameOffset, shadowOffset, 0};
        pass.SetBindGroup(0, bindGroup, 3, offsets);
        WGPUBindGroupEntry modelEntry{};
        modelEntry.binding = 0;
        modelEntry.buffer  = gpuDrivenResidentDrawPending_ ? gpuDrivenResidentBuffer_ : gpuDrivenVisibleBuffer_.Get();
        modelEntry.offset =
            gpuDrivenResidentDrawPending_ ? gpuDrivenResidentOffset_ : uint64_t(bucket.outputBase) * sizeof(glm::mat4);
        modelEntry.size =
            gpuDrivenResidentDrawPending_ ? gpuDrivenResidentSize_ : uint64_t(bucket.inputCount) * sizeof(glm::mat4);
        WGPUBindGroupDescriptor modelDesc{};
        modelDesc.layout = gpuDrivenRenderSetLayout_.Get();
        modelDesc.entryCount = 1;
        modelDesc.entries = &modelEntry;
        wgpu::BindGroup modelGroup = device.CreateBindGroup(
            reinterpret_cast<const wgpu::BindGroupDescriptor *>(&modelDesc));
        pass.SetBindGroup(1, modelGroup, 0, nullptr);
        pass.SetVertexBuffer(0, gpu->vertexBuffer, 0, gpu->vertexCount * 32ull);
        const uint64_t indexBytes = gpu->indexFormat == wgpu::IndexFormat::Uint16 ? 2u : 4u;
        pass.SetIndexBuffer(gpu->indexBuffer, gpu->indexFormat, 0,
                            uint64_t(gpu->indexCount) * indexBytes);
        pass.DrawIndexedIndirect(gpuDrivenIndirectBuffer_,
                                 uint64_t(i) * sizeof(GpuIndirectCommand));
        ++gpuDrivenLastIndirectDrawCount_;
    }
    gpuDrivenDrawPending_ = false;
    gpuDrivenResidentDrawPending_ = false;
    gpuDrivenResidentBuffer_      = nullptr;
    gpuDrivenResidentOffset_      = 0;
    gpuDrivenResidentSize_        = 0;
    gpuDrivenBuckets_.clear();
}

}  // namespace eve::graphics::webgpu
