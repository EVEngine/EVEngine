#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve::procgen::urban {

/**
 * @brief Minimal 2D vector used by the urban layout algorithms (paper coordinates).
 */
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }
};

inline double cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline double dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline double length(const Vec2& a) { return std::sqrt(dot(a, a)); }
inline double lengthSq(const Vec2& a) { return dot(a, a); }
inline Vec2   normalize(const Vec2& a) {
    const double l = length(a);
    if (l <= 1e-12) return {1.0, 0.0};
    return a / l;
}
inline Vec2   perpendicular(const Vec2& a) { return {-a.y, a.x}; }
inline double distance(const Vec2& a, const Vec2& b) { return length(a - b); }

/** @brief Closed polygon ring, stored CCW, without repeating the first point. */
using Polygon = std::vector<Vec2>;
/** @brief Open polyline (e.g. a streamline candidate or a street centerline). */
using Polyline = std::vector<Vec2>;

/**
 * @brief A street decomposed from the street network graph: consecutive street edges
 * whose included angle is larger than 135° belong to the same street.
 */
struct Street {
    Polyline pts;  // centerline along parcel boundary edges
    double   width = 1.0;
};

/** @brief One parcel: a CCW ring of indices into UrbanLayout::corners. */
struct Parcel {
    std::vector<int> ring;  // corner indices, CCW, no repeated first corner
};

/** @brief Undirected edge of the parcel corner graph. */
struct GraphEdge {
    int  a        = -1;
    int  b        = -1;
    bool isStreet = false;
};

/**
 * @brief Structured urban layout produced by the hierarchical co-generation
 * (paper Section 4) plus the geometric optimization (paper Section 5).
 */
struct UrbanLayout {
    std::vector<Vec2>                corners;         // welded unique parcel corners
    std::vector<Parcel>              parcels;         // CCW rings over `corners`
    std::vector<GraphEdge>           edges;           // parcel corner graph edges
    std::vector<Street>              streets;         // decomposed street network
    std::vector<std::pair<int, int>> streetSegments;  // corner index pairs (for mesh/access)

    int    streetJunctions   = 0;
    int    streetEnds        = 0;
    double totalStreetLength = 0.0;
    double avgIrregularity   = 0.0;
    double minIrregularity   = 0.0;
    double maxIrregularity   = 0.0;
    int    levelsUsed        = 0;

    void clear() {
        corners.clear();
        parcels.clear();
        edges.clear();
        streets.clear();
        streetSegments.clear();
        streetJunctions = streetEnds = 0;
        totalStreetLength = avgIrregularity = minIrregularity = maxIrregularity = 0.0;
        levelsUsed                                                              = 0;
    }
};

/**
 * @brief All user-facing controls for the urban generator.
 * Defaults follow the paper (λ=0.3/0.5/0.2, γ=0.75/0.25, τ=0.5, I/L-shaped access,
 * cul-de-sac avoidance on).
 */
struct UrbanOptions {
    Polygon  land;                 // input land polygon (CCW); filled by the generator
    double   minParcelArea = 4.0;  // minimally allowed parcel area (world units^2)
    int      targetParcels = 120;  // desired parcel count (0 = run until no parcel is splittable)
    int      maxLevels     = 10;
    uint32_t seed          = 1;

    double lambdaSize             = 0.3;   // λ1 size balance
    double lambdaRegu             = 0.5;   // λ2 regularity of resulting parcels
    double lambdaAcce             = 0.2;   // λ3 street access
    double lambdaOrient           = 0.0;   // optional orientation preference weight
    double gammaAngle             = 0.75;  // γ1 interior-angle variance
    double gammaSide              = 0.25;  // γ2 side-length variance
    double accessThreshold        = 0.5;   // τ for Q_acce
    double shortEdgeFactor        = 0.2;   // short-edge removal threshold factor
    double streetWidth            = 1.0;   // street width (world units) for output
    double dijkstraJunctionWeight = 1.5;   // extra cost of a turn when connecting streets
    double boundaryStreetFraction = 0.5;   // fraction of boundary that is street (mode 2)

    int streetPattern      = 0;  // 0=default(avoid cul-de-sacs), 1=loop, 2=culdesac, 3=tree
    int culDeSacAfterLevel = 4;  // allow street ends only from this level on (pattern 2)
    int orientation        = 0;  // 0=none, 1=east-west, 2=north-south
    int boundaryStreetMode = 0;  // 0=all boundary is street, 1=none, 2=random segments

    bool   optimize           = true;
    int    optimizeIterations = 160;
    double optRegu            = 0.20;  // ω1
    double optSide            = 1.00;  // ω2
    double optStre            = 1.00;  // ω3
    double optJunc            = 0.50;  // ω4
    double optClose           = 0.30;  // ω5
    double optStep            = 0.35;
};

/** @brief Where a point sits on the polygon boundary: edge `edgeIndex` at parameter t in [0,1]. */
struct BoundaryPosition {
    int    edgeIndex = -1;
    double t         = 0.0;
};

/** @brief A boundary sample used for field constraints / candidate seeds. */
struct BoundarySample {
    Vec2   p;
    double tangentAngle = 0.0;  // radians, angle of the boundary tangent
    double arcLength    = 0.0;
};

}  // namespace eve::procgen::urban
