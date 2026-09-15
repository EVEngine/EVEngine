#include "animation/tensor/SmrMeshRetNet.h"

#include "common/Diagnostic.h"
#include "tensor/Tensor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>

namespace eve::animation {
namespace {

void gemm(const float* a, const float* b, float* c, int m, int k, int n) {
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) {
            float sum = 0.f;
            for (int t = 0; t < k; ++t) sum += a[i * k + t] * b[t * n + j];
            c[i * n + j] = sum;
        }
}

void addBias(float* x, const float* bias, int rows, int cols) {
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) x[r * cols + c] += bias[c];
}

void gelu(float* x, int n) {
    for (int i = 0; i < n; ++i) {
        const float v = x[i];
        x[i]          = 0.5f * v * (1.f + std::tanh(0.7978845608f * (v + 0.044715f * v * v * v)));
    }
}

void layernorm(float* x, int rows, int cols, const float* g, const float* b) {
    for (int r = 0; r < rows; ++r) {
        float* row  = x + r * cols;
        float  mean = 0.f;
        for (int c = 0; c < cols; ++c) mean += row[c];
        mean /= static_cast<float>(cols);
        float var = 0.f;
        for (int c = 0; c < cols; ++c) {
            const float d = row[c] - mean;
            var += d * d;
        }
        var = 1.f / std::sqrt(var / static_cast<float>(cols) + 1e-5f);
        for (int c = 0; c < cols; ++c) row[c] = (row[c] - mean) * var * g[c] + b[c];
    }
}

void softmaxRows(float* x, int rows, int cols) {
    for (int r = 0; r < rows; ++r) {
        float* row = x + r * cols;
        float  mx  = row[0];
        for (int c = 1; c < cols; ++c) mx = std::max(mx, row[c]);
        float sum = 0.f;
        for (int c = 0; c < cols; ++c) {
            row[c] = std::exp(row[c] - mx);
            sum += row[c];
        }
        const float inv = 1.f / std::max(sum, 1e-8f);
        for (int c = 0; c < cols; ++c) row[c] *= inv;
    }
}

std::vector<float> xavier(std::mt19937& rng, int rows, int cols) {
    std::normal_distribution<float> dist(0.f, std::sqrt(2.f / static_cast<float>(rows + cols)));
    std::vector<float>              out(static_cast<size_t>(rows * cols));
    for (float& v : out) v = dist(rng);
    return out;
}

std::vector<float> zeros(int n) { return std::vector<float>(static_cast<size_t>(n), 0.f); }
std::vector<float> ones(int n) { return std::vector<float>(static_cast<size_t>(n), 1.f); }

std::vector<float> mlp2(const float* x, int rows, int inDim, int mid, int outDim, const std::vector<float>& w1,
                        const std::vector<float>& b1, const std::vector<float>& w2, const std::vector<float>& b2) {
    std::vector<float> h(static_cast<size_t>(rows * mid));
    gemm(x, w1.data(), h.data(), rows, inDim, mid);
    addBias(h.data(), b1.data(), rows, mid);
    gelu(h.data(), rows * mid);
    std::vector<float> y(static_cast<size_t>(rows * outDim));
    gemm(h.data(), w2.data(), y.data(), rows, mid, outDim);
    addBias(y.data(), b2.data(), rows, outDim);
    return y;
}

std::vector<float> maxPool(const std::vector<float>& x, int groups, int count, int dim) {
    std::vector<float> out(static_cast<size_t>(groups * dim), -1e30f);
    for (int g = 0; g < groups; ++g)
        for (int i = 0; i < count; ++i)
            for (int d = 0; d < dim; ++d) {
                const float v    = x[static_cast<size_t>((g * count + i) * dim + d)];
                float&      slot = out[static_cast<size_t>(g * dim + d)];
                slot             = std::max(slot, v);
            }
    for (float& v : out)
        if (v < -1e29f) v = 0.f;
    return out;
}

