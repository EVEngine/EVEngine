#pragma once

/**
 * @file VehicleDefinitionRuntime.h
 * @brief Strongly typed Vehicle definition/entity runtime adapter.
 *
 * VehicleDefinition remains an explicit domain type. The common runtime
 * stores only mutable entity state and the generation-qualified reference;
 * definition data is resolved from DefinitionRegistry at the projection
 * boundary.
 */

#include "common/Snapshot.h"
#include "common/definitions/DefinitionRuntime.h"
#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"
#include "vehicle/VehicleTypes.h"

#include <string>
#include <string_view>

namespace eve::vehicle {

class Vehicle;

/**
 * @brief Parse one canonical registry definition into a short-lived typed projection.
 * @param source Immutable definition owned by DefinitionRegistry.
 * @return A typed projection for one synchronous consumer, or a structured parse failure.
 * @remarks The returned value is not an additional definition store. Callers that need to
 *          retain the projection must own a copy for their runtime instance.
 */
[[nodiscard]] eve::Result<VehicleDefinition> parseVehicleDefinition(const eve::definitions::Definition& source);

/** @brief Mutable state owned by one typed vehicle runtime instance. */
struct VehicleRuntimeState {
    float       x       = 0.f;
    float       y       = 0.f;
    float       heading = 0.f;
    float       speed   = 0.f;
    float       health  = 0.f;
    std::string faction;
    bool        destroyed = false;
};

/**
 * @brief Binds a VehicleDefinition to a VehicleEntity without a new root type.
 *
 * The registry is borrowed and must outlive synchronous calls. `applyTo`
 * installs an immutable definition projection into the entity component and
 * writes the mutable ECS components in one candidate swap. Vehicle physics,
 * weapons and orders remain links/components owned by their own domains.
 */
class VehicleDefinitionRuntime final {
public:
    /**
     * @brief Create from a common `vehicle:<name>` definition.
     * @return A typed adapter or a structured parse/resolution failure.
     */
    [[nodiscard]] static eve::Result<VehicleDefinitionRuntime> create(
        eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition, eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    /** @brief Create from a Vehicle module's canonical definition registry. */
    [[nodiscard]] static eve::Result<VehicleDefinitionRuntime> create(
        eve::definitions::DefinitionRegistry& registry, std::string_view definitionId, eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    /** @brief Create through the existing Vehicle module facade. */
    [[nodiscard]] static eve::Result<VehicleDefinitionRuntime> create(
        Vehicle& module, std::string_view definitionId, eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    VehicleDefinitionRuntime(VehicleDefinitionRuntime&&) noexcept            = default;
    VehicleDefinitionRuntime& operator=(VehicleDefinitionRuntime&&) noexcept = default;
    VehicleDefinitionRuntime(const VehicleDefinitionRuntime&)                = delete;
    VehicleDefinitionRuntime& operator=(const VehicleDefinitionRuntime&)     = delete;
    ~VehicleDefinitionRuntime()                                              = default;

    /** @brief Borrow common instance identity and exact definition generation. */
    [[nodiscard]] const eve::definition::InstanceIdentity& identity() const noexcept;
    /** @brief Borrow mutable vehicle entity state. */
    [[nodiscard]] const VehicleRuntimeState& state() const noexcept;
    /** @brief Mutate vehicle state on the owner thread. */
    [[nodiscard]] VehicleRuntimeState& state() noexcept;
    /** @brief Return the generation-qualified definition handle. */
    [[nodiscard]] eve::definition::DefinitionHandle definitionHandle() const noexcept;
    /** @brief Resolve and parse the current strongly typed VehicleDefinition. */
    [[nodiscard]] eve::Result<VehicleDefinition> definition() const;
    /** @brief Return the reload policy used by the next reload. */
    [[nodiscard]] eve::definition::ReloadPolicy reloadPolicy() const noexcept { return policy_; }
    /** @brief Set the active guard used by RejectWhileActive. */
    void setActive(bool active) noexcept;
    /** @brief Return the active guard state. */
    [[nodiscard]] bool isActive() const noexcept;

    /**
     * @brief Project typed state and identity into a borrowed VehicleEntity.
     * @return Success or a structured projection failure; no state is changed
     *         when definition parsing or allocation fails.
     */
    [[nodiscard]] eve::Result<void> applyTo(VehicleEntity* entity) const;

    /** @brief Reload the registry's current generation using a typed policy. */
    [[nodiscard]] eve::Result<eve::definition::ReloadOutcome> reload(eve::definition::ReloadPolicy policy);

    /** @brief Capture mutable entity state in a common SnapshotEnvelope. */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(eve::Revision revision, eve::SimulationTick tick,
                                                              const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Serialize snapshot() as canonical deterministic JSON. */
    [[nodiscard]] eve::Result<std::string> snapshotJson(eve::Revision revision, eve::SimulationTick tick,
                                                        const eve::SnapshotHashProvider& hashProvider) const;
    /**
     * @brief Verify and restore a same-generation vehicle snapshot atomically.
     * @return Failure leaves all mutable runtime state unchanged.
     */
    [[nodiscard]] eve::Result<void> restore(const eve::SnapshotEnvelope&     snapshot,
                                            const eve::SnapshotHashProvider& hashProvider);

private:
    VehicleDefinitionRuntime(eve::definitions::DefinitionRegistry&                 registry,
                             eve::definition::RuntimeInstance<VehicleRuntimeState> runtime,
                             eve::definition::ReloadPolicy                         policy)
        : registry_(&registry), runtime_(std::move(runtime)), policy_(policy) {}

    eve::definitions::DefinitionRegistry*                 registry_ = nullptr;  // borrowed
    eve::definition::RuntimeInstance<VehicleRuntimeState> runtime_;
    eve::definition::ReloadPolicy                         policy_ = eve::definition::ReloadPolicy::RebuildInstance;
};

}  // namespace eve::vehicle
