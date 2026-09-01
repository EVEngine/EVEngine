#pragma once

#include <cstdint>
#include <string>

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

/** @brief Observable lifecycle state of a reusable GPU command sequence. */
enum class SequenceStatus : uint8_t { Idle, Recording, Submitted, Complete, Failed };

/**
 * Kompute-style GPU command sequence.
 *
 * Records storage-buffer transfers and compute dispatches into a single
 * command buffer, then submits everything once. This turns an inference graph
 * (placeholder uploads -> N fused kernel dispatches -> output download) from
 * N+2 separate record/submit/wait round trips into one GPU submission.
 *
 * Typical usage:
 *
 *     seq->begin();
 *     seq->recordUpload(inputBuffer, inputData, nbytes);
 *     seq->recordDispatch(shaderA, groupsA);   // shader bindings/push
 *     seq->recordDispatch(shaderB, groupsB);   // constants already set
 *     seq->recordDownload(outputBuffer, staging, nbytes);
 *     seq->submit();                            // one submit, waits once
 *     // or submitAsync() + poll()/wait() to overlap unrelated CPU work
 *     staging->downloadBytes(dst, nbytes);      // host-visible memcpy
 *
 * A Sequence is reusable after synchronous submit(), or after an asynchronous
 * submission reaches Complete/Failed. Shaders keep their bindings between
 * records; pending work retains no C++ owners, so callers must keep referenced
 * shaders and buffers alive until completion.
 */
class Sequence {
public:
    Sequence();
    ~Sequence();

    Sequence(const Sequence &) = delete;
    Sequence &operator=(const Sequence &) = delete;

    /** True when the active Vulkan or WebGPU backend supports command recording. */
    bool isAvailable() const;

    /** Start recording. Safe to call again after submit() to reuse. */
    void begin();

    /**
     * Record a host->device copy. `src` is memcpy'd into an internal
     * host-visible staging buffer immediately, so it only needs to stay
     * valid until this call returns.
     */
    void recordUpload(GpuBuffer *dst, const void *src, uint64_t nbytes,
                      uint64_t dstOffset = 0);

    /**
     * Record a device->host copy into `staging` (a "staging" usage buffer).
     * Read the staging buffer after submit().
     */
    void recordDownload(GpuBuffer *src, GpuBuffer *staging, uint64_t nbytes,
                        uint64_t srcOffset = 0);

    /** Record a compute dispatch using the shader's current bindings/push constants. */
    void recordDispatch(ComputeShader *shader, int groupsX, int groupsY = 1,
                        int groupsZ = 1);

    /** End recording, submit the whole sequence once, and wait for completion. */
    void submit();

    /**
     * @brief Submit without waiting for GPU completion.
     * @lifetime Buffers and shaders referenced by recorded work must remain alive until
     * poll() or wait() returns Complete/Failed. begin() rejects pending work.
     */
    [[nodiscard]] SequenceStatus submitAsync();

    /** @brief Non-blocking completion query; also retires completed backend resources. */
    [[nodiscard]] SequenceStatus poll();

    /** @brief Wait for pending work and retire its backend resources. */
    [[nodiscard]] SequenceStatus wait();

    /** @brief Current lifecycle state without driving backend event processing. */
    SequenceStatus getStatus() const;

    /** @brief Stable script/debug name for getStatus(). */
    std::string getStatusName() const;

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace eve::gpgpu
