#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::graphics {
class Texture;
/** @brief Per-fragment normal decoding, evaluated after texture filtering. */
enum class PbrNormalMode : uint32_t {
    TangentXYZ    = 0, /**< Conventional signed XYZ tangent normal. */
    VegetationRG  = 1, /**< TVE RG remap with fixed Z=1. */
    VegetationRAG = 2, /**< TVE R*A and G remap with fixed Z=1. */
    VegetationAG  = 3  /**< TVE A and G remap with fixed Z=1. */
};
/** @brief TVE tangent-space normal treatment for back-facing fragments. */
enum class PbrVegetationBackfaceNormalMode : uint32_t { Flip = 0, Mirror = 1, Same = 2 };
/** @brief Raster face culling requested by a PBR material. */
enum class PbrCullMode : uint32_t { Inherit = 0, None = 1, Back = 2, Front = 3 };
/** @brief Canonical glTF texture roles, with fixed semantic channels (not a UV set limit). */
enum class PbrTextureSlot : uint32_t {
    BaseColor,
    MetallicRoughness,
    Normal,
    Occlusion,
    Emissive,
    Specular,
    SpecularColor,
    Anisotropy,
    Clearcoat,
    ClearcoatRoughness,
    ClearcoatNormal,
    Count
};
/** @brief One borrowed texture plus independently owned UV/sampler parameters.
 * Texture is graphics-factory-owned and must outlive this binding and queued draws.
 */
struct PbrTextureBinding {
    Texture*             texture    = nullptr;
    uint32_t             texcoord   = 0;
    bool                 srgbDecode = true;
    std::array<float, 2> offset{0, 0}, scale{1, 1};
    float                rotation = 0;
    uint32_t             wrapS = 10497, wrapT = 10497, minFilter = 9987, magFilter = 9729;
};
/** @brief Owning vegetation translucency controls, evaluated per light after normal mapping.
 * Intensity zero disables
 * the contribution. Color is linear HDR; intensity,
 * direct, ambient, shadow, maskAmount, and normalDistortion are
 * [0,1], strength
 * is [0,50], and scattering is [1,50]. Global/overlay multipliers are finite
 * nonnegative values.

 * * Values have no external lifetime, callbacks or thread affinity; publishing the
 * containing PbrSurface follows the
 * graphics submission contract.
 */
struct PbrTranslucency {
    std::array<float, 3> color{1, 1, 1};
    float                intensity = 0, strength = 1, normalDistortion = .5f, scattering = 2;
    float                direct = .9f, ambient = .1f, shadow = .5f, maskAmount = 0;
    float                globalIntensity = 1, overlay = 1;
    /** @brief Independent ORM-alpha endpoints in [0,1]. Reversed ranges are valid;
     * maximum-minimum+0.0001 must
     * be nonzero. */
    float maskMinimum = 0, maskMaximum = 0;
};
/** @brief Owning per-fragment TVE surface-stage inputs in linear space.
 * Scalar controls are [0,1]. Overlay RGB is
 * finite nonnegative HDR. Overlay
 * variation uses the mesh-authored variation channel; projection uses the
 *
 * post-normal-map world normal; vertex occlusion can be inverted before its
 * alpha influence is applied. The shader
 * remaps the Colors and Overlay masks through their independent global endpoint pairs,`n * applies overlay color,
 * metallic, smoothness and normal strength, then
 *
 * applies wetness color, smoothness and normal strength. Values retain no
 * borrows or callbacks.
 */
struct PbrVegetationColorStages {
    std::array<float, 4> fieldColor{1, 1, 1, 0};
    std::array<float, 3> overlayColor{1, 1, 1}, vertexOcclusionColor{1, 1, 1};
    float                overlay = 0, wetness = 0, overlayVariation = 0.5f, overlayProjection = 0.5f;
    float                vertexOcclusionAlpha = 0.5019608f, overlayNormalScale = .5f, wetnessNormalScale = .5f;
    float                overlaySmoothness = .5f, wetnessContrast = .5f, overlaySubsurface = .5f;
    float                colorsCoverage = 1, colorsIntensity = 1, colorsMask = 1, colorsVariation = .5f;
    float                globalColorMaskMinimum = .1f, globalColorMaskMaximum = .2f;
    float                globalOverlayMaskMinimum = .1f, globalOverlayMaskMaximum = .2f;
    float                globalAlphaThresholdOffset = 0;
    float                vertexOcclusionMinimum = 0, vertexOcclusionMaximum = 1;
    bool                 invertVertexOcclusion = false, invertVertexOcclusionColors = false;
    PbrVegetationBackfaceNormalMode backfaceNormalMode = PbrVegetationBackfaceNormalMode::Flip;
};
/** @brief TVE secondary material layer evaluated after primary texture sampling.
 * Three borrowed bindings contain
 * secondary albedo, normal and mask data. UV mode 0 uses
 * the primary mesh UV, 1 uses the authored TVE detail UV and
 * 2 projects world XZ. All
 * scalar masks are remapped with the TVE 0.0001 epsilon; reversed endpoints are valid.
 */
