#pragma once

/**
 * @file OwnedQuery3D.h
 * @brief Owning, generation-qualified results for cross-module 3D physics queries.
 */

#include "physics/PhysicsHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace eve::physics {

/** @brief Allocation-free owning three-dimensional physics value. */
struct PhysicsVector3D {
    float x = 0.f, y = 0.f, z = 0.f;
};

/** @brief Query filter applied atomically for one owning query operation. */
struct QueryFilter3D {
    std::uint32_t categoryBits   = 0xFFFFFFFFu;
    std::uint32_t maskBits       = 0xFFFFFFFFu;
    int           ignoredBodyId  = -1;
    int           ignoredShapeId = -1;
};

/** @brief Per-call capsule mover classification policy; never mutates persistent World3D settings. */
struct CapsuleMovePolicy3D {
    float upX = 0.f, upY = 1.f, upZ = 0.f;
    float maxSlopeRadians = 0.87266463f;
};

/** @brief One generation-qualified shape returned by an owning broad-phase query. */
struct BroadPhaseHit3D {
    PhysicsBodyHandle  body       = PhysicsBodyHandle::invalid();
    PhysicsShapeHandle shape      = PhysicsShapeHandle::invalid();
    int                bodyId     = -1;
    int                shapeId    = -1;
    int                shapeTag   = 0;
    int                materialId = 0;
};

/** @brief Fixed-capacity deterministic result of one AABB broad-phase query. */
struct BroadPhaseAabb3D {
    static constexpr std::size_t          Capacity = 256;
    std::array<BroadPhaseHit3D, Capacity> hits{};
    std::size_t                           count     = 0;
    bool                                  truncated = false;
};

/** @brief Owning snapshot of the closest ray intersection. */
struct RayHit3D {
    bool               hit           = false;
    PhysicsWorldHandle world         = PhysicsWorldHandle::invalid();
    PhysicsBodyHandle  body          = PhysicsBodyHandle::invalid();
    PhysicsShapeHandle shape         = PhysicsShapeHandle::invalid();
    int                bodyId        = -1;
    int                shapeId       = -1;
    int                shapeTag      = 0;
    int                materialId    = 0;
    int                triangleIndex = -1;
    float              x = 0.f, y = 0.f, z = 0.f;
    float              normalX = 0.f, normalY = 0.f, normalZ = 0.f;
    float              fraction = 1.f;
};

/** @brief Owning summary of a capsule overlap query. */
struct CapsuleOverlap3D {
    PhysicsWorldHandle world     = PhysicsWorldHandle::invalid();
    int                bodyCount = 0;
};

/** @brief Owning result of continuous capsule movement. */
struct CapsuleMove3D {
    PhysicsWorldHandle world       = PhysicsWorldHandle::invalid();
    bool               constrained = false;
    bool               grounded    = false;
    float              deltaX = 0.f, deltaY = 0.f, deltaZ = 0.f;
    float              normalX = 0.f, normalY = 0.f, normalZ = 0.f;
    int                planeCount = 0;
    int                iterations = 0;
};

}  // namespace eve::physics
