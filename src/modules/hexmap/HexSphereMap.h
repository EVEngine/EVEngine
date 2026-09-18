#pragma once

/** @file HexSphereMap.h @brief Editable spherical hex map over an icosahedral hex topology. */

#include "common/Result.h"
#include "hexmap/HexCell.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"
#include "hexmap/HexSphereTopology.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/**
 * @brief An editable hex map wrapped onto a sphere.
 *
 * This is the spherical counterpart of `HexMap`. It reuses the whole
 * per-cell data model unchanged — `HexCellData`, `HexValues` and `HexFlags` —
 * and the geometry constants of `HexMetrics`, and replaces only the two things
 * that cannot survive the change of surface: cell **identity** and the
 * **neighbour relation**.
 *
 * Cell identity is a dense `HexSphereCell` id from `HexSphereTopology` instead
 * of axial/offset `HexCoordinates`, and neighbours are reached through the
 * topology's half-edge tables instead of integer direction offsets. There is no
 * chunk grid: the topology already gives every cell a stable id over the whole
 * sphere, so dirty tracking is per cell.
 *
 * Elevation is radial. A cell's surface sits at
 * `sphereRadius + elevation * elevationStep`, so `elevationStep` is the world
 * distance one elevation level lifts the surface, and it defaults to a fraction
 * of the radius rather than to the planar `HexMetrics::kElevationStep` (three
 * world units), which would be an absurd relief on a planet-sized sphere. The
 * editable range, the packed layout and every clamping rule stay exactly those
 * of `HexValues`.
 *
 * Direction indices are the topology's edge indices in
 * `[0, neighborCount(cell))`, i.e. six for a hexagon and five for a pentagon.
 * `HexFlags` stores roads and rivers in six one-bit slots indexed by
 * `HexDirection`, so a direction index is cast into that enumeration directly;
 * slot 5 is simply never set on a pentagon.
 *
 * Ownership and lifetime: a value type owning its topology, its cell storage and
 * its noise field. It performs no I/O, opens no GPU resources and invokes no
 * callbacks.
 *
 * Thread affinity: affine to one thread, like `HexMap`. The type is not
 * synchronized.
 *
 * Determinism: the topology, the noise field and every derived position depend
 * only on the subdivision level, the radius and the seed. Two maps built with
 * the same triple produce identical topology, cell storage and positions.
 */
class HexSphereMap {
public:
    /** @brief Ratio of the default radial elevation step to the sphere radius. */
    static constexpr float kDefaultElevationRatio = 0.006f;

    HexSphereMap() = default;

    /**
     * @brief Replaces the map with a new sphere of `10 * 4^subdivision + 2` cells.
     *
     * Every cell starts as a default record at elevation 0 and the whole sphere
     * is marked dirty. The subdivision level fixes the cell count permanently for
     * the lifetime of this map.
     *
     * @param subdivision Subdivision level in `[0, kMaxHexSphereSubdivision]`.
     * @param radius Sphere radius in world units; must be finite and positive.
     * @param seed Deterministic seed for the noise field.
     * @return Success, or InvalidArgument when an argument is out of range, or the
     *         topology construction's own failure status.
     * @cost Builds the topology's five tables; proportional to the cell count.
     */
    [[nodiscard]] Result<void> reset(std::int32_t subdivision, float radius, std::uint32_t seed);

    // --- shape and identity -------------------------------------------------

    /** @brief Whether the map holds any cell. */
    [[nodiscard]] bool empty() const noexcept { return cells_.empty(); }
    /** @brief The subdivision level this map was built at. */
    [[nodiscard]] std::int32_t subdivision() const noexcept { return topology_.subdivision(); }
    /** @brief Subdivision frequency `f = 2^subdivision`. */
    [[nodiscard]] std::int32_t frequency() const noexcept { return topology_.frequency(); }
    /** @brief Number of cells (`10 * f^2 + 2`). */
    [[nodiscard]] std::int32_t cellCount() const noexcept { return static_cast<std::int32_t>(cells_.size()); }
    /** @brief Number of pentagonal cells (12 for a non-empty map). */
    [[nodiscard]] std::int32_t pentagonCount() const noexcept { return topology_.pentagonCount(); }
    /** @brief Sphere radius in world units. */
    [[nodiscard]] float sphereRadius() const noexcept { return sphereRadius_; }
    /**
     * @brief Mean centre-to-centre distance between neighbouring cells, in world units.
     *
     * The spherical replacement for `HexMetrics::kOuterRadius` as a *scale*: geometry
     * that the planar builders express as a fixed number of world units has to be
     * expressed as a fraction of the cell size on a sphere, because the cell size is
     * set by the subdivision level and the radius together.
     */
    [[nodiscard]] float cellSpacing() const noexcept;
    /** @brief World distance one elevation level lifts the surface. */
    [[nodiscard]] float elevationStep() const noexcept { return elevationStep_; }
    /**
     * @brief Sets the radial world distance of one elevation level.
     *
     * Only geometry changes, so the cells stay clean; the revision is bumped so a
     * renderer can tell that the built meshes are stale.
     */
    void setElevationStep(float step) noexcept;
    /** @brief The seed this map was built with. */
    [[nodiscard]] std::uint32_t seed() const noexcept { return noise_.seed(); }
    /** @brief Monotonic counter bumped by every mutation; used to detect stale meshes. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Deterministic noise field shared by this map's mesh builders. */
    [[nodiscard]] const HexNoise& noise() const noexcept { return noise_; }
    /**
     * @brief The topology backing this map.
     * @ownership Borrowed; valid until this map is destroyed or `reset` is called.
     */
    [[nodiscard]] const HexSphereTopology& topology() const noexcept { return topology_; }