struct PbrVegetationDetail {
    std::array<PbrTextureBinding, 3> textures{};
    std::array<float, 4>             color{1, 1, 1, 1}, colorTwo{1, 1, 1, 1};
    std::array<float, 2>             uvScale{1, 1}, uvOffset{0, 0};
    float    value = 0, normalValue = 1, normalBlendValue = 1, albedoValue = 1, metallicValue = 0;
    float    occlusionValue = 1, smoothnessValue = 1;
    float    blendMinimum = 0, blendMaximum = 1, maskMinimum = 0, maskMaximum = 1;
    float    meshMinimum = 0, meshMaximum = 1;
    uint32_t uvMode = 0, colorMode = 0, blendMode = 0, alphaMode = 1;
    uint32_t maskMode = 0, meshMode = 0;
    bool     inverseUvScale = false;
};
/** @brief Borrowed TVE Extras RGBA16F array and owning sampling controls.
 * Channels are emissive, wetness, overlay
 * and alpha. UV equals coords.zw plus
 * coords.xy times world XZ, or the model pivot XZ when usePivotPosition is
 * true.
 * Usage selects the sampled layer when at least 0.5 and the stored value otherwise.
 */
struct PbrVegetationExtras {
    Texture*             texture = nullptr;
    std::array<float, 9> usage{};
    std::array<float, 4> fallback{1, 0, 0, 1};
    std::array<float, 4> coords{1, 1, 0, 0};
    uint32_t             layer            = 0;
    bool                 usePivotPosition = false;
};
/** @brief Borrowed TVE Colors RGBA16F array and owning layer sampling controls.
 * RGB is linear tint and alpha is
 * influence; projection matches PbrVegetationExtras.
 */
struct PbrVegetationColors {
    Texture*             texture = nullptr;
    std::array<float, 9> usage{};
    std::array<float, 4> fallback{1, 1, 1, 0}, coords{1, 1, 0, 0};
    uint32_t             layer            = 0;
    bool                 usePivotPosition = false;
};
/** @brief Select the single owner of vegetation vertex deformation for a draw.
 * RestMesh leaves authored positions unchanged. CpuDeformed declares that the mesh already contains
 * deformVegetation output. GpuFields applies borrowed global-field inputs in the vertex shader.
 * A draw must never combine CpuDeformed geometry with GpuFields.
 */
enum class PbrVegetationDeformationSource : uint32_t { RestMesh, CpuDeformed, GpuFields };
/** @brief Borrowed TVE Vertex RGBA16F array and object-level size controls.
 * TVE Plant Standard 12.6 consumes the selected layer's alpha as the object size multiplier; RGB is
 * preserved in the atlas but is not consumed by that shader. GPU deformation samples the model pivot
 * XZ at LOD zero and supports unskinned object-mode meshes. A Mesh vegetation-deformation stream supplies
 * an authored local pivot; meshes without that stream use the local origin.
 */
struct PbrVegetationVertex {
    Texture*                       texture = nullptr;
    std::array<float, 9>           usage{};
    std::array<float, 4>           fallback{0, 0, 0, 1}, coords{1, 1, 0, 0};
    float                          globalSize = 1, sizeFadeStart = 0, sizeFadeEnd = 100, distanceFadeBias = 1;
    uint32_t                       layer  = 0;
    PbrVegetationDeformationSource source = PbrVegetationDeformationSource::RestMesh;
};
/** @brief Select source-equivalent GPU motion evaluation for rest vegetation geometry. */
enum class PbrVegetationMotionMode : uint32_t { Disabled, Object };
/** @brief Borrowed TVE Motion atlas/noise and owning explicit object-motion controls.
 * Object mode requires an
 * unskinned Mesh nine-float vegetation-deformation stream. Motion sampling uses
 * the authored pivot, while
 * repeat-addressed noise varies over each vertex's world position. Time and all
 * controls are caller-owned snapshots;
 * the renderer reads no clock or mutable global shader state.
 */
struct PbrVegetationMotion {
    Texture*                texture = nullptr;
    Texture*                noise   = nullptr;
    std::array<float, 9>    usage{};
    std::array<float, 4>    fallback{1, 0, .5f, 0}, coords{1, 1, 0, 0};
    std::array<float, 2>    globalDirection{1, 0};
    std::array<float, 3>    worldOrigin{0, 0, 0};
    double                  time        = 0;
    float                   dynamicMode = 0, rigidity = .5f, facing = .5f;
    float                   bending = .2f, bendingSpeed = 2, bendingScale = 1, bendingVariation = 0;
    float                   branch = .2f, rolling = .2f, branchSpeed = 6, branchScale = 3, branchVariation = 0;
    float                   flutter = .2f, flutterSpeed = 20, flutterScale = 10, flutterVariation = 0;
    float                   globalBending = 1, globalBranch = 1, globalFlutter = 1, noiseTiling = 1;
    float                   interaction = 1, interactionMask = 1, fadeDistance = 100;
    float                   perspectivePush = 0, perspectiveNoise = 0, perspectiveAngle = 1;
    uint32_t                layer = 0;
    PbrVegetationMotionMode mode  = PbrVegetationMotionMode::Disabled;
};
/** @brief TVE global-alpha controls applied after optional detail alpha blending.
 * When enabled, masked clipping uses
 * the Extras alpha channel and mesh variation.
 */
