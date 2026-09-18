#pragma once

/** @file HexMap.h @brief Editable hex cell grid: topology, queries, picking and authoring. */

#include "common/Result.h"
#include "hexmap/HexCell.h"
#include "hexmap/HexCoordinates.h"
#include "hexmap/HexMetrics.h"
#include "hexmap/HexNoise.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/** @brief Surface streams a chunk mesh is generated for. */
enum class HexSurface : std::int32_t {
    Terrain = 0,
    Water   = 1,
    River   = 2,
    Road    = 3,
    /**
     * @brief Fog-of-war overlay of a chunk.
     *
     * Unlike the other streams this one depends on `HexVisibility` as well as on
     * the grid, so it is built by `buildFogMesh` and not by `buildChunkSurfaceMesh`.
     */
    Fog     = 4,
    /** @brief City/farm walls, wall towers and bridges; built by `buildWallMesh`. */
    Wall    = 5,
    /** @brief Urban, farm, plant and special decorations; built by `buildFeatureMesh`. */
    Feature = 6,
};

/** @brief Number of chunk surface streams. */
inline constexpr std::int32_t kHexSurfaceCount = 7;

/**
 * @brief Largest grid edge, in cells, any entry point may request.
 *
 * Both the script bindings and the save format check against this before allocating,
 * so the limit has exactly one definition. 512 x 512 cells is the largest map the
 * chunked mesh path is validated for.
 */
inline constexpr std::int32_t kMaxHexGridDimension = 512;

/** @brief Stable identifier of one chunk surface stream. */
struct HexSurfaceId {
    std::int32_t chunkIndex = 0;
    HexSurface   surface    = HexSurface::Terrain;
};

/**
 * @brief An editable, chunked, pointy-top hex map.
 *
 * The grid is rectangular in odd-row offset space: `cellCountX` columns and
 * `cellCountZ` rows, with odd rows shifted half a cell to the right. Cells are
 * partitioned into `chunkSizeX * chunkSizeZ` chunks; every mutation marks the
 * owning chunk (and the chunks of affected neighbours) dirty so a renderer can
 * rebuild only what changed.
 *
 * Ownership and lifetime: `HexMap` owns its cell storage and its noise field and
 * is a plain value type. It performs no I/O, opens no GPU resources and invokes
 * no callbacks, so it is safe to hold on any single thread.
 *
 * Determinism: cell positions, perturbation and any future generation step are
 * derived from the construction seed and the cell coordinates only. Two maps
 * built with the same size and seed produce identical positions.
 */
class HexMap {
public:
    HexMap() = default;

    /**
     * @brief Replaces the grid with a new `cellCountX * cellCountZ` map.
     *
     * Existing cells are discarded and every chunk is marked dirty. Cell count
     * must be a positive multiple of the chunk size in each dimension because
     * chunk meshes own the blend strips on their north and west borders.
     *
     * @param cellCountX Columns; positive multiple of `kChunkSizeX`.
     * @param cellCountZ Rows; positive multiple of `kChunkSizeZ`.
     * @param seed Deterministic seed for the noise field.
     * @return Success, or InvalidArgument when a dimension is out of range.
     */
    [[nodiscard]] Result<void> reset(std::int32_t cellCountX, std::int32_t cellCountZ, std::uint32_t seed);

    /** @brief Whether the map holds any cell. */
    [[nodiscard]] bool empty() const noexcept { return cells_.empty(); }
    /** @brief Number of columns. */
    [[nodiscard]] std::int32_t cellCountX() const noexcept { return cellCountX_; }
    /** @brief Number of rows. */
    [[nodiscard]] std::int32_t cellCountZ() const noexcept { return cellCountZ_; }
    /** @brief Number of chunk columns. */
    [[nodiscard]] std::int32_t chunkCountX() const noexcept { return chunkCountX_; }
    /** @brief Number of chunk rows. */
    [[nodiscard]] std::int32_t chunkCountZ() const noexcept { return chunkCountZ_; }
    /** @brief Total number of chunks. */
    [[nodiscard]] std::int32_t chunkCount() const noexcept { return chunkCountX_ * chunkCountZ_; }
    /** @brief Total number of cells. */
    [[nodiscard]] std::int32_t cellCount() const noexcept { return static_cast<std::int32_t>(cells_.size()); }
    /** @brief The seed this map was built with. */
    [[nodiscard]] std::uint32_t seed() const noexcept { return noise_.seed(); }
    /** @brief Monotonic counter bumped by every mutation; used to detect stale meshes. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Deterministic noise field shared by this map's mesh builders. */
    [[nodiscard]] const HexNoise& noise() const noexcept { return noise_; }

