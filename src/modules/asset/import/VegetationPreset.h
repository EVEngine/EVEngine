#pragma once

#include "asset/CanonicalMesh.h"
#include "asset/import/AssetImporter.h"
#include "common/Result.h"
#include "common/Value.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace eve::asset_import {

/** @brief One evaluated TVE conversion command in source execution order. */
struct VegetationPresetCommand {
    std::string              domain;
    std::string              operation;
    std::vector<std::string> arguments;
    std::string              sourcePath;
};

/** @brief Immutable source facts used by TVE preset predicates. */
struct VegetationPresetContext {
    std::set<std::string>         outputOptions;
    std::string                   shaderName;
    std::string                   materialName;
    std::string                   shaderPipeline;
    std::map<std::string, double> materialFloats;
    std::set<std::string>         materialProperties;
    std::set<std::string>         materialTextures;
    std::set<std::string>         materialKeywords;
};

/** @brief One channel instruction in a TVE packed-texture recipe. */
struct VegetationTextureChannelRecipe {
    std::string selector;
    std::string sourceProperty;
    std::string action;
};

/** @brief One ordered texture packing recipe produced by preset execution. */
struct VegetationTexturePackRecipe {
    std::string                                   targetProperty;
    std::string                                   importType = "DEFAULT";
    std::string                                   transformSpace;
    std::array<VegetationTextureChannelRecipe, 4> channels;
};

/** @brief Owning linear RGBA8 source or packed texture used by preset conversion. */
struct VegetationPresetImage {
    std::uint32_t             width  = 0;
    std::uint32_t             height = 0;
    std::vector<std::uint8_t> pixels;
};

/** @brief Owning unpublished material, mesh and texture candidate mutated transactionally by preset commands. */
struct VegetationConversionCandidate {
    std::map<std::string, double>                   materialFloats;
    std::map<std::string, std::array<double, 4>>    materialVectors;
    std::map<std::string, std::array<double, 4>>    materialColors;
    std::map<std::string, std::string>              materialTextures;
    std::map<std::string, std::array<double, 4>>    materialTextureTransforms;
    std::set<std::string>                           materialKeywords;
    std::set<std::string>                           lockedMaterialProperties;
    std::set<std::string>                           firstValidTextureDestinations;
    std::string                                     materialShader;
    std::string                                     materialLighting;
    bool                                            materialInstancing = false;
    std::map<std::string, std::vector<std::string>> meshRules;
    std::vector<VegetationTexturePackRecipe>        texturePacks;
    std::map<std::string, std::vector<std::string>> outputDirectives;
    std::map<std::string, std::string>              shaderAliases;
    std::vector<std::string>                        utilityModes;
};

/** @brief TVE mesh publication mode selected by OutputMeshes. */
enum class VegetationMeshOutputMode : std::uint8_t { Off, Default, Custom };
/** @brief TVE material publication mode selected by OutputMaterials. */
enum class VegetationMaterialOutputMode : std::uint8_t { Off, Default };
/** @brief Requested source encoding for converted texture publication. */
enum class VegetationTextureOutputEncoding : std::uint8_t { Png, Tga, Exr, UnityAsset };
/** @brief Transform policy selected for converted mesh publication. */
enum class VegetationTransformOutputMode : std::uint8_t { KeepOriginal, TransformToWorldSpace };

/** @brief Complete detached output of one vegetation preset conversion transaction. */
struct VegetationConversionResult {
    VegetationConversionCandidate                candidate;
    std::optional<asset::CanonicalMeshData>      mesh;
    std::map<std::string, VegetationPresetImage> textures;
    std::optional<std::array<float, 3>>          meshBoundsMinimum;
    std::optional<std::array<float, 3>>          meshBoundsMaximum;
    bool                                         meshCpuReadable = false;
    VegetationMeshOutputMode                     meshOutput      = VegetationMeshOutputMode::Default;
    VegetationMaterialOutputMode                 materialOutput  = VegetationMaterialOutputMode::Default;
    VegetationTextureOutputEncoding              textureOutput   = VegetationTextureOutputEncoding::Png;
    VegetationTransformOutputMode                transformOutput = VegetationTransformOutputMode::TransformToWorldSpace;
};

/** @brief Owning inputs for one complete Unity Material and TVE preset conversion transaction. */
struct UnityVegetationConversionRequest {
    std::vector<std::uint8_t>                    materialYaml;
    std::string                                  sourcePath;
    std::string                                  shaderName;
    std::string                                  materialName;
    std::string                                  shaderPipeline;
    std::set<std::string>                        outputOptions;
    std::map<std::string, Value>                 presetDefinitions;
    std::string                                  rootPreset;
    std::optional<asset::CanonicalMeshData>      mesh;
    std::map<std::string, VegetationPresetImage> textures;
    std::optional<std::array<float, 16>>         sourceToWorld;
    std::optional<std::array<float, 9>>          sourceDirectionToWorld;
    float                                        variationSeed        = 1.f;
    std::uint64_t                                maximumMaterialBytes = 16ull * 1024ull * 1024ull;
};

