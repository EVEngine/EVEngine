#pragma once

#include "gpgpu/ComputeShader.h"
#include "gpgpu/GpuBuffer.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <string>

namespace eve::gpgpu {

class WebGpuGpuBuffer;

/**
 * Compute program for the WebGPU backend. Accepts WGSL source; GLSL/SPIR-V
 * input is rejected (browsers only accept WGSL at runtime).
 */
class WebGpuComputeShader final : public ComputeShader {
public:
    WebGpuComputeShader() = default;
    ~WebGpuComputeShader() override;

    void bindBuffer(int binding, GpuBuffer *buffer) override;
    GpuBuffer *getBoundBuffer(int binding) const override;

    void setFloat(int index, float value) override;
    float getFloat(int index) const override;

    void clearBindings() override;

    wgpu::ComputePipeline pipeline;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::BindGroupLayout setLayout;
    wgpu::Buffer pushUbo;
    bool ready = false;

private:
    std::array<GpuBuffer *, kMaxBindings> bindings_{};
};

/** Storage / staging buffer for the WebGPU backend. */
class WebGpuGpuBuffer final : public GpuBuffer {
public:
    WebGpuGpuBuffer() = default;
    ~WebGpuGpuBuffer() override;

    int getSize() const override { return int(size_); }
    std::string getUsage() const override { return usage_; }

    void writeData(data::ByteData *data, int dstOffset = 0) override;
    data::ByteData *readData(int srcOffset = 0, int size = -1) override;

    void writeFloat32(int floatIndex, float value) override;
    float readFloat32(int floatIndex) override;
    void fillFloat32(float value) override;

    void writeFloat32s(const float *data, int count, int startIndex = 0) override;
    void readFloat32s(float *out, int count, int startIndex = 0) const override;

    void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) override;
    void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) const override;

    wgpu::Buffer buffer;
    uint64_t size_ = 0;
    std::string usage_;
};

// Backend entry points (mirror gpgpu/vulkan/VulkanGpgpu.h).
bool webgpuGpgpuReady();
WebGpuComputeShader *webgpuNewShaderFromWgsl(const std::string &wgsl);
WebGpuComputeShader *webgpuNewShaderFromSpirv(const std::vector<uint32_t> &spv);
WebGpuGpuBuffer *webgpuNewBuffer(int byteSize, const std::string &usage);
void webgpuDispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ);

}  // namespace eve::gpgpu
