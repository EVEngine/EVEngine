#include "tensor/AffineQuant.h"
#include "tensor/AffineQuantInternal.h"
#include "tensor/OnnxGpuKernels.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::tensor::affine {
namespace {
template <class T>
Result<T> fail(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
int value(ByteView v, size_t i) {
    const int x = v.bytes[i];
    return v.signedValues && x >= 128 ? x - 256 : x;
}
bool   validZero(int z, bool sign) { return sign ? z >= -128 && z <= 127 : z >= 0 && z <= 255; }
double evenRound(double x) {
    const double lo = std::floor(x), fraction = x - lo;
    return lo + (fraction > 0.5 || (fraction == 0.5 && std::fmod(lo, 2.0) != 0));
}
bool fits(int64_t n) { return n >= INT32_MIN && n <= INT32_MAX; }
bool product(size_t a, size_t b, size_t limit) { return b == 0 || a <= limit / b; }
}  // namespace

Result<std::vector<uint8_t>> quantize(std::span<const float> input, float scale, int zero, bool sign) {
    if (!(scale > 0) || !std::isfinite(scale) || !validZero(zero, sign))
        return fail<std::vector<uint8_t>>("Invalid affine scale or zero point");
    std::vector<uint8_t> out;
    out.reserve(input.size());
    for (float x : input) {
        if (!std::isfinite(x)) return fail<std::vector<uint8_t>>("Nonfinite quantization input");
        const double rounded = evenRound(static_cast<double>(x) / scale) + zero;
        const int    q       = static_cast<int>(std::clamp(rounded, sign ? -128.0 : 0.0, sign ? 127.0 : 255.0));
        out.push_back(static_cast<uint8_t>(q));
    }
    return Result<std::vector<uint8_t>>::success(std::move(out));
}

Result<QuantizedActivation> dynamicQuantize(std::span<const float> input) {
    float lo = 0, hi = 0;
    for (float x : input) {
        if (!std::isfinite(x)) return fail<QuantizedActivation>("Nonfinite dynamic quantization input");
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    QuantizedActivation result;
    result.scale = hi == lo ? 1.f : static_cast<float>((static_cast<double>(hi) - lo) / 255.0);
    if (!(result.scale > 0) || !std::isfinite(result.scale))
        return fail<QuantizedActivation>("Dynamic quantization scale is not representable");
    result.zeroPoint = static_cast<uint8_t>(std::clamp(evenRound(-static_cast<double>(lo) / result.scale), 0.0, 255.0));
    auto bytes       = quantize(input, result.scale, result.zeroPoint, false);
    if (!bytes.ok()) return Result<QuantizedActivation>::failure(bytes.status());
    result.values = std::move(bytes.value());
    return Result<QuantizedActivation>::success(std::move(result));
}

Result<std::vector<float>> dequantize(ByteView input, std::span<const float> scales, std::span<const int32_t> zeros,
                                      size_t inner) {
    if (scales.empty() || scales.size() != zeros.size() || inner == 0 || !product(scales.size(), inner, SIZE_MAX) ||
        input.bytes.size() % (scales.size() * inner))
        return fail<std::vector<float>>("Invalid affine channel layout");
    for (size_t i = 0; i < scales.size(); ++i)
        if (!(scales[i] > 0) || !std::isfinite(scales[i]) || !validZero(zeros[i], input.signedValues))
            return fail<std::vector<float>>("Invalid affine scale or zero point");
    std::vector<float> out(input.bytes.size());
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t channel = (i / inner) % scales.size();
        out[i]               = static_cast<float>(value(input, i) - zeros[channel]) * scales[channel];
    }
    return Result<std::vector<float>>::success(std::move(out));
}

Result<std::vector<int32_t>> matmul(ByteView a, ByteView b, size_t m, size_t k, size_t n, int aZero, int bZero,
                                    OnnxCompute* compute) {
    if (!validZero(aZero, a.signedValues) || !validZero(bZero, b.signedValues) || !product(m, k, SIZE_MAX) ||
        !product(k, n, SIZE_MAX) || !product(m, n, INT32_MAX) || a.bytes.size() != m * k || b.bytes.size() != k * n)
        return fail<std::vector<int32_t>>("Invalid integer matmul dimensions or zero points");
    if (compute && m && n && k) {
        try {
            const int32_t z = bZero;
            return Result<std::vector<int32_t>>::success(
                onnx_detail::gpuMatmul(*compute, a, b, m, k, n, aZero, {&z, 1}));
        } catch (const std::exception& e) {
            return Result<std::vector<int32_t>>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what()));
        }
    }
    std::vector<int32_t> out(m * n);
    for (size_t r = 0; r < m; ++r)
        for (size_t c = 0; c < n; ++c) {
            int64_t sum = 0;
            for (size_t j = 0; j < k; ++j)
                sum += static_cast<int64_t>(value(a, r * k + j) - aZero) * (value(b, j * n + c) - bZero);
            if (!fits(sum)) return fail<std::vector<int32_t>>("Integer matmul accumulator overflow");
            out[r * n + c] = static_cast<int32_t>(sum);
        }
    return Result<std::vector<int32_t>>::success(std::move(out));
}

