#pragma once
#include "common/Export.h"


/** @file AbilityAsset.h @brief Versioned persistent codec and timeline assembly for ability definitions. */

#include "action/AbilitySystem.h"
#include "action/ActionTimeline.h"
#include "common/SchemaVersion.h"

#include <string_view>

namespace eve::action {

inline constexpr std::string_view kAbilityAssetSchemaId = "eve.action.ability";
inline constexpr std::uint64_t    kAbilityAssetSchemaVersion = 2;

/**
 * @brief Encode one validated ability definition as canonical schema version 2 data.
 * @param definition Owning definition borrowed only for this synchronous call.
 * @return Independently owning Value, or a structured validation/encoding failure.
 * @remarks Owner-thread-only and reentrant; no callbacks, registries, files, or clocks are used.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<Value> encodeAbilityAsset(const AbilityDefinition& definition);

/**
 * @brief Decode schema version 2 or migrate version 1 millisecond data to the current definition.
 * @param value Untrusted owning value tree borrowed only for this synchronous call.
 * @return Fully validated owning definition, or a structured parse, migration, or validation failure.
 * @remarks Unknown top-level fields and unknown/future versions are rejected. Failure publishes no runtime state.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<AbilityDefinition> decodeAbilityAsset(const Value& value);

/**
 * @brief Parse a stable instancing policy spelling used by script and tools.
 * @param text One of `non-instanced`, `per-owner`, `per-execution`.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<AbilityInstancingPolicy> parseAbilityInstancingPolicy(std::string_view text);

/**
 * @brief Parse a stable activation-group spelling used by script and tools.
 * @param text One of `independent`, `exclusive-replaceable`, `exclusive-blocking`.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<AbilityActivationGroup> parseAbilityActivationGroup(std::string_view text);

/**
 * @brief Assemble one AbilityDefinition from a timeline asset and policy inputs.
 * @param definitionId Ability identity.
 * @param timeline Owning timeline whose duration and splitTimestamps define action timing.
 * @param cooldown Injected cooldown duration.
 * @param instancing Instancing policy.
 * @param activationGroup Concurrency group.
 * @param triggerTag Optional exact gameplay-event trigger; empty omits triggers.
 * @return Fully validated owning definition, or a structured assembly/validation failure.
 * @remarks This is the asset/assembly face; AbilityRuntime remains the grant/activation owner.
 *          Failure publishes no runtime state.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<AbilityDefinition> abilityDefinitionFromTimeline(
    LogicalId definitionId, ActionTimeline timeline, Duration cooldown, AbilityInstancingPolicy instancing,
    AbilityActivationGroup activationGroup, std::string_view triggerTag = {});

}  // namespace eve::action
