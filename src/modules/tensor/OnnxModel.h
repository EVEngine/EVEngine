#pragma once

#include "common/Result.h"
#include "tensor/OnnxStorage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eve::tensor {
class OnnxCompute;
/** @brief ONNX wire element types; distinct from block-quantized Tensor storage. */
enum class OnnxElement : int { Float32 = 1, UInt8 = 2, Int8 = 3, Int32 = 6, Int64 = 7, Bool = 9 };

/**
 * @brief Owning ONNX boundary tensor: row-major little-endian bytes and exact integer shape.
 * @note No borrowed storage. run() validates caller-provided values before execution.
 *       Scalar shape is empty; zero-sized dimensions are supported. No implicit FP32 integer conversion.
 */
struct OnnxTensor {
    OnnxElement          element = OnnxElement::Float32;
    std::vector<int64_t> shape;
    std::vector<uint8_t> bytes;
};
/** @brief Owning named feed/output value. */
struct OnnxNamedTensor {
    std::string name;
    OnnxTensor  tensor;
};
/** @brief Per-call deterministic RNG and optional strict finite-output diagnostic.
 * @note requireFinite rejects even intentional intermediate infinities/NaNs, so keep it false for Kokoro. */
struct OnnxRunOptions {
    uint64_t seed          = 0;
    bool     requireFinite = false;
};
/** @brief Owning GPU execution output and completed dispatch count. */
struct OnnxGpuResult {
    std::vector<OnnxNamedTensor> outputs;
    size_t                       dispatches = 0;
    OnnxTransferStats            transfers;
};
/** @brief Owning admission report; unsupported nodes remain inspectable but cannot execute. */
struct OnnxModelInfo {
    int64_t                  irVersion = 0;
    size_t                   nodeCount = 0, initializerBytes = 0;
    std::vector<std::string> inputs, outputs;
    std::vector<std::string> unsupportedNodes;
};

/**
 * @brief Native ONNX import and CPU/GPU execution using tensor kernels, without ONNX Runtime.
 * @note Owns immutable model data. Concurrent CPU run calls are independent; no script callbacks,
 *       global RNG or background threads. runGpu uses a caller-provided device on its owning thread.
 *       FP32 results use tolerance comparison. Seeded random excitation is local to a run.
 *       Loading is transactional. Unknown protobuf metadata is ignored, unknown operators
 *       are reported; unsupported semantic features fail explicitly, never become identity.
 */
class OnnxModel {
public:
    /** @brief Destroy model-owned graph and packed initializers; outputs remain valid. */
    ~OnnxModel();
    /**
     * @brief Import bounded in-memory ONNX ModelProto; copies all retained data.
     * @param bytes Borrowed input, not retained. Maximum 512 MiB, 100000 recursive nodes, graph depth 16, rank 6.
     * @return Owning model or ParseError/Unsupported/UnknownVersion; no partial publication.
     * @note Supports ONNX IR 3..10 and default-domain opset 13..17. External data is rejected.
     */
    [[nodiscard]] static Result<std::unique_ptr<OnnxModel>> load(std::span<const uint8_t> bytes);
    /** @brief Return owning graph diagnostics; importing does not imply full operator support. */
    [[nodiscard]] OnnxModelInfo info() const;
    /**
     * @brief Execute selected named values, or graph outputs when requested is empty.
     * @param feeds Borrowed inputs, valid only during this call; never mutated or retained.
     * @param requested Borrowed output names; intermediate outputs may be selected for parity tests.
     * @return Owning outputs or structured node diagnostic; failed calls publish no outputs.
     * @note CPU only. Unsupported dependencies fail before executing the selected subgraph.
     */
    [[nodiscard]] Result<std::vector<OnnxNamedTensor>> run(std::span<const OnnxNamedTensor> feeds,
                                                           std::span<const std::string>     requested = {},
                                                           OnnxRunOptions                   options   = {}) const;

    /**
     * @brief Execute with GPU matrix, convolution, normalization, activation and resampling kernels.
     * @note Shape/index/control operations, padding and LSTM gates execute on CPU.
     * Dynamic quantization reads three validation scalars; activations remain on GPU.
     * Integer GEMM/Conv, LSTM projections, FP32 GEMM/ConvTranspose, elementwise math,
     * Softmax, sum/mean reductions, normalization, Resize and CumSum execute on GPU.
     * The borrowed compute provider must outlive this synchronous call; use its device thread.
     * No automatic CPU retry occurs after a GPU error. Outputs own their storage.
     */
    [[nodiscard]] Result<OnnxGpuResult> runGpu(std::span<const OnnxNamedTensor> feeds, OnnxCompute& compute,
                                               std::span<const std::string> requested = {},
                                               OnnxRunOptions               options   = {}) const;

private:
    [[nodiscard]] Result<std::vector<OnnxNamedTensor>> runInternal(std::span<const OnnxNamedTensor> feeds,
                                                                   std::span<const std::string>     requested,
                                                                   OnnxCompute* compute, OnnxRunOptions options) const;
    struct Impl;
    explicit OnnxModel(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::tensor
