#pragma once

#include <cstdint>

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

}  // namespace eve::graphics
