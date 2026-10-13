#include "animation/tensor/SmrFeatures.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/tensor/SmrMeshRetNet.h"
#include "animation/tensor/SmrNeuralProvider.h"
#include "animation/tensor/SmrOnnxRunner.h"
#include "common/Diagnostic.h"
#include "common/Exception.h"
#include "tensor/Tensor.h"

// SmrFeatures.cpp
namespace eve::animation {
namespace {

bool partsInteract(AnimSmrBodyPart a, AnimSmrBodyPart b) {
    if (a > b) std::swap(a, b);
    if (a == AnimSmrBodyPart::Arm &&
        (b == AnimSmrBodyPart::Torso || b == AnimSmrBodyPart::Arm || b == AnimSmrBodyPart::Head))
        return true;
    if (a == AnimSmrBodyPart::Leg && b == AnimSmrBodyPart::Leg) return true;
    if (a == AnimSmrBodyPart::Torso && b == AnimSmrBodyPart::Head) return true;
    return false;
}

std::string normalizeToken(const std::string& name) {
    const size_t separator = name.find_last_of(":|/");
    const size_t begin     = separator == std::string::npos ? 0 : separator + 1;
    std::string  out;
    out.reserve(name.size() - begin);
    for (size_t i = begin; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

int matchJoint(const AnimSkeleton* source, const AnimSkeleton* target, int targetBone) {
    const int exact = source->findBone(target->getBoneName(targetBone));
    if (exact >= 0) return exact;
    const std::string want = normalizeToken(target->getBoneName(targetBone));
    for (int bone = 0; bone < source->getBoneCount(); ++bone) {
        if (normalizeToken(source->getBoneName(bone)) == want) return bone;
    }
    return -1;
}

}  // namespace

void quatToRot6d(float qx, float qy, float qz, float qw, float out[6]) {
    const float x = qx, y = qy, z = qz, w = qw;
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float xx = x * x2, xy = x * y2, xz = x * z2;
    const float yy = y * y2, yz = y * z2, zz = z * z2;
    const float wx = w * x2, wy = w * y2, wz = w * z2;
    out[0] = 1.f - (yy + zz);
    out[1] = xy + wz;
    out[2] = xz - wy;
    out[3] = xy - wz;
    out[4] = 1.f - (xx + zz);
    out[5] = yz + wx;
}

void rot6dToQuat(const float in[6], float& qx, float& qy, float& qz, float& qw) {
    float c0[3]     = {in[0], in[1], in[2]};
    float c1[3]     = {in[3], in[4], in[5]};
    auto  normalize = [](float* v) {
        const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n < 1e-8f) {
            v[0] = 1.f;
            v[1] = 0.f;
            v[2] = 0.f;
            return;
        }
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
    };
    normalize(c0);
    const float dot = c0[0] * c1[0] + c0[1] * c1[1] + c0[2] * c1[2];
    c1[0] -= dot * c0[0];
    c1[1] -= dot * c0[1];
    c1[2] -= dot * c0[2];
    normalize(c1);
    const float c2[3] = {c0[1] * c1[2] - c0[2] * c1[1], c0[2] * c1[0] - c0[0] * c1[2], c0[0] * c1[1] - c0[1] * c1[0]};
    const float m00 = c0[0], m01 = c1[0], m02 = c2[0];
    const float m10 = c0[1], m11 = c1[1], m12 = c2[1];
    const float m20 = c0[2], m21 = c1[2], m22 = c2[2];
    const float trace = m00 + m11 + m22;
    if (trace > 0.f) {
        const float s = 0.5f / std::sqrt(trace + 1.f);
        qw            = 0.25f / s;
        qx            = (m21 - m12) * s;
        qy            = (m02 - m20) * s;
        qz            = (m10 - m01) * s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = 2.f * std::sqrt(1.f + m00 - m11 - m22);
        qw            = (m21 - m12) / s;
        qx            = 0.25f * s;
        qy            = (m01 + m10) / s;
        qz            = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = 2.f * std::sqrt(1.f + m11 - m00 - m22);
        qw            = (m02 - m20) / s;
        qx            = (m01 + m10) / s;
        qy            = 0.25f * s;
        qz            = (m12 + m21) / s;
    } else {
        const float s = 2.f * std::sqrt(1.f + m22 - m00 - m11);
        qw            = (m10 - m01) / s;
        qx            = (m02 + m20) / s;
        qy            = (m12 + m21) / s;
        qz            = 0.25f * s;
    }
    const float n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (n > 1e-8f) {
        qx /= n;
        qy /= n;
        qz /= n;
        qw /= n;
    } else {
        qx = qy = qz = 0.f;
        qw           = 1.f;
    }
}

SmrFeatureBatch buildSmrFeatures(const AnimClip& sourceClip, const AnimSkeleton* sourceSkeleton,
                                 const AnimSkeleton* targetSkeleton, const AnimSkin* /*sourceSkin*/,
                                 const AnimSkin* /*targetSkin*/, int ringsPerBone, int pointsPerRing) {
    if (!sourceSkeleton || !targetSkeleton) throw Exception("buildSmrFeatures: skeleton is null");

    SmrFeatureBatch batch;
    batch.joints = targetSkeleton->getBoneCount();
    batch.jointMap.assign(static_cast<size_t>(batch.joints), -1);
    for (int t = 0; t < batch.joints; ++t)
        batch.jointMap[static_cast<size_t>(t)] = matchJoint(sourceSkeleton, targetSkeleton, t);

    const AnimSmrSensorCloud sourceCloud =
        AnimSmrSensorCloud::fromSkeletonDense(sourceSkeleton, ringsPerBone, pointsPerRing);
    const AnimSmrSensorCloud targetCloud =
        AnimSmrSensorCloud::fromSkeletonDense(targetSkeleton, ringsPerBone, pointsPerRing);
    batch.sensors = std::min(sourceCloud.getSensorCount(), targetCloud.getSensorCount());
    if (batch.sensors <= 0) {
        batch.sensors = 1;
        batch.sourceGeom.assign(7u, 0.f);
        batch.targetGeom.assign(7u, 0.f);
    } else {
        batch.sourceGeom.assign(static_cast<size_t>(batch.sensors) * 7u, 0.f);
        batch.targetGeom.assign(static_cast<size_t>(batch.sensors) * 7u, 0.f);
    }

    AnimPose sourceBind;
    AnimPose targetBind;
    sourceSkeleton->applyBindPose(&sourceBind);
    targetSkeleton->applyBindPose(&targetBind);
    sourceBind.computeWorld(sourceSkeleton);
    targetBind.computeWorld(targetSkeleton);

    if (sourceCloud.getSensorCount() > 0 && targetCloud.getSensorCount() > 0) {
        std::vector<float> sourceRest;
        std::vector<float> targetRest;
        sourceCloud.evaluateWorldPositions(&sourceBind, sourceRest);
        targetCloud.evaluateWorldPositions(&targetBind, targetRest);
        for (int i = 0; i < batch.sensors; ++i) {
            for (int k = 0; k < 3; ++k) {
                batch.sourceGeom[static_cast<size_t>(i) * 7u + static_cast<size_t>(k)] =
                    sourceRest[static_cast<size_t>(i) * 3u + static_cast<size_t>(k)];
                batch.targetGeom[static_cast<size_t>(i) * 7u + static_cast<size_t>(k)] =
                    targetRest[static_cast<size_t>(i) * 3u + static_cast<size_t>(k)];
            }
            batch.sourceGeom[static_cast<size_t>(i) * 7u + 3u] = static_cast<float>(sourceCloud.getSensorPart(i));
            batch.targetGeom[static_cast<size_t>(i) * 7u + 3u] = static_cast<float>(targetCloud.getSensorPart(i));
            batch.sourceGeom[static_cast<size_t>(i) * 7u + 4u] = static_cast<float>(sourceCloud.getSensor(i).boneIndex);
            batch.targetGeom[static_cast<size_t>(i) * 7u + 4u] = static_cast<float>(targetCloud.getSensor(i).boneIndex);
        }
    }

    std::vector<std::pair<int, int>> pairIdx;
    const int                        sensorLimit = std::min(batch.sensors, sourceCloud.getSensorCount());
    for (int a = 0; a < sensorLimit; ++a) {
        for (int b = a + 1; b < sensorLimit; ++b) {
            if (partsInteract(sourceCloud.getSensorPart(a), sourceCloud.getSensorPart(b))) pairIdx.emplace_back(a, b);
        }
    }
    batch.pairs = static_cast<int>(pairIdx.size());

    const float duration = sourceClip.getDuration();
    const float rate     = std::max(sourceClip.getSampleRate(), 1.f);
    batch.frames         = duration <= 0.f ? 1 : std::max(1, static_cast<int>(std::ceil(duration * rate)) + 1);
    batch.sourceRot6d.assign(static_cast<size_t>(batch.frames) * static_cast<size_t>(batch.joints) * 6u, 0.f);
    batch.sourceDmi.assign(static_cast<size_t>(batch.frames) * static_cast<size_t>(std::max(batch.pairs, 1)) * 10u,
                           0.f);

    AnimPose           pose;
    std::vector<float> xyz;
    std::vector<float> prevXyz;
    for (int frame = 0; frame < batch.frames; ++frame) {
        const float time =
            duration <= 0.f ? 0.f
                            : duration * static_cast<float>(frame) / static_cast<float>(std::max(batch.frames - 1, 1));
        sourceClip.sample(time, &pose, sourceSkeleton);
        pose.computeWorld(sourceSkeleton);
        for (int joint = 0; joint < batch.joints; ++joint) {
            const int sourceJoint = batch.jointMap[static_cast<size_t>(joint)];
            float     rot[6]      = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
            if (sourceJoint >= 0) {
                const TransformTRS& local = pose.local(sourceJoint);
                quatToRot6d(local.qx, local.qy, local.qz, local.qw, rot);
            }
            const size_t base =
                (static_cast<size_t>(frame) * static_cast<size_t>(batch.joints) + static_cast<size_t>(joint)) * 6u;
            for (int k = 0; k < 6; ++k) batch.sourceRot6d[base + static_cast<size_t>(k)] = rot[k];
        }
        if (sourceCloud.getSensorCount() > 0) sourceCloud.evaluateWorldPositions(&pose, xyz);
        for (int p = 0; p < batch.pairs; ++p) {
            const int   a  = pairIdx[static_cast<size_t>(p)].first;
            const int   b  = pairIdx[static_cast<size_t>(p)].second;
            const float ax = xyz[static_cast<size_t>(a) * 3u];
            const float ay = xyz[static_cast<size_t>(a) * 3u + 1u];
            const float az = xyz[static_cast<size_t>(a) * 3u + 2u];
            const float bx = xyz[static_cast<size_t>(b) * 3u];
            const float by = xyz[static_cast<size_t>(b) * 3u + 1u];
            const float bz = xyz[static_cast<size_t>(b) * 3u + 2u];
            const float dx = bx - ax, dy = by - ay, dz = bz - az;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            float       vx = 0.f, vy = 0.f, vz = 0.f;
            if (!prevXyz.empty()) {
                vx = (bx - prevXyz[static_cast<size_t>(b) * 3u]) - (ax - prevXyz[static_cast<size_t>(a) * 3u]);
                vy =
                    (by - prevXyz[static_cast<size_t>(b) * 3u + 1u]) - (ay - prevXyz[static_cast<size_t>(a) * 3u + 1u]);
                vz =
                    (bz - prevXyz[static_cast<size_t>(b) * 3u + 2u]) - (az - prevXyz[static_cast<size_t>(a) * 3u + 2u]);
            }
            const size_t base =
                (static_cast<size_t>(frame) * static_cast<size_t>(batch.pairs) + static_cast<size_t>(p)) * 10u;
            batch.sourceDmi[base + 0] = dx;
            batch.sourceDmi[base + 1] = dy;
            batch.sourceDmi[base + 2] = dz;
            batch.sourceDmi[base + 3] = dist;
            batch.sourceDmi[base + 4] = static_cast<float>(sourceCloud.getSensorPart(a));
            batch.sourceDmi[base + 5] = static_cast<float>(sourceCloud.getSensorPart(b));
            batch.sourceDmi[base + 6] = vx;
            batch.sourceDmi[base + 7] = vy;
            batch.sourceDmi[base + 8] = vz;
            batch.sourceDmi[base + 9] = 1.f;
        }
        prevXyz = xyz;
    }
    return batch;
}

}  // namespace eve::animation

// SmrMeshRetNet.cpp
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

// SmrNeuralProvider.cpp
namespace eve::animation {

std::string SmrNeuralProvider::name() const { return loadedPath_.empty() ? "tensor-meshret" : "onnx"; }

bool SmrNeuralProvider::isReady(const AnimRetargetProfile* profile) const {
    if (!profile) return true;
    if (!profile->getNeuralRetargetEnabled()) return false;
    const std::string& backend = profile->getNeuralBackend();
    if (backend == "onnx") return ensureOnnx(*profile).ok();
    if (backend == "tensor") return true;
    if (!profile->getNeuralModelPath().empty()) return ensureOnnx(*profile).ok();
    return true;
}

Result<void> SmrNeuralProvider::ensureOnnx(const AnimRetargetProfile& profile) const {
    const std::string& path = profile.getNeuralModelPath();
    if (path.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "SmrNeuralProvider: empty ONNX path"));
    }
    if (modelLoaded_ && loadedPath_ == path) return Result<void>::success();
    auto loaded = onnx_.loadFile(path);
    if (!loaded.ok()) return loaded;
    loadedPath_  = path;
    modelLoaded_ = true;
    return Result<void>::success();
}

