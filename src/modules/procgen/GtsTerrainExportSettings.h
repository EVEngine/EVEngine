#pragma once

#include "common/Result.h"
#include "procgen/GtsTerrainLod.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::image { class ImageData; }

namespace eve::procgen {
class Heightmap;

/** @brief Purpose assigned to one exported terrain LOD. */
enum class GtsTerrainLodMode : int { Impostor = 0, LowPoly = 1, Custom = 2 };
/** @brief Edge-normal treatment requested for an exported terrain mesh. */
enum class GtsTerrainNormalEdgeMode : int { Smooth = 0, Sharp = 1 };
/** @brief Texture source used by a terrain export level. */
enum class GtsTerrainTextureExportMethod : int { OrthographicBake = 0, BaseMapExport = 1 };
/** @brief Lighting state used while baking exported textures. */
enum class GtsTerrainBakeLighting : int { NeutralLighting = 0, CurrentSceneLighting = 1 };
/** @brief Optional channel placed in the exported texture alpha channel. */
enum class GtsTerrainAlphaChannel : int { None = 0, Heightmap = 1 };
/** @brief Native material family created for an exported terrain level. */
enum class GtsTerrainExportShader : int { Standard = 0, VertexColor = 1 };
/** @brief Operation performed for the selected source terrain. */
enum class GtsTerrainConversionAction : int { MeshTerrain = 0, ColliderOnly = 1, ObjFileExport = 2 };
/** @brief Treatment applied to a source terrain after a successful conversion commit. */
enum class GtsSourceTerrainTreatment : int { Nothing = 0, Deactivate = 1, StoreInBackup = 2, Delete = 3 };
/** @brief Source selection scope used by a terrain export. */
enum class GtsTerrainExportSelection : int { AllTerrains = 0, SingleTerrain = 1 };
/** @brief Collider representation requested by the export workflow. */
enum class GtsTerrainColliderType : int { Mesh = 0, Heightfield = 1 };
/** @brief Polygon representation written by the Pcg terrain OBJ exporter. */
enum class GtsTerrainObjFaceMode : int { Triangles = 0, Quads = 1 };

/** @brief Validated execution plan compiled from top-level Pcg terrain export controls. */
struct GtsTerrainExportWorkflow {
    GtsTerrainConversionAction action = GtsTerrainConversionAction::MeshTerrain;
    GtsSourceTerrainTreatment sourceTreatment = GtsSourceTerrainTreatment::Deactivate;
    GtsTerrainExportSelection selection = GtsTerrainExportSelection::AllTerrains;
    GtsTerrainObjFaceMode objFaceMode = GtsTerrainObjFaceMode::Triangles;
    bool addTerrainCollider = true;
    GtsTerrainColliderType colliderType = GtsTerrainColliderType::Heightfield;
    bool addMeshColliderToImpostor = true;
    bool invertExportMask = false;
    bool copyPcgObjects = true;
    bool convertTreesToObjects = true;
    bool copyPcgObjectsToImpostor = false;
    bool convertSourceTerrains = false;
    GtsTerrainSaveResolution colliderResolution = GtsTerrainSaveResolution::Full;
    float colliderSimplifyQuality = 1.0f;
    bool addTreeColliders = true;
    bool addObjectColliders = true;
    bool createColliderScenes = true;
    bool bakeCombinedCollisionMesh = true;
    bool createImpostorScenes = false;
    double impostorRange = 0.0;
};

/** @brief Complete native counterpart of one `GTSExportTerrainLODSettings` record. */
struct GtsTerrainExportLodSettings {
    GtsTerrainSaveResolution saveResolution = GtsTerrainSaveResolution::Half;
    float simplifyQuality = 1.0f;
    GtsMeshSimplificationOptions simplification{};
    GtsTerrainNormalEdgeMode normalEdgeMode = GtsTerrainNormalEdgeMode::Smooth;
    GtsTerrainLodMode mode = GtsTerrainLodMode::Impostor;
    bool exportTextures = true;
    bool exportNormalMaps = true;
    bool exportSplatmaps = true;
    bool createMaterials = true;
    GtsTerrainExportShader materialShader = GtsTerrainExportShader::Standard;
    bool bakeVertexColors = true;
    int vertexColorSmoothing = 3;
    std::uint32_t bakeLayerMask = 0xffffffffU;
    GtsTerrainTextureExportMethod textureExportMethod = GtsTerrainTextureExportMethod::OrthographicBake;
    GtsTerrainAlphaChannel alphaChannel = GtsTerrainAlphaChannel::Heightmap;
    int textureExportResolution = 2048;
    GtsTerrainBakeLighting bakeLighting = GtsTerrainBakeLighting::NeutralLighting;
    std::string namePrefix;
    bool captureBaseMapTextures = false;
    float screenRelativeTransitionHeight = 0.8f;

