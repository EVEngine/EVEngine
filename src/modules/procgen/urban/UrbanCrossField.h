#pragma once

#include "procgen/urban/UrbanTypes.h"

#include <cstddef>
#include <vector>

namespace eve::procgen::urban {

/**
 * @brief Candidate splitting lines for a parcel (paper Section 4.1).
 * Each candidate is a polyline from one boundary point to another, strictly inside
 * otherwise, and verified to split the parcel into two simple polygons.
 */
struct SplitCandidate {
    Polyline line;
    double   areaFrac = 0.5;  // area fraction of the smaller/larger half (Si/Sj with Si<Sj)
};

/**
 * @brief Generate candidate streamlines for binary partitioning of `poly`.
 *
 * The paper computes ~20 streamlines following the cross-field approach of
 * Yang et al. 2013: a cross field (two orthogonal directions) is aligned with the
 * parcel boundary and smoothed over the interior; hyperstreamlines are traced from
 * boundary seeds until they re-hit the boundary. We discretize the field on a
 * regular grid. When tracing yields too few usable curves (e.g. very thin parcels),
 * straight chords between boundary samples are added as a fallback so the
 * generator always has candidates.
 *
 * @param poly CCW simple parcel polygon.
 * @param maxCandidates number of candidates to return (paper: ~20).
 * @param minHalfArea minimum area of each resulting half (fraction check is internal).
 */
std::vector<SplitCandidate> generateSplitCandidates(const Polygon& poly, int maxCandidates, double minHalfArea);

}  // namespace eve::procgen::urban
