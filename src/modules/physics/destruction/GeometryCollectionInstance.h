#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "common/Time.h"
#include "physics/PhysicsLink.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/GeometryCollectionAsset.h"
#include "physics/destruction/GeometryCollectionSnapshot.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace eve::physics {

class World3D;

/** @brief Runtime lifecycle for one bone inside a geometry-collection instance. */
enum class BoneRuntimeState : std::uint8_t {
    Attached = 0,
    Detached = 1,
    Sleeping = 2,
};

/** @brief One ordered detach event emitted during step. */
struct EVENGINE_API_DOMAINS BoneDetachEvent {
    int boneA = -1;
    int boneB = -1;
    std::int64_t tick = 0;
};

/**
 * @brief Emitted when a broken edge separates two different cook clusters.
 *
 * Aligns with Chaos cluster-break observability; topology authority remains the
 * connection graph.
 */
struct EVENGINE_API_DOMAINS ClusterBreakEvent {
    int clusterA = -1;
    int clusterB = -1;
    int boneA = -1;
    int boneB = -1;
    std::int64_t tick = 0;
};

/**
 * @brief Per-step production budgets (0 = unlimited).
 *
 * Caps how many edges may break and how many bones may sleep in one step /
 * Sleep field application so large collections do not spike Body creation work.
 */
struct EVENGINE_API_DOMAINS DestructionStepBudget {
    int maxEdgeBreaksPerStep = 0;
    int maxSleepsPerStep = 0;
};

/** @brief Observable outcome of one checked step. */
struct EVENGINE_API_DOMAINS DestructionStepReceipt {
    int edgesBroken = 0;
    int edgesDeferred = 0;
    int clusterBreaks = 0;
};

/**
 * @brief Presentation snapshot for one bone (world pose + proxy extents).
 *
 * Owning value for the current synchronous call. Pose comes from the live
 * Body3D; extents come from the instance-owned proxy definition.
 */
struct EVENGINE_API_DOMAINS BonePresentation {
    BoneRuntimeState state = BoneRuntimeState::Attached;
    float halfExtentX = 0.5f;
    float halfExtentY = 0.5f;
    float halfExtentZ = 0.5f;
    float worldX = 0.f;
    float worldY = 0.f;
    float worldZ = 0.f;
    float rotX = 0.f;
    float rotY = 0.f;
    float rotZ = 0.f;
    float rotW = 1.f;
    int clusterId = 0;
    int fractureLevel = 0;
};

/**
 * @brief Runtime owner of bone state, connection strain, and PhysicsLink bindings.
 *
 * @ownership Owns Body3D objects it creates; never retains raw World3D or Body3D
 * pointers across calls. Borrows World3D only through a process-local handle.
 * @lifetime Instance-first teardown destroys owned bodies then clears links;
 * world-first teardown makes later resolve/step/applyField return StaleHandle.
 * Either order leaves PhysicsLinks resolvable without use-after-free.
 */
class EVENGINE_API_DOMAINS GeometryCollectionInstance {
public:
    GeometryCollectionInstance(const GeometryCollectionInstance&)            = delete;
    GeometryCollectionInstance& operator=(const GeometryCollectionInstance&) = delete;
    ~GeometryCollectionInstance();

    /**
     * @brief Create bodies for every bone and bind PhysicsLinks.
     * @param world Borrowed World3D that must outlive successful physics writes.
     * @param asset Immutable bone/edge definition (copied).
     * @param originX Origin of the collection in world metres.
     * @param originY Origin of the collection in world metres.
     * @param originZ Origin of the collection in world metres.
     * @return Owning instance, or a structured failure without partial bodies.
     * @ownership Caller owns the returned unique_ptr; Instance owns created bodies.
     * @lifetime World must remain live for physics writes until releaseBodies or
     * Instance destruction; world destruction first yields StaleHandle.
     */
    [[nodiscard("check geometry-collection instantiation")]]
    static eve::Result<std::unique_ptr<GeometryCollectionInstance>> create(World3D& world,
                                                                            const GeometryCollectionAsset& asset,
                                                                            float originX, float originY,
                                                                            float originZ);

    /** @brief Whether the owning world handle still resolves. */
    [[nodiscard]] bool hasLiveWorld() const noexcept;

    /** @brief Replace the per-step break/sleep budgets (0 = unlimited). */
    void setStepBudget(DestructionStepBudget budget) noexcept { budget_ = budget; }
    /** @brief Return the active per-step budgets. */
    [[nodiscard]] DestructionStepBudget stepBudget() const noexcept { return budget_; }

    /** @brief Apply one destruction field; failure leaves instance unchanged. */
    [[nodiscard("check field application")]]
    eve::Result<FieldApplicationReceipt> applyField(const DestructionField& field);

