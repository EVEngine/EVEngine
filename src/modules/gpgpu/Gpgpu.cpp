#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeProgram.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/EcsScriptPack.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "gpgpu/ShaderSystem.h"

#include "common/Exception.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "filesystem/Filesystem.h"
#include "graphics/GpuDrivenTypes.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"

#ifdef EVENGINE_WEBGPU
#include "gpgpu/webgpu/WebGpuGpgpu.h"
#else
#include "gpgpu/vulkan/VulkanGpgpu.h"
#include "gpgpu/vulkan/VulkanUtil.h"
#endif

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace eve::gpgpu {
namespace {

std::string currentGraphicsBackend() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    if (!gfx) return {};
    return gfx->getBackendName();
}

eve::graphics::Graphics *currentGraphics() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    return gfx;
}

std::string residentSubmitStatusName(eve::graphics::GpuResidentSubmitStatus status) {
    using Status = eve::graphics::GpuResidentSubmitStatus;
    switch (status) {
        case Status::Submitted: return "submitted";
        case Status::Unsupported: return "unsupported";
        case Status::InvalidArgument: return "invalid_argument";
        case Status::BackendMismatch: return "backend_mismatch";
        case Status::ResourceUnavailable: return "resource_unavailable";
        case Status::CapacityExceeded: return "capacity_exceeded";
    }
    return "unsupported";
}

