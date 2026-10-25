// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <algorithm>
#include "physics/World3D.h"
#include "physics/jolt/JoltQueryInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
namespace {
eve::Result<QueryVolume> boxVolume(float x, float y, float z, float w, float h, float d, float qx, float qy, float qz,
                                   float qw) {
    JPH::Quat rotation(qx, qy, qz, qw);
    if (!isFinite(x, y, z) || !isFinite(w, h, d) || w <= 0 || h <= 0 || d <= 0 || !std::isfinite(rotation.LengthSq()) ||
        rotation.LengthSq() < 1.e-12f)
        return rejected<QueryVolume>("Invalid box query", eve::DiagnosticCode::InvalidArgument);
    QueryVolume volume;
    volume.shape     = new JPH::BoxShape({w * .5f, h * .5f, d * .5f}, std::min({.05f, w * .5f, h * .5f, d * .5f}));
    volume.transform = JPH::RMat44::sRotationTranslation(rotation.Normalized(), {x, y, z});
    return eve::Result<QueryVolume>::success(std::move(volume));
}
}  // namespace
eve::Result<int> WorldBackend::overlapQuery(const QueryVolume& volume, QueryFilter3D filter) {
    if (!isValid()) return rejected<int>("World expired", eve::DiagnosticCode::StaleHandle);
    auto                             hits = overlapVolume(*state, volume);
    std::vector<std::pair<int, int>> shapes;
    std::vector<int>                 bodies;
    for (const auto& hit : hits.mHits) {
        if (!isAcceptedHit(*state, hit.mBodyID2, hit.mSubShapeID2, filter)) continue;
        auto value = queryHit(*state, hit.mBodyID2, hit.mSubShapeID2, JPH::RVec3::sZero(), JPH::Vec3::sZero(), 0);
        shapes.emplace_back(value.bodyId, value.shapeId);
        bodies.push_back(value.bodyId);
    }
    std::sort(shapes.begin(), shapes.end());
    shapes.erase(std::unique(shapes.begin(), shapes.end()), shapes.end());
    std::sort(bodies.begin(), bodies.end());
    bodies.erase(std::unique(bodies.begin(), bodies.end()), bodies.end());
    owner->queryShapes_  = std::move(shapes);
    owner->queryBodyIds_ = std::move(bodies);
    return eve::Result<int>::success(static_cast<int>(owner->queryBodyIds_.size()));
}
eve::Result<CapsuleOverlap3D> WorldBackend::queryCapsuleOwned(float ax, float ay, float az, float bx, float by,
                                                              float bz, float radius, QueryFilter3D filter) {
    auto volume = capsuleVolume(ax, ay, az, bx, by, bz, radius);
    if (!volume) return eve::Result<CapsuleOverlap3D>::failure(volume.status());
    auto queried = overlapQuery(volume.value(), filter);
    if (!queried) return eve::Result<CapsuleOverlap3D>::failure(queried.status());
    return eve::Result<CapsuleOverlap3D>::success({owner->runtimeHandle(), queried.value()});
}
eve::Result<int> WorldBackend::queryCapsule(float ax, float ay, float az, float bx, float by, float bz, float radius) {
    auto result = queryCapsuleOwned(
        ax, ay, az, bx, by, bz, radius,
        {owner->queryCategoryBits_, owner->queryMaskBits_, owner->queryIgnoredBodyId_, owner->queryIgnoredShapeId_});
    if (!result) return eve::Result<int>::failure(result.status());
    return eve::Result<int>::success(result.value().bodyCount);
}
eve::Result<int> WorldBackend::querySphere(float x, float y, float z, float radius) {
    return queryCapsule(x, y, z, x, y, z, radius);
}
eve::Result<int> WorldBackend::queryBox(float x, float y, float z, float w, float h, float d, float qx, float qy,
                                        float qz, float qw) {
    auto volume = boxVolume(x, y, z, w, h, d, qx, qy, qz, qw);
    if (!volume) return eve::Result<int>::failure(volume.status());
    return overlapQuery(volume.value(), {owner->queryCategoryBits_, owner->queryMaskBits_, owner->queryIgnoredBodyId_,
                                         owner->queryIgnoredShapeId_});
}
eve::Result<int> WorldBackend::castQuery(const QueryVolume& volume, float dx, float dy, float dz, int maxHits) {
    if (!isValid()) return rejected<int>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(dx, dy, dz) || maxHits < 1 || maxHits > 4096)
        return rejected<int>("Invalid cast", eve::DiagnosticCode::InvalidArgument);
    const QueryFilter3D filter{owner->queryCategoryBits_, owner->queryMaskBits_, owner->queryIgnoredBodyId_,
                               owner->queryIgnoredShapeId_};
    auto                hits = sweepVolume(*state, volume, {dx, dy, dz});
    std::vector<World3D::ShapeCastResult> values;
    for (const auto& hit : hits.mHits) {
        if (!isAcceptedHit(*state, hit.mBodyID2, hit.mSubShapeID2, filter)) continue;
        auto r =
            queryHit(*state, hit.mBodyID2, hit.mSubShapeID2, volume.transform.GetTranslation() + hit.mContactPointOn2,
                     -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()), hit.mFraction);
        values.push_back({r.bodyId, r.shapeId, r.shapeTag, r.materialId, r.triangleIndex, r.x, r.y, r.z, r.normalX,
                          r.normalY, r.normalZ, r.fraction});
    }
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
        return std::tie(a.fraction, a.shapeId, a.triangleIndex) < std::tie(b.fraction, b.shapeId, b.triangleIndex);
    });
    if (values.size() > size_t(maxHits)) values.resize(size_t(maxHits));
    const auto first               = values.empty() ? World3D::ShapeCastResult{} : values.front();
    owner->shapeCastResults_       = std::move(values);
    owner->shapeCastBodyId_        = first.bodyId;
    owner->shapeCastShapeId_       = first.shapeId;
    owner->shapeCastShapeTag_      = first.shapeTag;
    owner->shapeCastMaterialId_    = first.materialId;
    owner->shapeCastTriangleIndex_ = first.triangleIndex;
    owner->shapeCastX_             = first.x;
    owner->shapeCastY_             = first.y;
    owner->shapeCastZ_             = first.z;
    owner->shapeCastNormalX_       = first.normalX;
    owner->shapeCastNormalY_       = first.normalY;
    owner->shapeCastNormalZ_       = first.normalZ;
    owner->shapeCastFraction_      = first.fraction;
    return eve::Result<int>::success(static_cast<int>(owner->shapeCastResults_.size()));
}
eve::Result<int> WorldBackend::castCapsuleAll(float ax, float ay, float az, float bx, float by, float bz, float radius,
                                              float dx, float dy, float dz, int maxHits) {
    auto volume = capsuleVolume(ax, ay, az, bx, by, bz, radius);
    if (!volume) return eve::Result<int>::failure(volume.status());
    return castQuery(volume.value(), dx, dy, dz, maxHits);
}
eve::Result<int> WorldBackend::castSphereAll(float x, float y, float z, float radius, float dx, float dy, float dz,
                                             int maxHits) {
    return castCapsuleAll(x, y, z, x, y, z, radius, dx, dy, dz, maxHits);
}
eve::Result<int> WorldBackend::castBoxAll(float x, float y, float z, float w, float h, float d, float qx, float qy,
                                          float qz, float qw, float dx, float dy, float dz, int maxHits) {
    auto volume = boxVolume(x, y, z, w, h, d, qx, qy, qz, qw);
    if (!volume) return eve::Result<int>::failure(volume.status());
    return castQuery(volume.value(), dx, dy, dz, maxHits);
}
eve::Result<CameraSphereHit3D> WorldBackend::sphereCast(float x, float y, float z, float x2, float y2, float z2,
                                                        float radius, uint64_t mask, int ignore) const {
    const float dx = x2 - x, dy = y2 - y, dz = z2 - z;
    if (!isValid()) return rejected<CameraSphereHit3D>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(dx, dy, dz))
        return rejected<CameraSphereHit3D>("Invalid sweep", eve::DiagnosticCode::InvalidArgument);
    if (radius == 0) {
        if (!isFinite(x, y, z)) return rejected<CameraSphereHit3D>("Invalid ray", eve::DiagnosticCode::InvalidArgument);
        const JPH::RRayCast                                  ray({x, y, z}, {dx, dy, dz});
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> hits;
        state->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), hits);
        hits.Sort();
        CameraSphereHit3D result;
        for (const auto& hit : hits.mHits) {
            const auto* native = state->system.GetBodyLockInterfaceNoLock().TryGetBody(hit.mBodyID);
            if (!native) continue;
            const auto* shape = child(*native, hit.mSubShapeID2);
            if (!shape || !shape->owner || shape->sensor || !(shape->filter.categoryBits & mask)) continue;
            auto body = shape->body.lock();
            if (!body || !body->owner || body->owner->getId() == ignore) continue;
            auto point  = ray.GetPointOnRay(hit.mFraction);
            auto normal = native->GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
            result      = {true,
                           body->owner->getId(),
                           hit.mFraction,
                           float(point.GetX()),
                           float(point.GetY()),
                           float(point.GetZ()),
                           normal.GetX(),
                           normal.GetY(),
                           normal.GetZ()};
            break;
        }
        return eve::Result<CameraSphereHit3D>::success(result);
    }
    auto volume = capsuleVolume(x, y, z, x, y, z, radius);
    if (!volume) return eve::Result<CameraSphereHit3D>::failure(volume.status());
    auto              hits = sweepVolume(*state, volume.value(), {dx, dy, dz});
    CameraSphereHit3D result;
    for (const auto& hit : hits.mHits) {
        const auto* native = state->system.GetBodyLockInterfaceNoLock().TryGetBody(hit.mBodyID2);
        if (!native) continue;
        const auto* shape = child(*native, hit.mSubShapeID2);
        if (!shape || !shape->owner || shape->sensor || !(shape->filter.categoryBits & mask)) continue;
        auto body = shape->body.lock();
        if (!body || !body->owner || body->owner->getId() == ignore) continue;
        auto normal = -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
        auto point  = volume.value().transform.GetTranslation() + hit.mContactPointOn2;
        result      = {true,
                       body->owner->getId(),
                       hit.mFraction,
                       float(point.GetX()),
                       float(point.GetY()),
                       float(point.GetZ()),
                       normal.GetX(),
                       normal.GetY(),
                       normal.GetZ()};
        break;
    }
    return eve::Result<CameraSphereHit3D>::success(result);
}
}  // namespace eve::physics::jolt