    /**
     * @brief Advance connection-graph breaks under the configured budget.
     * @param step Injected simulation tick/delta; wall clock is never read.
     * @return Receipt with broken/deferred edge counts, or a structured failure
     * that leaves topology unchanged.
     */
    [[nodiscard("check geometry-collection step")]] eve::Result<DestructionStepReceipt> step(SimulationStep step);

    [[nodiscard]] int boneCount() const noexcept { return static_cast<int>(bones_.size()); }
    [[nodiscard]] int edgeCount() const noexcept { return static_cast<int>(edges_.size()); }
    [[nodiscard]] BoneRuntimeState boneState(int boneIndex) const;
    [[nodiscard]] int boneClusterId(int boneIndex) const;
    [[nodiscard]] float edgeStrain(int edgeIndex) const;
    /** @brief Whether the edge at index has already broken. */
    [[nodiscard]] bool isEdgeBroken(int edgeIndex) const;
    [[nodiscard]] int detachEventCount() const noexcept { return static_cast<int>(detachEvents_.size()); }
    [[nodiscard]] BoneDetachEvent detachEventAt(int index) const;
    [[nodiscard]] int clusterBreakEventCount() const noexcept {
        return static_cast<int>(clusterBreakEvents_.size());
    }
    [[nodiscard]] ClusterBreakEvent clusterBreakEventAt(int index) const;
    /** @brief Edges still waiting because of the break budget. */
    [[nodiscard]] int pendingEdgeBreakCount() const noexcept { return static_cast<int>(pendingEdgeBreaks_.size()); }

    /** @brief Resolve the PhysicsLink for one bone against the live world. */
    [[nodiscard("check bone physics-link resolution")]] eve::Result<PhysicsLink> boneLink(int boneIndex) const;

    /**
     * @brief Sample one bone's world pose and proxy extents for presentation.
     * @return Owning snapshot, or StaleHandle/InvalidArgument without mutating state.
     */
    [[nodiscard("check bone presentation sampling")]]
    eve::Result<BonePresentation> bonePresentation(int boneIndex) const;

    /** @brief Monotonic revision bumped when any bone enters Sleeping (for batch rebuild). */
    [[nodiscard]] std::uint64_t sleepBatchRevision() const noexcept { return sleepBatchRevision_; }

    /**
     * @brief Capture a process-portable runtime snapshot (no PhysicsLink handles).
     * @ownership Caller owns the returned snapshot value.
     */
    [[nodiscard("check geometry-collection snapshot capture")]]
    eve::Result<GeometryCollectionInstanceSnapshot> captureSnapshot() const;

    /**
     * @brief Restore bone/edge state from a snapshot against the live world.
     * @return Success, or InvalidArgument/StaleHandle without partial mutation.
     */
    [[nodiscard("check geometry-collection snapshot restore")]]
    eve::Result<void> restoreSnapshot(const GeometryCollectionInstanceSnapshot& snapshot);

    /** @brief Destroy owned bodies if the world is still live; always clears links. */
    void releaseBodies();

private:
    struct BoneRuntime {
        BoneRuntimeState state = BoneRuntimeState::Attached;
        bool             anchored = false;
        PhysicsLink      link;
        float            halfExtentX = 0.5f;
        float            halfExtentY = 0.5f;
        float            halfExtentZ = 0.5f;
        int              clusterId = 0;
        int              fractureLevel = 0;
    };
    struct EdgeRuntime {
        int   boneA = 0;
        int   boneB = 0;
        float strainThreshold = 1.f;
        float accumulatedStrain = 0.f;
        bool  broken = false;
    };

    GeometryCollectionInstance(PhysicsWorldHandle worldHandle, float originX, float originY, float originZ);

    [[nodiscard]] World3D* resolveWorld() const noexcept;
    [[nodiscard]] eve::Result<Body3D*> resolveBoneBody(int boneIndex) const;
    [[nodiscard]] float falloffAt(const DestructionField& field, float x, float y, float z) const;
    void breakEdge(int edgeIndex, std::uint64_t tick, DestructionStepReceipt& receipt);
    void destroyOwnedBodies(World3D& world);

    PhysicsWorldHandle worldHandle_ = PhysicsWorldHandle::invalid();
    float originX_ = 0.f;
    float originY_ = 0.f;
    float originZ_ = 0.f;
    std::uint64_t lastTick_ = 0;
    std::uint64_t sleepBatchRevision_ = 0;
    bool orphanedPhysics_ = false;
    DestructionStepBudget budget_{};
    std::vector<BoneRuntime> bones_;
    std::vector<EdgeRuntime> edges_;
    std::vector<BoneDetachEvent> detachEvents_;
    std::vector<ClusterBreakEvent> clusterBreakEvents_;
    std::vector<int> pendingEdgeBreaks_;
};

}  // namespace eve::physics
