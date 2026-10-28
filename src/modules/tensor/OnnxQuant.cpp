#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include "tensor/AffineQuant.h"
#include "tensor/AffineQuantInternal.h"
#include "tensor/OnnxGpuKernels.h"
#include "tensor/OnnxInternal.h"

// OnnxQuant.cpp
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

// OnnxReader.cpp
namespace eve::tensor::onnx_detail {
namespace {
struct Field {
    int                      id = 0, wire = 0;
    uint64_t                 number = 0;
    std::span<const uint8_t> bytes;
    std::string              text() const { return {reinterpret_cast<const char*>(bytes.data()), bytes.size()}; }
};
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}
    bool     empty() const { return offset_ == data_.size(); }
    uint64_t varint() {
        uint64_t n = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            if (offset_ == data_.size()) throw Failure("Truncated protobuf varint", DiagnosticCode::ParseError);
            const uint8_t b = data_[offset_++];
            if (shift == 63 && b > 1) throw Failure("Overflowing protobuf varint", DiagnosticCode::ParseError);
            n |= static_cast<uint64_t>(b & 127) << shift;
            if (!(b & 128)) return n;
        }
        throw Failure("Invalid protobuf varint", DiagnosticCode::ParseError);
    }
    std::span<const uint8_t> take(uint64_t n) {
        if (n > data_.size() - offset_) throw Failure("Truncated protobuf field", DiagnosticCode::ParseError);
        auto result = data_.subspan(offset_, static_cast<size_t>(n));
        offset_ += static_cast<size_t>(n);
        return result;
    }
    Field next() {
        const uint64_t tag = varint();
        if ((tag >> 3) == 0 || (tag >> 3) > 0x1fffffff)
            throw Failure("Invalid protobuf tag", DiagnosticCode::ParseError);
        Field f;
        f.id   = static_cast<int>(tag >> 3);
        f.wire = tag & 7;
        switch (f.wire) {
            case 0: f.number = varint(); break;
            case 1: f.bytes = take(8); break;
            case 2: f.bytes = take(varint()); break;
            case 5: f.bytes = take(4); break;
            default: throw Failure("Unsupported protobuf wire type", DiagnosticCode::ParseError);
        }
        return f;
    }

