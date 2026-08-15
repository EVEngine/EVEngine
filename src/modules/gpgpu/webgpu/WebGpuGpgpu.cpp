#include "gpgpu/webgpu/WebGpuGpgpu.h"

#include "common/Exception.h"
#include "common/Module.h"
#include "data/ByteData.h"
#include "graphics/Graphics.h"
#include "graphics/webgpu/Graphics.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <cstring>
#include <vector>

namespace eve::gpgpu {

namespace {

graphics::webgpu::Graphics *requireWebGpuGraphics() {
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) gfx = eve::graphics::Graphics::create();
    auto *wgg = dynamic_cast<graphics::webgpu::Graphics *>(gfx);
    if (!wgg) throw Exception("Gpgpu: requires WebGPU Graphics backend");
    if (!wgg->getDevice())
        throw Exception("Gpgpu: Graphics device not initialized (create a window first)");
    return wgg;
}

wgpu::Device &gpuDevice() { return requireWebGpuGraphics()->getDevice(); }
wgpu::Queue &gpuQueue() { return requireWebGpuGraphics()->getQueue(); }
wgpu::Instance &gpuInstance() { return requireWebGpuGraphics()->getInstance(); }

/** Build a WGPUStringView from a C string (null-safe, length auto-computed). */
WGPUStringView sv(const char *s) { return WGPUStringView{s, s ? std::strlen(s) : 0}; }

}  // namespace

bool webgpuGpgpuReady() {
    try {
        auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
        if (!gfx) return false;
        auto *wgg = dynamic_cast<graphics::webgpu::Graphics *>(gfx);
        if (!wgg) return false;
        return bool(wgg->getDevice());
    } catch (...) {
        return false;
    }
}

WebGpuComputeShader::~WebGpuComputeShader() = default;

void WebGpuComputeShader::bindBuffer(int binding, GpuBuffer *buffer) {
    if (binding < 0 || binding >= kMaxBindings) return;
    bindings_[size_t(binding)] = buffer;
}

GpuBuffer *WebGpuComputeShader::getBoundBuffer(int binding) const {
    if (binding < 0 || binding >= kMaxBindings) return nullptr;
    return bindings_[size_t(binding)];
}

void WebGpuComputeShader::setFloat(int index, float value) {
    if (index < 0 || index >= kMaxFloats) return;
    push_[size_t(index)] = value;
}

float WebGpuComputeShader::getFloat(int index) const {
    if (index < 0 || index >= kMaxFloats) return 0.f;
    return push_[size_t(index)];
}

void WebGpuComputeShader::clearBindings() {
    bindings_.fill(nullptr);
}

WebGpuComputeShader *webgpuNewShaderFromWgsl(const std::string &wgsl) {
    auto &device = gpuDevice();
    auto *shader = new WebGpuComputeShader();

    // Bind group layout: 8 storage buffers (bindings 0..7) + push UBO (binding 8).
    WGPUBindGroupLayoutEntry entries[ComputeShader::kMaxBindings + 1]{};
    for (int i = 0; i < ComputeShader::kMaxBindings; ++i) {
        entries[size_t(i)].binding = uint32_t(i);
        entries[size_t(i)].visibility = WGPUShaderStage_Compute;
        entries[size_t(i)].buffer.type = WGPUBufferBindingType_Storage;
        entries[size_t(i)].buffer.hasDynamicOffset = false;
        entries[size_t(i)].buffer.minBindingSize = 0;
    }
    entries[ComputeShader::kMaxBindings].binding = uint32_t(ComputeShader::kMaxBindings);
    entries[ComputeShader::kMaxBindings].visibility = WGPUShaderStage_Compute;
    entries[ComputeShader::kMaxBindings].buffer.type = WGPUBufferBindingType_Uniform;
    entries[ComputeShader::kMaxBindings].buffer.hasDynamicOffset = true;
    entries[ComputeShader::kMaxBindings].buffer.minBindingSize = ComputeShader::kPushConstantBytes;

    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.label = sv("eve_compute");
    bglDesc.entryCount = ComputeShader::kMaxBindings + 1;
    bglDesc.entries = entries;
    shader->setLayout = device.CreateBindGroupLayout(reinterpret_cast<const wgpu::BindGroupLayoutDescriptor*>(&bglDesc));

    WGPUBindGroupLayout bgl = shader->setLayout.Get();
    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.label = sv("eve_compute_layout");
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    shader->pipelineLayout = device.CreatePipelineLayout(reinterpret_cast<const wgpu::PipelineLayoutDescriptor*>(&plDesc));

    WGPUShaderSourceWGSL wd{};
    wd.chain.sType = WGPUSType_ShaderSourceWGSL;
    wd.code = sv(wgsl.c_str());
    WGPUShaderModuleDescriptor md{};
    md.label = sv("eve_compute_module");
    md.nextInChain = &wd.chain;
    wgpu::ShaderModule module = device.CreateShaderModule(reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));

    WGPUComputePipelineDescriptor pd{};
    pd.label = sv("eve_compute_pipeline");
    pd.layout = shader->pipelineLayout.Get();
    pd.compute.module = module.Get();
    pd.compute.entryPoint = sv("main");
    shader->pipeline = device.CreateComputePipeline(reinterpret_cast<const wgpu::ComputePipelineDescriptor*>(&pd));

    // Push-constant UBO (128 bytes, 256-aligned for dynamic offsets).
    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_compute_push");
    bd.size = ComputeShader::kPushConstantBytes;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform;
    bd.mappedAtCreation = false;
    shader->pushUbo = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    shader->ready = true;
    return shader;
}

