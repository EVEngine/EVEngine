#pragma once

#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"

#include <cstdint>
#include <memory>
#include <string>

namespace eve::procgen {

/** @brief Axis-aligned bounds shared by all procedural spatial data types. */
struct SpatialBounds {
    float minX = 0.f;
    float minY = 0.f;
    float minZ = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float maxZ = 0.f;
    bool  valid = false;
};

/**
 * @brief Queryable spatial input for procedural pipelines.
 *
 * SpatialData mirrors the data-oriented role of UE PCG spatial data without
 * coupling the procgen module to scene or rendering types. Values are cheap to
 * copy and composite operations retain immutable copies of their operands.
 */
class SpatialData {
public:
    enum class Kind {
        Empty,
        Points,
        Box,
        Sphere,
        Spline,
        Heightfield,
        Union,
        Intersection,
        Difference
    };

    /** @brief Construct empty spatial data. */
    SpatialData() = default;

    /** @brief Create spatial data whose domain is the bounds of attributed points. */
    static SpatialData fromPoints(const PointSet& points);
    /** @brief Create an axis-aligned volume; unordered endpoints are normalized. */
    static SpatialData box(float minX, float minY, float minZ, float maxX, float maxY,
                           float maxZ);
    /** @brief Create a spherical volume. A non-positive radius produces an empty domain. */
    static SpatialData sphere(float x, float y, float z, float radius);
    /** @brief Create a polyline domain with a radial influence width. */
    static SpatialData spline(const PointSet& controlPoints, float radius);
    /** @brief Create a sampled heightfield surface in world space. */
    static SpatialData heightfield(const Heightmap& heightmap, float originX, float originZ,
                                   float cellSize, float heightScale);
    /** @brief Create the union of two domains. */
    static SpatialData unite(const SpatialData& a, const SpatialData& b);
    /** @brief Create the intersection of two domains. */
    static SpatialData intersect(const SpatialData& a, const SpatialData& b);
    /** @brief Create the part of a outside b. */
    static SpatialData subtract(const SpatialData& a, const SpatialData& b);

    /** @brief Stable textual kind used by scripts and diagnostics. */
    std::string getKind() const;
    /** @brief Whether this domain contains a world-space position. */
    bool contains(float x, float y, float z) const;
    /** @brief Conservative world-space bounds, or invalid bounds for an empty domain. */
    SpatialBounds bounds() const;
    bool          hasBounds() const;
    float         getMinX() const;
    float         getMinY() const;
    float         getMinZ() const;
    float         getMaxX() const;
    float         getMaxY() const;
    float         getMaxZ() const;

    /**
     * @brief Deterministically sample a 3D lattice inside the domain.
     * @param spacing Distance between candidates on all axes.
     * @param seed Root seed used for stable point seeds and jitter.
     * @param jitter Fraction of spacing used for position jitter, clamped to [0,1].
     * @return Attributed samples inside the domain.
     */
    PointSet sample(float spacing, uint32_t seed, float jitter) const;
    /** @brief Filter attributed points by this domain. */
    PointSet filter(const PointSet& input, bool invert = false) const;
    /** @brief Project points onto a heightfield surface; other kinds return an unchanged copy. */
    PointSet project(const PointSet& input) const;

private:
    Kind                         kind_ = Kind::Empty;
    SpatialBounds                bounds_;
    PointSet                     points_;
    float                        centerX_ = 0.f;
    float                        centerY_ = 0.f;
    float                        centerZ_ = 0.f;
    float                        radius_  = 0.f;
    std::shared_ptr<Heightmap>   heightmap_;
    float                        originX_    = 0.f;
    float                        originZ_    = 0.f;
    float                        cellSize_   = 1.f;
    float                        heightScale_ = 1.f;
    std::shared_ptr<SpatialData> left_;
    std::shared_ptr<SpatialData> right_;
};

}  // namespace eve::procgen
