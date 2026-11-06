#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <span>

namespace eve::graphics {
class Mesh;
class Texture;

/** @brief Surface coverage: opaque, cutoff mask, or stochastic alpha coverage (not refraction). */
enum class PathAlphaMode : uint32_t { Opaque, Masked, Blend };

/** @brief Linear-light surface parameters for path tracing.
 * @details Opacity multiplies texture alpha. Opaque ignores both; masked accepts coverage >= alphaCutoff.
 * Blend samples coverage independently per ray, including shadows and indirect light.
 */
struct PathMaterial {
    glm::vec3     baseColor{1.f};
    glm::vec3     emission{0.f};
    float         roughness        = 1.f;
    float         metallic         = 0.f;
    uint32_t      baseColorTexture = UINT32_MAX;
    float         opacity          = 1.f;
    float         alphaCutoff      = 0.5f;
    PathAlphaMode alphaMode        = PathAlphaMode::Opaque;
    /** @brief Linear data map: green multiplies roughness, blue multiplies metallic. */
    uint32_t metallicRoughnessTexture = UINT32_MAX;
    /** @brief Linear RGB multiplier of emission; native sRGB inputs are decoded during sampling. */
    uint32_t emissionTexture = UINT32_MAX;
    /** @brief Linear tangent-space XYZ normal map, decoded from [0,1] to [-1,1]. */
    uint32_t normalTexture = UINT32_MAX;
    /** @brief Finite signed multiplier of tangent-space normal X/Y before normalization. */
    float normalScale = 1.f;
};

/** @brief Base-level filter override; Source uses the source sampler (linear for pixel spans). */
enum class PathTextureFilter : uint8_t { Source, Nearest, Linear };
/** @brief Address override; Source uses the source sampler (repeat for pixel spans). */
enum class PathTextureAddress : uint8_t { Source, Repeat, Clamp, Mirror };

/** @brief Base-level color or material-data texture from linear pixels or an owned engine RGBA8 texture.
 * @ownership Pixels and sourceTexture are borrowed only during preparation; the scene owns a copied snapshot.
 * @details Pixel-span values must be in [0,1] and already linear. Native color inputs can decode sRGB;
 * native metallic-roughness inputs must disable srgbDecode. Rotation is in radians.
 * Samples use the base level; this input does not supply mip levels.
 * Optional alpha must have the same pixel count and finite values in [0,1]; empty means opaque.
 * Pixel spans use bilinear/repeat sampling. For sourceTexture, leave dimensions/spans empty;
 * preparation uses its magnification filter and address modes, unpremultiplies if needed,
 * and optionally decodes sRGB before filtering. Source release or edits cannot alter a prepared snapshot.
 * Minification mip selection is not yet performed.
 */
struct PathColorTexture {
    uint32_t                   width = 0, height = 0;
    std::span<const glm::vec3> pixels;
    std::span<const float>     alpha;
    Texture*                   sourceTexture = nullptr;
    bool                       srgbDecode    = true;
    /** @brief UV transform applied as offset + rotation * (scale * UV), before addressing. */
    glm::vec2 offset{0.f}, scale{1.f};
    float     rotation = 0.f;
    /** @brief Independent material binding overrides; source texture state is never mutated. */
    PathTextureFilter  filter   = PathTextureFilter::Source;
    PathTextureAddress addressU = PathTextureAddress::Source, addressV = PathTextureAddress::Source;
};

/** @brief Indexed geometry; spans are borrowed only during scene preparation.
 * @ownership Caller owns all spans and sourceMesh; preparation copies the current geometry.
 * @details Set sourceMesh with empty vertex/index spans to capture an engine mesh instead.
 * GPU-skinned/resident overrides require a supported deformation snapshot; unsupported inputs fail explicitly.
 * Empty normals on span input request geometric normals.
 */
struct PathGeometry {
    std::span<const glm::vec3> positions;
    std::span<const glm::vec3> normals;
    std::span<const uint32_t>  indices;
    uint32_t                   material = 0;
    std::span<const glm::vec2> texcoords;
    Mesh*                      sourceMesh = nullptr;
    /** @brief Optional authored object-space XYZ frames, one pair per vertex; copied during preparation.
     * Empty frames derive a triangle UV0 basis. Both streams must be present together. */
    std::span<const glm::vec3> tangents, bitangents;
};

/** @brief Instance of shared geometry with an invertible affine object-to-world transform. */
struct PathInstance {
    uint32_t  geometry = 0;
    glm::mat4 transform{1.f};
};

/** @brief Analytic light distribution; area emitters are derived from emissive geometry. */
enum class PathLightKind : uint8_t { Point, Directional, Spot };

/** @brief World-space analytic light copied during scene preparation.
 * @details Point strength is RGB radiant intensity with inverse-square falloff.
 * Directional strength is RGB irradiance perpendicular to directionToLight.
 * All colors are linear; directionToLight must be finite and nonzero for directional lights.
 * Spot strength has the same units as point strength on the beam axis. The beam
 * points away from the source; angular attenuation is the square of the clamped
 * linear ramp between outer and inner cone cosines. Equal cosines form a hard rim.
 */
struct PathLight {
    PathLightKind kind = PathLightKind::Point;
    glm::vec3     position{0.f};
    glm::vec3     directionToLight{0.f, 0.f, -1.f};
    glm::vec3     strength{1.f};
    /** @brief Finite nonzero emission direction for spot lights. */
    glm::vec3 beamDirection{0.f, 0.f, 1.f};
    /** @brief Spot half-angle cosines, -1 <= outer <= inner <= 1. */
    float cosOuter = 0.8f, cosInner = 0.95f;
};

/** @brief Borrowed scene snapshot; preparation copies all inputs into provider-owned resources.
 * @lifetime No caller memory is retained after preparation returns.
 * @thread Render thread only; callers must not mutate spans during preparation.
 */
struct PathSceneDescription {
    std::span<const PathGeometry>     geometry;
    std::span<const PathInstance>     instances;
    std::span<const PathMaterial>     materials;
    std::span<const PathLight>        lights;
    std::span<const PathColorTexture> colorTextures;
};

/** @brief Opaque prepared-scene identity; invalid after replacement or device detachment.
 * @ownership Does not own the scene. Only the issuing provider accepts this token.
 */
struct PathSceneToken {
    uint64_t generation = 0;
};

/** @brief Provider-owned frame output identity; invalidated by resize, scene replacement or device detach. */
struct PathFrameToken {
    uint64_t generation = 0;
};

/** @brief Output reconstruction; spatial filtering does not modify raw accumulation. */
enum class PathDenoise : uint8_t { None, Spatial, Temporal };

/** @brief Deterministic path sample batch. Zero history starts a new accumulation.
 * @details Caller supplies absolute sample index and exact prior sample count.
 * Reset history after camera, environment, seed, or scene changes. Matrices use RH/ZO projection.
 */
struct PathTraceView {
    glm::mat4   invViewProj{1.f};
    glm::vec3   eye{0.f};
    glm::vec3   environment{0.f};
    uint32_t    firstSample    = 0;
    uint32_t    samples        = 1;
    uint32_t    maxBounces     = 4;
    uint32_t    seed           = 0;
    uint32_t    historySamples = 0;
    PathDenoise denoise        = PathDenoise::None;
    /** @brief Discard temporal reconstruction history for this batch (for example an explicit camera cut).
     * Temporal mode requires historySamples=0 and consecutive absolute sample indices. It automatically
     * resets on scene/size/environment/seed/bounce changes; moving-instance scenes currently reset globally.
     * It reprojects static-world history across camera motion and applies spatial filtering afterwards. */
    bool resetTemporalHistory = false;
};

}  // namespace eve::graphics
