// eve-hot-path
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <algorithm>
#include <tuple>
#include "physics/World3D.h"
#include "physics/jolt/JoltInternal.h"
#include "physics/jolt/JoltWorld.h"

namespace eve::physics::jolt {
void Listener::OnContactSolved(const JPH::Body& a, const JPH::SubShapeID& subA, const JPH::Body& b,
                               const JPH::SubShapeID& subB, const JPH::ContactSolveResult& result) {
    auto* sa = child(a, subA);
    auto* sb = child(b, subB);
    if (!sa || !sb || !sa->owner || !sb->owner || sa->sensor || sb->sensor) return;
    auto ba = sa->body.lock(), bb = sb->body.lock();
    if (!ba || !bb || !ba->owner || !bb->owner) return;
    const bool                flip   = sa->owner->getId() > sb->owner->getId();
    const JPH::SubShapeIDPair key    = flip ? JPH::SubShapeIDPair(b.GetID(), subB, a.GetID(), subA)
                                            : JPH::SubShapeIDPair(a.GetID(), subA, b.GetID(), subB);
    auto*                     first  = flip ? sb->owner : sa->owner;
    auto*                     second = flip ? sa->owner : sb->owner;
    auto*                     bodyA  = flip ? bb->owner : ba->owner;
    auto*                     bodyB  = flip ? ba->owner : bb->owner;
    const auto                normal = flip ? -result.mNormal : result.mNormal;
    const auto                posA   = flip ? result.mPosition2 : result.mPosition1;
    const auto                posB   = flip ? result.mPosition1 : result.mPosition2;
    auto&                     points = solved[key];
    // CCD solves a single averaged manifold point; use a separate feature from discrete points.
    const uint32_t index = result.mCCD ? JPH::ContactConstraintManager::MaxContactPoints : result.mPointIndex;
    if (points.size() <= index) points.resize(size_t(index) + 1);
    auto&          point     = points[index];
    const float    total     = point.pointA.totalNormalImpulse + result.mNormalImpulse;
    const uint32_t feature   = index;
    point.bodyA              = bodyA->runtimeHandle();
    point.bodyB              = bodyB->runtimeHandle();
    point.shapeA             = first->runtimeHandle();
    point.shapeB             = second->runtimeHandle();
    point.substep            = substep;
    point.pointA             = {first->getId(),         first->getTag(),       bodyB->getId(),
                                second->getId(),        second->getTag(),      feature,
                                float(posA.GetX()),     float(posA.GetY()),    float(posA.GetZ()),
                                normal.GetX(),          normal.GetY(),         normal.GetZ(),
                                result.mSeparation,     result.mNormalImpulse, total,
                                result.mNormalVelocity, result.mPersisted};
    point.pointB             = {second->getId(),        second->getTag(),      bodyA->getId(),
                                first->getId(),         first->getTag(),       feature,
                                float(posB.GetX()),     float(posB.GetY()),    float(posB.GetZ()),
                                -normal.GetX(),         -normal.GetY(),        -normal.GetZ(),
                                result.mSeparation,     result.mNormalImpulse, total,
                                result.mNormalVelocity, result.mPersisted};
    const uint64_t hitKey    = (uint64_t(first->getId()) << 32) | uint32_t(second->getId());
    const float    pairTotal = (normalTotals[hitKey] += result.mNormalImpulse);
    const float    speed     = -result.mNormalVelocity;
    auto           hit       = hits.find(hitKey);
    if ((first->areHitEventsEnabled() || second->areHitEventsEnabled()) && result.mNormalImpulse > 0 &&
        speed > state->hitThreshold && (hit == hits.end() || speed > hit->second.speed)) {
        const auto position = .5 * (posA + posB);
        SolvedHit  value{point.shapeA,
                        point.shapeB,
                         {bodyA->getId(), first->getId(), first->getTag(), bodyB->getId(), second->getId(),
                          second->getTag(), false, false},
                        float(position.GetX()),
                        float(position.GetY()),
                        float(position.GetZ()),
                        normal.GetX(),
                        normal.GetY(),
                        normal.GetZ(),
                        speed,
                        pairTotal};
        hit = hits.insert_or_assign(hitKey, value).first;
    }
    if (hit != hits.end()) hit->second.impulse = pairTotal;
}

eve::Result<BodyContacts3D> WorldBackend::queryBodyContactsOwned(PhysicsBodyHandle body, int maxPoints) const {
    if (!isValid()) return rejected<BodyContacts3D>("World expired", eve::DiagnosticCode::StaleHandle);
    if (state->faulted)
        return rejected<BodyContacts3D>("No completed contact snapshot after solver failure",
                                        eve::DiagnosticCode::Failed);
    if (maxPoints < 1 || maxPoints > 4096)
        return rejected<BodyContacts3D>("maxPoints must be in [1,4096]", eve::DiagnosticCode::InvalidArgument);
    if (!owner->findBody(body)) return rejected<BodyContacts3D>("Body expired", eve::DiagnosticCode::StaleHandle);
    BodyContacts3D result;
    result.body = body;
    if (!owner->findBody(body)->isActive()) return eve::Result<BodyContacts3D>::success(std::move(result));
    for (const auto& [key, points] : state->listener->solved)
        for (const auto& point : points) {
            if (!point.substep || point.substep != state->listener->substep || !owner->findShape(point.shapeA) ||
                !owner->findShape(point.shapeB) || !owner->findShape(point.shapeA)->getBody()->isActive() ||
                !owner->findShape(point.shapeB)->getBody()->isActive())
                continue;
            if (point.bodyA == body)
                result.points.push_back(point.pointA);
            else if (point.bodyB == body)
                result.points.push_back(point.pointB);
        }
    std::sort(result.points.begin(), result.points.end(), [](const auto& a, const auto& b) {
        return std::tie(a.shapeId, a.otherShapeId, a.featureId, a.x, a.y, a.z) <
               std::tie(b.shapeId, b.otherShapeId, b.featureId, b.x, b.y, b.z);
    });
    if (result.points.size() > size_t(maxPoints)) {
        result.points.resize(size_t(maxPoints));
        result.coverage = ContactQueryCoverage3D::Truncated;
    }
    return eve::Result<BodyContacts3D>::success(std::move(result));
}
}  // namespace eve::physics::jolt
