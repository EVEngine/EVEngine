#pragma once
#include "common/Export.h"


/** @file HexSphereGenerator.h @brief Procedural terrain generator for a spherical hex map. */

#include "common/Result.h"
#include "hexmap/HexSphereMap.h"

#include <cstdint>

namespace eve::hexmap {

/**
 * @brief Tunables of the spherical generator.
 *
 * The planar generator works in the map's own XZ plane, so its noise field, its
 * plate seeding and its river carving all have a coordinate frame to live in. A
 * sphere has none: this generator therefore samples a **3D** noise field along each
 * cell's own direction, which is the only formulation that does not pinch at the
 * poles or seam at the date line.
 */
struct HexSphereGeneratorSettings {
    /** @brief Deterministic seed; the same seed always yields the same planet. */
    std::uint32_t seed = 0u;
    /** @brief Target share of cells that end up above the water line, in percent. */
    std::int32_t landPercentage = 45;
    /** @brief Cells at or below this elevation are flooded. */
    std::int32_t waterLevel = 0;
    /** @brief fBm octaves of the continent field. */
    std::int32_t octaves = 5;
    /** @brief Continent field frequency; higher makes more, smaller land masses. */
    float frequency = 1.55f;
};

/**
 * @brief Regenerates every cell of `map` procedurally.
 *
 * Overwrites the whole cell storage: elevation, water level, terrain type and
 * special index. Rivers, roads, walls and the fog latches are left as they are, so
 * a caller that wants a clean planet must `reset` the map first (which is what the
 * module's generate path does).
 *
 * The result is a pure function of the map's topology, its radius and the settings,
 * so two runs of the same settings produce identical cells.
 *
 * @param map Map to fill; must not be empty.
 * @param settings Generator tunables.
 * @return Success, or InvalidArgument when the map has no cells.
 * @cost One 3D fBm evaluation per cell for the continents and one more for the
 *       moisture, so proportional to the cell count and dominated by the noise.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> generateSphereMap(HexSphereMap&                     map,
                                                                const HexSphereGeneratorSettings& settings);

}  // namespace eve::hexmap
