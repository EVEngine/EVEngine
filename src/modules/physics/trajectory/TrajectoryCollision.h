#pragma once
#include "common/Export.h"

/**
 * @file TrajectoryCollision.h
 * @brief Bone-bound continuous collision sweeps against World3D (UE5-style trajectory traces).
 *
 * Each armed collider samples attachment points every advance, then casts a sphere
 * or capsule from the previous sample to the current sample through World3D.
 * World3D remains the collision authority; this runtime stores only generation-
 * qualified world handles and borrowed pose sources across frames.
 */

#include "common/AttachmentPoint.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
#include "physics/OwnedQuery3D.h"
#include "physics/PhysicsHandles.h"
#include "physics/trajectory/BoneCollider.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace eve {
class IAttachmentPointSource;
}

namespace eve::physics {
class World3D;
}

namespace eve::physics::trajectory {

/** @brief World-space sample of one collider endpoint pair. */
struct TrajectorySample {
    AttachmentPoint start{};
    AttachmentPoint end{};
    bool            valid = false;
};

/** @brief One confirmed World3D contact produced by a frame sweep. */
struct EVENGINE_API_DOMAINS TrajectoryHit {
    SubjectRef         subject;
    std::string        colliderId;
    PhysicsWorldHandle world  = PhysicsWorldHandle::invalid();
    PhysicsBodyHandle  body   = PhysicsBodyHandle::invalid();
    PhysicsShapeHandle shape  = PhysicsShapeHandle::invalid();
    int                bodyId = -1;
    int                shapeId = -1;
    int                shapeTag = 0;
    int                materialId = 0;
    float              x = 0.f, y = 0.f, z = 0.f;
    float              normalX = 0.f, normalY = 0.f, normalZ = 0.f;
    /** @brief Translation fraction in [0,1] along this frame's previous→current sweep. */
    float              fraction = 1.f;
};

/**
 * @brief Owning result of one deterministic trajectory advance.
 * @cost Linear in the number of unique World3D cast hits emitted this frame.
 */
struct EVENGINE_API_DOMAINS TrajectoryFrame {
    SimulationTick             tick = SimulationTick::zero();
    /** @brief Unique contacts produced by armed collider sweeps this tick. */
    std::vector<TrajectoryHit> hits;
};

/**
 * @brief Owner-thread authority for bone-bound collider catalogs and continuous sweeps.
 *
 * Pose sources are borrowed and must outlive the binding or be cleared first.
 * The World3D is observed through its weak lifetime token, so destroying the
 * world first safely invalidates later advances without retaining body pointers.
 *
 * @ownership Catalog and armed state are owned by this runtime. World3D owns
 *            all solver bodies/shapes. Hits publish process-local handles only.
 * @thread Affine to the physics owner thread; callbacks are never invoked.
 * @reentrancy Methods must not re-enter this runtime or mutate World3D topology
 *             from an attachment source during sampling.
 */
class EVENGINE_API_DOMAINS TrajectoryCollisionRuntime {
public:
    /**
     * @brief Construct a runtime bound to one borrowed World3D.
     * @param world World observed through its weak lifetime token; it may be destroyed first.
     */
    explicit TrajectoryCollisionRuntime(World3D& world);
    /** @brief Releases TrajectoryCollisionRuntime resources. */
    ~TrajectoryCollisionRuntime();

    TrajectoryCollisionRuntime(const TrajectoryCollisionRuntime&)            = delete;
    TrajectoryCollisionRuntime& operator=(const TrajectoryCollisionRuntime&) = delete;

    /** @brief Register or replace one collider catalog entry after full validation. */
    [[nodiscard]] Result<void> registerCollider(BoneColliderDefinition definition);
    /** @brief Remove one catalog entry and disarm every matching armed window. */
    [[nodiscard]] Result<void> unregisterCollider(std::string_view colliderId);

    /**
     * @brief Bind a subject's attachment pose source.
     * @param subject Non-nil subject that owns the animated skeleton.
     * @param source Borrowed pose source; must outlive this binding or be cleared.
     */
    [[nodiscard]] Result<void> bindPoseSource(SubjectRef subject, IAttachmentPointSource& source);
    /** @brief Remove one subject's pose source and disarm its armed colliders. */
    [[nodiscard]] Result<void> clearPoseSource(SubjectRef subject);

    /**
     * @brief Arm one collider for continuous sweeps while an attack window is open.
     * @remarks Requires a pose source. The first sample becomes both previous and current
     *          so the arming frame does not sweep from an uninitialized origin.
     */
    [[nodiscard]] Result<void> arm(SubjectRef subject, std::string_view colliderId);
    /** @brief Disarm one collider and forget its per-target hit memory. */
    [[nodiscard]] Result<void> disarm(SubjectRef subject, std::string_view colliderId);

    /**
     * @brief Sample poses for armed colliders, sweep previous→current through World3D, emit unique hits.
     * @param tick Deterministic simulation tick; must be non-decreasing.
     */
    [[nodiscard]] Result<TrajectoryFrame> advance(SimulationTick tick);

    /** @brief Current world-space sample for one armed collider, or NotFound. */
    [[nodiscard]] Result<TrajectorySample> sampleArmed(SubjectRef subject,
                                                       std::string_view colliderId) const;

    /** @brief Number of registered catalog entries. */
    [[nodiscard]] std::size_t colliderCount() const noexcept { return colliders_.size(); }
    /** @brief Number of armed collider windows. */
    [[nodiscard]] std::size_t armedCount() const noexcept { return armed_.size(); }
    /** @brief Number of subjects with a bound pose source. */
    [[nodiscard]] std::size_t poseSourceCount() const noexcept { return poses_.size(); }

private:
    struct ArmedCollider {
        SubjectRef        subject;
        std::string       colliderId;
        TrajectorySample  previous{};
        TrajectorySample  current{};
        bool              hasSample = false;
    };
    using ArmedKey     = std::tuple<std::string, std::string>;
    using HitMemoryKey = std::tuple<std::string, std::string, int, int>;

    [[nodiscard]] World3D* liveWorld() const noexcept;
    [[nodiscard]] Result<TrajectorySample> sampleCollider(SubjectRef subject,
                                                          const BoneColliderDefinition& def) const;
    [[nodiscard]] Result<void> sweepArmed(World3D& world, ArmedCollider& armed,
                                          const BoneColliderDefinition& def,
                                          std::vector<TrajectoryHit>& outHits);

    World3D*                      world_       = nullptr;
    std::weak_ptr<const void>     worldLifetime_;
    PhysicsWorldHandle            worldHandle_ = PhysicsWorldHandle::invalid();
    std::map<std::string, BoneColliderDefinition, std::less<>> colliders_;
    std::map<std::string, IAttachmentPointSource*, std::less<>> poses_;
    std::map<ArmedKey, ArmedCollider>                           armed_;
    std::map<HitMemoryKey, bool>                                hitMemory_;
    SimulationTick                                              lastTick_ = SimulationTick::zero();
};

}  // namespace eve::physics::trajectory
