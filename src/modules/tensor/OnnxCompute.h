#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "common/Result.h"
#include "tensor/OnnxStorage.h"

namespace eve::tensor {
/** @brief Synchronous GPU kernel request; all inputs are borrowed only until dispatch returns. */
struct OnnxKernel {
    std::string                           source;
    std::vector<std::span<const uint8_t>> inputs;
    size_t                                outputBytes = 0;
    uint32_t                              workItems   = 0;
};
/**
 * @brief GPU execution boundary for native ONNX; retains no model and retains compiled resources for the lifetime of
 * the provider.
 * @note Use on the device's owning thread, without concurrent calls or reentrancy.
 * dispatch completes GPU work before returning host bytes; enqueue may defer completion. Errors never
 * trigger CPU execution. Integer kernels require exact int32 results.
 */
class OnnxCompute {
public:
    /** @brief Release device resources after all synchronous calls have returned. */
    virtual ~OnnxCompute() = default;
    /** @brief Compatibility-only synchronous host-byte entry point; engine dispatch delegates to enqueue.
     * @note GLSL 450, local_size_x=64; inputs occupy bindings 0..N-1, output N. */
    [[nodiscard]] virtual Result<std::vector<uint8_t>> dispatch(const OnnxKernel& kernel) = 0;

    /** @brief Queue a kernel using immutable host/device buffers; no readback is required.
     * @note Default provider executes synchronously through dispatch. Engine provider records a
     * resident graph segment inside runGpu, completing it at a host-read or resource-budget boundary.
     * Inputs are retained until completion; returned device storage expires when runGpu ends. */
    [[nodiscard]] virtual Result<OnnxBuffer> enqueue(const std::string& source, std::span<const OnnxBuffer> inputs,
                                                     size_t outputBytes, uint32_t workItems) {
        OnnxKernel                        k{source, {}, outputBytes, workItems};
        std::vector<std::vector<uint8_t>> copies;
        copies.reserve(inputs.size());
        for (const auto& input : inputs) {
            if (input.host)
                k.inputs.push_back(*input.host);
            else {
                if (!input.device)
                    return Result<OnnxBuffer>::failure(
                        Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing GPU input"));
                auto r = input.device->readback();
                if (!r.ok()) return Result<OnnxBuffer>::failure(r.status());
                copies.push_back(std::move(r.value()));
                k.inputs.push_back(copies.back());
            }
        }
        auto r = dispatch(k);
        if (!r.ok()) return Result<OnnxBuffer>::failure(r.status());
        auto bytes = std::make_shared<const std::vector<uint8_t>>(std::move(r.value()));
        return Result<OnnxBuffer>::success({bytes, {}, bytes->size()});
    }
    /** @brief Observable transfer counters for the current or most recent run. */
    virtual OnnxTransferStats transferStats() const { return {}; }

protected:
    friend class OnnxModel;
    /** @brief Start a synchronous run cache scope; invoked only by OnnxModel on the device thread. */
    virtual void beginRun() noexcept {}
    /** @brief Retire temporary activations/recordings; compiled resources may survive for later calls. */
    virtual void endRun() noexcept {}
};
/**
 * @brief Create a reusable GPU session for the active engine Gpgpu Vulkan device, or an error.
 * @note Retain and reuse this owning provider across runGpu calls. Pipelines, immutable initializers
 * and buffer pools persist until provider destruction or Graphics resource retirement. Cache bounds:
 * 1024 shader variants, 128 MiB initializer payload and 512 MiB device buffers.
 * CPU shader compilation uses a
 * bounded queue; pipeline creation and submission remain on the device thread.
 * @param compilerWorkers Number of CPU
 * compiler workers (1..8); default 4. Source is owned by each job.
 * @lifetime Graphics retirement clears device
 * resources before device destruction; either destruction order is supported. A retired session rejects further GPU
 * work; create a new session for a new device.
 * @thread Graphics thread only, no concurrent calls, reentrancy or device destruction during runGpu.
 */
[[nodiscard]] Result<std::unique_ptr<OnnxCompute>> createOnnxGpuCompute(uint32_t compilerWorkers = 4);
}  // namespace eve::tensor
