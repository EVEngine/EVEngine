#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <string>

namespace eve::graphics {

/**
 * @brief 3D lighting strategy for opaque geometry.
 *
 * Transparent / hair draws always use Forward+ (or the legacy transparent
 * forward path). Hybrid selects clustered deferred lighting for core PBR
 * opaque/masked geometry once that pass is available.
 *
 * @see docs/dev/superpowers/specs/2026-09-26-hybrid-clustered-deferred-forward-plus-design.md
 */
enum class LightingMode : uint8_t {
    ForwardPlus = 0, /**< Clustered-forward for opaque (default). */
    Hybrid      = 1, /**< Opaque: clustered deferred; transparent: Forward+. */
};

/**
 * @brief Platform lighting quality preset (does not replace an explicit setLightingMode).
 *
 * Desktop → Hybrid when deferred lighting is available; Mobile / Ci → ForwardPlus.
 * Suggested by OS + GPU (Lavapipe / CPU ICD → Ci) and optional env overrides.
 */
enum class LightingPreset : uint8_t {
    Desktop = 0, /**< Prefer Hybrid when the backend supports deferred lighting. */
    Mobile  = 1, /**< Bandwidth-sensitive; keep ForwardPlus. */
    Ci      = 2, /**< Headless / software Vulkan (Lavapipe); keep ForwardPlus. */
};

/**
 * @brief Suggest a lighting preset from OS, GPU type/name, and env.
 *
 * Precedence:
 * 1. `EVENGINE_LIGHTING_PRESET=desktop|mobile|ci`
 * 2. Android / iOS → Mobile
 * 3. GPU device type `cpu` or name containing llvmpipe/lavapipe → Ci
 * 4. else Desktop
 *
 * @thread Safe to call from the render thread after Graphics init (GPU info may be empty earlier).
 */
[[nodiscard]] EVENGINE_API_BACKENDS LightingPreset suggestedLightingPreset();

/**
 * @brief Optional explicit mode from `EVENGINE_LIGHTING_MODE=forwardPlus|hybrid`.
 * @return Parsed mode, or empty when the env var is unset / unknown.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<LightingMode> lightingModeFromEnv();

}  // namespace eve::graphics
