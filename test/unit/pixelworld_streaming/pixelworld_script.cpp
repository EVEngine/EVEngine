// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "pixelworld_script.cpp"
  ]
}
*/
// clang-format on

#include "pixelworld/PixelWorld.h"
#include "pixelworld/PixelWorldModule.h"
#include "pixelworld/streaming/PixelWorldStreaming.h"
#include "pixelworld/streaming/PixelWorldTransport.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

using namespace eve::pixelworld;
using namespace eve::pixelworld_streaming;

namespace {
eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t left  = 14695981039346656037ULL;
        std::uint64_t right = 1099511628211ULL;
        for (const unsigned char byte : input) {
            left  = (left ^ byte) * 1099511628211ULL;
            right = (right ^ (std::uint64_t(byte) + 0x9e3779b97f4a7c15ULL)) * 14029467366897019727ULL;
        }
        eve::ContentId::Bytes bytes{};
        for (int index = 0; index < 8; ++index) {
            bytes[std::size_t(index)]     = std::uint8_t(left >> (56 - index * 8));
            bytes[std::size_t(index + 8)] = std::uint8_t(right >> (56 - index * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

eve::LogicalId archiveSchema() { return *eve::LogicalId::parse("pixelworld:chunk-batch"); }
}  // namespace

TEST_CASE("pixelworld.module_new_world_starts_empty_with_requested_seed") {
    auto* module = PixelWorldModule::create();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("PixelWorldModule"));
    PixelWorld* world = module->newWorld(42);
    REQUIRE(world != nullptr);
    CHECK_EQ(world->seed(), std::uint64_t{42});
    CHECK_EQ(world->chunkCount(), std::size_t{0});
    delete world;
}

TEST_CASE("pixelworld.cursor_interest_move_emits_entry_and_eviction_then_noop") {
    PixelWorld source(8301);
    source.setMaterial(1, 1, "stone");
    source.setMaterial(2 * kPixelChunkSize + 1, 1, "water");
    PixelChunkStreamCursor cursor;
    const auto             first = cursor.capture(source, {0, 0, 0, 0}).expect("initial interest");
    CHECK(first.fullResync);
    CHECK_EQ(cursor.knownChunkCount(), std::size_t{1});
    REQUIRE_EQ(first.batch.chunks.size(), std::size_t{1});
    PixelWorld replica(8301);
    replica.applyChunkBatch(first.batch, 0).expect("initial projection");
    CHECK_EQ(replica.seed(), source.seed());
    CHECK_EQ(replica.getMaterial(1, 1), int(MaterialId::Stone));
    CHECK_EQ(replica.getMaterial(2 * kPixelChunkSize + 1, 1), int(MaterialId::Air));

    const auto moved = cursor.capture(source, {2, 0, 2, 0}).expect("move interest");
    CHECK(!moved.fullResync);
    CHECK_EQ(moved.chunksEntered, std::uint32_t{1});
    CHECK_EQ(moved.chunksEvicted, std::uint32_t{1});
    REQUIRE_EQ(moved.batch.chunks.size(), std::size_t{2});
    replica.applyChunkBatch(moved.batch, replica.revision()).expect("apply correction");
    CHECK_EQ(replica.getMaterial(1, 1), int(MaterialId::Air));
    CHECK_EQ(replica.getMaterial(2 * kPixelChunkSize + 1, 1), int(MaterialId::Water));
    const auto unchanged = cursor.capture(source, {2, 0, 2, 0}).expect("unchanged interest");
    CHECK(unchanged.batch.chunks.empty());
    cursor.reset();
    const auto restarted = cursor.capture(source, {2, 0, 2, 0}).expect("reset cursor");
    CHECK(restarted.fullResync);
}

TEST_CASE("pixelworld.cursor_reconnect_sends_delta_for_changed_cells_only") {
    PixelWorld source(8302);
    source.setMaterial(4, 4, "wood");
    PixelChunkStreamCursor cursor;
    const auto             initial = cursor.capture(source, {0, 0, 0, 0}).expect("initial capture");
    PixelWorld             replica(8302);
    replica.applyChunkBatch(initial.batch, 0).expect("initial apply");
    source.setMaterial(4, 4, "fire");
    source.setMaterial(5, 4, "oil");
    const auto delta = cursor.capture(source, {0, 0, 0, 0}).expect("reconnect");
    CHECK(!delta.fullResync);
    REQUIRE_EQ(delta.batch.chunks.size(), std::size_t{1});
    replica.applyChunkBatch(delta.batch, replica.revision()).expect("delta apply");
    CHECK_EQ(replica.getCell(4, 4), source.getCell(4, 4));
    CHECK_EQ(replica.getCell(5, 4), source.getCell(5, 4));
    CHECK_EQ(replica.revision(), source.revision());
}

TEST_CASE("pixelworld.cursor_epoch_rewind_forces_full_resync_and_stale_apply_is_atomic") {
    PixelWorld source(8303);
    source.setMaterial(3, 3, "sand");
    const auto             saved = source.saveSnapshot().expect("save source");
    PixelChunkStreamCursor cursor;
    CHECK(cursor.capture(source, {0, 0, 0, 0}).expect("first").fullResync);
    source.restoreSnapshot(saved).expect("change epoch");
    const auto resync = cursor.capture(source, {0, 0, 0, 0}).expect("resync");
    CHECK(resync.fullResync);
    REQUIRE_EQ(resync.batch.chunks.size(), std::size_t{1});

    PixelWorld replica(8303);
    replica.setMaterial(10, 10, "stone");
    const auto before = replica.saveSnapshot().expect("before rejected apply");
    CHECK(!replica.applyChunkBatch(resync.batch, 0).ok());
    const auto after = replica.saveSnapshot().expect("after rejected apply");
    CHECK_EQ(after.size(), before.size());
    CHECK(std::equal(after.begin(), after.end(), before.begin()));
}

TEST_CASE("pixelworld.cursor_empty_full_resync_replaces_old_projection_and_revision") {
    PixelWorld source(8304);
    source.setMaterial(3, 3, "stone");
    PixelChunkStreamCursor cursor;
    const auto             populated = cursor.capture(source, {0, 0, 0, 0}).expect("populated");
    PixelWorld replica(8304);
    replica.applyChunkBatch(populated.batch, 0).expect("apply populated");
    CHECK_EQ(replica.seed(), source.seed());
    CHECK_EQ(replica.getMaterial(3, 3), int(MaterialId::Stone));

    PixelWorld empty(8304);
    source.restoreSnapshot(empty.saveSnapshot().expect("empty snapshot")).expect("rewind");
    const auto reset = cursor.capture(source, {0, 0, 0, 0}).expect("empty full resync");
    CHECK(reset.fullResync);
    CHECK(reset.batch.fullResync);
    CHECK_EQ(reset.batch.sourceRevision, std::uint64_t{0});
    CHECK(reset.batch.chunks.empty());
    replica.applyChunkBatch(reset.batch, replica.revision()).expect("replace projection");
    CHECK_EQ(replica.revision(), std::uint64_t{0});
    CHECK_EQ(replica.chunkCount(), std::size_t{0});
    REQUIRE_EQ(replica.getMaterial(3, 3), int(MaterialId::Air));
}

TEST_CASE("pixelworld.archive_zstd_and_raw_decode_to_same_authoritative_cells") {
    const auto hash = testHash();
    PixelWorld source(8401);
    for (int y = 0; y < kPixelChunkSize; ++y)
        for (int x = 0; x < kPixelChunkSize; ++x) source.setCell(x, y, {MaterialId::Stone, 20, 0});
    source.setCell(17, 23, {MaterialId::Stone, 20, 0, 73});
    PixelChunkStreamCursor cursor;
    const auto             batch = cursor.capture(source, {0, 0, 0, 0}).expect("capture").batch;
    const auto             compressed =
        archiveChunkBatch(batch, eve::PersistentId::nil(), PixelChunkArchiveCodec::Zstd, hash).expect("zstd");
    const auto raw =
        archiveChunkBatch(batch, eve::PersistentId::nil(), PixelChunkArchiveCodec::None, hash).expect("raw");
    const auto compressedJson = eve::serializeSnapshotEnvelope(compressed).expect("serialize zstd");
    const auto rawJson        = eve::serializeSnapshotEnvelope(raw).expect("serialize raw");
    CHECK(compressedJson.size() < rawJson.size() / 4);
    const auto decoded = decodeChunkBatchArchive(eve::parseSnapshotEnvelope(compressedJson, hash).expect("parse"), hash)
                             .expect("decode");
    PixelWorld replica(8401);
    replica.applyChunkBatch(decoded, 0).expect("apply decoded");
    CHECK_EQ(replica.getCell(17, 23), source.getCell(17, 23));
    CHECK_EQ(replica.getCell(17, 23).thermalRemainder, std::uint16_t{73});
    REQUIRE_EQ(replica.revision(), source.revision());
}

TEST_CASE("pixelworld.archive_legacy_migration_and_corrupt_chunk_rejection") {
    const auto hash = testHash();
    PixelWorld source(8402);
    source.setMaterial(3, 4, "water");
    PixelChunkStreamCursor cursor;
    const auto             batch = cursor.capture(source, {0, 0, 0, 0}).expect("capture").batch;
    const auto             current =
        archiveChunkBatch(batch, eve::PersistentId::nil(), PixelChunkArchiveCodec::Zstd, hash).expect("archive");
    auto legacyPayload = current.payload;
    legacyPayload.getIf<eve::Value::Object>()->erase("sourceLastEditSequence");
    legacyPayload.getIf<eve::Value::Object>()->erase("fullResync");
    const auto legacy = eve::makeSnapshotEnvelope("pixelworld.chunk-batch", archiveSchema(), eve::SchemaVersion(1),
                                                  eve::PersistentId::nil(), current.revision, current.tick,
                                                  std::move(legacyPayload), hash)
                            .expect("legacy envelope");
    const auto migrated = decodeChunkBatchArchive(legacy, hash).expect("migrate");
    CHECK_EQ(migrated.sourceLastEditSequence, std::uint64_t{0});
    REQUIRE_EQ(migrated.chunks.size(), std::size_t{1});

    auto  corruptPayload = current.payload;
    auto& chunks         = *corruptPayload.getIf<eve::Value::Object>()->at("chunks").getIf<eve::Value::Array>();
    auto& stored         = *chunks.front().getIf<eve::Value::Object>()->at("stored").getIf<std::string>();
    REQUIRE(!stored.empty());
    stored[0] = stored[0] == '0' ? '1' : '0';
    const auto corrupt = eve::makeSnapshotEnvelope("pixelworld.chunk-batch", archiveSchema(), eve::SchemaVersion(2),
                                                   eve::PersistentId::nil(), current.revision, current.tick,
                                                   std::move(corruptPayload), hash)
                             .expect("reseal corrupted chunk");
    CHECK(!decodeChunkBatchArchive(corrupt, hash).ok());
    auto badOuterHash        = current;
    badOuterHash.contentHash = eve::ContentId::nil();
    CHECK(!decodeChunkBatchArchive(badOuterHash, hash).ok());
}

TEST_CASE("pixelworld.transport_buffers_out_of_order_parts_and_commits_only_complete_transfer") {
    PixelWorld source(8501);
    source.setMaterial(1, 1, "stone");
    source.setMaterial(kPixelChunkSize + 1, 1, "water");
    source.setMaterial(2 * kPixelChunkSize + 1, 1, "wood");
    auto       sender   = ReliablePixelChunkSender::create(91).expect("sender");
    auto       receiver = ReliablePixelChunkReceiver::create(91).expect("receiver");
    const auto parts    = sender.capture(source, {0, 0, 2, 0}).expect("capture");
    REQUIRE_EQ(parts.size(), std::size_t{3});
    PixelWorld replica(8501);
    CHECK_EQ(receiver.receive(parts[2], replica).expect("part 2").transfersApplied, std::uint32_t{0});
    CHECK_EQ(receiver.receive(parts[0], replica).expect("part 0").transfersApplied, std::uint32_t{0});
    CHECK(receiver.receive(parts[2], replica).expect("duplicate").duplicate);
    CHECK_EQ(replica.chunkCount(), std::size_t{0});
    CHECK_EQ(sender.pendingParts().size(), std::size_t{3});
    const auto completed = receiver.receive(sender.pendingParts()[1], replica).expect("missing part");
    CHECK_EQ(completed.transfersApplied, std::uint32_t{1});
    CHECK_EQ(completed.ack.acknowledgedThrough, std::uint64_t{1});
    CHECK_EQ(replica.getCell(2 * kPixelChunkSize + 1, 1), source.getCell(2 * kPixelChunkSize + 1, 1));
    CHECK_EQ(sender.acknowledge(completed.ack).expect("ACK").transfersReleased, std::uint32_t{1});
    CHECK_EQ(sender.inFlightTransferCount(), std::size_t{0});
}

TEST_CASE("pixelworld.transport_orders_transfers_and_retains_eviction_until_ACK") {
    PixelWorld source(8502);
    source.setMaterial(1, 1, "stone");
    source.setMaterial(kPixelChunkSize + 1, 1, "water");
    auto                     sender   = ReliablePixelChunkSender::create(92).expect("sender");
    auto                     receiver = ReliablePixelChunkReceiver::create(92).expect("receiver");
    PixelWorld               replica(8502);
    const auto               initial = sender.capture(source, {0, 0, 1, 0}).expect("initial");
    PixelChunkReceiveReceipt receipt;
    for (const auto& part : initial) receipt = receiver.receive(part, replica).expect("initial part");
    sender.acknowledge(receipt.ack).expect("initial ACK");

    source.setMaterial(2, 2, "oil");
    const auto second = sender.capture(source, {0, 0, 1, 0}).expect("second");
    source.setMaterial(kPixelChunkSize + 2, 2, "wood");
    const auto third = sender.capture(source, {0, 0, 1, 0}).expect("third");
    REQUIRE_EQ(second.size(), std::size_t{1});
    REQUIRE_EQ(third.size(), std::size_t{1});
    CHECK_EQ(receiver.receive(third.front(), replica).expect("third early").transfersApplied, std::uint32_t{0});
    const auto ordered = receiver.receive(second.front(), replica).expect("fill gap");
    CHECK_EQ(ordered.transfersApplied, std::uint32_t{2});
    CHECK_EQ(ordered.ack.acknowledgedThrough, std::uint64_t{3});
    CHECK_EQ(replica.getCell(kPixelChunkSize + 2, 2), source.getCell(kPixelChunkSize + 2, 2));
    sender.acknowledge(ordered.ack).expect("ordered ACK");

    const auto eviction = sender.capture(source, {0, 0, 0, 0}).expect("eviction");
    REQUIRE_EQ(eviction.size(), std::size_t{1});
    CHECK(eviction.front().batch.chunks.front().removed);
    CHECK_EQ(sender.retainedTombstoneCount(), std::size_t{1});
    const auto tombstone = receiver.receive(eviction.front(), replica).expect("apply tombstone");
    CHECK_EQ(replica.getMaterial(kPixelChunkSize + 1, 1), int(MaterialId::Air));
    sender.acknowledge(tombstone.ack).expect("eviction ACK");
    CHECK_EQ(sender.retainedTombstoneCount(), std::size_t{0});
    CHECK_EQ(sender.acknowledge(tombstone.ack).expect("duplicate ACK").transfersReleased, std::uint32_t{0});
}

TEST_CASE("pixelworld.transport_window_failure_preserves_capture_cursor") {
    PixelChunkTransportConfig config;
    config.maximumInFlightTransfers = 1;
    PixelWorld source(8503);
    source.setMaterial(1, 1, "stone");
    auto       sender   = ReliablePixelChunkSender::create(93, config).expect("sender");
    auto       receiver = ReliablePixelChunkReceiver::create(93, config).expect("receiver");
    PixelWorld replica(8503);
    const auto initial = sender.capture(source, {0, 0, 0, 0}).expect("first");
    source.setMaterial(2, 2, "water");
    CHECK(!sender.capture(source, {0, 0, 0, 0}).ok());
    const auto first = receiver.receive(initial.front(), replica).expect("receive");
    sender.acknowledge(first.ack).expect("free window");
    const auto retry = sender.capture(source, {0, 0, 0, 0}).expect("retry");
    REQUIRE_EQ(retry.size(), std::size_t{1});
    CHECK_EQ(retry.front().batch.sourceRevision, source.revision());
    CHECK_EQ(int(retry.front().batch.chunks.front().cells[2 * kPixelChunkSize + 2].material), int(MaterialId::Water));

    CHECK_EQ(sender.inFlightTransferCount(), std::size_t{1});
}

TEST_CASE("pixelworld.transport_conflicting_duplicate_and_invalid_ack_are_rejected") {
    PixelWorld source(8504);
    source.setMaterial(1, 1, "stone");
    source.setMaterial(kPixelChunkSize + 1, 1, "water");
    auto       sender   = ReliablePixelChunkSender::create(94).expect("sender");
    auto       receiver = ReliablePixelChunkReceiver::create(94).expect("receiver");
    const auto parts    = sender.capture(source, {0, 0, 1, 0}).expect("capture");
    REQUIRE_EQ(parts.size(), std::size_t{2});
    PixelWorld replica(8504);
    receiver.receive(parts.front(), replica).expect("partial transfer");
    auto conflicting = parts.front();
    conflicting.batch.sourceRevision += 1;
    CHECK(!receiver.receive(std::move(conflicting), replica).ok());
    CHECK_EQ(replica.chunkCount(), std::size_t{0});
    CHECK(!sender.acknowledge({94, 1, source.revision() + 1}).ok());
    CHECK_EQ(sender.inFlightTransferCount(), std::size_t{1});
}
