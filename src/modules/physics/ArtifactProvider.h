#pragma once

/**
 * @file ArtifactProvider.h
 * @brief Physics-owned CPU collider provider for generated artifacts.
 */

#include "common/ArtifactPublication.h"
#include "common/RuntimeHandle.h"
#include "physics/PhysicsHandles.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::physics {

class Body3D;
class Shape3D;
class World3D;

/** @brief Owner tag for physics artifact runtime handles. */
struct PhysicsArtifactHandleTag {};
/** @brief Generation-qualified handle for a physics artifact collider. */
using PhysicsArtifactHandle = eve::RuntimeHandle<PhysicsArtifactHandleTag>;

/** @brief Backend-neutral ray-query result used for parity tests. */
struct PhysicsArtifactRayHit {
    bool  hit      = false;
    float fraction = 0.f;
    float x        = 0.f;
    float y        = 0.f;
    float z        = 0.f;
    float normalX  = 0.f;
    float normalY  = 0.f;
    float normalZ  = 0.f;
};

/** @brief Queryable CPU collider resource owned by the physics module. */
struct PhysicsArtifactCollider {
    eve::PersistentId     id;
    std::string           buildKey;
    PhysicsArtifactHandle handle;
    /** @brief Shared or compatibility-world identity that owns the live collider. */
    PhysicsWorldHandle    world = PhysicsWorldHandle::invalid();
    std::string           shape;
    /** @brief Backend family that owns the live shape, normally `box3d`. */
    std::string                backend = "box3d";
    eve::artifact::Bounds      bounds;
    std::vector<float>         vertices;
    std::vector<std::uint32_t> indices;
};

/**
 * @brief Real physics-module provider for generated collider publication.
 *
 * It owns the backend-neutral descriptor plus a real static Box3D body and
 * triangle-mesh shape for every committed collider. By default each collider
 * keeps its historical private compatibility world; bindWorld() instead puts
 * future colliders in one caller-owned simulation authority. Procgen never
 * includes Box3D; the backend remains an implementation detail of this module.
 */
class PhysicsArtifactProvider final : public eve::artifact::IPhysicsArtifactAdapter {
public:
    /** @brief Construct an empty provider; all methods use the owner thread. */
    PhysicsArtifactProvider();
    /** @brief Release all staged/committed Box3D resources. */
    ~PhysicsArtifactProvider() override;

    /** @brief Stage and copy a collider leaf from a generated publication. */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> prepare(
        const eve::artifact::PublicationView& publication) override;
    /** @brief Export collider identity, build keys, handles and CPU geometry. */
    [[nodiscard]] eve::Result<eve::Value> snapshotState() const override;
    /** @brief Replace colliders from validated backend-neutral CPU geometry. */
    [[nodiscard]] eve::Result<void> restoreState(const eve::Value& state) override;
    /** @brief Return whether the physics registry has no committed colliders. */
    [[nodiscard]] bool emptyState() const noexcept override;
    /** @brief Clear provider state through the common restore-cleanup hook. */
    void clearState() noexcept override { clear(); }

