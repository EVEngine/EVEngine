#pragma once

/** @file AbilityAsset.h @brief Versioned persistent codec for ability definitions. */

#include "action/AbilitySystem.h"
#include "common/SchemaVersion.h"

namespace eve::action {

inline constexpr std::string_view kAbilityAssetSchemaId = "eve.action.ability";
inline constexpr std::uint64_t    kAbilityAssetSchemaVersion = 2;

/**
 * @brief Encode one validated ability definition as canonical schema version 2 data.
 * @param definition Owning definition borrowed only for this synchronous call.
 * @return Independently owning Value, or a structured validation/encoding failure.
 * @remarks Owner-thread-only and reentrant; no callbacks, registries, files, or clocks are used.
 */
[[nodiscard]] Result<Value> encodeAbilityAsset(const AbilityDefinition& definition);

/**
 * @brief Decode schema version 2 or migrate version 1 millisecond data to the current definition.
 * @param value Untrusted owning value tree borrowed only for this synchronous call.
 * @return Fully validated owning definition, or a structured parse, migration, or validation failure.
 * @remarks Unknown top-level fields and unknown/future versions are rejected. Failure publishes no runtime state.
 */
[[nodiscard]] Result<AbilityDefinition> decodeAbilityAsset(const Value& value);

}  // namespace eve::action
