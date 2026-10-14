#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include "tensor/AffineQuant.h"
#include "tensor/CpuKernels.h"
#include "tensor/OnnxGpuKernels.h"
#include "tensor/OnnxInternal.h"
#include "tensor/Tensor.h"

// OnnxExecute.cpp
namespace eve::tensor::onnx_detail {
bool isSupported(const Node& n) {
    if (n.domain == "com.microsoft" && n.op == "DynamicQuantizeLSTM") {
        if (n.inputs.size() != 12 || n.outputs.empty() || n.outputs.size() > 3) return false;
        for (const auto& [key, a] : n.attrs) {
            if (key == "direction") {
                if (a.type != 3 || (a.text != "forward" && a.text != "reverse" && a.text != "bidirectional"))
                    return false;
            } else if (key == "hidden_size" || key == "input_forget") {
                if (a.type != 2) return false;
            } else if (key == "clip") {
                if (a.type != 1) return false;
            } else
                return false;
        }
        return true;
    }
    if (!n.domain.empty()) return false;
    if (n.op == "If" || n.op == "Loop") {
        const std::set<std::string> keys =
            n.op == "If" ? std::set<std::string>{"then_branch", "else_branch"} : std::set<std::string>{"body"};
        if (n.attrs.size() != keys.size() || n.outputs.empty() ||
            (n.op == "If" ? n.inputs.size() != 1 : n.inputs.size() < 2))
            return false;
        for (const auto& key : keys) {
            auto it = n.attrs.find(key);
            if (it == n.attrs.end() || it->second.type != 5 || !it->second.graph) return false;
        }
        return true;
    }

    static const std::map<std::string, std::set<std::string>> allowed{
        {"DynamicQuantizeLinear", {}},
        {"QuantizeLinear", {"axis"}},
        {"DequantizeLinear", {"axis"}},
        {"MatMulInteger", {}},
        {"ConvInteger", {"auto_pad", "dilations", "group", "kernel_shape", "pads", "strides"}},
        {"Constant", {"value"}},
        {"Clip", {}},
        {"Pad", {"mode"}},
        {"CumSum", {"exclusive", "reverse"}},
        {"SequenceEmpty", {"dtype"}},
        {"SequenceAt", {}},
        {"SequenceInsert", {}},
        {"SplitToSequence", {"axis", "keepdims"}},
        {"ConcatFromSequence", {"axis", "new_axis"}},
        {"RandomUniformLike", {"dtype", "seed", "low", "high"}},
        {"RandomNormalLike", {"dtype", "seed", "mean", "scale"}},
        {"InstanceNormalization", {"epsilon"}},
        {"ConvTranspose", {"auto_pad", "dilations", "group", "kernel_shape", "output_padding", "pads", "strides"}},
        {"Resize", {"coordinate_transformation_mode", "mode", "nearest_mode", "cubic_coeff_a"}},
        {"Range", {}},
        {"TopK", {"axis", "largest", "sorted"}},
        {"ScatterND", {"reduction"}},
        {"ScatterElements", {"axis", "reduction"}},
        {"ReduceProd", {"axes", "keepdims"}},
        {"ReduceMax", {"axes", "keepdims"}},
        {"Slice", {}},
        {"Concat", {"axis"}},
        {"Expand", {}},
        {"ConstantOfShape", {"value"}},
        {"Where", {}},
        {"Equal", {}},
        {"Less", {}},
        {"Greater", {}},
        {"And", {}},
        {"Not", {}},
        {"Reciprocal", {}},
        {"LeakyRelu", {"alpha"}},
        {"Floor", {}},
        {"Round", {}},
        {"Atan", {}},
        {"Identity", {}},
        {"Cast", {"to"}},
        {"Gather", {"axis"}},
        {"Shape", {"start", "end"}},
        {"Reshape", {"allowzero"}},
        {"Transpose", {"perm"}},
        {"Unsqueeze", {}},
        {"Squeeze", {}},
        {"MatMul", {}},
        {"Add", {}},
        {"Sub", {}},
        {"Mul", {}},
        {"Div", {}},
        {"Sqrt", {}},
        {"Exp", {}},
        {"Log", {}},
        {"Sin", {}},
        {"Cos", {}},
        {"Relu", {}},
        {"Sigmoid", {}},
        {"Tanh", {}},
        {"Neg", {}},
        {"Abs", {}},
        {"Pow", {}},
        {"Softmax", {"axis"}},
        {"ReduceMean", {"axes", "keepdims"}},
        {"ReduceSum", {"keepdims", "noop_with_empty_axes"}}};
    auto op = allowed.find(n.op);
    if (op == allowed.end()) return false;
    size_t minimum = 1, maximum = 1, outputs = 1;
    if (n.op == "Clip") maximum = 3;
    if (n.op == "Pad") {
        minimum = 2;
        maximum = 3;
    }
    if (n.op == "CumSum") minimum = maximum = 2;
    if (n.op == "SequenceEmpty") minimum = maximum = 0;
    if (n.op == "SequenceAt") minimum = maximum = 2;
    if (n.op == "SequenceInsert") {
        minimum = 2;
        maximum = 3;
    }
    if (n.op == "SplitToSequence") maximum = 2;
    if (n.op == "InstanceNormalization") minimum = maximum = 3;
    if (n.op == "ConvTranspose") {
        minimum = 2;
        maximum = 3;
    }
    if (n.op == "Resize") {
        minimum = 3;
        maximum = 4;
    }
    if (n.op == "Range" || n.op == "ScatterElements" || n.op == "ScatterND") minimum = maximum = 3;
    if (n.op == "TopK") {
        minimum = maximum = 2;
        outputs           = 2;
    }
    if (n.op == "Slice") {
        minimum = 3;
        maximum = 5;
    }
    if (n.op == "Concat") maximum = 100000;
    if (n.op == "Where") minimum = maximum = 3;
    if (n.op == "Expand" || n.op == "Equal" || n.op == "Less" || n.op == "Greater" || n.op == "And")
        minimum = maximum = 2;
    if (n.op == "Constant") minimum = maximum = 0;
    if (n.op == "DynamicQuantizeLinear") outputs = 3;
    if (n.op == "QuantizeLinear" || n.op == "DequantizeLinear") {
        minimum = 2;
        maximum = 3;
    }
    if (n.op == "MatMulInteger" || n.op == "ConvInteger") {
        minimum = 2;
        maximum = 4;
    }
    if (n.op == "Gather" || n.op == "Reshape" || n.op == "Unsqueeze" || n.op == "MatMul" || n.op == "Add" ||
        n.op == "Sub" || n.op == "Mul" || n.op == "Div" || n.op == "Pow")
        minimum = maximum = 2;
    if (n.op == "Squeeze" || n.op == "ReduceSum") maximum = 2;
    if (n.inputs.size() < minimum || n.inputs.size() > maximum || n.outputs.size() != outputs) return false;
    for (const auto& [key, a] : n.attrs) {
        if (!op->second.contains(key)) return false;
        const int expected = (key == "alpha" || key == "epsilon" || key == "cubic_coeff_a" || key == "seed" ||
                              key == "low" || key == "high" || key == "mean" || key == "scale")
                                 ? 1
                             : key == "value" ? 4
                             : (key == "auto_pad" || key == "reduction" || key == "coordinate_transformation_mode" ||
                                key == "mode" || key == "nearest_mode")
                                 ? 3
                             : (key == "perm" || key == "axes" || key == "dilations" || key == "kernel_shape" ||
                                key == "pads" || key == "strides" || key == "output_padding")
                                 ? 7
                                 : 2;
        if (a.type != expected) return false;
        if (key == "reduction" && a.text != "none") return false;
        if (key == "auto_pad" && a.text != "NOTSET") return false;
        if (key == "allowzero" && a.integer != 0) return false;
    }
    if (n.op == "Pad" && n.attrs.contains("mode") && n.attrs.at("mode").text != "constant" &&
        n.attrs.at("mode").text != "edge" && n.attrs.at("mode").text != "reflect")
        return false;
    if (n.op == "Resize") {
        const auto mode       = n.attrs.contains("mode") ? n.attrs.at("mode").text : "nearest";
        const auto coordinate = n.attrs.contains("coordinate_transformation_mode")
                                    ? n.attrs.at("coordinate_transformation_mode").text
                                    : "half_pixel";
        const auto nearest = n.attrs.contains("nearest_mode") ? n.attrs.at("nearest_mode").text : "round_prefer_floor";
        if (mode == "nearest") {
            if (coordinate != "asymmetric" || nearest != "floor") return false;
        } else if (mode != "linear" || (coordinate != "half_pixel" && coordinate != "asymmetric"))
            return false;
    }
    return true;
}
namespace {
std::vector<int> dims(const RuntimeTensor& x) {
    std::vector<int> out;
    for (auto d : x.shape) {
        if (d <= 0) throw Failure("Empty float kernel input", DiagnosticCode::Unsupported);
        out.push_back(static_cast<int>(d));
    }
    return out;
}
std::vector<RuntimeTensor> single(RuntimeTensor x) { return {std::move(x)}; }
}  // namespace
std::vector<RuntimeTensor> execute(const Node& n, const std::vector<const RuntimeTensor*>& in, OnnxCompute* compute) {
    if (n.domain == "com.microsoft" && n.op == "DynamicQuantizeLSTM") return executeQuantLstm(n, in, compute);
    if (n.op == "Constant") {
        auto a = n.attrs.find("value");
        if (a == n.attrs.end() || !a->second.tensor) throw Failure("Constant requires tensor value");
        return single(*a->second.tensor);
    }
    if (n.op.find("Quantize") != std::string::npos || n.op == "DequantizeLinear" || n.op == "MatMulInteger" ||
        n.op == "ConvInteger")
        return executeQuant(n, in, compute);
    const auto& x = required(in, 0);
    if (n.op == "Identity") return single(x);
    if (n.op == "Shape") {
        const int64_t        rank  = x.shape.size();
        auto                 norm  = [rank](int64_t i) { return std::clamp(i < 0 ? i + rank : i, int64_t(0), rank); };
        const auto           begin = norm(attr(n, "start", 0)), end = norm(attr(n, "end", rank));
        std::vector<int64_t> shape;
        if (begin < end) shape.assign(x.shape.begin() + begin, x.shape.begin() + end);
        return single(make(OnnxElement::Int64, {static_cast<int64_t>(shape.size())}, shape));
    }
    if (n.op == "Cast") {
        const auto target = static_cast<OnnxElement>(attr(n, "to", 0));
        if (compute && target == OnnxElement::Float32 && x.element == OnnxElement::Int32)
            return single(dispatchFloat(*compute, {&x}, x.shape, "y[i]=float(x0[i]);"));
        const auto    size = elementSize(target);
        RuntimeTensor out{target, x.shape, std::vector<uint8_t>(count(x.shape) * size)};
        for (size_t i = 0; i < count(x.shape); ++i) {
            if (target == OnnxElement::Float32) {
                const float v =
                    x.element == OnnxElement::Float32 ? read<float>(x, i) : static_cast<float>(integer(x, i));
                std::memcpy(out.bytes.data() + i * 4, &v, 4);
            } else {
                int64_t v;
                if (x.element == OnnxElement::Float32) {
                    const double f = read<float>(x, i);
                    if (!std::isfinite(f) || f < -9223372036854775808.0 || f >= 9223372036854775808.0)
                        throw Failure("Cast overflow");
                    v = target == OnnxElement::Bool ? (f != 0) : static_cast<int64_t>(f);
                } else
                    v = integer(x, i);
                if (target == OnnxElement::Bool) v = v != 0;
                std::memcpy(out.bytes.data() + i * size, &v, size);
            }
        }
        return single(std::move(out));
    }
    if (n.op == "Gather") {
        const auto& idx = required(in, 1);
        const int   a   = axis(attr(n, "axis", 0), x.shape.size());
        if (idx.element != OnnxElement::Int32 && idx.element != OnnxElement::Int64)
            throw Failure("Gather indices must be int32/int64");
        size_t inner = 1, outer = 1;
        for (size_t i = a + 1; i < x.shape.size(); ++i) inner *= x.shape[i];
        for (int i = 0; i < a; ++i) outer *= x.shape[i];
        std::vector<int64_t> shape(x.shape.begin(), x.shape.begin() + a);
        shape.insert(shape.end(), idx.shape.begin(), idx.shape.end());
        shape.insert(shape.end(), x.shape.begin() + a + 1, x.shape.end());
        const size_t  stride = inner * elementSize(x.element), ni = count(idx.shape);
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        for (size_t o = 0; o < outer; ++o)
            for (size_t i = 0; i < ni; ++i) {
                auto j = integer(idx, i);
                if (j < 0) j += x.shape[a];
                if (j < 0 || j >= x.shape[a]) throw Failure("Gather index out of range");
                if (stride)
                    std::memcpy(out.bytes.data() + (o * ni + i) * stride,
                                x.bytes.data() + (o * x.shape[a] + j) * stride, stride);
            }
        return single(std::move(out));
    }
    if (n.op == "Reshape" || n.op == "Unsqueeze" || n.op == "Squeeze") {
        auto shape = x.shape;
        if (n.op == "Reshape") {
            shape        = ints(required(in, 1));
            int    infer = -1;
            size_t known = 1;
            for (size_t i = 0; i < shape.size(); ++i) {
                if (shape[i] == 0) {
                    if (i >= x.shape.size()) throw Failure("Reshape zero axis out of range");
                    shape[i] = x.shape[i];
                }
                if (shape[i] == -1) {
                    if (infer != -1) throw Failure("Multiple inferred dimensions");
                    infer = static_cast<int>(i);
                } else {
                    if (shape[i] < 0 || shape[i] > INT32_MAX ||
                        (shape[i] && known > 128u * 1024u * 1024u / static_cast<size_t>(shape[i])))
                        throw Failure("Reshape overflow");
                    known *= static_cast<size_t>(shape[i]);
                }
            }
            if (infer >= 0) {
                if (!known || count(x.shape) % known) throw Failure("Invalid inferred shape");
                shape[infer] = count(x.shape) / known;
            }
        } else {
            std::vector<int64_t> axes;
            if (in.size() > 1 && in[1])
                axes = ints(*in[1]);
            else if (n.op == "Unsqueeze")
                throw Failure("Unsqueeze needs axes");
            else
                for (size_t i = 0; i < shape.size(); ++i)
                    if (shape[i] == 1) axes.push_back(i);
            const size_t  rank = shape.size() + (n.op == "Unsqueeze" ? axes.size() : 0);
            std::set<int> positions;
            for (auto a : axes)
                if (!positions.insert(axis(a, rank)).second) throw Failure("Duplicate squeeze axis");
            if (n.op == "Unsqueeze") {
                for (int a : positions) shape.insert(shape.begin() + a, 1);
            } else
                for (auto it = positions.rbegin(); it != positions.rend(); ++it) {
                    if (shape[*it] != 1) throw Failure("Squeeze dimension not one");
                    shape.erase(shape.begin() + *it);
                }
        }
        if (count(shape) != count(x.shape)) throw Failure("Reshape element count mismatch");
        RuntimeTensor out = x;
        out.shape         = std::move(shape);
        return single(std::move(out));
    }
    if (n.op == "Transpose") {
        std::vector<int64_t> order = attrs(n, "perm", {});
        if (order.empty())
            for (size_t i = x.shape.size(); i > 0; --i) order.push_back(i - 1);
        if (order.size() != x.shape.size()) throw Failure("Invalid transpose rank");
        std::set<int64_t>    used;
        std::vector<int64_t> shape;
        for (auto a : order) {
            if (a < 0 || a >= static_cast<int64_t>(x.shape.size()) || !used.insert(a).second)
                throw Failure("Invalid transpose permutation");
            shape.push_back(x.shape[a]);
        }
        RuntimeTensor       out{x.element, shape, std::vector<uint8_t>(x.bytes.size())};
        const auto          size = elementSize(x.element);
        std::vector<size_t> strides(x.shape.size(), 1);
        for (size_t i = x.shape.size(); i > 1; --i) strides[i - 2] = strides[i - 1] * x.shape[i - 1];
        for (size_t i = 0; i < count(shape); ++i) {
            size_t rest = i, source = 0;
            for (size_t j = shape.size(); j > 0; --j) {
                source += (rest % shape[j - 1]) * strides[order[j - 1]];
                rest /= shape[j - 1];
            }
            std::memcpy(out.bytes.data() + i * size, x.bytes.data() + source * size, size);
        }
        return single(std::move(out));
    }
    if (auto indexResult = executeIndex(n, in)) return std::move(*indexResult);
    if (auto shapeResult = executeShape(n, in)) return single(std::move(*shapeResult));
    if (auto miscResult = executeMisc(n, in, compute)) return single(std::move(*miscResult));
    if (auto neuralResult = executeNeural(n, in, compute)) return single(std::move(*neuralResult));
    if (auto numericResult = executeNumeric(n, in, compute)) return single(std::move(*numericResult));
    const auto a = floats(x);
    if (n.op == "Softmax") {
        auto               xd = dims(x);
        std::vector<float> out(a.size());
        kernels::softmax(a.data(), xd.data(), static_cast<int>(xd.size()), axis(attr(n, "axis", -1), xd.size()), false,
                         out.data());
        return single(make(OnnxElement::Float32, x.shape, out));
    }
    static const std::map<std::string, OpType> unary{
        {"Sqrt", OpType::Sqrt}, {"Exp", OpType::Exp},   {"Log", OpType::Log},         {"Sin", OpType::Sin},
        {"Cos", OpType::Cos},   {"Relu", OpType::Relu}, {"Sigmoid", OpType::Sigmoid}, {"Tanh", OpType::Tanh},
        {"Neg", OpType::Neg},   {"Abs", OpType::Abs}};
    auto op = unary.find(n.op);
    if (op == unary.end()) throw Failure("Unsupported operator", DiagnosticCode::Unsupported);
    std::vector<float> out(a.size());
    kernels::unaryOp(op->second, a.data(), static_cast<int>(a.size()), out.data(), 0, 0);
    return single(make(OnnxElement::Float32, x.shape, out));
}
}  // namespace eve::tensor::onnx_detail