    // --- topology -----------------------------------------------------------

    /** @brief Whether `cell` is a valid id of this map. */
    [[nodiscard]] bool contains(HexSphereCell cell) const noexcept { return topology_.contains(cell); }
    /** @brief Whether `cell` has five edges instead of six. */
    [[nodiscard]] bool isPentagon(HexSphereCell cell) const noexcept { return topology_.isPentagon(cell); }
    /**
     * @brief Number of edges of `cell` (5 or 6), or 0 when out of range.
     *
     * On the sphere this is not a constant, so a caller enumerating the edges of a
     * cell must bound its loop by this and never by `kHexDirectionCount`.
     */
    [[nodiscard]] std::int32_t neighborCount(HexSphereCell cell) const noexcept {
        return topology_.neighborCount(cell);
    }
    /** @brief Neighbour of `cell` across edge `direction`, or `kNoHexSphereCell`. */
    [[nodiscard]] HexSphereCell neighbor(HexSphereCell cell, std::int32_t direction) const noexcept {
        return topology_.neighbor(cell, direction);
    }
    /** @brief Edge index shared with `other`, or `kNoHexSphereCell` when not adjacent. */
    [[nodiscard]] std::int32_t directionOf(HexSphereCell cell, HexSphereCell other) const noexcept {
        return topology_.directionOf(cell, other);
    }
    /** @brief Edge index of `cell`'s edge towards its neighbour across `direction`. */
    [[nodiscard]] std::int32_t oppositeDirection(HexSphereCell cell, std::int32_t direction) const noexcept {
        return topology_.oppositeDirection(cell, direction);
    }
    /** @brief Number of corners of `cell` (5 or 6), or 0 when out of range. */
    [[nodiscard]] std::int32_t cornerCountOf(HexSphereCell cell) const noexcept {
        return topology_.cornerCountOf(cell);
    }
    /** @brief Relationship between two adjacent cells; Flat when not adjacent. */
    [[nodiscard]] HexEdgeType edgeTypeTo(HexSphereCell a, HexSphereCell b) const noexcept;

    // --- geometry -----------------------------------------------------------

    /** @brief Unit direction from the sphere centre to the centre of `cell`. */
    [[nodiscard]] HexVec3 direction(HexSphereCell cell) const noexcept { return topology_.direction(cell); }
    /**
     * @brief Unit direction of corner `corner` of `cell`.
     *
     * A corner is not a fixed angular offset of its cell on a sphere, so the mesh
     * builder must read it here instead of deriving it from edge directions.
     */
    [[nodiscard]] HexVec3 cornerDirection(HexSphereCell cell, std::int32_t corner) const noexcept {
        return topology_.corner(cell, corner);
    }
    /** @brief Radius of the cell's surface: `sphereRadius + elevation * elevationStep`. */
    [[nodiscard]] float surfaceRadius(HexSphereCell cell) const noexcept;
    /** @brief World-space centre of the cell's surface. */
    [[nodiscard]] HexVec3 cellPosition(HexSphereCell cell) const noexcept;
    /** @brief World-space centre of the cell at radius, ignoring elevation. */
    [[nodiscard]] HexVec3 cellGroundPosition(HexSphereCell cell) const noexcept;

    // --- cell access --------------------------------------------------------

    /**
     * @brief Cell record by id, or null when out of range.
     * @ownership Borrowed; the map owns the record.
     * @lifetime Valid until this map is destroyed or `reset` is called; any
     *           mutation of the map's cell storage invalidates the pointer.
     */
    [[nodiscard]] const HexCellData* cellAt(HexSphereCell cell) const noexcept;

