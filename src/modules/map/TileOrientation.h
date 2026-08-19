#pragma once

namespace eve::map {

/** @brief Tile map layout: orthogonal, isometric, staggered, or hexagonal. */
enum class MapOrientation { Orthogonal, Isometric, Staggered, Hexagonal };
/** @brief Stagger axis for staggered/hex maps (shift every other row/column). */
enum class StaggerAxis { X, Y };
/** @brief Which rows/columns are offset by half a tile. */
enum class StaggerIndex { Odd, Even };

}  // namespace eve::map
