#include "tensor/AffineQuant.h"
#include "tensor/OnnxGpuKernels.h"
#include "tensor/OnnxInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