    /**
     * @brief Find a committed collider by persistent identity.
     * @return Borrowed nullable collider owned by this provider.
     * @ownership PhysicsArtifactProvider owns committed collider records; callers must not delete or mutate the result.
     * @lifetime Valid until clear(), restoreState(), or provider destruction; retain PhysicsArtifactHandle across
     * frames.
     * @thread Call on the physics provider's owning thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across provider mutation.
     */
    [[nodiscard]] const PhysicsArtifactCollider* find(eve::PersistentId id) const noexcept;
    /**
     * @brief Resolve a live artifact handle.
     * @return Borrowed nullable collider owned by this provider; null denotes a stale or foreign handle.
     * @ownership PhysicsArtifactProvider owns the collider; callers must not delete or mutate the result.
     * @lifetime Valid until provider mutation or destruction; use the handle for later resolution.
     * @thread Call on the physics provider's owning thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across provider mutation.
     */
    [[nodiscard]] const PhysicsArtifactCollider* find(PhysicsArtifactHandle handle) const noexcept;
    /** @brief Return whether a generation-qualified artifact handle is live. */
    [[nodiscard]] bool isHandleLive(PhysicsArtifactHandle handle) const noexcept;
    /** @brief Ray query one collider through its live Box3D world. */
    [[nodiscard]] PhysicsArtifactRayHit rayCast(eve::PersistentId id, float ox, float oy, float oz, float dx, float dy,
                                                float dz) const noexcept;
    /** @brief Return the number of committed physics colliders. */
    [[nodiscard]] std::size_t size() const noexcept;
    /** @brief Return the backend family for a committed collider, or empty. */
    [[nodiscard]] std::string backendName(eve::PersistentId id) const;
    /** @brief Return whether a collider currently owns a valid Box3D shape. */
    [[nodiscard]] bool isBox3DBacked(eve::PersistentId id) const noexcept;
    /**
     * @brief Bind future collider publications to a caller-owned shared world.
     * @param world Live world borrowed until unbindWorld() or provider destruction.
     * @return Applied, NoOp for the same target, or Conflict while state/stages exist.
     * @ownership The caller retains the world; the provider owns and removes only the bodies and shapes it prepares.
     * @lifetime The caller must keep the world alive through every staged transaction. World destruction after commit
     * makes published runtime colliders stale; clear() safely releases their invalidated wrappers.
     * @note prepare() reserves a disabled body in this world. It cannot participate in simulation or queries before
     * commit(); rollback() removes it. Diagnostic body counts may include this disabled reservation while staged.
     * @thread Owner-thread only; publication and world stepping must not run concurrently.
     * @reentrancy Invokes no callbacks.
     */
    [[nodiscard]] eve::Result<void> bindWorld(World3D& world);
    /**
     * @brief Remove an empty provider's matching shared-world binding.
     * @param world Expected generation-qualified world identity.
     * @return Applied, NotFound for a different/stale binding, or Conflict while publications exist.
     */
    [[nodiscard]] eve::Result<void> unbindWorld(PhysicsWorldHandle world);
    /** @brief Return the live shared-world binding, or invalid for compatibility-private mode/stale binding. */
    [[nodiscard]] PhysicsWorldHandle boundWorld() const noexcept;
    /** @brief Return whether the collider resolves to the currently live shared world binding. */
    [[nodiscard]] bool isSharedWorldBacked(eve::PersistentId id) const noexcept;
    /** @brief Remove all colliders while retiring their runtime handle slots; the world binding is retained. */
    void clear() noexcept;
    /** @brief Inject a prepare failure for composition tests. */
    void setPrepareFailure(bool enabled) noexcept { failPrepare_ = enabled; }

private:
    struct State;
    friend class PhysicsArtifactStage;
    void commit(PhysicsArtifactCollider collider, std::unique_ptr<World3D> world, std::unique_ptr<Body3D> body,
                std::unique_ptr<Shape3D> shape) noexcept;
    void release(PhysicsArtifactCollider& collider, std::unique_ptr<World3D>& world, std::unique_ptr<Body3D>& body,
                 std::unique_ptr<Shape3D>& shape) noexcept;
    std::unique_ptr<State> state_;
    std::uint32_t          nextIndex_   = 0;
    bool                   failPrepare_ = false;
    World3D*               boundWorld_  = nullptr;
    PhysicsWorldHandle     boundWorldHandle_ = PhysicsWorldHandle::invalid();
    std::weak_ptr<const void> boundWorldLifetime_;
    std::shared_ptr<const void> providerLifetime_ = std::make_shared<int>(0);
};

/** @brief Return the process-owned physics artifact provider singleton. */
[[nodiscard]] PhysicsArtifactProvider& physicsArtifactProvider() noexcept;
/** @brief Register the physics provider in the common capability registry. */
void registerPhysicsArtifactProvider();

}  // namespace eve::physics
