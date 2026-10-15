// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include "physics/jolt/JoltQueryInternal.h"
namespace eve::physics::jolt {
eve::Result<QueryVolume> capsuleVolume(float ax, float ay, float az, float bx, float by, float bz, float radius) {
    if (!isFinite(ax, ay, az) || !isFinite(bx, by, bz) || !std::isfinite(radius) || radius <= 0)
        return rejected<QueryVolume>("Invalid capsule", eve::DiagnosticCode::InvalidArgument);
    const JPH::Vec3 axis(bx - ax, by - ay, bz - az);
    const float     length = axis.Length();
    if (!std::isfinite(length)) return rejected<QueryVolume>("Capsule overflow", eve::DiagnosticCode::InvalidArgument);
    QueryVolume result;
    result.shape  = length < 1.e-6f ? JPH::RefConst<JPH::Shape>(new JPH::SphereShape(radius))
                                    : JPH::RefConst<JPH::Shape>(new JPH::CapsuleShape(length * .5f, radius));
    auto rotation = length < 1.e-6f ? JPH::Quat::sIdentity() : JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis / length);
    result.transform =
        JPH::RMat44::sRotationTranslation(rotation, (JPH::RVec3(ax, ay, az) + JPH::RVec3(bx, by, bz)) * .5);
    return eve::Result<QueryVolume>::success(std::move(result));
}
bool isAcceptedHit(const State& state, JPH::BodyID id, const JPH::SubShapeID& sub, QueryFilter3D filter) {
    const auto* native = state.system.GetBodyLockInterfaceNoLock().TryGetBody(id);
    if (!native) return false;
    const auto* shape = child(*native, sub);
    if (!shape || !shape->owner || shape->sensor) return false;
    auto body = shape->body.lock();
    return body && body->owner && body->owner->getId() != filter.ignoredBodyId &&
           shape->owner->getId() != filter.ignoredShapeId && (shape->filter.categoryBits & filter.maskBits) &&
           (shape->filter.maskBits & filter.categoryBits);
}
RayHit3D queryHit(const State& state, JPH::BodyID id, const JPH::SubShapeID& sub, JPH::RVec3Arg point,
                  JPH::Vec3Arg normal, float fraction) {
    const auto& native = *state.system.GetBodyLockInterfaceNoLock().TryGetBody(id);
    const auto& shape  = *child(native, sub);
    const auto& body   = *shape.body.lock()->owner;
    RayHit3D    result;
    result.hit           = true;
    result.body          = body.runtimeHandle();
    result.shape         = shape.owner->runtimeHandle();
    result.bodyId        = body.getId();
    result.shapeId       = shape.owner->getId();
    result.shapeTag      = shape.owner->getTag();
    result.materialId    = static_cast<int>(contactMaterial(native, sub, shape).userMaterialId & 0xffffffffu);
    result.triangleIndex = triangleIndex(native, sub);
    result.x             = float(point.GetX());
    result.y             = float(point.GetY());
    result.z             = float(point.GetZ());
    result.normalX       = normal.GetX();
    result.normalY       = normal.GetY();
    result.normalZ       = normal.GetZ();
    result.fraction      = fraction;
    return result;
}
JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> overlapVolume(const State& state, const QueryVolume& volume,
                                                                        float separation) {
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> hits;
    JPH::CollideShapeSettings                                 settings;
    settings.mMaxSeparationDistance = separation;
    state.system.GetNarrowPhaseQuery().CollideShape(volume.shape, JPH::Vec3::sReplicate(1), volume.transform, settings,
                                                    volume.transform.GetTranslation(), hits);
    hits.Sort();
    return hits;
}
JPH::AllHitCollisionCollector<JPH::CastShapeCollector> sweepVolume(const State& state, const QueryVolume& volume,
                                                                   JPH::Vec3Arg delta) {
    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> hits;
    JPH::ShapeCastSettings                                 settings;
    settings.mReturnDeepestPoint = true;
    state.system.GetNarrowPhaseQuery().CastShape(
        JPH::RShapeCast(volume.shape, JPH::Vec3::sReplicate(1), volume.transform, delta), settings,
        volume.transform.GetTranslation(), hits);
    hits.Sort();
    return hits;
}
}  // namespace eve::physics::jolt
