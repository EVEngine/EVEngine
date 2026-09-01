#pragma once

#include "common/Result.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld_streaming/PixelWorldStreaming.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace eve::pixelworld_streaming {

/** @brief Bounded application-level reliability policy layered over any byte transport. */
struct PixelChunkTransportConfig {
    std::uint32_t maximumInFlightTransfers = 8;
    std::uint32_t maximumChunksPerPart = 1;
    std::uint32_t maximumPartsPerTransfer = 8192;
    std::uint32_t maximumBufferedTransfers = 16;
};

/** @brief One independently retransmittable part of an atomic Chunk transfer. */
struct PixelChunkTransferPart {
    std::uint64_t streamId = 0;
    std::uint64_t transferId = 0;
    std::uint32_t partIndex = 0;
    std::uint32_t partCount = 0;
    eve::pixelworld::PixelChunkRegion interest{};
    eve::pixelworld::PixelChunkBatch batch;

    friend bool operator==(const PixelChunkTransferPart&, const PixelChunkTransferPart&) = default;
};

/** @brief Cumulative application ACK proving transfers were committed to replica authority. */
struct PixelChunkTransferAck {
    std::uint64_t streamId = 0;
    std::uint64_t acknowledgedThrough = 0;
    std::uint64_t appliedRevision = 0;
};

/** @brief Counters returned after accepting a cumulative ACK. */
struct PixelChunkAckReceipt {
    std::uint32_t transfersReleased = 0;
    std::uint32_t tombstonesReleased = 0;
    std::uint64_t acknowledgedThrough = 0;
};

/** @brief Receiver outcome after one possibly duplicate or out-of-order part. */
struct PixelChunkReceiveReceipt {
    PixelChunkTransferAck ack;
    std::uint32_t transfersApplied = 0;
    std::uint32_t partsBuffered = 0;
    bool duplicate = false;
};

/**
 * @brief Reliable sender retaining owning transfer parts and tombstones until application ACK.
 *
 * The sender is transport-neutral: callers may send `pendingParts()` through EVNetwork reliable
 * messages, another socket, or deterministic tests. Cursor state advances only after a transfer
 * fits the bounded in-flight window. Repeated `pendingParts()` calls are intentional retransmits.
 */
class ReliablePixelChunkSender {
public:
    /** @brief Validate policy and create a fresh nonzero stream session. */
    [[nodiscard]] static eve::Result<ReliablePixelChunkSender> create(
        std::uint64_t streamId, PixelChunkTransportConfig config = {});

    /** @brief Capture and enqueue one atomic revision transfer, split into bounded owning parts. */
    [[nodiscard]] eve::Result<std::vector<PixelChunkTransferPart>> capture(
        const eve::pixelworld::PixelWorld& source,
        eve::pixelworld::PixelChunkRegion interest);

    /** @brief Return every unacknowledged part in canonical transfer/part order for transmission. */
    [[nodiscard]] std::vector<PixelChunkTransferPart> pendingParts() const;
    /** @brief Release cumulatively acknowledged transfers and their retained tombstones. */
    [[nodiscard]] eve::Result<PixelChunkAckReceipt> acknowledge(PixelChunkTransferAck ack);
    /** @brief Number of unacknowledged atomic transfers. */
    [[nodiscard]] std::size_t inFlightTransferCount() const noexcept;
    /** @brief Number of tombstones retained specifically for possible retransmission. */
    [[nodiscard]] std::size_t retainedTombstoneCount() const noexcept;

private:
    ReliablePixelChunkSender(std::uint64_t streamId, PixelChunkTransportConfig config);

    std::uint64_t streamId_ = 0;
    std::uint64_t nextTransferId_ = 1;
    std::uint64_t acknowledgedThrough_ = 0;
    std::uint64_t lastEnqueuedRevision_ = 0;
    bool captured_ = false;
    PixelChunkTransportConfig config_{};
    PixelChunkStreamCursor cursor_;
    std::map<std::uint64_t, std::vector<PixelChunkTransferPart>> inFlight_;
};

/**
 * @brief Ordered atomic receiver with duplicate suppression and bounded out-of-order buffering.
 *
 * Parts never mutate PixelWorld individually. A complete next transfer is canonicalized and
 * committed through `applyChunkBatch`; only then does the cumulative ACK advance. Failure leaves
 * both replica and expected transfer id unchanged.
 */
class ReliablePixelChunkReceiver {
public:
    /** @brief Validate policy and create a receiver for one nonzero stream session. */
    [[nodiscard]] static eve::Result<ReliablePixelChunkReceiver> create(
        std::uint64_t streamId, PixelChunkTransportConfig config = {});

    /** @brief Buffer one part and atomically apply every newly contiguous complete transfer. */
    [[nodiscard]] eve::Result<PixelChunkReceiveReceipt> receive(
        PixelChunkTransferPart part, eve::pixelworld::PixelWorld& replica);
    /** @brief Last cumulatively applied transfer id. */
    [[nodiscard]] std::uint64_t acknowledgedThrough() const noexcept { return expectedTransferId_ - 1; }
    /** @brief Count parts retained while waiting for gaps or completion. */
    [[nodiscard]] std::size_t bufferedPartCount() const noexcept;

private:
    struct Assembly {
        std::uint32_t partCount = 0;
        std::vector<std::unique_ptr<PixelChunkTransferPart>> parts;
    };

    ReliablePixelChunkReceiver(std::uint64_t streamId, PixelChunkTransportConfig config);

    std::uint64_t streamId_ = 0;
    std::uint64_t expectedTransferId_ = 1;
    PixelChunkTransportConfig config_{};
    std::map<std::uint64_t, Assembly> buffered_;
};

}  // namespace eve::pixelworld_streaming
