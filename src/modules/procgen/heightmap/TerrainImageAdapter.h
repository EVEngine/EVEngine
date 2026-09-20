#pragma once
#include <array>
#include <memory>
#include <vector>
#include "common/Result.h"
namespace ssq {
class Table;
}
namespace eve::image {
class ImageData;
}
namespace eve::procgen {
class Heightmap;
class TerrainSplatmap;
struct TerrainImageMaskSettings;
enum class TerrainMaskBlend;
/** @brief GTS snow albedo controls copied from GTSSnowSettings. */
struct GtsSnowSurfaceSettings {
    bool enabled = false;
    float power = 1, minimumHeight = 100, blendRange = 20, slopeBlend = 20, age = 0, scale = 1;
    float colorR = 1, colorG = 1, colorB = 1;
    float normalStrength = 3;
    std::array<float, 4> maskRemapMin{0, 0, 0, 0}, maskRemapMax{1, 1, 3, 1};
    float heightContrast = 1, heightBrightness = 1, heightIncrease = 0;
    float displacementContrast = 1, displacementBrightness = 0.4F, displacementIncrease = 0.1F;
    float tessellationAmount = 25;
    /** @brief Set finite packed snow-mask remap minima in RGBA order. */
    void setMaskRemapMin(float r, float g, float b, float a) { maskRemapMin = {r, g, b, a}; }
    /** @brief Set finite packed snow-mask remap maxima in RGBA order. */
    void setMaskRemapMax(float r, float g, float b, float a) { maskRemapMax = {r, g, b, a}; }
};
/** @brief GTS rain albedo controls copied from GTSRainSettings. */
struct GtsRainSurfaceSettings {
    bool enabled = false;
    float power = 1, minimumHeight = 0, maximumHeight = 3000, darkness = 0.2F;
    float speed = 1, smoothness = 0.8F, scale = 2;
};
/** @brief GTS terrain colormap controls from GTSColorMapSettings. */
struct GtsColorMapSettings {
    float alphaIntensity = 1, colorIntensity = 1, nearIntensity = 1, farIntensity = 1;
};
/** @brief GTS three-scale macro color variation controls from GTSVariationSettings. */
struct GtsMacroVariationSettings {
    float sizeA = 10, sizeB = 10, sizeC = 10, intensity = 0.5F;
    bool objectSpace = false;
};
/** @brief GTS height-axis geological overlay controls from GTSGeoSettings. */
struct GtsGeologicalSettings {
    bool enabled = false, objectSpace = false;
    float nearStrength = 0.2F, nearNormalStrength = 0.5F, nearScale = 50, nearOffset = 0;
    float farStrength = 0.2F, farNormalStrength = 0.5F, farScale = 200, farOffset = 0;
};
/** @brief GTS near/far detail-normal controls from GTSDetailSettings. */
struct GtsDetailNormalSettings {
    bool enabled = true, objectSpace = false;
    float nearTiling = 100, nearStrength = 0.6F, farTiling = 1000, farStrength = 0.8F;
};
/** @brief Ordered linear RGBA colors used to visualize terrain splat layers. */
class EVENGINE_API_DOMAINS TerrainSplatPalette {
public:
    /** @brief Append one finite normalized layer color and return the palette size. */
    [[nodiscard]] Result<int> addColor(float red, float green, float blue, float alpha = 1);
    /** @brief Return the number of layer colors. */
    [[nodiscard]] int getColorCount() const noexcept { return static_cast<int>(colors_.size()); }
private:
    friend EVENGINE_API_DOMAINS Result<int> bakeTerrainSplatAlbedo(image::ImageData&, const TerrainSplatmap&,
                                              const TerrainSplatPalette&);
    std::vector<std::array<float, 4>> colors_;
};
/** @brief One GTS packed texture-array layer's planar sampling and material controls. */
struct GtsPackedLayerSettings {
    bool triPlanar = false, stochastic = false;
    float tileSizeX = 8, tileSizeZ = 8, offsetX = 0, offsetZ = 0;
    float triPlanarSizeX = 1, triPlanarSizeZ = 1;
    float tintR = 1, tintG = 1, tintB = 1, normalStrength = 1;
    float aoMin = 0, smoothnessMin = 0, aoMax = 1, smoothnessMax = 1;
    float geoAmount = 1, detailAmount = 1;
    float heightContrast = 1, heightBrightness = 1, heightIncrease = 0;
    float displacementContrast = 0.1F, displacementBrightness = 0.1F, displacementIncrease = 0;
    float tessellationAmount = 25;
};
/** @brief Owning ordered GTS packed albedo/height and normal/AO/smoothness texture layers. */
class EVENGINE_API_DOMAINS GtsPackedLayerSet {
public:
    GtsPackedLayerSet();
    ~GtsPackedLayerSet();
    GtsPackedLayerSet(GtsPackedLayerSet&&) noexcept;
    GtsPackedLayerSet& operator=(GtsPackedLayerSet&&) noexcept;
    GtsPackedLayerSet(const GtsPackedLayerSet&) = delete;
    GtsPackedLayerSet& operator=(const GtsPackedLayerSet&) = delete;
    /** @brief Copy and append one readable packed texture pair and its finite settings. */
    [[nodiscard]] Result<int> addLayer(const image::ImageData& albedoHeight,
                                       const image::ImageData& normalMask,
                                       const GtsPackedLayerSettings& settings);
    /** @brief Return the number of owned packed layers. */
    int getLayerCount() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend EVENGINE_API_DOMAINS Result<int> bakeGtsPackedLayers(image::ImageData&, image::ImageData&, Heightmap&, Heightmap&,
                                           const Heightmap&, const TerrainSplatmap&, const GtsPackedLayerSet&,
                                           double, double, double, double, double);
    friend EVENGINE_API_DOMAINS Result<int> bakeGtsPackedLayerDisplacement(Heightmap&, Heightmap&, const Heightmap&,
                                                       const TerrainSplatmap&, const GtsPackedLayerSet&,
                                                       double, double, double, double, double, double,
                                                       double, double, double);
};
/**
 * @brief Bake planar, stochastic or triplanar GTS packed layers using one normalized splatmap.
 * @param albedo Matching writable color output.
 * @param packedNormal Matching writable tangent-normal, AO and smoothness output.
 * @param geoStrength Matching mutable geological-strength raster.
 * @param detailStrength Matching mutable detail-strength raster.
 * @param heights Borrowed local heights used for world positions and triplanar weights.
 * @param splatmap Borrowed authoritative normalized layer weights.
 * @param layers Borrowed owning packed layer set in splat order.
 * @param originX World terrain origin X.
 * @param originY World terrain elevation.
 * @param originZ World terrain origin Z.
 * @param spacingX Positive world X sample spacing.
 * @param spacingZ Positive world Z sample spacing.
 * @return Total changed samples or InvalidArgument; all four outputs remain unchanged on failure.
 * @thread Synchronous exclusive output access; no borrow survives the call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsPackedLayers(image::ImageData& albedo,
                                              image::ImageData& packedNormal,
                                              Heightmap& geoStrength,
                                              Heightmap& detailStrength,
                                              const Heightmap& heights,
                                              const TerrainSplatmap& splatmap,
                                              const GtsPackedLayerSet& layers,
                                              double originX = 0, double originY = 0, double originZ = 0,
                                              double spacingX = 1, double spacingZ = 1);
/**
 * @brief Bake GTS planar top-four layer displacement and tessellation with its camera cutoff.
 * @param displacement Matching mutable scalar displacement output.
 * @param tessellation Matching mutable scalar tessellation output.
 * @param heights Borrowed matching local terrain heights used to form world positions.
 * @param splatmap Borrowed authoritative normalized layer weights.
 * @param layers Borrowed owning packed layer set in splat order.
 * @param cameraX Explicit world camera X.
 * @param cameraY Explicit world camera Y.
 * @param cameraZ Explicit world camera Z.
 * @param tessellationMultiplier Finite final multiplier corresponding to GTS _TessellationMultiplier.
 * @param originX World X of sample (0,0).
 * @param originY World elevation added to local heights.
 * @param originZ World Z of sample (0,0).
 * @param spacingX Positive world X sample spacing.
 * @param spacingZ Positive world Z sample spacing.
 * @return Total changed samples or InvalidArgument; both outputs remain unchanged on failure.
 * @thread Synchronous exclusive output access; inputs are borrowed only for the call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsPackedLayerDisplacement(
    Heightmap& displacement, Heightmap& tessellation, const Heightmap& heights,
    const TerrainSplatmap& splatmap, const GtsPackedLayerSet& layers,
    double cameraX, double cameraY, double cameraZ, double tessellationMultiplier = 1,
    double originX = 0, double originY = 0, double originZ = 0,
    double spacingX = 1, double spacingZ = 1);
/**
 * @brief Read a borrowed ImageData into scalar RGBA planes and apply the image-mask kernel atomically.
 * @param target Exclusively borrowed destination, same shape as input; may alias input or curve.
 * @param input Borrowed base mask.
 * @param image Borrowed decoded image with readable pixel format and complete storage.
 * @param curve Borrowed one-row strength curve.
 * @param settings Native image filter and UV controls.
 * @param mode Mask combination mode.
 * @return Changed count or InvalidArgument; invalid storage, pixels or settings leave target unchanged.
 * @throws std::bad_alloc Target remains unchanged.
 * @thread Synchronous; caller holds exclusive target access and prevents concurrent image/input mutation.
 * No image reference, raw pixel pointer, callback or GPU resource is retained. Pixel values use
 * ImageData's format conversion only; no extra gamma conversion or implicit vertical flip occurs.
 */
[[nodiscard]] Result<int> generateTerrainImageMaskFromImage(Heightmap& target, const Heightmap& input,
                                                            const image::ImageData& image, const Heightmap& curve,
                                                            const TerrainImageMaskSettings& settings,
                                                            TerrainMaskBlend                mode);
/**
 * @brief Blend every splat layer color into a borrowed ImageData atomically.
 * @return Written pixel count or InvalidArgument; output is unchanged on failure.
 * @ownership No image, splatmap or palette storage is retained. Caller serializes mutable output access.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeTerrainSplatAlbedo(image::ImageData& output, const TerrainSplatmap& splatmap,
                                                 const TerrainSplatPalette& palette);
/**
 * @brief Generate GTS camera-distance near/far blend values for a terrain raster.
 * @param output Exclusively borrowed matching destination.
 * @param heights Borrowed local terrain heights.
 * @param cameraX World camera X.
 * @param cameraY World camera Y.
 * @param cameraZ World camera Z.
 * @param blendDistance Nonnegative GTS global blendDistance.
 * @param blendRange Positive GTS global blendRange exponent.
 * @param originX World X of sample (0,0).
 * @param originY World terrain elevation added to heights.
 * @param originZ World Z of sample (0,0).
 * @param spacingX Positive X sample spacing.
 * @param spacingZ Positive Z sample spacing.
 * @return Changed sample count or InvalidArgument; failure leaves output unchanged.
 * @thread Synchronous; no reference survives the call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> generateGtsGlobalBlendDistance(Heightmap& output, const Heightmap& heights,
                                                          double cameraX, double cameraY, double cameraZ,
                                                          double blendDistance, double blendRange,
                                                          double originX = 0, double originY = 0,
                                                          double originZ = 0, double spacingX = 1,
                                                          double spacingZ = 1);
/**
 * @brief Blend the GTS terrain colormap into an existing albedo before weather layers.
 * @param output Exclusively borrowed matching albedo, atomically replaced on success.
 * @param colorMap Borrowed matching terrain colormap sampled in terrain UV space.
 * @param globalBlendDistance Borrowed matching normalized near-to-far blend raster.
 * @param settings GTS alpha, color and near/far intensity controls.
 * @return Changed pixel count or InvalidArgument; failure leaves output unchanged.
 * @thread Synchronous; no reference or pointer survives the call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsColorMapAlbedo(image::ImageData& output, const image::ImageData& colorMap,
                                                const Heightmap& globalBlendDistance,
                                                const GtsColorMapSettings& settings);
/**
 * @brief Apply GTS three-scale repeating macro variation after weather layers.
 * @param output Exclusively borrowed albedo, atomically replaced on success.
 * @param variationMap Borrowed repeating variation texture; its red channel drives modulation.
 * @param settings GTS scale, intensity and coordinate-space controls.
 * @param originX World X of output sample (0,0); ignored in object space.
 * @param originZ World Z of output sample (0,0); ignored in object space.
 * @param spacingX Positive X sample spacing.
 * @param spacingZ Positive Z sample spacing.
 * @return Changed pixel count or InvalidArgument; failure leaves output unchanged.
 * @thread Synchronous; no reference or pointer survives the call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsMacroVariationAlbedo(image::ImageData& output,
                                                       const image::ImageData& variationMap,
                                                       const GtsMacroVariationSettings& settings,
                                                       double originX = 0, double originZ = 0,
                                                       double spacingX = 1, double spacingZ = 1);
/**
 * @brief Bake the GTS near/far height-axis geological color and tangent normal overlay atomically.
 * @param albedo Exclusively borrowed matching terrain albedo.
 * @param packedNormal Exclusively borrowed matching texture with tangent XY in RG, AO in B and smoothness in A.
 * @param heights Borrowed local terrain heights.
 * @param layerStrength Borrowed normalized geological layer strength raster.
 * @param globalBlendDistance Borrowed normalized near-to-far blend raster.
 * @param geoAlbedo Borrowed repeating geological color strip.
 * @param geoNormal Borrowed repeating DXT5nm geological normal strip.
 * @param settings GTS near/far scale, offset, color/normal strength and coordinate-space controls.
 * @param originY World elevation added when objectSpace is false.
 * @return Total changed output samples or InvalidArgument; both outputs remain unchanged on failure.
 * @thread Synchronous exclusive output access; no input is retained.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsGeologicalSurface(image::ImageData& albedo,
                                                    image::ImageData& packedNormal,
                                                    const Heightmap& heights,
                                                    const Heightmap& layerStrength,
                                                    const Heightmap& globalBlendDistance,
                                                    const image::ImageData& geoAlbedo,
                                                    const image::ImageData& geoNormal,
                                                    const GtsGeologicalSettings& settings,
                                                    double originY = 0);
/**
 * @brief Bake GTS near/far detail normals, albedo micro-shadowing and snow-detail influence atomically.
 * @param albedo Exclusively borrowed matching terrain albedo.
 * @param packedNormal Exclusively borrowed matching packed terrain normal.
 * @param detailGreyscale Matching scalar output consumed by the subsequent weather pass.
 * @param layerStrength Borrowed normalized detail-layer strength raster.
 * @param globalBlendDistance Borrowed normalized near-to-far blend raster.
 * @param detailNormal Borrowed repeating RG detail-normal texture.
 * @param settings GTS tiling, strength and coordinate-space controls.
 * @param originX World X of sample (0,0), ignored in object space.
 * @param originZ World Z of sample (0,0), ignored in object space.
 * @param spacingX Positive X sample spacing.
 * @param spacingZ Positive Z sample spacing.
 * @return Total changed samples or InvalidArgument; all three outputs remain unchanged on failure.
 * @thread Synchronous exclusive output access; no input is retained.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsDetailSurface(image::ImageData& albedo,
                                               image::ImageData& packedNormal,
                                               Heightmap& detailGreyscale,
                                               const Heightmap& layerStrength,
                                               const Heightmap& globalBlendDistance,
                                               const image::ImageData& detailNormal,
                                               const GtsDetailNormalSettings& settings,
                                               double originX = 0, double originZ = 0,
                                               double spacingX = 1, double spacingZ = 1);
/**
 * @brief Bake the GTS snow and rain albedo branches over an existing terrain image.
 * @param output Exclusively borrowed readable/writable base albedo, atomically replaced on success.
 * @param heights Borrowed matching world-height raster used for altitude and slope masks.
 * @param snowAlbedo Borrowed repeating snow color texture.
 * @param snowMask Borrowed readable companion GTS mask texture; its channels affect PBR outputs, not this albedo-only pass.
 * @param snow Snow height, slope, age, power, scale and tint controls.
 * @param rain Rain altitude, power and darkness controls; evaluated after snow as in GTS_Functions.hlsl.
 * @param originX World X of height sample (0,0).
 * @param originZ World Z of height sample (0,0).
 * @param spacingX Positive world X spacing used for normals and texture coordinates.
 * @param spacingZ Positive world Z spacing used for normals and texture coordinates.
 * @return Changed pixel count or InvalidArgument; failure leaves output unchanged.
 * @thread Synchronous exclusive output access; all other inputs are immutable and no borrow survives the call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsWeatherAlbedo(image::ImageData& output, const Heightmap& heights,
                                               const image::ImageData& snowAlbedo,
                                               const image::ImageData& snowMask,
                                               const GtsSnowSurfaceSettings& snow,
                                               const GtsRainSurfaceSettings& rain,
                                               double originX = 0, double originZ = 0,
                                               double spacingX = 1, double spacingZ = 1,
                                               const Heightmap* detailGreyscale = nullptr);
/**
 * @brief Bake GTS snow/rain world normals, packed mask, displacement and tessellation atomically.
 * @param normalOutput Matching readable/writable packed texture: tangent normal XY in RG, AO in B, smoothness in A.
 * @param maskOutput Matching readable/writable packed material mask; alpha is smoothness.
 * @param displacement Matching mutable scalar displacement raster.
 * @param tessellation Matching mutable scalar tessellation raster.
 * @param heights Borrowed world-height raster used for terrain normals and altitude masks.
 * @param snowNormal Borrowed repeating DXT5nm-style snow normal texture.
 * @param snowMask Borrowed repeating packed snow mask texture.
 * @param rainData Borrowed repeating four-channel rain phase texture.
 * @param snow Snow profile controls.
 * @param rain Rain profile controls.
 * @param timeSeconds Explicit finite animation time; no hidden clock is read.
 * @param originX World X of sample (0,0).
 * @param originZ World Z of sample (0,0).
 * @param spacingX Positive world X spacing.
 * @param spacingZ Positive world Z spacing.
 * @return Total changed output samples or InvalidArgument; all four outputs remain unchanged on failure.
 * @thread Synchronous exclusive access to outputs; no input or pointer is retained.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> bakeGtsWeatherPbr(image::ImageData& normalOutput, image::ImageData& maskOutput,
                                            Heightmap& displacement, Heightmap& tessellation,
                                            const Heightmap& heights, const image::ImageData& snowNormal,
                                            const image::ImageData& snowMask, const image::ImageData& rainData,
                                            const GtsSnowSurfaceSettings& snow,
                                            const GtsRainSurfaceSettings& rain, double timeSeconds,
                                            double originX = 0, double originZ = 0,
                                            double spacingX = 1, double spacingZ = 1);
/**
 * @brief Combine Pcg Mask Map Export channel rasters into one RGBA image.
 * @param output Exclusively borrowed writable destination; replaced atomically.
 * @param red Borrowed readable source whose red component supplies output R.
 * @param green Borrowed readable source whose red component supplies output G.
 * @param blue Borrowed readable source whose red component supplies output B.
 * @param alpha Borrowed readable source whose red component supplies output A.
 * @param activeChannels Four-bit RGBA mask; inactive channels become zero.
 * @return Changed pixel count or InvalidArgument; failure leaves output unchanged.
 * @thread Synchronous exclusive output access; input storage is not retained and no callbacks run.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> combinePcgMaskMapChannels(
    image::ImageData& output, const image::ImageData& red, const image::ImageData& green,
    const image::ImageData& blue, const image::ImageData& alpha, std::uint32_t activeChannels = 15);

/**
 * @brief Scale and copy one Pcg local mask-map result into a combined atlas rectangle.
 * @param output Exclusively borrowed writable atlas, replaced atomically.
 * @param tile Borrowed readable local terrain result.
 * @param destinationX Left atlas pixel.
 * @param destinationY Top atlas pixel.
 * @param destinationWidth Positive destination width.
 * @param destinationHeight Positive destination height.
 * @return Changed atlas pixel count or InvalidArgument; failure leaves output unchanged.
 * @thread Synchronous exclusive output access; tile storage is not retained and no callbacks run.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<int> placePcgMaskMapTileInto(
    image::ImageData& output, const image::ImageData& tile, int destinationX, int destinationY,
    int destinationWidth, int destinationHeight);

/** @brief Register the full-host ImageData adapter; VM-thread only, no retained table reference. */
EVENGINE_API_DOMAINS void exposeTerrainImageAdapter(ssq::Table& table);
}  // namespace eve::procgen