/** @brief Owning inputs for one atomically prepared Unity vegetation asset group. */
struct UnityVegetationAssetImportRequest {
    ImportPackageIdentity                            package;
    UnityVegetationConversionRequest                 conversion;
    std::string                                      materialGuid;
    std::string                                      outputKey;
    std::map<std::string, std::vector<std::uint8_t>> sourceFiles;
    AssetImportLimits                                limits;
};

/** @brief Owning ordered set of independently converted vegetation objects published as one package. */
struct UnityVegetationBatchImportRequest {
    ImportPackageIdentity                          package;
    std::vector<UnityVegetationAssetImportRequest> objects;
    AssetImportLimits                              limits;
};

/**
 * @brief Decode Unity text Material saved properties into an owning vegetation conversion baseline.
 * @param yaml Borrowed UTF-8 Unity YAML observed only during this call.
 * @param sourcePath Diagnostic-only source path copied when a failure is produced.
 * @param maximumBytes Input budget checked before parsing or allocation.
 * @return Complete detached material-property candidate or a checked syntax, duplicate, or budget failure.
 * @thread Worker-safe and reentrant; no IO, callbacks, shared mutation, or retained pointers.
 */
[[nodiscard]] Result<VegetationConversionCandidate> decodeUnityVegetationConversionCandidate(
    std::span<const std::uint8_t> yaml, std::string sourcePath, std::uint64_t maximumBytes = 16ull * 1024ull * 1024ull);

/**
 * @brief Encode an applied vegetation candidate as a detached Unity text Material for the canonical TVE importer.
 * @param candidate Immutable complete material candidate.
 * @param maximumBytes Output budget checked before the encoded document escapes.
 * @return Owning UTF-8 Unity YAML, or a checked unsupported, conflict, or budget failure.
 * @thread Worker-safe and reentrant; no IO, callbacks, shared mutation, or retained pointers.
 * @remarks This is the single bridge back into prepareUnityVegetationMaterial; only the four TVE 12.6 target
 * shader families admitted by that importer are accepted.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeUnityVegetationConversionMaterial(
    const VegetationConversionCandidate& candidate, std::uint64_t maximumBytes = 16ull * 1024ull * 1024ull);

/**
 * @brief Project one conversion baseline into immutable preset predicate facts.
 * @param candidate Borrowed source candidate observed only during this call.
 * @param shaderName Resolved source shader name; copied into the result.
 * @param materialName Source material name; copied into the result.
 * @param shaderPipeline Active Unity render-pipeline spelling; copied into the result.
 * @param outputOptions Selected output option spellings; moved into the result.
 * @return Owning context whose property, texture, and keyword sets agree with the candidate.
 * @thread Worker-safe and reentrant; no callbacks, IO, shared mutation, or retained pointers.
 */
[[nodiscard]] VegetationPresetContext makeVegetationPresetContext(const VegetationConversionCandidate& candidate,
                                                                  std::string shaderName, std::string materialName,
                                                                  std::string           shaderPipeline,
                                                                  std::set<std::string> outputOptions = {});

/**
 * @brief Decode, resolve and evaluate a library of canonical TVE preset definitions.
 * @param definitions Map from logical preset name to `eve.vegetation-conversion-preset/1` JSON value.
 * Include names use the source spelling after `[INCLUDE]` and are matched exactly.
 * @param root Logical root preset name.
 * @param context Owning immutable predicate facts supplied by the conversion transaction.
 * @return Fully expanded owning command sequence, or a checked schema/include/predicate failure.
 * @thread Worker-safe and reentrant; no callbacks, IO, mutation or retained pointers.
 * @remarks Include cycles, missing includes, unknown predicates and malformed argument types reject the whole plan.
 */
[[nodiscard]] Result<std::vector<VegetationPresetCommand>> evaluateVegetationPreset(
    const std::map<std::string, Value>& definitions, std::string_view root, const VegetationPresetContext& context);

/**
 * @brief Type-check and atomically apply an evaluated TVE command sequence.
 * @param source Owning baseline copied before mutation.
 * @param commands Evaluated ordered commands borrowed for this call.
 * @return Complete owning candidate, or a checked failure with the caller's baseline unchanged.
 * @thread Worker-safe and reentrant; no callbacks, IO or shared mutation.
 */
[[nodiscard]] Result<VegetationConversionCandidate> applyVegetationPreset(
    VegetationConversionCandidate source, const std::vector<VegetationPresetCommand>& commands);

