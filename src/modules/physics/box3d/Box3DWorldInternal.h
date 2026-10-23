#pragma once
#include <box3d/box3d.h>
#include "physics/World3DInternal.h"
namespace eve::physics::world3d_detail {
/**
 * @brief Resolves a native Box3D user-data observer for an immediate internal call.
 * @return Borrowed nullable Body3D pointer; invalid native IDs yield null.
 * @ownership The native user-data slot does not own the wrapper.
 * @lifetime Valid only until wrapper invalidation or destruction; never retain across mutation.
 * @thread Owning simulation thread only; invokes no callbacks.
 */
inline Body3D *bodyFromShape(b3ShapeId shapeId) {
    if (!b3Shape_IsValid(shapeId)) return nullptr;
    return static_cast<Body3D *>(b3Body_GetUserData(b3Shape_GetBody(shapeId)));
}

/**
 * @brief Resolves a native Box3D user-data observer for an immediate internal call.
 * @return Borrowed nullable Shape3D pointer; invalid native IDs yield null.
 * @ownership The native user-data slot does not own the wrapper.
 * @lifetime Valid only until wrapper invalidation or destruction; never retain across mutation.
 * @thread Owning simulation thread only; invokes no callbacks.
 */
inline Shape3D *shapeFromShape(b3ShapeId shapeId) {
    if (!b3Shape_IsValid(shapeId)) return nullptr;
    return static_cast<Shape3D *>(b3Shape_GetUserData(shapeId));
}

inline b3Quat normalizedQueryQuat(float qx, float qy, float qz, float qw, const char *operation) {
    if (!std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw))
        throw eve::Exception("%s: quaternion must be finite", operation);
    const float lengthSquared = qx * qx + qy * qy + qz * qz + qw * qw;
    if (!(lengthSquared > 1e-12f)) throw eve::Exception("%s: quaternion must be non-zero", operation);
    const float inverseLength = 1.f / std::sqrt(lengthSquared);
    return b3Quat{{qx * inverseLength, qy * inverseLength, qz * inverseLength}, qw * inverseLength};
}

inline void makeBoxProxyPoints(b3Vec3 (&points)[8], float width, float height, float depth, b3Quat q,
                               const char *operation) {
    if (!(width > 0.f) || !(height > 0.f) || !(depth > 0.f) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(depth))
        throw eve::Exception("%s: box dimensions must be finite and > 0", operation);
    const float hx = 0.5f * width, hy = 0.5f * height, hz = 0.5f * depth;
    int         point = 0;
    for (int z = -1; z <= 1; z += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int x = -1; x <= 1; x += 2)
                points[point++] = b3RotateVector(
                    q, b3Vec3{static_cast<float>(x) * hx, static_cast<float>(y) * hy, static_cast<float>(z) * hz});
}


}  // namespace eve::physics::world3d_detail
