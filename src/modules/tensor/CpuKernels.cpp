#include "tensor/CpuKernels.h"
#include "tensor/Tensor.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace eve::tensor::kernels {
namespace {

int product(const int *dims, int rank) {
    int n = 1;
    for (int i = 0; i < rank; ++i) {
        if (dims[i] <= 0) throw Exception("Tensor kernel: dims must be > 0");
        n *= dims[i];
    }
    return n;
}

float applyBinary(OpType type, float x, float y) {
    switch (type) {
        case OpType::Add: return x + y;
        case OpType::Sub: return x - y;
        case OpType::Multiply: return x * y;
        case OpType::Divide: return x / y;
        default: throw Exception("Tensor kernel: not a binary op");
    }
}

/** Row-major stride per output dim; broadcast dims (input size 1) get stride 0. */
void broadcastStrides(const int *inDims, int inRank, int outRank, int *strides) {
    const int pad = outRank - inRank;
    for (int k = 0; k < outRank; ++k) {
        const int d = k < pad ? 1 : inDims[k - pad];
        int s = 1;
        for (int t = k + 1; t < outRank; ++t) {
            const int dt = t < pad ? 1 : inDims[t - pad];
            if (dt != 1) s *= dt;
        }
        strides[k] = (d == 1) ? 0 : s;
    }
}

int normalizeAxis(int axis, int rank) {
    if (axis < 0) axis += rank;
    if (axis < 0 || axis >= rank) throw Exception("Tensor kernel: axis out of range");
    return axis;
}

}  // namespace

float applyUnary(OpType t, float x, float s0, float s1) {
    switch (t) {
        case OpType::Neg: return -x;
        case OpType::Abs: return std::fabs(x);
        case OpType::Sqrt: return std::sqrt(x);
        case OpType::Exp: return std::exp(x);
        case OpType::Log: return std::log(x);
        case OpType::Sin: return std::sin(x);
        case OpType::Cos: return std::cos(x);
        case OpType::Tanh: return std::tanh(x);
        case OpType::Relu: return x > 0.f ? x : 0.f;
        case OpType::Sigmoid: return 1.f / (1.f + std::exp(-x));
        case OpType::Gelu:
            // tanh approximation (matches the generated GPU kernels)
            return 0.5f * x * (1.f + std::tanh(0.7978845608028654f * (x + 0.044715f * x * x * x)));
        case OpType::Silu: return x / (1.f + std::exp(-x));
        case OpType::AddScalar: return x + s0;
        case OpType::SubScalar: return x - s0;
        case OpType::MulScalar: return x * s0;
        case OpType::DivScalar: return x / s0;
        case OpType::PowScalar: return std::pow(x, s0);
        case OpType::Clamp: {
            float lo = s0, hi = s1;
            if (lo > hi) std::swap(lo, hi);
            return std::clamp(x, lo, hi);
        }
        case OpType::MaximumScalar: return std::max(x, s0);
        case OpType::MinimumScalar: return std::min(x, s0);
        default: return x;
    }
}

bool isElementwiseOp(OpType t) {
    switch (t) {
        case OpType::Neg:
        case OpType::Abs:
        case OpType::Sqrt:
        case OpType::Exp:
        case OpType::Log:
        case OpType::Sin:
        case OpType::Cos:
        case OpType::Tanh:
        case OpType::Relu:
        case OpType::Sigmoid:
        case OpType::Gelu:
        case OpType::Silu:
        case OpType::AddScalar:
        case OpType::SubScalar:
        case OpType::MulScalar:
        case OpType::DivScalar:
        case OpType::PowScalar:
        case OpType::Clamp:
        case OpType::MaximumScalar:
        case OpType::MinimumScalar:
        case OpType::Add:
        case OpType::Sub:
        case OpType::Multiply:
        case OpType::Divide:
        case OpType::Where:
            return true;
        default:
            return false;
    }
}

bool broadcastShape(const int *aDims, int aRank, const int *bDims, int bRank, int *outDims,
                    int &outRank) {
    const int r = std::max(aRank, bRank);
    if (r > Tensor::kMaxRank) return false;
    outRank = r;
    const int aPad = r - aRank;
    const int bPad = r - bRank;
    for (int k = 0; k < r; ++k) {
        const int da = k < aPad ? 1 : aDims[k - aPad];
        const int db = k < bPad ? 1 : bDims[k - bPad];
        if (da != db && da != 1 && db != 1) return false;
        outDims[k] = std::max(da, db);
    }
    return true;
}

