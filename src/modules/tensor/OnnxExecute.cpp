#include "tensor/CpuKernels.h"
#include "tensor/OnnxInternal.h"
#include "tensor/Tensor.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

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
