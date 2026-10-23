#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include "tensor/OnnxCompute.h"
#include "tensor/OnnxInternal.h"

// OnnxMisc.cpp
namespace eve::tensor::onnx_detail {
std::optional<RuntimeTensor> executeMisc(const Node& n, const std::vector<const RuntimeTensor*>& in,
                                         OnnxCompute* compute) {
    const auto& x = required(in, 0);
    if (n.op == "Clip") {
        if (x.element != OnnxElement::Float32)
            throw Failure("Clip currently requires FP32", DiagnosticCode::Unsupported);
        auto low      = make(x.element, {}, std::vector<float>{-std::numeric_limits<float>::infinity()}),
             high     = make(x.element, {}, std::vector<float>{std::numeric_limits<float>::infinity()});
        const auto& a = in.size() > 1 && in[1] ? *in[1] : low;
        const auto& b = in.size() > 2 && in[2] ? *in[2] : high;
        if (a.element != x.element || b.element != x.element || count(a.shape) != 1 || count(b.shape) != 1)
            throw Failure("Clip bounds must be matching scalars");
        if (compute) return dispatchFloat(*compute, {&x, &a, &b}, x.shape, "y[i]=min(max(x0[i],x1[0]),x2[0]);");
        auto v = floats(x);
        for (auto& f : v) f = std::min(std::max(f, read<float>(a, 0)), read<float>(b, 0));
        return make(x.element, x.shape, v);
    }
    if (n.op == "CumSum") {
        const auto& at = required(in, 1);
        if (count(at.shape) != 1) throw Failure("CumSum axis must be scalar");
        const int a = axis(integer(at), x.shape.size());
        if (x.element != OnnxElement::Float32)
            throw Failure("CumSum currently requires FP32", DiagnosticCode::Unsupported);
        const bool reverse = attr(n, "reverse", 0) != 0, exclusive = attr(n, "exclusive", 0) != 0;
        size_t     outer = 1, inner = 1;
        for (int j = 0; j < a; ++j) outer *= x.shape[j];
        for (size_t j = a + 1; j < x.shape.size(); ++j) inner *= x.shape[j];
        const auto width = x.shape[a];
        if (compute && outer && inner && width) {
            std::ostringstream s;
            s << "uint base=i/" << inner << "u*" << inner * width << "u+i%" << inner
              << "u;precise float sum=0.0;for(uint j=0;j<" << width << "u;++j){uint index=base+"
              << (reverse ? "(" + std::to_string(width - 1) + "u-j)" : "j") << "*" << inner << "u;"
              << (exclusive ? "y[index]=sum;sum+=x0[index];" : "sum+=x0[index];y[index]=sum;") << "}";
            return dispatchFloat(*compute, {&x}, x.shape, s.str(), outer * inner);
        }
        auto out = floats(x);
        for (size_t o = 0; o < outer; ++o)
            for (size_t i = 0; i < inner; ++i) {
                float sum = 0;
                for (int64_t j = 0; j < width; ++j) {
                    size_t      p = (o * width + (reverse ? width - 1 - j : j)) * inner + i;
                    const float v = out[p];
                    if (exclusive) {
                        out[p] = sum;
                        sum += v;
                    } else {
                        sum += v;
                        out[p] = sum;
                    }
                }
            }
        return make(x.element, x.shape, out);
    }
    if (n.op == "Pad") {
        const auto pads = ints(required(in, 1));
        if (pads.size() != 2 * x.shape.size()) throw Failure("Pad rank mismatch");
        auto       shape = x.shape;
        const auto mode  = n.attrs.contains("mode") ? n.attrs.at("mode").text : "constant";
        for (size_t j = 0; j < shape.size(); ++j) {
            if (pads[j] < -INT32_MAX || pads[j] > INT32_MAX || pads[j + shape.size()] < -INT32_MAX ||
                pads[j + shape.size()] > INT32_MAX)
                throw Failure("Pad extent out of range");
            shape[j] += pads[j] + pads[j + shape.size()];
            if (mode != "constant" && x.shape[j] <= 0)
                throw Failure("Edge/reflect padding requires nonempty dimensions");
            if (mode == "reflect" && (pads[j] >= x.shape[j] || pads[j + shape.size()] >= x.shape[j]))
                throw Failure("Reflect padding exceeds input dimension");
        }
        const auto           size = elementSize(x.element);
        std::vector<uint8_t> fill(size, 0);
        if (in.size() > 2 && in[2]) {
            if (in[2]->element != x.element || count(in[2]->shape) != 1)
                throw Failure("Pad value dtype/shape mismatch");
            fill = in[2]->bytes;
        }
        RuntimeTensor out{x.element, shape, std::vector<uint8_t>(count(shape) * size)};
        for (size_t i = 0; i < count(shape); ++i) {
            size_t rest = i, source = 0, stride = 1;
            bool   outside = false;
            for (size_t j = shape.size(); j > 0; --j) {
                int64_t p = static_cast<int64_t>(rest % shape[j - 1]) - pads[j - 1];
                rest /= shape[j - 1];
                if (p < 0 || p >= x.shape[j - 1]) {
                    if (mode == "constant")
                        outside = true;
                    else if (mode == "edge")
                        p = std::clamp(p, int64_t(0), x.shape[j - 1] - 1);
                    else
                        p = p < 0 ? -p : 2 * x.shape[j - 1] - 2 - p;
                }
                if (!outside) source += p * stride;
                stride *= x.shape[j - 1];
            }
            std::memcpy(out.bytes.data() + i * size, outside ? fill.data() : x.bytes.data() + source * size, size);
        }
        return out;
    }
    return std::nullopt;
}
}  // namespace eve::tensor::onnx_detail

