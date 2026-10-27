// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "pixelworld_replay.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorld.h"
#include "pixelworld/replay/PixelWorldReplay.h"

#include <algorithm>
#include <cstdint>
#include <string>

using namespace eve::pixelworld;
using namespace eve::pixelworld_replay;

namespace {

eve::SnapshotHashProvider fixedHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        std::uint64_t a = 14695981039346656037ULL;
        std::uint64_t b = 1099511628211ULL;
        for (const unsigned char byte : input) {
            a = (a ^ byte) * 1099511628211ULL;
            b = (b ^ (std::uint64_t(byte) + 0x9e3779b97f4a7c15ULL)) * 14029467366897019727ULL;
        }
        eve::ContentId::Bytes bytes{};
        for (int i = 0; i < 8; ++i) {
            bytes[std::size_t(i)]     = std::uint8_t(a >> (56 - i * 8));
            bytes[std::size_t(i + 8)] = std::uint8_t(b >> (56 - i * 8));
        }
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

PixelEditCommand paint(std::uint64_t sequence, int x, MaterialId material = MaterialId::Sand) {
    PixelEditCommand command;
    command.sequence = sequence;
    command.kind     = PixelEditKind::PaintCircle;
    command.centerX  = x;
    command.centerY  = 5;
    command.radius   = 8;
    command.material = material;
    return command;
}

eve::LogicalId schemaId() {
    const auto parsed = eve::LogicalId::parse("pixelworld:replay-log");
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::Value legacyPayload() {
    eve::Value::Object command{{"centerX", eve::Value(3)},
                               {"centerY", eve::Value(4)},
                               {"kind", eve::Value(int(PixelEditKind::PaintCircle))},
                               {"material", eve::Value(int(MaterialId::Sand))},
                               {"radius", eve::Value(2)},
                               {"sequence", eve::Value("1")},
                               {"strength", eve::Value(0)},
                               {"temperatureDelta", eve::Value(0)}};
    eve::Value::Array  entries;
    entries.emplace_back(eve::Value::Object{{"command", eve::Value(std::move(command))}, {"tick", eve::Value("1")}});
    eve::Value::Array checkpoints;
    checkpoints.emplace_back(eve::Value::Object{
        {"revision", eve::Value("1")}, {"tick", eve::Value("2")}, {"worldDigest", eve::Value("42")}});
    return eve::Value(eve::Value::Object{{"entries", eve::Value(std::move(entries))},
                                         {"checkpoints", eve::Value(std::move(checkpoints))}});
}

}  // namespace

TEST_CASE("pixelworld.replay append enforces contiguous sequence and nondecreasing tick") {
    PixelReplayLog log;
    CHECK(!log.append({eve::SimulationTick(1), paint(2, 0)}).ok());
    CHECK(log.entries().empty());
    REQUIRE(log.append({eve::SimulationTick(2), paint(1, 0)}).ok());
    CHECK(!log.append({eve::SimulationTick(1), paint(2, 1)}).ok());
    CHECK(!log.append({eve::SimulationTick(3), paint(3, 1)}).ok());
    REQUIRE_EQ(log.entries().size(), std::size_t{1});
    CHECK_EQ(log.entries()[0].tick.value(), std::uint64_t{2});
}

TEST_CASE("pixelworld.replay reaches exact tick and reproduces authoritative world bytes") {
    PixelWorld     original(8101);
    PixelReplayLog log;
    for (std::uint64_t tick = 1; tick <= 80; ++tick) {
        if (tick == 1 || tick == 20 || tick == 45) {
            const auto command = paint(tick == 1    ? 1
                                       : tick == 20 ? 2
                                                    : 3,
                                       int(tick) - 30, tick == 45 ? MaterialId::Water : MaterialId::Sand);
            REQUIRE(log.append({eve::SimulationTick(tick), command}).ok());
            REQUIRE(original.applyEdit(command).ok());
        }
        REQUIRE(original.advance(eve::SimulationTick(tick)).ok());
        if (tick % 20 == 0) REQUIRE(log.captureCheckpoint(original).ok());
    }
    PixelWorld target(8101);
    auto       replayed = log.replay(target, eve::SimulationTick(80));
    REQUIRE(replayed.ok());
    CHECK(replayed.value().match == PixelReplayMatch::Matched);
    CHECK_EQ(replayed.value().reachedTick.value(), std::uint64_t{80});
    CHECK_EQ(replayed.value().commandsApplied, std::uint64_t{3});
    const auto sourceBytes = original.saveSnapshot().expect("source snapshot");
    const auto targetBytes = target.saveSnapshot().expect("target snapshot");
    CHECK_EQ(targetBytes, sourceBytes);
}

TEST_CASE("pixelworld.replay reports divergence as data at first checkpoint") {
    PixelWorld     source(8102);
    PixelReplayLog log;
    source.paintCircle(0, 0, 5, "stone");
    for (std::uint64_t tick = 1; tick <= 10; ++tick) REQUIRE(source.advance(eve::SimulationTick(tick)).ok());
    REQUIRE(log.captureCheckpoint(source).ok());
    PixelWorld target(8102);
    auto       result = log.replay(target, eve::SimulationTick(10));
    REQUIRE(result.ok());
    CHECK(result.value().match == PixelReplayMatch::Diverged);
    REQUIRE(result.value().firstDivergence.has_value());
    CHECK_EQ(result.value().firstDivergence->tick.value(), std::uint64_t{10});
    const bool hasChunkEvidence = result.value().firstDivergence->expectedChunk.has_value() ||
                                  result.value().firstDivergence->actualChunk.has_value();
    CHECK(hasChunkEvidence);
}

TEST_CASE("pixelworld.replay rejects nonempty target before changing it") {
    PixelReplayLog log;
    REQUIRE(log.append({eve::SimulationTick(1), paint(1, 2)}).ok());
    PixelWorld target(8103);
    target.setMaterial(0, 0, "stone");
    const auto before = target.saveSnapshot().expect("before");
    CHECK(!log.replay(target, eve::SimulationTick(1)).ok());
    const auto after = target.saveSnapshot().expect("after");
    CHECK_EQ(after, before);
    CHECK_EQ(log.entries().size(), std::size_t{1});
}

TEST_CASE("pixelworld.replay checkpoint digest uses canonical byte ordering") {
    PixelWorld world(8104);
    world.setMaterial(-1, 0, "stone");
    REQUIRE(world.advance(eve::SimulationTick(1)).ok());
    PixelReplayLog log;
    REQUIRE(log.captureCheckpoint(world).ok());
    REQUIRE_EQ(log.checkpoints().size(), std::size_t{1});
    REQUIRE_EQ(log.checkpoints()[0].chunks.size(), std::size_t{1});
    CHECK_EQ(log.checkpoints()[0].chunks[0].x, -1);
    CHECK_EQ(log.checkpoints()[0].chunks[0].y, 0);
    // Little-endian x/y followed by material, temperature, lifetime and thermal remainder per cell.
    CHECK_EQ(log.checkpoints()[0].chunks[0].digest, std::uint64_t{9661110577551370436ULL});
}

TEST_CASE("pixelworld.replay version 2 snapshot restores complete log and replay behavior") {
    const auto     hash = fixedHash();
    PixelWorld     source(8201);
    PixelReplayLog log;
    auto           command = paint(1, -9, MaterialId::Water);
    REQUIRE(log.append({eve::SimulationTick(1), command}).ok());
    REQUIRE(source.applyEdit(command).ok());
    for (std::uint64_t tick = 1; tick <= 6; ++tick) {
        REQUIRE(source.advance(eve::SimulationTick(tick)).ok());
        if (tick == 3 || tick == 6) REQUIRE(log.captureCheckpoint(source).ok());
    }
    auto envelope = log.snapshot(eve::PersistentId::nil(), hash);
    REQUIRE(envelope.ok());
    CHECK_EQ(envelope.value().schemaVersion.value(), std::uint64_t{2});
    auto json = eve::serializeSnapshotEnvelope(envelope.value());
    REQUIRE(json.ok());
    auto parsed = eve::parseSnapshotEnvelope(json.value(), hash);
    REQUIRE(parsed.ok());
    PixelReplayLog restored;
    REQUIRE(restored.restoreSnapshot(parsed.value(), hash).ok());
    CHECK_EQ(restored.entries().size(), log.entries().size());
    CHECK_EQ(restored.checkpoints().back().chunks, log.checkpoints().back().chunks);
    PixelWorld replayed(8201);
    auto       result = restored.replay(replayed, eve::SimulationTick(6));
    REQUIRE(result.ok());
    CHECK(result.value().match == PixelReplayMatch::Matched);
}

TEST_CASE("pixelworld.replay migrates schema version 1 checkpoints without fabricated chunk diagnostics") {
    const auto hash = fixedHash();
    auto       legacy =
        eve::makeSnapshotEnvelope("pixelworld.replay-log", schemaId(), eve::SchemaVersion(1), eve::PersistentId::nil(),
                                  eve::Revision(2), eve::SimulationTick(2), legacyPayload(), hash);
    REQUIRE(legacy.ok());
    PixelReplayLog log;
    REQUIRE(log.restoreSnapshot(legacy.value(), hash).ok());
    REQUIRE_EQ(log.entries().size(), std::size_t{1});
    REQUIRE_EQ(log.checkpoints().size(), std::size_t{1});
    CHECK(log.checkpoints()[0].chunks.empty());
    auto upgraded = log.snapshot(eve::PersistentId::nil(), hash);
    REQUIRE(upgraded.ok());
    CHECK_EQ(upgraded.value().schemaVersion.value(), std::uint64_t{2});
}

TEST_CASE("pixelworld.replay invalid envelopes leave existing entries and checkpoints unchanged") {
    const auto     hash = fixedHash();
    PixelReplayLog log;
    REQUIRE(log.append({eve::SimulationTick(1), paint(1, 0)}).ok());
    auto baseline = log.snapshot(eve::PersistentId::nil(), hash);
    REQUIRE(baseline.ok());
    auto corruptHash        = baseline.value();
    corruptHash.contentHash = eve::ContentId::nil();
    CHECK(!log.restoreSnapshot(corruptHash, hash).ok());

    auto future =
        eve::makeSnapshotEnvelope("pixelworld.replay-log", schemaId(), eve::SchemaVersion(3), eve::PersistentId::nil(),
                                  baseline.value().revision, baseline.value().tick, baseline.value().payload, hash);
    REQUIRE(future.ok());
    CHECK(!log.restoreSnapshot(future.value(), hash).ok());
    CHECK_EQ(log.entries().size(), std::size_t{1});
    CHECK(log.checkpoints().empty());
    auto after = log.snapshot(eve::PersistentId::nil(), hash);
    REQUIRE(after.ok());
    CHECK_EQ(after.value().contentHash, baseline.value().contentHash);
}
