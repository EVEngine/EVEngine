#pragma once

/**
 * @file WeaponDefinitionRuntime.h
 * @brief Strongly typed Weapon definition/runtime adapter.
 *
 * WeaponDefinition remains the domain-owned definition type. The common
 * DefinitionRuntime owns only the per-instance mutable state and its
 * generation-qualified definition identity, so this adapter does not create
 * a second dynamic field table.
 */

#include "common/Snapshot.h"
#include "common/definitions/DefinitionRuntime.h"
#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"
#include "weapon/WeaponTypes.h"

#include <string>
#include <string_view>

namespace eve::weapon {

class Weapon;

/**
 * @brief Parse one canonical registry definition into an owning typed snapshot.
 * @param source Immutable definition owned by DefinitionRegistry.
 * @return A value-owned projection for one consumer, or a structured parse failure.
 * @remarks The returned value is independent of the registry and does not form a second store.
 */
[[nodiscard]] eve::Result<WeaponDefinition> parseWeaponDefinition(
    const eve::definitions::Definition& source);

/**
 * @brief Mutable state owned by one typed weapon runtime instance.
 *
 * The target handle, staged request and shared ammo-pool pointer are transient
 * ECS links and are intentionally not persisted here. The owning WeaponEntity
 * remains responsible for those links; the stable firing/resource state below
 * is the snapshot contract.
 */
struct WeaponRuntimeState {
    Resource resource;
    float cooldown = 0.f;
    int burstRemaining = 0;
    float burstTimer = 0.f;
    bool jammed = false;
    AttackStage stage = AttackStage::Idle;
    float stageTimer = 0.f;
    float currentSpread = 0.f;
    float recoilPitch = 0.f;
    float recoilYaw = 0.f;
    FireMode selector = FireMode::Single;
    bool aiming = false;
};

/**
 * @brief Binds a typed WeaponDefinition to one mutable runtime instance.
 *
 * The registry is borrowed and must outlive this adapter. All calls are
 * synchronous and owner-thread affine. `RebuildInstance` is the default
 * policy: changing a definition preserves compatible firing state while
 * clamping it to the new typed limits. `RejectWhileActive` rejects replacement
 * while the instance is marked active.
 */
class WeaponDefinitionRuntime final {
public:
    /**
     * @brief Create from a common `weapon:<name>` definition reference.
     * @param registry Borrowed registry containing the definition.
     * @param definition Logical weapon definition reference.
     * @param instanceId Non-nil persistent identity for this weapon instance.
     * @param policy Policy used by subsequent reload calls.
     * @return A typed adapter or a structured resolution/parse failure.
     */
    [[nodiscard]] static eve::Result<WeaponDefinitionRuntime> create(
        eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition,
        eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    /** @brief Create from a `weapon:<definitionId>` logical name. */
    [[nodiscard]] static eve::Result<WeaponDefinitionRuntime> create(
        eve::definitions::DefinitionRegistry& registry, std::string_view definitionId,
        eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    /** @brief Create through the existing Weapon module facade. */
    [[nodiscard]] static eve::Result<WeaponDefinitionRuntime> create(
        Weapon& module, std::string_view definitionId, eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    WeaponDefinitionRuntime(WeaponDefinitionRuntime&&) noexcept = default;
    WeaponDefinitionRuntime& operator=(WeaponDefinitionRuntime&&) noexcept = default;
    WeaponDefinitionRuntime(const WeaponDefinitionRuntime&) = delete;
    WeaponDefinitionRuntime& operator=(const WeaponDefinitionRuntime&) = delete;
    ~WeaponDefinitionRuntime() = default;

    /** @brief Borrow the common instance identity and exact definition generation. */
    [[nodiscard]] const eve::definition::InstanceIdentity& identity() const noexcept;
    /** @brief Borrow mutable typed firing/resource state. */
    [[nodiscard]] const WeaponRuntimeState& state() const noexcept;
    /** @brief Mutate typed firing/resource state on the owner thread. */
    [[nodiscard]] WeaponRuntimeState& state() noexcept;
    /** @brief Return the exact generation-qualified definition handle. */
    [[nodiscard]] eve::definition::DefinitionHandle definitionHandle() const noexcept;
    /** @brief Resolve the current strongly typed WeaponDefinition projection. */
    [[nodiscard]] eve::Result<WeaponDefinition> definition() const;
    /** @brief Return the policy used by the next reload call. */
    [[nodiscard]] eve::definition::ReloadPolicy reloadPolicy() const noexcept { return policy_; }
    /** @brief Set the active guard used by RejectWhileActive. */
    void setActive(bool active) noexcept;
    /** @brief Return whether the instance is active. */
    [[nodiscard]] bool isActive() const noexcept;

    /**
     * @brief Atomically project this runtime into a borrowed WeaponEntity.
     * @param entity Borrowed ECS entity. The adapter does not retain it.
     * @ownership Borrowed; ECS owns the entity and its components.
     * @lifetime The pointer is used only during this synchronous call.
     * @thread Must be called on the entity's owner thread.
     * @return Success or a structured projection failure. Failure leaves the
     *         entity unchanged.
     */
    [[nodiscard]] eve::Result<void> applyTo(WeaponEntity* entity) const;

    /** @brief Reload the registry's current generation using a typed policy. */
    [[nodiscard]] eve::Result<eve::definition::ReloadOutcome> reload(
        eve::definition::ReloadPolicy policy);

    /** @brief Capture stable typed runtime state in the common SnapshotEnvelope. */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        eve::Revision revision, eve::SimulationTick tick,
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Serialize snapshot() as canonical deterministic JSON. */
    [[nodiscard]] eve::Result<std::string> snapshotJson(
        eve::Revision revision, eve::SimulationTick tick,
        const eve::SnapshotHashProvider& hashProvider) const;
    /**
     * @brief Verify and restore a same-generation snapshot transactionally.
     * @return Failure leaves all typed runtime state unchanged.
     */
    [[nodiscard]] eve::Result<void> restore(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);

private:
    WeaponDefinitionRuntime(eve::definitions::DefinitionRegistry& registry,
                            eve::definition::RuntimeInstance<WeaponRuntimeState> runtime,
                            eve::definition::ReloadPolicy policy)
        : registry_(&registry), runtime_(std::move(runtime)), policy_(policy) {}

    eve::definitions::DefinitionRegistry* registry_ = nullptr;  // borrowed
    eve::definition::RuntimeInstance<WeaponRuntimeState> runtime_;
    eve::definition::ReloadPolicy policy_ = eve::definition::ReloadPolicy::RebuildInstance;
};

}  // namespace eve::weapon
