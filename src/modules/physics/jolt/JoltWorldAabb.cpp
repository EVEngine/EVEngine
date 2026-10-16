// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <algorithm>
#include "physics/World3D.h"
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltWorld.h"
namespace eve::physics::jolt {
eve::Result<BroadPhaseAabb3D> WorldBackend::queryAabbBroadPhaseOwned(float x1, float y1, float z1, float x2, float y2,
                                                                     float z2, QueryFilter3D filter) {
    if (!isValid()) return rejected<BroadPhaseAabb3D>("World expired", eve::DiagnosticCode::StaleHandle);
    if (!isFinite(x1, y1, z1) || !isFinite(x2, y2, z2) || x1 > x2 || y1 > y2 || z1 > z2)
        return rejected<BroadPhaseAabb3D>("Invalid AABB", eve::DiagnosticCode::InvalidArgument);
    const JPH::AABox             box(JPH::Vec3(x1, y1, z1), JPH::Vec3(x2, y2, z2));
    std::vector<BroadPhaseHit3D> hits;
    for (const auto& [id, body] : state->bodies) {
        if (!body->owner || !body->active || body->owner->getId() == filter.ignoredBodyId) continue;
        const auto transform = body->api().GetWorldTransform(body->id);
        for (const auto& shape : body->shapes) {
            if (!shape->owner || shape->sensor || shape->owner->getId() == filter.ignoredShapeId ||
                !(shape->filter.categoryBits & filter.maskBits) || !(shape->filter.maskBits & filter.categoryBits))
                continue;
            const auto& g     = shape->geometry;
            auto        local = JPH::Mat44::sRotationTranslation(
                JPH::Quat(g.localQx, g.localQy, g.localQz, g.localQw).Normalized(), {g.localX, g.localY, g.localZ});
            auto bounds = shape->native->GetWorldSpaceBounds(
                (transform * local).PreTranslated(shape->native->GetCenterOfMass()), JPH::Vec3::sReplicate(1));
            if (!box.Overlaps(bounds)) continue;
            hits.push_back({body->owner->runtimeHandle(), shape->owner->runtimeHandle(), body->owner->getId(),
                            shape->owner->getId(), shape->owner->getTag(),
                            static_cast<int>(shape->material.userMaterialId & 0xffffffffu)});
        }
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        return std::tie(a.bodyId, a.shapeId) < std::tie(b.bodyId, b.shapeId);
    });
    BroadPhaseAabb3D result;
    result.count     = std::min(hits.size(), result.Capacity);
    result.truncated = hits.size() > result.Capacity;
    std::copy_n(hits.begin(), result.count, result.hits.begin());
    return eve::Result<BroadPhaseAabb3D>::success(result);
}
eve::Result<int> WorldBackend::queryAABB(float x1, float y1, float z1, float x2, float y2, float z2) {
    auto queried = queryAabbBroadPhaseOwned(
        x1, y1, z1, x2, y2, z2,
        {owner->queryCategoryBits_, owner->queryMaskBits_, owner->queryIgnoredBodyId_, owner->queryIgnoredShapeId_});
    if (!queried) return eve::Result<int>::failure(queried.status());
    // The legacy unbounded API must not silently report a truncated owning result.
    if (queried.value().truncated)
        return rejected<int>("AABB result exceeds the owning query capacity", eve::DiagnosticCode::Failed);
    owner->queryBodyIds_.clear();
    owner->queryShapes_.clear();
    for (size_t i = 0; i < queried.value().count; ++i) {
        const auto& hit = queried.value().hits[i];
        owner->queryShapes_.emplace_back(hit.bodyId, hit.shapeId);
        if (owner->queryBodyIds_.empty() || owner->queryBodyIds_.back() != hit.bodyId)
            owner->queryBodyIds_.push_back(hit.bodyId);
    }
    return eve::Result<int>::success(static_cast<int>(owner->queryBodyIds_.size()));
}
}  // namespace eve::physics::jolt