// OnnxGpuKernels.cpp
namespace eve::tensor::onnx_detail {
namespace {
std::string prefix(bool as, bool bs) {
    std::ostringstream s;
    s << "#version 450\nlayout(local_size_x=64) in;\n"
         "layout(std430,binding=0) readonly buffer A {uint a[];};\n"
         "layout(std430,binding=1) readonly buffer B {uint b[];};\n"
         "layout(std430,binding=2) readonly buffer Z {int zp[];};\n"
         "layout(std430,binding=3) writeonly buffer Y {int y[];};\n"
         "int av(uint i){int v=int((a[i/4]>>((i%4)*8))&255u);return "
      << (as ? "v>=128?v-256:v" : "v")
      << ";}\n"
         "int bv(uint i){int v=int((b[i/4]>>((i%4)*8))&255u);return "
      << (bs ? "v>=128?v-256:v" : "v") << ";}\n";
    return s.str();
}
// Compatibility host-byte calls use the same resident kernel generators. The
// adapter below requests completion through the provider's synchronous contract.
class Synchronous final : public OnnxCompute {
    OnnxCompute& target;

public:
    explicit Synchronous(OnnxCompute& t) : target(t) {}
    Result<std::vector<uint8_t>> dispatch(const OnnxKernel& k) override { return target.dispatch(k); }
};
OnnxBuffer hostBuffer(std::span<const uint8_t> bytes) {
    auto p = std::make_shared<const std::vector<uint8_t>>(bytes.begin(), bytes.end());
    return {p, {}, p->size()};
}
std::vector<int32_t> integerOutput(const OnnxBuffer& result) {
    if (!result.host || result.host->size() % 4) throw Failure("Invalid synchronous integer output");
    std::vector<int32_t> out(result.host->size() / 4);
    std::memcpy(out.data(), result.host->data(), result.host->size());
    return out;
}
}  // namespace
std::vector<int32_t> gpuMatmul(OnnxCompute& d, affine::ByteView a, affine::ByteView b, size_t m, size_t k, size_t n,
                               int az, std::span<const int32_t> bz) {
    Synchronous sync(d);
    return integerOutput(gpuMatmulResident(sync, hostBuffer(a.bytes), hostBuffer(b.bytes), a.signedValues,
                                           b.signedValues, m, k, n, az, bz));
}
std::vector<int32_t> gpuConv(OnnxCompute& d, affine::ByteView x, affine::ByteView w, const affine::ConvShape& c, int xz,
                             std::span<const int32_t> wz) {
    Synchronous sync(d);
    return integerOutput(
        gpuConvResident(sync, hostBuffer(x.bytes), hostBuffer(w.bytes), x.signedValues, w.signedValues, c, xz, wz));
}
OnnxBuffer enqueueInteger(OnnxCompute& d, const std::string& source, OnnxBuffer a, OnnxBuffer b,
                          std::span<const int32_t> z, size_t size, size_t terms) {
    if (terms > INT32_MAX / 65025 || !size || size > 64u * 65535u)
        throw Failure("GPU integer kernel exceeds exact accumulator or dispatch limit", DiagnosticCode::Unsupported);
    std::vector<uint8_t> bytes(z.size_bytes());
    std::memcpy(bytes.data(), z.data(), bytes.size());
    auto                          host = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    const std::vector<OnnxBuffer> inputs{a, b, {host, {}, host->size()}};
    auto                          r = d.enqueue(source, inputs, size * 4, static_cast<uint32_t>(size));
    if (!r.ok()) throw Failure(r.error()->message(), r.error()->code());
    return std::move(r.value());
}
OnnxBuffer gpuMatmulResident(OnnxCompute& d, OnnxBuffer a, OnnxBuffer b, bool as, bool bs, size_t m, size_t k, size_t n,
                             int az, std::span<const int32_t> bz, size_t bOffset) {
    if (bz.size() != 1 && bz.size() != n) throw Failure("GPU matmul zero-point extent mismatch");
    std::ostringstream s;
    s << prefix(as, bs) << "void main(){uint i=gl_GlobalInvocationID.x;if(i>=" << m * n << "u)return;"
      << "uint r=i/" << n << "u,c=i%" << n << "u;int v=0;for(uint j=0;j<" << k << "u;++j)"
      << "v+=(av(r*" << k << "u+j)-(" << az << "))*(bv(" << bOffset << "u+j*" << n << "u+c)-zp["
      << (bz.size() == 1 ? "0" : "c") << "]);y[i]=v;}";
    return enqueueInteger(d, s.str(), a, b, bz, m * n, k);
}

OnnxBuffer gpuConvResident(OnnxCompute& d, OnnxBuffer x, OnnxBuffer w, bool xs, bool ws, const affine::ConvShape& c,
                           int xz, std::span<const int32_t> wz) {
    const int64_t oh =
        (int64_t(c.height) + c.padTop + c.padBottom - int64_t(c.dilationH) * (c.kernelH - 1) - 1) / c.strideH + 1;
    const int64_t ow =
        (int64_t(c.width) + c.padLeft + c.padRight - int64_t(c.dilationW) * (c.kernelW - 1) - 1) / c.strideW + 1;
    if ((oh - 1) * c.strideH > INT32_MAX || (ow - 1) * c.strideW > INT32_MAX ||
        (oh - 1) * c.strideH - c.padTop + int64_t(c.kernelH - 1) * c.dilationH > INT32_MAX ||
        (ow - 1) * c.strideW - c.padLeft + int64_t(c.kernelW - 1) * c.dilationW > INT32_MAX)
        throw Failure("GPU convolution coordinate overflow", DiagnosticCode::Unsupported);
    const size_t       size = size_t(c.batch) * c.outputs * oh * ow;
    const int          cg   = c.channels / c.groups;
    std::ostringstream s;
    s << prefix(xs, ws) << "void main(){uint i=gl_GlobalInvocationID.x;if(i>=" << size << "u)return;"
      << "int xx=int(i%" << ow << "u),yy=int(i/" << ow << "u%" << oh << "u),o=int(i/" << ow * oh << "u%" << c.outputs
      << "u),batch=int(i/" << ow * oh * c.outputs << "u);int v=0;"
      << "for(int ch=0;ch<" << cg << ";++ch)for(int ky=0;ky<" << c.kernelH << ";++ky)for(int kx=0;kx<" << c.kernelW
      << ";++kx){"
      << "int iy=yy*" << c.strideH << "-" << c.padTop << "+ky*" << c.dilationH << ",ix=xx*" << c.strideW << "-"
      << c.padLeft << "+kx*" << c.dilationW << ";"
      << "if(iy<0||ix<0||iy>=" << c.height << "||ix>=" << c.width << ")continue;"
      << "uint ai=uint(((batch*" << c.channels << "+o/" << c.outputs / c.groups << "*" << cg << "+ch)*" << c.height
      << "+iy)*" << c.width << "+ix);"
      << "uint bi=uint(((o*" << cg << "+ch)*" << c.kernelH << "+ky)*" << c.kernelW << "+kx);"
      << "v+=(av(ai)-(" << xz << "))*(bv(bi)-zp[" << (wz.size() == 1 ? "0" : "o") << "]);}y[i]=v;}";
    return enqueueInteger(d, s.str(), x, w, wz, size, size_t(cg) * c.kernelH * c.kernelW);
}
}  // namespace eve::tensor::onnx_detail

