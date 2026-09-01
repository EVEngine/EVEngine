#include "pixelworld_streaming/PixelWorldTransport.h"

#include <algorithm>
#include <limits>
#include <string>

namespace eve::pixelworld_streaming {
namespace {

template <class T>
eve::Result<T> fail(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "pixelworld.reliable-transport"));
}

bool valid(PixelChunkTransportConfig config) {
    return config.maximumInFlightTransfers > 0 && config.maximumInFlightTransfers <= 1024 &&
           config.maximumChunksPerPart > 0 && config.maximumChunksPerPart <= 1024 &&
           config.maximumPartsPerTransfer > 0 && config.maximumPartsPerTransfer <= 65'536 &&
           config.maximumBufferedTransfers > 0 && config.maximumBufferedTransfers <= 1024;
}

bool sameMetadata(const PixelChunkTransferPart& left, const PixelChunkTransferPart& right) {
    return left.streamId == right.streamId && left.transferId == right.transferId &&
           left.partCount == right.partCount && left.interest == right.interest &&
           left.batch.catalogFingerprint == right.batch.catalogFingerprint &&
           left.batch.sourceSeed == right.batch.sourceSeed &&
           left.batch.sourceRevision == right.batch.sourceRevision &&
           left.batch.sourceTick == right.batch.sourceTick &&
           left.batch.sourceLastEditSequence == right.batch.sourceLastEditSequence &&
           left.batch.fullResync == right.batch.fullResync;
}

}  // namespace

ReliablePixelChunkSender::ReliablePixelChunkSender(std::uint64_t streamId,
                                                   PixelChunkTransportConfig config)
    : streamId_(streamId), config_(config) {}

eve::Result<ReliablePixelChunkSender> ReliablePixelChunkSender::create(
    std::uint64_t streamId, PixelChunkTransportConfig config) {
    if (streamId == 0 || !valid(config))
        return fail<ReliablePixelChunkSender>(eve::DiagnosticCode::InvalidArgument,
                                              "stream id or transport policy is invalid", "config");
    return eve::Result<ReliablePixelChunkSender>::success(
        ReliablePixelChunkSender(streamId, config));
}

eve::Result<std::vector<PixelChunkTransferPart>> ReliablePixelChunkSender::capture(
    const eve::pixelworld::PixelWorld& source, eve::pixelworld::PixelChunkRegion interest) {
    if (inFlight_.size() >= config_.maximumInFlightTransfers)
        return fail<std::vector<PixelChunkTransferPart>>(
            eve::DiagnosticCode::PreconditionViolation, "reliable send window is full",
            "maximumInFlightTransfers");
    if (nextTransferId_ == std::numeric_limits<std::uint64_t>::max())
        return fail<std::vector<PixelChunkTransferPart>>(
            eve::DiagnosticCode::PreconditionViolation, "transfer id space is exhausted",
            "transferId");
    auto candidateCursor = cursor_;
    auto captured = candidateCursor.capture(source, interest);
    if (!captured.ok())
        return eve::Result<std::vector<PixelChunkTransferPart>>::failure(captured.status());
    auto update = std::move(captured).takeValue();
    if (captured_ && !update.fullResync && update.batch.chunks.empty() &&
        update.batch.sourceRevision == lastEnqueuedRevision_) {
        cursor_ = std::move(candidateCursor);
        return eve::Result<std::vector<PixelChunkTransferPart>>::success({});
    }

    const std::size_t chunkCount = update.batch.chunks.size();
    const std::size_t partCount = std::max<std::size_t>(
        1, (chunkCount + config_.maximumChunksPerPart - 1) / config_.maximumChunksPerPart);
    if (partCount > config_.maximumPartsPerTransfer)
        return fail<std::vector<PixelChunkTransferPart>>(
            eve::DiagnosticCode::PreconditionViolation, "captured update exceeds part budget",
            "maximumPartsPerTransfer");
    std::vector<PixelChunkTransferPart> parts;
    parts.reserve(partCount);
    for (std::size_t index = 0; index < partCount; ++index) {
        PixelChunkTransferPart part;
        part.streamId = streamId_;
        part.transferId = nextTransferId_;
        part.partIndex = std::uint32_t(index);
        part.partCount = std::uint32_t(partCount);
        part.interest = interest;
        part.batch.catalogFingerprint = update.batch.catalogFingerprint;
        part.batch.sourceSeed = update.batch.sourceSeed;
        part.batch.sourceRevision = update.batch.sourceRevision;
        part.batch.sourceTick = update.batch.sourceTick;
        part.batch.sourceLastEditSequence = update.batch.sourceLastEditSequence;
        part.batch.fullResync = update.batch.fullResync;
        const std::size_t begin = index * config_.maximumChunksPerPart;
        const std::size_t end = std::min(chunkCount, begin + config_.maximumChunksPerPart);
        if (begin < end)
            part.batch.chunks.assign(update.batch.chunks.begin() + std::ptrdiff_t(begin),
                                     update.batch.chunks.begin() + std::ptrdiff_t(end));
        parts.push_back(std::move(part));
    }
    inFlight_.emplace(nextTransferId_, parts);
    cursor_ = std::move(candidateCursor);
    ++nextTransferId_;
    captured_ = true;
    lastEnqueuedRevision_ = update.batch.sourceRevision;
    return eve::Result<std::vector<PixelChunkTransferPart>>::success(std::move(parts));
}

