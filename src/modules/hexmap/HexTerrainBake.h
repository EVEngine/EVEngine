#pragma once
#include "common/Export.h"


/** @file HexTerrainBake.h @brief Terrain bake hexmap applies; procgen produces the payload. */

#include "common/Result.h"
#include "common/Value.h"
#include "hexmap/HexCell.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexSphereMap.h"

#include <cstdint>
#include <vector>

namespace eve::hexmap {

/**
 * @brief Packed terrain produced by a generator and applied onto a hex map.
 *
 * This is the consumer-owned contract: hexmap asks for cells, not for a
 * generator algorithm. Planar bakes fill a rectangular offset grid; sphere
 * bakes fill a Goldberg topology that must already exist on the live map.
 *
 * @note The packed `cells` string in the script Value uses little-endian
 *       `HexValues` then `HexFlags` (eight bytes per cell).
 */
struct HexTerrainBake {
    /** @brief Which live map the bake is allowed to land on. */
    enum class Kind : std::uint8_t { Planar = 0, Sphere = 1 };

    Kind                     kind        = Kind::Planar;
    std::int32_t             cellCountX  = 0;
    std::int32_t             cellCountZ  = 0;
    std::int32_t             cellCount   = 0;
    std::int32_t             subdivision = 0;
    float                    radius      = 0.f;
    std::uint32_t            seed        = 0;
    std::vector<HexCellData> cells;
};

/**
 * @brief Snapshot every cell of a planar map into a bake.
 * @param map Source grid; empty maps produce an empty planar bake.
 * @return A planar bake with the map's size, seed and packed cells.
 */
[[nodiscard]] EVENGINE_API_WORLD HexTerrainBake snapshotHexTerrain(const HexMap& map);

/**
 * @brief Snapshot every cell of a spherical map into a bake.
 * @param map Source sphere; empty maps produce an empty sphere bake.
 * @return A sphere bake with the topology, seed and packed cells.
 */
[[nodiscard]] EVENGINE_API_WORLD HexTerrainBake snapshotHexSphereTerrain(const HexSphereMap& map);

/**
 * @brief Project a bake into the script Result payload.
 * @param bake Source bake.
 * @return Owning Value object with `kind`, dimensions, seed and packed cells.
 */
[[nodiscard]] EVENGINE_API_WORLD Value hexTerrainBakeToValue(const HexTerrainBake& bake);

/**
 * @brief Parse a script bake payload produced by `hexTerrainBakeToValue`.
 * @param value Object with `kind`, dimensions, seed and packed `cells`.
 * @return The bake, or InvalidArgument when the payload is malformed.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<HexTerrainBake> hexTerrainBakeFromValue(const Value& value);

/**
 * @brief Replace a planar map's cells with a bake.
 *
 * Resets the grid to the bake's size and seed first, so terrain perturbation
 * matches the generator seed. The caller owns units, fog and GPU meshes.
 *
 * @param map Target grid.
 * @param bake Planar bake whose cell count matches `cellCountX * cellCountZ`.
 * @return Success, or InvalidArgument when the bake is not planar or the size
 *         is not a legal hex grid.
 * @cost One reset plus one write per cell.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> applyHexTerrain(HexMap& map, const HexTerrainBake& bake);

/**
 * @brief Replace a spherical map's cells with a bake.
 *
 * The live topology must already match the bake (`cellCount` and
 * `subdivision`). This does not rebuild GPU meshes.
 *
 * @param map Target sphere.
 * @param bake Sphere bake.
 * @return Success, or InvalidArgument when the bake is not a sphere or the
 *         live topology does not match.
 * @cost One write per cell, then `markAllDirty`.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> applyHexSphereTerrain(HexSphereMap& map, const HexTerrainBake& bake);

}  // namespace eve::hexmap