// OnnxIndexOps.cpp
namespace eve::tensor::onnx_detail {
std::optional<std::vector<RuntimeTensor>> executeIndex(const Node& n, const std::vector<const RuntimeTensor*>& in) {
    const auto& x   = required(in, 0);
    auto        one = [](RuntimeTensor t) { return std::vector<RuntimeTensor>{std::move(t)}; };
    if (n.op == "Range") {
        const auto& end   = required(in, 1);
        const auto& delta = required(in, 2);
        if (count(x.shape) != 1 || count(end.shape) != 1 || count(delta.shape) != 1 || x.element != end.element ||
            x.element != delta.element)
            throw Failure("Range requires matching scalars");
        if (x.element == OnnxElement::Float32) {
            const double start = read<float>(x, 0), stop = read<float>(end, 0), step = read<float>(delta, 0);
            if (!std::isfinite(start) || !std::isfinite(stop) || !std::isfinite(step) || step == 0)
                throw Failure("Invalid Range");
            const double length = std::max(0., std::ceil((stop - start) / step));
            if (length > 128u * 1024u * 1024u) throw Failure("Range too large");
            std::vector<float> v(static_cast<size_t>(length));
            for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(start + i * step);
            return one(make(x.element, {static_cast<int64_t>(v.size())}, v));
        }
        if (x.element != OnnxElement::Int64 && x.element != OnnxElement::Int32)
            throw Failure("Range integer dtype unsupported");
        const int64_t start = integer(x), stop = integer(end), step = integer(delta);
        if (!step || step == INT64_MIN) throw Failure("Invalid Range step");
        uint64_t length = 0;
        if (step > 0 && stop > start) length = 1 + (uint64_t(stop) - uint64_t(start) - 1) / uint64_t(step);
        if (step < 0 && stop < start) length = 1 + (uint64_t(start) - uint64_t(stop) - 1) / uint64_t(-step);
        if (length > 128u * 1024u * 1024u) throw Failure("Range too large");
        RuntimeTensor out{
            x.element, {static_cast<int64_t>(length)}, std::vector<uint8_t>(length * elementSize(x.element))};
        int64_t v = start;
        for (size_t i = 0; i < length; ++i) {
            std::memcpy(out.bytes.data() + i * elementSize(x.element), &v, elementSize(x.element));
            if (i + 1 < length) v += step;
        }
        return one(std::move(out));
    }
    if (n.op == "TopK") {
        const auto& kt = required(in, 1);
        if (count(kt.shape) != 1) throw Failure("TopK requires one K");
        int           a = axis(attr(n, "axis", -1), x.shape.size());
        const int64_t k = integer(kt), size = x.shape[a];
        if (k < 0 || k > size) throw Failure("Invalid TopK K");
        size_t outer = 1, inner = 1;
        for (int j = 0; j < a; ++j) outer *= x.shape[j];
        for (size_t j = a + 1; j < x.shape.size(); ++j) inner *= x.shape[j];
        auto shape = x.shape;
        shape[a]   = k;
        RuntimeTensor        values{x.element, shape, std::vector<uint8_t>(count(shape) * elementSize(x.element))};
        std::vector<int64_t> indices(count(shape));
        for (size_t o = 0; o < outer; ++o)
            for (size_t i = 0; i < inner; ++i) {
                std::vector<int64_t> order(size);
                std::iota(order.begin(), order.end(), 0);
                std::stable_sort(order.begin(), order.end(), [&](int64_t aa, int64_t bb) {
                    auto         compare = [&](auto u, auto v) { return attr(n, "largest", 1) ? u > v : u < v; };
                    const size_t ai = (o * size + aa) * inner + i, bi = (o * size + bb) * inner + i;
                    return x.element == OnnxElement::Float32 ? compare(read<float>(x, ai), read<float>(x, bi))
                                                             : compare(integer(x, ai), integer(x, bi));
                });
                for (int64_t j = 0; j < k; ++j) {
                    size_t dst = (o * k + j) * inner + i, src = (o * size + order[j]) * inner + i;
                    indices[dst] = order[j];
                    std::memcpy(values.bytes.data() + dst * elementSize(x.element),
                                x.bytes.data() + src * elementSize(x.element), elementSize(x.element));
                }
            }
        return std::vector<RuntimeTensor>{std::move(values), make(OnnxElement::Int64, shape, indices)};
    }
    if (n.op == "ScatterElements" || n.op == "ScatterND") {
        const auto& idx     = required(in, 1);
        const auto& updates = required(in, 2);
        if (updates.element != x.element || (idx.element != OnnxElement::Int64 && idx.element != OnnxElement::Int32))
            throw Failure("Scatter dtype mismatch");
        auto       out   = x;
        const auto bytes = elementSize(x.element);
        if (n.op == "ScatterElements") {
            if (idx.shape != updates.shape || idx.shape.size() != x.shape.size())
                throw Failure("ScatterElements shape mismatch");
            int a = axis(attr(n, "axis", 0), x.shape.size());
            for (size_t j = 0; j < x.shape.size(); ++j)
                if (j != a && idx.shape[j] > x.shape[j]) throw Failure("Scatter index shape exceeds data");
            for (size_t i = 0; i < count(idx.shape); ++i) {
                size_t rest = i, source = 0, stride = 1;
                for (size_t j = x.shape.size(); j > 0; --j) {
                    int64_t c = rest % idx.shape[j - 1];
                    rest /= idx.shape[j - 1];
                    if (j - 1 == a) {
                        c = integer(idx, i);
                        if (c < 0) c += x.shape[j - 1];
                    }
                    if (c < 0 || c >= x.shape[j - 1]) throw Failure("Scatter index out of range");
                    source += c * stride;
                    stride *= x.shape[j - 1];
                }
                std::memcpy(out.bytes.data() + source * bytes, updates.bytes.data() + i * bytes, bytes);
            }
        } else {
            if (idx.shape.empty() || idx.shape.back() < 1 || static_cast<size_t>(idx.shape.back()) > x.shape.size())
                throw Failure("ScatterND index rank mismatch");
            const size_t         depth = idx.shape.back();
            std::vector<int64_t> expected(idx.shape.begin(), idx.shape.end() - 1);
            expected.insert(expected.end(), x.shape.begin() + depth, x.shape.end());
            if (expected != updates.shape) throw Failure("ScatterND updates shape mismatch");
            size_t chunk = 1;
            for (size_t j = depth; j < x.shape.size(); ++j) chunk *= x.shape[j];
            for (size_t i = 0; i < count(idx.shape) / depth; ++i) {
                size_t target = 0;
                for (size_t j = 0; j < depth; ++j) {
                    auto c = integer(idx, i * depth + j);
                    if (c < 0) c += x.shape[j];
                    if (c < 0 || c >= x.shape[j]) throw Failure("ScatterND index out of range");
                    target = target * x.shape[j] + c;
                }
                if (chunk)
                    std::memcpy(out.bytes.data() + target * chunk * bytes, updates.bytes.data() + i * chunk * bytes,
                                chunk * bytes);
            }
        }
        return one(std::move(out));
    }
    if (n.op == "ReduceProd" || n.op == "ReduceMax") {
        auto axes = attrs(n, "axes", {});
        if (axes.empty())
            for (size_t i = 0; i < x.shape.size(); ++i) axes.push_back(i);
        std::set<int> reduced;
        for (auto a : axes)
            if (!reduced.insert(axis(a, x.shape.size())).second) throw Failure("Duplicate reduction axis");
        auto                 mapShape = x.shape;
        std::vector<int64_t> shape;
        for (size_t j = 0; j < x.shape.size(); ++j) {
            if (reduced.contains(static_cast<int>(j))) {
                mapShape[j] = 1;
                if (attr(n, "keepdims", 1)) shape.push_back(1);
            } else
                shape.push_back(x.shape[j]);
        }
        if (x.element == OnnxElement::Float32) {
            std::vector<float> out(count(shape), n.op == "ReduceProd" ? 1.f : -std::numeric_limits<float>::infinity());
            for (size_t i = 0; i < count(x.shape); ++i) {
                size_t      t = broadcastIndex(i, mapShape, x.shape);
                const float v = read<float>(x, i);
                out[t]        = n.op == "ReduceProd" ? out[t] * v : std::max(out[t], v);
            }
            return one(make(x.element, shape, out));
        }
        if (x.element != OnnxElement::Int64 && x.element != OnnxElement::Int32)
            throw Failure("Integer reduction dtype unsupported");
        std::vector<int64_t> out(count(shape), n.op == "ReduceProd" ? 1 : INT64_MIN);
        for (size_t i = 0; i < count(x.shape); ++i) {
            auto&      a = out[broadcastIndex(i, mapShape, x.shape)];
            const auto b = integer(x, i);
            if (n.op == "ReduceMax")
                a = std::max(a, b);
            else {
                if (a && b &&
                    (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
                           : (b > 0 ? a < INT64_MIN / b : a < INT64_MAX / b)))
                    throw Failure("ReduceProd overflow");
                a *= b;
            }
        }
        if (x.element == OnnxElement::Int64) return one(make(x.element, shape, out));
        std::vector<int32_t> narrow;
        for (auto v : out) {
            if (v < INT32_MIN || v > INT32_MAX) throw Failure("Int32 reduction overflow");
            narrow.push_back(static_cast<int32_t>(v));
        }
        return one(make(x.element, shape, narrow));
    }
    return std::nullopt;
}
}  // namespace eve::tensor::onnx_detail

