#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/EcsScriptPack.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "gpgpu/ShaderSystem.h"

#include "common/Exception.h"
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

}  // namespace

Module_IMPL(Gpgpu, new Gpgpu());

bool Gpgpu::isAvailable() const {
#ifdef EVENGINE_WEBGPU
    if (currentGraphicsBackend() != "webgpu") return false;
    return webgpuGpgpuReady();
#else
    if (currentGraphicsBackend() != "vulkan") return false;
    return vulkanGpgpuReady();
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
    return vulkanNewShaderFromSpirv(compileComputeGlsl(source));
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
    seq.addFunc("submitAsync", std::function<std::string(Sequence *)>([](Sequence *self) {
                    if (!self) return std::string("failed");
                    (void)self->submitAsync();
                    return self->getStatusName();
                }));
    seq.addFunc("poll", std::function<std::string(Sequence *)>([](Sequence *self) {
                    if (!self) return std::string("failed");
                    (void)self->poll();
                    return self->getStatusName();
                }));
    seq.addFunc("wait", std::function<std::string(Sequence *)>([](Sequence *self) {
                    if (!self) return std::string("failed");
                    (void)self->wait();
                    return self->getStatusName();
                }));
    seq.addFunc("getStatus", &Sequence::getStatusName);

    // Native ECS↔GPU helper (used by eve.ShaderSystem script class).
    auto ecsSys = table.addClass<ShaderSystem>(
        "EcsShaderSystem",
        std::function<ShaderSystem *()>([]() -> ShaderSystem * { return new ShaderSystem(); }),
        true);
    ecsSys.addFunc("setGpgpu", &ShaderSystem::setGpgpu);
    ecsSys.addFunc("getGpgpu", &ShaderSystem::getGpgpu);
    ecsSys.addFunc("setShaderSource", &ShaderSystem::setShaderSource);
    ecsSys.addFunc("setShader", std::function<void(ShaderSystem *, ComputeShader *)>(
                                    [](ShaderSystem *self, ComputeShader *s) {
                                        if (self) self->setShader(s, false);
                                    }));
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
    ecsSys.addFunc("dispatch", std::function<void(ShaderSystem *, int, float)>(
                                   [](ShaderSystem *self, int n, float dt) {
                                       if (self) self->dispatch(n, dt);
                                   }));
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
    cls.addFunc("setGpuDrivenEnabled", std::function<void(Gpgpu *, bool)>([](Gpgpu *, bool enabled) {
                    if (auto *gfx = currentGraphics()) gfx->gpuDrivenSetEnabled(enabled);
                }));
    cls.addFunc("isGpuDrivenEnabled", std::function<bool(Gpgpu *)>([](Gpgpu *) {
                    auto *gfx = currentGraphics();
                    return gfx && gfx->gpuDrivenEnabled();
                }));
    cls.addFunc(
        "gpuDrivenMeshRecord",
        std::function<int(Gpgpu *, eve::graphics::Mesh *)>([](Gpgpu *, eve::graphics::Mesh *mesh) {
            auto *gfx = currentGraphics();
            return scriptGpuDrivenSlot(gfx ? gfx->gpuDrivenMeshRecord(mesh) : eve::graphics::kInvalidGpuDrivenSlot);
        }));
    cls.addFunc("gpuDrivenMaterialRecord",
                std::function<int(Gpgpu *, eve::graphics::Material *)>([](Gpgpu *, eve::graphics::Material *material) {
                    auto *gfx = currentGraphics();
                    return scriptGpuDrivenSlot(gfx ? gfx->gpuDrivenMaterialRecord(material)
                                                   : eve::graphics::kInvalidGpuDrivenSlot);
                }));
    cls.addFunc("gpuDrivenMaterialUsable",
                std::function<bool(Gpgpu *, eve::graphics::Material *)>([](Gpgpu *, eve::graphics::Material *material) {
                    auto *gfx = currentGraphics();
                    return gfx && gfx->gpuDrivenMaterialUsable(material);
                }));
    cls.addFunc("getGpuDrivenInstanceStride",
                std::function<int(Gpgpu *)>([](Gpgpu *) { return sizeof(eve::graphics::GpuInstance); }));
    cls.addFunc("getGpuResidentOffsetAlignment",
                std::function<int(Gpgpu *)>([](Gpgpu *) { return eve::kGpuResidentStorageOffsetAlignment; }));
    cls.addFunc("writeGpuDrivenInstance",
                std::function<void(Gpgpu *, GpuBuffer *, int, ssq::Array, int, int, int, int)>(
                    [](Gpgpu *, GpuBuffer *buffer, int instanceIndex, ssq::Array model, int meshId, int materialId,
                       int flags, int lodGroupId) {
                        writeGpuDrivenInstance(buffer, instanceIndex, model, meshId, materialId, flags, lodGroupId);
                    }));
    cls.addFunc("submitResidentInstances",
                std::function<std::string(Gpgpu *, GpuBuffer *, ssq::Array, int, int)>(
                    [](Gpgpu *, GpuBuffer *buffer, ssq::Array buckets, int instanceCount, int offsetBytes) {
                        return submitResidentInstances(buffer, buckets, instanceCount, offsetBytes);
                    }));
}

}  // namespace eve::gpgpu
