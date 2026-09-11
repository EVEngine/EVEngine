#pragma once

#include "common/Result.h"

#include <cstdint>
#include <span>
#include <vector>

namespace eve::tensor {
class OnnxCompute;
}
namespace eve::tensor::affine {

/** @brief ONNX unsigned activation quantization; owns bytes and scalar parameters. */
struct QuantizedActivation {
    std::vector<uint8_t> values;
    float                scale     = 1;
    uint8_t              zeroPoint = 0;
};

/** @brief Borrowed packed 8-bit values, valid for the duration of a synchronous call. */
struct ByteView {
    std::span<const uint8_t> bytes;
    bool                     signedValues = false;
};

/**
 * @brief Quantize finite FP32 activations with ONNX DynamicQuantizeLinear semantics.
 * @param input Borrowed flat input; not retained.
 * @return Owning uint8 values, scale, zero point; InvalidArgument for nonfinite input.
 * @note CPU, thread-safe, no callbacks; nearest-even rounding independent of fenv.
 */
[[nodiscard]] Result<QuantizedActivation> dynamicQuantize(std::span<const float> input);

/**
 * @brief Affine quantization to int8/uint8 bytes, saturating and rounding ties to even.
 * @return Owning packed bytes; rejects invalid scale, zero point or nonfinite input.
 * @note CPU, thread-safe; spans are borrowed only during the call.
 */
[[nodiscard]] Result<std::vector<uint8_t>> quantize(std::span<const float> input, float scale, int zeroPoint,
                                                    bool signedValues);

/**
 * @brief Affine dequantization using scalar or per-axis scale/zero point.
 * @param inner Number of contiguous values following the channel axis.
 * @return Owning FP32 values; InvalidArgument on inconsistent channel layout.
 * @note CPU, thread-safe; all views are borrowed for the call, no callbacks.
 */
[[nodiscard]] Result<std::vector<float>> dequantize(ByteView input, std::span<const float> scales,
                                                    std::span<const int32_t> zeros, size_t inner = 1);

/**
 * @brief Integer row-major [M,K] x [K,N], subtracting scalar zero points.
 * @return Exact owning int32 accumulators; rejects dimension and int32 overflow.
 * @param compute Optional borrowed GPU provider; null selects CPU. No retry on GPU failure.
 * @note Synchronous. CPU is thread-safe; GPU uses the provider thread. Weights remain packed.
 */
[[nodiscard]] Result<std::vector<int32_t>> matmul(ByteView a, ByteView b, size_t m, size_t k, size_t n, int aZero = 0,
                                                  int bZero = 0, OnnxCompute* compute = nullptr);

/** @brief Explicit 1D/2D NCHW convolution geometry; missing 1D height is one. */
struct ConvShape {
    int batch = 1, channels = 1, height = 1, width = 1;
    int outputs = 1, kernelH = 1, kernelW = 1;
    int strideH = 1, strideW = 1, dilationH = 1, dilationW = 1;
    int padTop = 0, padLeft = 0, padBottom = 0, padRight = 0, groups = 1;
};

/**
 * @brief Integer Conv, supporting groups, asymmetric padding and dilation.
 * @return Owning NCHW int32 values; rejects bad geometry or accumulator overflow.
 * @param compute Optional borrowed GPU provider; null selects CPU. No retry on GPU failure.
 * @note Synchronous. CPU is thread-safe; GPU uses the provider thread. Padding represents real zero.
 */
[[nodiscard]] Result<std::vector<int32_t>> conv(ByteView x, ByteView w, const ConvShape& shape, int xZero,
                                                std::span<const int32_t> wZeros, OnnxCompute* compute = nullptr);
}  // namespace eve::tensor::affine
