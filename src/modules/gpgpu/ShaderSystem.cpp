#include "gpgpu/ShaderSystem.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"

#include "common/Exception.h"

#include <algorithm>
#include <cstring>

namespace eve::gpgpu {

ShaderSystem::~ShaderSystem() {
    clearBuffers();
    if (ownsShader_) {
        delete shader_;
        shader_ = nullptr;
        ownsShader_ = false;
    }
}

void ShaderSystem::setGpgpu(Gpgpu *gpu) { gpu_ = gpu; }

void ShaderSystem::setShaderSource(const std::string &glsl) {
    if (!gpu_) throw Exception("ShaderSystem.setShaderSource: Gpgpu is null");
    ComputeShader *compiled = gpu_->newShader(glsl);
    if (ownsShader_) delete shader_;
    shader_ = compiled;
    ownsShader_ = true;
}

void ShaderSystem::setShader(ComputeShader *shader, bool takeOwnership) {
    if (ownsShader_ && shader_ != shader) {
        delete shader_;
        ownsShader_ = false;
    }
    shader_ = shader;
    ownsShader_ = takeOwnership && shader != nullptr;
}

void ShaderSystem::setLocalSize(int localSize) {
    localSize_ = localSize > 0 ? localSize : kDefaultLocalSize;
}

GpuBuffer *ShaderSystem::ensureBuffer(int binding, int floatCount) {
    if (binding < 0 || binding >= kMaxBindings)
        throw Exception("ShaderSystem.ensureBuffer: binding out of range");
    if (!gpu_) throw Exception("ShaderSystem.ensureBuffer: Gpgpu is null");
    if (floatCount <= 0) floatCount = 1;

    const int bytes = floatCount * int(sizeof(float));
    GpuBuffer *existing = buffers_[binding];
    if (existing && existing->getSize() >= bytes) return existing;

    GpuBuffer *created = gpu_->newBuffer(bytes, "storage");
    if (ownsBuffer_[binding]) delete existing;
    buffers_[binding] = created;
    ownsBuffer_[binding] = true;
    return created;
}

GpuBuffer *ShaderSystem::getBuffer(int binding) const {
    if (binding < 0 || binding >= kMaxBindings) return nullptr;
    return buffers_[binding];
}

void ShaderSystem::attachBuffer(int binding, GpuBuffer *buffer) {
    if (binding < 0 || binding >= kMaxBindings) throw Exception("ShaderSystem.attachBuffer: binding out of range");
    if (!buffer) throw Exception("ShaderSystem.attachBuffer: buffer is null");
    if (buffers_[binding] == buffer) return;
    if (ownsBuffer_[binding]) delete buffers_[binding];
    buffers_[binding]    = buffer;
    ownsBuffer_[binding] = false;
}

void ShaderSystem::upload(int binding, const float *data, int floatCount) {
    if (!data || floatCount <= 0) return;
    GpuBuffer *buf = ensureBuffer(binding, floatCount);
    buf->writeFloat32s(data, floatCount, 0);
    ++uploadCount_;
}

void ShaderSystem::download(int binding, float *out, int floatCount) const {
    if (!out || floatCount <= 0) return;
    GpuBuffer *buf = getBuffer(binding);
    if (!buf) throw Exception("ShaderSystem.download: binding has no buffer");
    buf->readFloat32s(out, floatCount, 0);
    ++downloadCount_;
}

void ShaderSystem::uploadRange(int binding, const float *data, int floatCount, int startFloat) {
    if (!data || floatCount <= 0) return;
    if (startFloat < 0) throw Exception("ShaderSystem.uploadRange: negative offset");
    GpuBuffer *buf = getBuffer(binding);
    if (!buf) throw Exception("ShaderSystem.uploadRange: binding has no buffer");
    const uint64_t end = (uint64_t(startFloat) + uint64_t(floatCount)) * sizeof(float);
    if (end > uint64_t(buf->getSize())) throw Exception("ShaderSystem.uploadRange: range exceeds buffer");
    buf->writeFloat32s(data, floatCount, startFloat);
    ++uploadCount_;
}

void ShaderSystem::downloadRange(int binding, float *out, int floatCount, int startFloat) const {
    if (!out || floatCount <= 0) return;
    if (startFloat < 0) throw Exception("ShaderSystem.downloadRange: negative offset");
    GpuBuffer *buf = getBuffer(binding);
    if (!buf) throw Exception("ShaderSystem.downloadRange: binding has no buffer");
    const uint64_t end = (uint64_t(startFloat) + uint64_t(floatCount)) * sizeof(float);
    if (end > uint64_t(buf->getSize())) throw Exception("ShaderSystem.downloadRange: range exceeds buffer");
    buf->readFloat32s(out, floatCount, startFloat);
    ++downloadCount_;
}

void ShaderSystem::upload(int binding, const std::vector<float> &data) {
    if (data.empty()) return;
    upload(binding, data.data(), int(data.size()));
}

std::vector<float> ShaderSystem::download(int binding, int floatCount) const {
    std::vector<float> out(size_t(std::max(0, floatCount)));
    if (floatCount > 0) download(binding, out.data(), floatCount);
    return out;
}

void ShaderSystem::setFloat(int index, float value) {
    if (!shader_) return;
    shader_->setFloat(index, value);
}

float ShaderSystem::getFloat(int index) const {
    if (!shader_) return 0.f;
    return shader_->getFloat(index);
}

void ShaderSystem::dispatch(int entityCount, float dt) {
    if (!gpu_ || !shader_ || entityCount <= 0) return;

    for (int i = 0; i < kMaxBindings; ++i) {
        if (buffers_[i]) shader_->bindBuffer(i, buffers_[i]);
    }

    shader_->setFloat(0, dt);
    shader_->setFloat(1, float(entityCount));

    const int ls = localSize_ > 0 ? localSize_ : kDefaultLocalSize;
    const int groups = (entityCount + ls - 1) / ls;
    gpu_->dispatch(shader_, groups, 1, 1);
    ++dispatchCount_;
}

void ShaderSystem::recordDispatch(Sequence *sequence, int entityCount, float dt) {
    if (!sequence) throw Exception("ShaderSystem.recordDispatch: sequence is null");
    if (!shader_ || entityCount <= 0) return;

    for (int i = 0; i < kMaxBindings; ++i) {
        if (buffers_[i]) shader_->bindBuffer(i, buffers_[i]);
    }
    shader_->setFloat(0, dt);
    shader_->setFloat(1, float(entityCount));

    const int ls = localSize_ > 0 ? localSize_ : kDefaultLocalSize;
    sequence->recordDispatch(shader_, (entityCount + ls - 1) / ls, 1, 1);
    ++dispatchCount_;
}

void ShaderSystem::resetStatistics() {
    uploadCount_   = 0;
    downloadCount_ = 0;
    dispatchCount_ = 0;
}

void ShaderSystem::clearBuffers() {
    for (int i = 0; i < kMaxBindings; ++i) {
        if (ownsBuffer_[i]) {
            delete buffers_[i];
            ownsBuffer_[i] = false;
        }
        buffers_[i] = nullptr;
    }
}

}  // namespace eve::gpgpu
