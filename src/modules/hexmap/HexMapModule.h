#pragma once

/** @file HexMapModule.h @brief Script-facing hex map module: one editable map plus its chunk meshes. */

#include "common/Module.h"
#include "common/Result.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMapMesh.h"
#include "hexmap/HexSearch.h"
#include "hexmap/HexSerializer.h"
#include "hexmap/HexUnits.h"
#include "hexmap/HexVisibility.h"

#include <cstdint>
#include <vector>

namespace eve::graphics {
class Graphics;
class Mesh;
}  // namespace eve::graphics

namespace eve::hexmap {

/**
 * @brief Owns the active hex map and the GPU meshes generated from it.
 *
 * The module exposes exactly one editable map to scripts (the reference
 * hex-map editor also edits a single grid). `newGrid` replaces it and releases
 * the meshes generated from the previous map.
 *
 * Ownership and lifetime:
 * - The module owns `HexMap` and every `graphics::Mesh` it created.
 * - `chunkMeshAt` returns a borrowed mesh; a `Renderable3D` may reference it
 *   only while this module is alive and the grid has not been replaced.
 * - `releaseMeshes` must be called with the same `Graphics` device that created
 *   the meshes, before that device is destroyed.
 *
 * Thread affinity: main/render thread only; no callbacks are invoked and no
 * lock is held across an external call.
 */
class HexMapModule : public Module {
public:
    Module_REG(HexMapModule);

    /** @brief Constructs an empty module with no map and no meshes. */
    HexMapModule();
    /** @brief Destructor; GPU meshes must already have been released. */
    ~HexMapModule() override;

    /**
     * @brief Replaces the active grid and releases the meshes of the previous one.
     *
     * @param gfx Graphics device used to release previously created meshes.
     * @param cellCountX Columns; must be a positive multiple of the chunk size.
     * @param cellCountZ Rows; must be a positive multiple of the chunk size.
     * @param seed Deterministic seed for the cell noise field.
     * @return Success, or InvalidArgument for an unsupported size.
     * @cost Releases every owned mesh and allocates `cellCountX * cellCountZ` cells;
     *       proportional to the map area, amortized over a whole-map rebuild.
     */
    [[nodiscard]] Result<void> newGrid(graphics::Graphics* gfx, std::int32_t cellCountX, std::int32_t cellCountZ,
                                       std::uint32_t seed);

    /**
     * @brief Rebuilds every chunk surface currently marked dirty.
     *
     * @param gfx Graphics device that owns the chunk meshes.
     * @return Number of chunk surfaces that were (re)generated.
     * @cost One CPU mesh build plus one vertex-buffer update per dirty surface.
     */
    std::int32_t rebuildDirtyChunks(graphics::Graphics* gfx);

    /**
     * @brief Rebuilds all surfaces of one chunk.
     *
     * @param gfx Graphics device that owns the chunk meshes.
     * @param chunkIndex Chunk to regenerate.
     * @return Number of surfaces that produced geometry.
     * @cost One CPU mesh build plus one vertex-buffer update per surface.
     */
    std::int32_t rebuildChunk(graphics::Graphics* gfx, std::int32_t chunkIndex);

    /**
     * @brief Borrowed GPU mesh of one chunk surface.
     *
     * @param chunkIndex Chunk index in `[0, chunkCount())`.
     * @param surface Surface stream to query.
     * @return The mesh, or null when that surface has no geometry or was never built.
     * @ownership Borrowed; the module retains ownership until `releaseMeshes`.
     */
    [[nodiscard]] graphics::Mesh* chunkMeshAt(std::int32_t chunkIndex, HexSurface surface) const;

    /**
     * @brief Releases every mesh owned by the module.
     * @param gfx Device that created them; null releases nothing.
     * @note The script must call this before the grid is replaced by a different
     *       device or before shutdown; `newGrid` only releases through itself.
     */
    void releaseMeshes(graphics::Graphics* gfx);

    /** @brief The active map. */
    [[nodiscard]] const HexMap& map() const noexcept { return map_; }
    /** @brief The active map (mutable, for internal build steps). */
    [[nodiscard]] HexMap& map() noexcept { return map_; }
    /** @brief Whether a grid has been created. */
    [[nodiscard]] bool hasGrid() const noexcept { return !map_.empty(); }

    /** @brief Fog-of-war counters of the active map. */
    [[nodiscard]] HexVisibility& visibility() noexcept { return visibility_; }
    /** @brief Units of the active map. */
    [[nodiscard]] HexUnitRegistry& units() noexcept { return units_; }
    /** @brief Search scratch sized to the active map. */
    [[nodiscard]] HexSearchContext& scratch() noexcept { return scratch_; }

    /**
     * @brief Replaces the active grid with an already-decoded map.
     *
     * The load path: the payload was decoded into `restored` before this call, so
     * the module only has to release the meshes of the previous grid, adopt the
     * new one and rebuild its derived state (scratch, fog of war, mesh slots).
     * The caller must follow it with a whole-map rebuild, because every chunk of
     * the adopted grid starts dirty and no renderable points at the new meshes.
     *
     * @param gfx Device that owns the current meshes; required.
     * @param restored Decoded grid, moved into the module.
     * @param units Unit states to restore on the adopted grid.
     * @return Success, or InvalidArgument when the device or unit states are invalid.
     * @cost Releases every owned mesh and allocates the derived state for the new grid.
     */
    [[nodiscard]] Result<void> adoptGrid(graphics::Graphics* gfx, HexMap&& restored,
                                         const std::vector<HexUnitState>& units);

    /**
     * @brief Re-sizes the search scratch to the active map.
     * @cost Proportional to the cell count; called by `newGrid` and by load.
     */
    void syncScratch();

    /**
     * @brief Regenerates the active grid procedurally and drops every unit.
     *
     * The grid is rebuilt from `seed` first, because the terrain perturbation has
     * to match the generation seed; that releases every mesh, unit and visibility
     * counter of the previous grid. The caller must rebuild every chunk afterwards
     * and place new units: a generated map has **no explored cells**, so nothing is
     * visible until a unit grants vision.
     *
     * @param gfx Device that owns the current meshes; required.
     * @param seed Deterministic seed for both the grid noise and the generator.
     * @param landPercentage Target share of land, in percent; clamped by the generator.
     * @param waterLevel Cells at or below this elevation are flooded; clamped by the generator.
     * @param riverPercentage Target share of the map covered by rivers, in percent; clamped by the generator.
     * @return Success, or InvalidArgument when there is no grid or no device, or a
     *         failure forwarded from the generator.
     * @cost Releases every owned mesh and allocates the whole grid; proportional to the cell count.
     */
    [[nodiscard]] Result<void> generateMap(graphics::Graphics* gfx, std::uint32_t seed, std::int32_t landPercentage,
                                           std::int32_t waterLevel, std::int32_t riverPercentage);

private:
    [[nodiscard]] std::int32_t meshSlot(std::int32_t chunkIndex, HexSurface surface) const noexcept;
    void                       clearMeshSlots() noexcept;

    HexMap                       map_{};
    std::vector<graphics::Mesh*> meshes_{};
    std::vector<std::uint8_t>    meshPopulated_{};
    HexSearchContext             scratch_{};
    HexVisibility                visibility_{};
    HexUnitRegistry              units_{};
};

}  // namespace eve::hexmap
