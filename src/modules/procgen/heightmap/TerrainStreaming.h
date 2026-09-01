#pragma once

#include "procgen/heightmap/TerrainAsset.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::procgen {

struct TerrainStreamStats {
    int loaded = 0;
    int evicted = 0;
    int pending = 0;
    int failed = 0;
    int resident = 0;
};

struct TerrainSample {
    float height = 0.f, flowAccumulation = 0.f, lakeDepth = 0.f;
    float flowVectorX = 0.f, flowVectorY = 0.f;
    float temperature = 0.f, moisture = 0.f;
    int flowDirection = -1, streamOrder = 0;
    bool river = false, lake = false;
    Biome biome = Biome::Ocean;
};

/** @brief A rectangular, globally addressed terrain window assembled from resident chunks. */
struct TerrainStreamingWindow {
    int originX = 0, originY = 0;
    Heightmap heights;
    HydrologyMap hydrology;
    ClimateMap climate;
};

/**
 * @brief Runtime decoded-chunk cache for an EVTR terrain asset.
 *
 * Requests are ordered nearest-first and may be limited per call with
 * `maxLoads`, allowing a game to spread decompression across frames. Chunks
 * outside the requested circular radius are evicted before new chunks load.
 */
class TerrainStreamingCache {
public:
    /** @brief Compatibility operation that opens EVTR and clears decoded chunks. */
    bool open(const uint8_t *data, size_t size, std::string *error = nullptr);
    void clear();

    /**
     * @brief Stream chunks around a world-space sample coordinate.
     * @param radiusChunks Circular residency radius in chunks; negative is a no-op.
     * @param maxLoads Maximum new chunks decoded this call; zero means unlimited.
     */
    TerrainStreamStats streamAround(int worldX, int worldY, int radiusChunks,
                                    int maxLoads = 0, std::string *error = nullptr);

    /** @brief Borrow a resident chunk. @lifetime Pointer remains valid until cache mutation. */
    const TerrainChunkData *getChunk(int chunkX, int chunkY) const;
    /** @brief Compatibility operation that reads a resident integer world cell. */
    bool sampleCell(int worldX, int worldY, TerrainSample &out) const;
    /** @brief Compatibility operation for cross-chunk bilinear height sampling. */
    bool sampleHeight(float worldX, float worldY, float &out) const;
    /**
     * @brief Resolve a cell's D8 receiver in global coordinates, including an adjacent chunk.
     * @return False for outlets, invalid directions, or when either required chunk is absent.
     */
    bool getReceiver(int worldX, int worldY, int &receiverX, int &receiverY) const;
    /**
     * @brief Compatibility operation that follows D8 receivers across chunks.
     * @param maxSteps Safety limit; must be positive.
     * @param out Global cell coordinates, including the starting cell.
     * @return False if tracing reaches a non-resident chunk or an invalid/cyclic receiver.
     */
    bool traceFlow(int worldX, int worldY, int maxSteps,
                   std::vector<std::pair<int, int>> &out) const;
    /**
     * @brief Compatibility operation that assembles a dense hydrology window.
     *
     * Requesting one extra cell on each side provides the halo needed for seam-stable
     * river ribbons, normals, and bank calculations. Coordinates are global cells.
     */
    bool buildWindow(int originX, int originY, int width, int height,
                     TerrainStreamingWindow &out) const;

    int getResidentCount() const { return int(resident_.size()); }
    const TerrainAsset &asset() const { return asset_; }

private:
    static uint64_t key(int chunkX, int chunkY);
    TerrainAsset asset_;
    std::unordered_map<uint64_t, TerrainChunkData> resident_;
};

}  // namespace eve::procgen