void binaryOp(OpType type, const float *a, const int *aDims, int aRank, const float *b,
              const int *bDims, int bRank, float *out, const int *outDims, int outRank) {
    int aStrides[Tensor::kMaxRank] = {};
    int bStrides[Tensor::kMaxRank] = {};
    broadcastStrides(aDims, aRank, outRank, aStrides);
    broadcastStrides(bDims, bRank, outRank, bStrides);

    int S[Tensor::kMaxRank] = {};
    S[outRank - 1] = 1;
    for (int k = outRank - 2; k >= 0; --k) S[k] = S[k + 1] * outDims[k + 1];

    const int count = product(outDims, outRank);
    for (int o = 0; o < count; ++o) {
        int ia = 0, ib = 0;
        for (int k = 0; k < outRank; ++k) {
            const int coord = (o / S[k]) % outDims[k];
            ia += coord * aStrides[k];
            ib += coord * bStrides[k];
        }
        out[o] = applyBinary(type, a[ia], b[ib]);
    }
}

void unaryOp(OpType type, const float *in, int count, float *out, float s0, float s1) {
    for (int i = 0; i < count; ++i) out[i] = applyUnary(type, in[i], s0, s1);
}

void softmax(const float *in, const int *dims, int rank, int axis, bool logMode, float *out) {
    axis = normalizeAxis(axis, rank);
    int outer = 1, reduce = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= dims[k];
    reduce = dims[axis];
    for (int k = axis + 1; k < rank; ++k) inner *= dims[k];
    const int rows = outer * inner;
    for (int r = 0; r < rows; ++r) {
        const int o = r / inner;
        const int i = r % inner;
        const float *row = in + (o * reduce) * inner + i;
        float mx = -std::numeric_limits<float>::infinity();
        for (int j = 0; j < reduce; ++j) mx = std::max(mx, row[j * inner]);
        double sum = 0.0;
        for (int j = 0; j < reduce; ++j) sum += std::exp(double(row[j * inner] - mx));
        const float invSum = float(1.0 / sum);
        for (int j = 0; j < reduce; ++j) {
            const float v = row[j * inner];
            out[(o * reduce + j) * inner + i] =
                logMode ? (v - mx) - std::log(float(sum)) : std::exp(v - mx) * invSum;
        }
    }
}

void layernorm(const float *in, int rows, int cols, const float *scale, const float *bias,
               float eps, float *out) {
    for (int r = 0; r < rows; ++r) {
        const float *row = in + size_t(r) * cols;
        double sum = 0.0, sumsq = 0.0;
        for (int c = 0; c < cols; ++c) {
            const double v = row[c];
            sum += v;
            sumsq += v * v;
        }
        const double mean = sum / cols;
        double var = sumsq / cols - mean * mean;
        if (var < 0.0) var = 0.0;
        const float inv = float(1.0 / std::sqrt(var + eps));
        float *o = out + size_t(r) * cols;
        for (int c = 0; c < cols; ++c) {
            float y = float(row[c] - mean) * inv;
            if (scale) y *= scale[c];
            if (bias) y += bias[c];
            o[c] = y;
        }
    }
}

void rmsnorm(const float *in, int rows, int cols, const float *scale, float eps, float *out) {
    for (int r = 0; r < rows; ++r) {
        const float *row = in + size_t(r) * cols;
        double sumsq = 0.0;
        for (int c = 0; c < cols; ++c) sumsq += double(row[c]) * row[c];
        const float inv = float(1.0 / std::sqrt(sumsq / cols + eps));
        float *o = out + size_t(r) * cols;
        for (int c = 0; c < cols; ++c) o[c] = row[c] * inv * (scale ? scale[c] : 1.f);
    }
}

namespace {

int convOutSize(int inSize, int kernel, int stride, int pad) {
    return (inSize + 2 * pad - kernel) / stride + 1;
}

}  // namespace

