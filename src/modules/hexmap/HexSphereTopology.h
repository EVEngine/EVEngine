#pragma once
#include "common/Export.h"


/** @file HexSphereTopology.h @brief Icosahedral hex topology: twelve pentagons and hexagons on a sphere. */

#include "common/Result.h"
#include "hexmap/HexMetrics.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/** @brief Dense identifier of one cell of a spherical hex topology. */
using HexSphereCell = std::int32_t;

/** @brief Returned by a spherical cell query that has no answer. */
inline constexpr HexSphereCell kNoHexSphereCell = -1;

/** @brief Largest supported subdivision level; `10 * 4^7 + 2 = 163842` cells. */
inline constexpr std::int32_t kMaxHexSphereSubdivision = 7;

/**
 * @brief Icosahedral (Goldberg) hex topology for a spherical hex map.
 *
 * The polyhedron is the dual of a geodesic sphere: an icosahedron subdivided
 * `subdivision` times at frequency `f = 2^subdivision`, whose **vertices** become
 * the cells. A cell therefore has six neighbours, except the twelve cells that
 * descend from the original icosahedron vertices, which have five. That makes
 * `cellCount() == 10 * f^2 + 2` and `pentagonCount() == 12` for every level,
 * matching the `mesh.hexplanet` recipe of the procgen module.
 *
 * Cell ids are dense and stable: cells `[0, 12)` are exactly the twelve
 * pentagons, because midpoint subdivision never renumbers the original vertices.
 *
 * Directions are not arithmetic. The six planar directions of `HexMetrics` are
 * constant integer offsets in one global frame; on a sphere the edge order around
 * a cell depends on the cell, and a pentagon has only five edges, so there is no
 * `opposite(direction)` offset that works everywhere. Every cross-edge query goes
 * through `directionOf` instead.
 *
 * Direction `d` of a cell is the edge between `corner(d)` and
 * `corner((d + 1) % neighbourCount)`. Both corners are shared with the neighbour
 * across that edge, in the opposite order, which is what lets a mesh builder walk
 * a boundary from either side.
 *
 * Ownership and lifetime: a value type owning three flat arrays; no I/O, no GPU
 * resources, no callbacks. Immutable once built, so it is safe to share.
 *
 * Determinism: the construction is a pure function of `subdivision`; two builds
 * at the same level produce identical ids, directions, neighbours and corners.
 */
class EVENGINE_API_WORLD HexSphereTopology {
public:
    HexSphereTopology() = default;

    /**
     * @brief Builds the topology of one subdivision level.
     *
     * @param subdivision Subdivision level in `[0, kMaxHexSphereSubdivision]`;
     *        level 0 is the bare icosahedron dual (12 pentagons, 20 corners).
     * @return The topology, or InvalidArgument when the level is out of range, or
     *         a construction failure when the half-edge walk cannot close (an
     *         internal consistency error that a valid icosahedron never triggers).
     * @cost Builds all five tables at once; proportional to the cell count and
     *       dominated by the `10 * 4^subdivision` cells, amortized over every
     *       query the caller makes afterwards.
     */
    [[nodiscard]] static Result<HexSphereTopology> build(std::int32_t subdivision);

    /** @brief Whether the topology holds no cell. */
    [[nodiscard]] bool empty() const noexcept { return directions_.empty(); }
    /** @brief The subdivision level this topology was built at. */
    [[nodiscard]] std::int32_t subdivision() const noexcept { return subdivision_; }
    /** @brief Subdivision frequency `f = 2^subdivision`. */
    [[nodiscard]] std::int32_t frequency() const noexcept { return frequency_; }
    /** @brief Number of cells (`10 * f^2 + 2`). */
    [[nodiscard]] std::int32_t cellCount() const noexcept { return static_cast<std::int32_t>(directions_.size()); }
    /** @brief Number of pentagonal cells (12 for every non-empty topology). */
    [[nodiscard]] std::int32_t pentagonCount() const noexcept { return empty() ? 0 : 12; }
    /** @brief Number of cell-to-cell edges (`30 * f^2`). */
    [[nodiscard]] std::int32_t edgeCount() const noexcept { return static_cast<std::int32_t>(neighbors_.size() / 2u); }
    /** @brief Number of distinct corners (`20 * f^2`), each shared by three cells. */
    [[nodiscard]] std::int32_t cornerCount() const noexcept {
        return static_cast<std::int32_t>(cornerDirections_.size());
    }

