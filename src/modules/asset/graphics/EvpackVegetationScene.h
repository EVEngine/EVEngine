#pragma once

/** @file EvpackVegetationScene.h @brief Runtime decoding and projection of TVE scene-manager state. */

#include "asset/EvpackResourceReader.h"
#include "graphics/VegetationDetails.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationFieldGpu.h"

#include <array>
#include <map>
#include <glm/vec2.hpp>
#include <memory>
#include <vector>
#include <span>

namespace eve::asset_graphics {

/** @brief Owning TVE Global Control values; texture identities remain source-map GUIDs. */
struct VegetationSceneControl {
    float season = 2, globalAlpha = 1, globalOverlay = 0, globalWetness = 0, globalEmissive = 1;
    float globalSubsurface = 1, globalSize = 1, overlaySmoothness = .5f, overlayNormalScale = .5f;
    float overlaySubsurface = .5f, overlayScale = 1, wetnessContrast = .5f, wetnessNormalScale = .5f;
    float noiseTiling = 1, proximityFade = 1, distanceFadeBias = 1, defaultConformHeight = 0;
    std::array<float, 4> globalColor{.5f, .5f, .5f, 0}, overlayColor{1, 1, 1, 1};
    std::string overlayAlbedoGuid, overlayNormalGuid, noiseTextureGuid;
};

/** @brief Owning TVE Global Motion values; time is supplied by each frame. */
struct VegetationSceneMotion {
    float windPower = .5f, noiseTiling = 1, bending = 1, branch = 1, flutter = 1;
    float speed = 1, fadeDistance = 100;
    bool  animatedTime = true;
    std::array<float, 2> direction{-1, 0};
    std::string noiseTextureGuid;
};

/** @brief One TVE volume texture channel allocation policy. */
struct VegetationSceneVolumeChannel {
    std::int32_t renderMode = 10;
    std::uint32_t width = 1024, height = 1024;
};

/** @brief Owning TVE Global Volume policy; render resources are allocated by the runtime host. */
struct VegetationSceneVolume {
    float renderScale = 1, edgeFade = .75f;
    std::uint32_t visibility = 0, sorting = 0;
    std::array<VegetationSceneVolumeChannel, 4> channels{};
};

/** @brief One normalized Unity TVEElement definition awaiting texture realization. */
struct VegetationSceneElementProperty {
    std::string name, textureGuid, textureAsset;
    std::int32_t type = 0;
    std::array<float, 4> vector{};
    float value = 0;
};

/** @brief One normalized Unity TVEElement definition awaiting texture realization. */
struct VegetationSceneElement {
    std::int64_t sourceFileId = 0;
    std::string shaderGuid, kind, textureGuid, textureAsset;
    std::vector<VegetationSceneElementProperty> properties;
    std::uint32_t channel = 0;
    bool enabled = true, seasonal = false, invertDirection = false, volumeFade = false;
    std::int32_t visibility = -1, blendRgb = 0, blendAlpha = 0, directionMode = 20, motionMode = 15;
    std::uint16_t layers = 1;
    float intensity = 1, motionPower = 0;
    std::array<float, 3> position{}, scale{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1}, value{1, 1, 1, 1};
    std::array<std::array<float, 4>, 4> seasons{};
    std::array<float, 6> remap{0, 1, 0, 1, 0, 0};
};

/** @brief Explicit per-draw inputs consumed by one TVE Element fragment evaluation. */
struct VegetationSceneElementPixelInput {
    glm::vec2 localUv{.5f};
    glm::vec4 mainSample{1.f};
    glm::vec4 elementParams{1.f};
    glm::vec4 vertexColor{1.f};
    glm::vec3 worldPosition{0.f};
    glm::vec3 worldNormal{0.f, 1.f, 0.f};
    glm::vec2 velocityDirection{0.f};
    glm::vec4 noiseSample{0.f};
    float terrainHeight = 0.f;
    float volumeFade = 1.f;
    float season = 2.f;
};

/** @brief Detached TVE render-target source value and independent hardware blend policy. */
enum class VegetationScenePixelBlend : std::uint8_t { Alpha, Multiply, Add, Replace };

/** @brief Detached TVE render-target source value and independent hardware blend policy. */
struct VegetationSceneElementPixel {
    glm::vec4 value{0.f};
    std::uint8_t colorMask = 15;
    std::int32_t blendRgb = 0, blendAlpha = 0;
    VegetationScenePixelBlend rgbOperation = VegetationScenePixelBlend::Alpha;
    VegetationScenePixelBlend alphaOperation = VegetationScenePixelBlend::Alpha;
};

/** @brief Fully validated, detached scene-manager candidate with no archive or GPU borrows. */
struct LoadedVegetationScene {
    AssetRef                            asset;
    std::string                         sourceGuid;
    asset::EvpackVariantSelection      variant;
    graphics::VegetationDetailSettings details;
    VegetationSceneControl             control;
    VegetationSceneMotion              motion;
    VegetationSceneVolume              volume;
    std::vector<VegetationSceneElement> elements;
};

/** @brief Detached PBR and CPU-motion snapshot ready for atomic material publication. */
struct VegetationSceneProjection {
    graphics::PbrSurface       surface;
    graphics::VegetationMotion motion;
    VegetationSceneVolume      volume;
    float                      season = 2;
};

/** @brief Strict capability-aware decoder for `eve.vegetation-scene/1`. */
class EVENGINE_API_WORLD EvpackVegetationSceneLoader {
public:
    /** @brief Bind a borrowed immutable reader which must outlive this loader. */
    explicit EvpackVegetationSceneLoader(const asset::EvpackResourceReader& reader) noexcept : reader_(reader) {}

