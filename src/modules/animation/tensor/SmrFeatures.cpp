#include "animation/tensor/SmrFeatures.h"

#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "common/Exception.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

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
