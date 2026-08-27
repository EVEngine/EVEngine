#pragma once

/**
 * @file EffectDefinitionRuntime.h
 * @brief Strongly typed effect definition/runtime adapter.
 */

#include "common/definitions/DefinitionRuntime.h"
#include "definitions/Definitions.h"
#include "effects/EffectTypes.h"

namespace eve::effects {

/**
 * @brief Typed mutable lifecycle values for one effect instance.
 *
 * EffectContainer remains the authoritative owner for live effects. This
 * adapter owns a definition-bound candidate and projects it into an existing
 * EffectInstance after successful preparation; it does not create a second
 * dynamic field representation.
 */
struct EffectRuntimeState {
    std::string              stackKey;
    int                      priority   = 0;
    double                   duration   = 0.0;
    double                   remaining  = -1.0;
    double                   magnitude  = 0.0;
    std::uint32_t            stackCount = 1;
    std::uint32_t            maxStacks  = 1;
    EffectPolicy             policy;
    EffectPayload            payload;
    std::vector<std::string> tags;
};

/**
 * @brief Binds a typed effect runtime to a generation-qualified definition.
 *
 * `DefinitionRegistry` and the target `EffectInstance` are borrowed. They
 * must outlive each synchronous call. Reload uses the common four-policy
 * contract and is atomic with respect to this adapter's typed state; callers
 * explicitly call `applyTo` to update the container-owned instance.
 */
class EffectDefinitionRuntime final {
public:
    /**
     * @brief Create a typed effect runtime from a registry definition.
     * @param registry Registry whose lifetime exceeds this adapter.
     * @param definition Logical `effect:<name>` reference.
     * @param subject Non-empty owner/target projection for the EffectInstance.
     * @param source Optional source projection.
     * @param instanceId Non-nil persistent identity for this runtime instance.
     * @param policy Policy used by later reload calls.
     * @return A typed runtime or a structured parse/resolution failure.
     */
    [[nodiscard]] static eve::Result<EffectDefinitionRuntime> create(
        eve::definitions::DefinitionRegistry& registry, eve::DefinitionRef definition, std::string subject,
        std::string source, eve::PersistentId instanceId,
        eve::definition::ReloadPolicy policy = eve::definition::ReloadPolicy::KeepInstanceValues);

    /** @brief Move an effect runtime while retaining its borrowed registry binding. */
    EffectDefinitionRuntime(EffectDefinitionRuntime&&) noexcept = default;
    /** @brief Move-assign an effect runtime. */
    EffectDefinitionRuntime& operator=(EffectDefinitionRuntime&&) noexcept = default;
    /** @brief Effect runtimes are not implicitly copyable. */
    EffectDefinitionRuntime(const EffectDefinitionRuntime&) = delete;
    /** @brief Effect runtimes are not implicitly copy-assignable. */
    EffectDefinitionRuntime& operator=(const EffectDefinitionRuntime&) = delete;
    /** @brief Destroy the effect runtime; the registry remains caller-owned. */
    ~EffectDefinitionRuntime() = default;

    /** @brief Borrow the unified instance identity. */
    [[nodiscard]] const eve::definition::InstanceIdentity& identity() const noexcept;
    /** @brief Borrow the strongly typed effect runtime state. */
    [[nodiscard]] const EffectRuntimeState& state() const noexcept;
    /** @brief Mutate the strongly typed effect runtime state on the owner thread. */
    [[nodiscard]] EffectRuntimeState& state() noexcept;
    /** @brief Borrow the current generation-qualified definition handle. */
    [[nodiscard]] eve::definition::DefinitionHandle definitionHandle() const noexcept;
    /** @brief Return the configured reload policy. */
    [[nodiscard]] eve::definition::ReloadPolicy reloadPolicy() const noexcept { return policy_; }
    /** @brief Mark this effect active/inactive for RejectWhileActive. */
    void setActive(bool active) noexcept;
    /** @brief Return whether the effect is active for reload policy decisions. */
    [[nodiscard]] bool isActive() const noexcept;

    /**
     * @brief Atomically project the typed runtime into an EffectInstance.
     * @param effect Borrowed container-owned instance; it is not retained.
     * @return Success, or InvalidArgument for a null destination.
     */
    [[nodiscard]] eve::Result<void> applyTo(EffectInstance* effect) const;

    /**
     * @brief Resolve the registry's current generation and execute a policy.
     * @return Reload outcome, or a failure leaving typed state and identity unchanged.
     */
    [[nodiscard]] eve::Result<eve::definition::ReloadOutcome> reload(eve::definition::ReloadPolicy policy);

private:
    EffectDefinitionRuntime(eve::definitions::DefinitionRegistry&                registry,
                            eve::definition::RuntimeInstance<EffectRuntimeState> runtime, std::string subject,
                            std::string source, eve::definition::ReloadPolicy policy)
        : registry_(&registry),
          runtime_(std::move(runtime)),
          subject_(std::move(subject)),
          source_(std::move(source)),
          policy_(policy) {}

    eve::definitions::DefinitionRegistry*                registry_ = nullptr;  // borrowed
    eve::definition::RuntimeInstance<EffectRuntimeState> runtime_;
    std::string                                          subject_;
    std::string                                          source_;
    eve::definition::ReloadPolicy                        policy_;
};

}  // namespace eve::effects
