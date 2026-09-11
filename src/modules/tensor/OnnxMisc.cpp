#include <algorithm>
#include <limits>
#include <sstream>
#include "tensor/OnnxInternal.h"

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