void conv1d(const float *x, const int *xDims, const float *w, const int *wDims,
            const float *bias, int stride, int pad, float *out) {
    const int N = xDims[0], C = xDims[1], L = xDims[2];
    const int F = wDims[0], K = wDims[2];
    const int OL = convOutSize(L, K, stride, pad);
    for (int n = 0; n < N; ++n) {
        for (int f = 0; f < F; ++f) {
            for (int ol = 0; ol < OL; ++ol) {
                float acc = bias ? bias[f] : 0.f;
                for (int c = 0; c < C; ++c) {
                    for (int k = 0; k < K; ++k) {
                        const int il = ol * stride + k - pad;
                        if (il < 0 || il >= L) continue;
                        acc += x[((n * C + c) * L + il)] * w[((f * C + c) * K + k)];
                    }
                }
                out[(n * F + f) * OL + ol] = acc;
            }
        }
    }
}

void conv2d(const float *x, const int *xDims, const float *w, const int *wDims,
            const float *bias, int stride, int pad, float *out) {
    const int N = xDims[0], C = xDims[1], H = xDims[2], W = xDims[3];
    const int F = wDims[0], KH = wDims[2], KW = wDims[3];
    const int OH = convOutSize(H, KH, stride, pad);
    const int OW = convOutSize(W, KW, stride, pad);
    for (int n = 0; n < N; ++n) {
        for (int f = 0; f < F; ++f) {
            for (int oh = 0; oh < OH; ++oh) {
                for (int ow = 0; ow < OW; ++ow) {
                    float acc = bias ? bias[f] : 0.f;
                    for (int c = 0; c < C; ++c) {
                        for (int kh = 0; kh < KH; ++kh) {
                            const int ih = oh * stride + kh - pad;
                            if (ih < 0 || ih >= H) continue;
                            for (int kw = 0; kw < KW; ++kw) {
                                const int iw = ow * stride + kw - pad;
                                if (iw < 0 || iw >= W) continue;
                                acc += x[((n * C + c) * H + ih) * W + iw] *
                                       w[((f * C + c) * KH + kh) * KW + kw];
                            }
                        }
                    }
                    out[((n * F + f) * OH + oh) * OW + ow] = acc;
                }
            }
        }
    }
}

void maxpool2d(const float *in, const int *dims, int ksize, int stride, int pad, float *out) {
    const int N = dims[0], C = dims[1], H = dims[2], W = dims[3];
    const int OH = convOutSize(H, ksize, stride, pad);
    const int OW = convOutSize(W, ksize, stride, pad);
    for (int n = 0; n < N; ++n)
        for (int c = 0; c < C; ++c)
            for (int oh = 0; oh < OH; ++oh)
                for (int ow = 0; ow < OW; ++ow) {
                    float best = -std::numeric_limits<float>::infinity();
                    for (int kh = 0; kh < ksize; ++kh)
                        for (int kw = 0; kw < ksize; ++kw) {
                            const int ih = oh * stride + kh - pad;
                            const int iw = ow * stride + kw - pad;
                            if (ih < 0 || ih >= H || iw < 0 || iw >= W) continue;
                            best = std::max(best, in[((n * C + c) * H + ih) * W + iw]);
                        }
                    out[((n * C + c) * OH + oh) * OW + ow] = best;
                }
}

void avgpool2d(const float *in, const int *dims, int ksize, int stride, int pad, float *out) {
    const int N = dims[0], C = dims[1], H = dims[2], W = dims[3];
    const int OH = convOutSize(H, ksize, stride, pad);
    const int OW = convOutSize(W, ksize, stride, pad);
    for (int n = 0; n < N; ++n)
        for (int c = 0; c < C; ++c)
            for (int oh = 0; oh < OH; ++oh)
                for (int ow = 0; ow < OW; ++ow) {
                    double acc = 0.0;
                    int valid = 0;
                    for (int kh = 0; kh < ksize; ++kh)
                        for (int kw = 0; kw < ksize; ++kw) {
                            const int ih = oh * stride + kh - pad;
                            const int iw = ow * stride + kw - pad;
                            if (ih < 0 || ih >= H || iw < 0 || iw >= W) continue;
                            acc += in[((n * C + c) * H + ih) * W + iw];
                            ++valid;
                        }
                    out[((n * C + c) * OH + oh) * OW + ow] =
                        valid > 0 ? float(acc / valid) : 0.f;
                }
}