int scriptGpuDrivenSlot(uint32_t slot) {
    if (slot == eve::graphics::kInvalidGpuDrivenSlot || slot > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        return -1;
    return static_cast<int>(slot);
}

std::vector<eve::graphics::GpuResidentInstanceBucket> scriptResidentBuckets(ssq::Array buckets) {
    std::vector<eve::graphics::GpuResidentInstanceBucket> result;
    result.reserve(buckets.size());
    for (std::size_t i = 0; i < buckets.size(); ++i) {
        ssq::Table bucket   = buckets.get<ssq::Table>(i);
        const int  first    = bucket.get<int>("firstInstance");
        const int  count    = bucket.get<int>("instanceCount");
        const int  mesh     = bucket.get<int>("meshId");
        const int  material = bucket.get<int>("materialId");
        if (first < 0 || count <= 0 || mesh < 0 || material < 0)
            throw Exception("Gpgpu.submitResidentInstances: bucket %d has invalid fields", static_cast<int>(i));
        result.push_back({static_cast<uint32_t>(first), static_cast<uint32_t>(count), static_cast<uint32_t>(mesh),
                          static_cast<uint32_t>(material)});
    }
    return result;
}

std::string submitResidentInstances(GpuBuffer *buffer, ssq::Array buckets, int instanceCount, int offsetBytes) {
    if (!buffer || instanceCount <= 0 || offsetBytes < 0 ||
        offsetBytes % static_cast<int>(eve::kGpuResidentStorageOffsetAlignment) != 0)
        return "invalid_argument";
    auto *gfx = currentGraphics();
    if (!gfx) return "resource_unavailable";
    const auto                              nativeBuckets = scriptResidentBuckets(buckets);
    eve::graphics::GpuResidentInstanceBatch batch;
    batch.buffer             = buffer->residentView();
    batch.buffer.offsetBytes = static_cast<uint64_t>(offsetBytes);
    batch.buffer.strideBytes = sizeof(eve::graphics::GpuInstance);
    batch.buckets            = nativeBuckets.data();
    batch.bucketCount        = static_cast<uint32_t>(nativeBuckets.size());
    batch.instanceCount      = static_cast<uint32_t>(instanceCount);
    return residentSubmitStatusName(gfx->gpuDrivenSubmitResident(batch));
}

void writeGpuDrivenInstance(GpuBuffer *buffer, int instanceIndex, ssq::Array model, int meshId, int materialId,
                            int flags, int lodGroupId) {
    if (!buffer || instanceIndex < 0 || model.size() != 16 || meshId < 0 || materialId < 0 || flags < 0 ||
        lodGroupId < -1)
        throw Exception("Gpgpu.writeGpuDrivenInstance: invalid argument");
    eve::graphics::GpuInstance instance;
    float                     *matrix = &instance.model[0][0];
    for (std::size_t i = 0; i < 16; ++i) matrix[i] = model.get<float>(i);
    instance.meshId       = static_cast<uint32_t>(meshId);
    instance.materialId   = static_cast<uint32_t>(materialId);
    instance.flags        = static_cast<uint32_t>(flags);
    instance.lodGroupId   = lodGroupId < 0 ? eve::graphics::kInvalidGpuDrivenSlot : static_cast<uint32_t>(lodGroupId);
    const uint64_t offset = static_cast<uint64_t>(instanceIndex) * sizeof(instance);
    if (offset + sizeof(instance) > static_cast<uint64_t>(buffer->getSize()))
        throw Exception("Gpgpu.writeGpuDrivenInstance: instance exceeds buffer capacity");
    buffer->uploadBytes(&instance, sizeof(instance), offset);
}

void setGpuDrivenEnabledScript(Gpgpu *gpgpu, bool enabled) {
    (void)gpgpu;
    if (auto *gfx = currentGraphics()) gfx->gpuDrivenSetEnabled(enabled);
}

bool isGpuDrivenEnabledScript(Gpgpu *gpgpu) {
    (void)gpgpu;
    auto *gfx = currentGraphics();
    return gfx && gfx->gpuDrivenEnabled();
}

int gpuDrivenMeshRecordScript(Gpgpu *gpgpu, eve::graphics::Mesh *mesh) {
    (void)gpgpu;
    auto *gfx = currentGraphics();
    return scriptGpuDrivenSlot(gfx ? gfx->gpuDrivenMeshRecord(mesh) : eve::graphics::kInvalidGpuDrivenSlot);
}

int gpuDrivenMaterialRecordScript(Gpgpu *gpgpu, eve::graphics::Material *material) {
    (void)gpgpu;
    auto *gfx = currentGraphics();
    return scriptGpuDrivenSlot(gfx ? gfx->gpuDrivenMaterialRecord(material) : eve::graphics::kInvalidGpuDrivenSlot);
}

bool gpuDrivenMaterialUsableScript(Gpgpu *gpgpu, eve::graphics::Material *material) {
    (void)gpgpu;
    auto *gfx = currentGraphics();
    return gfx && gfx->gpuDrivenMaterialUsable(material);
}

int getGpuDrivenInstanceStrideScript(Gpgpu *gpgpu) {
    (void)gpgpu;
    return sizeof(eve::graphics::GpuInstance);
}

int getGpuResidentOffsetAlignmentScript(Gpgpu *gpgpu) {
    (void)gpgpu;
    return eve::kGpuResidentStorageOffsetAlignment;
}

std::string submitSequenceAsyncScript(Sequence *sequence) {
    if (!sequence) return "failed";
    (void)sequence->submitAsync();
    return sequence->getStatusName();
}

std::string pollSequenceScript(Sequence *sequence) {
    if (!sequence) return "failed";
    (void)sequence->poll();
    return sequence->getStatusName();
}

std::string waitSequenceScript(Sequence *sequence) {
    if (!sequence) return "failed";
    (void)sequence->wait();
    return sequence->getStatusName();
}

void setShaderScript(ShaderSystem *system, ComputeShader *shader) {
    if (system) system->setShader(shader, false);
}

void dispatchShaderSystemScript(ShaderSystem *system, int entityCount, float deltaTime) {
    if (system) system->dispatch(entityCount, deltaTime);
}

void writeGpuDrivenInstanceScript(Gpgpu *gpgpu, GpuBuffer *buffer, int instanceIndex, ssq::Array model, int meshId,
                                  int materialId, int flags, int lodGroupId) {
    (void)gpgpu;
    writeGpuDrivenInstance(buffer, instanceIndex, model, meshId, materialId, flags, lodGroupId);
}

std::string submitResidentInstancesScript(Gpgpu *gpgpu, GpuBuffer *buffer, ssq::Array buckets, int instanceCount,
                                          int offsetBytes) {
    (void)gpgpu;
    return submitResidentInstances(buffer, buckets, instanceCount, offsetBytes);
}

}  // namespace

Module_IMPL(Gpgpu, new Gpgpu());

Gpgpu::Gpgpu() { eve::cap::provide<IMeshDeformationCompute>(this); }