Result<SmrNeuralResult> SmrNeuralProvider::applyRot6d(const SmrNeuralRequest& request, const SmrFeatureBatch& features,
                                                      const std::vector<float>& rot6d,
                                                      const std::string&        backend) const {
    if (!request.targetClip || !request.targetSkeleton) {
        return Result<SmrNeuralResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrNeuralProvider: null clip/skeleton"));
    }
    if (static_cast<int>(rot6d.size()) < features.frames * features.joints * 6) {
        return Result<SmrNeuralResult>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "SmrNeuralProvider: rot6d size mismatch"));
    }

    AnimClip&   clip     = *request.targetClip;
    const float duration = clip.getDuration();
    for (int joint = 0; joint < features.joints; ++joint) {
        clip.clearTrack(joint);
        for (int frame = 0; frame < features.frames; ++frame) {
            const float  time = duration <= 0.f ? 0.f
                                                : duration * static_cast<float>(frame) /
                                                     static_cast<float>(std::max(features.frames - 1, 1));
            const float* r6   = rot6d.data() + static_cast<size_t>((frame * features.joints + joint) * 6);
            float        qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
            rot6dToQuat(r6, qx, qy, qz, qw);
            clip.addRotationKey(joint, time, qx, qy, qz, qw);
        }
    }

    SmrNeuralResult result;
    result.framesWritten = features.frames;
    result.backend       = backend;
    result.sensorCount   = static_cast<size_t>(features.sensors);
    result.pairCount     = static_cast<size_t>(features.pairs);
    return Result<SmrNeuralResult>::success(std::move(result));
}