void embedding(const float *table, int vocab, int dim, const float *indices, int count,
               float *out) {
    for (int r = 0; r < count; ++r) {
        int idx = int(indices[r]);
        idx = std::max(0, std::min(idx, vocab - 1));
        const float *row = table + size_t(idx) * dim;
        float *o = out + size_t(r) * dim;
        for (int d = 0; d < dim; ++d) o[d] = row[d];
    }
}

void concat(const float *const *ins, const int *const *inDims, const int *inRanks, int n,
            int axis, float *out, const int *outDims, int outRank) {
    axis = normalizeAxis(axis, outRank);
    int starts[4] = {};
    int axisSize = 0;
    for (int k = 0; k < n; ++k) {
        starts[k] = axisSize;
        axisSize += inDims[k][axis];
    }
    int outer = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= outDims[k];
    for (int k = axis + 1; k < outRank; ++k) inner *= outDims[k];
    const int count = outer * axisSize * inner;
    for (int o = 0; o < count; ++o) {
        const int ax = (o / inner) % axisSize;
        const int outerPart = o / (axisSize * inner);
        const int innerPart = o % inner;
        float v = 0.f;
        for (int k = 0; k < n; ++k) {
            const int sz = inDims[k][axis];
            if (ax >= starts[k] && ax < starts[k] + sz) {
                v = ins[k][(outerPart * sz + (ax - starts[k])) * inner + innerPart];
                break;
            }
        }
        out[o] = v;
    }
}

void sliceOp(const float *in, const int *inDims, int inRank, int axis, int begin, int end,
             float *out, const int *outDims, int outRank) {
    (void)inRank;
    axis = normalizeAxis(axis, outRank);
    const int axisSize = end - begin;
    int outer = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= outDims[k];
    for (int k = axis + 1; k < outRank; ++k) inner *= outDims[k];
    const int count = outer * axisSize * inner;
    for (int o = 0; o < count; ++o) {
        const int ax = (o / inner) % axisSize;
        const int outerPart = o / (axisSize * inner);
        const int innerPart = o % inner;
        out[o] = in[(outerPart * inDims[axis] + (ax + begin)) * inner + innerPart];
    }
}

void permute(const float *in, const int *inDims, int rank, const int *order, float *out,
             const int *outDims) {
    int S[Tensor::kMaxRank] = {};
    S[rank - 1] = 1;
    for (int k = rank - 2; k >= 0; --k) S[k] = S[k + 1] * outDims[k + 1];
    int inStride[Tensor::kMaxRank] = {};
    inStride[rank - 1] = 1;
    for (int k = rank - 2; k >= 0; --k) inStride[k] = inStride[k + 1] * inDims[k + 1];
    const int count = product(outDims, rank);
    for (int o = 0; o < count; ++o) {
        int idx = 0;
        for (int k = 0; k < rank; ++k) {
            const int coord = (o / S[k]) % outDims[k];
            idx += coord * inStride[order[k]];
        }
        out[o] = in[idx];
    }
}

void reduceAxis(OpType type, const float *in, const int *dims, int rank, int axis, float *out,
                const int *outDims, int outRank) {
    (void)outDims;
    (void)outRank;
    axis = normalizeAxis(axis, rank);
    int outer = 1, reduce = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= dims[k];
    reduce = dims[axis];
    for (int k = axis + 1; k < rank; ++k) inner *= dims[k];
    const int rows = outer * inner;
    const float negInf = -std::numeric_limits<float>::infinity();
    const float posInf = std::numeric_limits<float>::infinity();
    for (int r = 0; r < rows; ++r) {
        const int o = r / inner;
        const int i = r % inner;
        float acc = (type == OpType::ReduceMin) ? posInf : (type == OpType::ReduceMax ? negInf : 0.f);
        for (int j = 0; j < reduce; ++j) {
            const float v = in[(o * reduce + j) * inner + i];
            if (type == OpType::ReduceSum || type == OpType::ReduceMean) acc += v;
            else if (type == OpType::ReduceMin) acc = std::min(acc, v);
            else if (type == OpType::ReduceMax) acc = std::max(acc, v);
        }
        if (type == OpType::ReduceMean) acc /= float(reduce);
        out[o * inner + i] = acc;
    }
}