// OnnxLstm.cpp
namespace eve::tensor::onnx_detail {
namespace {
template <class T>
T checked(Result<T> r) {
    if (!r.ok()) throw Failure(r.error()->message(), r.error()->code());
    return std::move(r.value());
}
void shape(const RuntimeTensor& t, const std::vector<int64_t>& expected) {
    if (t.shape != expected) throw Failure("LSTM input shape mismatch");
}
std::vector<float> optionalFloats(const std::vector<const RuntimeTensor*>& in, size_t i,
                                  const std::vector<int64_t>& dims) {
    if (i >= in.size() || !in[i]) return std::vector<float>(count(dims), 0);
    shape(*in[i], dims);
    return floats(*in[i]);
}
struct Weights {
    const RuntimeTensor& tensor;
    std::vector<float>   scales;
    std::vector<int64_t> zeros;
    size_t               directions, gates, rows;
    Weights(const RuntimeTensor& t, const RuntimeTensor& s, const RuntimeTensor& z, size_t d, size_t g, size_t r)
        : tensor(t), scales(floats(s)), zeros(ints(z)), directions(d), gates(g), rows(r) {
        if (t.element != OnnxElement::Int8 && t.element != OnnxElement::UInt8)
            throw Failure("LSTM weights must be 8-bit");
        if (t.element != z.element || s.shape != z.shape ||
            (s.shape != std::vector<int64_t>{static_cast<int64_t>(d)} &&
             s.shape != std::vector<int64_t>{static_cast<int64_t>(d), static_cast<int64_t>(g)}))
            throw Failure("LSTM quantization parameter shape mismatch");
        shape(t, {static_cast<int64_t>(d), static_cast<int64_t>(r), static_cast<int64_t>(g)});
        for (float v : scales)
            if (!(v > 0) || !std::isfinite(v)) throw Failure("Invalid LSTM weight scale");
    }
    std::vector<float> multiply(const affine::QuantizedActivation& a, size_t batch, size_t direction,
                                OnnxCompute* compute) const {
        std::vector<float> out(batch * gates);
        const bool         sign = tensor.element == OnnxElement::Int8;
        if (compute) {
            std::vector<int32_t> z(scales.size() == directions ? 1 : gates);
            for (size_t i = 0; i < z.size(); ++i) z[i] = static_cast<int32_t>(zeros[direction * z.size() + i]);
            ByteStorage activation(a.values);
            ByteStorage accumulator(gpuMatmulResident(*compute, activation.buffer(), tensor.bytes.buffer(), false, sign,
                                                      batch, rows, gates, a.zeroPoint, z, direction * rows * gates));
            std::vector<int32_t> sums(batch * gates);
            std::memcpy(sums.data(), static_cast<const ByteStorage&>(accumulator).data(),
                        sums.size() * sizeof(int32_t));
            for (size_t b = 0; b < batch; ++b)
                for (size_t g = 0; g < gates; ++g)
                    out[b * gates + g] = static_cast<float>(sums[b * gates + g]) * a.scale *
                                         scales[direction * z.size() + (z.size() == 1 ? 0 : g)];
            return out;
        }
        // Per-output-channel affine zero points do not require unpacking a weight matrix.
        for (size_t b = 0; b < batch; ++b)
            for (size_t g = 0; g < gates; ++g) {
                const size_t p   = scales.size() == directions ? direction : direction * gates + g;
                int64_t      sum = 0;
                for (size_t k = 0; k < rows; ++k) {
                    const int byte = tensor.bytes[(direction * rows + k) * gates + g];
                    const int w    = sign && byte >= 128 ? byte - 256 : byte;
                    sum += static_cast<int64_t>(int(a.values[b * rows + k]) - a.zeroPoint) * (w - zeros[p]);
                }
                if (sum < INT32_MIN || sum > INT32_MAX) throw Failure("LSTM integer accumulator overflow");
                out[b * gates + g] = static_cast<float>(sum) * a.scale * scales[p];
            }
        return out;
    }
};
}  // namespace
std::vector<RuntimeTensor> executeQuantLstm(const Node& n, const std::vector<const RuntimeTensor*>& in,
                                            OnnxCompute* compute) {
    const auto& xt = required(in, 0);
    if (xt.shape.size() != 3) throw Failure("LSTM X must be [time,batch,input]");
    const auto    x    = floats(xt);
    const int64_t time = xt.shape[0], batch = xt.shape[1], input = xt.shape[2], hidden = attr(n, "hidden_size", 0);
    const auto    direction = n.attrs.contains("direction") ? n.attrs.at("direction").text : "forward";
    const int64_t dirs      = direction == "bidirectional" ? 2 : 1;
    if (time <= 0 || batch <= 0 || input <= 0 || hidden <= 0 || hidden > 16384)
        throw Failure("Invalid LSTM dimensions");
    const size_t gates          = 4 * hidden;
    const size_t projectionSize = count({time, batch, static_cast<int64_t>(gates)});
    if (projectionSize > 128u * 1024u * 1024u) throw Failure("LSTM projection exceeds memory limit");
    Weights w(required(in, 1), required(in, 8), required(in, 9), dirs, gates, input);
    Weights r(required(in, 2), required(in, 10), required(in, 11), dirs, gates, hidden);
    auto    biases = optionalFloats(in, 3, {dirs, 8 * hidden});
    auto    h = optionalFloats(in, 5, {dirs, batch, hidden}), c = optionalFloats(in, 6, {dirs, batch, hidden});
    auto    p = optionalFloats(in, 7, {dirs, 3 * hidden});
    std::vector<int64_t> lengths(batch, time);
    if (in.size() > 4 && in[4]) {
        if (in[4]->element != OnnxElement::Int32) throw Failure("LSTM sequence lengths require int32");
        shape(*in[4], {batch});
        lengths = ints(*in[4]);
    }
    for (auto len : lengths)
        if (len < 0 || len > time) throw Failure("Invalid LSTM sequence length");
    const int64_t coupled = attr(n, "input_forget", 0);
    if (coupled != 0 && coupled != 1) throw Failure("Invalid coupled gate flag");
    const float clip = n.attrs.contains("clip") ? n.attrs.at("clip").real : std::numeric_limits<float>::max();
    if (!(clip > 0) || !std::isfinite(clip)) throw Failure("Invalid LSTM clipping threshold");
    auto               sigmoid = [clip](float z) { return 1.f / (1.f + std::exp(-std::clamp(z, -clip, clip))); };
    auto               tanh    = [clip](float z) { return std::tanh(std::clamp(z, -clip, clip)); };
    std::vector<float> y(count({time, dirs, batch, hidden}), 0);
    const auto         qx = checked(affine::dynamicQuantize(x));
    for (int64_t d = 0; d < dirs; ++d) {
        const auto projected = w.multiply(qx, time * batch, d, compute);
        const bool reverse   = direction == "reverse" || d == 1;
        for (int64_t step = 0; step < time; ++step) {
            const auto qh = checked(affine::dynamicQuantize(std::span(h).subspan(d * batch * hidden, batch * hidden)));
            const auto recurrent = r.multiply(qh, batch, d, compute);
            for (int64_t b = 0; b < batch; ++b) {
                if (step >= lengths[b]) continue;
                const int64_t t     = reverse ? lengths[b] - 1 - step : step;
                const size_t  state = (d * batch + b) * hidden;
                for (int64_t j = 0; j < hidden; ++j) {
                    auto gate = [&](int g) {
                        const size_t channel = g * hidden + j;
                        return projected[(t * batch + b) * gates + channel] + recurrent[b * gates + channel] +
                               biases[d * 8 * hidden + channel] + biases[d * 8 * hidden + gates + channel];
                    };
                    const float i = sigmoid(gate(0) + p[d * 3 * hidden + j] * c[state + j]);
                    const float f =
                        coupled ? 1 - i : sigmoid(gate(2) + p[d * 3 * hidden + 2 * hidden + j] * c[state + j]);
                    c[state + j]  = f * c[state + j] + i * tanh(gate(3));
                    const float o = sigmoid(gate(1) + p[d * 3 * hidden + hidden + j] * c[state + j]);
                    h[state + j]  = o * tanh(c[state + j]);
                    y[((t * dirs + d) * batch + b) * hidden + j] = h[state + j];
                }
            }
        }
    }
    return {make(OnnxElement::Float32, {time, dirs, batch, hidden}, y),
            make(OnnxElement::Float32, {dirs, batch, hidden}, h), make(OnnxElement::Float32, {dirs, batch, hidden}, c)};
}
}  // namespace eve::tensor::onnx_detail
