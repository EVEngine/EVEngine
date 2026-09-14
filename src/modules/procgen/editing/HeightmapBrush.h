#pragma once

#include "editing/EditingResult.h"

namespace eve::procgen { class Heightmap; }

namespace eve::procgen_editing {

/**
 * @brief Apply the legacy circular brush through the procgen authoring satellite.
 * @param heightmap Borrowed target heightmap.
 * @param centerX Brush center in cell coordinates.
 * @param centerY Brush center in cell coordinates.
 * @param radius Non-negative radius in cells.
 * @param strength Signed center-height delta.
 * @return Applied with the changed sample count, NoOp when strength is zero, or a validation failure.
 */
[[nodiscard]] editing::Result<int> applyHeightmapBrush(procgen::Heightmap* heightmap,
                                                       float centerX, float centerY,
                                                       float radius, float strength);

}  // namespace eve::procgen_editing