Result<SmrNeuralResult> SmrNeuralProvider::retarget(const SmrNeuralRequest& request) {
    if (!request.sourceClip || !request.targetClip || !request.sourceSkeleton || !request.targetSkeleton ||
        !request.profile) {
        return Result<SmrNeuralResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrNeuralProvider.retarget: null argument"));
    }

    SmrFeatureBatch features = buildSmrFeatures(*request.sourceClip, request.sourceSkeleton, request.targetSkeleton,
                                                request.sourceSkin, request.targetSkin, 2, 4);

    const std::string& backendHint = request.profile->getNeuralBackend();
    const bool         preferOnnx =
        backendHint == "onnx" || (backendHint != "tensor" && !request.profile->getNeuralModelPath().empty());
    if (preferOnnx) {
        auto ready = ensureOnnx(*request.profile);
        if (ready.ok()) {
            auto ran = onnx_.run(features);
            if (ran.ok()) return applyRot6d(request, features, ran.value(), "onnx");
            if (backendHint == "onnx") return Result<SmrNeuralResult>::failure(ran.status());
        } else if (backendHint == "onnx") {
            return Result<SmrNeuralResult>::failure(ready.status());
        }
    }

    auto ran = net_.forward(features);
    if (!ran.ok()) return Result<SmrNeuralResult>::failure(ran.status());
    return applyRot6d(request, features, ran.value(), "tensor-meshret");
}

}  // namespace eve::animation

