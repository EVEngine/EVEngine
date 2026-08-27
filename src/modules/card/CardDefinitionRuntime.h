#pragma once

/**
 * @file CardDefinitionRuntime.h
 * @brief Strongly typed CardDefinition to CardData runtime adapter.
 */

#include "card/CardTypes.h"
#include "common/Snapshot.h"
#include "common/definitions/DefinitionRuntime.h"
#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"

#include <string>
#include <vector>

namespace eve::card {

/**
 * @brief Typed mutable values owned by one card definition instance.
 *
 * This is deliberately a card-domain structure rather than a generic field
 * table. `currentHealth` is runtime state; the other members are the typed
 * definition projection used to initialize or rebuild a CardData entity.
 */
struct CardRuntimeState {
    std::string name;
    std::string kind = "creature";
    int cost = 0;
    int attack = 0;
    int maxHealth = 0;
    int currentHealth = 0;
    glm::vec3 tint{0.62f, 0.50f, 0.40f};
    std::vector<std::string> tags;
};

/**
 * @brief Binds a typed card runtime to one Definitions registry incarnation.
 *
 * The registry is borrowed and must outlive this adapter. `CardData` remains
 * the ECS/presentation object; this adapter owns the definition-bound typed
 * state and projects it into CardData through `applyTo`. All methods are
 * owner-thread affine and synchronous. Reload prepares the complete typed
 * state before changing its identity; a failure leaves both unchanged.
 */
class CardDefinitionRuntime final {
public:
    /**
     * @brief Create a card runtime from the current typed definition.
     * @param registry Registry whose lifetime exceeds this adapter.
     * @param definition Logical `card:<name>` reference.
     * @param instanceId Non-nil persistent identity for this card instance.
     * @return A typed runtime or a structured resolution/parse failure.
     */
    [[nodiscard]] static eve::Result<CardDefinitionRuntime> create(
        eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition,
        eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::KeepInstanceValues);

    /** @brief Move a card runtime while retaining its borrowed registry binding. */
    CardDefinitionRuntime(CardDefinitionRuntime&&) noexcept = default;
    /** @brief Move-assign a card runtime. */
    CardDefinitionRuntime& operator=(CardDefinitionRuntime&&) noexcept = default;
    /** @brief Card runtimes are not implicitly copyable. */
    CardDefinitionRuntime(const CardDefinitionRuntime&) = delete;
    /** @brief Card runtimes are not implicitly copy-assignable. */
    CardDefinitionRuntime& operator=(const CardDefinitionRuntime&) = delete;
    /** @brief Destroy the card runtime; the registry remains caller-owned. */
    ~CardDefinitionRuntime() = default;

    /** @brief Borrow the unified instance identity. */
    [[nodiscard]] const eve::definition::InstanceIdentity& identity() const noexcept;
    /** @brief Borrow the strongly typed card runtime state. */
    [[nodiscard]] const CardRuntimeState& state() const noexcept;
    /** @brief Mutate the strongly typed card runtime state on the owner thread. */
    [[nodiscard]] CardRuntimeState& state() noexcept;
    /** @brief Borrow the exact generation-qualified definition handle. */
    [[nodiscard]] eve::definition::DefinitionHandle definitionHandle() const noexcept;
    /** @brief Return the policy used by the next reload call. */
    [[nodiscard]] eve::definition::ReloadPolicy reloadPolicy() const noexcept { return policy_; }
    /** @brief Mark the instance active/inactive for RejectWhileActive. */
    void setActive(bool active) noexcept;
    /** @brief Return whether RejectWhileActive currently treats the instance as active. */
    [[nodiscard]] bool isActive() const noexcept;

    /**
     * @brief Apply the typed state and common identity to an ECS CardData.
     * @param card Borrowed CardData; it remains owned by ECS.
     * @return Success, or InvalidArgument for a null card.
     */
    [[nodiscard]] eve::Result<void> applyTo(CardData* card) const;

    /**
     * @brief Reload from the registry's current generation and execute policy.
     * @param policy Policy to execute for this card instance.
     * @return A typed reload outcome, or a stale/parse/policy failure. On
     *         failure the adapter state and generation remain unchanged.
     */
    [[nodiscard]] eve::Result<eve::definition::ReloadOutcome> reload(
        eve::definition::ReloadPolicy policy);

    /** @brief Capture card instance state in the common SnapshotEnvelope. */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        eve::Revision revision, eve::SimulationTick tick,
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Serialize snapshot() as canonical JSON. */
    [[nodiscard]] eve::Result<std::string> snapshotJson(
        eve::Revision revision, eve::SimulationTick tick,
        const eve::SnapshotHashProvider& hashProvider) const;
    /** @brief Verify and atomically restore a same-generation card snapshot. */
    [[nodiscard]] eve::Result<void> restore(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);

private:
    CardDefinitionRuntime(eve::definitions::DefinitionRegistry& registry,
                          eve::definition::RuntimeInstance<CardRuntimeState> runtime,
                          eve::definition::ReloadPolicy policy)
        : registry_(&registry), runtime_(std::move(runtime)), policy_(policy) {}

    eve::definitions::DefinitionRegistry* registry_ = nullptr;  // borrowed
    eve::definition::RuntimeInstance<CardRuntimeState> runtime_;
    eve::definition::ReloadPolicy policy_;
};

}  // namespace eve::card
