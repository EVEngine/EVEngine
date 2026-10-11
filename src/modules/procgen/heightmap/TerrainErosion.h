#pragma once

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
struct TerrainWaterSettings;

/**
 * @brief Effective sediment coefficients from Hydraulic.compute, after UI conversion.
 * The source hardcodes hardness to zero, disabling dissolution; capacity and dissolve
 * controls therefore have no finite contribution and are not duplicated here.
 */
struct TerrainSedimentSettings {
    float effect = -1, depositRate = 0.00004F;
    float bankDeposit = 1, bedDeposit = 5;
};

/** @brief Controls Pcg HeightMap.ErodeHydraulic's legacy CPU erosion loop. */
struct TerrainLegacyHydraulicSettings {
    int   iterations = 1;
    int   rainFrequency = 1;
    float sedimentDissolveRate = 0.1F;
};

/**
 * @brief Apply Pcg HeightMap.ErodeHydraulic's rain, four-way water and sediment loop atomically.
 * @param heights Exclusively borrowed finite normalized terrain, modified and clamped to [0,1].
 * @param sediment Exclusively borrowed matching finite signed sediment, distinct from all inputs.
 * @param hardness Borrowed matching finite [0,1] resistance map; it may alias either output.
 * @param rain Borrowed matching finite nonnegative periodic rainfall; it may alias either output.
 * @param settings Nonnegative iterations, positive rain frequency and dissolve rate in [0,1].
 * @return Cells where height or sediment changed, or InvalidArgument; failure preserves both outputs.
 * @throws std::bad_alloc Both outputs remain unchanged.
 * @thread Synchronous caller-owned access; inputs are snapshotted and no references, callbacks, RNG or clock remain.
 * Each iteration adds rain when iteration modulo rainFrequency is zero, accumulates four-way flux with TIME=0.2,
 * updates water, moves newly dissolved material to every strictly lower 8-neighbor in proportion to height drop,
 * evaporates 1/rainFrequency, and clears the sediment delta. The unused source water-diff raster is omitted.
 */
[[nodiscard]] Result<int> applyTerrainLegacyHydraulic(Heightmap& heights, Heightmap& sediment,
                                                       const Heightmap& hardness, const Heightmap& rain,
                                                       const TerrainLegacyHydraulicSettings& settings);

/** @brief Thresholds and iteration count for Pcg HeightMap.Erode's synchronous cardinal transport. */
struct TerrainLegacyDistributedErosionSettings {
    float minimumThreshold = 0;
    float maximumThreshold = 1;
    int   iterations = 1;
};

/**
 * @brief Apply Pcg HeightMap.Erode's synchronous four-neighbor material redistribution.
 * @param heights Exclusively borrowed finite terrain raster.
 * @param settings Finite thresholds in [0,1], minimum below maximum, and nonnegative iterations.
 * @return Changed samples or InvalidArgument; failure preserves heights.
 * @throws std::bad_alloc Heights remains unchanged.
 * @thread Synchronous caller-owned access; deterministic with no callbacks, RNG, clock or retained references.
 * Border cells are sources only indirectly: every iteration scans interior cells, moves half the greatest accepted
 * drop from each source, distributes it across all lower cardinal neighbors by relative drop, then publishes the diff.
 */
[[nodiscard]] Result<int> applyTerrainLegacyDistributedErosion(
    Heightmap& heights, const TerrainLegacyDistributedErosionSettings& settings);

/** @brief Thresholds and iteration count for Pcg HeightMap.ErodeThermal's in-place steepest transport. */
struct TerrainLegacySteepestErosionSettings {
    int   iterations = 1;
    float talusMinimum = 0;
    float talusMaximum = 1;
};

/**
 * @brief Apply Pcg HeightMap.ErodeThermal's ordered steepest-cardinal transport with hardness.
 * @param heights Exclusively borrowed finite terrain raster.
 * @param hardness Borrowed finite [0,1] raster of any positive size; snapshotted before mutation.
 * @param settings Finite talus bounds in [0,1], minimum not above maximum, and nonnegative iterations.
 * @return Changed samples or InvalidArgument; failure preserves heights.
 * @throws std::bad_alloc Heights remains unchanged.
 * @thread Synchronous caller-owned access; deterministic X-major/Z-minor in-place traversal, no retained references.
 * Strict-greater tie selection is bottom, left, right, top source order. Hardness uses Pcg's normalized width/depth
 * bilinear sampler; each accepted move transfers half the steepest drop times one minus hardness.
 */