Gpgpu::~Gpgpu() { eve::cap::revoke<IMeshDeformationCompute>(this); }

Result<std::vector<float>> Gpgpu::deform(MeshDeformationComputeRequest request) {
    const std::size_t count = request.positions.size() / 3u;
    if (!isAvailable())
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "mesh deformation compute backend is unavailable"));
    if (count == 0 || request.positions.size() != count * 3u || request.normals.size() != count * 3u ||
        (!request.baseline.empty() && request.baseline.size() != count * 3u) ||
        (!request.targets.empty() && request.targets.size() != count * 3u) || count > 4'000'000u)
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid mesh deformation compute buffers"));

    std::vector<float> positions(count * 4u), normals(count * 4u), baseline(count * 4u), targets(count * 4u);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t c = 0; c < 3u; ++c) {
            positions[i * 4u + c] = request.positions[i * 3u + c];
            normals[i * 4u + c]   = request.normals[i * 3u + c];
            baseline[i * 4u + c]  = request.baseline.empty() ? positions[i * 4u + c] : request.baseline[i * 3u + c];
            targets[i * 4u + c]   = request.targets.empty() ? positions[i * 4u + c] : request.targets[i * 3u + c];
        }
    }
    static constexpr const char* glsl = R"(#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0) buffer P{vec4 v[];} p;
layout(set=0,binding=1) readonly buffer N{vec4 v[];} n;
layout(set=0,binding=2) readonly buffer B{vec4 v[];} b;
layout(set=0,binding=3) readonly buffer T{vec4 v[];} t;
layout(push_constant) uniform PC{float d[32];} pc;
void main(){uint i=gl_GlobalInvocationID.x; if(i>=uint(pc.d[0]))return;
 vec3 q=p.v[i].xyz,c=vec3(pc.d[2],pc.d[3],pc.d[4]); float dist=distance(q,c);
 if(dist>=pc.d[5])return; float w=pow(1.0-dist/pc.d[5],pc.d[7])*pc.d[6]; int op=int(pc.d[1]);
 if(op==4){p.v[i].xyz=mix(q,t.v[i].xyz,clamp(abs(w),0.0,1.0));return;}
 if(op==2){p.v[i].y=mix(q.y,pc.d[3],clamp(abs(w),0.0,1.0));return;}
 vec3 dir=(op==3||op==5)?normalize(vec3(pc.d[8],pc.d[9],pc.d[10])):n.v[i].xyz;
 if(op==1)w=-w; vec3 outp=q+dir*w;
 if(op==5){vec3 delta=outp-b.v[i].xyz;float len=length(delta);if(len>pc.d[11])delta*=pc.d[11]/len;outp=b.v[i].xyz+delta;}
 p.v[i].xyz=outp;}
)";
    static constexpr const char* wgsl = R"(
