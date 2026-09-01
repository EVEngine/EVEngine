#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/heightmap/TerrainPipeline.h"

#include <string>
#include <vector>

namespace eve::procgen {

struct TerrainMeshSettings {
    int originX = 0, originY = 0;
    int cellsX = 0, cellsY = 0;
    int lod = 0;
    float cellSize = 1.f;
    float heightScale = 1.f;
    float skirtDepth = 1.f;
};

/**
 * @brief One renderable heightfield chunk plus four ecological material weights.
 *
 * Weight channels are sand, vegetation, rock, and snow. The base grid comes
 * first; optional crack-hiding skirt vertices follow it.
 */
class TerrainMeshChunk {
public:
    const MeshBuild &mesh() const { return mesh_; }
    MeshBuild &mesh() { return mesh_; }
    int getVertexCount() const { return mesh_.getVertexCount(); }
    int getIndexCount() const { return mesh_.getIndexCount(); }
    int getBaseVertexCount() const { return baseVertexCount_; }
    int getLodStep() const { return lodStep_; }
    int getOriginX() const { return originX_; }
    int getOriginY() const { return originY_; }
    int getSplatWidth() const { return splatWidth_; }
    int getSplatHeight() const { return splatHeight_; }
    float getGeometricError() const { return geometricError_; }
    /** @brief Return the biome id stored for one mesh vertex, or -1 when out of range. */
    int getBiome(int vertex) const;
    float getMaterialWeight(int vertex, int channel) const;

private:
    friend class TerrainMeshBuilder;
    MeshBuild mesh_;
    std::vector<float> weights_;
    std::vector<uint8_t> biomes_;
    int baseVertexCount_ = 0, lodStep_ = 1, originX_ = 0, originY_ = 0;
    int splatWidth_ = 0, splatHeight_ = 0;
    float geometricError_ = 0.f;
};

/** @brief Builds LOD heightfield chunks with stable normals, skirts, and biome splat weights. */
class TerrainMeshBuilder {
public:
    /** @brief Compatibility operation that builds a terrain mesh chunk. */
    static bool build(const Heightmap &heightmap, const TerrainLayers *layers,
                      const TerrainMeshSettings &settings, TerrainMeshChunk &out,
                      std::string *error = nullptr);
    /** @brief Maximum world-space deviation between a LOD grid and the source heightfield. */
    static float estimateGeometricError(const Heightmap &heightmap,
                                        const TerrainMeshSettings &settings);
};

/** @brief Screen-space-error LOD choice based on measured heightfield deviation. */
class TerrainLodSelector {
public:
    /**
     * @brief Select the coarsest LOD whose projected geometric error is within budget.
     * @return LOD in [0,maxLod], or -1 for invalid arguments.
     */
    static int select(const Heightmap &heightmap, TerrainMeshSettings settings, int maxLod,
                      float distance, float viewportHeight, float verticalFovDegrees,
                      float targetPixelError);
};

struct TerrainRiverMeshSettings {
    int originX = 0, originY = 0;
    int cellsX = 0, cellsY = 0;
    float cellSize = 1.f;
    float heightScale = 1.f;
    float minWidth = 0.12f;
    float maxWidth = 0.6f;
    float heightOffset = 0.03f;
    /** Minimum world-space rise/run included in this mesh batch. */
    float minSurfaceSlope = 0.f;
    /** Maximum world-space rise/run rendered as a calm ribbon; steeper reaches use wet terrain. */
    float maxSurfaceSlope = 0.30f;
};

/** @brief Builds overlapping flow-aligned water ribbons from a terrain river network. */
class TerrainRiverMeshBuilder {
public:
    /** @brief Compatibility operation that builds river surface geometry. */
    static bool build(const Heightmap &heightmap, const TerrainLayers &layers,
                      const TerrainRiverMeshSettings &settings, MeshBuild &out,
                      std::string *error = nullptr);
};

struct TerrainLakeMeshSettings {
    int originX = 0, originY = 0;
    int cellsX = 0, cellsY = 0;
    float cellSize = 1.f;
    float heightScale = 1.f;
    float minimumDepth = 0.002f;
    float heightOffset = 0.025f;
};

/** @brief Builds flat water-surface cells for Priority-Flood depressions. */
class TerrainLakeMeshBuilder {
public:
    /** @brief Compatibility operation that builds lake surface geometry. */
    static bool build(const Heightmap &heightmap, const TerrainLayers &layers,
                      const TerrainLakeMeshSettings &settings, MeshBuild &out,
                      std::string *error = nullptr);
};

}  // namespace eve::procgen