    // --- topology -----------------------------------------------------------

    /**
     * @brief Whether `coordinates` address a cell inside the grid.
     *
     * The grid is the rectangle `[0, cellCountX) x [0, cellCountZ)` in
     * **odd-row offset space**. An axial pair is inside only when its offset
     * column `x + z/2` is in range, so cells with a negative axial X are valid
     * members of the rectangle.
     */
    [[nodiscard]] bool contains(HexCoordinates coordinates) const noexcept {
        const std::int32_t column = coordinates.offsetX();
        return column >= 0 && column < cellCountX_ && coordinates.z >= 0 && coordinates.z < cellCountZ_;
    }
    /** @brief Linear cell index of `coordinates` (offset order), or -1 when outside the grid. */
    [[nodiscard]] std::int32_t indexOf(HexCoordinates coordinates) const noexcept;
    /** @brief Coordinates of a linear cell index; out-of-range indices return `(0, 0)`. */
    [[nodiscard]] HexCoordinates coordinatesAt(std::int32_t index) const noexcept;
    /**
     * @brief Cell record, or null when outside the grid.
     * @ownership Borrowed; the map owns the record.
     * @lifetime Valid until this map is destroyed or `reset` is called; any mutation
     *           of the map's cell storage invalidates the pointer.
     */
    [[nodiscard]] const HexCellData* cell(HexCoordinates coordinates) const noexcept;
    /**
     * @brief Cell record by linear index, or null when out of range.
     * @ownership Borrowed; the map owns the record.
     * @lifetime Valid until this map is destroyed or `reset` is called; any mutation
     *           of the map's cell storage invalidates the pointer.
     */
    [[nodiscard]] const HexCellData* cellAt(std::int32_t index) const noexcept;

    /** @brief Neighbour of `coordinates` in `direction`; false when it is off-grid. */
    [[nodiscard]] bool getNeighbor(HexCoordinates coordinates, HexDirection direction,
                                   HexCoordinates& out) const noexcept;
    /** @brief Relationship between two adjacent cells; Flat when not adjacent or out of range. */
    [[nodiscard]] HexEdgeType edgeTypeTo(HexCoordinates a, HexCoordinates b) const noexcept;

    // --- chunking -----------------------------------------------------------

    /** @brief Chunk index owning `coordinates`, or -1 when outside the grid. */
    [[nodiscard]] std::int32_t chunkIndexOf(HexCoordinates coordinates) const noexcept;
    /** @brief Chunk column of a chunk index. */
    [[nodiscard]] std::int32_t chunkColumnOf(std::int32_t chunkIndex) const noexcept;
    /** @brief Chunk row of a chunk index. */
    [[nodiscard]] std::int32_t chunkRowOf(std::int32_t chunkIndex) const noexcept;
    /**
     * @brief Coordinates of the cell at `(column, row)` inside a chunk.
     *
     * Chunks are squares in **odd-row offset space**, so every mesh builder must
     * enumerate cells through this helper rather than constructing axial
     * coordinates from the loop indices directly.
     *
     * @param chunkIndex Chunk to address.
     * @param column Column inside the chunk, `[0, kChunkSizeX)`.
     * @param row Row inside the chunk, `[0, kChunkSizeZ)`.
     * @return The cell's axial coordinates (not range checked).
     */
    [[nodiscard]] HexCoordinates chunkCell(std::int32_t chunkIndex, std::int32_t column,
                                           std::int32_t row) const noexcept;
    /** @brief World-space centre of a chunk, at the first cell's elevation. */
    [[nodiscard]] HexVec3 chunkCenter(std::int32_t chunkIndex) const noexcept;
    /** @brief Marks every chunk dirty. */
    void markAllChunksDirty() noexcept;
    /** @brief Marks one chunk and its direct chunk neighbours dirty. */
    void markChunkDirtyAndNeighbors(std::int32_t chunkIndex) noexcept;

