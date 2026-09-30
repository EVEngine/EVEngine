#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "common/Time.h"
#include "physics/PhysicsLink.h"
#include "physics/destruction/DestructionField.h"
#include "physics/destruction/GeometryCollectionAsset.h"

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
};

/**
 * @brief Runtime owner of bone state, connection strain, and PhysicsLink bindings.
 *
 * Borrows a World3D through a process-local world handle. Instance owns the
 * Body3D objects it creates and destroys them on teardown. Either destruction
 * order (instance first or world first) leaves links resolvable as StaleHandle.
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
     */
    [[nodiscard("check geometry-collection instantiation")]]
    static eve::Result<std::unique_ptr<GeometryCollectionInstance>> create(World3D& world,
                                                                            const GeometryCollectionAsset& asset,
                                                                            float originX, float originY,
                                                                            float originZ);

    /** @brief Whether the owning world handle still resolves. */
    [[nodiscard]] bool hasLiveWorld() const noexcept;

    /** @brief Apply one destruction field; failure leaves instance unchanged. */
    [[nodiscard("check field application")]]
    eve::Result<FieldApplicationReceipt> applyField(const DestructionField& field);

    /**
     * @brief Advance connection-graph breaks and sleep requests.
     * @param step Injected simulation tick/delta; wall clock is never read.
     */
    [[nodiscard("check geometry-collection step")]] eve::Result<void> step(SimulationStep step);

    [[nodiscard]] int boneCount() const noexcept { return static_cast<int>(bones_.size()); }
    [[nodiscard]] int edgeCount() const noexcept { return static_cast<int>(edges_.size()); }
    [[nodiscard]] BoneRuntimeState boneState(int boneIndex) const;
    [[nodiscard]] float edgeStrain(int edgeIndex) const;
    [[nodiscard]] bool edgeBroken(int edgeIndex) const;
    [[nodiscard]] int detachEventCount() const noexcept { return static_cast<int>(detachEvents_.size()); }
    [[nodiscard]] BoneDetachEvent detachEventAt(int index) const;

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
    void breakEdge(int edgeIndex, std::uint64_t tick);
    void destroyOwnedBodies(World3D& world);

    PhysicsWorldHandle worldHandle_ = PhysicsWorldHandle::invalid();
    float originX_ = 0.f;
    float originY_ = 0.f;
    float originZ_ = 0.f;
    std::uint64_t lastTick_ = 0;
    std::uint64_t sleepBatchRevision_ = 0;
    bool orphanedPhysics_ = false;
    std::vector<BoneRuntime> bones_;
    std::vector<EdgeRuntime> edges_;
    std::vector<BoneDetachEvent> detachEvents_;
};

}  // namespace eve::physics
