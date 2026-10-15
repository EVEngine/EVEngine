#pragma once
// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include "physics/jolt/JoltInternal.h"
namespace eve::physics::jolt {
struct QueryVolume {
    JPH::RefConst<JPH::Shape> shape;
    JPH::RMat44               transform;
};
/** @brief Returns a convex leaf's world closest point; shape/body are borrowed only during this idle-thread call. */
JPH::RVec3 closestConvexPoint(const ShapeState&, JPH::RVec3Arg);
/** @brief Cooks an ephemeral query capsule; inputs are endpoints in world space. */
[[nodiscard]] eve::Result<QueryVolume> capsuleVolume(float, float, float, float, float, float, float);
/** @brief Filters a synchronous native hit, borrowing state only for the call. */
bool isAcceptedHit(const State&, JPH::BodyID, const JPH::SubShapeID&, QueryFilter3D);
/** @brief Projects a validated native hit into an owning result on the idle simulation thread. */
RayHit3D queryHit(const State&, JPH::BodyID, const JPH::SubShapeID&, JPH::RVec3Arg, JPH::Vec3Arg, float);
/** @brief Collects owning native records for this synchronous query; no pointers escape. */
JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> overlapVolume(const State&, const QueryVolume&,
                                                                        float separation = 0.f);
/** @brief Sweeps one ephemeral convex volume; results are local to its starting position. */
JPH::AllHitCollisionCollector<JPH::CastShapeCollector> sweepVolume(const State&, const QueryVolume&, JPH::Vec3Arg);
}  // namespace eve::physics::jolt
