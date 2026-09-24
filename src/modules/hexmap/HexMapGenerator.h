#pragma once
#include "common/Export.h"


/** @file HexMapGenerator.h @brief Deterministic procedural map generation for the hex grid. */

#include "common/Result.h"
#include "hexmap/HexMap.h"
#include "hexmap/HexMetrics.h"

#include <cstdint>

namespace eve::hexmap {

/**
 * @brief Tunables of the reference project's map generator.
 *
 * The defaults are the reference `MapGeneratorSettings` defaults, so a call with
 * a bare settings value reproduces the Unity generator's behaviour.
 */
struct HexMapGeneratorSettings {
    /** @brief Deterministic seed; the same seed and grid always produce the same map. */
    std::uint32_t seed = 0u;

    /** @brief Probability that a land seed starts one elevation step higher. */
    float highRiseProbability = 0.25f;
    /** @brief Probability that a land seed starts one elevation step lower. */
    float sinkProbability = 0.2f;
    /** @brief Probability that a region grows by a random block instead of a square. */
    float jitterProbability = 0.25f;

    /** @brief Smallest land-region growth step, in cells. */
    std::int32_t chunkSizeMin = 30;
    /** @brief Largest land-region growth step, in cells. */
    std::int32_t chunkSizeMax = 100;
    /** @brief Target share of the map that ends up above the water level, in percent. */
    std::int32_t landPercentage = 50;
    /** @brief Cells at or below this elevation are flooded. */
    std::int32_t waterLevel = 3;
    /** @brief Lowest elevation the generator writes. */
    std::int32_t elevationMinimum = -2;
    /** @brief Highest elevation the generator writes. */
    std::int32_t elevationMaximum = 8;

    /** @brief Unused border of the map in the X direction, in cells. */
    std::int32_t mapBorderX = 5;
    /** @brief Unused border of the map in the Z direction, in cells. */
    std::int32_t mapBorderZ = 5;
    /** @brief Gap kept between two generated regions, in cells. */
    std::int32_t regionBorder = 5;
    /** @brief Number of independent land regions. */
    std::int32_t regionCount = 1;
    /** @brief Share of the land that is eroded back towards the water level, in percent. */
    std::int32_t erosionPercentage = 50;

    /** @brief Moisture every cell starts with, in `[0, 1]`. */
    float startingMoisture = 0.1f;
    /** @brief Share of a cell's moisture that evaporates before it spreads. */
    float evaporationFactor = 0.5f;
    /** @brief Share of a cell's moisture that rains out as it spreads downwind. */
    float precipitationFactor = 0.25f;
    /** @brief Share of a cell's moisture that runs downhill instead of spreading. */
    float runoffFactor = 0.25f;
    /** @brief Share of a cell's moisture that seeps into the ground. */
    float seepageFactor = 0.25f;
    /** @brief Direction the wind blows towards, used by the moisture model. */
    HexDirection windDirection = HexDirection::NW;
    /** @brief Wind strength, in cells the moisture is pushed per step. */
    float windStrength = 4.f;

    /** @brief Target share of the map covered by rivers, in percent. */
    std::int32_t riverPercentage = 10;
    /** @brief Probability that a low point becomes an extra lake instead of land. */
    float extraLakeProbability = 0.25f;

    /** @brief Temperature at the lowest elevation. */
    float lowTemperature = 0.f;
    /** @brief Temperature at the highest elevation. */
    float highTemperature = 1.f;
    /** @brief Per-cell random spread added to the temperature. */
    float temperatureJitter = 0.1f;
};

/**
 * @brief Replaces the grid's contents with a procedurally generated map.
 *
 * The generator mirrors the reference project's pipeline: land regions grow from
 * seeded cells with jitter and occasional rises/sinks, the coastline is eroded
 * back towards the water level until the target land share is met, rivers are
 * carved downhill, moisture is transported downwind to pick the terrain type,
 * and plant levels follow the moisture.
 *
 * @param map Grid to fill. Its size and seed must already be set by
 *            `HexMap::reset`; the settings' own seed only drives the generator's
 *            internal choices, so two calls with the same settings and the same
 *            grid produce the same map.
 * @param settings Generator tunables.
 * @return Success, or InvalidArgument when the grid is empty.
 * @cost Proportional to the cell count; allocates one scratch value per cell.
 * @note Every cell is left *explorable but unexplored* and the whole map is
 *       marked dirty, so the caller rebuilds every chunk afterwards.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<void> generateHexMap(HexMap& map, const HexMapGeneratorSettings& settings);

}  // namespace eve::hexmap
