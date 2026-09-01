#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorld.h"
#include "pixelworld_streaming/PixelWorldStreaming.h"
#include "pixelworld_streaming/PixelWorldTransport.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

using namespace eve::pixelworld;
using namespace eve::pixelworld_streaming;

namespace {

eve::SnapshotHashProvider archiveHashProvider() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left = 14695981039346656037ULL;
        std::uint64_t right = 1099511628211ULL;
        for (const unsigned char byte : input) {
            left = (left ^ byte) * 1099511628211ULL;
            right = (right ^ (std::uint64_t(byte) + 0x9e3779b97f4a7c15ULL)) * 14029467366897019727ULL;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[std::size_t(index)] = std::uint8_t(left >> (56 - index * 8));
            bytes[std::size_t(index + 8)] = std::uint8_t(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::LogicalId chunkArchiveSchema() {
    return *eve::LogicalId::parse("pixelworld:chunk-batch");
}

}  // namespace

TEST_CASE("pixelworld_streaming_interest_move_emits_entry_and_eviction_correction") {
    PixelWorld authority(8301);
    authority.setMaterial(1, 1, "stone");
    authority.setMaterial(2 * kPixelChunkSize + 1, 1, "water");
    PixelChunkStreamCursor cursor;

    auto first = cursor.capture(authority, {0, 0, 0, 0}).expect("capture first interest");
    CHECK(first.fullResync);
    REQUIRE_EQ(first.batch.chunks.size(), std::size_t(1));
    PixelWorld replica(8301);
    replica.applyChunkBatch(first.batch, 0).expect("apply first interest");
    CHECK_EQ(replica.getMaterial(1, 1), int(MaterialId::Stone));
    CHECK_EQ(replica.getMaterial(2 * kPixelChunkSize + 1, 1), int(MaterialId::Air));

    auto moved = cursor.capture(authority, {2, 0, 2, 0}).expect("move interest");
    CHECK(!moved.fullResync);
    CHECK_EQ(moved.chunksEntered, std::uint32_t(1));
    CHECK_EQ(moved.chunksEvicted, std::uint32_t(1));
    REQUIRE_EQ(moved.batch.chunks.size(), std::size_t(2));
    replica.applyChunkBatch(moved.batch, replica.revision()).expect("apply moved interest");
    CHECK_EQ(replica.getMaterial(1, 1), int(MaterialId::Air));
    CHECK_EQ(replica.getMaterial(2 * kPixelChunkSize + 1, 1), int(MaterialId::Water));

    const auto unchanged = cursor.capture(authority, {2, 0, 2, 0}).expect("unchanged interest");
    CHECK(unchanged.batch.chunks.empty());
}

TEST_CASE("pixelworld_streaming_disconnect_reconnect_applies_authoritative_chunk_delta") {
    PixelWorld authority(8302);
    authority.setMaterial(4, 4, "wood");
    PixelChunkStreamCursor cursor;
    auto initial = cursor.capture(authority, {0, 0, 0, 0}).expect("initial sync");
    PixelWorld replica(8302);
    replica.applyChunkBatch(initial.batch, 0).expect("apply initial sync");

    authority.setMaterial(4, 4, "fire");
    authority.setMaterial(5, 4, "oil");
    auto reconnect = cursor.capture(authority, {0, 0, 0, 0}).expect("reconnect delta");
    REQUIRE_EQ(reconnect.batch.chunks.size(), std::size_t(1));
    replica.applyChunkBatch(reconnect.batch, replica.revision()).expect("apply reconnect correction");
    CHECK_EQ(replica.getCell(4, 4), authority.getCell(4, 4));
    CHECK_EQ(replica.getCell(5, 4), authority.getCell(5, 4));
    CHECK_EQ(replica.revision(), authority.revision());
}

TEST_CASE("pixelworld_streaming_epoch_change_forces_full_resync_and_stale_writer_rejects") {
    PixelWorld authority(8303);
    authority.setMaterial(3, 3, "sand");
    const auto saved = authority.saveSnapshot().expect("save authority");
    PixelChunkStreamCursor cursor;
    auto initial = cursor.capture(authority, {0, 0, 0, 0}).expect("initial");
    CHECK(initial.fullResync);
    authority.restoreSnapshot(saved).expect("restore changes epoch");
    auto rebuilt = cursor.capture(authority, {0, 0, 0, 0}).expect("full rebuild");
    CHECK(rebuilt.fullResync);
    REQUIRE_EQ(rebuilt.batch.chunks.size(), std::size_t(1));

    PixelWorld replica(8303);
    replica.setMaterial(10, 10, "stone");
    const auto before = replica.saveSnapshot().expect("before stale correction");
    CHECK(!replica.applyChunkBatch(rebuilt.batch, 0).ok());
    const auto after = replica.saveSnapshot().expect("after stale correction");
    REQUIRE_EQ(before.size(), after.size());
    CHECK(std::equal(before.begin(), before.end(), after.begin()));
}

TEST_CASE("pixelworld_streaming_full_resync_atomically_clears_projection_and_accepts_rewind") {
    PixelWorld authority(8304);
    authority.setMaterial(3, 3, "stone");
    PixelChunkStreamCursor cursor;
    const auto populated = cursor.capture(authority, {0, 0, 0, 0}).expect("populated sync");
    REQUIRE(populated.batch.fullResync);
    PixelWorld replica(8304);
    replica.applyChunkBatch(populated.batch, 0).expect("apply populated projection");
    CHECK_EQ(replica.getMaterial(3, 3), int(MaterialId::Stone));

    PixelWorld emptyAuthority(8304);
    const auto emptySnapshot = emptyAuthority.saveSnapshot().expect("empty authority snapshot");
    authority.restoreSnapshot(emptySnapshot).expect("rewind authority to empty epoch");
    const auto reset = cursor.capture(authority, {0, 0, 0, 0}).expect("rewound full sync");
    CHECK(reset.fullResync);
    CHECK(reset.batch.fullResync);
    CHECK_EQ(reset.batch.sourceRevision, std::uint64_t(0));
    CHECK(reset.batch.chunks.empty());
    replica.applyChunkBatch(reset.batch, replica.revision()).expect("replace projection after rewind");
    CHECK_EQ(replica.revision(), std::uint64_t(0));
    CHECK_EQ(replica.chunkCount(), std::size_t(0));
    CHECK_EQ(replica.getMaterial(3, 3), int(MaterialId::Air));
}

TEST_CASE("pixelworld_streaming_zstd_chunk_archive_round_trips_and_is_smaller_than_raw") {
    const auto hash = archiveHashProvider();
    PixelWorld authority(8401);
    for (int y = 0; y < kPixelChunkSize; ++y)
        for (int x = 0; x < kPixelChunkSize; ++x)
            authority.setCell(x, y, {MaterialId::Stone, 20, 0});
    authority.setCell(17, 23, {MaterialId::Stone, 20, 0, 73});
    PixelChunkStreamCursor cursor;
    const auto update = cursor.capture(authority, {0, 0, 0, 0}).expect("capture archive batch");
    const auto compressed = archiveChunkBatch(update.batch, eve::PersistentId::nil(),
                                               PixelChunkArchiveCodec::Zstd, hash)
                                .expect("compress archive");
    const auto raw = archiveChunkBatch(update.batch, eve::PersistentId::nil(),
                                        PixelChunkArchiveCodec::None, hash)
                         .expect("raw archive");
    const auto compressedJson = eve::serializeSnapshotEnvelope(compressed).expect("serialize compressed");
    const auto rawJson = eve::serializeSnapshotEnvelope(raw).expect("serialize raw");
    CHECK(compressedJson.size() < rawJson.size() / 4);

    const auto parsed = eve::parseSnapshotEnvelope(compressedJson, hash).expect("parse compressed");
    const auto decoded = decodeChunkBatchArchive(parsed, hash).expect("decode archive");
    PixelWorld replica(8401);
    replica.applyChunkBatch(decoded, 0).expect("apply decoded archive");
    CHECK_EQ(replica.getCell(17, 23), authority.getCell(17, 23));
    CHECK_EQ(replica.getCell(17, 23).thermalRemainder, std::uint16_t(73));
    CHECK_EQ(replica.revision(), authority.revision());
}

TEST_CASE("pixelworld_streaming_chunk_archive_v1_migrates_and_corruption_is_isolated") {
    const auto hash = archiveHashProvider();
    PixelWorld authority(8402);
    authority.setMaterial(3, 4, "water");
    PixelChunkStreamCursor cursor;
    const auto update = cursor.capture(authority, {0, 0, 0, 0}).expect("capture");
    const auto current = archiveChunkBatch(update.batch, eve::PersistentId::nil(),
                                           PixelChunkArchiveCodec::Zstd, hash)
                             .expect("archive");

    auto legacyPayload = current.payload;
    legacyPayload.getIf<eve::Value::Object>()->erase("sourceLastEditSequence");
    legacyPayload.getIf<eve::Value::Object>()->erase("fullResync");
    const auto legacy = eve::makeSnapshotEnvelope(
                            "pixelworld.chunk-batch", chunkArchiveSchema(), eve::SchemaVersion(1),
                            eve::PersistentId::nil(), current.revision, current.tick,
                            std::move(legacyPayload), hash)
                            .expect("seal legacy");
    const auto migrated = decodeChunkBatchArchive(legacy, hash).expect("decode legacy");
    CHECK_EQ(migrated.sourceLastEditSequence, std::uint64_t(0));
    REQUIRE_EQ(migrated.chunks.size(), std::size_t(1));

    auto corruptPayload = current.payload;
    auto& chunks = *corruptPayload.getIf<eve::Value::Object>()->at("chunks").getIf<eve::Value::Array>();
    auto& stored = *chunks.front().getIf<eve::Value::Object>()->at("stored").getIf<std::string>();
    REQUIRE(!stored.empty());
    stored[0] = stored[0] == '0' ? '1' : '0';
    const auto resealedCorrupt = eve::makeSnapshotEnvelope(
                                     "pixelworld.chunk-batch", chunkArchiveSchema(), eve::SchemaVersion(2),
                                     eve::PersistentId::nil(), current.revision, current.tick,
                                     std::move(corruptPayload), hash)
                                     .expect("seal corrupt physical payload");
    PixelWorld untouched(8402);
    untouched.setMaterial(9, 9, "wood");
    const auto before = untouched.saveSnapshot().expect("before corrupt decode");
    CHECK(!decodeChunkBatchArchive(resealedCorrupt, hash).ok());
    const auto after = untouched.saveSnapshot().expect("after corrupt decode");
    CHECK(std::equal(before.begin(), before.end(), after.begin()));

    auto badOuterHash = current;
    badOuterHash.contentHash = eve::ContentId::nil();
    CHECK(!decodeChunkBatchArchive(badOuterHash, hash).ok());
}

TEST_CASE("pixelworld_transport_retransmits_loss_and_commits_complete_transfer_atomically") {
    PixelWorld authority(8501);
    authority.setMaterial(1, 1, "stone");
    authority.setMaterial(kPixelChunkSize + 1, 1, "water");
    authority.setMaterial(2 * kPixelChunkSize + 1, 1, "wood");
    auto sender = ReliablePixelChunkSender::create(91).expect("create sender");
    auto receiver = ReliablePixelChunkReceiver::create(91).expect("create receiver");
    const auto parts = sender.capture(authority, {0, 0, 2, 0}).expect("capture transfer");
    REQUIRE_EQ(parts.size(), std::size_t(3));
    PixelWorld replica(8501);

    CHECK_EQ(receiver.receive(parts[2], replica).expect("part 2").transfersApplied,
             std::uint32_t(0));
    CHECK_EQ(receiver.receive(parts[0], replica).expect("part 0").transfersApplied,
             std::uint32_t(0));
    CHECK(receiver.receive(parts[2], replica).expect("duplicate part 2").duplicate);
    CHECK_EQ(replica.chunkCount(), std::size_t(0));
    REQUIRE_EQ(sender.pendingParts().size(), std::size_t(3));

    const auto completed = receiver.receive(sender.pendingParts()[1], replica)
                               .expect("retransmitted missing part");
    CHECK_EQ(completed.transfersApplied, std::uint32_t(1));
    CHECK_EQ(completed.ack.acknowledgedThrough, std::uint64_t(1));
    CHECK_EQ(replica.getCell(2 * kPixelChunkSize + 1, 1),
             authority.getCell(2 * kPixelChunkSize + 1, 1));
    const auto released = sender.acknowledge(completed.ack).expect("application ACK");
    CHECK_EQ(released.transfersReleased, std::uint32_t(1));
    CHECK_EQ(sender.inFlightTransferCount(), std::size_t(0));
}

TEST_CASE("pixelworld_transport_buffers_transfer_order_and_retains_tombstones_until_ACK") {
    PixelWorld authority(8502);
    authority.setMaterial(1, 1, "stone");
    authority.setMaterial(kPixelChunkSize + 1, 1, "water");
    auto sender = ReliablePixelChunkSender::create(92).expect("create sender");
    auto receiver = ReliablePixelChunkReceiver::create(92).expect("create receiver");
    PixelWorld replica(8502);
    const auto initial = sender.capture(authority, {0, 0, 1, 0}).expect("initial");
    PixelChunkReceiveReceipt receipt;
    for (const auto& part : initial)
        receipt = receiver.receive(part, replica).expect("receive initial");
    sender.acknowledge(receipt.ack).expect("ACK initial");

    authority.setMaterial(2, 2, "oil");
    const auto second = sender.capture(authority, {0, 0, 1, 0}).expect("second");
    authority.setMaterial(kPixelChunkSize + 2, 2, "wood");
    const auto third = sender.capture(authority, {0, 0, 1, 0}).expect("third");
    REQUIRE_EQ(second.size(), std::size_t(1));
    REQUIRE_EQ(third.size(), std::size_t(1));
    CHECK_EQ(receiver.receive(third.front(), replica).expect("third early").transfersApplied,
             std::uint32_t(0));
    const auto ordered = receiver.receive(second.front(), replica).expect("fill transfer gap");
    CHECK_EQ(ordered.transfersApplied, std::uint32_t(2));
    CHECK_EQ(ordered.ack.acknowledgedThrough, std::uint64_t(3));
    CHECK_EQ(replica.getCell(kPixelChunkSize + 2, 2),
             authority.getCell(kPixelChunkSize + 2, 2));
    sender.acknowledge(ordered.ack).expect("ACK ordered transfers");

    const auto evicted = sender.capture(authority, {0, 0, 0, 0}).expect("evict interest");
    REQUIRE_EQ(evicted.size(), std::size_t(1));
    CHECK(evicted.front().batch.chunks.front().removed);
    CHECK_EQ(sender.retainedTombstoneCount(), std::size_t(1));
    const auto tombstone = receiver.receive(evicted.front(), replica).expect("apply tombstone");
    CHECK_EQ(replica.getMaterial(kPixelChunkSize + 1, 1), int(MaterialId::Air));
    sender.acknowledge(tombstone.ack).expect("ACK tombstone");
    CHECK_EQ(sender.retainedTombstoneCount(), std::size_t(0));
    CHECK_EQ(sender.acknowledge(tombstone.ack).expect("duplicate ACK").transfersReleased,
             std::uint32_t(0));
}

TEST_CASE("pixelworld_transport_window_failure_does_not_advance_capture_cursor") {
    PixelChunkTransportConfig config;
    config.maximumInFlightTransfers = 1;
    PixelWorld authority(8503);
    authority.setMaterial(1, 1, "stone");
    auto sender = ReliablePixelChunkSender::create(93, config).expect("create sender");
    auto receiver = ReliablePixelChunkReceiver::create(93, config).expect("create receiver");
    PixelWorld replica(8503);
    const auto initial = sender.capture(authority, {0, 0, 0, 0}).expect("initial");
    authority.setMaterial(2, 2, "water");
    CHECK(!sender.capture(authority, {0, 0, 0, 0}).ok());
    const auto first = receiver.receive(initial.front(), replica).expect("receive initial");
    sender.acknowledge(first.ack).expect("free window");
    const auto retry = sender.capture(authority, {0, 0, 0, 0}).expect("retry capture");
    REQUIRE_EQ(retry.size(), std::size_t(1));
    CHECK_EQ(retry.front().batch.sourceRevision, authority.revision());
    CHECK_EQ(int(retry.front().batch.chunks.front().cells[2 * kPixelChunkSize + 2].material),
             int(MaterialId::Water));
}

TEST_CASE("pixelworld_transport_rejects_conflicting_duplicate_and_invalid_ACK") {
    PixelWorld authority(8504);
    authority.setMaterial(1, 1, "stone");
    authority.setMaterial(kPixelChunkSize + 1, 1, "water");
    auto sender = ReliablePixelChunkSender::create(94).expect("create sender");
    auto receiver = ReliablePixelChunkReceiver::create(94).expect("create receiver");
    const auto parts = sender.capture(authority, {0, 0, 1, 0}).expect("capture");
    REQUIRE_EQ(parts.size(), std::size_t(2));
    PixelWorld replica(8504);
    receiver.receive(parts.front(), replica).expect("receive partial");
    auto conflict = parts.front();
    conflict.batch.sourceRevision += 1;
    CHECK(!receiver.receive(std::move(conflict), replica).ok());
    CHECK(!sender.acknowledge({94, 1, authority.revision() + 1}).ok());
    CHECK_EQ(sender.inFlightTransferCount(), std::size_t(1));
}
