#pragma once
#include "common/Export.h"

#include "archspace/ArchSpaceTypes.h"
#include "common/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::archspace {

class Document;

/**
 * @brief Raster cell kinds accepted by dungeon/cave grid import (procgen-facing).
 *
 * Values align with GridCellKind so a baked projection can round-trip into import.
 */
enum class ImportCellKind : std::uint8_t { Empty = 0, Floor = 1, Wall = 2, Portal = 3, Solid = 4 };

/**
 * @brief Flat row-major cell grid in level XZ (x grows with column, z with row).
 *
 * @ownership Caller owns the cells buffer for the duration of importCellGrid.
 */
struct CellGridImport {
    int                         width    = 0;
    int                         height   = 0;
    double                      cellSize = 1.0;
    double                      originX  = 0.0;
    double                      originZ  = 0.0;
    std::vector<ImportCellKind> cells;
};

/**
 * @brief Import a dungeon/cave occupancy grid into an existing level.
 *
 * Flood-fills Floor cells into Region zones (AABB polygons). Portal cells are
 * bridges — they do not join regions during flood fill — and become Portal
 * nodes when they touch two distinct Floor regions. Sets document topology to
 * Dungeon when currently Architectural.
 *
 * Does not depend on `procgen` / `map`; callers feed cells from any generator.
 *
 * @param document Mutated in place; on failure the previous state is restored.
 * @param levelId Existing Level node id.
 * @param grid Row-major cells; size must equal width*height.
 * @param idPrefix Prefix for generated region/portal ids (non-empty).
 * @return Applied on success; InvalidArgument/NotFound on bad input.
 * @cost O(cells) flood fill + portal scan; amortize per imported layout.
 */
[[nodiscard]] EVENGINE_API_WORLD eve::Result<void> importCellGrid(Document& document, const std::string& levelId,
                                                                  const CellGridImport& grid,
                                                                  const std::string&    idPrefix = "cell");

}  // namespace eve::archspace