struct V{v:array<vec4f>}; struct Push{d:array<vec4f,8>};
@group(0) @binding(0) var<storage,read_write> p:V; @group(0) @binding(1) var<storage,read> n:V;
@group(0) @binding(2) var<storage,read> b:V; @group(0) @binding(3) var<storage,read> t:V;
@group(0) @binding(8) var<uniform> pc:Push;
fn f(i:u32)->f32{return pc.d[i/4u][i%4u];}
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid:vec3u){let i=gid.x;if(i>=u32(f(0u))){return;}
 let q=p.v[i].xyz;let c=vec3f(f(2u),f(3u),f(4u));let dist=distance(q,c);if(dist>=f(5u)){return;}
 var w=pow(1.0-dist/f(5u),f(7u))*f(6u);let op=i32(f(1u));
 if(op==4){p.v[i]=vec4f(mix(q,t.v[i].xyz,clamp(abs(w),0.0,1.0)),0.0);return;}
 if(op==2){p.v[i]=vec4f(q.x,mix(q.y,f(3u),clamp(abs(w),0.0,1.0)),q.z,0.0);return;}
 var dir=n.v[i].xyz;if(op==3||op==5){dir=normalize(vec3f(f(8u),f(9u),f(10u)));}if(op==1){w=-w;}
 var outp=q+dir*w;if(op==5){var delta=outp-b.v[i].xyz;let len=length(delta);if(len>f(11u)){delta*=f(11u)/len;}outp=b.v[i].xyz+delta;}
 p.v[i]=vec4f(outp,0.0);}
)";
    try {
        std::unique_ptr<GpuBuffer> p(newBuffer(static_cast<int>(positions.size() * sizeof(float)), "storage"));
        std::unique_ptr<GpuBuffer> n(newBuffer(static_cast<int>(normals.size() * sizeof(float)), "storage"));
        std::unique_ptr<GpuBuffer> b(newBuffer(static_cast<int>(baseline.size() * sizeof(float)), "storage"));
        std::unique_ptr<GpuBuffer> t(newBuffer(static_cast<int>(targets.size() * sizeof(float)), "storage"));
        p->writeFloat32s(positions.data(), static_cast<int>(positions.size()));
        n->writeFloat32s(normals.data(), static_cast<int>(normals.size()));
        b->writeFloat32s(baseline.data(), static_cast<int>(baseline.size()));
        t->writeFloat32s(targets.data(), static_cast<int>(targets.size()));
        std::unique_ptr<ComputeShader> shader(newShader(currentGraphicsBackend() == "webgpu" ? wgsl : glsl));
        shader->bindBuffer(0, p.get()); shader->bindBuffer(1, n.get()); shader->bindBuffer(2, b.get()); shader->bindBuffer(3, t.get());
        shader->setFloat(0, static_cast<float>(count));
        shader->setFloat(1, static_cast<float>(static_cast<int>(request.operation)));
        shader->setFloat(2, request.centerX); shader->setFloat(3, request.centerY); shader->setFloat(4, request.centerZ);
        shader->setFloat(5, request.radius); shader->setFloat(6, request.strength); shader->setFloat(7, request.falloff);
        shader->setFloat(8, request.directionX); shader->setFloat(9, request.directionY); shader->setFloat(10, request.directionZ);
        shader->setFloat(11, request.maxDisplacement);
        dispatch(shader.get(), static_cast<int>((count + 63u) / 64u), 1, 1);
        p->readFloat32s(positions.data(), static_cast<int>(positions.size()));
        std::vector<float> result(count * 3u);
        for (std::size_t i = 0; i < count; ++i)
            for (std::size_t c = 0; c < 3u; ++c) result[i * 3u + c] = positions[i * 4u + c];
        return Result<std::vector<float>>::success(std::move(result));
    } catch (const std::exception& error) {
        return Result<std::vector<float>>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what()));
    }
}

bool Gpgpu::isAvailable() const {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu") return false;
    return webgpuGpgpuReady();
#else
    if (currentGraphicsBackend() != "vulkan") return false;
    return vulkanGpgpuReady();
#endif
}

Result<std::vector<uint32_t>> compileComputeSpirv(const std::string &source) {
#ifdef EVENGINE_WEBGPU
    return Result<std::vector<uint32_t>>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "GLSL compilation requires Vulkan"));
#else
    try {
        return Result<std::vector<uint32_t>>::success(compileComputeGlsl(source));
    } catch (const std::exception &e) {
        return Result<std::vector<uint32_t>>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what()));
    }
#endif
}

Result<std::unique_ptr<ComputeShader>> createComputeShader(const std::vector<uint32_t> &words) {
#ifdef EVENGINE_WEBGPU
    return Result<std::unique_ptr<ComputeShader>>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "SPIR-V pipelines require Vulkan"));
#else
    try {
        if (words.size() < 5 || words.front() != 0x07230203)
            return Result<std::unique_ptr<ComputeShader>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid SPIR-V header"));
        return Result<std::unique_ptr<ComputeShader>>::success(
            std::unique_ptr<ComputeShader>(vulkanNewShaderFromSpirv(words)));
    } catch (const std::exception &e) {
        return Result<std::unique_ptr<ComputeShader>>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what()));
    }
#endif
}

ComputeShader *Gpgpu::newShader(const std::string &source) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newShader: requires webgpu Graphics backend");
    // Source is WGSL on the WebGPU backend (browsers cannot compile GLSL).
    return webgpuNewShaderFromWgsl(source);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShader: requires vulkan Graphics backend");
    auto compiled = compileComputeSpirv(source);
    if (!compiled.ok()) throw Exception("%s", compiled.error()->message().c_str());
    auto shader = createComputeShader(compiled.value());
    if (!shader.ok()) throw Exception("%s", shader.error()->message().c_str());
    return shader.value().release();