void transformerLayer(std::vector<float>& x, int tokens, int dim, int heads, const std::vector<float>& wq,
                      const std::vector<float>& wk, const std::vector<float>& wv, const std::vector<float>& wo,
                      const std::vector<float>& w1, const std::vector<float>& b1, const std::vector<float>& w2,
                      const std::vector<float>& b2, const std::vector<float>& ln1g, const std::vector<float>& ln1b,
                      const std::vector<float>& ln2g, const std::vector<float>& ln2b, const std::vector<float>* memory,
                      int memoryTokens) {
    const int          headDim   = std::max(1, dim / std::max(heads, 1));
    const int          srcTokens = memory ? memoryTokens : tokens;
    std::vector<float> residual  = x;
    layernorm(x.data(), tokens, dim, ln1g.data(), ln1b.data());
    std::vector<float> q(static_cast<size_t>(tokens * dim));
    std::vector<float> k(static_cast<size_t>(srcTokens * dim));
    std::vector<float> v(static_cast<size_t>(srcTokens * dim));
    gemm(x.data(), wq.data(), q.data(), tokens, dim, dim);
    const float* src = memory ? memory->data() : x.data();
    gemm(src, wk.data(), k.data(), srcTokens, dim, dim);
    gemm(src, wv.data(), v.data(), srcTokens, dim, dim);
    std::vector<float> ctx(static_cast<size_t>(tokens * dim), 0.f);
    const float        scale = 1.f / std::sqrt(static_cast<float>(headDim));
    for (int h = 0; h < heads; ++h) {
        std::vector<float> scores(static_cast<size_t>(tokens * srcTokens));
        for (int t = 0; t < tokens; ++t)
            for (int s = 0; s < srcTokens; ++s) {
                float sum = 0.f;
                for (int d = 0; d < headDim; ++d)
                    sum += q[static_cast<size_t>(t * dim + h * headDim + d)] *
                           k[static_cast<size_t>(s * dim + h * headDim + d)];
                scores[static_cast<size_t>(t * srcTokens + s)] = sum * scale;
            }
        softmaxRows(scores.data(), tokens, srcTokens);
        for (int t = 0; t < tokens; ++t)
            for (int d = 0; d < headDim; ++d) {
                float sum = 0.f;
                for (int s = 0; s < srcTokens; ++s)
                    sum += scores[static_cast<size_t>(t * srcTokens + s)] *
                           v[static_cast<size_t>(s * dim + h * headDim + d)];
                ctx[static_cast<size_t>(t * dim + h * headDim + d)] = sum;
            }
    }
    std::vector<float> projected(static_cast<size_t>(tokens * dim));
    gemm(ctx.data(), wo.data(), projected.data(), tokens, dim, dim);
    for (size_t i = 0; i < x.size(); ++i) x[i] = residual[i] + projected[i];
    residual = x;
    layernorm(x.data(), tokens, dim, ln2g.data(), ln2b.data());
    auto ff = mlp2(x.data(), tokens, dim, static_cast<int>(b1.size()), dim, w1, b1, w2, b2);
    for (size_t i = 0; i < x.size(); ++i) x[i] = residual[i] + ff[i];
}

}  // namespace

SmrMeshRetNet::SmrMeshRetNet(SmrMeshRetConfig config) : config_(config) { initWeights(); }

void SmrMeshRetNet::initWeights() {
    std::mt19937 rng(static_cast<uint32_t>(config_.seed));
    const int    d = config_.latentDim;
    geomW1_        = xavier(rng, 7, d);
    geomB1_        = zeros(d);
    geomW2_        = xavier(rng, d, d);
    geomB2_        = zeros(d);
    dmiW1_         = xavier(rng, 10, d);
    dmiB1_         = zeros(d);
    dmiW2_         = xavier(rng, d, d);
    dmiB2_         = zeros(d);
    motionW_       = xavier(rng, 6, d);
    motionB_       = zeros(d);
    fuseW_         = xavier(rng, d * 3, d);
    fuseB_         = zeros(d);
    outW_          = xavier(rng, d, 6);
    outB_          = zeros(6);
    auto fill      = [&](std::vector<LayerWeights>& layers) {
        layers.assign(static_cast<size_t>(config_.numLayers), {});
        for (auto& layer : layers) {
            layer.wq   = xavier(rng, d, d);
            layer.wk   = xavier(rng, d, d);
            layer.wv   = xavier(rng, d, d);
            layer.wo   = xavier(rng, d, d);
            layer.w1   = xavier(rng, d, config_.ffSize);
            layer.b1   = zeros(config_.ffSize);
            layer.w2   = xavier(rng, config_.ffSize, d);
            layer.b2   = zeros(d);
            layer.ln1g = ones(d);
            layer.ln1b = zeros(d);
            layer.ln2g = ones(d);
            layer.ln2b = zeros(d);
        }
    };
    fill(enc_);
    fill(dec_);
    tensor::Tensor probe(d);
    probe.fill(0.f);
    (void)probe;
}