// OnnxModel.cpp
namespace eve::tensor {
struct OnnxModel::Impl {
    onnx_detail::ModelData model;
};
OnnxModel::OnnxModel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxModel::~OnnxModel() = default;
OnnxModelInfo                      OnnxModel::info() const { return impl_->model.info; }
Result<std::unique_ptr<OnnxModel>> OnnxModel::load(std::span<const uint8_t> bytes) {
    try {
        auto impl   = std::make_unique<Impl>();
        impl->model = onnx_detail::parse(bytes);
        return Result<std::unique_ptr<OnnxModel>>::success(std::unique_ptr<OnnxModel>(new OnnxModel(std::move(impl))));
    } catch (const onnx_detail::Failure& e) {
        return Result<std::unique_ptr<OnnxModel>>::failure(
            Diagnostic::error(e.code, e.what(), {}, {}, "tensor.onnx.import"));
    } catch (const std::exception& e) {
        return Result<std::unique_ptr<OnnxModel>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, e.what(), {}, {}, "tensor.onnx.import"));
    }
}
Result<std::vector<OnnxNamedTensor>> OnnxModel::run(std::span<const OnnxNamedTensor> feeds,
                                                    std::span<const std::string>     requested,
                                                    OnnxRunOptions                   options) const {
    return runInternal(feeds, requested, nullptr, options);
}
Result<OnnxGpuResult> OnnxModel::runGpu(std::span<const OnnxNamedTensor> feeds, OnnxCompute& compute,
                                        std::span<const std::string> requested, OnnxRunOptions options) const {
    class Counter final : public OnnxCompute {
    public:
        OnnxCompute& target;
        size_t       count = 0;
        explicit Counter(OnnxCompute& t) : target(t) {}
        Result<std::vector<uint8_t>> dispatch(const OnnxKernel& k) override {
            auto r = target.dispatch(k);
            if (r.ok()) ++count;
            return r;
        }
        Result<OnnxBuffer> enqueue(const std::string& source, std::span<const OnnxBuffer> inputs, size_t bytes,
                                   uint32_t work) override {
            auto r = target.enqueue(source, inputs, bytes, work);
            if (r.ok()) ++count;
            return r;
        }
    } counter(compute);
    compute.beginRun();
    struct RunScope {
        OnnxCompute& device;
        ~RunScope() { device.endRun(); }
    } scope{compute};
    auto r = runInternal(feeds, requested, &counter, options);
    if (!r.ok()) return Result<OnnxGpuResult>::failure(r.status());
    return Result<OnnxGpuResult>::success({std::move(r.value()), counter.count, compute.transferStats()});
}
Result<std::vector<OnnxNamedTensor>> OnnxModel::runInternal(std::span<const OnnxNamedTensor> feeds,
                                                            std::span<const std::string>     requested,
                                                            OnnxCompute* compute, OnnxRunOptions options) const {
    using namespace onnx_detail;
    std::string nodeName;
    try {
        const auto&                    model = impl_->model;
        const std::vector<std::string> targets =
            requested.empty() ? model.info.outputs : std::vector<std::string>(requested.begin(), requested.end());
        std::set<std::string> seenFeeds;
        for (const auto& f : feeds) {
            nodeName = f.name;
            if (count(f.tensor.shape) * elementSize(f.tensor.element) != f.tensor.bytes.size())
                throw Failure("Tensor byte count mismatch");
            auto input = model.inputs.find(f.name);
            if (input == model.inputs.end() || !seenFeeds.insert(f.name).second)
                throw Failure("Unknown or duplicate feed");
            if (static_cast<int>(f.tensor.element) != input->second.type ||
                f.tensor.shape.size() != input->second.shape.size())
                throw Failure("Feed type/rank mismatch");
            for (size_t i = 0; i < f.tensor.shape.size(); ++i)
                if (input->second.shape[i] >= 0 && input->second.shape[i] != f.tensor.shape[i])
                    throw Failure("Feed dimension mismatch");
        }
        nodeName.clear();
        auto result = evaluate(model, feeds, targets, compute, options);
        return Result<std::vector<OnnxNamedTensor>>::success(std::move(result));
    } catch (const Failure& e) {
        return Result<std::vector<OnnxNamedTensor>>::failure(
            Diagnostic::error(e.code, e.what(), e.path.empty() ? nodeName : e.path, {}, "tensor.onnx.run"));
    } catch (const std::exception& e) {
        return Result<std::vector<OnnxNamedTensor>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, e.what(), nodeName, {}, "tensor.onnx.run"));
    }
}
}  // namespace eve::tensor