    /** @brief Whether `cell` is a valid id of this topology. */
    [[nodiscard]] bool contains(HexSphereCell cell) const noexcept { return cell >= 0 && cell < cellCount(); }
    /** @brief Whether `cell` has five edges instead of six; false when out of range. */
    [[nodiscard]] bool isPentagon(HexSphereCell cell) const noexcept {
        return contains(cell) && neighborCount(cell) == 5;
    }
    /** @brief Number of edges of `cell` (5 or 6), or 0 when out of range. */
    [[nodiscard]] std::int32_t neighborCount(HexSphereCell cell) const noexcept;
    /**
     * @brief Neighbour of `cell` across edge `direction`.
     *
     * @param cell Source cell.
     * @param direction Edge index in `[0, neighborCount(cell))`.
     * @return The neighbouring cell, or `kNoHexSphereCell` when either argument is
     *         out of range.
     */
    [[nodiscard]] HexSphereCell neighbor(HexSphereCell cell, std::int32_t direction) const noexcept;
    /**
     * @brief Edge index of the edge shared with `other`.
     *
     * The spherical replacement for `opposite`: a hexagon-hexagon edge happens to
     * satisfy `d' == (d + 3) % 6`, but a hexagon-pentagon edge has no arithmetic
     * form, so callers must always ask this instead of computing an offset.
     *
     * @return Edge index in `[0, neighborCount(cell))`, or `kNoHexSphereCell` when
     *         `other` is not adjacent to `cell`.
     */
    [[nodiscard]] std::int32_t directionOf(HexSphereCell cell, HexSphereCell other) const noexcept;
    /** @brief Edge index of `cell`'s edge towards its neighbour across `direction`. */
    [[nodiscard]] std::int32_t oppositeDirection(HexSphereCell cell, std::int32_t direction) const noexcept;

    /** @brief Unit direction from the sphere centre to the centre of `cell`. */
    [[nodiscard]] HexVec3 direction(HexSphereCell cell) const noexcept;
    /** @brief Number of corners of `cell` (5 or 6), or 0 when out of range. */
    [[nodiscard]] std::int32_t cornerCountOf(HexSphereCell cell) const noexcept;
    /**
     * @brief Unit direction of corner `corner` of `cell`.
     *
     * Corner `d` lies between edge `d - 1` and edge `d` (both cyclic), and is
     * shared by three cells. On a sphere a corner is not a fixed angular offset of
     * its cell, so the mesh builder reads it here instead of deriving it.
     *
     * @return A unit vector, or the sphere's north pole when an argument is out
     *         of range (callers gate on `contains`).
     */
    [[nodiscard]] HexVec3 corner(HexSphereCell cell, std::int32_t corner) const noexcept;

    /**
     * @brief The cell whose centre direction is nearest to `unitDirection`.
     *
     * Descends the neighbour graph from the best of the twelve pentagons, which
     * makes the query cost proportional to the angular distance walked rather than
     * to the cell count.
     *
     * @param unitDirection Any non-zero direction; normalized internally.
     * @return The containing cell, or `kNoHexSphereCell` when the topology is empty.
     */
    [[nodiscard]] HexSphereCell cellAt(HexVec3 unitDirection) const noexcept;
    /** @brief Great-circle angle between two directions, in radians. */
    [[nodiscard]] static float angularDistance(HexVec3 a, HexVec3 b) noexcept;

private:
    std::vector<HexVec3>       directions_;        /**< Cell centres, unit length. */
    std::vector<std::int32_t>  cellOffsets_;       /**< `cellCount + 1` offsets into the two flat tables. */
    std::vector<HexSphereCell> neighbors_;         /**< Neighbour per (cell, direction). */
    std::vector<std::int32_t>  corners_;           /**< Corner id per (cell, direction). */
    std::vector<HexVec3>       cornerDirections_;  /**< Corner positions, unit length. */
    std::int32_t               subdivision_ = 0;
    std::int32_t               frequency_   = 1;
};

}  // namespace eve::hexmap
