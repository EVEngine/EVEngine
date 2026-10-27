// eve-hot-path
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Geometry/GJKClosestPoint.h>
#include <Jolt/Physics/Collision/Shape/ConvexShape.h>
#include "physics/World3D.h"
#include "physics/jolt/JoltQueryInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
JPH::RVec3 closestConvexPoint(const ShapeState& shape, JPH::RVec3Arg target) {
    const auto  body = shape.body.lock();
    const auto& g    = shape.geometry;
    const auto  pose = body->api().GetWorldTransform(body->id) *
                      JPH::RMat44::sRotationTranslation(JPH::Quat(g.localQx, g.localQy, g.localQz, g.localQw),
                                                        JPH::RVec3(g.localX, g.localY, g.localZ));
    const auto center = shape.native->GetCenterOfMass();
    const auto local  = JPH::Vec3(pose.InversedRotationTranslation() * target) - center;
    struct Point {
        JPH::Vec3 value;
        JPH::Vec3 GetSupport(JPH::Vec3Arg) const { return value; }
    } point{local};
    JPH::ConvexShape::SupportBuffer buffer;
    const auto*                     support =
        static_cast<const JPH::ConvexShape*>(shape.native.GetPtr())
            ->GetSupportFunction(JPH::ConvexShape::ESupportMode::IncludeConvexRadius, buffer, JPH::Vec3::sReplicate(1));
    JPH::GJKClosestPoint gjk;
    JPH::Vec3            direction = local.NormalizedOr(JPH::Vec3::sAxisX()), a, b;
    const float          distance  = gjk.GetClosestPoints(*support, point, 1.e-6f, FLT_MAX, direction, a, b);
    if (distance <= 1.e-12f) return target;
    return pose * (a + center);
}
eve::Result<int> WorldBackend::closestPoint(float x, float y, float z, float maxDistance) {
    if (!isValid()) return rejected<int>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x, y, z) || !std::isfinite(maxDistance) || maxDistance < 0 ||
        double(maxDistance) * maxDistance > FLT_MAX)
        return rejected<int>("Invalid closest point query", eve::DiagnosticCode::InvalidArgument);
    owner->closestBodyId_ = owner->closestShapeId_ = -1;
    owner->closestShapeTag_                        = 0;
    owner->closestX_ = owner->closestY_ = owner->closestZ_ = 0;
    owner->closestNormalX_ = owner->closestNormalY_ = owner->closestNormalZ_ = owner->closestDistance_ = 0;
    const JPH::RVec3 target(x, y, z);
    double           best = double(maxDistance) * maxDistance;
    for (const auto& [id, body] : state->bodies) {
        if (!body->active || !body->owner || body->owner->getId() == owner->queryIgnoredBodyId_) continue;
        for (const auto& shape : body->shapes) {
            if (!shape->owner || shape->sensor || shape->native->GetType() != JPH::EShapeType::Convex ||
                shape->owner->getId() == owner->queryIgnoredShapeId_ ||
                !(shape->filter.categoryBits & owner->queryMaskBits_) ||
                !(shape->filter.maskBits & owner->queryCategoryBits_))
                continue;
            const auto point    = closestConvexPoint(*shape, target);
            const auto distance = (point - target).LengthSq();
            const int  bid = body->owner->getId(), sid = shape->owner->getId();
            if (distance > best + 1.e-7 ||
                (std::abs(distance - best) <= 1.e-7 && owner->closestBodyId_ >= 0 &&
                 (bid > owner->closestBodyId_ || (bid == owner->closestBodyId_ && sid >= owner->closestShapeId_))))
                continue;
            best                    = distance;
            owner->closestBodyId_   = bid;
            owner->closestShapeId_  = sid;
            owner->closestShapeTag_ = shape->owner->getTag();
            owner->closestX_        = float(point.GetX());
            owner->closestY_        = float(point.GetY());
            owner->closestZ_        = float(point.GetZ());
            owner->closestDistance_ = float(std::sqrt(distance));
            const auto normal       = JPH::Vec3(target - point).NormalizedOr(JPH::Vec3::sZero());
            owner->closestNormalX_  = normal.GetX();
            owner->closestNormalY_  = normal.GetY();
            owner->closestNormalZ_  = normal.GetZ();
        }
    }
    return eve::Result<int>::success(owner->closestBodyId_);
}
eve::Result<ClothContact3D> WorldBackend::pointProbeFiltered(float x, float y, float z, float radius, uint64_t category,
                                                             uint64_t mask) const {
    if (!isValid()) return rejected<ClothContact3D>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x, y, z) || !std::isfinite(radius) || radius < 0)
        return rejected<ClothContact3D>("Invalid point probe", eve::DiagnosticCode::InvalidArgument);
    ClothContact3D result;
    for (const auto& [id, body] : state->bodies) {
        if (!body->owner) continue;
        for (const auto& shape : body->shapes) {
            if (shape->sensor || shape->native->GetType() != JPH::EShapeType::Convex ||
                !(shape->filter.categoryBits & mask) || !(shape->filter.maskBits & category))
                continue;
            const auto  delta = JPH::Vec3(JPH::RVec3(x, y, z) - closestConvexPoint(*shape, {x, y, z}));
            const float depth = radius - delta.Length();
            if (depth <= result.depth) continue;
            const auto normal  = delta.NormalizedOr(JPH::Vec3::sAxisY());
            result.hit         = true;
            result.depth       = depth;
            result.nx          = normal.GetX();
            result.ny          = normal.GetY();
            result.nz          = normal.GetZ();
            result.body        = body->owner;
            result.friction    = shape->material.friction;
            result.restitution = shape->material.restitution;
        }
    }
    return eve::Result<ClothContact3D>::success(result);
}
}  // namespace eve::physics::jolt
