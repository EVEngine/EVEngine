#pragma once
#include "common/Export.h"


#include "common/Result.h"
#include "procgen/GtsMeshSimplifier.h"
#include "procgen/GtsMeshSplitter.h"

#include <vector>
#include <string>

namespace eve::procgen {

class GtsTerrainLodSet;
class Heightmap;

/** @brief Pcg/GTS power-of-two sampling reduction used before terrain mesh export. */
enum class GtsTerrainSaveResolution : int { Full=0,Half=1,Quarter=2,Eighth=3,Sixteenth=4 };

/**
 * @brief Build the local-space base mesh consumed by the GTS split and LOD pipeline.
 * @param output Replaced only after the complete mesh and normals are valid.
 * @param heightmap Immutable normalized or world-unit height samples.
 * @param resolution Power-of-two source sample stride.
 * @param sizeX World width of the complete terrain.
 * @param sizeY Multiplier applied to every height sample.
 * @param sizeZ World depth of the complete terrain.
 * @return Applied result or a structured error without modifying output.
 */
[[nodiscard]] Result<void> buildGtsTerrainBaseMesh(MeshBuild& output,const Heightmap& heightmap,
    GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ);

/** @brief Configuration for one sequential GTS terrain mesh LOD. */
struct GtsTerrainLodLevelSettings {
    float quality = 1.0f;
    float screenRelativeTransitionHeight = 0.95f;
    GtsMeshSimplificationOptions simplification{};
};

/** @brief Schema-versioned native counterpart of GTSMeshSettings. */
class GtsTerrainMeshSettings {
public:
    /** @brief Return the SaveResolution enum value in the range 0 through 4. */
    [[nodiscard]] int getSaveResolution() const noexcept { return saveResolution_; }
    /** @brief Atomically set the SaveResolution enum value. */
    [[nodiscard]] Result<void> setSaveResolution(int value);
    /** @brief Return the active LOD count in the Pcg editor range 1 through 4. */
    [[nodiscard]] int getLodCount() const noexcept { return lodCount_; }
    /** @brief Atomically set the active LOD count. */
    [[nodiscard]] Result<void> setLodCount(int value);
    /** @brief Return the equal X/Z split-plane count in the range 0 through 5. */
    [[nodiscard]] int getSubTiles() const noexcept { return subTiles_; }
    /** @brief Atomically set the equal X/Z split-plane count. */
    [[nodiscard]] Result<void> setSubTiles(int value);
    /** @brief Return one stored simplification percentage, or -1 for an invalid slot. */
    [[nodiscard]] float getLodQuality(int index) const noexcept;
    /** @brief Set one of the four simplification percentages in the range 0 through 100. */
    [[nodiscard]] Result<void> setLodQuality(int index,float percent);
    /** @brief Return the effective fixed GTS transition height for an active LOD, or -1. */
    [[nodiscard]] float getLodTransitionHeight(int index) const noexcept;
    /** @brief Serialize as strict `eve.procgen.gts-terrain-mesh-settings` version 1 JSON. */
    [[nodiscard]] Result<std::string> snapshotJson() const;
    /** @brief Atomically restore settings from strict schema-versioned JSON. */
    [[nodiscard]] Result<void> restoreJson(const std::string& json);
    /** @brief Compile active profile slots into sequential native LOD settings. */
    [[nodiscard]] Result<std::vector<GtsTerrainLodLevelSettings>> compileLevels() const;
private:
    int saveResolution_=0,lodCount_=4,subTiles_=3;
    float lodQuality_[4]{100.f,50.f,25.f,12.5f};
};

/** @brief One split terrain tile and all of its local-space LOD meshes. */
struct GtsTerrainLodTile {
    float offsetX = 0.0f;
    float offsetZ = 0.0f;
    std::vector<MeshBuild> levels;
};

/** @brief Deterministic native export identity for one GTS tile LOD mesh. */
struct GtsTerrainLodAssetEntry {
    int tileIndex=0;
    int levelIndex=0;
    std::string objectName;
    std::string meshName;
    std::string relativePath;
};

/** @brief Owned script-friendly GTS terrain LOD export manifest. */
class GtsTerrainLodAssetPlan {
public:
    /** @brief Return the number of export entries. */
    [[nodiscard]] int getEntryCount() const;
    /** @brief Return an entry tile index, or -1. */
    [[nodiscard]] int getTileIndex(int index)const;
    /** @brief Return an entry LOD index, or -1. */
    [[nodiscard]] int getLevelIndex(int index)const;
    /** @brief Return an entry object name, or empty. */
    [[nodiscard]] std::string getObjectName(int index)const;
    /** @brief Return an entry mesh name, or empty. */
    [[nodiscard]] std::string getMeshName(int index)const;
    /** @brief Return an entry relative path, or empty. */
    [[nodiscard]] std::string getRelativePath(int index)const;
private:
    friend Result<void> planGtsTerrainLodAssetsInto(GtsTerrainLodAssetPlan&,const GtsTerrainLodSet&,const std::string&,const std::string&);
    std::vector<GtsTerrainLodAssetEntry>entries_;
};

/** @brief Owned result of the GTS split-and-sequential-simplify pipeline. */
class EVENGINE_API_DOMAINS GtsTerrainLodSet {
public:
    /** @brief Return the number of tile columns. */
    [[nodiscard]] int getColumnCount() const { return columns_; }
    /** @brief Return the number of tile rows. */
    [[nodiscard]] int getRowCount() const { return rows_; }
    /** @brief Return the number of tiles, including empty source cells. */
    [[nodiscard]] int getTileCount() const { return static_cast<int>(tiles_.size()); }
    /** @brief Return the common number of LOD levels per tile. */
    [[nodiscard]] int getLevelCount() const { return static_cast<int>(settings_.size()); }
    /**
     * @brief Return one tile or nullptr for an invalid index.
     * @ownership The LOD set retains ownership; the caller must not delete the pointer.
     * @lifetime Valid until this LOD set is destroyed or replaced.
     */
    [[nodiscard]] const GtsTerrainLodTile* tileAt(int index) const;
    /**
     * @brief Return one level setting or nullptr for an invalid index.
     * @ownership The LOD set retains ownership; the caller must not delete the pointer.
     * @lifetime Valid until this LOD set is destroyed or replaced.
     */
    [[nodiscard]] const GtsTerrainLodLevelSettings* levelAt(int index) const;
    /**
     * @brief Select the active level for a projected screen-relative height.
     * @param relativeHeight Projected object height divided by viewport height.
     * @return Level index, -1 when below the last culling threshold, or -2 for invalid input.
     */
    [[nodiscard]] int selectLevel(float relativeHeight) const;
    /**
     * @brief Convert one level's screen-relative threshold to a perspective-camera distance.
     * @param level Level index.
     * @param worldDiameter World-space diameter used by the LOD bounds.
     * @param verticalFovDegrees Camera vertical field of view in degrees.
     * @return Positive switch distance, or -1 for invalid arguments.
     */
    [[nodiscard]] float getLevelSwitchDistance(int level, float worldDiameter, float verticalFovDegrees) const;
    /**
     * @brief Select a level directly from perspective-camera distance and bounds diameter.
     * @return Level index, -1 when culled, or -2 for invalid arguments.
     */
    [[nodiscard]] int selectLevelForCamera(float distance, float worldDiameter, float verticalFovDegrees) const;
    /** @brief Serialize the complete owned LOD set as schema-versioned JSON. */
    [[nodiscard]] Result<std::string> snapshotJson() const;
    /** @brief Atomically restore a complete LOD set from strict schema-versioned JSON. */
    [[nodiscard]] Result<void> restoreJson(const std::string& json);

private:
    friend Result<GtsTerrainLodSet> buildGtsTerrainLods(
        const MeshBuild&, int, int, GtsMeshPivot, const std::vector<GtsTerrainLodLevelSettings>&);
    int columns_ = 0;
    int rows_ = 0;
    std::vector<GtsTerrainLodLevelSettings> settings_;
    std::vector<GtsTerrainLodTile> tiles_;
};

/**
 * @brief Split a terrain mesh and generate each tile's sequential GTS LOD chain.
 * @param source Immutable world-space terrain mesh.
 * @param xSplits Number of regular X split planes.
 * @param zSplits Number of regular Z split planes.
 * @param pivot Local pivot convention for every tile.
 * @param levels Ordered near-to-far LOD settings with descending transition heights.
 * @return Owned tiles and levels, or a structured error without observable partial state.
 */
[[nodiscard]] Result<GtsTerrainLodSet> buildGtsTerrainLods(
    const MeshBuild& source, int xSplits, int zSplits, GtsMeshPivot pivot,
    const std::vector<GtsTerrainLodLevelSettings>& levels);

/**
 * @brief Execute the complete GTS heightmap-to-split-LOD conversion as one atomic operation.
 * @param subTileSplits Split planes on both axes; output is `(subTileSplits+1)^2` row-major tiles.
 */
[[nodiscard]] Result<GtsTerrainLodSet> buildGtsTerrainLodsFromHeightmap(const Heightmap& heightmap,
    GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ,int subTileSplits,
    GtsMeshPivot pivot,const std::vector<GtsTerrainLodLevelSettings>& levels);

/** @brief Atomically replace output with the complete default four-level GTS terrain conversion. */
[[nodiscard]] Result<void> buildDefaultGtsTerrainLodsFromHeightmapInto(GtsTerrainLodSet& output,
    const Heightmap& heightmap,GtsTerrainSaveResolution resolution,float sizeX,float sizeY,float sizeZ,
    int subTileSplits,GtsMeshPivot pivot=GtsMeshPivot::None);

/** @brief Atomically build a complete heightmap conversion using persisted GTS mesh settings. */
[[nodiscard]] Result<void> buildGtsTerrainLodsFromHeightmapInto(GtsTerrainLodSet& output,
    const Heightmap& heightmap,const GtsTerrainMeshSettings& settings,float sizeX,float sizeY,float sizeZ,
    GtsMeshPivot pivot=GtsMeshPivot::None);

/** @brief Return the four-level quality and transition profile used by GTSMeshSettings. */
[[nodiscard]] std::vector<GtsTerrainLodLevelSettings> defaultGtsTerrainLodLevels();

/**
 * @brief Build the deterministic object and mesh naming plan used by GTS terrain export.
 * @param lods Complete LOD set whose non-empty levels become entries.
 * @param terrainName Valid filesystem-safe terrain name.
 * @param meshFolder Valid relative output folder, normally `Meshes`.
 * @return Row-major, then near-to-far export entries.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<GtsTerrainLodAssetEntry>> planGtsTerrainLodAssets(
    const GtsTerrainLodSet& lods,const std::string& terrainName,const std::string& meshFolder="Meshes");
/** @brief Atomically replace a script-friendly export plan. */
[[nodiscard]] Result<void> planGtsTerrainLodAssetsInto(GtsTerrainLodAssetPlan& output,
    const GtsTerrainLodSet& lods,const std::string& terrainName,const std::string& meshFolder="Meshes");

}
