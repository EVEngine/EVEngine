#pragma once
#include "tensor/AffineQuant.h"
#include "tensor/OnnxCompute.h"
namespace eve::tensor::onnx_detail {
// Internal kernels; callers validate geometry and byte extents before dispatch.
/** @brief Gpu matmul. */
std::vector<int32_t> gpuMatmul(OnnxCompute& device, affine::ByteView a, affine::ByteView b, size_t m, size_t k,
                               size_t n, int az, std::span<const int32_t> bz);
/** @brief Gpu conv. */
std::vector<int32_t> gpuConv(OnnxCompute& device, affine::ByteView x, affine::ByteView w, const affine::ConvShape& s,
                             int xz, std::span<const int32_t> wz);
/** @brief Gpu matmul resident. */
OnnxBuffer           gpuMatmulResident(OnnxCompute&, OnnxBuffer, OnnxBuffer, bool, bool, size_t, size_t, size_t, int,
                                       std::span<const int32_t>, size_t bOffset = 0);
/** @brief Gpu conv resident. */
OnnxBuffer           gpuConvResident(OnnxCompute&, OnnxBuffer, OnnxBuffer, bool, bool, const affine::ConvShape&, int,
                                     std::span<const int32_t>);
}  // namespace eve::tensor::onnx_detail
