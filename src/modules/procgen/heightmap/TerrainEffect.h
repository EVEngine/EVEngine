#pragma once
#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
/**
 * @brief Apply the nine-tap Pcg contrast equation to native scalar heights.
 * @param target Exclusively borrowed finite destination; also the source snapshot.
 * @param mask Borrowed finite raster of matching dimensions, values in [0,1]; may alias target.
 * @param strength Finite nonnegative blend strength; values above 1 extrapolate (Pcg default is 2).
 * @param featureSize Finite nonnegative sample offset in texels; fractional offsets use clamped bilinear sampling.
 * @return Changed count or InvalidArgument; invalid input/overflow leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access; no retained pointers, callbacks, RNG or clock.
 * Output retains caller units without Unity height packing or output clamping.
 * Cross-tile filtering requires caller-provided neighboring samples (halo then crop).
 */
[[nodiscard]] Result<int> applyTerrainContrast(Heightmap& target, const Heightmap& mask, float strength,
                                               float featureSize);

/** @brief Two-pass smoothing controls; radius in texels, verticality in [-1,1], strength in [0,1]. */
struct TerrainSmoothSettings {
    float radius = 10, verticality = 0, strength = 1;
};

/**
 * @brief Ridge controls in caller height units, with explicit clipping bounds.
 * Defaults match the Pcg packed scalar domain [0,0.5]. World-height callers must
 * supply their bounds. Passes counts actual shader applications: Pcg's UI iteration
 * value N maps to floor(N)+2 passes, so its default 16 maps to 18 here.
 */
struct TerrainRidgeSettings {
    float mixStrength = 0.5F, exponent = 16, strength = 1;
    float minimum = 0, maximum = 0.5F;
    int   passes = 18;
};

/** @brief Terrace frequency in inverse height units, interior bevel in [0,1], strength in [0,1]. */
struct TerrainTerraceSettings {
    float count = 100, bevel = 0, strength = 1;
};

/**
 * @brief Apply Pcg's horizontal then vertical seven-pair weighted smooth passes.
 * @param target Exclusively borrowed finite destination and source snapshot.
 * @param mask Borrowed matching raster in [0,1]; fixed across both passes, including when aliased.
 * @param settings Finite controls, nonnegative radius. Negative verticality only lowers;
 * positive verticality only raises. Vertical radius is radius*height/width, retaining
 * the source shader's use of texelSize.x in both passes on rectangular rasters.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target; no retained borrows, callbacks, RNG or time.
 * Samples use Clamp/Bilinear; tiled callers supply a halo and crop. No height packing.
 */
[[nodiscard]] Result<int> applyTerrainSmooth(Heightmap& target, const Heightmap& mask,
                                             const TerrainSmoothSettings& settings);

/**
 * @brief Apply ordered horizontal/vertical ridge powers, neighbor mixing and per-pass clipping.
 * @param target Exclusively borrowed finite destination and source snapshot.
 * @param mask Borrowed matching unit raster; remains fixed across passes, may alias target.
 * @param settings Finite controls: positive exponent, unit strengths, minimum<maximum,
 * nonnegative passes (zero is identity). Every pass clamps all output to the bounds,
 * including zero-mask samples, matching the shader's final clamp.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target; no retained borrows/callbacks/RNG/time.
 * Edge neighbors clamp; tiled callers require a halo covering the requested passes.
 */
[[nodiscard]] Result<int> applyTerrainRidges(Heightmap& target, const Heightmap& mask,
                                             const TerrainRidgeSettings& settings);

/**
 * @brief Terrace scalar heights using nearest-even rounding and the shader's one-sided bevel.
 * @param target Exclusively borrowed finite destination and source snapshot, no automatic normalization.
 * @param mask Borrowed matching unit raster; may alias target.
 * @param settings Finite controls; positive count, unit bevel and strength.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target; no retained borrows/callbacks/RNG/time.
 */
[[nodiscard]] Result<int> applyTerrainTerrace(Heightmap& target, const Heightmap& mask,
                                              const TerrainTerraceSettings& settings);

/**
 * @brief Apply the Pcg PowerOf control: lerp(height,pow(height,4-power),mask).
 * @param target Exclusively borrowed finite scalar heights; no automatic normalization.
 * @param mask Borrowed matching unit raster; zero mask preserves the sample, may alias target.
 * @param power Finite source UI control, not the exponent. Negative masked heights and
 * zero masked heights with nonpositive exponent are rejected as undefined shader inputs.
 * @return Changed count or InvalidArgument (including overflow); failure is atomic.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target; no retained borrows/callbacks/RNG/time.
 */
[[nodiscard]] Result<int> applyTerrainPower(Heightmap& target, const Heightmap& mask, float power);

/**
 * @brief Mix-height scalar controls; delta scale is maximum-minimum, independent of clipping bounds.
 * Defaults use Pcg's scalar input range [0,1] and packed terrain output [0,0.5].
 * World-height callers must provide consistent delta scale and clipping bounds.
 */
struct TerrainHeightMixSettings {
    float minimum = 0, maximum = 1, midpoint = 0.5F, strength = 0.5F;
    float clipMinimum = 0, clipMaximum = 0.5F;
};

/**
 * @brief Apply the height-transform shader's smoothstep and one-row height curve.
 * @param target Exclusively borrowed finite scalar heights, without hidden normalization.
 * @param mask Borrowed matching unit raster; may alias target.
 * @param curve Borrowed finite nonempty one-row Clamp/Bilinear texture; may alias target.
 * @param minimum Finite smoothstep lower bound.
 * @param maximum Finite upper bound, greater than minimum.
 * @return Changed count or InvalidArgument; overflow/invalid inputs leave target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target; no retained borrows/callbacks/RNG/time.
 * Output is lerp(height,(maximum-minimum)*curve(smoothstep(minimum,maximum,height)),mask).
 * The source shader does not add minimum back to the transformed value.
 */
[[nodiscard]] Result<int> applyTerrainHeightCurve(Heightmap& target, const Heightmap& mask, const Heightmap& curve,
                                                  float minimum, float maximum);

/**
 * @brief Mix centered local brush heights into terrain, then globally mask and clip.
 * @param target Exclusively borrowed finite scalar heights.
 * @param local Borrowed finite matching local brush raster; may alias target or global.
 * @param global Borrowed matching unit raster; may alias target, fixed for the entire operation.
 * @param settings Finite controls: ordered ranges, unit midpoint, nonnegative strength (above 1 allowed).
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target; no retained borrows/callbacks/RNG/time.
 * Computes clamp(height+(local-midpoint)*(maximum-minimum)*strength*global,clipMinimum,clipMaximum).
 * Zero global mask still clips, as in the source shader. Rasters cover the full operation footprint;
 * brush-UV transforms and out-of-footprint preservation belong to the caller's rasterization stage.
 */
[[nodiscard]] Result<int> applyTerrainHeightMix(Heightmap& target, const Heightmap& local, const Heightmap& global,
                                                const TerrainHeightMixSettings& settings);
}  // namespace eve::procgen
