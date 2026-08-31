#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorld.h"
#include "pixelworld_replay/PixelWorldReplay.h"

#include <algorithm>
#include <cstdint>
#include <string>

using namespace eve::pixelworld;
using namespace eve::pixelworld_replay;

namespace {

eve::SnapshotHashProvider replayHashProvider() {
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

eve::LogicalId replaySchema() {
    const auto parsed = eve::LogicalId::parse("pixelworld:replay-log");
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::Value versionOnePayload() {
    eve::Value::Object command{
        {"centerX", eve::Value(3)},
        {"centerY", eve::Value(4)},
        {"kind", eve::Value(int(PixelEditKind::PaintCircle))},
        {"material", eve::Value(int(MaterialId::Sand))},
        {"radius", eve::Value(2)},
        {"sequence", eve::Value("1")},
        {"strength", eve::Value(0)},
        {"temperatureDelta", eve::Value(0)},
    };
    eve::Value::Array entries;
    entries.emplace_back(eve::Value::Object{
        {"command", eve::Value(std::move(command))},
        {"tick", eve::Value("1")},
    });
    eve::Value::Array checkpoints;
    checkpoints.emplace_back(eve::Value::Object{
        {"revision", eve::Value("1")},
        {"tick", eve::Value("2")},
        {"worldDigest", eve::Value("42")},
    });
    return eve::Value(eve::Value::Object{
        {"checkpoints", eve::Value(std::move(checkpoints))},
        {"entries", eve::Value(std::move(entries))},
    });
}

}  // namespace

TEST_CASE("pixelworld_replay.command_log_checkpoints_replay_bit_exact") {
    PixelWorld source(8101);
    PixelReplayLog log;
    for (std::uint64_t tick = 1; tick <= 80; ++tick) {
        if (tick == 1 || tick == 20 || tick == 45) {
            PixelEditCommand command;
            command.sequence = tick == 1 ? 1 : tick == 20 ? 2 : 3;
            command.kind = PixelEditKind::PaintCircle;
            command.centerX = int(tick) - 30;
            command.centerY = 5;
            command.radius = 8;
            command.material = tick == 45 ? MaterialId::Water : MaterialId::Sand;
            log.append({eve::SimulationTick(tick), command}).expect("append replay command");
            source.applyEdit(command).expect("apply source command");
        }
        source.advance(eve::SimulationTick(tick)).expect("advance source");
        if (tick % 20 == 0) log.captureCheckpoint(source).expect("capture checkpoint");
    }

    PixelWorld replayed(8101);
    const auto receipt = log.replay(replayed, eve::SimulationTick(80)).expect("replay log");
    CHECK_EQ(int(receipt.match), int(PixelReplayMatch::Matched));
    CHECK_EQ(receipt.commandsApplied, std::uint64_t(3));
    const auto expected = source.saveSnapshot().expect("source snapshot");
    const auto actual = replayed.saveSnapshot().expect("replayed snapshot");
    REQUIRE_EQ(expected.size(), actual.size());
    CHECK(std::equal(expected.begin(), expected.end(), actual.begin()));
}

TEST_CASE("pixelworld_replay_reports_first_tick_and_chunk_divergence") {
    PixelWorld source(8102);
    PixelReplayLog log;
    source.paintCircle(0, 0, 5, "stone");  // Deliberately absent from the command log.
    for (std::uint64_t tick = 1; tick <= 10; ++tick) source.advance(eve::SimulationTick(tick)).expect("step");
    log.captureCheckpoint(source).expect("checkpoint");

    PixelWorld missingInput(8102);
    const auto receipt = log.replay(missingInput, eve::SimulationTick(10)).expect("divergent replay");
    CHECK_EQ(int(receipt.match), int(PixelReplayMatch::Diverged));
    REQUIRE(receipt.firstDivergence.has_value());
    CHECK_EQ(receipt.firstDivergence->tick.value(), std::uint64_t(10));
    const bool hasChunkDiagnostic = receipt.firstDivergence->expectedChunk.has_value() ||
                                    receipt.firstDivergence->actualChunk.has_value();
    CHECK(hasChunkDiagnostic);
}

TEST_CASE("pixelworld_replay_chunk_digest_has_canonical_little_endian_encoding") {
    PixelWorld world(8104);
    world.setMaterial(-1, 0, "stone");
    world.advance(eve::SimulationTick(1)).expect("advance world");
    PixelReplayLog log;
    log.captureCheckpoint(world).expect("capture checkpoint");
    REQUIRE_EQ(log.checkpoints().size(), std::size_t(1));
    REQUIRE_EQ(log.checkpoints().front().chunks.size(), std::size_t(1));
    CHECK_EQ(log.checkpoints().front().chunks.front().digest, std::uint64_t(17357935190315814178ULL));
}

TEST_CASE("pixelworld_replay_rejects_sequence_tick_and_nonempty_target_without_log_mutation") {
    PixelReplayLog log;
    PixelEditCommand command;
    command.sequence = 2;
    auto gap = log.append({eve::SimulationTick(1), command});
    CHECK(!gap.ok());
    CHECK(log.entries().empty());

    command.sequence = 1;
    log.append({eve::SimulationTick(2), command}).expect("first entry");
    command.sequence = 2;
    auto backwards = log.append({eve::SimulationTick(1), command});
    CHECK(!backwards.ok());
    CHECK_EQ(log.entries().size(), std::size_t(1));

    PixelWorld nonempty(8103);
    nonempty.setMaterial(0, 0, "stone");
    const auto before = nonempty.saveSnapshot().expect("before rejection");
    auto rejected = log.replay(nonempty, eve::SimulationTick(2));
    CHECK(!rejected.ok());
    const auto after = nonempty.saveSnapshot().expect("after rejection");
    REQUIRE_EQ(before.size(), after.size());
    CHECK(std::equal(before.begin(), before.end(), after.begin()));
}

TEST_CASE("pixelworld_replay_snapshot_v2_round_trip_preserves_bit_exact_replay") {
    const auto hash = replayHashProvider();
    PixelWorld source(8201);
    PixelReplayLog original;
    PixelEditCommand command;
    command.sequence = 1;
    command.kind = PixelEditKind::PaintCircle;
    command.centerX = -9;
    command.centerY = 2;
    command.radius = 4;
    command.material = MaterialId::Water;
    original.append({eve::SimulationTick(1), command}).expect("append");
    source.applyEdit(command).expect("apply");
    for (std::uint64_t tick = 1; tick <= 6; ++tick) {
        source.advance(eve::SimulationTick(tick)).expect("advance");
        if (tick == 3 || tick == 6) original.captureCheckpoint(source).expect("checkpoint");
    }

    const auto sealed = original.snapshot(eve::PersistentId::nil(), hash).expect("seal replay");
    CHECK_EQ(sealed.schemaVersion.value(), std::uint64_t(2));
    const auto json = eve::serializeSnapshotEnvelope(sealed).expect("serialize replay");
    const auto parsed = eve::parseSnapshotEnvelope(json, hash).expect("parse replay");
    PixelReplayLog restored;
    restored.restoreSnapshot(parsed, hash).expect("restore replay");
    CHECK_EQ(restored.entries().size(), original.entries().size());
    CHECK_EQ(restored.checkpoints().size(), original.checkpoints().size());
    CHECK_EQ(restored.checkpoints().back().chunks, original.checkpoints().back().chunks);

    PixelWorld replayed(8201);
    const auto receipt = restored.replay(replayed, eve::SimulationTick(6)).expect("replay restored log");
    CHECK_EQ(int(receipt.match), int(PixelReplayMatch::Matched));
}

TEST_CASE("pixelworld_replay_migrates_v1_checkpoint_without_chunk_diagnostics") {
    const auto hash = replayHashProvider();
    const auto legacy = eve::makeSnapshotEnvelope(
                            "pixelworld.replay-log", replaySchema(), eve::SchemaVersion(1),
                            eve::PersistentId::nil(), eve::Revision(2), eve::SimulationTick(2),
                            versionOnePayload(), hash)
                            .expect("seal v1 replay");
    PixelReplayLog restored;
    restored.restoreSnapshot(legacy, hash).expect("restore v1 replay");
    REQUIRE_EQ(restored.entries().size(), std::size_t(1));
    REQUIRE_EQ(restored.checkpoints().size(), std::size_t(1));
    CHECK(restored.checkpoints().front().chunks.empty());
    const auto upgraded = restored.snapshot(eve::PersistentId::nil(), hash).expect("upgrade replay");
    CHECK_EQ(upgraded.schemaVersion.value(), std::uint64_t(2));
}

TEST_CASE("pixelworld_replay_bad_envelopes_leave_existing_log_unchanged") {
    const auto hash = replayHashProvider();
    PixelReplayLog existing;
    PixelEditCommand command;
    command.sequence = 1;
    existing.append({eve::SimulationTick(1), command}).expect("append existing");
    const auto before = existing.snapshot(eve::PersistentId::nil(), hash).expect("before");

    auto badHash = before;
    badHash.contentHash = eve::ContentId::nil();
    CHECK(!existing.restoreSnapshot(badHash, hash).ok());
    CHECK_EQ(existing.entries().size(), std::size_t(1));

    auto unknown = eve::makeSnapshotEnvelope(
                       "pixelworld.replay-log", replaySchema(), eve::SchemaVersion(3),
                       eve::PersistentId::nil(), before.revision, before.tick, before.payload, hash)
                       .expect("seal unknown version");
    CHECK(!existing.restoreSnapshot(unknown, hash).ok());
    CHECK_EQ(existing.entries().size(), std::size_t(1));

    auto malformedPayload = before.payload;
    malformedPayload.getIf<eve::Value::Object>()->emplace("unknown", eve::Value(true));
    auto malformed = eve::makeSnapshotEnvelope(
                         "pixelworld.replay-log", replaySchema(), eve::SchemaVersion(2),
                         eve::PersistentId::nil(), before.revision, before.tick,
                         std::move(malformedPayload), hash)
                         .expect("seal malformed payload");
    CHECK(!existing.restoreSnapshot(malformed, hash).ok());
    CHECK_EQ(existing.entries().size(), std::size_t(1));

    std::string truncated = eve::serializeSnapshotEnvelope(before).expect("serialize before");
    truncated.resize(truncated.size() / 2);
    CHECK(!eve::parseSnapshotEnvelope(truncated, hash).ok());
    CHECK_EQ(existing.entries().size(), std::size_t(1));
}
