#include "tensor/AffineQuant.h"
#include "tensor/AffineQuantInternal.h"
#include "tensor/OnnxGpuKernels.h"
#include "tensor/OnnxInternal.h"

#include <algorithm>
#include <cmath>

namespace eve::tensor::onnx_detail {
namespace {
affine::ByteView bytes(const RuntimeTensor& t) {
    if (t.element != OnnxElement::Int8 && t.element != OnnxElement::UInt8)
        throw Failure("Expected packed int8/uint8 tensor");
    return {t.bytes, t.element == OnnxElement::Int8};
}
template <class T>
T checked(Result<T> result) {
    if (!result.ok()) throw Failure(result.error()->message(), result.error()->code());
    return std::move(result.value());
}
int zero(const std::vector<const RuntimeTensor*>& in, size_t i, const RuntimeTensor& owner) {
    if (i >= in.size() || !in[i]) return 0;
    if (in[i]->element != owner.element || count(in[i]->shape) != 1)
        throw Failure("Expected matching scalar zero point");
    return static_cast<int>(integer(*in[i]));
}
}  // namespace
std::vector<RuntimeTensor> executeQuant(const Node& n, const std::vector<const RuntimeTensor*>& in,
                                        OnnxCompute* compute) {
    const auto& x = required(in, 0);
    if (n.op == "DynamicQuantizeLinear") {
        if (compute && count(x.shape)) {
            if (x.element != OnnxElement::Float32) throw Failure("Expected FP32 quantization input");
            const size_t total = count(x.shape);
            auto         stats = dispatchFloat(
                *compute, {&x}, {3},
                "float lo=0.0,hi=0.0,invalid=0.0;for(uint j=0;j<" + std::to_string(total) +
                    "u;++j){float v=x0[j];if(isinf(v))invalid=1.0;if(!isnan(v)){lo=min(lo,v);hi=max(hi,v);}}float "
                            "s=(hi==lo)?1.0:(hi-lo)/255.0;y[0]=s;y[1]=clamp(roundEven(-lo/s),0.0,255.0);y[2]=invalid;",
                1);
            auto values = floats(stats);
            if (values[2] != 0 || !std::isfinite(values[0]) || !(values[0] > 0))
                throw Failure("Nonfinite quantization input/range");
            auto        scale     = make(OnnxElement::Float32, {}, std::vector<float>{values[0]});
            auto        zeroPoint = make(OnnxElement::UInt8, {}, std::vector<uint8_t>{static_cast<uint8_t>(values[1])});
            auto        params    = make(OnnxElement::Float32, {2}, std::vector<float>{values[0], values[1]});
            std::string source =
                "#version 450\nlayout(local_size_x=64)in;layout(std430,binding=0)readonly buffer X{float "
                "x[];};layout(std430,binding=1)readonly buffer P{float p[];};layout(std430,binding=2)writeonly buffer "
                "Y{uint y[];};void main(){uint word=gl_GlobalInvocationID.x;if(word>=" +
                std::to_string((total + 3) / 4) +
                "u)return;uint packed=0u;for(uint c=0u;c<4u;++c){uint j=word*4u+c;if(j<" + std::to_string(total) +
                "u){float v=x[j];uint "
                "q=isnan(v)?0u:uint(clamp(roundEven(v/p[0])+p[1],0.0,255.0));packed|=q<<(c*8u);}}y[word]=packed;}";
            const std::vector<OnnxBuffer> inputs{x.bytes.buffer(), params.bytes.buffer()};
            auto q = checked(compute->enqueue(source, inputs, total, static_cast<uint32_t>((total + 3) / 4)));
            return {{OnnxElement::UInt8, x.shape, std::move(q)}, std::move(scale), std::move(zeroPoint)};
        }
        auto input = floats(x);
        // ORT CPU DynamicQuantizeLinear ignores NaN for range estimation and stores code 0.
        // Exported Kokoro phase graphs produce 0/0 at zero-amplitude STFT bins.
        std::vector<size_t> nanPositions;
        for (size_t i = 0; i < input.size(); ++i)
            if (std::isnan(input[i])) {
                nanPositions.push_back(i);
                input[i] = 0;
            }
        auto q = checked(affine::dynamicQuantize(input));
        for (auto i : nanPositions) q.values[i] = 0;
        return {{OnnxElement::UInt8, x.shape, std::move(q.values)},
                make(OnnxElement::Float32, {}, std::vector<float>{q.scale}),
                make(OnnxElement::UInt8, {}, std::vector<uint8_t>{q.zeroPoint})};
    }
    if (n.op == "QuantizeLinear" || n.op == "DequantizeLinear") {
        const auto scales = floats(required(in, 1));
        if (required(in, 1).shape.size() > 1) throw Failure("Quantization scale must be scalar or vector");
        if (scales.empty()) throw Failure("Empty quantization scales");
        const RuntimeTensor* zp   = in.size() > 2 ? in[2] : nullptr;
        const auto           type = n.op == "DequantizeLinear" ? x.element : (zp ? zp->element : OnnxElement::UInt8);
        if (type != OnnxElement::UInt8 && type != OnnxElement::Int8)
            throw Failure("Unsupported quantization dtype", DiagnosticCode::Unsupported);
        if (zp && (zp->element != type || zp->shape != required(in, 1).shape))
            throw Failure("Scale/zero point mismatch");
        std::vector<int32_t> zeros(scales.size(), 0);
        if (zp)
            for (size_t i = 0; i < zeros.size(); ++i) zeros[i] = static_cast<int32_t>(integer(*zp, i));
        size_t inner = 1;
        if (scales.size() > 1) {
            const int a = axis(attr(n, "axis", 1), x.shape.size());
            if (static_cast<size_t>(x.shape[a]) != scales.size()) throw Failure("Scale channel count mismatch");
            for (size_t i = a + 1; i < x.shape.size(); ++i) inner *= x.shape[i];
        }
        if (n.op == "DequantizeLinear")
            return {make(OnnxElement::Float32, x.shape, checked(affine::dequantize(bytes(x), scales, zeros, inner)))};
        const auto           values = floats(x);
        std::vector<uint8_t> out(values.size());
        for (size_t i = 0; i < values.size(); i += inner) {
            const size_t channel = (i / inner) % scales.size();
            auto q = checked(affine::quantize(std::span(values).subspan(i, inner), scales[channel], zeros[channel],
                                              type == OnnxElement::Int8));
            std::copy(q.begin(), q.end(), out.begin() + i);
        }
        return {{type, x.shape, std::move(out)}};
    }
    if (n.op == "MatMulInteger") {
        const auto& b = required(in, 1);
        if (x.shape.size() < 2 || b.shape.size() != 2)
            throw Failure("MatMulInteger requires rank >=2 A and rank 2 B", DiagnosticCode::Unsupported);
        const size_t k = x.shape.back(), columns = b.shape[1];
        if (k != static_cast<size_t>(b.shape[0]) || k == 0) throw Failure("Invalid MatMulInteger inner dimension");
        const size_t rows  = count(x.shape) / k;
        auto         shape = x.shape;
        shape.back()       = columns;
        if (compute) {
            if ((x.element != OnnxElement::Int8 && x.element != OnnxElement::UInt8) ||
                (b.element != OnnxElement::Int8 && b.element != OnnxElement::UInt8))
                throw Failure("Expected packed integer matrices");
            std::vector<int32_t> bz{zero(in, 3, b)};
            return {{OnnxElement::Int32, shape,
                     gpuMatmulResident(*compute, x.bytes.buffer(), b.bytes.buffer(), x.element == OnnxElement::Int8,
                                       b.element == OnnxElement::Int8, rows, k, columns, zero(in, 2, x), bz)}};
        }
        return {make(
            OnnxElement::Int32, shape,
            checked(affine::matmul(bytes(x), bytes(b), rows, k, columns, zero(in, 2, x), zero(in, 3, b), compute)))};
    }
    if (n.op == "ConvInteger") {
        const auto& w = required(in, 1);
        if ((x.shape.size() != 3 && x.shape.size() != 4) || w.shape.size() != x.shape.size())
            throw Failure("ConvInteger supports 1D/2D NCHW", DiagnosticCode::Unsupported);
        const size_t spatial = x.shape.size() - 2;
        if (n.attrs.contains("kernel_shape") &&
            n.attrs.at("kernel_shape").integers != std::vector<int64_t>(w.shape.begin() + 2, w.shape.end()))
            throw Failure("Convolution kernel_shape mismatch");
        auto strides   = attrs(n, "strides", std::vector<int64_t>(spatial, 1));
        auto pads      = attrs(n, "pads", std::vector<int64_t>(2 * spatial, 0));
        auto dilations = attrs(n, "dilations", std::vector<int64_t>(spatial, 1));
        if (strides.size() != spatial || pads.size() != 2 * spatial || dilations.size() != spatial)
            throw Failure("Invalid convolution attributes");
        auto narrow = [](int64_t v) {
            if (v < 0 || v > 65536) throw Failure("Convolution attribute out of range");
            return static_cast<int>(v);
        };
        affine::ConvShape s;
        s.batch     = narrow(x.shape[0]);
        s.channels  = narrow(x.shape[1]);
        s.outputs   = narrow(w.shape[0]);
        s.width     = narrow(x.shape.back());
        s.kernelW   = narrow(w.shape.back());
        s.strideW   = narrow(strides.back());
        s.dilationW = narrow(dilations.back());
        s.padLeft   = narrow(pads[spatial - 1]);
        s.padRight  = narrow(pads.back());
        s.groups    = narrow(attr(n, "group", 1));
        if (spatial == 2) {
            s.height    = narrow(x.shape[2]);
            s.kernelH   = narrow(w.shape[2]);
            s.strideH   = narrow(strides[0]);
            s.dilationH = narrow(dilations[0]);
            s.padTop    = narrow(pads[0]);
            s.padBottom = narrow(pads[2]);
        }
        if (!s.groups || s.channels % s.groups || w.shape[1] != s.channels / s.groups)
            throw Failure("Convolution channel mismatch");
        std::vector<int32_t> wz{0};
        if (in.size() > 3 && in[3]) {
            if (in[3]->element != w.element) throw Failure("Weight zero-point type mismatch");
            wz.clear();
            for (auto z : ints(*in[3])) wz.push_back(static_cast<int32_t>(z));
        }
        if (compute) {
            if ((x.element != OnnxElement::Int8 && x.element != OnnxElement::UInt8) ||
                (w.element != OnnxElement::Int8 && w.element != OnnxElement::UInt8))
                throw Failure("Expected packed convolution inputs");
            const auto extent =
                checked(affine::detail::validateConv(x.bytes.size(), w.bytes.size(), x.element == OnnxElement::Int8,
                                                     w.element == OnnxElement::Int8, s, zero(in, 2, x), wz));
            std::vector<int64_t> outputShape{s.batch, s.outputs};
            if (spatial == 2) outputShape.push_back(extent.height);
            outputShape.push_back(extent.width);
            return {{OnnxElement::Int32, outputShape,
                     gpuConvResident(*compute, x.bytes.buffer(), w.bytes.buffer(), x.element == OnnxElement::Int8,
                                     w.element == OnnxElement::Int8, s, zero(in, 2, x), wz)}};
        }
        auto                 values = checked(affine::conv(bytes(x), bytes(w), s, zero(in, 2, x), wz, compute));
        std::vector<int64_t> shape{s.batch, s.outputs};
        if (spatial == 2)
            shape.push_back(
                (s.height + s.padTop + s.padBottom - int64_t(s.dilationH) * (s.kernelH - 1) - 1) / s.strideH + 1);
        shape.push_back((s.width + s.padLeft + s.padRight - int64_t(s.dilationW) * (s.kernelW - 1) - 1) / s.strideW +
                        1);
        return {make(OnnxElement::Int32, shape, values)};
    }
    throw Failure("Unsupported quantized operation", DiagnosticCode::Unsupported);
}
}  // namespace eve::tensor::onnx_detail
