#pragma once

#include "procgen/algorithms/CaveFieldSampling.h"

#include <optional>
#include <vector>

namespace eve::procgen {

struct CaveSurfaceAnchor {
    CaveFieldPoint position;
    CaveFieldPoint rockNormal;
};

struct CaveVerticalSpan {
    CaveSurfaceAnchor floor;
    CaveSurfaceAnchor ceiling;
};

/**
 * @brief Finds the first connected cave-air interval intersected by a vertical ray.
 * @param density Final pre-deposition signed density; negative values are cave air.
 * @param nx Density resolution on X.
 * @param ny Density resolution on Y.
 * @param nz Density resolution on Z.
 * @param x Normalized field-space X coordinate in [-1, 1].
 * @param z Normalized field-space Z coordinate in [-1, 1].
 * @param preferredY Preferred point inside the requested air interval.
 * @return Floor and ceiling zero crossings, or no value when no reliable air interval exists.
 */
std::optional<CaveVerticalSpan> findCaveVerticalSpan(const std::vector<float>& density, int nx, int ny, int nz, float x,
                                                     float z, float preferredY);

/**
 * @brief Projects a nearby field-space point to the final cave zero isosurface.
 * @param density Final pre-deposition signed density; negative values are cave air.
 * @param nx Density resolution on X.
 * @param ny Density resolution on Y.
 * @param nz Density resolution on Z.
 * @param point Normalized field-space point in [-1, 1].
 * @param maximumDistance Maximum accepted displacement from point.
 * @return Surface position and normal pointing into rock, or no value when projection is unreliable.
 */
std::optional<CaveSurfaceAnchor> projectToFinalCaveSurface(const std::vector<float>& density, int nx, int ny, int nz,
                                                           CaveFieldPoint point, float maximumDistance);

}  // namespace eve::procgen
