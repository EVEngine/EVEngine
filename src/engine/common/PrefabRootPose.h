#pragma once

#include <cstdint>

namespace eve {
/** @brief Immutable prefab placement convention shared by geometry bounds and renderer preparation.
 * Authored preserves the complete source hierarchy. ReplaceTranslationRotation replaces only the
 * source root position/orientation with the instance pose, preserving its signed scale and all children.
 * Values are persistent; unknown values must be rejected before publication.
 */
enum class PrefabRootPose : std::uint8_t { Authored = 0, ReplaceTranslationRotation = 1 };
}  // namespace eve