    /**
     * @brief Pops the next dirty chunk.
     *
     * @return The dirty chunk index, or -1 when nothing is pending.
     * @note Callers loop until -1 is returned; the returned chunk is no longer
     *       reported dirty until it is mutated again.
     */
    [[nodiscard]] std::int32_t takeDirtyChunk() noexcept;
    /** @brief Number of chunks currently pending a rebuild. */
    [[nodiscard]] std::int32_t dirtyChunkCount() const noexcept {
        return static_cast<std::int32_t>(dirtyQueue_.size());
    }

    // --- geometry -----------------------------------------------------------

    /** @brief Cell centre including elevation and vertical perturbation. */
    [[nodiscard]] HexVec3 cellPosition(HexCoordinates coordinates) const noexcept;
    /** @brief Cell centre at `y = 0` (no elevation, no perturbation). */
    [[nodiscard]] HexVec3 cellGroundPosition(HexCoordinates coordinates) const noexcept {
        return HexCoordinates::toWorldPosition(coordinates);
    }

    /**
     * @brief Finds the cell under a world-space ray.
     *
     * The map has no collision geometry, so the ray is marched against the
     * elevation plane of the candidate cell and refined until it converges.
     *
     * @param rayOrigin Ray origin in world space.
     * @param rayDirection Normalised ray direction in world space.
     * @return The hit cell, or NotFound when the ray misses the grid or points away.
     */
    [[nodiscard]] Result<HexCoordinates> pickCell(HexVec3 rayOrigin, HexVec3 rayDirection) const noexcept;

    // --- cell queries -------------------------------------------------------

    [[nodiscard]] std::int32_t elevation(HexCoordinates c) const noexcept;
    [[nodiscard]] std::int32_t waterLevel(HexCoordinates c) const noexcept;
    [[nodiscard]] std::int32_t terrainType(HexCoordinates c) const noexcept;
    [[nodiscard]] std::int32_t urbanLevel(HexCoordinates c) const noexcept;
    [[nodiscard]] std::int32_t farmLevel(HexCoordinates c) const noexcept;
    [[nodiscard]] std::int32_t plantLevel(HexCoordinates c) const noexcept;
    [[nodiscard]] std::int32_t specialIndex(HexCoordinates c) const noexcept;
    [[nodiscard]] bool         isUnderwater(HexCoordinates c) const noexcept;
    [[nodiscard]] bool         hasRiver(HexCoordinates c) const noexcept;
    [[nodiscard]] bool         hasRoad(HexCoordinates c) const noexcept;
    [[nodiscard]] bool         hasRiverThrough(HexCoordinates c, HexDirection d) const noexcept;
    [[nodiscard]] bool         isWalled(HexCoordinates c) const noexcept;
    /** @brief Whether a cell has ever been seen by a viewer (the fog-of-war latch). */
    [[nodiscard]] bool         isExplored(HexCoordinates c) const noexcept;
    /** @brief Whether a cell may ever be revealed by a viewer. */
    [[nodiscard]] bool         isExplorable(HexCoordinates c) const noexcept;
    /** @brief Full packed value record of a cell (zero when outside the grid). */
    [[nodiscard]] HexValues    values(HexCoordinates c) const noexcept;
    /** @brief Full packed flag record of a cell (zero when outside the grid). */
    [[nodiscard]] HexFlags     flags(HexCoordinates c) const noexcept;

    // --- cell authoring -----------------------------------------------------