// SmrOnnxRunner.cpp
namespace eve::animation {
namespace {

tensor::OnnxNamedTensor makeF32(const char* name, std::vector<int64_t> shape, const std::vector<float>& data) {
    tensor::OnnxNamedTensor named;
    named.name           = name;
    named.tensor.element = tensor::OnnxElement::Float32;
    named.tensor.shape   = std::move(shape);
    named.tensor.bytes.resize(data.size() * sizeof(float));
    if (!data.empty()) std::memcpy(named.tensor.bytes.data(), data.data(), named.tensor.bytes.size());
    return named;
}

}  // namespace

Result<void> SmrOnnxRunner::load(std::span<const uint8_t> bytes) {
    auto loaded = tensor::OnnxModel::load(bytes);
    if (!loaded.ok()) return Result<void>::failure(loaded.status());
    model_ = std::move(loaded.value());
    return Result<void>::success();
}

Result<void> SmrOnnxRunner::loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "SmrOnnxRunner: open failed", path));
    }
    const auto size = static_cast<std::streamoff>(file.tellg());
    if (size <= 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrOnnxRunner: empty file", path));
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "SmrOnnxRunner: read failed", path));
    }
    return load(bytes);
}

tensor::OnnxModelInfo SmrOnnxRunner::info() const {
    if (!model_) return {};
    return model_->info();
}