/**
 * @brief Execute all texture recipes against detached RGBA8 source images.
 * @param candidate Immutable applied preset candidate.
 * @param sources Images keyed by material texture property. Images are borrowed only during this call.
 * @param maximumPixels Per-output allocation limit checked before publication.
 * @return Owning images keyed by target material property, or a checked failure with no partial output.
 * @thread Worker-safe and reentrant; no IO, callbacks, GPU work or retained pointers.
 * @remarks Output dimensions are the largest source dimensions in each recipe. Sampling is bilinear and clamped.
 */
[[nodiscard]] Result<std::map<std::string, VegetationPresetImage>> executeVegetationTexturePacks(
    const VegetationConversionCandidate& candidate, const std::map<std::string, VegetationPresetImage>& sources,
    std::uint64_t maximumPixels = 16ull * 1024ull * 1024ull);

/** @brief Execute texture recipes with a canonical mesh for object/tangent normal-space transforms. */
[[nodiscard]] Result<std::map<std::string, VegetationPresetImage>> executeVegetationTexturePacks(
    const VegetationConversionCandidate& candidate, const std::map<std::string, VegetationPresetImage>& sources,
    const asset::CanonicalMeshData& mesh, std::uint64_t maximumPixels = 16ull * 1024ull * 1024ull);

/**
 * @brief Execute preset mask and coordinate rules into canonical TVE authoring streams.
 * @param candidate Immutable applied preset candidate.
 * @param source Detached owning mesh baseline.
 * @param variationSeed Explicit deterministic seed used by predictive variation.
 * @return Complete owning canonical mesh candidate, or a checked failure without partial publication.
 * @thread Worker-safe and reentrant; no IO, callbacks, global RNG or retained pointers.
 */
[[nodiscard]] Result<asset::CanonicalMeshData> executeVegetationMeshRules(
    const VegetationConversionCandidate& candidate, asset::CanonicalMeshData source, float variationSeed = 1.f);

/** @brief Execute mesh rules with RGBA8 textures keyed by source material property. */
[[nodiscard]] Result<asset::CanonicalMeshData> executeVegetationMeshRules(
    const VegetationConversionCandidate& candidate, asset::CanonicalMeshData source,
    const std::map<std::string, VegetationPresetImage>& textures, float variationSeed = 1.f);

/**
 * @brief Apply commands and execute all available mesh and texture outputs as one transaction.
 * @param source Owning material conversion baseline.
 * @param commands Evaluated ordered commands borrowed during the call.
 * @param mesh Optional owning canonical source mesh.
 * @param textures Immutable source images keyed by material texture property.
 * @param variationSeed Explicit deterministic mesh variation seed.
 * @param sourceToWorld Optional column-major affine point transform used only for world-space output.
 * @param sourceDirectionToWorld Optional column-major proper rotation matching Unity TransformDirection.
 * @return Detached complete result; failure publishes no candidate, mesh or packed texture.
 * @thread Worker-safe and reentrant; no IO, callbacks, shared mutation or retained pointers.
 */
[[nodiscard]] Result<VegetationConversionResult> executeVegetationConversion(
    VegetationConversionCandidate source, const std::vector<VegetationPresetCommand>& commands,
    std::optional<asset::CanonicalMeshData> mesh, const std::map<std::string, VegetationPresetImage>& textures,
    float variationSeed = 1.f, const std::optional<std::array<float, 16>>& sourceToWorld = {},
    const std::optional<std::array<float, 9>>& sourceDirectionToWorld = {});

/**
 * @brief Decode a Unity Material, evaluate one TVE preset graph, and execute every output atomically.
 * @param request Immutable owning request observed only during this synchronous call.
 * @return Complete detached material, mesh, texture, bounds, and readability result; no partial stage escapes.
 * @thread Worker-safe and reentrant when the request is not mutated concurrently; no IO, callbacks, or shared state.
 */
[[nodiscard]] Result<VegetationConversionResult> executeUnityVegetationConversion(
    const UnityVegetationConversionRequest& request);

/**
 * @brief Convert and prepare one material, its packed images, and optional mesh as a single EVA candidate.
 * @param request Immutable owning request observed only during this synchronous call.
 * @return Complete unpublished asset group or a checked failure with no partial candidate.
 * @thread Worker-safe and reentrant when the request is not mutated concurrently; no IO, callbacks, or shared state.
 * @remarks Referenced source textures and metadata are supplied through sourceFiles. Packed images receive stable
 * Unity-style GUIDs and are routed through the normal Unity image and TVE material importers.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityVegetationConversionImport(
    const UnityVegetationAssetImportRequest& request);

/**
 * @brief Prepare several vegetation objects as one all-or-nothing EVA candidate.
 * @param request Immutable batch with a unique non-empty outputKey per object.
 * @return One owning candidate with namespaced entrypoints/mappings, or a conflict/budget failure without output.
 * @thread Worker-safe and reentrant when the request is not mutated concurrently; no IO, callbacks, or shared state.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityVegetationConversionBatch(
    const UnityVegetationBatchImportRequest& request);

}  // namespace eve::asset_import
