#pragma once

#include "pixelworld/PixelWorld.h"

#include <cstdint>
#include <vector>

namespace eve::pixelworld {

/** @brief Stable coarse biome selected from deterministic world coordinates. */
enum class PixelBiome : std::uint8_t { Forest, Desert, Fungal, Volcanic };

/** @brief Owning row-major material stamp applied after terrain and cave generation. */
struct PixelMaterialStamp {
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
    std::vector<PixelCell> cells;
};

/** @brief Versioned bounded request for deterministic biome and cave generation. */
struct PixelWorldGenerationRequest {
    static constexpr std::uint32_t kSchemaVersion = 1;
    std::uint32_t schemaVersion = kSchemaVersion;
    std::uint64_t seed = 1;
    PixelChunkRegion region;
    int surfaceY = 0;
    int terrainAmplitude = 24;
    int waterLevel = 36;
    /** @brief Cave admission threshold in [0, 10000]; larger values carve more cells. */
    std::uint32_t caveThreshold = 3600;
    std::vector<PixelMaterialStamp> stamps;
};

/** @brief Deterministic diagnostics for one generated Chunk. */
struct PixelGeneratedChunkSummary {
    int x = 0;
    int y = 0;
    PixelBiome biome = PixelBiome::Forest;
    std::uint32_t solidCells = 0;
    std::uint32_t caveCells = 0;
    std::uint32_t stampedCells = 0;

    friend bool operator==(const PixelGeneratedChunkSummary&,
                           const PixelGeneratedChunkSummary&) = default;
};

/** @brief Owning generated correction plus reproducibility evidence. */
struct PixelWorldGenerationOutput {
    PixelChunkBatch batch;
    std::vector<PixelGeneratedChunkSummary> chunks;
    std::uint64_t contentHash = 0;
};

/**
 * @brief Generate a seamless canonical Chunk batch without mutating a PixelWorld.
 * @param request Versioned finite generation request and optional owning stamps.
 * @param catalog Catalog whose ids/default cells give generated materials meaning.
 * @param sourceRevision Positive authority revision assigned to every output Chunk.
 * @param sourceTick Authority Tick carried by the correction batch.
 * @param sourceLastEditSequence Authority edit sequence carried by the correction batch.
 * @return Owning canonical batch and summaries, or a structured bounded-input error.
 * @remarks Integer coordinate hashing makes output bit-exact across worker count and backend.
 * The caller may transactionally admit `output.batch` through `PixelWorld::applyChunkBatch`.
 * Unknown schema versions are rejected and no world state is observed or changed.
 */
[[nodiscard]] eve::Result<PixelWorldGenerationOutput> generatePixelWorld(
    const PixelWorldGenerationRequest& request, const MaterialCatalog& catalog,
    std::uint64_t sourceRevision, eve::SimulationTick sourceTick = eve::SimulationTick::zero(),
    std::uint64_t sourceLastEditSequence = 0);

}  // namespace eve::pixelworld
