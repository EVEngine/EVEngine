#pragma once

#include "common/Result.h"

namespace eve::procgen {
class Heightmap;
struct TerrainStampSettings;
enum class TerrainMaskBlend;

/** @brief Concavity source controls; featureSize is in height texels, concavity is signed. */
struct TerrainConcavitySettings {
    float featureSize = 10, concavity = 1;
};

/**
 * @brief Generate source normalized-gradient concavity with border attenuation and indexed curve lookup.
 * @param target Exclusively borrowed finite destination, same size as input; input aliases permitted.
 * @param input Borrowed finite base mask.
 * @param heights Borrowed finite height raster, mapped by heightWidth/inputWidth on both axes.
 * @param curve Borrowed finite nonempty one-row curve, read at floor(clampedValue*(width-1)).
 * @param settings Positive finite featureSize representable as uint32, and finite signed concavity.
 * @param mode Multiply, maximum, minimum, add or subtract; no hidden strength/inversion.
 * @return Changed count or InvalidArgument, atomically. Mapped coordinates must fit uint32.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access, immutable snapshots, no callbacks or retained references.
 * Source unsigned neighbor arithmetic and inclusive width/height upper clamps are retained;
 * out-of-range integer samples and zero-length normalized gradients explicitly return zero.
 * Integer neighbor offset truncates featureSize; border attenuation uses its untruncated value.
 */
[[nodiscard]] Result<int> generateTerrainConcavityMask(Heightmap& target, const Heightmap& input,
                                                       const Heightmap& heights, const Heightmap& curve,
                                                       const TerrainConcavitySettings& settings, TerrainMaskBlend mode);

/** @brief Radial curvature controls; radius is UV distance, worldUnits scales signed height-minus-blur. */
struct TerrainCurvatureSettings {
    float radius = 0.001F, worldUnits = -400, intensity = 0.7F;
    int   steps = 32, directions = 16;
};

/**
 * @brief Radially blur heights, derive source curvature, transform its curve and combine with an input mask.
 * @param target Exclusively borrowed finite destination, same dimensions as input; may alias any input.
 * @param input Borrowed finite mask.
 * @param heights Borrowed finite height texture; independent resolution, Clamp/Bilinear sampling.
 * @param curve Borrowed finite nonempty one-row strength curve.
 * @param settings Finite nonnegative radius, signed worldUnits, positive intensity and sample counts;
 * steps*directions must be less than INT_MAX. Negative bases with fractional intensity are rejected.
 * @param mode Source pass order. Minimum uses curveValue < 1-input, not ordinary min.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access, immutable snapshots, no callbacks or retained references.
 * Curvature=clamp(abs(pow((height-blur)*worldUnits,intensity)),0,1), then curve and mode.
 * No strength or inversion is applied by this shader. Integer-indexed radial sampling defines
 * counts explicitly instead of reproducing GPU floating-loop drift; no GPU bit parity claim.
 */
[[nodiscard]] Result<int> generateTerrainCurvatureMask(Heightmap& target, const Heightmap& input,
                                                       const Heightmap& heights, const Heightmap& curve,
                                                       const TerrainCurvatureSettings& settings, TerrainMaskBlend mode);

/**
 * @brief Ordered radial Grow/Shrink followed by the reference strength curve.
 * @param target Exclusively borrowed finite destination, same size as source; aliases allowed.
 * @param source Borrowed finite mask sampled with Clamp/Bilinear texture-center coordinates.
 * @param curve Borrowed finite nonempty one-row strength lookup, applied even at zero distance.
 * @param distance Signed UV radius (world distance divided by operation range); positive grows.
 * Twice the absolute radius times source width must fit int for indexed sampling.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access, immutable snapshots, no callbacks or retained references.
 * X is the outer loop, Z the inner; both step by 1/sourceWidth. Each candidate blends
 * toward the current accumulator using radial smoothstep before max/min selection.
 * Integer-indexed double offsets resolve shader float-loop drift explicitly; no GPU bit parity claim.
 */
[[nodiscard]] Result<int> growShrinkTerrainMask(Heightmap& target, const Heightmap& source, const Heightmap& curve,
                                                float distance);

/** @brief StrengthTransform pass order; source pass 0 named Multiply actually replaces with the curve value. */
enum class TerrainStrengthMode { Replace, Maximum, Minimum, Add, Subtract };

/**
 * @brief Apply the reference strength curve, inversion, mode and final interpolation.
 * @param target Exclusively borrowed finite raster, same shape as source; input aliases permitted.
 * @param source Borrowed finite scalar input.
 * @param curve Borrowed finite nonempty one-row Clamp/Bilinear lookup.
 * @param mode Curve replacement, max, min, addition or subtraction before interpolation.
 * @param strength Finite weight in [0,1]. Output is not clamped.
 * @param invert Invert the sampled curve value before the mode operation.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access, immutable input snapshots, no retained borrows/callbacks.
 */
[[nodiscard]] Result<int> applyTerrainStrength(Heightmap& target, const Heightmap& source, const Heightmap& curve,
                                               TerrainStrengthMode mode, float strength, bool invert);

/**
 * @brief Apply Pcg's two-pass Smooth ImageMask operation to a scalar mask.
 * @param target Exclusively borrowed finite destination matching input; aliases are permitted.
 * @param input Borrowed finite mask to smooth.
 * @param verticality Finite selector in [-1,1]: -1 only lowers, 0 averages, 1 only raises.
 * @param blurRadius Finite nonnegative texel stride used by each of seven weighted tap pairs.
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access; no state, callback, time or RNG is retained.
 * The vertical pass deliberately uses the X texel size, matching SmoothHeight.shader on rectangular rasters.
 * The shader computes but does not apply HeightTransformTex, so this operation has no curve or blend stage.
 */
[[nodiscard]] Result<int> smoothTerrainMask(Heightmap& target, const Heightmap& input, float verticality,
                                            float blurRadius);

/** @brief Paint-context affine UV mappings, evaluated at destination pixel centers. */
struct TerrainBrushBlendSettings {
    float heightXX = 1, heightXZ = 0, heightZX = 0, heightZZ = 1;
    float heightOffsetX = 0, heightOffsetZ = 0;
    float brushXX = 1, brushXZ = 0, brushZX = 0, brushZZ = 1;
    float brushOffsetX = 0, brushOffsetZ = 0;
    float strength = 1;
};

/** @brief World X/Z positions of height sample centers, with positive spacing and dimensions. */
struct TerrainSampleGrid {
    double originX = 0, originZ = 0, spacingX = 1, spacingZ = 1;
    int    width = 1, height = 1;
};

/**
 * @brief Configure paint-context UV matrices from world sample centers and a rotated stamp footprint.
 * @param target Exclusively borrowed settings; strength is preserved, matrices publish atomically.
 * @param world Stamp grid origin/spacing describes context sample centers; center/width/depth/rotation describes
 * the brush. Height-operation/amplitude/baseHeight/blendStrength fields are unused.
 * @param contextWidth Positive destination sample count along X.
 * @param contextHeight Positive destination sample count along Z.
 * @param heights Source height texture's world sample grid; dimensions match the texture supplied to blendBrush.
 * @return Number of changed matrix coefficients, or InvalidArgument without mutation.
 * @thread Synchronous exclusive target access, immutable value inputs, no retained references or callbacks.
 * Positive finite spacing/extents and finite coordinates are required. World origins are subtracted
 * in double before float conversion. Half-texel offsets align world sample centers with texture centers.
 * Result coefficients must fit finite floats; float UV arithmetic has its usual precision limits.
 */
[[nodiscard]] Result<int> configureTerrainBrushWorld(TerrainBrushBlendSettings&  target,
                                                     const TerrainStampSettings& world, int contextWidth,
                                                     int contextHeight, const TerrainSampleGrid& heights);

/**
 * @brief Blend an erosion scalar output with old heights through transformed brush UVs.
 * @param target Exclusively borrowed finite destination; may alias any input.
 * @param oldHeights Finite scalar old-height texture; independent resolution permitted.
 * @param newHeights Finite scalar erosion channel; independent resolution permitted.
 * @param brush Finite scalar brush texture; independent resolution permitted.
 * @param settings Finite affine coefficients and strength. UV=(XX*u+XZ*v+offsetX, ZX*u+ZZ*v+offsetZ).
 * @return Changed count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access; immutable input snapshots, no retained borrows/callbacks.
 * Uses Clamp/Bilinear texel-center sampling. Outside inclusive brush [0,1]^2 the sampled old
 * height is retained. Inside, weight=strength*brush, without saturation, as SimpleHeightBlend.
 * Inputs/outputs are native scalars; Unity packed-height encoding is not applied.
 */
[[nodiscard]] Result<int> blendTerrainBrush(Heightmap& target, const Heightmap& oldHeights, const Heightmap& newHeights,
                                            const Heightmap& brush, const TerrainBrushBlendSettings& settings);

/**
 * @brief Compose spatial erosion-output blending and the source's inverted strength filter atomically.
 * @param target Exclusively borrowed destination; aliases with inputs permitted.
 * @param oldHeights Borrowed old terrain scalar texture.
 * @param erosion Borrowed already-computed sediment, right flux or X velocity scalar texture.
 * @param brush Borrowed input mask used as spatial blend weight.
 * @param spatial Explicit PaintContext mappings and brush strength.
 * @param curve Borrowed one-row strength curve.
 * @param mode StrengthTransform pass, with Replace corresponding to source Multiply.
 * @param strength Final filter weight in [0,1].
 * @param userInvert User-facing inversion; the source filter receives its negation.
 * @return Changed count or InvalidArgument; both stages publish once, failure leaves target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access and immutable borrowed inputs; no callbacks or retained references.
 * Simulation is supplied separately; this operation does not advance or mutate a water field.
 */
[[nodiscard]] Result<int> generateTerrainErosionMask(Heightmap& target, const Heightmap& oldHeights,
                                                     const Heightmap& erosion, const Heightmap& brush,
                                                     const TerrainBrushBlendSettings& spatial, const Heightmap& curve,
                                                     TerrainStrengthMode mode, float strength, bool userInvert);

/** @brief Distance coordinates used by the Pcg distance-mask shader. */
enum class TerrainDistanceAxis { Circle, X, Z, RoundedSquare };

/** @brief Value-only UV transform; distances are relative to the target raster, rotation in radians. */
struct TerrainDistanceMaskSettings {
    float               offsetX = 0, offsetZ = 0;
    float               scaleX = 1, scaleZ = 1;
    float               rotation = 0, roundness = 0.5F;
    bool                tiling = false;
    TerrainDistanceAxis axis   = TerrainDistanceAxis::Circle;
};

/**
 * @brief Transform scalar input through a clamped bilinear one-row curve texture.
 * @param target Borrowed destination, same dimensions as source; may alias any input.
 * @param source Borrowed finite scalar raster; UV = its value, clamped to [0,1].
 * @param curve Borrowed nonempty one-row texture. Texel centers are (i+0.5)/width,
 * matching GPU texture sampling (not interpolation between endpoint knots).
 * @return Changed sample count or InvalidArgument; failure leaves target unchanged.
 * @throws std::bad_alloc Target is unchanged on allocation failure.
 * @thread Synchronous, exclusively owned target; immutable inputs, no retained borrows or callbacks.
 */
[[nodiscard]] Result<int> transformTerrainMask(Heightmap& target, const Heightmap& source, const Heightmap& curve);

/**
 * @brief Generate height/range fitness with smoothstep followed by filter and strength curves.
 * @param target Borrowed destination, same dimensions as source; may alias inputs.
 * @param source Borrowed scalar raster in caller-defined units (world height or normalized slope).
 * @param minimum Finite lower range edge.
 * @param maximum Finite upper edge, strictly greater than minimum.
 * @param filterCurve Borrowed one-row Clamp/Bilinear texture, sampled at smoothstep(minimum,maximum,source).
 * @param strengthCurve Borrowed one-row Clamp/Bilinear texture, sampled at filterCurve output.
 * @return Changed sample count or InvalidArgument, atomically; no output clamping after curves.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access; no retained borrows, RNG, time or callbacks.
 */
[[nodiscard]] Result<int> generateTerrainRangeMask(Heightmap& target, const Heightmap& source, float minimum,
                                                   float maximum, const Heightmap& filterCurve,
                                                   const Heightmap& strengthCurve);

/**
 * @brief Derive the shader's slope metric 1-normal.y from world-scaled height gradients.
 * @param target Borrowed destination, same size as heights; may alias heights.
 * @param heights Borrowed finite height grid. Interior differences are centered;
 * edges use one-sided differences, singleton axes have zero derivative. For tiled
 * derivatives supply a halo and crop the result; no absent neighbor is invented.
 * @param spacingX Positive finite X sample spacing.
 * @param spacingZ Positive finite Z sample spacing.
 * @param heightScale Finite multiplier converting stored heights to world units.
 * @return Changed sample count or InvalidArgument; failure is atomic. Output is [0,1].
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous exclusive target access, no retained borrows or callbacks.
 * This derives normals from heights; imported Unity normals may yield different gradients.
 */
[[nodiscard]] Result<int> deriveTerrainSlope(Heightmap& target, const Heightmap& heights, float spacingX,
                                             float spacingZ, float heightScale = 1);

/**
 * @brief Generate a transformed distance mask using circle, X, Z or rounded-square coordinates.
 * @param target Borrowed destination; UVs are output pixel centers. Inputs may alias target.
 * @param settings Finite UV transform with nonzero scales and strictly positive roundness.
 * @param filterCurve Borrowed one-row Clamp/Bilinear distance curve.
 * @param strengthCurve Borrowed one-row Clamp/Bilinear strength curve.
 * @return Changed sample count or InvalidArgument, atomically. Rounded-square samples
 * outside [0,1]^2 use filter value zero before strength transformation; other modes clamp
 * curve lookups. Repeated UV coordinates wrap, including negative coordinates.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Exclusive target, immutable input snapshots; synchronous, no callbacks/RNG/time.
 */
[[nodiscard]] Result<int> generateTerrainDistanceMask(Heightmap& target, const TerrainDistanceMaskSettings& settings,
                                                      const Heightmap& filterCurve, const Heightmap& strengthCurve);
}  // namespace eve::procgen
