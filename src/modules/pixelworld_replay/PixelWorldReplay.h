#pragma once

#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/Time.h"
#include "pixelworld/PixelWorld.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace eve::pixelworld_replay {

/** @brief One deterministic edit admitted immediately before its simulation Tick. */
struct PixelReplayEntry {
    eve::SimulationTick tick = eve::SimulationTick::zero();
    eve::pixelworld::PixelEditCommand command;
};

/** @brief Canonical content digest for one authoritative PixelWorld Chunk. */
struct PixelChunkDigest {
    int x = 0;
    int y = 0;
    std::uint64_t digest = 0;
    friend bool operator==(const PixelChunkDigest&, const PixelChunkDigest&) = default;
};

/** @brief Periodic replay checkpoint containing world and per-Chunk diagnostics. */
struct PixelReplayCheckpoint {
    eve::SimulationTick tick = eve::SimulationTick::zero();
    std::uint64_t revision = 0;
    std::uint64_t worldDigest = 0;
    std::vector<PixelChunkDigest> chunks;
};

/** @brief Replay verification outcome; divergence is data, not an operational failure. */
enum class PixelReplayMatch : std::uint8_t { Matched, Diverged };

/** @brief First canonical mismatch found while replaying a log. */
struct PixelReplayDivergence {
    eve::SimulationTick tick = eve::SimulationTick::zero();
    std::uint64_t expectedRevision = 0;
    std::uint64_t actualRevision = 0;
    std::optional<PixelChunkDigest> expectedChunk;
    std::optional<PixelChunkDigest> actualChunk;
};

/** @brief Receipt from deterministic replay through an exact target Tick. */
struct PixelReplayReceipt {
    PixelReplayMatch match = PixelReplayMatch::Matched;
    eve::SimulationTick reachedTick = eve::SimulationTick::zero();
    std::uint64_t commandsApplied = 0;
    std::optional<PixelReplayDivergence> firstDivergence;
};

/**
 * @brief Owning command log and periodic checkpoint projection for PixelWorld.
 *
 * The log is not another mutable world authority. It owns immutable replay inputs
 * and digests only. APIs are simulation-thread affine, invoke no callbacks, and
 * use bit-exact integer hashes over canonical PixelWorld bytes.
 */
class PixelReplayLog {
public:
    /** @brief Append the next sequenced command; Tick order must be nondecreasing. */
    [[nodiscard]] eve::Result<void> append(PixelReplayEntry entry);

    /**
     * @brief Capture a checkpoint from the synchronously borrowed world.
     * @remarks The world is not retained or mutated. Checkpoint Tick must increase.
     */
    [[nodiscard]] eve::Result<void> captureCheckpoint(const eve::pixelworld::PixelWorld& world);

    /**
     * @brief Replay into an empty target through `throughTick` and verify checkpoints.
     * @return A matched receipt or the first Tick/Chunk divergence. Malformed log or
     * non-empty target is a structured failure and leaves admission state unchanged.
     * @remarks The target is mutated after validation succeeds; operational failure
     * during replay may leave it at the last fully committed Tick.
     */
    [[nodiscard]] eve::Result<PixelReplayReceipt> replay(
        eve::pixelworld::PixelWorld& target, eve::SimulationTick throughTick) const;

    /**
     * @brief Seal this log in the shared versioned SnapshotEnvelope.
     * @param instanceId Stable identity of the owning replay/session.
     * @param hashProvider Injected content digest provider; no weak default is selected.
     * @return Schema `pixelworld:replay-log` version 2 with canonical owning payload.
     */
    [[nodiscard]] eve::Result<eve::SnapshotEnvelope> snapshot(
        eve::PersistentId instanceId, const eve::SnapshotHashProvider& hashProvider) const;

    /**
     * @brief Verify, migrate and transactionally replace this log from an envelope.
     * @param snapshot Externally owned envelope borrowed only for this call.
     * @param hashProvider Digest provider used to verify the envelope before parsing.
     * @remarks Versions 1 and 2 are accepted. Version 1 checkpoints migrate without
     * per-Chunk diagnostics. Any failure leaves entries and checkpoints unchanged.
     */
    [[nodiscard]] eve::Result<void> restoreSnapshot(
        const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider);

    /** @brief Immutable owning-log views, valid until this log is mutated or destroyed. */
    [[nodiscard]] std::span<const PixelReplayEntry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::span<const PixelReplayCheckpoint> checkpoints() const noexcept { return checkpoints_; }

private:
    std::vector<PixelReplayEntry> entries_;
    std::vector<PixelReplayCheckpoint> checkpoints_;
};

}  // namespace eve::pixelworld_replay