private:
    std::span<const uint8_t> data_;
    size_t                   offset_ = 0;
};
void wire(const Field& f, int expected) {
    if (f.wire != expected) throw Failure("Unexpected protobuf field type", DiagnosticCode::ParseError);
}
void integers(const Field& f, std::vector<int64_t>& out) {
    if (f.wire == 0)
        out.push_back(std::bit_cast<int64_t>(f.number));
    else {
        wire(f, 2);
        Reader r(f.bytes);
        while (!r.empty()) out.push_back(std::bit_cast<int64_t>(r.varint()));
    }
}
void reals(const Field& f, std::vector<float>& out) {
    if (f.wire != 2 && f.wire != 5) throw Failure("Invalid float wire type", DiagnosticCode::ParseError);
    if (f.bytes.size() % 4) throw Failure("Truncated float payload", DiagnosticCode::ParseError);
    for (size_t i = 0; i < f.bytes.size(); i += 4) {
        float x;
        std::memcpy(&x, f.bytes.data() + i, 4);
        out.push_back(x);
    }
}
std::pair<std::string, RuntimeTensor> tensor(std::span<const uint8_t> data) {
    Reader               r(data);
    RuntimeTensor        v;
    std::string          name;
    std::vector<int64_t> iv;
    std::vector<float>   fv;
    bool                 raw = false, type = false;
    while (!r.empty()) {
        auto f = r.next();
        switch (f.id) {
            case 1: integers(f, v.shape); break;
            case 2:
                wire(f, 0);
                if (f.number > 9) throw Failure("Unsupported tensor element type", DiagnosticCode::Unsupported);
                v.element = static_cast<OnnxElement>(f.number);
                type      = true;
                break;
            case 4: reals(f, fv); break;
            case 5:
            case 7: integers(f, iv); break;
            case 8:
                wire(f, 2);
                name = f.text();
                break;
            case 9:
                wire(f, 2);
                if (raw) throw Failure("Duplicate raw tensor data", DiagnosticCode::ParseError);
                v.bytes.assign(f.bytes.begin(), f.bytes.end());
                raw = true;
                break;
            case 14:
                wire(f, 0);
                if (f.number != 0) throw Failure("External tensor data unsupported", DiagnosticCode::Unsupported);
                break;
            case 3:
            case 6:
            case 10:
            case 11:
            case 13: throw Failure("External, segmented or unsupported tensor data", DiagnosticCode::Unsupported);
            default: break;
        }
    }
    if (!type) throw Failure("Tensor has no dtype", DiagnosticCode::ParseError);
    const size_t n = count(v.shape), size = elementSize(v.element);
    if (raw && (!iv.empty() || !fv.empty())) throw Failure("Ambiguous tensor payload", DiagnosticCode::ParseError);
    if (!raw) {
        if (v.element == OnnxElement::Float32) {
            if (fv.size() != n || !iv.empty())
                throw Failure("Invalid float tensor payload", DiagnosticCode::ParseError);
            v.bytes.resize(n * size);
            if (n) std::memcpy(v.bytes.data(), fv.data(), v.bytes.size());
        } else {
            if (iv.size() != n || !fv.empty())
                throw Failure("Invalid integer tensor payload", DiagnosticCode::ParseError);
            v.bytes.resize(n * size);
            for (size_t i = 0; i < n; ++i) {
                const auto x = iv[i];
                if ((v.element == OnnxElement::UInt8 && (x < 0 || x > 255)) ||
                    (v.element == OnnxElement::Int8 && (x < -128 || x > 127)) ||
                    (v.element == OnnxElement::Bool && x != 0 && x != 1) ||
                    (v.element == OnnxElement::Int32 && (x < INT32_MIN || x > INT32_MAX)))
                    throw Failure("Integer initializer out of range", DiagnosticCode::ParseError);
                std::memcpy(v.bytes.data() + i * size, &x, size);
            }
        }
    }
    validate(v);
    return {std::move(name), std::move(v)};
}
ModelData                         parseGraph(std::span<const uint8_t> bytes, size_t depth);
std::pair<std::string, Attribute> attribute(std::span<const uint8_t> bytes, size_t depth) {
    Reader      r(bytes);
    Attribute   a;
    std::string name;
    while (!r.empty()) {
        auto f = r.next();
        switch (f.id) {
            case 1:
                wire(f, 2);
                name = f.text();
                break;
            case 20:
                wire(f, 0);
                a.type = static_cast<int>(f.number);
                break;
            case 2: {
                wire(f, 5);
                std::memcpy(&a.real, f.bytes.data(), 4);
                break;
            }
            case 3:
                wire(f, 0);
                a.integer = std::bit_cast<int64_t>(f.number);
                break;
            case 4:
                wire(f, 2);
                a.text = f.text();
                break;
            case 5:
                wire(f, 2);
                a.tensor = tensor(f.bytes).second;
                break;
            case 7: reals(f, a.reals); break;
            case 8: integers(f, a.integers); break;
            case 9:
                wire(f, 2);
                a.strings.push_back(f.text());
                break;
            case 6:
                wire(f, 2);
                a.graph = std::make_shared<ModelData>(parseGraph(f.bytes, depth + 1));
                break;
            case 11: throw Failure("Multiple graph attributes unsupported", DiagnosticCode::Unsupported);
            case 21: throw Failure("Function attribute references unsupported", DiagnosticCode::Unsupported);
            default: break;
        }
    }
    if (name.empty() || !a.type) throw Failure("Incomplete attribute", DiagnosticCode::ParseError);
    return {std::move(name), std::move(a)};
}
Node node(std::span<const uint8_t> bytes, size_t depth) {
    Reader r(bytes);
    Node   n;
    while (!r.empty()) {
        auto f = r.next();
        if (f.id >= 1 && f.id <= 5) wire(f, 2);
        switch (f.id) {
            case 1: n.inputs.push_back(f.text()); break;
            case 2: n.outputs.push_back(f.text()); break;
            case 3: n.name = f.text(); break;
            case 4: n.op = f.text(); break;
            case 5:
                if (!n.attrs.insert(attribute(f.bytes, depth)).second)
                    throw Failure("Duplicate attribute", DiagnosticCode::ParseError);
                break;
            case 7:
                wire(f, 2);
                n.domain = f.text();
                break;
            case 8: throw Failure("Function overloads unsupported", DiagnosticCode::Unsupported);
            default: break;
        }
    }
    if (n.op.empty() || n.outputs.empty()) throw Failure("Incomplete node", DiagnosticCode::ParseError);
    return n;
}
std::pair<std::string, Input> input(std::span<const uint8_t> bytes) {
    Reader      r(bytes);
    std::string name;
    Input       value;
    while (!r.empty()) {
        auto f = r.next();
        if (f.id == 1) {
            wire(f, 2);
            name = f.text();
        }
        if (f.id == 2) {
            wire(f, 2);
            Reader type(f.bytes);
            while (!type.empty()) {
                auto t = type.next();
                if (t.id == 4) {
                    value.type = 0;
                    continue;
                }
                if (t.id != 1) throw Failure("Non-tensor graph input", DiagnosticCode::Unsupported);
                wire(t, 2);
                Reader tt(t.bytes);
                while (!tt.empty()) {
                    auto p = tt.next();
                    if (p.id == 1) {
                        wire(p, 0);
                        value.type = static_cast<int>(p.number);
                    }
                    if (p.id == 2) {
                        wire(p, 2);
                        Reader shape(p.bytes);
                        while (!shape.empty()) {
                            auto d = shape.next();
                            if (d.id != 1) continue;
                            wire(d, 2);
                            Reader  dim(d.bytes);
                            int64_t v = -1;
                            while (!dim.empty()) {
                                auto x = dim.next();
                                if (x.id == 1) {
                                    wire(x, 0);
                                    v = std::bit_cast<int64_t>(x.number);
                                }
                            }
                            value.shape.push_back(v);
                        }
                    }
                }
            }
        }
    }
    if (name.empty()) throw Failure("Empty value name", DiagnosticCode::ParseError);
    if (value.shape.size() > 6) throw Failure("Input rank exceeds six", DiagnosticCode::Unsupported);
    for (auto d : value.shape)
        if (d < -1 || d > INT32_MAX) throw Failure("Invalid interface dimension", DiagnosticCode::ParseError);
    return {name, value};
}
ModelData parseGraph(std::span<const uint8_t> bytes, size_t depth) {
    ModelData model;
    if (depth > 16) throw Failure("ONNX graph nesting limit exceeded", DiagnosticCode::ParseError);
    Reader g(bytes);
    while (!g.empty()) {
        auto f = g.next();
        if (f.id == 1) {
            wire(f, 2);
            if (model.nodes.size() >= 100000) throw Failure("Node limit exceeded", DiagnosticCode::ParseError);
            model.nodes.push_back(node(f.bytes, depth));
        }
        if (f.id == 5) {
            wire(f, 2);
            auto t = tensor(f.bytes);
            t.second.bytes.retainAcrossRuns();
            if (t.first.empty() || !model.constants.emplace(std::move(t)).second)
                throw Failure("Duplicate/empty initializer", DiagnosticCode::ParseError);
        }
        if (f.id == 11) {
            wire(f, 2);
            auto v = input(f.bytes);
            model.info.inputs.push_back(v.first);
            if (!model.inputs.emplace(std::move(v)).second)
                throw Failure("Duplicate graph input", DiagnosticCode::ParseError);
        }
        if (f.id == 12) {
            wire(f, 2);
            model.info.outputs.push_back(input(f.bytes).first);
        }
        if (f.id == 15) throw Failure("Sparse initializers unsupported", DiagnosticCode::Unsupported);
    }
    return model;
}
}  // namespace

