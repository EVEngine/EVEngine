#pragma once

#include <cmath>
#include <algorithm>

namespace eve::animation {

/** Local TRS used by skeletal animation (quaternion xyzw). */
struct TransformTRS {
    float px = 0.f, py = 0.f, pz = 0.f;
    float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    float sx = 1.f, sy = 1.f, sz = 1.f;

    static TransformTRS identity() { return {}; }

    void normalizeRotation() {
        const float len = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (len > 1e-8f) {
            const float inv = 1.f / len;
            qx *= inv;
            qy *= inv;
            qz *= inv;
            qw *= inv;
        } else {
            qx = qy = qz = 0.f;
            qw           = 1.f;
        }
    }
};

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline void slerpQuat(float ax, float ay, float az, float aw, float bx, float by, float bz, float bw,
                      float t, float &ox, float &oy, float &oz, float &ow) {
    float cosTheta = ax * bx + ay * by + az * bz + aw * bw;
    if (cosTheta < 0.f) {
        bx       = -bx;
        by       = -by;
        bz       = -bz;
        bw       = -bw;
        cosTheta = -cosTheta;
    }
    if (cosTheta > 0.9995f) {
        ox = lerpf(ax, bx, t);
        oy = lerpf(ay, by, t);
        oz = lerpf(az, bz, t);
        ow = lerpf(aw, bw, t);
        const float len = std::sqrt(ox * ox + oy * oy + oz * oz + ow * ow);
        if (len > 1e-8f) {
            const float inv = 1.f / len;
            ox *= inv;
            oy *= inv;
            oz *= inv;
            ow *= inv;
        }
        return;
    }
    const float theta    = std::acos(clampf(cosTheta, -1.f, 1.f));
    const float sinTheta = std::sin(theta);
    const float w0       = std::sin((1.f - t) * theta) / sinTheta;
    const float w1       = std::sin(t * theta) / sinTheta;
    ox                   = ax * w0 + bx * w1;
    oy                   = ay * w0 + by * w1;
    oz                   = az * w0 + bz * w1;
    ow                   = aw * w0 + bw * w1;
}

inline TransformTRS blendTRS(const TransformTRS &a, const TransformTRS &b, float t) {
    t = clampf(t, 0.f, 1.f);
    TransformTRS out;
    out.px = lerpf(a.px, b.px, t);
    out.py = lerpf(a.py, b.py, t);
    out.pz = lerpf(a.pz, b.pz, t);
    out.sx = lerpf(a.sx, b.sx, t);
    out.sy = lerpf(a.sy, b.sy, t);
    out.sz = lerpf(a.sz, b.sz, t);
    slerpQuat(a.qx, a.qy, a.qz, a.qw, b.qx, b.qy, b.qz, b.qw, t, out.qx, out.qy, out.qz, out.qw);
    return out;
}

/** Rotate unit +Z by yaw (radians) around Y — used for planar facing. */
inline void yawToForward(float yaw, float &fx, float &fz) {
    fx = std::sin(yaw);
    fz = std::cos(yaw);
}

inline float length2(float x, float z) { return std::sqrt(x * x + z * z); }

}  // namespace eve::animation
