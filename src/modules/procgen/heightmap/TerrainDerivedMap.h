#pragma once

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;

/**
 * @brief Read a finite heightmap at an integer coordinate clamped to its nearest border.
 * @return Sample value or InvalidArgument for an empty/nonfinite raster.
 * @thread Synchronous immutable access; no retained reference or callback.
 */
[[nodiscard]] Result<float> sampleTerrainHeightmapSafe(const Heightmap& source, int x, int z);

/**
 * @brief Read Pcg HeightMap's normalized bilinear sample using x*width and z*depth coordinates.
 * @param source Borrowed finite nonempty raster.
 * @param x Normalized finite X in [0,1].
 * @param z Normalized finite Z in [0,1].
 * @return Interpolated value or InvalidArgument; x/z equal to one select the last sample.
 * @thread Synchronous immutable access; no retained reference or callback.
 */
[[nodiscard]] Result<float> sampleTerrainHeightmapNormalized(const Heightmap& source, float x, float z);

/** @brief Return whether a heightmap contains a valid positive-size sample array. */
[[nodiscard]] Result<bool> terrainHeightmapHasData(const Heightmap& source);
/** @brief Return whether both positive heightmap dimensions are powers of two; malformed storage fails. */
[[nodiscard]] Result<bool> terrainHeightmapIsPowerOfTwo(const Heightmap& source);

/** @brief Pcg HeightMap.CurvatureMap modes in source enum order. */
enum class TerrainHeightmapCurvature { Average = 0, Horizontal = 1, Vertical = 2 };
/** @brief Pcg HeightMap.Aspect modes in source enum order. */
enum class TerrainHeightmapAspect { Aspect = 0, Northerness = 1, Easterness = 2 };
/** @brief Pcg HeightMap neighborhood mutation modes. */
enum class TerrainHeightmapNeighborhood { DeNoise = 0, GrowEdges = 1, ShrinkEdges = 2 };
/** @brief Pcg HeightMap pointwise arithmetic operations. */
enum class TerrainHeightmapArithmetic { Add = 0, Subtract = 1, Multiply = 2, Divide = 3 };
/** @brief Pcg HeightMap whole-raster value transforms. */
enum class TerrainHeightmapTransform { Invert = 0, Normalise = 1, Power = 2, Contrast = 3 };
/** @brief Pcg HeightMap.Copy conditional modes in source enum order. */
enum class TerrainHeightmapCopy { Always = 0, IfLess = 1, IfGreater = 2 };
/** @brief Pcg HeightMap read-only scalar measurements. */
enum class TerrainHeightmapMeasure { Minimum = 0, Maximum = 1, Sum = 2, Average = 3, BaseLevel = 4 };
/** @brief Pcg HeightMap point slope query variants. */
enum class TerrainHeightmapSlopeQuery { GridForward = 0, NormalizedCentral = 1, NormalizedAverage = 2 };

/**
 * @brief Derive Pcg HeightMap.CurvatureMap's normalized differential curvature.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite raster with both dimensions at least two.
 * @param mode Average, horizontal or vertical curvature.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous caller-owned access; no callbacks or retained references.
 */
[[nodiscard]] Result<int> generateTerrainHeightmapCurvature(Heightmap& target, const Heightmap& source,
                                                             TerrainHeightmapCurvature mode);

/**
 * @brief Derive Pcg HeightMap.Aspect's aspect, northerness, or easterness raster.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite raster with both dimensions at least two.
 * @param mode Full normalized aspect, northerness, or easterness.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous caller-owned access; no callbacks or retained references.
 */
[[nodiscard]] Result<int> generateTerrainHeightmapAspect(Heightmap& target, const Heightmap& source,
                                                          TerrainHeightmapAspect mode);

/**
 * @brief Apply Pcg HeightMap.DeNoise, GrowEdges, or ShrinkEdges in source traversal order.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite input copied before mutation.
 * @param radius Nonnegative square-neighborhood radius; DeNoise requires at least one.
 * @param mode Clamp outliers, grow toward neighbor maximum, or shrink toward neighbor minimum.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous caller-owned access; no callbacks or retained references.
 * The candidate is intentionally updated in X-major/Y-minor order, so later samples observe earlier writes.
 */
[[nodiscard]] Result<int> filterTerrainHeightmapNeighborhood(Heightmap& target, const Heightmap& source, int radius,
                                                              TerrainHeightmapNeighborhood mode);

/**
 * @brief Apply Pcg HeightMap.Smooth's in-place four-neighbor passes.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite input copied before mutation.
 * @param iterations Nonnegative pass count.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @thread Synchronous caller-owned access; no retained references.
 */
