#pragma once

#include "procgen/heightmap/TerrainPipeline.h"
#include "common/Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eve::procgen {

/** @brief Immutable random-access byte source for an EVTR archive. */
class ITerrainArchiveSource {
public:
    virtual ~ITerrainArchiveSource() = default;
    /** @brief Total stable byte length of the archive. @thread Safe for concurrent reads. */
    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    /**
     * @brief Read exactly one bounded byte range.
     * @return Owning bytes, or a structured failure without changing reader state.
     * @thread Safe for concurrent calls. @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] virtual Result<std::vector<std::uint8_t>> read(
        std::uint64_t offset, std::size_t length) const = 0;
};

/** @brief Owning in-memory EVTR source used by compatibility callers and tests. */
class MemoryTerrainArchiveSource final : public ITerrainArchiveSource {
public:
    /** @brief Copy immutable archive bytes into this source. */
    explicit MemoryTerrainArchiveSource(std::span<const std::uint8_t> bytes)
        : bytes_(bytes.begin(), bytes.end()) {}
    [[nodiscard]] std::uint64_t size() const noexcept override { return bytes_.size(); }
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(
        std::uint64_t offset, std::size_t length) const override;

private:
    std::vector<std::uint8_t> bytes_;
};

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
    /** @brief Compatibility operation that bakes all terrain layers into EVTR. */
    static bool bake(const Heightmap &heightmap, const HydrologyMap &hydrology,
                     const ClimateMap &climate, int chunkSize, std::vector<uint8_t> &out,
                     std::string *error = nullptr);

    /** @brief Compatibility operation that opens only the header and directory. */
    bool open(const uint8_t *data, size_t size, std::string *error = nullptr);
    /**
     * @brief Open only EVTR metadata from a shared immutable random-access source.
     * @param source Shared owner that remains retained for later chunk reads.
     * @return Success after validating the complete header and directory; failure publishes no state.
     * @thread The asset is not safe to mutate concurrently; chunk reads inherit source read safety.
     */
    [[nodiscard]] Result<void> openSource(std::shared_ptr<const ITerrainArchiveSource> source);
    /** @brief Compatibility operation that loads and verifies one chunk. */
    bool loadChunk(int chunkX, int chunkY, TerrainChunkData &out,
                   std::string *error = nullptr) const;

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getChunkSize() const { return chunkSize_; }
    float getMinHeight() const { return minHeight_; }
    float getMaxHeight() const { return maxHeight_; }
    const std::vector<TerrainChunkEntry> &chunks() const { return chunks_; }
    /** @brief Find one chunk directory entry without scanning the full directory. */
    [[nodiscard]] const TerrainChunkEntry* findChunk(int chunkX, int chunkY) const noexcept;

private:
    bool openMetadata(const std::uint8_t* data, std::size_t metadataSize,
                      std::uint64_t archiveSize, std::string* error);
    int width_ = 0, height_ = 0, chunkSize_ = 0;
    uint16_t version_ = 0;
    float minHeight_ = 0.f, maxHeight_ = 1.f, maxFlow_ = 1.f;
    std::shared_ptr<const ITerrainArchiveSource> source_;
    std::vector<TerrainChunkEntry> chunks_;
};

}  // namespace eve::procgen
