#include "gpgpu/ShaderSystem.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"

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

void ShaderSystem::upload(int binding, const float *data, int floatCount) {
    if (!data || floatCount <= 0) return;
    GpuBuffer *buf = ensureBuffer(binding, floatCount);
    buf->writeFloat32s(data, floatCount, 0);
}

void ShaderSystem::download(int binding, float *out, int floatCount) const {
    if (!out || floatCount <= 0) return;
    GpuBuffer *buf = getBuffer(binding);
    if (!buf) throw Exception("ShaderSystem.download: binding has no buffer");
    buf->readFloat32s(out, floatCount, 0);
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
