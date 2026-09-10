#pragma once
#include "tensor/AffineQuant.h"
namespace eve::tensor::affine::detail {
struct ConvExtent {
    int64_t height, width, count;
};
Result<ConvExtent> validateConv(size_t xSize, size_t wSize, bool xSigned, bool wSigned, const ConvShape&, int,
                                std::span<const int32_t>);
}  // namespace eve::tensor::affine::detail
