#pragma once

/**
 * @file SkillDefinitionRuntime.h
 * @brief Strongly typed runtime adapter for RPG skill definitions.
 *
 * The definition itself remains in the common DefinitionRegistry. This
 * adapter owns only per-instance state (cooldown and learned flag), so a
 * snapshot never duplicates a definition field table.
 */

#include "common/Snapshot.h"
#include "common/definitions/DefinitionRuntime.h"
#include "common/definitions/RuntimeSnapshot.h"
#include "definitions/Definitions.h"
#include "rpg/Skill.h"

#include <string>
#include <string_view>

namespace eve::rpg {

class RPGActor;

/** @brief Mutable state belonging to one learned skill instance. */
struct SkillRuntimeState {
    float cooldownRemaining = 0.f;
    bool  learned           = true;
};

/**
 * @brief Typed SkillDefinition-to-runtime adapter.
 *
 * The registry is borrowed and must outlive this object. All methods are
 * synchronous and owner-thread affine. `RebuildInstance` is the default
 * policy: it adopts the new typed definition while retaining a valid
 * cooldown/learned state. `RejectWhileActive` uses the adapter active flag;
 * callers set it while a cast session owns the skill.
 */
class SkillDefinitionRuntime final {
public:
    /**
     * @brief Create a runtime from a generation-qualified common definition.
     * @param registry Borrowed registry containing a `rpg.skill:<name>` entry.
     * @param definition Logical skill definition reference.
     * @param instanceId Non-nil persistent identity of this learned skill.
     * @param policy Reload policy used by subsequent reload calls.
     * @return A typed adapter or a structured resolution/validation failure.
     */
    [[nodiscard]] static eve::Result<SkillDefinitionRuntime> create(
        eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition, eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    /** @brief Create from the canonical legacy SkillRegistry facade. */
    [[nodiscard]] static eve::Result<SkillDefinitionRuntime> create(
        eve::PersistentId instanceId, std::string_view skillId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::RebuildInstance);

    SkillDefinitionRuntime(SkillDefinitionRuntime&&) noexcept            = default;
    SkillDefinitionRuntime& operator=(SkillDefinitionRuntime&&) noexcept = default;
    SkillDefinitionRuntime(const SkillDefinitionRuntime&)                = delete;
    SkillDefinitionRuntime& operator=(const SkillDefinitionRuntime&)     = delete;
    ~SkillDefinitionRuntime()                                            = default;

    /** @brief Borrow the common identity, including exact definition generation. */
    [[nodiscard]] const eve::definition::InstanceIdentity& identity() const noexcept;
    /** @brief Borrow mutable per-instance skill state. */
    [[nodiscard]] const SkillRuntimeState& state() const noexcept;
    /** @brief Mutate per-instance state on the owner thread. */
    [[nodiscard]] SkillRuntimeState& state() noexcept;
    /** @brief Return the exact generation-qualified definition handle. */
    [[nodiscard]] eve::definition::DefinitionHandle definitionHandle() const noexcept;
    /** @brief Resolve and parse the current typed definition projection. */
    [[nodiscard]] eve::Result<SkillDefinition> definition() const;
    /** @brief Return the configured reload policy. */
    [[nodiscard]] eve::definition::ReloadPolicy reloadPolicy() const noexcept { return policy_; }
    /** @brief Set the active-cast guard used by RejectWhileActive. */
    void setActive(bool active) noexcept;
    /** @brief Return whether this skill is currently active. */
    [[nodiscard]] bool isActive() const noexcept;

    /**
     * @brief Project this skill instance into an RPGActor's Skills component.
     * @param actor Borrowed ECS actor; no pointer is retained.
     * @return Success or InvalidArgument; projection is committed as one
     *         component swap.
     */
    [[nodiscard]] eve::Result<void> applyTo(RPGActor* actor) const;

    /** @brief Reload the current registry generation using a typed policy. */
    [[nodiscard]] eve::Result<eve::definition::ReloadOutcome> reload(eve::definition::ReloadPolicy policy);

    /**
     * @brief Capture this skill's instance state in a common SnapshotEnvelope.
     * @param revision Domain revision associated with the capture.
     * @param tick Deterministic simulation tick associated with the capture.
     * @param hashProvider Explicit content digest provider.
     * @return A sealed snapshot or a structured failure.
     */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(eve::Revision revision, eve::SimulationTick tick,
                                                              const eve::SnapshotHashProvider& hashProvider) const;

    /** @brief Serialize snapshot() as canonical JSON. */
    [[nodiscard]] eve::Result<std::string> snapshotJson(eve::Revision revision, eve::SimulationTick tick,
                                                        const eve::SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Verify and restore a same-generation skill snapshot atomically.
     * @return Success, or schema/hash/definition/stale/decode failure; state
     *         remains unchanged on every failure.
     */
    [[nodiscard]] eve::Result<void> restore(const eve::SnapshotEnvelope&     snapshot,
                                            const eve::SnapshotHashProvider& hashProvider);

private:
    SkillDefinitionRuntime(eve::definitions::DefinitionRegistry&               registry,
                           eve::definition::RuntimeInstance<SkillRuntimeState> runtime,
                           eve::definition::ReloadPolicy                       policy)
        : registry_(&registry), runtime_(std::move(runtime)), policy_(policy) {}

    eve::definitions::DefinitionRegistry*               registry_ = nullptr;  // borrowed
    eve::definition::RuntimeInstance<SkillRuntimeState> runtime_;
    eve::definition::ReloadPolicy                       policy_ = eve::definition::ReloadPolicy::RebuildInstance;
};

}  // namespace eve::rpg