// OnnxNeural.cpp
namespace eve::tensor::onnx_detail {
std::optional<RuntimeTensor> executeNeural(const Node& n, const std::vector<const RuntimeTensor*>& in,
                                           OnnxCompute* compute) {
    const auto& x = required(in, 0);
    if (x.element != OnnxElement::Float32) return std::nullopt;
    if (n.op == "InstanceNormalization") {
        const auto& scale = required(in, 1);
        const auto& bias  = required(in, 2);
        if (x.shape.size() < 3 || scale.shape != std::vector<int64_t>{x.shape[1]} || bias.shape != scale.shape ||
            scale.element != x.element || bias.element != x.element)
            throw Failure("InstanceNormalization shape/type mismatch");
        const float epsilon = n.attrs.contains("epsilon") ? n.attrs.at("epsilon").real : 1e-5f;
        if (!(epsilon > 0) || !std::isfinite(epsilon)) throw Failure("Invalid normalization epsilon");
        size_t spatial = 1;
        for (size_t j = 2; j < x.shape.size(); ++j) spatial *= x.shape[j];
        if (!spatial) throw Failure("Empty InstanceNormalization");
        if (compute) {
            std::ostringstream s;
            s.precision(9);
            s << std::scientific;
            s << "uint base=i*" << spatial << "u;precise float mean=0.0;for(uint j=0;j<" << spatial
              << "u;++j)mean+=x0[base+j];mean/=" << spatial << ".0;precise float var=0.0;for(uint j=0;j<" << spatial
              << "u;++j){float d=x0[base+j]-mean;var+=d*d;}float inv=inversesqrt(var/" << spatial << ".0+" << epsilon
              << ");for(uint j=0;j<" << spatial << "u;++j)y[base+j]=(x0[base+j]-mean)*inv*x1[i%" << x.shape[1]
              << "u]+x2[i%" << x.shape[1] << "u];";
            return dispatchFloat(*compute, {&x, &scale, &bias}, x.shape, s.str(), count(x.shape) / spatial);
        }
        auto out = floats(x);
        for (size_t i = 0; i < out.size() / spatial; ++i) {
            float mean = 0, var = 0;
            for (size_t j = 0; j < spatial; ++j) mean += out[i * spatial + j];
            mean /= spatial;
            for (size_t j = 0; j < spatial; ++j) {
                float d = out[i * spatial + j] - mean;
                var += d * d;
            }
            const float multiplier = read<float>(scale, i % x.shape[1]) / std::sqrt(var / spatial + epsilon),
                        offset     = read<float>(bias, i % x.shape[1]);
            for (size_t j = 0; j < spatial; ++j)
                out[i * spatial + j] = (out[i * spatial + j] - mean) * multiplier + offset;
        }
        return make(x.element, x.shape, out);
    }
    if (n.op == "ConvTranspose") {
        const auto& w = required(in, 1);
        if (x.shape.size() != 3 || w.shape.size() != 3 || w.element != x.element || w.shape[0] != x.shape[1])
            throw Failure("ConvTranspose requires NCL / CIO weights", DiagnosticCode::Unsupported);
        const auto strides = attrs(n, "strides", {1}), pads = attrs(n, "pads", {0, 0}),
                   dilations = attrs(n, "dilations", {1}), extra = attrs(n, "output_padding", {0});
        if (strides.size() != 1 || pads.size() != 2 || dilations.size() != 1 || extra.size() != 1)
            throw Failure("ConvTranspose attribute rank mismatch");
        const int64_t stride = strides[0], dilation = dilations[0], groups = attr(n, "group", 1), channels = x.shape[1],
                      length = x.shape[2], kernel = w.shape[2], cout = w.shape[1];
        if (stride <= 0 || stride > 65536 || dilation <= 0 || dilation > 65536 || groups <= 0 || channels % groups ||
            pads[0] < 0 || pads[1] < 0 || extra[0] < 0 || extra[0] >= std::max(stride, dilation) || length <= 0 ||
            kernel <= 0 || cout <= 0)
            throw Failure("Invalid ConvTranspose geometry");
        if (n.attrs.contains("kernel_shape") && attrs(n, "kernel_shape", {}) != std::vector<int64_t>{kernel})
            throw Failure("ConvTranspose kernel_shape mismatch");
        const int64_t width   = stride * (length - 1) + extra[0] + dilation * (kernel - 1) + 1 - pads[0] - pads[1],
                      outputs = cout * groups;
        if (width <= 0 || width > INT32_MAX) throw Failure("Invalid ConvTranspose output extent");
        std::vector<int64_t> shape{x.shape[0], outputs, width};
        count(shape);
        RuntimeTensor zeroBias = make(OnnxElement::Float32, {outputs}, std::vector<float>(outputs, 0));
        const auto&   bias     = in.size() > 2 && in[2] ? *in[2] : zeroBias;
        if (bias.element != x.element || bias.shape != zeroBias.shape) throw Failure("ConvTranspose bias mismatch");
        if (compute) {
            std::ostringstream s;
            s << "uint pos=i%" << width << "u,o=i/" << width << "u%" << outputs << "u,batch=i/" << width * outputs
              << "u;precise float v=x2[o];for(uint c=0;c<" << channels / groups << "u;++c)for(uint j=0;j<" << kernel
              << "u;++j){int z=int(pos)+" << pads[0] << "-int(j)*" << dilation << ";if(z<0||z%" << stride << "!=0||z/"
              << stride << ">=" << length << ")continue;uint ch=o/" << cout << "u*" << channels / groups
              << "u+c;v+=x0[(batch*" << channels << "u+ch)*" << length << "u+uint(z/" << stride << ")]*x1[(ch*" << cout
              << "u+o%" << cout << "u)*" << kernel << "u+j];}y[i]=v;";
            return dispatchFloat(*compute, {&x, &w, &bias}, shape, s.str());
        }
        std::vector<float> out(count(shape));
        for (int64_t batch = 0; batch < x.shape[0]; ++batch)
            for (int64_t o = 0; o < outputs; ++o)
                for (int64_t pos = 0; pos < width; ++pos) {
                    float v = read<float>(bias, o);
                    for (int64_t c = 0; c < channels / groups; ++c)
                        for (int64_t j = 0; j < kernel; ++j) {
                            int64_t z = pos + pads[0] - j * dilation;
                            if (z < 0 || z % stride || z / stride >= length) continue;
                            int64_t ch = o / cout * (channels / groups) + c;
                            v += read<float>(x, (batch * channels + ch) * length + z / stride) *
                                 read<float>(w, (ch * cout + o % cout) * kernel + j);
                        }
                    out[(batch * outputs + o) * width + pos] = v;
                }
        return make(x.element, shape, out);
    }
    if (n.op == "Resize") {
        if (in.size() > 1 && in[1] && !in[1]->bytes.empty())
            throw Failure("Resize ROI unsupported", DiagnosticCode::Unsupported);
        std::vector<double> scales;
        auto                shape = x.shape;
        if (in.size() > 2 && in[2] && !in[2]->bytes.empty()) {
            auto s = floats(*in[2]);
            scales.assign(s.begin(), s.end());
            if (scales.size() != shape.size()) throw Failure("Resize scales rank mismatch");
            for (size_t j = 0; j < shape.size(); ++j) {
                if (!(scales[j] > 0) || !std::isfinite(scales[j]) || std::floor(shape[j] * scales[j]) > INT32_MAX)
                    throw Failure("Invalid Resize scale");
                shape[j] = static_cast<int64_t>(std::floor(shape[j] * scales[j]));
            }
        } else {
            shape = ints(required(in, 3));
            if (shape.size() != x.shape.size()) throw Failure("Resize sizes rank mismatch");
            for (size_t j = 0; j < shape.size(); ++j) {
                if (x.shape[j] <= 0) throw Failure("Empty Resize input");
                scales.push_back(double(shape[j]) / x.shape[j]);
            }
        }
        count(shape);
        const auto mode       = n.attrs.contains("mode") ? n.attrs.at("mode").text : "nearest",
                   coordinate = n.attrs.contains("coordinate_transformation_mode")
                                    ? n.attrs.at("coordinate_transformation_mode").text
                                    : "half_pixel";
        for (auto d : x.shape)
            if (d <= 0) throw Failure("Empty Resize input");
        if (mode == "nearest") {
            if (compute) {
                std::ostringstream s;
                s.precision(17);
                s << "uint rest=i,source=0u;";
                size_t stride = 1;
                for (size_t j = shape.size(); j > 0; --j) {
                    s << "source+=uint(min(floor(float(rest%" << shape[j - 1] << "u)/" << std::scientific
                      << scales[j - 1] << ")," << double(x.shape[j - 1] - 1) << "))*" << stride
                      << "u;rest/=" << shape[j - 1] << "u;";
                    stride *= x.shape[j - 1];
                }
                s << "y[i]=x0[source];";
                return dispatchFloat(*compute, {&x}, shape, s.str());
            }
            std::vector<float> out(count(shape));
            for (size_t i = 0; i < out.size(); ++i) {
                size_t rest = i, source = 0, stride = 1;
                for (size_t j = shape.size(); j > 0; --j) {
                    source += std::min(int64_t(std::floor((rest % shape[j - 1]) / scales[j - 1])), x.shape[j - 1] - 1) *
                              stride;
                    rest /= shape[j - 1];
                    stride *= x.shape[j - 1];
                }
                out[i] = read<float>(x, source);
            }
            return make(x.element, shape, out);
        }
        // The speech model interpolates its final axis only. Reject other geometries explicitly.
        for (size_t j = 0; j + 1 < shape.size(); ++j)
            if (shape[j] != x.shape[j])
                throw Failure("Linear Resize supports final axis only", DiagnosticCode::Unsupported);
        if (shape.empty()) throw Failure("Resize scalar unsupported");
        const auto width = shape.back(), old = x.shape.back();
        if (!width) return RuntimeTensor{x.element, shape, {}};
        auto position = [&](int64_t p) {
            return coordinate == "half_pixel" ? (p + .5) / scales.back() - .5 : p / scales.back();
        };
        if (compute) {
            std::ostringstream s;
            s.precision(17);
            s << std::scientific;
            s << "uint base=i/" << width << "u*" << old
              << "u;float p=" << (coordinate == "half_pixel" ? "(float(i%" : "float(i%") << width
              << (coordinate == "half_pixel" ? "u)+0.5)/" : "u)/") << scales.back()
              << (coordinate == "half_pixel" ? "-0.5" : "") << ";float t=floor(p),f=p-t;uint a=uint(clamp(t,0.0,"
              << double(old - 1) << ")),b=uint(clamp(t+1.0,0.0," << double(old - 1)
              << "));y[i]=x0[base+a]*(1.0-f)+x0[base+b]*f;";
            return dispatchFloat(*compute, {&x}, shape, s.str());
        }
        std::vector<float> out(count(shape));
        for (size_t i = 0; i < out.size(); ++i) {
            double p = position(i % width), lo = std::floor(p), f = p - lo;
            size_t base = i / width * old;
            out[i] = static_cast<float>(read<float>(x, base + std::clamp(int64_t(lo), int64_t(0), old - 1)) * (1 - f) +
                                        read<float>(x, base + std::clamp(int64_t(lo + 1), int64_t(0), old - 1)) * f);
        }
        return make(x.element, shape, out);
    }
    return std::nullopt;
}
}  // namespace eve::tensor::onnx_detail

// OnnxNumeric.cpp
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