    /** @brief Validate all persisted and executable values in this record. */
    [[nodiscard]] Result<void> validate() const;
    /** @brief Convert this export record to the sequential mesh-LOD contract. */
    [[nodiscard]] Result<GtsTerrainLodLevelSettings> compileLevel() const;
};

/** @brief Construct the exact GTS impostor preset for a zero-based LOD index. */
[[nodiscard]] Result<GtsTerrainExportLodSettings> makeGtsTerrainImpostorLod(int level);
/** @brief Construct the exact GTS low-poly preset for a zero-based LOD index. */
[[nodiscard]] Result<GtsTerrainExportLodSettings> makeGtsTerrainLowPolyLod(int level);

/** @brief Schema-versioned source/impostor terrain export profile. */
class GtsTerrainExportSettings {
public:
    /** @brief Atomically configure all top-level conversion and collider workflow controls. */
    [[nodiscard]] Result<void> configureWorkflow(const GtsTerrainExportWorkflow& workflow);
    /** @brief Return a value copy of the current validated workflow. */
    [[nodiscard]] GtsTerrainExportWorkflow getWorkflow() const noexcept;
    /** @brief Append one source preset after validating the complete resulting sequence. */
    [[nodiscard]] Result<void> appendSourcePreset(int mode, int level, float quality, float transitionHeight);
    /** @brief Append one impostor preset after validating the complete resulting sequence. */
    [[nodiscard]] Result<void> appendImpostorPreset(int mode, int level, float quality, float transitionHeight);
    /** @brief Replace source-terrain LOD records only after every record validates. */
    [[nodiscard]] Result<void> setSourceLods(std::vector<GtsTerrainExportLodSettings> levels);
    /** @brief Replace impostor LOD records only after every record validates. */
    [[nodiscard]] Result<void> setImpostorLods(std::vector<GtsTerrainExportLodSettings> levels);
    /** @brief Return the number of source-terrain LOD records. */
    [[nodiscard]] int getSourceLodCount() const noexcept;
    /** @brief Return the number of impostor LOD records. */
    [[nodiscard]] int getImpostorLodCount() const noexcept;
    /** @brief Return an immutable source LOD record, or nullptr for an invalid index.
     * @ownership The export settings retain ownership; the caller must not delete the pointer.
     * @lifetime Valid until the source list is replaced, restored, or this object is destroyed.
     */
    [[nodiscard]] const GtsTerrainExportLodSettings* sourceLodAt(int index) const noexcept;
    /** @brief Return an immutable impostor LOD record, or nullptr for an invalid index.
     * @ownership The export settings retain ownership; the caller must not delete the pointer.
     * @lifetime Valid until the impostor list is replaced, restored, or this object is destroyed.
     */
    [[nodiscard]] const GtsTerrainExportLodSettings* impostorLodAt(int index) const noexcept;
    /** @brief Compile source records for the existing sequential LOD mesh builder. */
    [[nodiscard]] Result<std::vector<GtsTerrainLodLevelSettings>> compileSourceLevels() const;
    /** @brief Serialize as strict `eve.procgen.gts-terrain-export-settings` version 1 JSON. */
    [[nodiscard]] Result<std::string> snapshotJson() const;
    /** @brief Atomically restore a strict version-1 terrain export profile. */
    [[nodiscard]] Result<void> restoreJson(const std::string& json);

private:
    GtsTerrainExportWorkflow workflow_{};
    std::vector<GtsTerrainExportLodSettings> sourceLods_;
    std::vector<GtsTerrainExportLodSettings> impostorLods_;
};

/**
 * @brief Build source-terrain LODs using the first export record's SaveResolution and the complete sequential list.
 * @param output Replaced only after base mesh, split and every LOD complete.
 * @param heightmap Borrowed immutable source heightmap consumed synchronously.
 * @param settings Borrowed immutable export settings consumed synchronously.
 * @param subTiles Equal X/Z split-plane count in the original editor range 0 through 5.
 * @return Applied result or a structured failure without partial output.
 */
[[nodiscard]] Result<void> buildGtsTerrainExportLodsFromHeightmapInto(
    GtsTerrainLodSet& output,const Heightmap& heightmap,const GtsTerrainExportSettings& settings,
    float sizeX,float sizeY,float sizeZ,int subTiles,GtsMeshPivot pivot=GtsMeshPivot::None);

/**
 * @brief Build the independently sampled and simplified MeshCollider geometry requested by a workflow.
 * @param output Replaced only after the collider mesh is valid and non-empty.
 * @param heightmap Borrowed immutable source heightmap consumed synchronously.
 * @param workflow Borrowed value settings consumed synchronously.
 * @return Final triangle mesh, or a structured failure without partial output.
 */
[[nodiscard]] Result<void> buildGtsTerrainColliderMeshFromHeightmapInto(
    MeshBuild& output,const Heightmap& heightmap,const GtsTerrainExportWorkflow& workflow,
    float sizeX,float sizeY,float sizeZ);

/**
 * @brief Encode the original Pcg unmasked terrain OBJ coordinate, UV and face convention.
 * @param heightmap Borrowed immutable source consumed synchronously.
 * @param resolution Power-of-two terrain sampling reduction.
 * @param faceMode Triangle or quad face output.
 * @return Deterministic UTF-8 Wavefront OBJ text or a structured validation failure.
 */
[[nodiscard]] Result<std::string> encodeGtsTerrainObj(const Heightmap& heightmap,
    GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ,GtsTerrainObjFaceMode faceMode);

/**
 * @brief Encode Pcg's cell-classified masked terrain OBJ convention.
 * @param heightmap Borrowed immutable terrain heights consumed synchronously.
 * @param maskmap Borrowed immutable normalized mask consumed synchronously; dimensions may differ from the terrain.
 * @param threshold Cells whose lower-left mask sample is below this value belong to the outside mesh.
 * @param invert Select the outside mesh when true and the inside mesh otherwise.
 * @return Deterministic UTF-8 Wavefront OBJ text or a structured validation failure.
 */
[[nodiscard]] Result<std::string> encodeGtsMaskedTerrainObj(const Heightmap& heightmap,const Heightmap& maskmap,
    GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ,GtsTerrainObjFaceMode faceMode,
    float threshold=0.2f,bool invert=false);

/**
 * @brief Apply Pcg low-poly edge processing and baked texture vertex colors atomically.
 * @param output Replaced only after the complete mesh and RGBA stream validate.
 * @param source Borrowed triangle mesh consumed synchronously.
 * @param bakedTexture Borrowed readable color texture consumed synchronously.
 * @param edgeMode Smooth preserves shared vertices; Sharp flattens every triangle corner.
 * @param smoothingIterations Pcg's clamped four-neighbor texture smoothing pass count.
 * @param linearize Convert sampled sRGB RGB channels to linear values for an orthographic SRP bake.
 * @return Output vertex count or a structured failure with output unchanged.
 */
[[nodiscard]] Result<int> bakeGtsTerrainVertexColorsInto(MeshBuild& output,const MeshBuild& source,
    const image::ImageData& bakedTexture,GtsTerrainNormalEdgeMode edgeMode,int smoothingIterations,
    float terrainSizeX,float terrainSizeZ,bool linearize=false);

}  // namespace eve::procgen