Result<std::vector<float>> SmrMeshRetNet::forward(const SmrFeatureBatch& features) const {
    if (features.frames <= 0 || features.joints <= 0) {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrMeshRetNet.forward: empty features"));
    }
    const int d     = config_.latentDim;
    const int T     = features.frames;
    const int J     = features.joints;
    const int S     = std::max(features.sensors, 1);
    const int P     = std::max(features.pairs, 1);
    const int heads = std::max(1, config_.numHeads);

    auto srcGeom = mlp2(features.sourceGeom.data(), S, 7, d, d, geomW1_, geomB1_, geomW2_, geomB2_);
    auto tgtGeom = mlp2(features.targetGeom.data(), S, 7, d, d, geomW1_, geomB1_, geomW2_, geomB2_);
    auto srcPool = maxPool(srcGeom, 1, S, d);
    auto tgtPool = maxPool(tgtGeom, 1, S, d);

    std::vector<float> temporal(static_cast<size_t>(T * d), 0.f);
    for (int t = 0; t < T; ++t) {
        const float*       dmiFrame = features.sourceDmi.data() + static_cast<size_t>(t) * static_cast<size_t>(P) * 10u;
        auto               dmiEnc   = mlp2(dmiFrame, P, 10, d, d, dmiW1_, dmiB1_, dmiW2_, dmiB2_);
        auto               dmiPool  = maxPool(dmiEnc, 1, P, d);
        std::vector<float> meanRot(6, 0.f);
        for (int j = 0; j < J; ++j) {
            const float* r = features.sourceRot6d.data() + static_cast<size_t>((t * J + j) * 6);
            for (int k = 0; k < 6; ++k) meanRot[static_cast<size_t>(k)] += r[k];
        }
        for (float& v : meanRot) v /= static_cast<float>(std::max(J, 1));
        std::vector<float> motion(static_cast<size_t>(d), 0.f);
        gemm(meanRot.data(), motionW_.data(), motion.data(), 1, 6, d);
        addBias(motion.data(), motionB_.data(), 1, d);
        std::vector<float> fusedIn(static_cast<size_t>(d * 3));
        for (int i = 0; i < d; ++i) {
            fusedIn[static_cast<size_t>(i)]         = dmiPool[static_cast<size_t>(i)];
            fusedIn[static_cast<size_t>(d + i)]     = motion[static_cast<size_t>(i)];
            fusedIn[static_cast<size_t>(2 * d + i)] = srcPool[static_cast<size_t>(i)];
        }
        std::vector<float> fused(static_cast<size_t>(d));
        gemm(fusedIn.data(), fuseW_.data(), fused.data(), 1, d * 3, d);
        addBias(fused.data(), fuseB_.data(), 1, d);
        for (int i = 0; i < d; ++i) temporal[static_cast<size_t>(t * d + i)] = fused[static_cast<size_t>(i)];
    }

    std::vector<float> memory = temporal;
    for (const auto& layer : enc_)
        transformerLayer(memory, T, d, heads, layer.wq, layer.wk, layer.wv, layer.wo, layer.w1, layer.b1, layer.w2,
                         layer.b2, layer.ln1g, layer.ln1b, layer.ln2g, layer.ln2b, nullptr, 0);

    std::vector<float> queries(static_cast<size_t>(T * d));
    for (int t = 0; t < T; ++t)
        for (int i = 0; i < d; ++i)
            queries[static_cast<size_t>(t * d + i)] =
                tgtPool[static_cast<size_t>(i)] + temporal[static_cast<size_t>(t * d + i)];
    for (const auto& layer : dec_)
        transformerLayer(queries, T, d, heads, layer.wq, layer.wk, layer.wv, layer.wo, layer.w1, layer.b1, layer.w2,
                         layer.b2, layer.ln1g, layer.ln1b, layer.ln2g, layer.ln2b, &memory, T);

    std::vector<float> out(static_cast<size_t>(T * J * 6));
    for (int t = 0; t < T; ++t) {
        std::vector<float> delta(6, 0.f);
        gemm(queries.data() + static_cast<size_t>(t * d), outW_.data(), delta.data(), 1, d, 6);
        addBias(delta.data(), outB_.data(), 1, 6);
        for (int j = 0; j < J; ++j) {
            const float* src = features.sourceRot6d.data() + static_cast<size_t>((t * J + j) * 6);
            float*       dst = out.data() + static_cast<size_t>((t * J + j) * 6);
            for (int k = 0; k < 6; ++k) dst[k] = src[k] + 0.05f * delta[static_cast<size_t>(k)];
        }
    }
    return Result<std::vector<float>>::success(std::move(out));
}

}  // namespace eve::animation