[[nodiscard]] Result<int> smoothTerrainHeightmap(Heightmap& target, const Heightmap& source, int iterations);

/**
 * @brief Apply Pcg HeightMap.SmoothRadius's scaled sliding-window filter, including its first-row omission.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite input copied before mutation.
 * @param radius Nonnegative radius that Pcg promotes to at least five.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @thread Synchronous caller-owned access; no retained references.
 */
[[nodiscard]] Result<int> smoothTerrainHeightmapRadius(Heightmap& target, const Heightmap& source, int radius);

/**
 * @brief Apply Pcg HeightMap.Convolve with an odd square kernel and source-order in-place feedback.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite input copied before mutation.
 * @param kernel Borrowed finite odd-square convolution kernel.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @thread Synchronous caller-owned access; no retained references.
 */
[[nodiscard]] Result<int> convolveTerrainHeightmap(Heightmap& target, const Heightmap& source, const Heightmap& kernel);

/**
 * @brief Generate Pcg HeightMap.SlopeMap's normalized gradient magnitude.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite raster with both dimensions at least two.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @thread Synchronous caller-owned access; no retained references.
 */
[[nodiscard]] Result<int> generateTerrainHeightmapSlope(Heightmap& target, const Heightmap& source);

/**
 * @brief Quantize every sample using Pcg HeightMap.Quantize's Mathf.Round rule.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite input.
 * @param divisor Finite nonzero quantization interval.
 * @return Changed samples or InvalidArgument; failure preserves target.
 * @thread Synchronous caller-owned access; no retained references.
 */
[[nodiscard]] Result<int> quantizeTerrainHeightmap(Heightmap& target, const Heightmap& source, float divisor);

/**
 * @brief Apply a scalar Pcg HeightMap arithmetic operation with optional clamping.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite input.
 * @param operand Finite scalar right operand; division requires nonzero.
 * @param operation Arithmetic operation in Pcg source order.
 * @param clampResult Whether to clamp each result to minValue and maxValue.
 * @param minValue Finite lower clamp bound.
 * @param maxValue Finite upper clamp bound, not less than minValue.
 * @return Changed samples or InvalidArgument; failure preserves target.
 */
[[nodiscard]] Result<int> applyTerrainHeightmapScalarArithmetic(Heightmap& target, const Heightmap& source,
                                                                 float operand, TerrainHeightmapArithmetic operation,
                                                                 bool clampResult, float minValue, float maxValue);

/**
 * @brief Apply a raster Pcg HeightMap arithmetic operation, resampling a differently sized operand.
 * @param target Exclusively borrowed matching output; it may alias either input.
 * @param source Borrowed finite left operand matching target.
 * @param operand Borrowed finite right operand, sampled with Pcg normalized coordinates when dimensions differ.
 * @param operation Arithmetic operation in Pcg source order.
 * @param clampResult Whether to clamp each result to minValue and maxValue.
 * @param minValue Finite lower clamp bound.
 * @param maxValue Finite upper clamp bound, not less than minValue.
 * @return Changed samples or InvalidArgument; failure preserves target.
 */
[[nodiscard]] Result<int> applyTerrainHeightmapRasterArithmetic(Heightmap& target, const Heightmap& source,
                                                                 const Heightmap& operand,
                                                                 TerrainHeightmapArithmetic operation,
                                                                 bool clampResult, float minValue, float maxValue);

/**
 * @brief Lerp source toward values under a mask using Pcg's clamped Mathf.Lerp semantics.
 * @param target Exclusively borrowed matching output; it may alias any input.
 * @param source Borrowed finite starting raster matching target.
 * @param values Borrowed finite destination raster, resampled when dimensions differ.
 * @param mask Borrowed finite interpolation mask, resampled when dimensions differ.
 * @return Changed samples or InvalidArgument; failure preserves target.
 */
[[nodiscard]] Result<int> lerpTerrainHeightmap(Heightmap& target, const Heightmap& source, const Heightmap& values,
                                               const Heightmap& mask);

/**
 * @brief Apply Pcg Invert, Normalise, Power, or Contrast to a finite heightmap.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite input.
 * @param transform Transform in source-family order.
 * @param parameter Exponent for Power or factor for Contrast; ignored by Invert and Normalise.
 * @return Changed samples or InvalidArgument; failure preserves target.
 */
[[nodiscard]] Result<int> transformTerrainHeightmap(Heightmap& target, const Heightmap& source,
                                                     TerrainHeightmapTransform transform, float parameter);

