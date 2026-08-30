#pragma once

/** @file RuntimeDefinition.h @brief JSON-free versioned runtime metadata codec. */

#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <span>
#include <vector>

namespace eve::asset {

/** @brief Structural and allocation limits for untrusted EVDEF metadata. */
struct RuntimeDefinitionLimits {
    std::uint64_t maximumBytes = 64ull * 1024ull * 1024ull;
    std::uint32_t maximumDepth = 128;
    std::uint32_t maximumValues = 1'000'000;
    std::uint32_t maximumStringBytes = 4 * 1024 * 1024;
};

/**
 * @brief Encode an owning metadata tree as deterministic `EVDEF\0\1` bytes.
 * @return Canonical little-endian bytes; object keys use Value's sorted order.
 * @thread Worker-safe when value is not concurrently mutated.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeRuntimeDefinition(
    const Value& value, const RuntimeDefinitionLimits& limits = {});

/**
 * @brief Decode a bounded `EVDEF\0\1` metadata tree without a JSON parser.
 * @return Owning Value after exact-length, canonical-key and finite-number validation.
 * @thread Worker-safe; no shared mutable state.
 */
[[nodiscard]] Result<Value> decodeRuntimeDefinition(
    std::span<const std::uint8_t> bytes, const RuntimeDefinitionLimits& limits = {});

}  // namespace eve::asset
