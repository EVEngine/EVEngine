#pragma once

#include <cstdint>

namespace eve::procgen {
class GeneratorRegistry;
class Grid2D;

/** @brief Register the "level.roguelike" algorithm (idempotent). */
void registerRoguelikeGenerator(GeneratorRegistry &registry);

/**
 * @brief Post-process any generated Grid2D: fill each wall cell's `detail` with an
 * 8-bit neighbour mask describing which adjacent cells are walkable. Useful to
 * add direction-aware wall tiles to levels from other generators.
 * Returns false if the grid has no wall cells (still clears detail).
 */
bool autotileGridInPlace(Grid2D &grid);

/** @brief Produce a fresh seed suitable for regenerating a level (never 0). */
uint32_t randomSeedValue();

}  // namespace eve::procgen
