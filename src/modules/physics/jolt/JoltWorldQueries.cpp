// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <algorithm>
#include "physics/World3D.h"
#include "physics/jolt/JoltQueryInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
eve::Result<RayHit3D> WorldBackend::rayCastOwned(float x1, float y1, float z1, float x2, float y2, float z2,
                                                 QueryFilter3D filter) {
    if (!isValid()) return rejected<RayHit3D>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x1, y1, z1) || !isFinite(x2, y2, z2))
        return rejected<RayHit3D>("Invalid ray", eve::DiagnosticCode::InvalidArgument);
    RayHit3D result;
    result.world = owner->runtimeHandle();
    JPH::RRayCast                                        ray({x1, y1, z1}, {x2 - x1, y2 - y1, z2 - z1});
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> hits;
    state->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), hits);
    hits.Sort();
    for (const auto& hit : hits.mHits) {
        auto i = state->bodies.find(hit.mBodyID.GetIndexAndSequenceNumber());
        if (i == state->bodies.end() || !i->second->owner) continue;
        const auto* native = state->system.GetBodyLockInterfaceNoLock().TryGetBody(hit.mBodyID);
        if (!native) continue;
        auto* shape = child(*native, hit.mSubShapeID2);
        if (!shape || !shape->owner || shape->sensor) continue;
        auto* body = i->second->owner;
        if (body->getId() == filter.ignoredBodyId || shape->owner->getId() == filter.ignoredShapeId ||
            !(shape->filter.categoryBits & filter.maskBits) || !(shape->filter.maskBits & filter.categoryBits))
            continue;
        auto point      = ray.GetPointOnRay(hit.mFraction);
        auto normal     = native->GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
        result.hit      = true;
        result.body     = body->runtimeHandle();
        result.shape    = shape->owner->runtimeHandle();
        result.bodyId   = body->getId();
        result.shapeId  = shape->owner->getId();
        result.shapeTag = shape->owner->getTag();
        result.materialId =
            static_cast<int>(contactMaterial(*native, hit.mSubShapeID2, *shape).userMaterialId & 0xffffffffu);
        result.triangleIndex = triangleIndex(*native, hit.mSubShapeID2);
        result.x             = static_cast<float>(point.GetX());
        result.y             = static_cast<float>(point.GetY());
        result.z             = static_cast<float>(point.GetZ());
        result.normalX       = normal.GetX();
        result.normalY       = normal.GetY();
        result.normalZ       = normal.GetZ();
        result.fraction      = hit.mFraction;
        break;
    }
    return eve::Result<RayHit3D>::success(result);
}
eve::Result<int> WorldBackend::rayCastFiltered(float x1, float y1, float z1, float x2, float y2, float z2,
                                               uint64_t maskBits) {
    if (!isValid()) return rejected<int>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x1, y1, z1) || !isFinite(x2, y2, z2))
        return rejected<int>("Invalid ray", eve::DiagnosticCode::InvalidArgument);
    JPH::RRayCast                                        ray({x1, y1, z1}, {x2 - x1, y2 - y1, z2 - z1});
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> hits;
    state->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), hits);
    hits.Sort();
    RayHit3D r;
    for (const auto& hit : hits.mHits) {
        const auto* native = state->system.GetBodyLockInterfaceNoLock().TryGetBody(hit.mBodyID);
        if (!native) continue;
        const auto* shape = child(*native, hit.mSubShapeID2);
        if (!shape || !shape->owner || shape->sensor || !(shape->filter.categoryBits & maskBits) ||
            !(shape->filter.maskBits & owner->queryCategoryBits_) ||
            shape->owner->getId() == owner->queryIgnoredShapeId_)
            continue;
        auto body = shape->body.lock();
        if (!body || !body->owner || body->owner->getId() == owner->queryIgnoredBodyId_) continue;
        auto point = ray.GetPointOnRay(hit.mFraction);
        r          = queryHit(*state, hit.mBodyID, hit.mSubShapeID2, point,
                              native->GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point), hit.mFraction);
        break;
    }
    owner->rayHitBodyId_        = r.bodyId;
    owner->rayHitShapeId_       = r.shapeId;
    owner->rayHitShapeTag_      = r.shapeTag;
    owner->rayHitMaterialId_    = r.materialId;
    owner->rayHitTriangleIndex_ = r.triangleIndex;
    owner->rayHitX_             = r.x;
    owner->rayHitY_             = r.y;
    owner->rayHitZ_             = r.z;
    owner->rayHitNormalX_       = r.normalX;
    owner->rayHitNormalY_       = r.normalY;
    owner->rayHitNormalZ_       = r.normalZ;
    owner->rayHitFraction_      = r.fraction;
    return eve::Result<int>::success(r.bodyId);
}
eve::Result<int> WorldBackend::rayCastAll(float x1, float y1, float z1, float x2, float y2, float z2, int maxHits) {
    if (!isValid()) return rejected<int>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x1, y1, z1) || !isFinite(x2, y2, z2) || maxHits < 1 || maxHits > 4096)
        return rejected<int>("Invalid ray", eve::DiagnosticCode::InvalidArgument);
    const JPH::RRayCast                                  ray({x1, y1, z1}, {x2 - x1, y2 - y1, z2 - z1});
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> hits;
    state->system.GetNarrowPhaseQuery().CastRay(ray, JPH::RayCastSettings(), hits);
    const QueryFilter3D             filter{owner->queryCategoryBits_, owner->queryMaskBits_, owner->queryIgnoredBodyId_,
                               owner->queryIgnoredShapeId_};
    std::vector<World3D::RayResult> values;
    for (const auto& hit : hits.mHits) {
        if (!isAcceptedHit(*state, hit.mBodyID, hit.mSubShapeID2, filter)) continue;
        const auto& native = *state->system.GetBodyLockInterfaceNoLock().TryGetBody(hit.mBodyID);
        const auto  point  = ray.GetPointOnRay(hit.mFraction);
        auto        r      = queryHit(*state, hit.mBodyID, hit.mSubShapeID2, point,
                                      native.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point), hit.mFraction);
        values.push_back({r.bodyId, r.shapeId, r.shapeTag, r.materialId, r.triangleIndex, r.x, r.y, r.z, r.normalX,
                          r.normalY, r.normalZ, r.fraction});
    }
    std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
        return std::tie(a.fraction, a.shapeId, a.triangleIndex) < std::tie(b.fraction, b.shapeId, b.triangleIndex);
    });
    if (values.size() > size_t(maxHits)) values.resize(size_t(maxHits));
    const auto r                = values.empty() ? World3D::RayResult{} : values.front();
    owner->rayResults_          = std::move(values);
    owner->rayHitBodyId_        = r.bodyId;
    owner->rayHitShapeId_       = r.shapeId;
    owner->rayHitShapeTag_      = r.shapeTag;
    owner->rayHitMaterialId_    = r.materialId;
    owner->rayHitTriangleIndex_ = r.triangleIndex;
    owner->rayHitX_             = r.x;
    owner->rayHitY_             = r.y;
    owner->rayHitZ_             = r.z;
    owner->rayHitNormalX_       = r.normalX;
    owner->rayHitNormalY_       = r.normalY;
    owner->rayHitNormalZ_       = r.normalZ;
    owner->rayHitFraction_      = r.fraction;
    return eve::Result<int>::success(static_cast<int>(owner->rayResults_.size()));
}
}  // namespace eve::physics::jolt
