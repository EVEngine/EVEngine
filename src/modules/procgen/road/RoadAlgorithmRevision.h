#pragma once

#include <string_view>

namespace eve::procgen::road::detail {
// Producer-owned identity. Bump when road bake semantics or derived artifact layout changes.
inline constexpr std::string_view algorithmRevision = "road-core-2026-10-16-circular-unrealizable-chord-v22";
}  // namespace eve::procgen::road::detail