ModelData parse(std::span<const uint8_t> bytes) {
    if constexpr (std::endian::native != std::endian::little)
        throw Failure("ONNX requires little-endian host", DiagnosticCode::Unsupported);
    if (bytes.empty() || bytes.size() > 512u * 1024u * 1024u)
        throw Failure("Model size exceeds import limit", DiagnosticCode::ParseError);
    ModelData                model;
    Reader                   r(bytes);
    std::span<const uint8_t> graph;
    while (!r.empty()) {
        auto f = r.next();
        if (f.id == 1) {
            wire(f, 0);
            model.info.irVersion = static_cast<int64_t>(f.number);
        }
        if (f.id == 7) {
            wire(f, 2);
            if (!graph.empty()) throw Failure("Duplicate graph", DiagnosticCode::ParseError);
            graph = f.bytes;
        }
        if (f.id == 8) {
            wire(f, 2);
            Reader      ops(f.bytes);
            std::string domain;
            int64_t     version = 0;
            while (!ops.empty()) {
                auto x = ops.next();
                if (x.id == 1) {
                    wire(x, 2);
                    domain = x.text();
                }
                if (x.id == 2) {
                    wire(x, 0);
                    version = x.number;
                }
            }
            if (!model.opsets.emplace(domain, version).second)
                throw Failure("Duplicate opset", DiagnosticCode::ParseError);
        }
        if (f.id == 20 || f.id == 25 || f.id == 26)
            throw Failure("Training/functions/device configuration unsupported", DiagnosticCode::Unsupported);
    }
    if (model.info.irVersion < 3 || model.info.irVersion > 10 || !model.opsets.contains("") ||
        model.opsets.at("") < 13 || model.opsets.at("") > 17)
        throw Failure("Unsupported ONNX IR/opset version", DiagnosticCode::UnknownVersion);
    if (model.opsets.contains("com.microsoft") && model.opsets.at("com.microsoft") != 1)
        throw Failure("Unsupported Microsoft opset", DiagnosticCode::UnknownVersion);
    if (graph.empty()) throw Failure("Missing ONNX graph", DiagnosticCode::ParseError);
    auto parsed        = parseGraph(graph, 0);
    model.nodes        = std::move(parsed.nodes);
    model.constants    = std::move(parsed.constants);
    model.inputs       = std::move(parsed.inputs);
    model.info.inputs  = std::move(parsed.info.inputs);
    model.info.outputs = std::move(parsed.info.outputs);
    std::set<std::string> names;
    for (const auto& [name, v] : model.constants) {
        names.insert(name);
        model.info.initializerBytes += v.bytes.size();
    }
    for (const auto& [name, v] : model.inputs) names.insert(name);
    for (const auto& n : model.nodes) {
        if (!model.opsets.contains(n.domain)) throw Failure("Node domain has no opset", DiagnosticCode::ParseError);
        for (const auto& name : n.inputs)
            if (!name.empty() && !names.contains(name))
                throw Failure("Input not in topological order: " + name, DiagnosticCode::ParseError);
        for (const auto& name : n.outputs)
            if (!name.empty() && !names.insert(name).second)
                throw Failure("Duplicate graph value: " + name, DiagnosticCode::ParseError);
        if (!isSupported(n)) model.info.unsupportedNodes.push_back(n.name + " [" + n.domain + "::" + n.op + "]");
    }
    for (const auto& out : model.info.outputs)
        if (!names.contains(out)) throw Failure("Undefined graph output", DiagnosticCode::ParseError);
    size_t                                                                    recursiveNodes = 0;
    std::function<void(const ModelData&, const std::set<std::string>&, bool)> validateNested;
    validateNested = [&](const ModelData& g, const std::set<std::string>& outer, bool root) {
        recursiveNodes += g.nodes.size();
        if (recursiveNodes > 100000) throw Failure("Recursive node limit exceeded", DiagnosticCode::ParseError);
        auto                  available = outer;
        std::set<std::string> locals;
        for (const auto& [name, t] : g.constants) locals.insert(name);
        for (const auto& [name, t] : g.inputs) locals.insert(name);
        for (const auto& n : g.nodes)
            for (const auto& out : n.outputs)
                if (!out.empty() && !locals.insert(out).second)
                    throw Failure("Duplicate nested graph value: " + out, DiagnosticCode::ParseError);
        available.insert(locals.begin(), locals.end());
        for (const auto& n : g.nodes) {
            if (!model.opsets.contains(n.domain))
                throw Failure("Nested node domain has no opset", DiagnosticCode::ParseError);
            for (const auto& input : n.inputs)
                if (!input.empty() && !available.contains(input))
                    throw Failure("Undefined lexical capture: " + input, DiagnosticCode::ParseError);
            if (!root && !isSupported(n))
                model.info.unsupportedNodes.push_back(n.name + " [" + n.domain + "::" + n.op + "]");
            for (const auto& [key, a] : n.attrs)
                if (a.graph) validateNested(*a.graph, available, false);
        }
        for (const auto& out : g.info.outputs)
            if (!available.contains(out)) throw Failure("Undefined nested output: " + out, DiagnosticCode::ParseError);
    };
    validateNested(model, {}, true);
    model.info.nodeCount = model.nodes.size();
    return model;
}
}  // namespace eve::tensor::onnx_detail