Result<detail::ConvExtent> detail::validateConv(size_t xSize, size_t wSize, bool xSigned, bool wSigned,
                                                const ConvShape& s, int xZero, std::span<const int32_t> wZeros) {
    for (int d : {s.batch, s.channels, s.height, s.width, s.outputs, s.kernelH, s.kernelW, s.strideH, s.strideW,
                  s.dilationH, s.dilationW, s.groups})
        if (d <= 0 || d > 65536) return fail<detail::ConvExtent>("Invalid integer convolution dimensions");
    if (s.channels % s.groups || s.outputs % s.groups || s.padTop < 0 || s.padBottom < 0 || s.padLeft < 0 ||
        s.padRight < 0 || !validZero(xZero, xSigned) ||
        (wZeros.size() != 1 && wZeros.size() != static_cast<size_t>(s.outputs)))
        return fail<detail::ConvExtent>("Invalid convolution groups, padding or zero points");
    for (int z : wZeros)
        if (!validZero(z, wSigned)) return fail<detail::ConvExtent>("Invalid convolution weight zero point");
    const int64_t h = static_cast<int64_t>(s.height) + s.padTop + s.padBottom -
                      static_cast<int64_t>(s.dilationH) * (s.kernelH - 1) - 1;
    const int64_t width = static_cast<int64_t>(s.width) + s.padLeft + s.padRight -
                          static_cast<int64_t>(s.dilationW) * (s.kernelW - 1) - 1;
    if (h < 0 || width < 0) return fail<detail::ConvExtent>("Convolution kernel exceeds padded input");
    const int64_t oh = h / s.strideH + 1, ow = width / s.strideW + 1;
    int64_t       outCount = 1;
    for (int64_t d : {static_cast<int64_t>(s.batch), static_cast<int64_t>(s.outputs), oh, ow}) {
        if (d > INT32_MAX / outCount) return fail<detail::ConvExtent>("Convolution output too large");
        outCount *= d;
    }
    const int cg         = s.channels / s.groups;
    size_t    inputCount = 1, weightCount = 1;
    for (int d : {s.batch, s.channels, s.height, s.width}) {
        if (!product(inputCount, d, INT32_MAX)) return fail<detail::ConvExtent>("Convolution input too large");
        inputCount *= d;
    }
    for (int d : {s.outputs, cg, s.kernelH, s.kernelW}) {
        if (!product(weightCount, d, INT32_MAX)) return fail<detail::ConvExtent>("Convolution weights too large");
        weightCount *= d;
    }
    if (outCount > INT32_MAX || xSize != inputCount || wSize != weightCount)
        return fail<detail::ConvExtent>("Convolution buffer size mismatch or output too large");
    return Result<detail::ConvExtent>::success({oh, ow, outCount});
}
Result<std::vector<int32_t>> conv(ByteView x, ByteView w, const ConvShape& s, int xZero,
                                  std::span<const int32_t> wZeros, OnnxCompute* compute) {
    auto extent =
        detail::validateConv(x.bytes.size(), w.bytes.size(), x.signedValues, w.signedValues, s, xZero, wZeros);
    if (!extent.ok()) return Result<std::vector<int32_t>>::failure(extent.status());
    const auto [oh, ow, outCount] = extent.value();
    const int cg                  = s.channels / s.groups;
    if (compute) {
        try {
            return Result<std::vector<int32_t>>::success(onnx_detail::gpuConv(*compute, x, w, s, xZero, wZeros));
        } catch (const std::exception& e) {
            return Result<std::vector<int32_t>>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what()));
        }
    }
    std::vector<int32_t> out(static_cast<size_t>(outCount));
    for (int b = 0; b < s.batch; ++b)
        for (int o = 0; o < s.outputs; ++o)
            for (int64_t y = 0; y < oh; ++y)
                for (int64_t z = 0; z < ow; ++z) {
                    int64_t sum = 0;
                    for (int c = 0; c < cg; ++c)
                        for (int ky = 0; ky < s.kernelH; ++ky)
                            for (int kx = 0; kx < s.kernelW; ++kx) {
                                const int64_t iy = y * s.strideH - s.padTop + static_cast<int64_t>(ky) * s.dilationH;
                                const int64_t ix = z * s.strideW - s.padLeft + static_cast<int64_t>(kx) * s.dilationW;
                                if (iy < 0 || ix < 0 || iy >= s.height || ix >= s.width) continue;
                                const size_t xi =
                                    ((static_cast<size_t>(b) * s.channels + (o / (s.outputs / s.groups)) * cg + c) *
                                         s.height +
                                     iy) *
                                        s.width +
                                    ix;
                                const size_t wi = ((static_cast<size_t>(o) * cg + c) * s.kernelH + ky) * s.kernelW + kx;
                                sum += static_cast<int64_t>(value(x, xi) - xZero) *
                                       (value(w, wi) - wZeros[wZeros.size() == 1 ? 0 : o]);
                            }
                    if (!fits(sum)) return fail<std::vector<int32_t>>("Integer convolution accumulator overflow");
                    out[((static_cast<size_t>(b) * s.outputs + o) * oh + y) * ow + z] = static_cast<int32_t>(sum);
                }
    return Result<std::vector<int32_t>>::success(std::move(out));
}
}  // namespace eve::tensor::affine