std::vector<PixelChunkTransferPart> ReliablePixelChunkSender::pendingParts() const {
    std::vector<PixelChunkTransferPart> result;
    for (const auto& [transfer, parts] : inFlight_) {
        (void)transfer;
        result.insert(result.end(), parts.begin(), parts.end());
    }
    return result;
}

eve::Result<PixelChunkAckReceipt> ReliablePixelChunkSender::acknowledge(
    PixelChunkTransferAck ack) {
    if (ack.streamId != streamId_)
        return fail<PixelChunkAckReceipt>(eve::DiagnosticCode::Conflict,
                                          "ACK belongs to another stream", "streamId");
    if (ack.acknowledgedThrough <= acknowledgedThrough_)
        return eve::Result<PixelChunkAckReceipt>::success(
            {0, 0, acknowledgedThrough_});
    if (ack.acknowledgedThrough >= nextTransferId_)
        return fail<PixelChunkAckReceipt>(eve::DiagnosticCode::Conflict,
                                          "ACK advances beyond the sent window",
                                          "acknowledgedThrough");
    const auto acknowledged = inFlight_.find(ack.acknowledgedThrough);
    if (acknowledged == inFlight_.end() || acknowledged->second.empty() ||
        acknowledged->second.front().batch.sourceRevision != ack.appliedRevision)
        return fail<PixelChunkAckReceipt>(eve::DiagnosticCode::Conflict,
                                          "ACK revision does not match the transfer",
                                          "appliedRevision");
    PixelChunkAckReceipt receipt;
    for (auto iterator = inFlight_.begin(); iterator != inFlight_.end() &&
                                            iterator->first <= ack.acknowledgedThrough;) {
        for (const auto& part : iterator->second)
            receipt.tombstonesReleased += std::uint32_t(std::count_if(
                part.batch.chunks.begin(), part.batch.chunks.end(),
                [](const auto& chunk) { return chunk.removed; }));
        iterator = inFlight_.erase(iterator);
        ++receipt.transfersReleased;
    }
    acknowledgedThrough_ = ack.acknowledgedThrough;
    receipt.acknowledgedThrough = acknowledgedThrough_;
    return eve::Result<PixelChunkAckReceipt>::success(receipt);
}

std::size_t ReliablePixelChunkSender::inFlightTransferCount() const noexcept {
    return inFlight_.size();
}

std::size_t ReliablePixelChunkSender::retainedTombstoneCount() const noexcept {
    std::size_t count = 0;
    for (const auto& [transfer, parts] : inFlight_) {
        (void)transfer;
        for (const auto& part : parts)
            count += std::count_if(part.batch.chunks.begin(), part.batch.chunks.end(),
                                   [](const auto& chunk) { return chunk.removed; });
    }
    return count;
}

ReliablePixelChunkReceiver::ReliablePixelChunkReceiver(std::uint64_t streamId,
                                                       PixelChunkTransportConfig config)
    : streamId_(streamId), config_(config) {}

