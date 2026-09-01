#include "pixelworld/PixelWorldGeneration.h"

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace eve::pixelworld {
namespace {

std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

std::uint64_t coordinateHash(std::uint64_t seed, int x, int y, std::uint64_t stream) noexcept {
    return mix(seed ^ (std::uint64_t(std::uint32_t(x)) << 32U) ^
               std::uint64_t(std::uint32_t(y)) ^ stream);
}

int floorDivGeneration(int value, int divisor) noexcept {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

int interpolatedNoise(std::uint64_t seed, int coordinate, int spacing,
                      std::uint64_t stream) noexcept {
    const int lattice = floorDivGeneration(coordinate, spacing);
    const int offset = coordinate - lattice * spacing;
    const int first = int(coordinateHash(seed, lattice, 0, stream) % 20001ULL) - 10000;
    const int second = int(coordinateHash(seed, lattice + 1, 0, stream) % 20001ULL) - 10000;
    return (first * (spacing - offset) + second * offset) / spacing;
}

PixelBiome biomeAt(std::uint64_t seed, int worldX) noexcept {
    const int band = floorDivGeneration(worldX, 192);
    return PixelBiome(coordinateHash(seed, band, 0, 0x42494F4D45ULL) % 4ULL);
}

PixelCell defaultCell(const MaterialCatalog& catalog, MaterialId material) {
    PixelCell cell;
    cell.material = material;
    const auto& definition = catalog.definition(material);
    cell.temperature = definition.defaultTemperature;
    cell.lifetime = std::uint8_t(std::min<std::uint16_t>(
        definition.defaultLifetime, std::numeric_limits<std::uint8_t>::max()));
    return cell;
}

MaterialId terrainMaterial(PixelBiome biome, int depth, std::uint64_t feature) noexcept {
    if (biome == PixelBiome::Desert && depth < 10) return MaterialId::Sand;
    if (biome == PixelBiome::Fungal && depth < 4) return MaterialId::Wood;
    if (biome == PixelBiome::Volcanic && depth > 18 && feature % 97ULL == 0)
        return MaterialId::Lava;
    return MaterialId::Stone;
}

eve::Result<PixelWorldGenerationOutput> invalidGeneration(std::string message,
                                                          std::string path) {
    return eve::Result<PixelWorldGenerationOutput>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {},
        "pixelworld.generation"));
}

void hashByte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;
}

template <class T>
void hashValue(std::uint64_t& hash, T value) noexcept {
    using Unsigned = std::make_unsigned_t<T>;
    const Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(T); ++index)
        hashByte(hash, std::uint8_t(bits >> (index * 8U)));
}

}  // namespace

