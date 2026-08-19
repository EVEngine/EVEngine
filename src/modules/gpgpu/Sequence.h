#pragma once

#include <cstdint>

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

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
 *     staging->downloadBytes(dst, nbytes);      // host-visible memcpy
 *
 * A Sequence is reusable: call begin() again after submit(). Shaders keep
 * their bindings between records; only buffers they reference may be
 * re-bound between dispatches.
 */
class Sequence {
public:
    Sequence();
    ~Sequence();

    Sequence(const Sequence &) = delete;
    Sequence &operator=(const Sequence &) = delete;

    /** True when the active backend (Vulkan) supports command recording. */
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

private:
    struct Impl;
    Impl *impl_ = nullptr;
};

}  // namespace eve::gpgpu