eve::Result<ReliablePixelChunkReceiver> ReliablePixelChunkReceiver::create(
    std::uint64_t streamId, PixelChunkTransportConfig config) {
    if (streamId == 0 || !valid(config))
        return fail<ReliablePixelChunkReceiver>(eve::DiagnosticCode::InvalidArgument,
                                                "stream id or transport policy is invalid", "config");
    return eve::Result<ReliablePixelChunkReceiver>::success(
        ReliablePixelChunkReceiver(streamId, config));
}

eve::Result<PixelChunkReceiveReceipt> ReliablePixelChunkReceiver::receive(
    PixelChunkTransferPart part, eve::pixelworld::PixelWorld& replica) {
    if (part.streamId != streamId_ || part.transferId == 0 || part.partCount == 0 ||
        part.partCount > config_.maximumPartsPerTransfer || part.partIndex >= part.partCount ||
        part.batch.chunks.size() > config_.maximumChunksPerPart)
        return fail<PixelChunkReceiveReceipt>(eve::DiagnosticCode::InvalidArgument,
                                              "transfer part metadata exceeds policy", "part");
    PixelChunkReceiveReceipt receipt;
    if (part.transferId < expectedTransferId_) {
        receipt.duplicate = true;
        receipt.ack = {streamId_, expectedTransferId_ - 1, replica.revision()};
        receipt.partsBuffered = std::uint32_t(bufferedPartCount());
        return eve::Result<PixelChunkReceiveReceipt>::success(receipt);
    }
    auto found = buffered_.find(part.transferId);
    if (found == buffered_.end()) {
        if (buffered_.size() >= config_.maximumBufferedTransfers)
            return fail<PixelChunkReceiveReceipt>(
                eve::DiagnosticCode::PreconditionViolation,
                "out-of-order receive window is full", "maximumBufferedTransfers");
        Assembly assembly;
        assembly.partCount = part.partCount;
        assembly.parts.resize(part.partCount);
        found = buffered_.emplace(part.transferId, std::move(assembly)).first;
    }
    Assembly& assembly = found->second;
    if (assembly.partCount != part.partCount)
        return fail<PixelChunkReceiveReceipt>(eve::DiagnosticCode::Conflict,
                                              "transfer part count changed", "partCount");
    auto& slot = assembly.parts[part.partIndex];
    if (slot) {
        if (*slot != part)
            return fail<PixelChunkReceiveReceipt>(eve::DiagnosticCode::Conflict,
                                                  "duplicate part payload conflicts", "part");
        receipt.duplicate = true;
    } else {
        slot = std::make_unique<PixelChunkTransferPart>(std::move(part));
    }

    while (true) {
        auto next = buffered_.find(expectedTransferId_);
        if (next == buffered_.end()) break;
        const bool complete = std::all_of(next->second.parts.begin(), next->second.parts.end(),
                                          [](const auto& value) { return bool(value); });
        if (!complete) break;
        const PixelChunkTransferPart& first = *next->second.parts.front();
        eve::pixelworld::PixelChunkBatch combined = first.batch;
        combined.chunks.clear();
        for (const auto& value : next->second.parts) {
            if (!sameMetadata(first, *value))
                return fail<PixelChunkReceiveReceipt>(eve::DiagnosticCode::Conflict,
                                                      "transfer parts disagree on authority metadata",
                                                      "part.batch");
            combined.chunks.insert(combined.chunks.end(), value->batch.chunks.begin(),
                                   value->batch.chunks.end());
        }
        auto applied = replica.applyChunkBatch(combined, replica.revision());
        if (!applied.ok()) {
            buffered_.erase(next);
            return eve::Result<PixelChunkReceiveReceipt>::failure(applied.status());
        }
        buffered_.erase(next);
        ++expectedTransferId_;
        ++receipt.transfersApplied;
    }
    receipt.ack = {streamId_, expectedTransferId_ - 1, replica.revision()};
    receipt.partsBuffered = std::uint32_t(bufferedPartCount());
    return eve::Result<PixelChunkReceiveReceipt>::success(receipt);
}

std::size_t ReliablePixelChunkReceiver::bufferedPartCount() const noexcept {
    std::size_t count = 0;
    for (const auto& [transfer, assembly] : buffered_) {
        (void)transfer;
        count += std::count_if(assembly.parts.begin(), assembly.parts.end(),
                               [](const auto& value) { return bool(value); });
    }
    return count;
}

}  // namespace eve::pixelworld_streaming
