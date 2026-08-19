#pragma once

#include <cmath>
#include <algorithm>

namespace eve::animation {

/** @brief Local TRS used by skeletal animation (quaternion xyzw). */
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

/** @brief Rotate unit +Z by yaw (radians) around Y — used for planar facing. */
inline void yawToForward(float yaw, float &fx, float &fz) {
    fx = std::sin(yaw);
    fz = std::cos(yaw);
}

inline float length2(float x, float z) { return std::sqrt(x * x + z * z); }

/**
 * @brief Column-major 4x4 matrix (OpenGL / glTF / Assimp-compatible layout).
 * Elements: m[col * 4 + row].
 */
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return {}; }

    static Mat4 fromTRS(const TransformTRS &t) {
        Mat4 out;
        const float x = t.qx, y = t.qy, z = t.qz, w = t.qw;
        const float x2 = x + x, y2 = y + y, z2 = z + z;
        const float xx = x * x2, xy = x * y2, xz = x * z2;
        const float yy = y * y2, yz = y * z2, zz = z * z2;
        const float wx = w * x2, wy = w * y2, wz = w * z2;

        out.m[0]  = (1.f - (yy + zz)) * t.sx;
        out.m[1]  = (xy + wz) * t.sx;
        out.m[2]  = (xz - wy) * t.sx;
        out.m[3]  = 0.f;
        out.m[4]  = (xy - wz) * t.sy;
        out.m[5]  = (1.f - (xx + zz)) * t.sy;
        out.m[6]  = (yz + wx) * t.sy;
        out.m[7]  = 0.f;
        out.m[8]  = (xz + wy) * t.sz;
        out.m[9]  = (yz - wx) * t.sz;
        out.m[10] = (1.f - (xx + yy)) * t.sz;
        out.m[11] = 0.f;
        out.m[12] = t.px;
        out.m[13] = t.py;
        out.m[14] = t.pz;
        out.m[15] = 1.f;
        return out;
    }

    static Mat4 mul(const Mat4 &a, const Mat4 &b) {
        Mat4 out;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                out.m[col * 4 + row] = a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                                       a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                                       a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                                       a.m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return out;
    }

    void transformPoint(float x, float y, float z, float &ox, float &oy, float &oz) const {
        ox = m[0] * x + m[4] * y + m[8] * z + m[12];
        oy = m[1] * x + m[5] * y + m[9] * z + m[13];
        oz = m[2] * x + m[6] * y + m[10] * z + m[14];
    }
};

}  // namespace eve::animation
