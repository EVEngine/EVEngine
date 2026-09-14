#include "procgen/heightmap/TerrainFile.h"

#include "common/Capability.h"
#include "common/ServiceInterfaces.h"
#include "procgen/heightmap/TerrainAsset.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace eve::procgen {
namespace {

constexpr std::uint8_t kEvtrnMagic[8] = {'E', 'V', 'T', 'R', 'N', 0, 1, 0};
constexpr std::size_t   kEvtrnHeader  = 24;
/** Guards against a corrupt header allocating absurd amounts of memory. */
constexpr std::uint32_t kMaximumDimension = 32768;

eve::Result<DecodedTerrainFile> terrainFileFailure(eve::DiagnosticCode code, std::string message,
                                                   std::string path) {
    return eve::Result<DecodedTerrainFile>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.terrain-file"));
}

std::uint32_t littleU32(const std::uint8_t *bytes) {
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) |
           (std::uint32_t(bytes[3]) << 24);
}

float littleF32(const std::uint8_t *bytes) {
    return std::bit_cast<float>(littleU32(bytes));
}

bool startsWith(std::span<const std::uint8_t> bytes, const std::uint8_t *magic, std::size_t size) {
    return bytes.size() >= size && std::memcmp(bytes.data(), magic, size) == 0;
}

/** @brief Decode the raw `EVTRN` float32 heightfield payload. */
eve::Result<DecodedTerrainFile> decodeEvtrn(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kEvtrnHeader)
        return terrainFileFailure(eve::DiagnosticCode::ParseError, "EVTRN payload is truncated", "evtrn");
    const std::uint32_t width    = littleU32(bytes.data() + 8);
    const std::uint32_t height   = littleU32(bytes.data() + 12);
    const float         spacingX = littleF32(bytes.data() + 16);
    const float         spacingZ = littleF32(bytes.data() + 20);
    const std::uint64_t samples  = std::uint64_t(width) * std::uint64_t(height);
    if (width == 0 || height == 0 || width > kMaximumDimension || height > kMaximumDimension)
        return terrainFileFailure(eve::DiagnosticCode::InvalidArgument, "EVTRN dimensions are invalid", "evtrn");
    if (!std::isfinite(spacingX) || !std::isfinite(spacingZ) || spacingX <= 0.f || spacingZ <= 0.f)
        return terrainFileFailure(eve::DiagnosticCode::InvalidArgument, "EVTRN spacing is invalid", "evtrn");
    if (samples > (std::numeric_limits<std::uint64_t>::max() - kEvtrnHeader) / 4 ||
        samples * 4 + kEvtrnHeader != bytes.size())
        return terrainFileFailure(eve::DiagnosticCode::InvalidArgument,
                                  "EVTRN payload size does not match its dimensions", "evtrn");

    DecodedTerrainFile decoded;
    decoded.heightmap.resize(int(width), int(height));
    decoded.spacingX   = spacingX;
    decoded.spacingZ   = spacingZ;
    decoded.hasSpacing = true;
    decoded.format     = "evtrn";
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    std::size_t cursor = kEvtrnHeader;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const float value = littleF32(bytes.data() + cursor);
            cursor += 4;
            if (!std::isfinite(value))
                return terrainFileFailure(eve::DiagnosticCode::ParseError,
                                          "EVTRN contains a non-finite height", "evtrn");
            decoded.heightmap.setHeight(int(x), int(y), value);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    decoded.minHeight = minimum;
    decoded.maxHeight = maximum;
    return eve::Result<DecodedTerrainFile>::success(std::move(decoded));
}

/** @brief Decode a chunked `EVTR` archive by blitting every chunk into one grid. */
eve::Result<DecodedTerrainFile> decodeEvtr(std::span<const std::uint8_t> bytes) {
    TerrainAsset asset;
    std::string  error;
    if (!asset.open(bytes.data(), bytes.size(), &error))
        return terrainFileFailure(eve::DiagnosticCode::ParseError,
                                  error.empty() ? "EVTR archive is invalid" : error, "evtr");

    DecodedTerrainFile decoded;
    decoded.heightmap.resize(asset.getWidth(), asset.getHeight());
    decoded.minHeight = asset.getMinHeight();
    decoded.maxHeight = asset.getMaxHeight();
    // EVTR stores no metres-per-cell, so the referencing level owns the spacing.
    decoded.hasSpacing = false;
    decoded.format     = "evtr";

    const int chunkSize = asset.getChunkSize();
    for (const TerrainChunkEntry &entry : asset.chunks()) {
        TerrainChunkData chunk;
        error.clear();
        if (!asset.loadChunk(entry.chunkX, entry.chunkY, chunk, &error))
            return terrainFileFailure(eve::DiagnosticCode::ParseError,
                                      error.empty() ? "EVTR chunk failed to decode" : error, "evtr");
        for (int y = 0; y < chunk.height; ++y) {
            for (int x = 0; x < chunk.width; ++x) {
                const int gx = entry.chunkX * chunkSize + x;
                const int gy = entry.chunkY * chunkSize + y;
                if (!decoded.heightmap.inBounds(gx, gy)) continue;
                decoded.heightmap.setHeight(gx, gy, chunk.heights.height(x, y));
            }
        }
    }
    return eve::Result<DecodedTerrainFile>::success(std::move(decoded));
}

}  // namespace

TerrainFileFormat parseTerrainFileFormat(std::string_view name) noexcept {
    if (name.empty()) return TerrainFileFormat::Auto;
    std::string lowered(name);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (lowered == "evtr") return TerrainFileFormat::Evtr;
    if (lowered == "evtrn") return TerrainFileFormat::Evtrn;
    return TerrainFileFormat::Auto;
}

eve::Result<DecodedTerrainFile> decodeTerrainFile(std::span<const std::uint8_t> bytes,
                                                  TerrainFileFormat           format) {
    if (bytes.empty())
        return terrainFileFailure(eve::DiagnosticCode::InvalidArgument, "terrain file is empty", "terrain");

    if (format == TerrainFileFormat::Auto) {
        if (startsWith(bytes, kEvtrnMagic, sizeof(kEvtrnMagic))) format = TerrainFileFormat::Evtrn;
        else if (startsWith(bytes, reinterpret_cast<const std::uint8_t *>("EVTR"), 4)) format = TerrainFileFormat::Evtr;
        else
            return terrainFileFailure(eve::DiagnosticCode::Unsupported,
                                      "unrecognised terrain file magic (expected EVTR or EVTRN)", "terrain");
    }
    if (format == TerrainFileFormat::Evtrn) {
        if (!startsWith(bytes, kEvtrnMagic, sizeof(kEvtrnMagic)))
            return terrainFileFailure(eve::DiagnosticCode::ParseError,
                                      "EVTRN magic does not match the requested format", "evtrn");
        return decodeEvtrn(bytes);
    }
    return decodeEvtr(bytes);
}

eve::Result<DecodedTerrainFile> loadTerrainFile(const std::string &path, TerrainFileFormat format) {
    auto *filesystem = eve::cap::query<eve::service::IFileSystem>();
    if (filesystem == nullptr)
        return terrainFileFailure(eve::DiagnosticCode::Unsupported,
                                  "no filesystem service is registered in this build", "terrain");

    std::vector<std::uint8_t> bytes;
    if (!filesystem->readFile(path, bytes))
        return terrainFileFailure(eve::DiagnosticCode::NotFound, "terrain file could not be read: " + path, path);
    return decodeTerrainFile(std::span<const std::uint8_t>(bytes.data(), bytes.size()), format);
}

}  // namespace eve::procgen