// OnnxRuntime.cpp
namespace eve::tensor::onnx_detail {
namespace {
struct Value {
    const RuntimeTensor*                 borrowed = nullptr;
    std::shared_ptr<const RuntimeTensor> owned;
    std::shared_ptr<std::vector<Value>>  sequence;
    OnnxElement                          sequenceElement = OnnxElement::Float32;
    const RuntimeTensor&                 tensor() const {
        if (owned) return *owned;
        if (borrowed) return *borrowed;
        throw Failure("Expected tensor, received sequence");
    }
};
using Values = std::unordered_map<std::string, Value>;
struct Context {
    OnnxCompute*                                  compute;
    uint64_t                                      seed;
    bool                                          finite;
    size_t                                        steps = 0, liveBytes = 0;
    std::unordered_map<const Node*, std::mt19937> randomStreams;
};
Value hold(RuntimeTensor t, Context& c) {
    validate(t);
    if (c.finite && t.element == OnnxElement::Float32)
        for (size_t i = 0; i < count(t.shape); ++i)
            if (!std::isfinite(read<float>(t, i))) throw Failure("Nonfinite float output");
    const auto size = t.bytes.size();
    if (size > 512u * 1024u * 1024u - c.liveBytes) throw Failure("ONNX live tensor memory limit exceeded");
    // Context encloses every runtime Value; escaping public outputs are owning copies.
    auto* raw = new RuntimeTensor(std::move(t));
    c.liveBytes += size;
    return {nullptr,
            std::shared_ptr<const RuntimeTensor>(raw,
                                                 [&c, size](const RuntimeTensor* p) {
                                                     delete p;
                                                     c.liveBytes -= size;
                                                 }),
            {}};
}
std::set<std::string> captures(const ModelData& graph);
std::set<std::string> dependencies(const Node& n) {
    std::set<std::string> result;
    for (const auto& i : n.inputs)
        if (!i.empty()) result.insert(i);
    for (const auto& [key, a] : n.attrs)
        if (a.graph) {
            auto names = captures(*a.graph);
            result.insert(names.begin(), names.end());
        }
    return result;
}
std::set<std::string> captures(const ModelData& graph) {
    std::set<std::string> locals, result;
    for (const auto& [name, t] : graph.constants) locals.insert(name);
    for (const auto& [name, t] : graph.inputs) locals.insert(name);
    for (const auto& n : graph.nodes)
        for (const auto& out : n.outputs)
            if (!out.empty()) locals.insert(out);
    for (const auto& n : graph.nodes) {
        auto deps = dependencies(n);
        for (const auto& d : deps)
            if (!locals.contains(d)) result.insert(d);
    }
    for (const auto& out : graph.info.outputs)
        if (!locals.contains(out)) result.insert(out);
    return result;
}
void admit(const Node& n) {
    if (!isSupported(n))
        throw Failure(n.name + ": Unsupported ONNX node " + n.domain + "::" + n.op, DiagnosticCode::Unsupported,
                      n.name);
    for (const auto& [key, a] : n.attrs)
        if (a.graph)
            for (const auto& child : a.graph->nodes) admit(child);
}
std::vector<size_t> plan(const ModelData& g, const std::vector<std::string>& requested) {
    std::unordered_map<std::string, size_t> producers;
    for (size_t i = 0; i < g.nodes.size(); ++i)
        for (const auto& name : g.nodes[i].outputs)
            if (!name.empty() && !producers.emplace(name, i).second) throw Failure("Duplicate graph output");
    std::vector<bool>        selected(g.nodes.size(), false);
    std::vector<std::string> pending = requested;
    while (!pending.empty()) {
        auto name = std::move(pending.back());
        pending.pop_back();
        auto p = producers.find(name);
        if (p == producers.end() || selected[p->second]) continue;
        selected[p->second] = true;
        admit(g.nodes[p->second]);
        auto deps = dependencies(g.nodes[p->second]);
        pending.insert(pending.end(), deps.begin(), deps.end());
    }
    std::vector<size_t>                                                    indegree(g.nodes.size());
    std::vector<std::vector<size_t>>                                       users(g.nodes.size());
    size_t                                                                 total = 0;
    std::priority_queue<size_t, std::vector<size_t>, std::greater<size_t>> ready;
    for (size_t i = 0; i < g.nodes.size(); ++i)
        if (selected[i]) {
            ++total;
            std::set<size_t> parents;
            for (const auto& name : dependencies(g.nodes[i])) {
                auto p = producers.find(name);
                if (p != producers.end()) parents.insert(p->second);
            }
            indegree[i] = parents.size();
            for (auto p : parents) users[p].push_back(i);
            if (parents.empty()) ready.push(i);
        }
    std::vector<size_t> order;
    while (!ready.empty()) {
        size_t i = ready.top();
        ready.pop();
        order.push_back(i);
        for (auto user : users[i])
            if (--indegree[user] == 0) ready.push(user);
    }
    if (order.size() != total) throw Failure("ONNX graph has cyclic lexical dependencies", DiagnosticCode::ParseError);
    return order;
}
std::vector<Value> run(const ModelData&, Values, const std::vector<std::string>&, Context&, size_t);
std::vector<Value> control(const Node& n, const std::vector<Value>& in, const Values& values, Context& c,
                           size_t depth) {
    auto scalar = [&](size_t i) {
        const auto& t = in.at(i).tensor();
        if (count(t.shape) != 1) throw Failure("Control input must contain one element");
        return integer(t);
    };
    auto seq = [&](size_t i) -> const std::vector<Value>& {
        if (!in.at(i).sequence) throw Failure("Expected sequence input");
        return *in[i].sequence;
    };
    auto sequence = [](std::vector<Value> v, OnnxElement type) {
        return Value{nullptr, {}, std::make_shared<std::vector<Value>>(std::move(v)), type};
    };
    if (n.op == "SequenceEmpty") {
        auto type = static_cast<OnnxElement>(attr(n, "dtype", 1));
        elementSize(type);
        return {sequence({}, type)};
    }
    if (n.op == "SequenceAt") {
        const auto& s = seq(0);
        int64_t     i = scalar(1);
        if (i < 0) i += static_cast<int64_t>(s.size());
        if (i < 0 || static_cast<size_t>(i) >= s.size()) throw Failure("SequenceAt out of bounds");
        return {s[i]};
    }
    if (n.op == "SequenceInsert") {
        auto s = seq(0);
        if (s.size() >= 100000) throw Failure("Sequence length limit exceeded");
        int64_t i = in.size() > 2 ? scalar(2) : static_cast<int64_t>(s.size());
        if (i < 0) i += static_cast<int64_t>(s.size());
        if (i < 0 || static_cast<size_t>(i) > s.size()) throw Failure("SequenceInsert out of bounds");
        const auto& t = in.at(1).tensor();
        if (in[0].sequenceElement != t.element) throw Failure("Sequence element dtype mismatch");
        s.insert(s.begin() + i, in[1]);
        return {sequence(std::move(s), in[0].sequenceElement)};
    }
    if (n.op == "SplitToSequence") {
        const auto&          x = in.at(0).tensor();
        int                  a = axis(attr(n, "axis", 0), x.shape.size());
        std::vector<int64_t> lengths;
        if (in.size() > 1 && (in[1].borrowed || in[1].owned)) {
            const auto& split = in[1].tensor();
            lengths           = ints(split);
            if (split.shape.empty()) {
                if (lengths[0] <= 0) throw Failure("Invalid split size");
                int64_t width = lengths[0];
                lengths.clear();
                for (int64_t i = 0; i < x.shape[a]; i += width) {
                    if (lengths.size() >= 100000) throw Failure("Sequence length limit exceeded");
                    lengths.push_back(std::min(width, x.shape[a] - i));
                }
            }
        } else
            lengths.assign(x.shape[a], 1);
        if (lengths.size() > 100000) throw Failure("Sequence length limit exceeded");
        int64_t total = 0;
        for (auto size : lengths) {
            if (size < 0 || size > x.shape[a] - total) throw Failure("Invalid split lengths");
            total += size;
        }
        if (total != x.shape[a]) throw Failure("Split lengths do not cover axis");
        size_t outer = 1, inner = elementSize(x.element);
        for (int j = 0; j < a; ++j) outer *= x.shape[j];
        for (size_t j = a + 1; j < x.shape.size(); ++j) inner *= x.shape[j];
        std::vector<Value> s;
        size_t             offset = 0;
        for (auto length : lengths) {
            auto shape = x.shape;
            shape[a]   = length;
            if (!attr(n, "keepdims", 1)) {
                if (in.size() > 1 && (in[1].borrowed || in[1].owned))
                    throw Failure("keepdims=0 with explicit split unsupported", DiagnosticCode::Unsupported);
                shape.erase(shape.begin() + a);
            }
            RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
            for (size_t o = 0; o < outer; ++o)
                if (length)
                    std::memcpy(out.bytes.data() + o * length * inner,
                                x.bytes.data() + (o * x.shape[a] + offset) * inner, length * inner);
            s.push_back(hold(std::move(out), c));
            offset += length;
        }
        return {sequence(std::move(s), x.element)};
    }
    if (n.op == "ConcatFromSequence") {
        const auto& s = seq(0);
        if (s.empty()) throw Failure("Cannot concatenate empty sequence");
        std::vector<RuntimeTensor>        tensors;
        std::vector<const RuntimeTensor*> inputs;
        for (const auto& v : s) {
            tensors.push_back(v.tensor());
            if (attr(n, "new_axis", 0)) {
                const int a = axis(attr(n, "axis", 0), tensors.back().shape.size() + 1);
                tensors.back().shape.insert(tensors.back().shape.begin() + a, 1);
            }
        }
        for (const auto& t : tensors) inputs.push_back(&t);
        Node concat = n;
        concat.op   = "Concat";
        auto out    = executeShape(concat, inputs);
        if (!out) throw Failure("ConcatFromSequence dispatch failed");
        return {hold(std::move(*out), c)};
    }
    if (n.op == "If") {
        if (in.at(0).tensor().element != OnnxElement::Bool) throw Failure("If condition must be boolean");
        auto        key = scalar(0) ? "then_branch" : "else_branch";
        const auto& g   = n.attrs.at(key).graph;
        if (!g) throw Failure("Missing If branch");
        return run(*g, values, g->info.outputs, c, depth + 1);
    }
    if (n.op == "Loop") {
        const auto& body = n.attrs.at("body").graph;
        if (!body) throw Failure("Missing Loop body");
        if (in.size() < 2 || body->info.inputs.size() != in.size() || body->info.outputs.size() != in.size() - 1 ||
            n.outputs.size() != in.size() - 2)
            throw Failure("Loop scan outputs unsupported", DiagnosticCode::Unsupported);
        const int64_t trips = (in[0].borrowed || in[0].owned) ? scalar(0) : 10000;
        if (trips < 0 || trips > 10000) throw Failure("Loop trip budget exceeded");
        bool               condition = (in[1].borrowed || in[1].owned) ? scalar(1) != 0 : true;
        std::vector<Value> state(in.begin() + 2, in.end());
        for (int64_t i = 0; i < trips && condition; ++i) {
            auto scope                  = values;
            scope[body->info.inputs[0]] = hold(make(OnnxElement::Int64, {}, std::vector<int64_t>{i}), c);
            scope[body->info.inputs[1]] =
                hold(make(OnnxElement::Bool, {}, std::vector<uint8_t>{uint8_t(condition)}), c);
            for (size_t j = 0; j < state.size(); ++j) scope[body->info.inputs[j + 2]] = state[j];
            auto        result = run(*body, std::move(scope), body->info.outputs, c, depth + 1);
            const auto& cond   = result[0].tensor();
            if (cond.element != OnnxElement::Bool || count(cond.shape) != 1)
                throw Failure("Loop returned invalid condition");
            condition = integer(cond) != 0;
            state.assign(result.begin() + 1, result.end());
        }
        return state;
    }
    if (n.op == "RandomUniformLike" || n.op == "RandomNormalLike") {
        const auto& x = in.at(0).tensor();
        if (attr(n, "dtype", static_cast<int>(x.element)) != 1)
            throw Failure("Random output requires FP32", DiagnosticCode::Unsupported);
        uint64_t seed = c.seed;
        for (unsigned char ch : n.name) seed = (seed ^ ch) * 1099511628211ull;
        if (n.attrs.contains("seed")) {
            uint32_t bits;
            std::memcpy(&bits, &n.attrs.at("seed").real, 4);
            seed ^= bits;
        }
        auto [stream, inserted]    = c.randomStreams.try_emplace(&n, static_cast<uint32_t>(seed ^ (seed >> 32)));
        auto&              rng     = stream->second;
        auto               uniform = [&]() { return (double(rng()) + .5) / 4294967296.; };
        std::vector<float> v(count(x.shape));
        const float        low = n.attrs.contains("low") ? n.attrs.at("low").real : 0,
                    high       = n.attrs.contains("high") ? n.attrs.at("high").real : 1,
                    mean       = n.attrs.contains("mean") ? n.attrs.at("mean").real : 0,
                    scale      = n.attrs.contains("scale") ? n.attrs.at("scale").real : 1;
        if (!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(mean) || !std::isfinite(scale) ||
            high < low || scale < 0)
            throw Failure("Invalid random distribution parameters");
        for (auto& f : v)
            f = n.op == "RandomUniformLike" ? static_cast<float>(low + (high - low) * uniform())
                                            : static_cast<float>(mean + scale * std::sqrt(-2 * std::log(uniform())) *
                                                                            std::cos(6.283185307179586 * uniform()));
        return {hold(make(OnnxElement::Float32, x.shape, v), c)};
    }
    std::vector<const RuntimeTensor*> tensors;
    for (const auto& v : in) tensors.push_back(v.borrowed || v.owned ? &v.tensor() : nullptr);
    auto               outputs = execute(n, tensors, c.compute);
    std::vector<Value> result;
    for (auto& t : outputs) result.push_back(hold(std::move(t), c));
    return result;
}
std::vector<Value> run(const ModelData& g, Values values, const std::vector<std::string>& requested, Context& c,
                       size_t depth) {
    if (depth > 16) throw Failure("Execution graph nesting limit exceeded");
    for (const auto& [name, t] : g.constants)
        if (!g.inputs.contains(name) || !values.contains(name)) values[name] = {&t, {}, {}};
    auto                                    order = plan(g, requested);
    std::unordered_map<std::string, size_t> uses;
    for (auto i : order)
        for (const auto& dep : dependencies(g.nodes[i])) ++uses[dep];
    for (const auto& name : requested) ++uses[name];
    for (auto i : order) {
        const auto& n = g.nodes[i];
        if (++c.steps > 1000000) throw Failure("ONNX execution step budget exceeded");
        try {
            std::vector<Value> in;
            for (const auto& name : n.inputs) {
                if (name.empty()) {
                    in.push_back({});
                    continue;
                }
                auto it = values.find(name);
                if (it == values.end()) throw Failure("Missing feed or lexical capture: " + name);
                in.push_back(it->second);
            }
            auto outputs = control(n, in, values, c, depth);
            if (outputs.size() < n.outputs.size()) throw Failure("Operator output arity mismatch");
            for (size_t j = 0; j < n.outputs.size(); ++j)
                if (!n.outputs[j].empty() && uses[n.outputs[j]]) values[n.outputs[j]] = std::move(outputs[j]);
            for (const auto& dep : dependencies(n))
                if (--uses[dep] == 0) values.erase(dep);
        } catch (const Failure& e) {
            throw Failure(n.name + ": " + e.what(), e.code, e.path.empty() ? n.name : e.path);
        }
    }
    std::vector<Value> out;
    for (const auto& name : requested) {
        auto it = values.find(name);
        if (it == values.end()) throw Failure("Missing output: " + name, DiagnosticCode::NotFound);
        out.push_back(it->second);
    }
    return out;
}
}  // namespace
std::vector<OnnxNamedTensor> evaluate(const ModelData& model, std::span<const OnnxNamedTensor> feeds,
                                      const std::vector<std::string>& requested, OnnxCompute* compute,
                                      OnnxRunOptions options) {
    Context c{compute, options.seed, options.requireFinite};
    Values  inputs;
    for (const auto& f : feeds) inputs[f.name] = hold({f.tensor.element, f.tensor.shape, f.tensor.bytes}, c);
    auto                         outputs = run(model, std::move(inputs), requested, c, 0);
    std::vector<OnnxNamedTensor> result;
    size_t                       bytes = c.liveBytes;
    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& t = outputs[i].tensor();
        if (t.bytes.size() > 512u * 1024u * 1024u - bytes) throw Failure("ONNX output memory limit exceeded");
        bytes += t.bytes.size();
        result.push_back({requested[i], {t.element, t.shape, static_cast<std::vector<uint8_t>>(t.bytes)}});
    }
    return result;
}
}  // namespace eve::tensor::onnx_detail

