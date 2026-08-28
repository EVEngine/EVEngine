#pragma once

/**
 * @file ClimbingCodec.h
 * @brief Versioned, lossless codecs for climbing definitions.
 */

#include "climbing/Climbing.h"

namespace eve::climbing {

/** @brief Validate every known field of one action definition. */
[[nodiscard]] eve::Result<void> validateClimbingActionDefinition(const ClimbingActionDefinition& action);

/** @brief Validate every known field and nested action of one profile definition. */
[[nodiscard]] eve::Result<void> validateClimbingProfileDefinition(const ClimbingProfileDefinition& profile);

/** @brief Encode one validated action into canonical schema v4 data. */
[[nodiscard]] eve::Result<eve::Value> encodeClimbingActionDefinition(const ClimbingActionDefinition& action);

/**
 * @brief Decode and validate one action without partially mutating a registry.
 * @remarks Unknown root fields are retained in extensionMetadata and re-emitted by the encoder.
 */
[[nodiscard]] eve::Result<ClimbingActionDefinition> decodeClimbingActionDefinition(const eve::Value& value);

/** @brief Encode one validated profile and all nested actions into canonical schema v4 data. */
[[nodiscard]] eve::Result<eve::Value> encodeClimbingProfileDefinition(const ClimbingProfileDefinition& profile);

/**
 * @brief Decode and validate an owning profile candidate transactionally.
 * @remarks Unknown root and nested action fields are retained for lossless forward-compatible editing.
 */
[[nodiscard]] eve::Result<ClimbingProfileDefinition> decodeClimbingProfileDefinition(const eve::Value& value);

}  // namespace eve::climbing