Result<std::vector<float>> SmrOnnxRunner::run(const SmrFeatureBatch& features) const {
    if (!model_) {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "SmrOnnxRunner: no model"));
    }
    if (features.frames <= 0 || features.joints <= 0) {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "SmrOnnxRunner: empty features"));
    }
    const int64_t T = features.frames;
    const int64_t J = features.joints;
    const int64_t S = std::max(features.sensors, 1);
    const int64_t P = std::max(features.pairs, 1);

    std::vector<tensor::OnnxNamedTensor> feeds;
    feeds.push_back(makeF32("source_rot6d", {1, T, J, 6}, features.sourceRot6d));
    feeds.push_back(makeF32("source_geom", {1, S, 7}, features.sourceGeom));
    feeds.push_back(makeF32("target_geom", {1, S, 7}, features.targetGeom));
    feeds.push_back(makeF32("source_dmi", {1, T, P, 10}, features.sourceDmi));

    const std::string outName = "target_rot6d";
    auto              ran     = model_->run(feeds, std::span<const std::string>(&outName, 1));
    if (!ran.ok()) return Result<std::vector<float>>::failure(ran.status());
    if (ran.value().empty()) {
        return Result<std::vector<float>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "SmrOnnxRunner: missing output"));
    }
    const auto&        bytes = ran.value()[0].tensor.bytes;
    std::vector<float> out(bytes.size() / sizeof(float));
    if (!out.empty()) std::memcpy(out.data(), bytes.data(), out.size() * sizeof(float));
    return Result<std::vector<float>>::success(std::move(out));
}

}  // namespace eve::animation
