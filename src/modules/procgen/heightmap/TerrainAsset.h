#pragma once

#include "procgen/heightmap/TerrainPipeline.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen {

/** @brief Decoded terrain chunk containing all baked runtime layers. */
struct TerrainChunkData {
    int chunkX = 0, chunkY = 0;
    int width = 0, height = 0;
    Heightmap heights;
    std::vector<float> flowAccumulation, flowVectorX, flowVectorY;
    std::vector<float> lakeDepth, temperature, moisture;
    std::vector<int8_t> flowDirection;
    std::vector<uint8_t> rivers, streamOrder;
    std::vector<Biome> biomes;
};

/** @brief Metadata for one independently compressed chunk in an EVTR archive. */
struct TerrainChunkEntry {
    int chunkX = 0, chunkY = 0;
    int width = 0, height = 0;
    uint64_t offset = 0;
    uint32_t storedSize = 0, rawSize = 0, checksum = 0;
    bool compressed = false;
};

/**
 * @brief Versioned, random-access terrain archive.
 *
 * EVTR stores a fixed header and deterministic chunk directory followed by
 * independently PackBits-compressed payloads. Heights use archive-wide UNORM16;
 * temperature, moisture, normalized drainage and normalized lake depth use
 * UNORM8. Continuous flow vectors use two signed-normalized bytes; flow
 * direction, river and biome layers remain exact bytes. A corrupt chunk cannot
 * silently enter the world.
 */
class TerrainAsset {
public:
    /** @brief Bake all terrain layers into a portable EVTR byte buffer. */
    static bool bake(const Heightmap &heightmap, const HydrologyMap &hydrology,
                     const ClimateMap &climate, int chunkSize, std::vector<uint8_t> &out,
                     std::string *error = nullptr);

    /** @brief Open only the header and directory; chunk payloads stay compressed. */
    bool open(const uint8_t *data, size_t size, std::string *error = nullptr);
    /** @brief Load and verify one chunk without decoding any other chunk. */
    bool loadChunk(int chunkX, int chunkY, TerrainChunkData &out,
                   std::string *error = nullptr) const;

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getChunkSize() const { return chunkSize_; }
    float getMinHeight() const { return minHeight_; }
    float getMaxHeight() const { return maxHeight_; }
    const std::vector<TerrainChunkEntry> &chunks() const { return chunks_; }

private:
    int width_ = 0, height_ = 0, chunkSize_ = 0;
    uint16_t version_ = 0;
    float minHeight_ = 0.f, maxHeight_ = 1.f, maxFlow_ = 1.f;
    std::vector<uint8_t> bytes_;
    std::vector<TerrainChunkEntry> chunks_;
};

}  // namespace eve::procgen