    [[nodiscard]] std::int32_t elevation(HexSphereCell c) const noexcept;
    [[nodiscard]] std::int32_t waterLevel(HexSphereCell c) const noexcept;
    [[nodiscard]] std::int32_t terrainType(HexSphereCell c) const noexcept;
    [[nodiscard]] std::int32_t urbanLevel(HexSphereCell c) const noexcept;
    [[nodiscard]] std::int32_t farmLevel(HexSphereCell c) const noexcept;
    [[nodiscard]] std::int32_t plantLevel(HexSphereCell c) const noexcept;
    [[nodiscard]] std::int32_t specialIndex(HexSphereCell c) const noexcept;
    [[nodiscard]] bool         isUnderwater(HexSphereCell c) const noexcept;
    [[nodiscard]] bool         hasRiver(HexSphereCell c) const noexcept;
    [[nodiscard]] bool         hasRoad(HexSphereCell c) const noexcept;
    [[nodiscard]] bool         hasRiverThrough(HexSphereCell c, std::int32_t direction) const noexcept;
    [[nodiscard]] bool         isWalled(HexSphereCell c) const noexcept;
    /** @brief Whether a cell has ever been seen by a viewer (the fog-of-war latch). */
    [[nodiscard]] bool         isExplored(HexSphereCell c) const noexcept;
    /** @brief Whether a cell may ever be revealed by a viewer. */
    [[nodiscard]] bool         isExplorable(HexSphereCell c) const noexcept;
    /** @brief Full packed value record of a cell (zero when out of range). */
    [[nodiscard]] HexValues    values(HexSphereCell c) const noexcept;
    /** @brief Full packed flag record of a cell (zero when out of range). */
    [[nodiscard]] HexFlags     flags(HexSphereCell c) const noexcept;

    // --- picking ------------------------------------------------------------

    /**
     * @brief Finds the cell under a world-space ray.
     *
     * The map has no collision geometry, so the ray is intersected with the
     * sphere at the cell's own surface radius and the result is refined a few
     * times: the first hit is taken against the bounding sphere, then the radius
     * of the cell found there is used for the next intersection. A camera outside
     * the sphere converges in a handful of iterations.
     *
     * @param rayOrigin Ray origin in world space.
     * @param rayDirection Non-zero ray direction in world space; normalized internally.
     * @return The hit cell, or NotFound when the ray misses the sphere, or
     *         InvalidArgument when the direction is zero.
     * @cost A fixed small number of `HexSphereTopology::cellAt` walks.
     */
    [[nodiscard]] Result<HexSphereCell> pickCell(HexVec3 rayOrigin, HexVec3 rayDirection) const;

    // --- neighbourhood queries ----------------------------------------------

    /**
     * @brief Number of edges between two cells, or `kNoHexSphereCell` when either is invalid.
     *
     * This is a breadth-first walk over the neighbour graph: a sphere has no
     * cheap closed form for hex distance, unlike the planar offset arithmetic.
     *
     * @cost O(cellCount) time and memory; intended for authoring and tests, not
     *       for per-frame queries over many pairs.
     */
    [[nodiscard]] std::int32_t distance(HexSphereCell a, HexSphereCell b) const;

    /**
     * @brief Enumerates the cells within `radius` steps of `center` into `out`.
     *
     * `out` is cleared first and then filled in breadth-first order, which is
     * deterministic for a given topology. The centre is always the first entry.
     *
     * @param center Brush centre.
     * @param radius Number of steps the brush reaches; 0 collects only the centre.
     * @param out Receives the cell ids; cleared first.
     * @cost O(cellCount) memory for the visited set, plus the brush size in time.
     */
    void collectBrush(HexSphereCell center, std::int32_t radius, std::vector<HexSphereCell>& out) const;

    /** @brief Raises or lowers every cell within `radius` steps of `center`. */
    [[nodiscard]] Result<void> editElevation(HexSphereCell center, std::int32_t radius, std::int32_t delta);
    /** @brief Applies `additive` water-level change over a hex brush. */
    [[nodiscard]] Result<void> editWaterLevel(HexSphereCell center, std::int32_t radius, std::int32_t delta);
    /** @brief Paints a terrain palette index over a hex brush. */
    [[nodiscard]] Result<void> editTerrainType(HexSphereCell center, std::int32_t radius, std::int32_t terrainType);
    /** @brief Applies `additive` change to one feature level (`0` urban, `1` farm, `2` plant). */
    [[nodiscard]] Result<void> editFeatureLevel(HexSphereCell center, std::int32_t radius, std::int32_t feature,
                                                std::int32_t delta);

    // --- cell authoring -----------------------------------------------------

