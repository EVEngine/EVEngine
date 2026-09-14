#include <algorithm>
#include <cmath>
#include <sstream>
#include "tensor/OnnxInternal.h"

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