    /**
     * @brief Decode and validate one complete scene state without mutating runtime objects.
     * @return Owning candidate or a structured failure; unknown fields and versions are rejected.
     * @thread Worker-safe when the reader is safe for concurrent reads.
     */
    [[nodiscard]] Result<LoadedVegetationScene> load(const AssetRef& asset,
                                                     const asset::EvpackCapabilities& capabilities,
                                                     std::uint64_t maximumDecodedBytes = 1024 * 1024) const;

private:
    const asset::EvpackResourceReader& reader_;
};

/**
 * @brief Project validated TVE manager controls onto one material/motion snapshot.
 * @param baseSurface Borrowed immutable material values, including resolved texture pointers.
 * @param baseMotion Borrowed immutable object/time/camera values.
 * @param scene Owning validated scene state.
 * @return Detached complete candidate; inputs remain unchanged on failure.
 * @thread Worker-safe; no clocks, GPU calls, callbacks or global shader state.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<VegetationSceneProjection> projectVegetationScene(
    const graphics::PbrSurface& baseSurface, const graphics::VegetationMotion& baseMotion,
    const LoadedVegetationScene& scene);

/**
 * @brief Decode every runtime texture used by admitted TVE Element shaders into owning linear masks.
 * @param reader Borrowed immutable package reader, used only during this call.
 * @param scene Borrowed validated scene whose texture asset references identify the image dependencies.
 * @param capabilities Runtime variant capabilities.
 * @param maximumDecodedBytes Aggregate decoded-image and owning-mask byte budget across unique images.
 * @return Owning masks keyed by lowercase Unity GUID; missing required images fail the whole candidate.
 * @thread Worker-safe when the reader is safe for concurrent reads.
 * @reentrancy Performs no callbacks, GPU calls, global mutation, clocks or filesystem access.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<std::map<std::string, graphics::VegetationMask>> loadVegetationSceneElementMasks(
    const asset::EvpackResourceReader& reader, const LoadedVegetationScene& scene,
    const asset::EvpackCapabilities& capabilities, std::uint64_t maximumDecodedBytes = 256 * 1024 * 1024);

/**
 * @brief Evaluate one TVE Element fragment using caller-supplied texture, instance and particle inputs.
 * @param element Borrowed immutable canonical Element.
 * @param input Detached values corresponding to TVE `_MainTex`, `_ElementParams`, vertex color and volume fade.
 * @return Owning source pixel plus exact independent RGB/alpha blend modes and color mask.
 * @thread Worker-safe; no texture reads, callbacks, clocks, global state or GPU work.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<VegetationSceneElementPixel> evaluateVegetationSceneElementPixel(
    const VegetationSceneElement& element, const VegetationSceneElementPixelInput& input);

/** @brief Apply one evaluated source pixel to a destination with TVE's separate blend factors and ColorMask. */
[[nodiscard]] EVENGINE_API_WORLD glm::vec4 composeVegetationSceneElementPixel(glm::vec4 destination,
                                                            const VegetationSceneElementPixel& source) noexcept;

/**
 * @brief Rasterize the ordered TVE Element list over an owning four-channel atlas candidate.
 * @param scene Borrowed immutable manager and Element state.
 * @param masks Borrowed immutable masks keyed by Unity texture GUID.
 * @param base Owning initial atlas and world XZ mapping; copied before any evaluation.
 * @param layers Selected 0..8 layer for Colors, Extras, Motion and Vertex respectively.
 * @param worldNormals Optional per-texel model/world normals; empty selects up.
 * @param terrainHeights Optional per-texel resolved terrain heights; empty selects zero.
 * @param noiseSamples Optional per-texel already time-filtered noise values; empty selects zero.
 * @return Fully composed owning atlas; any invalid input or evaluation failure leaves base unchanged.
 * @thread Worker-safe; no callbacks, clocks, GPU work, global mutation or retained borrows.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<graphics::VegetationAtlas> bakeVegetationSceneElements(
    const LoadedVegetationScene& scene, const std::map<std::string, graphics::VegetationMask>& masks,
    const graphics::VegetationAtlas& base, std::array<std::uint8_t, 4> layers = {},
    std::span<const glm::vec3> worldNormals = {}, std::span<const float> terrainHeights = {},
    std::span<const glm::vec4> noiseSamples = {});

/** @brief Bake one TVE manager channel at its own resolution and world mapping.
 * @param channel Target channel; only Elements writing this channel are evaluated.
 * @param layer Selected TVE layer in [0,8].
 * @return Fully composed detached channel atlas, or failure without mutating base.
 */
[[nodiscard]] Result<graphics::VegetationChannelAtlas> bakeVegetationSceneChannel(
    const LoadedVegetationScene& scene, const std::map<std::string, graphics::VegetationMask>& masks,
    graphics::VegetationChannel channel, const graphics::VegetationChannelAtlas& base, std::uint8_t layer = 0,
    std::span<const glm::vec3> worldNormals = {}, std::span<const float> terrainHeights = {},
    std::span<const glm::vec4> noiseSamples = {});

/**
 * @brief Convert a fully composed TVE render-target atlas to EVEngine field channel conventions.
 * @param tveAtlas Borrowed owning-value atlas produced by bakeVegetationSceneElements.
 * @return Detached atlas whose Motion RG stores signed world X/Z; other TVE channels stay unchanged.
 * @thread Worker-safe; no callbacks, clocks, GPU work, global mutation or retained borrows.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<graphics::VegetationAtlas> convertVegetationSceneAtlasToNative(
    const graphics::VegetationAtlas& tveAtlas);

/** @brief Convert one fully composed TVE channel atlas to EVEngine conventions. */
[[nodiscard]] Result<graphics::VegetationChannelAtlas> convertVegetationSceneChannelToNative(
    graphics::VegetationChannel channel, const graphics::VegetationChannelAtlas& tveAtlas);

/** @brief Detached inputs for one independently sized TVE manager channel. */
struct VegetationSceneChannelGpuBuild {
    std::array<graphics::VegetationChannelAtlas, 9> baseLayers;
    std::span<const glm::vec3> worldNormals{};
    std::span<const float> terrainHeights{};
    std::span<const glm::vec4> noiseSamples{};
};

/** @brief Explicit detached inputs for publishing all nine TVE scene-manager field layers. */
struct VegetationSceneGpuBuild {
    std::array<VegetationSceneChannelGpuBuild, 4> channels;
    graphics::VegetationGpuRuntime runtime;
    std::uint64_t sourceRevision = 0;
};

/** @brief Receipt for one atomic scene-manager GPU replacement. */
struct VegetationSceneGpuPublication {
    std::uint64_t revision = 0;
    std::size_t deferredReleaseCount = 0;
};

/**
 * @brief Owning GPU publication of one immutable TVE manager scene.
 * The resource factory, mask map and scene are borrowed only while create executes. The returned runtime owns the
 * field textures and projected material snapshot. It must outlive every draw borrowing surface().
 */
class EVENGINE_API_WORLD VegetationSceneGpuRuntime final {
public:
    /**
     * @brief Bake, convert, upload and bind all field layers as one atomic candidate.
     * @param factory Borrowed graphics-thread factory which must outlive this runtime.
     * @param baseSurface Borrowed immutable source material snapshot.
     * @param baseMotion Borrowed immutable object motion snapshot; its explicit time drives the manager projection.
     * @param scene Borrowed immutable decoded manager state.
     * @param masks Borrowed immutable decoded Element masks.
     * @param build Detached atlas geometry, optional raster inputs, GPU leases and nonzero source revision.
     * @return Owning runtime, or failure after releasing every candidate GPU allocation.
     * @thread Graphics thread only; synchronous, non-reentrant and without callbacks.
     */
    [[nodiscard]] static Result<std::unique_ptr<VegetationSceneGpuRuntime>> create(
        graphics::IResourceFactory& factory, const graphics::PbrSurface& baseSurface,
        const graphics::VegetationMotion& baseMotion, const LoadedVegetationScene& scene,
        const std::map<std::string, graphics::VegetationMask>& masks, const VegetationSceneGpuBuild& build);

