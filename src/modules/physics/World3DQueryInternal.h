#pragma once

#include <box3d/box3d.h>
#include <cmath>
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

namespace eve::physics::query_detail {

class ScopedQueryState3D {
public:
    ScopedQueryState3D(World3D &world, QueryFilter3D filter)
        : world_(world),
          category_(world.getQueryCategoryBits()),
          mask_(world.getQueryMaskBits()),
          body_(world.getQueryIgnoredBodyId()),
          shape_(world.getQueryIgnoredShapeId()) {
        world_.setQueryFilter(static_cast<int>(filter.categoryBits), static_cast<int>(filter.maskBits));
        world_.setQueryIgnoredBodyId(filter.ignoredBodyId);
        world_.setQueryIgnoredShapeId(filter.ignoredShapeId);
    }

    ~ScopedQueryState3D() noexcept {
        world_.setQueryFilter(category_, mask_);
        world_.setQueryIgnoredBodyId(body_);
        world_.setQueryIgnoredShapeId(shape_);
    }

private:
    World3D &world_;
    int      category_;
    int      mask_;
    int      body_;
    int      shape_;
};

template <class T>
eve::Result<T> ownedQueryFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "physics.query3d"));
}

/** @brief Internal backend ABI lookup; null for an invalid backend id.
 * @ownership Borrowed from the owning World3D; never retained by this adapter.
 * @lifetime Immediate synchronous query only, invalidated by body/world destruction.
 * @thread Owning physics thread; invokes no callbacks.
 */
inline Body3D *bodyFromShape(b3ShapeId shapeId) {
    if (!b3Shape_IsValid(shapeId)) return nullptr;
    return static_cast<Body3D *>(b3Body_GetUserData(b3Shape_GetBody(shapeId)));
}

/** @brief Internal backend ABI lookup; null for an invalid backend id.
 * @ownership Borrowed from the owning World3D; never retained by this adapter.
 * @lifetime Immediate synchronous query only, invalidated by shape/world destruction.
 * @thread Owning physics thread; invokes no callbacks.
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

}  // namespace eve::physics::query_detail
