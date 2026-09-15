#pragma once

#include <array>
#include <cmath>
#include <span>
#include <vector>
#include "animation/AnimPose.h"

namespace eve::animation::detail {
// Finite-duration offsets relative to a moving target. Inputs are copied;
// simulation elapsed time is supplied by the owner, never wall-clock time.
struct PoseInertia {
    using Channels = std::array<float, 9>;
    std::vector<Channels> offsets, velocities;
    AnimPose              previousOutput;
    float                 historyInterval = 0.f;
    float                 duration        = 0.f;

    static Channels difference(const TransformTRS& source, const TransformTRS& target) {
        auto a = source, b = target;
        a.normalizeRotation();
        b.normalizeRotation();
        // log(source * inverse(target)), with hemisphere-independent shortest arc.
        float x = -a.qw * b.qx + a.qx * b.qw - a.qy * b.qz + a.qz * b.qy;
        float y = -a.qw * b.qy + a.qx * b.qz + a.qy * b.qw - a.qz * b.qx;
        float z = -a.qw * b.qz - a.qx * b.qy + a.qy * b.qx + a.qz * b.qw;
        float w = a.qw * b.qw + a.qx * b.qx + a.qy * b.qy + a.qz * b.qz;
        if (w < 0.f) {
            x = -x;
            y = -y;
            z = -z;
            w = -w;
        }
        float length = std::sqrt(x * x + y * y + z * z);
        float factor = length > 1e-8f ? 2.f * std::atan2(length, w) / length : 2.f;
        return {a.px - b.px, a.py - b.py, a.pz - b.pz, x * factor, y * factor,
                z * factor,  a.sx - b.sx, a.sy - b.sy, a.sz - b.sz};
    }

    void begin(const AnimPose& source, const AnimPose& target, const AnimPose& previousTarget, float seconds) {
        duration = seconds;
        offsets.resize(source.getBoneCount());
        velocities.resize(source.getBoneCount());
        bool history = historyInterval > 1e-6f && previousOutput.getBoneCount() == source.getBoneCount();
        for (int bone = 0; bone < source.getBoneCount(); ++bone) {
            offsets[bone] = difference(source.local(bone), target.local(bone));
            velocities[bone].fill(0.f);
            if (!history) continue;
            auto previous = difference(previousOutput.local(bone), previousTarget.local(bone));
            // At the pi branch cut, use the equivalent log closest to the current offset.
            float dot = 0.f, length = 0.f;
            for (int c = 3; c < 6; ++c) {
                dot += previous[c] * offsets[bone][c];
                length += previous[c] * previous[c];
            }
            length = std::sqrt(length);
            if (dot < 0.f && length > 1.57079632679f) {
                float factor = (length - 6.28318530718f) / length;
                for (int c = 3; c < 6; ++c) previous[c] *= factor;
            }
            for (int c = 0; c < 9; ++c) velocities[bone][c] = (offsets[bone][c] - previous[c]) / historyInterval;
        }
    }

    void remember(const AnimPose& output, float dt) {
        if (dt <= 0.f) return;
        previousOutput.copyFrom(&output);
        historyInterval = dt;
    }

    void apply(const AnimPose& target, float elapsed, AnimPose& output, std::span<const float> factors = {}) const {
        output.copyFrom(&target);
        if (duration <= 0.f || elapsed >= duration) return;
        for (int bone = 0; bone < target.getBoneCount(); ++bone) {
            const float boneDuration = duration * (factors.empty() ? 1.f : factors[bone]);
            if (boneDuration <= 0.f || elapsed >= boneDuration) continue;
            float u = std::clamp(elapsed / boneDuration, 0.f, 1.f), u2 = u * u, u3 = u2 * u, u4 = u3 * u, u5 = u4 * u;
            float position = 1.f - 10.f * u3 + 15.f * u4 - 6.f * u5;
            float velocity = boneDuration * (u - 6.f * u3 + 8.f * u4 - 3.f * u5);
            Channels value{};
            for (int c = 0; c < 9; ++c) value[c] = offsets[bone][c] * position + velocities[bone][c] * velocity;
            auto& result = output.local(bone);
            result.px += value[0];
            result.py += value[1];
            result.pz += value[2];
            result.sx += value[6];
            result.sy += value[7];
            result.sz += value[8];
            float angle  = std::sqrt(value[3] * value[3] + value[4] * value[4] + value[5] * value[5]);
            float factor = angle > 1e-8f ? std::sin(angle * .5f) / angle : .5f;
            float x = value[3] * factor, y = value[4] * factor, z = value[5] * factor, w = std::cos(angle * .5f);
            auto  q   = result;
            result.qx = w * q.qx + x * q.qw + y * q.qz - z * q.qy;
            result.qy = w * q.qy - x * q.qz + y * q.qw + z * q.qx;
            result.qz = w * q.qz + x * q.qy - y * q.qx + z * q.qw;
            result.qw = w * q.qw - x * q.qx - y * q.qy - z * q.qz;
            result.normalizeRotation();
        }
    }
};
}  // namespace eve::animation::detail
