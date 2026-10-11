#pragma once

#include "common/Result.h"

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

/**
 * @brief Write an eight-neighbour occupancy mask into every non-empty cell's detail value.
 * @param grid Dense grid mutated in place; empty cells receive detail zero.
 * @return Success after writing all masks; InvalidArgument for unusable dimensions.
 * @thread Caller-owned grid only; not safe for concurrent mutation.
 */
[[nodiscard]] eve::Result<void> autotileOccupiedGridInPlace(Grid2D &grid);

/** @brief Produce a fresh seed suitable for regenerating a level (never 0). */
uint32_t randomSeedValue();

}  // namespace eve::procgen