#endif
}

ComputeShader *Gpgpu::newShaderFromBytecode(const std::string &path) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newShaderFromBytecode: requires webgpu Graphics backend");
    // Load WGSL text from the given path.
    auto *fs = eve::filesystem::Filesystem::create();
    if (!fs) throw Exception("Gpgpu.newShaderFromBytecode: no filesystem");
    std::unique_ptr<eve::filesystem::FileData> file(fs->read(path));
    if (!file) throw Exception("Gpgpu.newShaderFromBytecode: cannot open '%s'", path.c_str());
    std::string src(reinterpret_cast<const char *>(file->getData()), file->getSize());
    return webgpuNewShaderFromWgsl(src);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShaderFromBytecode: requires vulkan Graphics backend");
    return vulkanNewShaderFromSpirv(loadSpirvFile(path));
#endif
}

ComputeShader *Gpgpu::newShaderFromSpvFile(const std::string &path) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newShaderFromSpvFile: requires webgpu Graphics backend");
    throw Exception("Gpgpu.newShaderFromSpvFile: SPIR-V is only supported on vulkan; "
                    "use newShaderFromBytecode with a .wgsl file on the WebGPU backend");
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newShaderFromSpvFile: SPIR-V is only supported on vulkan");
    return newShaderFromBytecode(path);
#endif
}

GpuBuffer *Gpgpu::newBuffer(int byteSize, const std::string &usage) {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newBuffer: requires webgpu Graphics backend");
    return webgpuNewBuffer(byteSize, usage);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newBuffer: requires vulkan Graphics backend");
    return vulkanNewBuffer(byteSize, usage);
#endif
}

Sequence *Gpgpu::newSequence() {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.newSequence: requires webgpu Graphics backend");
    return new Sequence();
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.newSequence: requires vulkan Graphics backend");
    return new Sequence();
#endif
}

void Gpgpu::dispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ) {
    if (!shader) return;
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu")
        throw Exception("Gpgpu.dispatch: requires webgpu Graphics backend");
    webgpuDispatch(shader, groupsX, groupsY, groupsZ);
#else
    if (currentGraphicsBackend() != "vulkan")
        throw Exception("Gpgpu.dispatch: requires vulkan Graphics backend");
    vulkanDispatch(shader, groupsX, groupsY, groupsZ);
#endif
}

