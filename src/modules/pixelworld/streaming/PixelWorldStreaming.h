#pragma once

#include "common/Result.h"
#include "common/Snapshot.h"
#include "pixelworld/PixelWorld.h"

#include <cstdint>
#include <set>
#include <utility>

namespace eve::pixelworld_streaming {

/** @brief Stable physical codec for one independently stored PixelWorld Chunk. */
enum class PixelChunkArchiveCodec : std::uint8_t { None = 0, Zstd = 1 };

/**
 * @brief Encode one authoritative batch as `pixelworld:chunk-batch` SnapshotEnvelope version 3.
 * @param batch Canonical owning batch to persist.
 * @param instanceId Stable world/save identity.
 * @param codec Per-present-Chunk physical compression codec.
 * @param hashProvider Injected digest used by the envelope and every decoded Chunk.
 * @return Integrity-sealed archive with independently compressed Chunk records.
 */
[[nodiscard]] eve::Result<eve::SnapshotEnvelope> archiveChunkBatch(
    const eve::pixelworld::PixelChunkBatch& batch, eve::PersistentId instanceId,
    PixelChunkArchiveCodec codec, const eve::SnapshotHashProvider& hashProvider);

/**
 * @brief Verify and decode a version 1, 2 or 3 Chunk-batch archive into an owning candidate.
 * @remarks Decoding is bounded and has no world side effects. Version 1 migrates with
 * `sourceLastEditSequence == 0`; versions 1 and 2 migrate with `fullResync == false`.
 * Bad envelope/Chunk hashes fail before any authority write.
 */
[[nodiscard]] eve::Result<eve::pixelworld::PixelChunkBatch> decodeChunkBatchArchive(
    const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);

/** @brief One interest-stream update suitable for persistence or network transport. */
struct PixelChunkStreamUpdate {
    eve::pixelworld::PixelChunkBatch batch;
    /** @brief Mirrors `batch.fullResync` for capture diagnostics. */
    bool fullResync = false;
    std::uint32_t chunksEntered = 0;
    std::uint32_t chunksEvicted = 0;
};

/**
 * @brief Stateful revision cursor that projects one PixelWorld interest region.
 *
 * The cursor owns only consumer projection state: source link, last revision and
 * known Chunk coordinates. PixelWorld remains the sole material authority. APIs
 * are simulation-thread affine, retain no world pointer and invoke no callbacks.
 */
class PixelChunkStreamCursor {
public:
    /**
     * @brief Capture changed/entered Chunks and eviction tombstones for an interest region.
     * @param source World borrowed synchronously and never retained.
     * @param interest Inclusive Chunk-coordinate rectangle, bounded by PixelWorld.
     * @return Owning canonical correction batch. Source epoch changes force full resync.
     * @remarks Failure leaves cursor revision and known coordinates unchanged.
     */
    [[nodiscard]] eve::Result<PixelChunkStreamUpdate> capture(
        const eve::pixelworld::PixelWorld& source, eve::pixelworld::PixelChunkRegion interest);

    /** @brief Forget consumer projection state so the next capture is a full resync. */
    void reset() noexcept;

    /** @brief Last successfully captured source revision, or zero before first capture. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Number of present Chunk coordinates believed to be held by the consumer. */
    [[nodiscard]] std::size_t knownChunkCount() const noexcept { return known_.size(); }

private:
    eve::pixelworld::PixelWorldLink source_{};
    std::uint64_t revision_ = 0;
    std::set<std::pair<int, int>> known_;
};

}  // namespace eve::pixelworld_streaming