WebGpuComputeShader *webgpuNewShaderFromSpirv(const std::vector<uint32_t> &spv) {
    (void)spv;
    throw Exception("Gpgpu.newShaderFromSpirv: SPIR-V compute shaders are not supported on the "
                    "WebGPU backend; use WGSL source instead.");
}

WebGpuGpuBuffer *webgpuNewBuffer(int byteSize, const std::string &usage) {
    if (byteSize <= 0) throw Exception("Gpgpu.newBuffer: byteSize must be > 0");
    auto &device = gpuDevice();
    bool staging = usage == "staging";
    WGPUBufferUsage bufUsage = staging ? (WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapRead)
                                       : (WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
                                          WGPUBufferUsage_CopySrc | WGPUBufferUsage_Vertex);
    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_compute_buffer");
    bd.size = uint64_t(byteSize);
    bd.usage = bufUsage;
    bd.mappedAtCreation = false;
    auto *b = new WebGpuGpuBuffer();
    b->buffer = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));
    b->size_ = uint64_t(byteSize);
    b->usage_ = usage;
    return b;
}

namespace {

bool readBufferToCpu(wgpu::Buffer src, uint64_t srcOffset, void *dst, uint64_t nbytes) {
    auto &device = gpuDevice();
    auto &queue = gpuQueue();
    if (!src || nbytes == 0) return false;

    WGPUBufferDescriptor bd{};
    bd.label = sv("eve_compute_readback");
    bd.size = nbytes;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.mappedAtCreation = false;
    wgpu::Buffer staging = device.CreateBuffer(reinterpret_cast<const wgpu::BufferDescriptor*>(&bd));

    wgpu::CommandEncoder enc = device.CreateCommandEncoder();
    enc.CopyBufferToBuffer(src, srcOffset, staging, 0, nbytes);
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
    wgpuBufferMapAsync(staging.Get(), WGPUMapMode_Read, 0, nbytes, cbInfo);

    int guard = 0;
    while (!mapped && guard < 2000) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(gpuInstance().Get());
        ++guard;
    }
    if (!mapped) return false;
    const uint8_t *data = static_cast<const uint8_t *>(staging.GetConstMappedRange(0, nbytes));
    if (!data) return false;
    std::memcpy(dst, data, size_t(nbytes));
    staging.Unmap();
    return true;
}

}  // namespace

void webgpuDispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ) {
    auto *ws = dynamic_cast<WebGpuComputeShader *>(shader);
    if (!ws || !ws->ready || !ws->pipeline) return;
    if (groupsX <= 0) groupsX = 1;
    if (groupsY <= 0) groupsY = 1;
    if (groupsZ <= 0) groupsZ = 1;

    auto &device = gpuDevice();
    auto &queue = gpuQueue();

    // Push constants -> uniform buffer (dynamic offset 0).
    queue.WriteBuffer(ws->pushUbo, 0, shader->pushConstantData(), ComputeShader::kPushConstantBytes);

    // Build a bind group for the current storage-buffer bindings.
    WGPUBindGroupEntry entries[ComputeShader::kMaxBindings + 1]{};
    WebGpuGpuBuffer *dummy = webgpuNewBuffer(4, "storage");
    for (int i = 0; i < ComputeShader::kMaxBindings; ++i) {
        auto *vb = dynamic_cast<WebGpuGpuBuffer *>(ws->getBoundBuffer(i));
        entries[size_t(i)].binding = uint32_t(i);
        entries[size_t(i)].buffer = (vb && vb->buffer) ? vb->buffer.Get() : dummy->buffer.Get();
        entries[size_t(i)].size = (vb && vb->buffer) ? vb->size_ : 4;
    }
    entries[ComputeShader::kMaxBindings].binding = uint32_t(ComputeShader::kMaxBindings);
    entries[ComputeShader::kMaxBindings].buffer = ws->pushUbo.Get();
    entries[ComputeShader::kMaxBindings].size = ComputeShader::kPushConstantBytes;
    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.layout = ws->setLayout.Get();
    bgDesc.entryCount = ComputeShader::kMaxBindings + 1;
    bgDesc.entries = entries;
    wgpu::BindGroup bg = device.CreateBindGroup(reinterpret_cast<const wgpu::BindGroupDescriptor*>(&bgDesc));
    delete dummy;

    wgpu::CommandEncoder enc = device.CreateCommandEncoder();
    WGPUComputePassDescriptor cpDesc{};
    wgpu::ComputePassEncoder pass = enc.BeginComputePass(reinterpret_cast<const wgpu::ComputePassDescriptor*>(&cpDesc));
    pass.SetPipeline(ws->pipeline);
    uint32_t offsets[1] = {0};
    pass.SetBindGroup(0, bg, 1, offsets);
    pass.DispatchWorkgroups(uint32_t(groupsX), uint32_t(groupsY), uint32_t(groupsZ));
    pass.End();
    wgpu::CommandBuffer cmd = enc.Finish();
    queue.Submit(1, &cmd);
}

// ---------------------------------------------------------------------------
// WebGpuGpuBuffer
// ---------------------------------------------------------------------------

WebGpuGpuBuffer::~WebGpuGpuBuffer() = default;

void WebGpuGpuBuffer::uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset) {
    if (!buffer || !src || nbytes == 0) return;
    if (dstOffset + nbytes > size_)
        throw Exception("GpuBuffer.write: out of range");
    gpuQueue().WriteBuffer(buffer, dstOffset, src, nbytes);
}

void WebGpuGpuBuffer::downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset) const {
    if (!buffer || !dst || nbytes == 0) return;
    if (srcOffset + nbytes > size_)
        throw Exception("GpuBuffer.read: out of range");
    readBufferToCpu(buffer, srcOffset, dst, nbytes);
}

void WebGpuGpuBuffer::writeData(data::ByteData *data, int dstOffset) {
    if (!data) return;
    uploadBytes(data->getData(), data->getSize(), uint64_t(dstOffset < 0 ? 0 : dstOffset));
}

data::ByteData *WebGpuGpuBuffer::readData(int srcOffset, int size) {
    const int off = srcOffset < 0 ? 0 : srcOffset;
    int nbytes = size;
    if (nbytes < 0) nbytes = int(size_) - off;
    if (nbytes <= 0) return new data::ByteData(size_t(0));
    auto *out = new data::ByteData(size_t(nbytes));
    downloadBytes(out->getData(), uint64_t(nbytes), uint64_t(off));
    return out;
}

void WebGpuGpuBuffer::writeFloat32(int floatIndex, float value) {
    if (floatIndex < 0) return;
    uploadBytes(&value, sizeof(float), uint64_t(floatIndex) * sizeof(float));
}

float WebGpuGpuBuffer::readFloat32(int floatIndex) {
    if (floatIndex < 0) return 0.f;
    float v = 0.f;
    downloadBytes(&v, sizeof(float), uint64_t(floatIndex) * sizeof(float));
    return v;
}

void WebGpuGpuBuffer::writeFloat32s(const float *data, int count, int startIndex) {
    if (!data || count <= 0 || startIndex < 0) return;
    uploadBytes(data, uint64_t(count) * sizeof(float), uint64_t(startIndex) * sizeof(float));
}

void WebGpuGpuBuffer::readFloat32s(float *out, int count, int startIndex) const {
    if (!out || count <= 0 || startIndex < 0) return;
    downloadBytes(out, uint64_t(count) * sizeof(float), uint64_t(startIndex) * sizeof(float));
}

void WebGpuGpuBuffer::fillFloat32(float value) {
    if (size_ < sizeof(float)) return;
    const size_t count = size_t(size_ / sizeof(float));
    std::vector<float> tmp(count, value);
    uploadBytes(tmp.data(), count * sizeof(float), 0);
}

}  // namespace eve::gpgpu