    /** @brief Sets a cell's elevation, clamped to the editable range, and refreshes dependents. */
    [[nodiscard]] Result<void> setElevation(HexCoordinates c, std::int32_t elevation);
    /** @brief Sets a cell's water level, clamped to `[0, maxElevation]`. */
    [[nodiscard]] Result<void> setWaterLevel(HexCoordinates c, std::int32_t waterLevel);
    /** @brief Sets a cell's terrain palette index, clamped to the palette range. */
    [[nodiscard]] Result<void> setTerrainType(HexCoordinates c, std::int32_t terrainType);
    /** @brief Sets a cell's urban level, clamped to `[0, 3]`. */
    [[nodiscard]] Result<void> setUrbanLevel(HexCoordinates c, std::int32_t level);
    /** @brief Sets a cell's farm level, clamped to `[0, 3]`. */
    [[nodiscard]] Result<void> setFarmLevel(HexCoordinates c, std::int32_t level);
    /** @brief Sets a cell's plant level, clamped to `[0, 3]`. */
    [[nodiscard]] Result<void> setPlantLevel(HexCoordinates c, std::int32_t level);
    /** @brief Sets a cell's special-feature index; ignored while the cell carries a river. */
    [[nodiscard]] Result<void> setSpecialIndex(HexCoordinates c, std::int32_t index);
    /** @brief Sets whether a cell is walled. */
    [[nodiscard]] Result<void> setWalled(HexCoordinates c, bool walled);
    /** @brief Sets the explored (fog-of-war) latch of a cell. */
    [[nodiscard]] Result<void> setExplored(HexCoordinates c, bool explored);
    /** @brief Sets whether a cell may ever be explored. */
    [[nodiscard]] Result<void> setExplorable(HexCoordinates c, bool explorable);
    /**
     * @brief Writes a complete cell record verbatim.
     *
     * This is the restore path used by the save format. It deliberately skips the
     * neighbour fix-ups of the individual authoring setters (`setOutgoingRiver`,
     * `addRoad`, ...) because a payload carries both sides of every connection;
     * the caller is responsible for a consistent payload and must finish with
     * `markAllChunksDirty()`.
     *
     * @param c Cell to overwrite.
     * @param values Packed values to store.
     * @param flags Packed flags to store.
     * @return Success, or InvalidArgument when `c` is outside the grid.
     * @cost Marks the owning chunk and its neighbours dirty.
     */
    [[nodiscard]] Result<void> setCellState(HexCoordinates c, HexValues values, HexFlags flags);
    /** @brief Forces a river to leave the cell through `direction`, mirroring the neighbour. */
    [[nodiscard]] Result<void> setOutgoingRiver(HexCoordinates c, HexDirection direction);
    /** @brief Removes every river connection of the cell and its neighbours. */
    [[nodiscard]] Result<void> removeRiver(HexCoordinates c);
    /** @brief Adds a road through `direction` when both cells can carry one. */
    [[nodiscard]] Result<void> addRoad(HexCoordinates c, HexDirection direction);
    /** @brief Removes every road of the cell and its neighbours. */
    [[nodiscard]] Result<void> removeRoads(HexCoordinates c);

    // --- brush authoring ----------------------------------------------------

    /**
     * @brief Raises or lowers every cell within `radius` steps of `center`.
     *
     * @param center Brush centre.
     * @param radius Hex distance reached by the brush; 0 edits only the centre.
     * @param delta Elevation change; clamped per cell to the editable range.
     * @return Success, or InvalidArgument when `center` is outside the grid or `radius` is negative.
     */
    [[nodiscard]] Result<void> editElevation(HexCoordinates center, std::int32_t radius, std::int32_t delta);
    /** @brief Applies `additive` water-level change over a hex brush. */
    [[nodiscard]] Result<void> editWaterLevel(HexCoordinates center, std::int32_t radius, std::int32_t delta);
    /** @brief Paints a terrain palette index over a hex brush. */
    [[nodiscard]] Result<void> editTerrainType(HexCoordinates center, std::int32_t radius, std::int32_t terrainType);
    /** @brief Applies `additive` change to one feature level (`0` urban, `1` farm, `2` plant). */
    [[nodiscard]] Result<void> editFeatureLevel(HexCoordinates center, std::int32_t radius, std::int32_t feature,
                                                std::int32_t delta);

    /** @brief Enumerates the cells of a hex brush into `out` (cleared first). */
    void collectBrush(HexCoordinates center, std::int32_t radius, std::vector<std::int32_t>& out) const;

private:
    void markChunkDirty(std::int32_t chunkIndex) noexcept;
    void refreshCellDependents(HexCoordinates coordinates) noexcept;
    /**
     * @brief Mutable cell record for an in-place edit.
     * @ownership Borrowed; the map owns the record.
     * @lifetime Valid until this map is destroyed or `reset` is called.
     */
    HexCellData* mutableCell(HexCoordinates coordinates) noexcept;
    void         validateRivers(HexCoordinates coordinates) noexcept;

    std::int32_t              cellCountX_  = 0;
    std::int32_t              cellCountZ_  = 0;
    std::int32_t              chunkCountX_ = 0;
    std::int32_t              chunkCountZ_ = 0;
    std::uint64_t             revision_    = 0;
    HexNoise                  noise_{};
    std::vector<HexCellData>  cells_;
    std::vector<std::uint8_t> chunkDirty_;
    std::vector<std::int32_t> dirtyQueue_;
};

}  // namespace eve::hexmap