void Gpgpu::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Gpgpu::create, false);
    expose(cls);

    auto shader = table.addClass<ComputeShader>(
        "ComputeShader",
        std::function<ComputeShader *()>([]() -> ComputeShader * { return nullptr; }), true);
    shader.addFunc("bindBuffer", &ComputeShader::bindBuffer);
    shader.addFunc("getBoundBuffer", &ComputeShader::getBoundBuffer);
    shader.addFunc("setFloat", &ComputeShader::setFloat);
    shader.addFunc("getFloat", &ComputeShader::getFloat);
    shader.addFunc("clearBindings", &ComputeShader::clearBindings);

    auto buf = table.addClass<GpuBuffer>(
        "GpuBuffer", std::function<GpuBuffer *()>([]() -> GpuBuffer * { return nullptr; }), true);
    buf.addFunc("getSize", &GpuBuffer::getSize);
    buf.addFunc("getUsage", &GpuBuffer::getUsage);
    buf.addFunc("writeData", &GpuBuffer::writeData);
    buf.addFunc("readData", &GpuBuffer::readData);
    buf.addFunc("writeFloat32", &GpuBuffer::writeFloat32);
    buf.addFunc("readFloat32", &GpuBuffer::readFloat32);
    buf.addFunc("fillFloat32", &GpuBuffer::fillFloat32);

    auto seq = table.addClass<Sequence>(
        "GpuSequence",
        std::function<Sequence *()>([]() -> Sequence * { return new Sequence(); }), true);
    seq.addFunc("isAvailable", &Sequence::isAvailable);
    seq.addFunc("begin", &Sequence::begin);
    seq.addFunc("recordUpload", &Sequence::recordUpload);
    seq.addFunc("recordDownload", &Sequence::recordDownload);
    seq.addFunc("recordDispatch", &Sequence::recordDispatch);
    seq.addFunc("submit", &Sequence::submit);
    seq.addFunc("submitAsync", submitSequenceAsyncScript);
    seq.addFunc("poll", pollSequenceScript);
    seq.addFunc("wait", waitSequenceScript);
    seq.addFunc("getStatus", &Sequence::getStatusName);

    // Native ECS↔GPU helper (used by eve.ShaderSystem script class).
    auto ecsSys = table.addClass<ShaderSystem>(
        "EcsShaderSystem",
        std::function<ShaderSystem *()>([]() -> ShaderSystem * { return new ShaderSystem(); }),
        true);
    ecsSys.addFunc("setGpgpu", &ShaderSystem::setGpgpu);
    ecsSys.addFunc("getGpgpu", &ShaderSystem::getGpgpu);
    ecsSys.addFunc("setShaderSource", &ShaderSystem::setShaderSource);
    ecsSys.addFunc("setShader", setShaderScript);
    ecsSys.addFunc("getShader", &ShaderSystem::getShader);
    ecsSys.addFunc("setLocalSize", &ShaderSystem::setLocalSize);
    ecsSys.addFunc("getLocalSize", &ShaderSystem::getLocalSize);
    ecsSys.addFunc("ensureBuffer", &ShaderSystem::ensureBuffer);
    ecsSys.addFunc("getBuffer", &ShaderSystem::getBuffer);
    ecsSys.addFunc("attachBuffer", &ShaderSystem::attachBuffer);
    ecsSys.addFunc("setFloat", &ShaderSystem::setFloat);
    ecsSys.addFunc("getFloat", &ShaderSystem::getFloat);
    ecsSys.addFunc("getUploadCount", &ShaderSystem::getUploadCount);
    ecsSys.addFunc("getDownloadCount", &ShaderSystem::getDownloadCount);
    ecsSys.addFunc("getDispatchCount", &ShaderSystem::getDispatchCount);
    ecsSys.addFunc("resetStatistics", &ShaderSystem::resetStatistics);
    ecsSys.addFunc("dispatch", dispatchShaderSystemScript);
    ecsSys.addFunc("recordDispatch", &ShaderSystem::recordDispatch);
    ecsSys.addFunc("clearBuffers", &ShaderSystem::clearBuffers);

    table.addFunc("packEcsFloats", packScriptEntityFloats);
    table.addFunc("packEcsFloatsRange", packScriptEntityFloatsRange);
    table.addFunc("unpackEcsFloats", unpackScriptEntityFloats);
    table.addFunc("unpackEcsFloatsRange", unpackScriptEntityFloatsRange);
}

void Gpgpu::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Gpgpu::getName);
    cls.addFunc("isAvailable", &Gpgpu::isAvailable);
    cls.addFunc("newShader", &Gpgpu::newShader);
    cls.addFunc("newShaderFromBytecode", &Gpgpu::newShaderFromBytecode);
    cls.addFunc("newShaderFromSpvFile", &Gpgpu::newShaderFromSpvFile);
    cls.addFunc("newBuffer", &Gpgpu::newBuffer);
    cls.addFunc("newSequence", &Gpgpu::newSequence);
    cls.addFunc("dispatch", &Gpgpu::dispatch);
    cls.addFunc("setGpuDrivenEnabled", setGpuDrivenEnabledScript);
    cls.addFunc("isGpuDrivenEnabled", isGpuDrivenEnabledScript);
    cls.addFunc("gpuDrivenMeshRecord", gpuDrivenMeshRecordScript);
    cls.addFunc("gpuDrivenMaterialRecord", gpuDrivenMaterialRecordScript);
    cls.addFunc("gpuDrivenMaterialUsable", gpuDrivenMaterialUsableScript);
    cls.addFunc("getGpuDrivenInstanceStride", getGpuDrivenInstanceStrideScript);
    cls.addFunc("getGpuResidentOffsetAlignment", getGpuResidentOffsetAlignmentScript);
    cls.addFunc("writeGpuDrivenInstance", writeGpuDrivenInstanceScript);
    cls.addFunc("submitResidentInstances", submitResidentInstancesScript);
}

}  // namespace eve::gpgpu
