#pragma once

#include "procgen/heightmap/Heightmap.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace eve::procgen {

struct ThermalErosionSettings { int iterations = 20; float talus = 0.02f; float strength = 0.35f; };
struct HydraulicErosionSettings {
    int iterations = 60; float rainfall = 0.012f; float evaporation = 0.08f;
    float capacity = 2.f; float erosion = 0.18f; float deposition = 0.12f;
};
struct FluvialErosionSettings {
    int iterations = 8;
    float riverThreshold = 0.015f;
    float incision = 0.012f;
    float maxDepth = 0.28f;
    float bankWidth = 3.f;
    /** Maximum depression depth whose spill sill may be breached. */
    float maxBreachDepth = 0.04f;
    /** Raster samples per reference cell, used to preserve physical feature scale. */
    float coordinateScale = 1.f;
};

enum class Biome : uint8_t {
    Ocean, Beach, Desert, Grassland, Forest, Rainforest, Tundra, Taiga, Alpine,
    River, Lake, Wetland
};

struct HydrologyMap {
    int width = 0, height = 0;
    std::vector<int8_t> flowDirection; ///< D8 neighbour index, or -1 for a sink.
    std::vector<float> flowVectorX, flowVectorY; ///< Normalized continuous downslope direction.
    std::vector<float> flowAccumulation;
    std::vector<float> lakeDepth; ///< Priority-Flood water depth; zero on drained terrain.
    std::vector<uint8_t> rivers;
    std::vector<uint8_t> streamOrder; ///< Strahler order; zero outside the resolved river network.
};

struct ClimateMap {
    int width = 0, height = 0;
    std::vector<float> temperature, moisture;
    std::vector<Biome> biomes;
};

/** @brief Per-cell diagnostic outputs produced by an erosion stage. */
struct TerrainErosionMap {
    int width = 0, height = 0;
    std::vector<float> wear;       ///< Gross material removed, in heightmap units.
    std::vector<float> deposition; ///< Material deposited after transport, in heightmap units.
    std::vector<float> heightDelta; ///< Final minus initial elevation; negative means net erosion.
    /** @brief Return the map width. */
    int getWidth() const { return width; }
    /** @brief Return the map height. */
    int getHeight() const { return height; }
    /** @brief Sample gross erosion, or zero outside the map. */
    float getWear(int x, int y) const;
    /** @brief Sample deposited material, or zero outside the map. */
    float getDeposition(int x, int y) const;
    /** @brief Sample final-minus-initial elevation, or zero outside the map. */
    float getHeightDelta(int x, int y) const;
};

/** @brief Script-friendly ownership wrapper for baked hydrology and climate layers. */
class TerrainLayers {
public:
    TerrainLayers() = default;
    TerrainLayers(HydrologyMap hydrology, ClimateMap climate);

    int getWidth() const;
    int getHeight() const;
    float getFlowAccumulation(int x, int y) const;
    /** @brief Return the D8 receiver direction for one cell, or -1 at an outlet/out of bounds. */
    int getFlowDirection(int x, int y) const;
    /** @brief Return the continuous normalized downslope X component. */
    float getFlowVectorX(int x, int y) const;
    /** @brief Return the continuous normalized downslope Y component. */
    float getFlowVectorY(int x, int y) const;
    bool isRiver(int x, int y) const;
    /** @brief Return Strahler river order, or zero outside the river network. */
    int getStreamOrder(int x, int y) const;
    /** @brief Return filled-depression water depth, or zero outside a lake. */
    float getLakeDepth(int x, int y) const;
    /** @brief Return whether the cell belongs to a resolved closed-basin lake. */
    bool isLake(int x, int y, float minimumDepth = 0.001f) const;
    float getTemperature(int x, int y) const;
    float getMoisture(int x, int y) const;
    int getBiome(int x, int y) const;
    std::string getBiomeName(int x, int y) const;

    const HydrologyMap &hydrology() const { return hydrology_; }
    const ClimateMap &climate() const { return climate_; }

private:
    size_t index(int x, int y) const;
    HydrologyMap hydrology_;
    ClimateMap climate_;
};

/** @brief Deterministic CPU terrain baking stages shared by editors and runtime tools. */
class TerrainPipeline {
public:
    /** @brief Relax slopes exceeding the configured talus angle while conserving mass. */
    static void erodeThermal(Heightmap &heightmap, const ThermalErosionSettings &settings = {});
    /** @brief Grid-based water/sediment simulation that carves channels and deposits sediment. */
    static void erodeHydraulic(Heightmap &heightmap, const HydraulicErosionSettings &settings = {});
    /** @brief Cut branching river valleys using depression-free drainage and stream power. */
    static void erodeFluvial(Heightmap &heightmap, const FluvialErosionSettings &settings = {});
    /** @brief Cut river valleys and return wear, deposition, and net-change diagnostic layers. */
    static TerrainErosionMap erodeFluvialDetailed(
        Heightmap &heightmap, const FluvialErosionSettings &settings = {});
    /** @brief Compute D8 drainage, upstream contributing area, and a thresholded river mask. */
    static HydrologyMap buildHydrology(const Heightmap &heightmap, float riverThreshold = 0.025f,
                                       float seaLevel = 0.25f, float coordinateScale = 1.f,
                                       bool classifyLakes = true);
    /** @brief Derive temperature, moisture, and biome layers from terrain and hydrology. */
    static ClimateMap buildClimate(const Heightmap &heightmap, const HydrologyMap &hydrology,
                                   float seaLevel = 0.25f, float latitude = 0.35f,
                                   float coordinateScale = 1.f);
};

}  // namespace eve::procgen
