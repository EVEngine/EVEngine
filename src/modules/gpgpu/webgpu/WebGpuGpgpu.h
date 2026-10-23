#pragma once

#include "gpgpu/ComputeShader.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <string>

namespace eve::gpgpu {

class WebGpuGpuBuffer;
class WebGpuSequence;

/**
 * @brief Compute program for the WebGPU backend. Accepts WGSL source; GLSL/SPIR-V
 * input is rejected (browsers only accept WGSL at runtime).
 */
class WebGpuComputeShader final : public ComputeShader {
public:
    /** @brief Constructs a WebGpuComputeShader. */
    WebGpuComputeShader() = default;
    /** @brief Releases WebGpuComputeShader resources. */
    ~WebGpuComputeShader() override;

    /** @brief Binds buffer. */
    void bindBuffer(int binding, GpuBuffer *buffer) override;
    /** @brief Returns the bound buffer. */
    GpuBuffer *getBoundBuffer(int binding) const override;

    /** @brief Sets the float. */
    void setFloat(int index, float value) override;
    /** @brief Returns the float. */
    float getFloat(int index) const override;

    /** @brief Clears bindings. */
    void clearBindings() override;

    wgpu::ComputePipeline pipeline;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::BindGroupLayout setLayout;
    wgpu::Buffer pushUbo;
    bool ready = false;

private:
    std::array<GpuBuffer *, kMaxBindings> bindings_{};
};

/** @brief Storage / staging buffer for the WebGPU backend. */
class WebGpuGpuBuffer final : public GpuBuffer {
public:
    /** @brief Constructs a WebGpuGpuBuffer. */
    WebGpuGpuBuffer() = default;
    /** @brief Releases WebGpuGpuBuffer resources. */
    ~WebGpuGpuBuffer() override;

    /** @brief Byte length of the owned buffer. */
    int getSize() const override { return int(size_); }
    /** @brief Returns the usage. */
    std::string getUsage() const override { return usage_; }

    /** @brief Writes data. */
    void writeData(data::ByteData *data, int dstOffset = 0) override;
    /** @brief Reads data. */
    data::ByteData *readData(int srcOffset = 0, int size = -1) override;

    /** @brief Writes float 32. */
    void writeFloat32(int floatIndex, float value) override;
    /** @brief Reads float 32. */
    float readFloat32(int floatIndex) override;
    /** @brief Fill float 32. */
    void fillFloat32(float value) override;

    /** @brief Writes float 32 s. */
    void writeFloat32s(const float *data, int count, int startIndex = 0) override;
    /** @brief Reads float 32 s. */
    void readFloat32s(float *out, int count, int startIndex = 0) const override;

    /** @brief Uploads bytes. */
    void uploadBytes(const void *src, uint64_t nbytes, uint64_t dstOffset = 0) override;
    /** @brief Downloads bytes. */
    void downloadBytes(void *dst, uint64_t nbytes, uint64_t srcOffset = 0) const override;
    /** @brief Resident view. */
    GpuResidentBufferView residentView() const override;

    wgpu::Buffer buffer;
    uint64_t size_ = 0;
    std::string usage_;
};

// Backend entry points (mirror gpgpu/vulkan/VulkanGpgpu.h).
/** @brief Webgpu gpgpu ready. */
bool webgpuGpgpuReady();
/** @brief Webgpu new shader from wgsl. */
WebGpuComputeShader *webgpuNewShaderFromWgsl(const std::string &wgsl);
/** @brief Webgpu new shader from spirv. */
WebGpuComputeShader *webgpuNewShaderFromSpirv(const std::vector<uint32_t> &spv);
/** @brief Webgpu new buffer. */
WebGpuGpuBuffer *webgpuNewBuffer(int byteSize, const std::string &usage);
/** @brief Webgpu dispatch. */
void webgpuDispatch(ComputeShader *shader, int groupsX, int groupsY, int groupsZ);
/** @brief Webgpu sequence create. */
WebGpuSequence *webgpuSequenceCreate();
/** @brief Webgpu sequence destroy. */
void webgpuSequenceDestroy(WebGpuSequence *sequence);
/** @brief Webgpu sequence ready. */
bool webgpuSequenceReady(WebGpuSequence *sequence);
/** @brief Webgpu sequence begin. */
void webgpuSequenceBegin(WebGpuSequence *sequence);
/** @brief Webgpu sequence record upload. */
void webgpuSequenceRecordUpload(WebGpuSequence *sequence, GpuBuffer *dst, const void *src,
                                uint64_t nbytes, uint64_t dstOffset);
/** @brief Webgpu sequence record download. */
void webgpuSequenceRecordDownload(WebGpuSequence *sequence, GpuBuffer *src, GpuBuffer *staging,
                                  uint64_t nbytes, uint64_t srcOffset);
/** @brief Webgpu sequence record dispatch. */
void webgpuSequenceRecordDispatch(WebGpuSequence *sequence, ComputeShader *shader, int groupsX,
                                  int groupsY, int groupsZ);
/** @brief Webgpu sequence submit. */
void webgpuSequenceSubmit(WebGpuSequence *sequence);
/** @brief Webgpu sequence submit async. */
SequenceStatus       webgpuSequenceSubmitAsync(WebGpuSequence *sequence);
/** @brief Webgpu sequence poll. */
SequenceStatus       webgpuSequencePoll(WebGpuSequence *sequence);
/** @brief Webgpu sequence wait. */
SequenceStatus       webgpuSequenceWait(WebGpuSequence *sequence);
/** @brief Webgpu sequence status. */
SequenceStatus       webgpuSequenceStatus(const WebGpuSequence *sequence);

}  // namespace eve::gpgpu