// OnnxShapeOps.cpp
namespace eve::tensor::onnx_detail {
std::vector<int64_t> broadcast(const std::vector<int64_t>& a, const std::vector<int64_t>& b) {
    std::vector<int64_t> out(std::max(a.size(), b.size()), 1);
    for (size_t i = 0; i < out.size(); ++i) {
        const auto x = i < a.size() ? a[a.size() - 1 - i] : 1, y = i < b.size() ? b[b.size() - 1 - i] : 1;
        if (x != y && x != 1 && y != 1) throw Failure("Broadcast dimension mismatch");
        out[out.size() - 1 - i] = x == 1 ? y : x;
    }
    count(out);
    return out;
}
size_t broadcastIndex(size_t i, const std::vector<int64_t>& shape, const std::vector<int64_t>& output) {
    size_t index = 0, stride = 1;
    for (size_t j = 0; j < output.size(); ++j) {
        const auto coordinate = i % output[output.size() - 1 - j];
        i /= output[output.size() - 1 - j];
        if (j < shape.size()) {
            const auto d = shape[shape.size() - 1 - j];
            if (d != 1) index += coordinate * stride;
            stride *= d;
        }
    }
    return index;
}
std::optional<RuntimeTensor> executeShape(const Node& n, const std::vector<const RuntimeTensor*>& in) {
    const auto& x = required(in, 0);
    if (n.op == "Slice") {
        auto starts = ints(required(in, 1)), ends = ints(required(in, 2));
        auto axes = in.size() > 3 && in[3] ? ints(*in[3]) : std::vector<int64_t>(starts.size());
        if (in.size() <= 3 || !in[3]) std::iota(axes.begin(), axes.end(), 0);
        auto steps = in.size() > 4 && in[4] ? ints(*in[4]) : std::vector<int64_t>(starts.size(), 1);
        if (starts.size() != ends.size() || starts.size() != axes.size() || starts.size() != steps.size())
            throw Failure("Slice parameter lengths mismatch");
        auto                 shape = x.shape;
        std::vector<int64_t> begin(shape.size(), 0), step(shape.size(), 1);
        std::set<int>        seen;
        for (size_t j = 0; j < axes.size(); ++j) {
            int a = axis(axes[j], shape.size());
            if (!seen.insert(a).second || !steps[j] || steps[j] == INT64_MIN) throw Failure("Invalid Slice axis/step");
            const auto d = x.shape[a], st = steps[j];
            auto       norm = [d, st](int64_t v, bool end) {
                if (v < 0) v = v < -d ? (st < 0 ? -1 : 0) : v + d;
                return std::clamp(v, st < 0 && end ? int64_t(-1) : int64_t(0),
                                  st < 0 ? std::max(int64_t(0), d - 1) : d);
            };
            begin[a]               = norm(starts[j], false);
            const auto e           = norm(ends[j], true);
            step[a]                = st;
            const int64_t distance = st > 0 ? e - begin[a] : begin[a] - e, magnitude = st > 0 ? st : -st;
            shape[a] = d == 0 || distance <= 0 ? 0 : 1 + (distance - 1) / magnitude;
        }
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        for (size_t i = 0; i < count(shape); ++i) {
            size_t rest = i, source = 0, stride = 1;
            for (size_t j = shape.size(); j > 0; --j) {
                auto c = rest % shape[j - 1];
                rest /= shape[j - 1];
                source += (begin[j - 1] + c * step[j - 1]) * stride;
                stride *= x.shape[j - 1];
            }
            std::memcpy(out.bytes.data() + i * elementSize(x.element), x.bytes.data() + source * elementSize(x.element),
                        elementSize(x.element));
        }
        return out;
    }
    if (n.op == "Concat") {
        int  a     = axis(attr(n, "axis", 0), x.shape.size());
        auto shape = x.shape;
        shape[a]   = 0;
        for (auto* t : in) {
            if (!t || t->element != x.element || t->shape.size() != shape.size())
                throw Failure("Concat type/rank mismatch");
            for (size_t j = 0; j < shape.size(); ++j)
                if (j != a && t->shape[j] != shape[j]) throw Failure("Concat dimension mismatch");
            shape[a] += t->shape[a];
        }
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        size_t        outer = 1, inner = elementSize(x.element);
        for (int j = 0; j < a; ++j) outer *= shape[j];
        for (size_t j = a + 1; j < shape.size(); ++j) inner *= shape[j];
        size_t offset = 0;
        for (size_t o = 0; o < outer; ++o)
            for (auto* t : in) {
                const size_t size = t->shape[a] * inner;
                if (size) std::memcpy(out.bytes.data() + offset, t->bytes.data() + o * size, size);
                offset += size;
            }
        return out;
    }
    if (n.op == "Expand") {
        auto requested = ints(required(in, 1));
        count(requested);
        auto          shape = broadcast(x.shape, requested);
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        for (size_t i = 0; i < count(shape); ++i)
            std::memcpy(out.bytes.data() + i * elementSize(x.element),
                        x.bytes.data() + broadcastIndex(i, x.shape, shape) * elementSize(x.element),
                        elementSize(x.element));
        return out;
    }
    if (n.op == "ConstantOfShape") {
        auto          shape = ints(x);
        RuntimeTensor value = make(OnnxElement::Float32, {}, std::vector<float>{0});
        if (n.attrs.contains("value")) {
            if (!n.attrs.at("value").tensor) throw Failure("Missing fill value");
            value = *n.attrs.at("value").tensor;
        }
        if (count(value.shape) != 1) throw Failure("ConstantOfShape requires one fill value");
        RuntimeTensor out{value.element, shape, std::vector<uint8_t>(count(shape) * elementSize(value.element))};
        for (size_t i = 0; i < out.bytes.size(); i += value.bytes.size())
            std::memcpy(out.bytes.data() + i, value.bytes.data(), value.bytes.size());
        return out;
    }
    if (n.op == "Where") {
        const auto& a = required(in, 1);
        const auto& b = required(in, 2);
        if (x.element != OnnxElement::Bool || a.element != b.element) throw Failure("Where type mismatch");
        auto          shape = broadcast(broadcast(x.shape, a.shape), b.shape);
        const auto    size  = elementSize(a.element);
        RuntimeTensor out{a.element, shape, std::vector<uint8_t>(count(shape) * size)};
        for (size_t i = 0; i < count(shape); ++i) {
            const auto& v = x.bytes[broadcastIndex(i, x.shape, shape)] ? a : b;
            std::memcpy(out.bytes.data() + i * size, v.bytes.data() + broadcastIndex(i, v.shape, shape) * size, size);
        }
        return out;
    }
    if (n.op == "Not") {
        if (x.element != OnnxElement::Bool) throw Failure("Not requires boolean");
        auto out = x;
        for (auto& b : out.bytes) b = !b;
        return out;
    }
    if (n.op == "Equal" || n.op == "Less" || n.op == "Greater" || n.op == "And") {
        const auto& y = required(in, 1);
        if (x.element != y.element || (n.op == "And" && x.element != OnnxElement::Bool))
            throw Failure("Comparison type mismatch");
        auto          shape = broadcast(x.shape, y.shape);
        RuntimeTensor out{OnnxElement::Bool, shape, std::vector<uint8_t>(count(shape))};
        for (size_t i = 0; i < out.bytes.size(); ++i) {
            auto a = broadcastIndex(i, x.shape, shape), b = broadcastIndex(i, y.shape, shape);
            auto compare = [&](auto u, auto v) {
                return n.op == "Equal"     ? u == v
                       : n.op == "Less"    ? u < v
                       : n.op == "Greater" ? u > v
                                           : bool(u) && bool(v);
            };
            out.bytes[i] = x.element == OnnxElement::Float32 ? compare(read<float>(x, a), read<float>(y, b))
                                                             : compare(integer(x, a), integer(y, b));
        }
        return out;
    }
    if ((n.op == "Add" || n.op == "Sub" || n.op == "Mul" || n.op == "Div") && x.element != OnnxElement::Float32) {
        const auto& y = required(in, 1);
        if (x.element != y.element || (x.element != OnnxElement::Int64 && x.element != OnnxElement::Int32))
            throw Failure("Integer arithmetic dtype mismatch");
        auto          shape = broadcast(x.shape, y.shape);
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        for (size_t i = 0; i < count(shape); ++i) {
            int64_t a = integer(x, broadcastIndex(i, x.shape, shape)),
                    b = integer(y, broadcastIndex(i, y.shape, shape)), v = 0;
            if (n.op == "Add") {
                if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
                    throw Failure("Integer addition overflow");
                v = a + b;
            }
            if (n.op == "Sub") {
                if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
                    throw Failure("Integer subtraction overflow");
                v = a - b;
            }
            if (n.op == "Mul") {
                if (a && b &&
                    (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
                           : (b > 0 ? a < INT64_MIN / b : a < INT64_MAX / b)))
                    throw Failure("Integer multiplication overflow");
                v = a * b;
            }
            if (n.op == "Div") {
                if (!b || (a == INT64_MIN && b == -1)) throw Failure("Invalid integer division");
                v = a / b;
            }
            if (x.element == OnnxElement::Int32 && (v < INT32_MIN || v > INT32_MAX))
                throw Failure("Int32 arithmetic overflow");
            std::memcpy(out.bytes.data() + i * elementSize(x.element), &v, elementSize(x.element));
        }
        return out;
    }
    return std::nullopt;
}
}  // namespace eve::tensor::onnx_detail
