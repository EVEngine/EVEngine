#pragma once
#include "procgen/road/RoadGeometrySources.h"
namespace eve::procgen::road::detail {
/** @brief Seal producer-owned intervals into full-mesh metadata; synchronous borrowed inputs, atomic metadata write. */
[[nodiscard]] Result<void> sealRoadGeometrySources(MeshBuild& mesh, std::span<const RoadGeometrySourceSpan> spans);
}  // namespace eve::procgen::road::detail