    ~VegetationSceneGpuRuntime();
    VegetationSceneGpuRuntime(const VegetationSceneGpuRuntime&) = delete;
    VegetationSceneGpuRuntime& operator=(const VegetationSceneGpuRuntime&) = delete;

    /** @brief Borrow the bound material snapshot; valid until release begins or destruction. */
    [[nodiscard]] const graphics::PbrSurface& surface() const noexcept { return projection_.surface; }
    /** @brief Borrow the projected CPU motion snapshot; valid for this runtime's lifetime. */
    [[nodiscard]] const graphics::VegetationMotion& motion() const noexcept { return projection_.motion; }
    /** @brief Revision shared by the immutable scene input and every GPU field texture. */
    [[nodiscard]] std::uint64_t sourceRevision() const noexcept { return sourceRevision_; }
    /** @brief Number of superseded field sets retained because their factory release must be retried. */
    [[nodiscard]] std::size_t deferredReleaseCount() const noexcept { return retiredFields_.size(); }
    /**
     * @brief Atomically replace this runtime after validating its observed revision.
     * A complete candidate is built before publication. Failure preserves the current surface and revision.
     * Superseded textures are released after publication; failed releases remain owned for release() retry.
     * @param expectedRevision Revision observed by the caller before preparing the replacement.
     * @return New revision and observable deferred cleanup count, or Conflict/validation failure without publication.
     * @thread Graphics thread only; synchronous, non-reentrant and without callbacks.
     */
    [[nodiscard]] Result<VegetationSceneGpuPublication> replace(
        std::uint64_t expectedRevision, const graphics::PbrSurface& baseSurface,
        const graphics::VegetationMotion& baseMotion, const LoadedVegetationScene& scene,
        const std::map<std::string, graphics::VegetationMask>& masks, const VegetationSceneGpuBuild& build);
    /** @brief Invalidate material borrows and release all fields; failed entries remain owned for retry. */
    [[nodiscard]] Result<void> release();

private:
    explicit VegetationSceneGpuRuntime(graphics::IResourceFactory& factory) noexcept : factory_(&factory) {}

    graphics::IResourceFactory* factory_ = nullptr;
    graphics::VegetationGpuFieldSet fields_;
    std::vector<graphics::VegetationGpuFieldSet> retiredFields_;
    VegetationSceneProjection projection_;
    std::uint64_t sourceRevision_ = 0;
};

}  // namespace eve::asset_graphics