/**
 * @brief Copy or conditionally merge a Pcg heightmap, resampling when dimensions differ.
 * @param target Exclusively borrowed authoritative destination and comparison baseline.
 * @param source Borrowed finite source; it may alias target.
 * @param mode Always copy, copy if source is less, or copy if source is greater.
 * @return Changed samples or InvalidArgument; failure preserves target.
 */
[[nodiscard]] Result<int> copyTerrainHeightmap(Heightmap& target, const Heightmap& source,
                                                TerrainHeightmapCopy mode);

/**
 * @brief Copy a Pcg heightmap with inclusive output clamping and normalized resampling.
 * @param target Exclusively borrowed destination; it may alias source.
 * @param source Borrowed finite source.
 * @param minValue Finite lower bound.
 * @param maxValue Finite upper bound, not less than minValue.
 * @return Changed samples or InvalidArgument; failure preserves target.
 */
[[nodiscard]] Result<int> copyTerrainHeightmapClamped(Heightmap& target, const Heightmap& source, float minValue,
                                                       float maxValue);

/**
 * @brief Transpose a Pcg HeightMap exactly as HeightMap.Flip does.
 * @param target Exclusively borrowed destination; it may alias source and is resized to source height by source width.
 * @param source Borrowed finite source.
 * @return Changed samples, counting every sample when dimensions change, or InvalidArgument.
 */
[[nodiscard]] Result<int> flipTerrainHeightmap(Heightmap& target, const Heightmap& source);

/**
 * @brief Measure Pcg height range, sum, average, or scanner base level from current samples.
 * @param source Borrowed finite source.
 * @param measure Requested measurement.
 * @return Scalar measurement or InvalidArgument.
 */
[[nodiscard]] Result<double> measureTerrainHeightmap(const Heightmap& source, TerrainHeightmapMeasure measure);

/**
 * @brief Apply Pcg's curve-driven Quantize overload using one sampled curve per raster row.
 * @param target Exclusively borrowed matching output; it may alias source.
 * @param source Borrowed finite heights covered by the terrace starts through height 1.
 * @param startHeights Borrowed N-by-1 strictly increasing terrace starts.
 * @param curves Borrowed curveWidth-by-N sampled curve rows evaluated linearly over [0,1].
 * @return Changed samples or InvalidArgument; failure preserves target.
 */
[[nodiscard]] Result<int> quantizeTerrainHeightmapTerraces(Heightmap& target, const Heightmap& source,
                                                            const Heightmap& startHeights, const Heightmap& curves);

/**
 * @brief Evaluate one of Pcg HeightMap's three point-slope formulas.
 * @param source Borrowed finite source with both dimensions at least two.
 * @param x Grid coordinate for GridForward, normalized coordinate for other modes.
 * @param y Grid coordinate for GridForward, normalized coordinate for other modes.
 * @param mode Point-slope formula.
 * @return Source-formula slope or InvalidArgument.
 */
[[nodiscard]] Result<double> measureTerrainHeightmapSlope(const Heightmap& source, float x, float y,
                                                           TerrainHeightmapSlopeQuery mode);

/** @brief Fill a finite Pcg heightmap with one value clamped to [0,1]. @param target Destination. @param value Fill value. @return Changed samples or InvalidArgument. */
[[nodiscard]] Result<int> fillTerrainHeightmap(Heightmap& target, float value);
/** @brief Set one sample after clamping its coordinates to the nearest border. @param target Destination. @param x X coordinate. @param y Z coordinate. @param value Finite value. @return Changed samples or InvalidArgument. */
[[nodiscard]] Result<int> setTerrainHeightmapSafe(Heightmap& target, int x, int y, float value);
/** @brief Set Pcg's fixed-X row from a one-dimensional strip. @param target Destination. @param rowX Fixed X. @param values Target-height strip. @return Changed samples or InvalidArgument. */
[[nodiscard]] Result<int> setTerrainHeightmapRow(Heightmap& target, int rowX, const Heightmap& values);
/** @brief Set Pcg's fixed-Z column from a one-dimensional strip. @param target Destination. @param columnZ Fixed Z. @param values Target-width strip. @return Changed samples or InvalidArgument. */
[[nodiscard]] Result<int> setTerrainHeightmapColumn(Heightmap& target, int columnZ, const Heightmap& values);
/** @brief Reset a heightmap to Pcg's empty zero-by-zero state. @param target Destination. @return Removed sample count. */
[[nodiscard]] Result<int> resetTerrainHeightmap(Heightmap& target);
}
