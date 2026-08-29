#pragma once

#include "procgen/urban/UrbanTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace eve::procgen::urban {

/** @brief Return the raw (possibly negative) signed area of a polygon ring. */
double signedArea(const Polygon& poly);
/** @brief Absolute polygon area. */
double area(const Polygon& poly);
/** @brief Total length of an open polyline. */
double polylineLength(const Polyline& pl);
/** @brief Centroid (area-weighted) of a simple polygon. */
Vec2 centroid(const Polygon& poly);
/** @brief Perimeter length of a closed ring. */
double perimeter(const Polygon& poly);

/** @brief Ensure the ring is CCW (positive signed area); returns whether it was flipped. */
bool ensureCCW(Polygon& poly);
/** @brief Remove consecutive duplicate points (within `eps`) and points that create zero spikes. */
void cleanupRing(Polygon& poly, double eps = 1e-9);

/** @brief Point-in-polygon test (ray casting; boundary counts as inside). */
bool pointInPolygon(const Vec2& p, const Polygon& poly);
/** @brief True if p lies on segment a-b (within tolerance). */
bool pointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b, double eps = 1e-9);
/** @brief True if the open segments a-b and c-d properly cross; `out` receives the crossing. */
bool segmentsIntersect(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, Vec2* out);
/** @brief True if a segment crosses any segment of an open polyline (excluding shared endpoints). */
bool segmentIntersectsPolyline(const Vec2& a, const Vec2& b, const Polyline& pl);
/** @brief True if any two non-adjacent segments of the open polyline cross. */
bool polylineSelfIntersects(const Polyline& pl);
/** @brief Simple polygon test: no self intersections among non-adjacent ring edges. */
bool polygonIsSimple(const Polygon& poly);

/** @brief Closest point on segment a-b; returns distance and writes `out`. */
double closestPointOnSegment(const Vec2& p, const Vec2& a, const Vec2& b, Vec2* out);
/** @brief Closest point on the polygon boundary; writes edge index + t in [0,1]. */
Vec2 closestPointOnBoundary(const Polygon& poly, const Vec2& p, BoundaryPosition* pos);
/** @brief Interpolate the boundary point at arc length `s` in [0, perimeter). */
Vec2 pointAtBoundaryLength(const Polygon& poly, double s, BoundaryPosition* pos);

/**
 * @brief Uniformly sample the polygon boundary (approx. `count` samples, at least 8).
 * Samples are ordered along the boundary; each stores the tangent angle in radians.
 */
std::vector<BoundarySample> sampleBoundary(const Polygon& poly, int count);

/** @brief Included angle in degrees at the shared vertex between edge u (prev->shared) and v (shared->next), in
 * [0,180]. */
double includedAngleDeg(const Vec2& u, const Vec2& v);
/** @brief True when two consecutive edges are considered collinear (included angle > 135°). */
bool isCollinear(const Vec2& prev, const Vec2& shared, const Vec2& next);

/**
 * @brief Simplify a ring to its approximate polygon: consecutive edges with included
 * angle > 135° are merged into a single side (paper Section 3).
 */
Polygon approximatePolygon(const Polygon& ring);

/**
 * @brief Shape irregularity metric of Eq. (1) over the *approximate* polygon:
 * I = γ1·(1/N)·Σ(θi−θ̄)² + γ2·(1/(N·l̄²))·Σ(li−l̄)².
 * Smaller is more regular; 0 for an ideal regular polygon.
 */
double shapeIrregularity(const Polygon& approxRing, double gammaAngle = 0.75, double gammaSide = 0.25);

/**
 * @brief Split a CCW simple polygon by a polyline whose endpoints lie on the boundary and
 * whose interior points are strictly inside the polygon. `split` goes from boundary point A
 * to boundary point B; the two resulting CCW rings are returned in `outA`/`outB`.
 * Returns false on degenerate input. Callers verify area constraints afterwards.
 */
bool splitPolygonByPolyline(const Polygon& poly, const Polyline& split, const BoundaryPosition& posA,
                            const BoundaryPosition& posB, Polygon& outA, Polygon& outB);

/** @brief Validity of a candidate split: both halves simple, positive area, inside the original. */
bool validSplit(const Polygon& poly, const Polyline& split, double minHalfArea, Polygon* outA, Polygon* outB,
                double* fracA);

/** @brief Triangulate a simple polygon by ear clipping; returns CCW triangles (3*i..3*i+2). */
bool triangulatePolygon(const Polygon& poly, std::vector<int>& outTriangles);

/** @brief Raster helper: does the pixel-center fall within `eps` of segment a-b? */
double distanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b);

}  // namespace eve::procgen::urban
