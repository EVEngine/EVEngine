#pragma once
#include "tensor/AffineQuant.h"
namespace eve::tensor::affine::detail {
/** @brief ConvExtent public API. */
struct ConvExtent {
    int64_t height, width, count;
};
/** @brief Validate conv. */
Result<ConvExtent> validateConv(size_t xSize, size_t wSize, bool xSigned, bool wSigned, const ConvShape&, int,
                                std::span<const int32_t>);
}  // namespace eve::tensor::affine::detail