struct PbrVegetationAlpha {
    Texture* noise  = nullptr;
    float    global = 0, variation = 0;
    float    glancing = 0, camera = 1, constant = 0;
    float    cameraFadeMin = 0, cameraFadeMax = 100, noiseTiling = 1;
    bool     detailFade = false;
    bool     enabled    = false;
};
/** @brief TVE emissive remap and global Extras modulation for the borrowed emissive texture binding. */
struct PbrVegetationEmission {
    float minimum = 0, maximum = 1, phase = 1, global = 1;
    bool  enabled = false;
};
/** @brief TVE mesh-height gradient tint, masked by the blended main/detail blue channel. */
struct PbrVegetationGradient {
    std::array<float, 3> colorOne{1, 1, 1}, colorTwo{1, 1, 1};
    float                minimum = 0, maximum = 1;
    bool                 enabled = false;
};
/** @brief Owning material parameter snapshot; texture pointers remain borrowed.
 * Base color/metallic/roughness
 * continue to be owned by Material's existing
 * fields. This value owns extension parameters and all eleven texture
 * bindings.
 */
struct PbrSurface {
    std::array<PbrTextureBinding, std::size_t(PbrTextureSlot::Count)> textures{};
    std::array<float, 3>                                              emissive{0, 0, 0}, specularColor{1, 1, 1};
    float normalScale = 1, occlusionStrength = 1, emissiveStrength = 1;
    float specularFactor = 1, ior = 1.5f;
    float anisotropyStrength = 0, anisotropyRotation = 0;
    float clearcoatFactor = 0, clearcoatRoughness = 0, clearcoatNormalScale = 1;
    bool  unlit = false;
    /** @brief Optional per-fragment RGB blend using projected ORM alpha as the color mask.
     * Primary RGB remains
     * Material's tint; alpha/clipping remain unchanged. The secondary
     * RGB is linear HDR. Remap uses the TVE
     * epsilon contract after texture filtering.
     * Requires a linear MetallicRoughness binding when enabled.
     */
    bool                 colorMaskEnabled = false;
    std::array<float, 3> colorMaskSecondary{1, 1, 1};
    float                colorMaskMin = 0, colorMaskMax = 0;
    /** @brief Interpolate texture RGB from white; texture alpha is unaffected. Range [0,1]. */
    float albedoTextureStrength = 1;
    /** @brief Normal reconstruction; vegetation modes require a linear Normal texture and
     * normalScale in [-8,8].
     * This owns only a value; texture lifetime follows textures[].
     */
    PbrNormalMode normalMode = PbrNormalMode::TangentXYZ;
    PbrCullMode   cullMode   = PbrCullMode::Inherit;
    /** @brief Enable alpha-to-coverage for masked rendering when the active target is multisampled. */
    bool alphaToCoverage = false;
    /** @brief Per-light vegetation backlighting. Native ambient RGB supplies the GI term.
     * Positive maskAmount
     * with enabled intensity requires a linear ORM texture;
     * its alpha uses translucency.maskMinimum/Maximum
     * remapping
     * independently of colorMaskEnabled.
     */
    PbrTranslucency          translucency{};
    PbrVegetationColorStages vegetationColor{};
    PbrVegetationDetail      vegetationDetail{};
    PbrVegetationExtras      vegetationExtras{};
    PbrVegetationColors      vegetationColors{};
    PbrVegetationVertex      vegetationVertex{};
    PbrVegetationMotion      vegetationMotion{};
    PbrVegetationAlpha       vegetationAlpha{};
    PbrVegetationEmission    vegetationEmission{};
    PbrVegetationGradient    vegetationGradient{};
    /** @brief Owning linear nonnegative motion-highlight RGB; zero disables the effect.
     * Multiplies final albedo
     * by 1+color*interpolated mesh highlight, after the
     * translucency albedo snapshot. Render-thread snapshot, no
     * retained borrows.
     */
    std::array<float, 3> motionHighlightColor{0, 0, 0};
};
/** @brief Validate finite material factors and sampler enums without changing the input.
 * @return Checked validation status; performs no IO, backend calls or callbacks.
 * @thread Worker-safe and reentrant. Texture pointers are never dereferenced or retained.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> validatePbrSurface(const PbrSurface& surface);
}  // namespace eve::graphics
