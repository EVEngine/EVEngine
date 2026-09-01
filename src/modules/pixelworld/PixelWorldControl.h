#pragma once

#include "pixelworld/PixelWorld.h"

#include <cstdint>
#include <span>
#include <vector>

namespace eve::pixelworld {

/** @brief Stable summary of one live PixelWorld registered for local tooling. */
struct PixelWorldSummary {
    PixelWorldLink link;
    std::uint64_t seed = 0;
    std::uint64_t revision = 0;
    std::uint64_t tick = 0;
    std::uint64_t lastEditSequence = 0;
    std::uint32_t chunks = 0;
    std::uint32_t activeChunks = 0;
    bool paused = false;
};

/** @brief Receipt for an explicit bounded tooling step. */
struct PixelWorldStepReceipt {
    std::uint64_t world = 0;
    std::uint32_t steps = 0;
    std::uint64_t firstTick = 0;
    std::uint64_t lastTick = 0;
    std::uint64_t elapsedMicroseconds = 0;
    StepStats finalStep;
};

/** @brief One bounded process-local simulation performance sample. */
struct PixelWorldPerformanceSample {
    std::uint64_t tick = 0;
    std::uint64_t elapsedMicroseconds = 0;
    StepStats stats;
};

/**
 * @brief Main-thread-affine control surface shared by editor, CLI and MCP adapters.
 * @remarks The service borrows live worlds only while they are registered by their
 * constructors. PixelWorld destruction and move rebinding prevent stale retention.
 */
class PixelWorldControlService {
public:
    /** @brief Return canonical world-id-ordered summaries of all live worlds. */
    [[nodiscard]] std::vector<PixelWorldSummary> worlds() const;
    /** @brief Query one live world summary. */
    [[nodiscard]] eve::Result<PixelWorldSummary> world(std::uint64_t worldId) const;
    /** @brief Pause or resume one live world. */
    [[nodiscard]] eve::Result<void> setPaused(std::uint64_t worldId, bool paused);
    /** @brief Advance a live world explicitly even while paused; count is limited to 1024. */
    [[nodiscard]] eve::Result<PixelWorldStepReceipt> step(std::uint64_t worldId,
                                                         std::uint32_t count = 1);
    /** @brief Apply the world's canonical strictly sequenced edit operation. */
    [[nodiscard]] eve::Result<PixelEditReceipt> applyEdit(std::uint64_t worldId,
                                                          const PixelEditCommand& command);
    /** @brief Validate JSON and transactionally hot-reload a compatible paused world Catalog. */
    [[nodiscard]] eve::Result<PixelCatalogReloadReceipt> reloadMaterialCatalog(
        std::uint64_t worldId, std::string_view catalogJson,
        std::uint64_t expectedFingerprint);
    /** @brief Copy a bounded read-only Chunk diagnostic region. */
    [[nodiscard]] eve::Result<std::vector<PixelChunkDiagnostic>> chunkDiagnostics(
        std::uint64_t worldId, PixelChunkRegion region) const;
    /** @brief Copy up to the latest 256 simulation performance samples. */
    [[nodiscard]] eve::Result<std::vector<PixelWorldPerformanceSample>> performanceSamples(
        std::uint64_t worldId, std::uint32_t limit = 120) const;
    /** @brief Capture an owning canonical world snapshot. */
    [[nodiscard]] eve::Result<std::vector<std::byte>> captureSnapshot(std::uint64_t worldId) const;
    /** @brief Transactionally restore a captured world snapshot. */
    [[nodiscard]] eve::Result<void> restoreSnapshot(std::uint64_t worldId,
                                                    std::span<const std::byte> bytes);

private:
    friend class PixelWorld;
    void registerWorld(PixelWorld& world);
    void unregisterWorld(const PixelWorld& world) noexcept;
    void rebindWorld(const PixelWorld* previous, PixelWorld& replacement) noexcept;
    void recordStep(std::uint64_t worldId, const StepStats& stats,
                    std::uint64_t elapsedMicroseconds);
    [[nodiscard]] PixelWorld* find(std::uint64_t worldId) const noexcept;

    struct Impl;
    Impl& impl() const;
};

/** @brief Process-lifetime PixelWorld tooling registry. */
PixelWorldControlService& pixelWorldControlService();

}  // namespace eve::pixelworld
