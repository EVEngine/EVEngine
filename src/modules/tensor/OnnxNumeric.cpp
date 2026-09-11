#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include "tensor/OnnxCompute.h"
#include "tensor/OnnxInternal.h"

namespace eve::tensor::onnx_detail {
namespace {
std::string literal(float v) {
    std::ostringstream s;
    s.precision(9);
    s << std::scientific << v;
    return s.str();
}
std::string indexCode(const std::vector<int64_t>& input, const std::vector<int64_t>& output, const char* index) {
    std::string result = "0u";
    size_t      src = 1, dst = 1;
    for (size_t j = 0; j < output.size(); ++j) {
        if (j < input.size()) {
            const auto dim = input[input.size() - 1 - j];
            if (dim != 1)
                result += "+(" + std::string(index) + "/" + std::to_string(dst) + "u%" + std::to_string(dim) + "u)*" +
                          std::to_string(src) + "u";
            src *= dim;
        }
        dst *= output[output.size() - 1 - j];
    }
    return result;
}
}  // namespace
RuntimeTensor dispatchFloat(OnnxCompute& compute, const std::vector<const RuntimeTensor*>& inputs,
                            const std::vector<int64_t>& shape, const std::string& body, size_t work) {
    RuntimeTensor out{OnnxElement::Float32, shape, std::vector<uint8_t>(count(shape) * 4)};
    if (out.bytes.empty()) return out;
    if (!work) work = count(shape);
    if (work > 65535u * 64u) throw Failure("GPU float dispatch limit exceeded", DiagnosticCode::Unsupported);
    OnnxKernel              k;
    std::vector<OnnxBuffer> buffers;
    k.outputBytes = out.bytes.size();
    k.workItems   = static_cast<uint32_t>(work);
    k.source      = "#version 450\nlayout(local_size_x=64)in;\n";
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i]->element != OnnxElement::Float32 && inputs[i]->element != OnnxElement::Int32)
            throw Failure("GPU float input type mismatch");
        buffers.push_back(inputs[i]->bytes.buffer());
        k.source += "layout(std430,binding=" + std::to_string(i) + ")readonly buffer B" + std::to_string(i) +
                    (inputs[i]->element == OnnxElement::Int32 ? "{int x" : "{float x") + std::to_string(i) + "[];};\n";
    }
    k.source +=
        "layout(std430,binding=" + std::to_string(inputs.size()) +
        ")writeonly buffer Y{float y[];};\nvoid main(){uint i=gl_GlobalInvocationID.x;if(i>=" + std::to_string(work) +
        "u)return;" + body + "}";
    auto r = compute.enqueue(k.source, buffers, k.outputBytes, k.workItems);
    if (!r.ok()) throw Failure(r.error()->message(), r.error()->code());
    if (r.value().size != out.bytes.size()) throw Failure("GPU output size mismatch");
    out.bytes = std::move(r.value());
    return out;
}
std::optional<RuntimeTensor> executeNumeric(const Node& n, const std::vector<const RuntimeTensor*>& in,
                                            OnnxCompute* compute) {
    const auto& x = required(in, 0);
    if (x.element != OnnxElement::Float32) return std::nullopt;
    if (n.op == "MatMul") {
        const auto& b = required(in, 1);
        if (b.element != x.element || x.shape.empty() || b.shape.empty()) throw Failure("MatMul type/rank mismatch");
        auto       as = x.shape, bs = b.shape;
        const bool av = as.size() == 1, bv = bs.size() == 1;
        if (av) as.insert(as.begin(), 1);
        if (bv) bs.push_back(1);
        const auto m = as[as.size() - 2], k = as.back(), cols = bs.back();
        if (k != bs[bs.size() - 2]) throw Failure("MatMul inner dimension mismatch");
        std::vector<int64_t> ab(as.begin(), as.end() - 2), bb(bs.begin(), bs.end() - 2);
        auto                 batches = broadcast(ab, bb);
        auto                 shape   = batches;
        shape.push_back(m);
        shape.push_back(cols);
        count(shape);
        if (av) shape.erase(shape.end() - 2);
        if (bv) shape.pop_back();
        if (compute && m && cols) {
            std::string body = "uint batch=i/" + std::to_string(m * cols) + "u,r=i/" + std::to_string(cols) + "u%" +
                               std::to_string(m) + "u,c=i%" + std::to_string(cols) +
                               "u;precise float v=0.0;for(uint j=0;j<" + std::to_string(k) + "u;++j)v+=x0[(" +
                               indexCode(ab, batches, "batch") + ")*" + std::to_string(m * k) + "u+r*" +
                               std::to_string(k) + "u+j]*x1[(" + indexCode(bb, batches, "batch") + ")*" +
                               std::to_string(k * cols) + "u+j*" + std::to_string(cols) + "u+c];y[i]=v;";
            return dispatchFloat(*compute, {&x, &b}, shape, body);
        }
        std::vector<float> out(count(shape), 0);
        for (size_t batch = 0; batch < count(batches); ++batch)
            for (int64_t r = 0; r < m; ++r)
                for (int64_t c = 0; c < cols; ++c) {
                    float sum = 0;
                    for (int64_t j = 0; j < k; ++j)
                        sum += read<float>(x, broadcastIndex(batch, ab, batches) * m * k + r * k + j) *
                               read<float>(b, broadcastIndex(batch, bb, batches) * k * cols + j * cols + c);
                    out[(batch * m + r) * cols + c] = sum;
                }
        return make(OnnxElement::Float32, shape, out);
    }
    static const std::set<std::string> binary{"Add", "Sub", "Mul", "Div", "Pow"};
    if (binary.contains(n.op)) {
        const auto& b = required(in, 1);
        if (b.element != x.element) throw Failure("Float arithmetic dtype mismatch");
        auto shape = broadcast(x.shape, b.shape);
        if (compute) {
            const std::string a = "x0[" + indexCode(x.shape, shape, "i") + "]",
                              v = "x1[" + indexCode(b.shape, shape, "i") + "]";
            const std::string e = n.op == "Pow" ? "pow(" + a + "," + v + ")"
                                                : a +
                                                      (n.op == "Add"   ? "+"
                                                       : n.op == "Sub" ? "-"
                                                       : n.op == "Mul" ? "*"
                                                                       : "/") +
                                                      v;
            if (n.op == "Pow")
                return dispatchFloat(*compute, {&x, &b}, shape,
                                     "float a=" + a + ",b=" + v +
                                         ";float value;if(b==0.0)value=1.0;else "
                                         "if(a==0.0)value=b>0.0?0.0:uintBitsToFloat(0x7f800000u);else "
                                         "if(a<0.0)value=b==floor(b)?pow(-a,b)*(mod(abs(b),2.0)==0.0?1.0:-1.0):"
                                         "uintBitsToFloat(0x7fc00000u);else value=pow(a,b);y[i]=value;");
            return dispatchFloat(*compute, {&x, &b}, shape, "y[i]=" + e + ";");
        }
        std::vector<float> out(count(shape));
        for (size_t i = 0; i < out.size(); ++i) {
            const float a = read<float>(x, broadcastIndex(i, x.shape, shape)),
                        v = read<float>(b, broadcastIndex(i, b.shape, shape));
            out[i]        = n.op == "Add"   ? a + v
                            : n.op == "Sub" ? a - v
                            : n.op == "Mul" ? a * v
                            : n.op == "Div" ? a / v
                                            : std::pow(a, v);
        }
        return make(OnnxElement::Float32, shape, out);
    }
    if (n.op == "ReduceMean" || n.op == "ReduceSum") {
        auto axes = n.op == "ReduceMean"     ? attrs(n, "axes", {})
                    : in.size() > 1 && in[1] ? ints(*in[1])
                                             : std::vector<int64_t>{};
        if (axes.empty() && n.op == "ReduceSum" && attr(n, "noop_with_empty_axes", 0)) return x;
        if (axes.empty())
            for (size_t i = 0; i < x.shape.size(); ++i) axes.push_back(i);
        std::set<int> set;
        for (auto a : axes)
            if (!set.insert(axis(a, x.shape.size())).second) throw Failure("Duplicate reduction axis");
        std::vector<int64_t> outerShape, reducedShape, shape;
        std::vector<size_t>  outerStrides, reducedStrides;
        size_t               stride = count(x.shape);
        for (size_t j = 0; j < x.shape.size(); ++j) {
            if (!x.shape[j]) throw Failure("Empty reduction unsupported", DiagnosticCode::Unsupported);
            stride /= x.shape[j];
            if (set.contains(static_cast<int>(j))) {
                reducedShape.push_back(x.shape[j]);
                reducedStrides.push_back(stride);
                if (attr(n, "keepdims", 1)) shape.push_back(1);
            } else {
                outerShape.push_back(x.shape[j]);
                outerStrides.push_back(stride);
                shape.push_back(x.shape[j]);
            }
        }
        auto offset = [](size_t i, const std::vector<int64_t>& dims, const std::vector<size_t>& strides) {
            size_t p = 0;
            for (size_t j = dims.size(); j > 0; --j) {
                p += (i % dims[j - 1]) * strides[j - 1];
                i /= dims[j - 1];
            }
            return p;
        };
        auto code = [](const char* var, const std::vector<int64_t>& dims, const std::vector<size_t>& strides) {
            std::string s       = "0u";
            size_t      divisor = 1;
            for (size_t j = dims.size(); j > 0; --j) {
                s += "+(" + std::string(var) + "/" + std::to_string(divisor) + "u%" + std::to_string(dims[j - 1]) +
                     "u)*" + std::to_string(strides[j - 1]) + "u";
                divisor *= dims[j - 1];
            }
            return s;
        };
        const size_t terms = count(reducedShape);
        if (compute)
            return dispatchFloat(*compute, {&x}, shape,
                                 "uint base=" + code("i", outerShape, outerStrides) +
                                     ";precise float v=0.0;for(uint j=0;j<" + std::to_string(terms) +
                                     "u;++j)v+=x0[base+(" + code("j", reducedShape, reducedStrides) + ")];y[i]=v" +
                                     (n.op == "ReduceMean" ? "/" + std::to_string(terms) + ".0" : "") + ";");
        std::vector<float> out(count(shape));
        for (size_t i = 0; i < out.size(); ++i) {
            float v = 0;
            for (size_t j = 0; j < terms; ++j)
                v += read<float>(x, offset(i, outerShape, outerStrides) + offset(j, reducedShape, reducedStrides));
            out[i] = n.op == "ReduceMean" ? v / terms : v;
        }
        return make(OnnxElement::Float32, shape, out);
    }
    if (n.op == "Softmax" && compute) {
        int    a     = axis(attr(n, "axis", -1), x.shape.size());
        size_t inner = 1, outer = 1;
        for (int j = 0; j < a; ++j) outer *= x.shape[j];
        for (size_t j = a + 1; j < x.shape.size(); ++j) inner *= x.shape[j];
        const auto channels = x.shape[a];
        if (!channels || !inner) return RuntimeTensor{x.element, x.shape, {}};
        auto ch = std::to_string(channels), ins = std::to_string(inner);
        return dispatchFloat(*compute, {&x}, x.shape,
                             "uint base=i/" + ins + "u*" + ins + "u*" + ch + "u+i%" + ins +
                                 "u;float m=-3.402823466e38;for(uint j=0;j<" + ch + "u;++j)m=max(m,x0[base+j*" + ins +
                                 "u]);float sum=0.0;for(uint j=0;j<" + ch + "u;++j)sum+=exp(x0[base+j*" + ins +
                                 "u]-m);for(uint j=0;j<" + ch + "u;++j)y[base+j*" + ins + "u]=exp(x0[base+j*" + ins +
                                 "u]-m)/sum;",
                             outer * inner);
    }
    static const std::map<std::string, std::string> unary{{"Sqrt", "sqrt(v)"},
                                                          {"Exp", "exp(v)"},
                                                          {"Log", "log(v)"},
                                                          {"Sin", "sin(v)"},
                                                          {"Cos", "cos(v)"},
                                                          {"Relu", "max(v,0.0)"},
                                                          {"Sigmoid", "1.0/(1.0+exp(-v))"},
                                                          {"Tanh", "isnan(v)?v:tanh(clamp(v,-10.0,10.0))"},
                                                          {"Neg", "-v"},
                                                          {"Abs", "abs(v)"},
                                                          {"Reciprocal", "1.0/v"},
                                                          {"Floor", "floor(v)"},
                                                          {"Round", "roundEven(v)"},
                                                          {"Atan", "isinf(v)?sign(v)*1.5707963267948966:atan(v)"}};
    if (n.op == "LeakyRelu" || unary.contains(n.op)) {
        const float alpha = n.attrs.contains("alpha") ? n.attrs.at("alpha").real : 0.01f;
        if (!std::isfinite(alpha)) throw Failure("Invalid LeakyRelu alpha");
        if (compute)
            return dispatchFloat(
                *compute, {&x}, x.shape,
                "float v=x0[i];y[i]=" + (n.op == "LeakyRelu" ? "v>=0.0?v:v*" + literal(alpha) : unary.at(n.op)) + ";");
        if (n.op != "LeakyRelu" && n.op != "Reciprocal" && n.op != "Floor" && n.op != "Round" && n.op != "Atan")
            return std::nullopt;
        auto out = floats(x);
        for (auto& v : out) {
            if (n.op == "LeakyRelu")
                v = v >= 0 ? v : alpha * v;
            else if (n.op == "Reciprocal")
                v = 1 / v;
            else if (n.op == "Floor")
                v = std::floor(v);
            else if (n.op == "Atan")
                v = std::atan(v);
            else {
                float lo = std::floor(v), f = v - lo;
                v = lo + (f > 0.5f || (f == 0.5f && std::fmod(lo, 2.f) != 0));
            }
        }
        return make(OnnxElement::Float32, x.shape, out);
    }
    return std::nullopt;
}
}  // namespace eve::tensor::onnx_detail