[[nodiscard]] Result<int> applyTerrainLegacySteepestErosion(
    Heightmap& heights, const Heightmap& hardness, const TerrainLegacySteepestErosionSettings& settings);

/**
 * @brief Apply the source sediment reaction and backtrace equations with atomic dual output.
 * @param heights Exclusively borrowed nonnegative finite scalar heights.
 * @param sediment Exclusively borrowed matching finite signed sediment; distinct from heights.
 * @param velocityX Borrowed matching finite velocity raster; may alias either output.
 * @param velocityZ Borrowed matching finite velocity raster; may alias either output.
 * @param water Finite nonnegative dt and positive spacing; other water settings are not used.
 * @param settings Finite effect and nonnegative deposition coefficients.
 * @return Cells where either output changed, or InvalidArgument; failure leaves both unchanged.
 * @throws std::bad_alloc Both outputs remain unchanged.
 * @thread Synchronous exclusive output access, immutable input snapshots, no retained references or callbacks.
 * Retains the source's additive sediment output, dimension-divided backtrace fractions
 * and mixed X/Y sample index. Zero gradients use slope factor zero; out-of-range integer
 * sediment samples are explicitly zero. No conservative transport claim or GPU bit parity.
 * At zero velocity and zero deposit rate, nonnegative sediment doubles, including when dt is zero.
 */
[[nodiscard]] Result<int> applyTerrainSediment(Heightmap& heights, Heightmap& sediment, const Heightmap& velocityX,
                                               const Heightmap& velocityZ, const TerrainWaterSettings& water,
                                               const TerrainSedimentSettings& settings);

/**
 * @brief Explicit simulation controls for the Pcg eight-neighbor thermal kernel.
 * Height differences are multiplied by heightScale before slope comparison. Spacing
 * is in simulation units; reposeSlope is tan(repose angle), not an angle in degrees.
 * dt is the thermal substep duration (Pcg multiplies thermal dt by hydraulic dt).
 * Defaults use unit spacing/height scale and tan(85 degrees), with three substeps.
 */
struct TerrainThermalSettings {
    float spacingX = 1, spacingZ = 1, heightScale = 1;
    float reposeSlope = 11.430052F, dt = 0.00025F;
    int   iterations = 3;
};

/**
 * @brief Apply the source thermal equation, publishing height and sediment together.
 * @param heights Exclusively borrowed nonnegative finite terrain raster, in caller scalar units.
 * @param sediment Exclusively borrowed matching finite signed accumulation raster. Positive
 * delta means removed terrain, negative delta means added terrain. Must not alias heights.
 * @param settings Finite positive spacing/height scale, nonnegative slope/dt/iterations.
 * Zero iterations or dt is identity. Caller injects dt; no wall clock or RNG is used.
 * @return Count of cells where either output changed, or InvalidArgument. Validation,
 * overflow and allocation failure leave both outputs unchanged, including after multiple substeps.
 * @throws std::bad_alloc Both destinations remain unchanged.
 * @thread Synchronous exclusive access to both outputs; no retained borrows or callbacks.
 * Each substep reads a snapshot with clamped edges. Cardinal slope-selected differences
 * have weight 1, diagonals 0.707. Movement is clamp(dt*sum/16,-height/2,height/2).
 * This is the Pcg scalar kernel, not TerrainPipeline's conservative talus redistribution.
 * The signed sediment accumulation balances each cell's height change up to float rounding;
 * terrain-only mass conservation is not implied. No Unity height packing is performed.
 */
[[nodiscard]] Result<int> applyTerrainThermal(Heightmap& heights, Heightmap& sediment,
                                              const TerrainThermalSettings& settings);
}  // namespace eve::procgen
