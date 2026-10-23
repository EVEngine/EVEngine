#pragma once

#include <cstddef>
#include <utility>
#include "procgen/road/RoadTypes.h"

namespace eve::procgen::road::detail {
/** @brief Validate finite source mesh topology and bounded source ranges. */
[[nodiscard]] Result<void> validateFacilityPrefab(const RoadFacilityPrefab& prefab);
/** @brief Reflect an owned prefab into reverse traversal coordinates, including its segment sequence/window. */
void reverseFacilityPrefab(RoadFacilityPrefab& prefab);
/** @brief Validate bounded simple section geometry and semantic material tags. */
[[nodiscard]] Result<void> validateSection(const std::vector<RoadProfilePoint>& points, bool closed);
/** @brief Validate structural intervals and continuous-facility authoring. */
[[nodiscard]] Result<void> validateRoadAuthoring(const RoadStyle& style);
/** @brief Reflect section and interval authoring while preserving physical placement. */
void reverseRoadAuthoring(RoadStyle& style);
/** @brief Reflect an owning section and preserve outgoing segment semantics. */
void reverseSectionGeometry(std::vector<RoadProfilePoint>& section, bool closed);
/** @brief Clip/remap normalized arc intervals into owning first/second styles. */
[[nodiscard]] Result<std::pair<RoadStyle, RoadStyle>> splitRoadAuthoring(const RoadEdge& edge, std::size_t pointIndex);
/** @brief Return custom asphalt height at one unscaled driving lateral position. */
float roadSurfaceHeight(const RoadStyle& style, float lateral);
}  // namespace eve::procgen::road::detail
