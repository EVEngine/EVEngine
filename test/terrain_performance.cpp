#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace eve;
using namespace eve::procgen;

namespace {
constexpr std::size_t kHeaderSize = 44, kEntrySize = 36;
void                  put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
void putFloat(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put32(out, bits);
}
std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t value = 2166136261U;
    for (auto byte : bytes) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}
double milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

/** Full EVTR address space backed only for metadata and the chunks actually read. */
class SparseTerrainArchiveSource final : public ITerrainArchiveSource {
public:
    explicit SparseTerrainArchiveSource(int dimension) {
        constexpr int chunkSize = 64;
        for (int index = 0; index < chunkSize * chunkSize; ++index) {
            put16(chunk_, std::uint16_t(index));
            chunk_.insert(chunk_.end(), {255, 0, 128, 128, 0, 0, 128, 128, 0, 2});
        }
        const int  columns = (dimension + chunkSize - 1) / chunkSize;
        const auto count   = std::uint32_t(columns * columns);
        payloadStart_      = kHeaderSize + std::uint64_t(count) * kEntrySize;
        metadata_.insert(metadata_.end(), {'E', 'V', 'T', 'R'});
        put16(metadata_, 5);
        put16(metadata_, 0);
        put32(metadata_, dimension);
        put32(metadata_, dimension);
        put32(metadata_, chunkSize);
        put32(metadata_, count);
        putFloat(metadata_, 0);
        putFloat(metadata_, 1);
        putFloat(metadata_, 1);
        put64(metadata_, kHeaderSize);
        const auto    chunkChecksum = checksum(chunk_);
        std::uint64_t offset        = payloadStart_;
        for (int y = 0; y < columns; ++y)
            for (int x = 0; x < columns; ++x) {
                put32(metadata_, x);
                put32(metadata_, y);
                put16(metadata_, chunkSize);
                put16(metadata_, chunkSize);
                metadata_.insert(metadata_.end(), {0, 0, 0, 0});
                put64(metadata_, offset);
                put32(metadata_, chunk_.size());
                put32(metadata_, chunk_.size());
                put32(metadata_, chunkChecksum);
                offset += chunk_.size();
            }
        size_ = offset;
    }
    [[nodiscard]] std::uint64_t                     size() const noexcept override { return size_; }
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(std::uint64_t offset, std::size_t length) const override {
        bytesRead_ += length;
        ++reads_;
        if (offset <= metadata_.size() && length <= metadata_.size() - offset) {
            auto begin = metadata_.begin() + std::ptrdiff_t(offset);
            return Result<std::vector<std::uint8_t>>::success({begin, begin + std::ptrdiff_t(length)});
        }
        if (offset >= payloadStart_ && length == chunk_.size() && (offset - payloadStart_) % chunk_.size() == 0 &&
            offset + length <= size_)
            return Result<std::vector<std::uint8_t>>::success(chunk_);
        return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                            "sparse terrain read is out of range", {},
                                                                            {}, "procgen.terrain-performance"));
    }
    [[nodiscard]] std::uint64_t bytesRead() const noexcept { return bytesRead_; }
    [[nodiscard]] std::uint64_t reads() const noexcept { return reads_; }

private:
    std::uint64_t             payloadStart_ = 0, size_ = 0;
    std::vector<std::uint8_t> metadata_, chunk_;
    mutable std::uint64_t     bytesRead_ = 0, reads_ = 0;
};
}  // namespace

TEST_CASE("procgen.terrain.performance.real1kRoundTripAnd1k4k8kRandomAccess") {
    constexpr int     dimension = 1024, chunkSize = 64;
    const std::size_t count = std::size_t(dimension) * dimension;
    Heightmap         heightmap(dimension, dimension);
    HydrologyMap      hydrology;
    hydrology.width = hydrology.height = dimension;
    hydrology.flowAccumulation.assign(count, 1);
    hydrology.rivers.assign(count, 0);
    ClimateMap climate;
    climate.width = climate.height = dimension;
    climate.temperature.assign(count, .5F);
    climate.moisture.assign(count, .5F);
    climate.biomes.assign(count, Biome::Grassland);
    for (int y = 0; y < dimension; ++y)
        for (int x = 0; x < dimension; ++x)
            heightmap.setHeight(x, y, .5F + .25F * std::sin(float(x) * .011F) * std::cos(float(y) * .013F));
    std::vector<std::uint8_t> archive;
    std::string               error;
    const auto                bakeBegin = std::chrono::steady_clock::now();
    REQUIRE(TerrainAsset::bake(heightmap, hydrology, climate, chunkSize, archive, &error));
    const double     bakeMs = milliseconds(std::chrono::steady_clock::now() - bakeBegin);
    TerrainAsset     roundTrip;
    TerrainChunkData decoded;
    REQUIRE(roundTrip.open(archive.data(), archive.size(), &error));
    REQUIRE(roundTrip.loadChunk(7, 9, decoded, &error));
    CHECK(std::abs(decoded.heights.height(3, 5) - heightmap.height(451, 581)) < .001F);
    CHECK(bakeMs < 20000);
    std::printf("terrain.performance dense_dimension=1024 archive_bytes=%zu bake_ms=%.3f\n", archive.size(), bakeMs);

    for (int worldDimension : std::array{1024, 4096, 8192}) {
        auto                  source = std::make_shared<SparseTerrainArchiveSource>(worldDimension);
        TerrainStreamingCache streaming;
        const auto            openBegin = std::chrono::steady_clock::now();
        REQUIRE(streaming.openSource(source).ok());
        const double        openMs = milliseconds(std::chrono::steady_clock::now() - openBegin);
        std::vector<double> moves;
        for (int index = 0; index < 20; ++index) {
            const int  coordinate = (index * 389) % (worldDimension - 1);
            const auto begin      = std::chrono::steady_clock::now();
            auto       stats      = streaming.streamAround(coordinate, worldDimension - 1 - coordinate, 2, 4, &error);
            moves.push_back(milliseconds(std::chrono::steady_clock::now() - begin));
            CHECK_EQ(stats.failed, 0);
            CHECK(stats.resident <= 13);
        }
        std::sort(moves.begin(), moves.end());
        const auto columns        = std::uint64_t((worldDimension + chunkSize - 1) / chunkSize);
        const auto directoryBytes = kHeaderSize + columns * columns * kEntrySize;
        CHECK(source->bytesRead() <= directoryBytes * 2 + 80 * chunkSize * chunkSize * 12);
        CHECK(openMs < 1000);
        CHECK(moves[18] < 100);
        std::printf(
            "terrain.performance sparse_dimension=%d virtual_bytes=%llu reads=%llu "
            "bytes_read=%llu open_ms=%.3f stream_p95_ms=%.3f\n",
            worldDimension, static_cast<unsigned long long>(source->size()),
            static_cast<unsigned long long>(source->reads()), static_cast<unsigned long long>(source->bytesRead()),
            openMs, moves[18]);
    }
}