void argmax(const float *in, const int *dims, int rank, int axis, float *out,
            const int *outDims, int outRank) {
    (void)outDims;
    (void)outRank;
    axis = normalizeAxis(axis, rank);
    int outer = 1, reduce = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= dims[k];
    reduce = dims[axis];
    for (int k = axis + 1; k < rank; ++k) inner *= dims[k];
    const int rows = outer * inner;
    for (int r = 0; r < rows; ++r) {
        const int o = r / inner;
        const int i = r % inner;
        float best = -std::numeric_limits<float>::infinity();
        int bestJ = 0;
        for (int j = 0; j < reduce; ++j) {
            const float v = in[(o * reduce + j) * inner + i];
            if (v > best) {
                best = v;
                bestJ = j;
            }
        }
        out[o * inner + i] = float(bestJ);
    }
}

void sdpa(const float *q, const float *k, const float *v, const float *mask, int B, int H,
          int T, int S, int D, float scale, float *out) {
    for (int b = 0; b < B; ++b) {
        for (int h = 0; h < H; ++h) {
            for (int t = 0; t < T; ++t) {
                std::vector<float> scores(static_cast<size_t>(S));
                const float *qRow = q + (size_t(b) * H + h) * T * D + size_t(t) * D;
                for (int s = 0; s < S; ++s) {
                    const float *kRow = k + (size_t(b) * H + h) * S * D + size_t(s) * D;
                    double acc = 0.0;
                    for (int d = 0; d < D; ++d) acc += double(qRow[d]) * kRow[d];
                    float score = float(acc) * scale;
                    if (mask) score += mask[(size_t(b) * H + h) * T * S + size_t(t) * S + s];
                    scores[static_cast<size_t>(s)] = score;
                }
                float mx = -std::numeric_limits<float>::infinity();
                for (int s = 0; s < S; ++s) mx = std::max(mx, scores[static_cast<size_t>(s)]);
                double sum = 0.0;
                for (int s = 0; s < S; ++s)
                    sum += std::exp(double(scores[static_cast<size_t>(s)] - mx));
                float *oRow = out + (size_t(b) * H + h) * T * D + size_t(t) * D;
                for (int d = 0; d < D; ++d) {
                    double acc = 0.0;
                    for (int s = 0; s < S; ++s) {
                        const float *vRow = v + (size_t(b) * H + h) * S * D + size_t(s) * D;
                        acc += std::exp(double(scores[static_cast<size_t>(s)] - mx)) * vRow[d];
                    }
                    oRow[d] = float(acc / sum);
                }
            }
        }
    }
}

void resize2d(const float *in, const int *inDims, int outW, int outH, int mode, float *out) {
    const int N = inDims[0], C = inDims[1], H = inDims[2], W = inDims[3];
    for (int n = 0; n < N; ++n) {
        for (int c = 0; c < C; ++c) {
            for (int oh = 0; oh < outH; ++oh) {
                for (int ow = 0; ow < outW; ++ow) {
                    const float *src = in + (size_t(n) * C + c) * H * W;
                    float value = 0.f;
                    if (mode == 0) {
                        const int ih = std::min(int(float(oh) * H / outH), H - 1);
                        const int iw = std::min(int(float(ow) * W / outW), W - 1);
                        value = src[size_t(ih) * W + iw];
                    } else {
                        float x = float(ow + 0.5f) * W / outW - 0.5f;
                        float y = float(oh + 0.5f) * H / outH - 0.5f;
                        x = std::clamp(x, 0.f, float(W - 1));
                        y = std::clamp(y, 0.f, float(H - 1));
                        const int x0 = int(std::floor(x));
                        const int y0 = int(std::floor(y));
                        const int x1 = std::min(x0 + 1, W - 1);
                        const int y1 = std::min(y0 + 1, H - 1);
                        const float fx = x - float(x0);
                        const float fy = y - float(y0);
                        const float v00 = src[size_t(y0) * W + x0];
                        const float v10 = src[size_t(y0) * W + x1];
                        const float v01 = src[size_t(y1) * W + x0];
                        const float v11 = src[size_t(y1) * W + x1];
                        value = (v00 * (1.f - fx) + v10 * fx) * (1.f - fy) +
                                (v01 * (1.f - fx) + v11 * fx) * fy;
                    }
                    out[((size_t(n) * C + c) * outH + oh) * outW + ow] = value;
                }
            }
        }
    }
}

}  // namespace eve::tensor::kernels
