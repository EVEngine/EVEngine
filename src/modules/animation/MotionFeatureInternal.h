#pragma once

#include <array>
#include <span>
#include "animation/AnimMath.h"

namespace eve::animation::detail {
inline constexpr int                  locomotionPoseOffset   = 19;
inline constexpr int                  locomotionFeatureCount = 30;
inline constexpr std::array<float, 5> locomotionHorizons{-0.05f, 0.f, 0.35f, 0.7f, 1.f};

inline float featureYaw(const TransformTRS& t) {
    return std::atan2(2.f * (t.qw * t.qy + t.qx * t.qz), 1.f - 2.f * (t.qx * t.qx + t.qy * t.qy));
}
inline std::array<float, 3> featureLocal(float x, float y, float z, float yaw) {
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    return {x * cs - z * sn, y, x * sn + z * cs};
}
inline std::array<float, 3> featurePosition(const TransformTRS& bone, const TransformTRS& root) {
    return featureLocal(bone.px - root.px, bone.py - root.py, bone.pz - root.pz, featureYaw(root));
}
// Shared by database indexing and live pose-history queries. Each sample removes
// its own root transform before differencing: capsule travel is not foot swing.
inline void encodeLocomotionPose(std::span<float> out, const std::array<TransformTRS, 4>& now,
                                 const std::array<TransformTRS, 4>& past, float interval) {
    const auto left = featurePosition(now[1], now[0]), right = featurePosition(now[2], now[0]);
    for (int axis = 0; axis < 3; ++axis) out[19 + axis] = left[axis] - right[axis];
    for (int foot = 1; foot <= 2; ++foot) {
        const auto current = featurePosition(now[foot], now[0]), previous = featurePosition(past[foot], past[0]);
        for (int axis = 0; axis < 3; ++axis)
            out[22 + (foot - 1) * 3 + axis] = (current[axis] - previous[axis]) / interval;
    }
    // UE's GLTFExporter maps local UE Y to glTF Z. Keep the projected vector's
    // length (a tilted pelvis must not be renormalized after stripping vertical).
    const auto& q       = now[3];
    const auto  heading = featureLocal(2.f * (q.qx * q.qz + q.qw * q.qy), 0.f, 1.f - 2.f * (q.qx * q.qx + q.qy * q.qy),
                                       featureYaw(now[0]));
    out[28]             = heading[0];
    out[29]             = heading[2];
}
template <class Sample>
inline void encodeLocomotionTrajectory(std::span<float> out, const std::array<Sample, 5>& samples, float yaw) {
    auto position = [&](int sample, int offset) {
        const auto p    = featureLocal(samples[sample].x, samples[sample].y, samples[sample].z, yaw);
        out[offset]     = p[0];
        out[offset + 1] = p[2];
    };
    auto velocity = [&](int sample, int offset) {
        const auto v    = featureLocal(samples[sample].vx, samples[sample].vy, samples[sample].vz, yaw);
        out[offset]     = v[0];
        out[offset + 1] = v[2];
    };
    auto heading = [&](int sample, int offset) {
        yawToForward(samples[sample].yaw - yaw, out[offset], out[offset + 1]);
    };
    velocity(1, 0);
    position(0, 2);
    heading(1, 4);
    position(2, 6);
    heading(2, 8);
    position(3, 10);
    velocity(3, 12);
    heading(3, 14);
    auto v = featureLocal(samples[4].vx, samples[4].vy, samples[4].vz, yaw);
    // Reference bNormalize clamps at 1 cm/s; these assets and queries use metres.
    const float divisor = std::max(0.01f, std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    for (int axis = 0; axis < 3; ++axis) out[16 + axis] = v[axis] / divisor;
}
inline float locomotionWeight(int index) {
    if (index == 2 || index == 3 || (index >= 22 && index < 28)) return 0.3f;
    if (index >= 16 && index < 19) return 1.5f;
    if (index >= 28) return 0.1f;
    return 1.f;
}
}  // namespace eve::animation::detail