eve::Result<PixelWorldGenerationOutput> generatePixelWorld(
    const PixelWorldGenerationRequest& request, const MaterialCatalog& catalog,
    std::uint64_t sourceRevision, eve::SimulationTick sourceTick,
    std::uint64_t sourceLastEditSequence) {
    if (request.schemaVersion != PixelWorldGenerationRequest::kSchemaVersion)
        return invalidGeneration("unsupported PixelWorld generation schema version", "schemaVersion");
    if (request.region.minX > request.region.maxX || request.region.minY > request.region.maxY)
        return invalidGeneration("generation region bounds are inverted", "region");
    const std::int64_t width = std::int64_t(request.region.maxX) - request.region.minX + 1;
    const std::int64_t height = std::int64_t(request.region.maxY) - request.region.minY + 1;
    if (width * height > 65'536)
        return invalidGeneration("generation region exceeds 65536 Chunks", "region");
    if (sourceRevision == 0)
        return invalidGeneration("source revision must be positive", "sourceRevision");
    if (catalog.definitions().size() <= std::size_t(MaterialId::Lava))
        return invalidGeneration("generation Catalog must provide the built-in terrain material ids",
                                 "catalog");
    if (request.terrainAmplitude < 0 || request.terrainAmplitude > 4096)
        return invalidGeneration("terrain amplitude must be in [0, 4096]", "terrainAmplitude");
    if (request.caveThreshold > 10'000)
        return invalidGeneration("cave threshold must be in [0, 10000]", "caveThreshold");
    std::uint64_t stampCells = 0;
    for (std::size_t index = 0; index < request.stamps.size(); ++index) {
        const auto& stamp = request.stamps[index];
        if (stamp.width <= 0 || stamp.height <= 0 ||
            std::uint64_t(stamp.width) * std::uint64_t(stamp.height) != stamp.cells.size())
            return invalidGeneration("stamp dimensions must match its row-major cells",
                                     "stamps[" + std::to_string(index) + "]");
        stampCells += stamp.cells.size();
        if (stampCells > 4'194'304)
            return invalidGeneration("material stamps exceed the cell budget", "stamps");
        for (const PixelCell cell : stamp.cells)
            if (std::size_t(cell.material) >= catalog.definitions().size())
                return invalidGeneration("stamp references an unknown material",
                                         "stamps[" + std::to_string(index) + "].cells");
    }

    PixelWorldGenerationOutput output;
    output.batch.catalogFingerprint = catalog.fingerprint();
    output.batch.sourceSeed = request.seed;
    output.batch.sourceRevision = sourceRevision;
    output.batch.sourceTick = sourceTick;
    output.batch.sourceLastEditSequence = sourceLastEditSequence;
    output.batch.chunks.reserve(std::size_t(width * height));
    output.chunks.reserve(std::size_t(width * height));
    std::uint64_t hash = 1469598103934665603ULL;

    for (int chunkY = request.region.minY; chunkY <= request.region.maxY; ++chunkY)
        for (int chunkX = request.region.minX; chunkX <= request.region.maxX; ++chunkX) {
            PixelChunkSnapshot snapshot;
            snapshot.x = chunkX;
            snapshot.y = chunkY;
            snapshot.revision = sourceRevision;
            snapshot.cells.resize(std::size_t(kPixelChunkSize * kPixelChunkSize));
            PixelGeneratedChunkSummary summary;
            summary.x = chunkX;
            summary.y = chunkY;
            summary.biome = biomeAt(request.seed, chunkX * kPixelChunkSize + kPixelChunkSize / 2);
            for (int localY = 0; localY < kPixelChunkSize; ++localY)
                for (int localX = 0; localX < kPixelChunkSize; ++localX) {
                    const int worldX = chunkX * kPixelChunkSize + localX;
                    const int worldY = chunkY * kPixelChunkSize + localY;
                    const PixelBiome biome = biomeAt(request.seed, worldX);
                    const int surface = request.surfaceY +
                        interpolatedNoise(request.seed, worldX, 32, 0x53555246414345ULL) *
                            request.terrainAmplitude / 10000;
                    PixelCell cell;
                    if (worldY >= surface) {
                        const int depth = worldY - surface;
                        const std::uint64_t caveNoise = coordinateHash(
                            request.seed, floorDivGeneration(worldX, 5),
                            floorDivGeneration(worldY, 5), 0x43415645ULL) % 10001ULL;
                        const bool cave = depth > 5 && caveNoise < request.caveThreshold;
                        if (cave) {
                            ++summary.caveCells;
                            if (worldY >= request.waterLevel &&
                                coordinateHash(request.seed, worldX, worldY, 0x5741544552ULL) % 5ULL == 0)
                                cell = defaultCell(catalog, MaterialId::Water);
                        } else {
                            const std::uint64_t feature = coordinateHash(
                                request.seed, worldX, worldY, 0x46454154555245ULL);
                            cell = defaultCell(catalog, terrainMaterial(biome, depth, feature));
                            ++summary.solidCells;
                        }
                    }
                    snapshot.cells[std::size_t(localY * kPixelChunkSize + localX)] = cell;
                }

            const int chunkMinX = chunkX * kPixelChunkSize;
            const int chunkMinY = chunkY * kPixelChunkSize;
            for (const PixelMaterialStamp& stamp : request.stamps)
                for (int stampY = 0; stampY < stamp.height; ++stampY)
                    for (int stampX = 0; stampX < stamp.width; ++stampX) {
                        const int worldX = stamp.originX + stampX;
                        const int worldY = stamp.originY + stampY;
                        if (worldX < chunkMinX || worldX >= chunkMinX + kPixelChunkSize ||
                            worldY < chunkMinY || worldY >= chunkMinY + kPixelChunkSize)
                            continue;
                        snapshot.cells[std::size_t(worldY - chunkMinY) * kPixelChunkSize +
                                       std::size_t(worldX - chunkMinX)] =
                            stamp.cells[std::size_t(stampY) * std::size_t(stamp.width) + stampX];
                        ++summary.stampedCells;
                    }

            hashValue(hash, std::int32_t(chunkX));
            hashValue(hash, std::int32_t(chunkY));
            for (const PixelCell cell : snapshot.cells) {
                hashValue(hash, std::uint16_t(cell.material));
                hashValue(hash, cell.temperature);
                hashValue(hash, cell.lifetime);
                hashValue(hash, cell.thermalRemainder);
            }
            output.batch.chunks.push_back(std::move(snapshot));
            output.chunks.push_back(summary);
        }
    output.contentHash = hash;
    return eve::Result<PixelWorldGenerationOutput>::success(std::move(output));
}

}  // namespace eve::pixelworld