    /** @brief Sets a cell's elevation, clamped to the editable range, and refreshes dependents. */
    [[nodiscard]] Result<void> setElevation(HexSphereCell c, std::int32_t elevation);
    /** @brief Sets a cell's water level, clamped to `[0, kMaxElevation]`. */
    [[nodiscard]] Result<void> setWaterLevel(HexSphereCell c, std::int32_t waterLevel);
    /** @brief Sets a cell's terrain palette index, clamped to the palette range. */
    [[nodiscard]] Result<void> setTerrainType(HexSphereCell c, std::int32_t terrainType);
    /** @brief Sets a cell's urban level, clamped to `[0, 3]`. */
    [[nodiscard]] Result<void> setUrbanLevel(HexSphereCell c, std::int32_t level);
    /** @brief Sets a cell's farm level, clamped to `[0, 3]`. */
    [[nodiscard]] Result<void> setFarmLevel(HexSphereCell c, std::int32_t level);
    /** @brief Sets a cell's plant level, clamped to `[0, 3]`. */
    [[nodiscard]] Result<void> setPlantLevel(HexSphereCell c, std::int32_t level);
    /** @brief Sets a cell's special-feature index; ignored while the cell carries a river. */
    [[nodiscard]] Result<void> setSpecialIndex(HexSphereCell c, std::int32_t index);
    /** @brief Sets whether a cell is walled. */
    [[nodiscard]] Result<void> setWalled(HexSphereCell c, bool walled);
    /** @brief Sets the explored (fog-of-war) latch of a cell. */
    [[nodiscard]] Result<void> setExplored(HexSphereCell c, bool explored);
    /** @brief Sets whether a cell may ever be explored. */
    [[nodiscard]] Result<void> setExplorable(HexSphereCell c, bool explorable);
    /**
     * @brief Writes a complete cell record verbatim.
     *
     * This is the restore path used by a save format. It deliberately skips the
     * neighbour fix-ups of the individual authoring setters, because a payload
     * carries both sides of every connection; the caller is responsible for a
     * consistent payload and must finish with `markAllDirty()`.
     *
     * @return Success, or InvalidArgument when `c` is out of range.
     */
    [[nodiscard]] Result<void> setCellState(HexSphereCell c, HexValues values, HexFlags flags);
    /** @brief Forces a river to leave the cell through `direction`, mirroring the neighbour. */
    [[nodiscard]] Result<void> setOutgoingRiver(HexSphereCell c, std::int32_t direction);
    /** @brief Removes every river connection of the cell and its neighbours. */
    [[nodiscard]] Result<void> removeRiver(HexSphereCell c);
    /** @brief Adds a road through `direction` when both cells can carry one. */
    [[nodiscard]] Result<void> addRoad(HexSphereCell c, std::int32_t direction);
    /** @brief Removes every road of the cell and its neighbours. */
    [[nodiscard]] Result<void> removeRoads(HexSphereCell c);

    // --- dirty tracking -----------------------------------------------------

    /** @brief Marks every cell dirty. */
    void markAllDirty() noexcept;
    /** @brief Marks one cell and its direct neighbours dirty. */
    void markCellDirtyAndNeighbors(HexSphereCell cell) noexcept;
    /**
     * @brief Pops the next dirty cell.
     *
     * @return The dirty cell id, or `kNoHexSphereCell` when nothing is pending.
     * @note Callers loop until `kNoHexSphereCell` is returned; the returned cell is
     *       no longer reported dirty until it is mutated again.
     */
    [[nodiscard]] std::int32_t takeDirtyCell() noexcept;
    /** @brief Number of cells currently pending a rebuild. */
    [[nodiscard]] std::int32_t dirtyCellCount() const noexcept {
        return static_cast<std::int32_t>(dirtyQueue_.size() - dirtyHead_);
    }

private:
    void         markCellDirty(HexSphereCell cell) noexcept;
    void         refreshCellDependents(HexSphereCell cell) noexcept;
    void         validateRivers(HexSphereCell cell) noexcept;
    /**
     * @brief Mutable cell record for an in-place edit.
     * @ownership Borrowed; the map owns the record.
     * @lifetime Valid until this map is destroyed or `reset` is called.
     */
    HexCellData* mutableCell(HexSphereCell cell) noexcept;

    HexSphereTopology         topology_{};
    HexNoise                  noise_{};
    std::vector<HexCellData>  cells_;
    std::vector<std::uint8_t> cellDirty_;
    std::vector<std::int32_t> dirtyQueue_;
    std::size_t               dirtyHead_    = 0;
    std::uint64_t             revision_     = 0;
    float                     sphereRadius_ = 0.f;
    float                     elevationStep_ = 0.f;
};

}  // namespace eve::hexmap
