#pragma once

#include "animation/AnimMath.h"

namespace eve::animation::detail {

inline TransformTRS mulTRS(const TransformTRS& parent, const TransformTRS& local) {
    // world = parent * local (TRS, scale ignored in rotation path for FK positions)
    TransformTRS out;
    // Rotate local translation by parent rotation, then add parent translation * scales.
    const float qx = parent.qx, qy = parent.qy, qz = parent.qz, qw = parent.qw;
    const float lx = local.px * parent.sx;
    const float ly = local.py * parent.sy;
    const float lz = local.pz * parent.sz;
    // q * v
    const float ix = qw * lx + qy * lz - qz * ly;
    const float iy = qw * ly + qz * lx - qx * lz;
    const float iz = qw * lz + qx * ly - qy * lx;
    const float iw = -qx * lx - qy * ly - qz * lz;
    out.px         = parent.px + (ix * qw + iw * -qx + iy * -qz - iz * -qy);
    out.py         = parent.py + (iy * qw + iw * -qy + iz * -qx - ix * -qz);
    out.pz         = parent.pz + (iz * qw + iw * -qz + ix * -qy - iy * -qx);

    // Quaternion multiply parent * local
    out.qw = parent.qw * local.qw - parent.qx * local.qx - parent.qy * local.qy - parent.qz * local.qz;
    out.qx = parent.qw * local.qx + parent.qx * local.qw + parent.qy * local.qz - parent.qz * local.qy;
    out.qy = parent.qw * local.qy - parent.qx * local.qz + parent.qy * local.qw + parent.qz * local.qx;
    out.qz = parent.qw * local.qz + parent.qx * local.qy - parent.qy * local.qx + parent.qz * local.qw;
    out.normalizeRotation();

    out.sx = parent.sx * local.sx;
    out.sy = parent.sy * local.sy;
    out.sz = parent.sz * local.sz;
    return out;
}

}  // namespace eve::animation::detail
