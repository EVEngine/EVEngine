#include "pixelworld_replay/PixelWorldReplay.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <set>
#include <type_traits>

namespace eve::pixelworld_replay {
namespace {

constexpr std::string_view kReplayType = "pixelworld.replay-log";
constexpr std::uint64_t kReplaySchemaVersion = 2;
constexpr std::size_t kMaximumReplayEntries = 1'000'000;
constexpr std::size_t kMaximumReplayCheckpoints = 100'000;
constexpr std::size_t kMaximumReplayChunkDigests = 1'000'000;

template <class T>
eve::Result<T> fail(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path), {},
                                                           "pixelworld.replay"));
}

eve::LogicalId replaySchema() {
    const auto parsed = eve::LogicalId::parse("pixelworld:replay-log");
    if (!parsed) std::terminate();
    return *parsed;
}

eve::Result<const eve::Value::Object*> object(const eve::Value& value, std::string path,
                                               std::initializer_list<std::string_view> fields) {
    const auto* result = value.getIf<eve::Value::Object>();
    if (!result || result->size() != fields.size())
        return fail<const eve::Value::Object*>(eve::DiagnosticCode::ParseError,
                                               "replay object has unknown or missing fields", std::move(path));
    for (const auto field : fields)
        if (!result->contains(std::string(field)))
            return fail<const eve::Value::Object*>(eve::DiagnosticCode::ParseError,
                                                   "replay object has unknown or missing fields", std::move(path));
    return eve::Result<const eve::Value::Object*>::success(result);
}

eve::Result<std::uint64_t> decimal(const eve::Value& value, std::string path) {
    const auto* text = value.getIf<std::string>();
    if (!text) return fail<std::uint64_t>(eve::DiagnosticCode::ParseError, "expected decimal string", path);
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), result);
    if (error != std::errc{} || end != text->data() + text->size())
        return fail<std::uint64_t>(eve::DiagnosticCode::ParseError, "invalid decimal string", std::move(path));
    return eve::Result<std::uint64_t>::success(result);
}

eve::Result<int> integer(const eve::Value& value, std::string path) {
    const auto* number = value.getIf<std::int64_t>();
    if (!number || *number < std::numeric_limits<int>::min() || *number > std::numeric_limits<int>::max())
        return fail<int>(eve::DiagnosticCode::ParseError, "expected in-range integer", std::move(path));
    return eve::Result<int>::success(static_cast<int>(*number));
}

eve::Value commandValue(const eve::pixelworld::PixelEditCommand& command) {
    return eve::Value(eve::Value::Object{
        {"centerX", eve::Value(command.centerX)},
        {"centerY", eve::Value(command.centerY)},
        {"kind", eve::Value(static_cast<int>(command.kind))},
        {"material", eve::Value(static_cast<int>(command.material))},
        {"radius", eve::Value(command.radius)},
        {"sequence", eve::Value(std::to_string(command.sequence))},
        {"strength", eve::Value(command.strength)},
        {"temperatureDelta", eve::Value(int(command.temperatureDelta))},
    });
}

eve::Result<eve::pixelworld::PixelEditCommand> parseCommand(const eve::Value& value, const std::string& path) {
    auto record = object(value, path, {"centerX", "centerY", "kind", "material", "radius", "sequence",
                                       "strength", "temperatureDelta"});
    if (!record) return eve::Result<eve::pixelworld::PixelEditCommand>::failure(record.status());
    eve::pixelworld::PixelEditCommand command;
    auto centerX = integer(record.value()->at("centerX"), path + ".centerX");
    auto centerY = integer(record.value()->at("centerY"), path + ".centerY");
    auto kind = integer(record.value()->at("kind"), path + ".kind");
    auto material = integer(record.value()->at("material"), path + ".material");
    auto radius = integer(record.value()->at("radius"), path + ".radius");
    auto sequence = decimal(record.value()->at("sequence"), path + ".sequence");
    auto strength = integer(record.value()->at("strength"), path + ".strength");
    auto temperature = integer(record.value()->at("temperatureDelta"), path + ".temperatureDelta");
    if (!centerX || !centerY || !kind || !material || !radius || !sequence || !strength || !temperature)
        return fail<eve::pixelworld::PixelEditCommand>(eve::DiagnosticCode::ParseError,
                                                       "invalid replay command", path);
    if (kind.value() < 0 || kind.value() > static_cast<int>(eve::pixelworld::PixelEditKind::Explosion) ||
        material.value() < 0 || material.value() > std::numeric_limits<std::uint16_t>::max() ||
        temperature.value() < std::numeric_limits<std::int16_t>::min() ||
        temperature.value() > std::numeric_limits<std::int16_t>::max())
        return fail<eve::pixelworld::PixelEditCommand>(eve::DiagnosticCode::ParseError,
                                                       "replay command enum or scalar is out of range", path);
    command.sequence = sequence.value();
    command.kind = static_cast<eve::pixelworld::PixelEditKind>(kind.value());
    command.centerX = centerX.value();
    command.centerY = centerY.value();
    command.radius = radius.value();
    command.material = static_cast<eve::pixelworld::MaterialId>(material.value());
    command.strength = strength.value();
    command.temperatureDelta = static_cast<std::int16_t>(temperature.value());
    return eve::Result<eve::pixelworld::PixelEditCommand>::success(command);
}

std::uint64_t hashBytes(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte value : bytes) {
        hash ^= std::uint64_t(std::to_integer<std::uint8_t>(value));
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <class T>
void hashValue(std::uint64_t& hash, T value) noexcept {
    static_assert(std::is_integral_v<T>);
    using Unsigned = std::make_unsigned_t<T>;
    static_assert(sizeof(Unsigned) <= sizeof(std::uint64_t));
    std::uint64_t encoded = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        hash ^= encoded & 0xffULL;
        hash *= 1099511628211ULL;
        encoded >>= 8;
    }
}

std::uint64_t hashChunk(const eve::pixelworld::PixelChunkSnapshot& chunk) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hashValue(hash, std::int32_t(chunk.x));
    hashValue(hash, std::int32_t(chunk.y));
    for (const auto cell : chunk.cells) {
        hashValue(hash, std::uint16_t(cell.material));
        hashValue(hash, cell.temperature);
        hashValue(hash, cell.lifetime);
        hashValue(hash, cell.thermalRemainder);
    }
    return hash;
}

eve::Result<PixelReplayCheckpoint> checkpointFor(const eve::pixelworld::PixelWorld& world) {
    auto snapshot = world.saveSnapshot();
    if (!snapshot.ok()) return eve::Result<PixelReplayCheckpoint>::failure(snapshot.status());
    PixelReplayCheckpoint checkpoint;
    checkpoint.tick = eve::SimulationTick(world.tickValue());
    checkpoint.revision = world.revision();
    checkpoint.worldDigest = hashBytes(snapshot.value());
    for (const auto& chunk : world.snapshotChangedChunks(0))
        if (!chunk.removed)
            checkpoint.chunks.push_back({chunk.x, chunk.y, hashChunk(chunk)});
    return eve::Result<PixelReplayCheckpoint>::success(std::move(checkpoint));
}

eve::Value replayPayload(const std::vector<PixelReplayEntry>& entries,
                         const std::vector<PixelReplayCheckpoint>& checkpoints) {
    eve::Value::Array encodedEntries;
    encodedEntries.reserve(entries.size());
    for (const auto& entry : entries)
        encodedEntries.emplace_back(eve::Value::Object{
            {"command", commandValue(entry.command)},
            {"tick", eve::Value(std::to_string(entry.tick.value()))},
        });

    eve::Value::Array encodedCheckpoints;
    encodedCheckpoints.reserve(checkpoints.size());
    for (const auto& checkpoint : checkpoints) {
        eve::Value::Array chunks;
        chunks.reserve(checkpoint.chunks.size());
        for (const auto& chunk : checkpoint.chunks)
            chunks.emplace_back(eve::Value::Object{
                {"digest", eve::Value(std::to_string(chunk.digest))},
                {"x", eve::Value(chunk.x)},
                {"y", eve::Value(chunk.y)},
            });
        encodedCheckpoints.emplace_back(eve::Value::Object{
            {"chunks", eve::Value(std::move(chunks))},
            {"revision", eve::Value(std::to_string(checkpoint.revision))},
            {"tick", eve::Value(std::to_string(checkpoint.tick.value()))},
            {"worldDigest", eve::Value(std::to_string(checkpoint.worldDigest))},
        });
    }
    return eve::Value(eve::Value::Object{
        {"checkpoints", eve::Value(std::move(encodedCheckpoints))},
        {"entries", eve::Value(std::move(encodedEntries))},
    });
}

PixelReplayDivergence compare(const PixelReplayCheckpoint& expected,
                              const PixelReplayCheckpoint& actual) {
    PixelReplayDivergence divergence;
    divergence.tick = expected.tick;
    divergence.expectedRevision = expected.revision;
    divergence.actualRevision = actual.revision;
    const std::size_t count = std::max(expected.chunks.size(), actual.chunks.size());
    for (std::size_t index = 0; index < count; ++index) {
        const bool expectedPresent = index < expected.chunks.size();
        const bool actualPresent = index < actual.chunks.size();
        if (expectedPresent && actualPresent && expected.chunks[index] == actual.chunks[index]) continue;
        if (expectedPresent) divergence.expectedChunk = expected.chunks[index];
        if (actualPresent) divergence.actualChunk = actual.chunks[index];
        break;
    }
    return divergence;
}

}  // namespace

eve::Result<void> PixelReplayLog::append(PixelReplayEntry entry) {
    if (entry.tick.value() == 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "replay command Tick must be positive",
            "tick", {}, "pixelworld.replay"));
    if (!entries_.empty() && entry.tick.value() < entries_.back().tick.value())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "replay command Ticks must be nondecreasing",
            "tick", {}, "pixelworld.replay"));
    const std::uint64_t expectedSequence = entries_.empty() ? 1 : entries_.back().command.sequence + 1;
    if (entry.command.sequence != expectedSequence)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "replay edit sequence must be contiguous",
            "command.sequence", {}, "pixelworld.replay"));
    entries_.push_back(std::move(entry));
    return eve::Result<void>::success();
}

eve::Result<void> PixelReplayLog::captureCheckpoint(const eve::pixelworld::PixelWorld& world) {
    if (!checkpoints_.empty() && world.tickValue() <= checkpoints_.back().tick.value())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "checkpoint Tick must increase",
            "tick", {}, "pixelworld.replay"));
    auto checkpoint = checkpointFor(world);
    if (!checkpoint.ok()) return eve::Result<void>::failure(checkpoint.status());
    checkpoints_.push_back(std::move(checkpoint).value());
    return eve::Result<void>::success();
}

eve::Result<PixelReplayReceipt> PixelReplayLog::replay(
    eve::pixelworld::PixelWorld& target, eve::SimulationTick throughTick) const {
    if (throughTick.value() == 0 || target.tickValue() != 0 || target.chunkCount() != 0 ||
        target.lastEditSequence() != 0)
        return eve::Result<PixelReplayReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "replay requires a positive target Tick and an empty target world",
            "target", {}, "pixelworld.replay"));
    if ((!entries_.empty() && entries_.back().tick.value() > throughTick.value()) ||
        (!checkpoints_.empty() && checkpoints_.back().tick.value() > throughTick.value()))
        return eve::Result<PixelReplayReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "throughTick does not cover the complete log",
            "throughTick", {}, "pixelworld.replay"));

    PixelReplayReceipt receipt;
    std::size_t entryIndex = 0;
    std::size_t checkpointIndex = 0;
    for (std::uint64_t tick = 1; tick <= throughTick.value(); ++tick) {
        while (entryIndex < entries_.size() && entries_[entryIndex].tick.value() == tick) {
            auto applied = target.applyEdit(entries_[entryIndex].command);
            if (!applied.ok()) return eve::Result<PixelReplayReceipt>::failure(applied.status());
            ++entryIndex;
            ++receipt.commandsApplied;
        }
        auto advanced = target.advance(eve::SimulationTick(tick));
        if (!advanced.ok()) return eve::Result<PixelReplayReceipt>::failure(advanced.status());
        receipt.reachedTick = eve::SimulationTick(tick);
        if (checkpointIndex < checkpoints_.size() && checkpoints_[checkpointIndex].tick.value() == tick) {
            auto actual = checkpointFor(target);
            if (!actual.ok()) return eve::Result<PixelReplayReceipt>::failure(actual.status());
            const auto& expected = checkpoints_[checkpointIndex];
            if (actual.value().worldDigest != expected.worldDigest) {
                receipt.match = PixelReplayMatch::Diverged;
                receipt.firstDivergence = compare(expected, actual.value());
                return eve::Result<PixelReplayReceipt>::success(std::move(receipt));
            }
            ++checkpointIndex;
        }
    }
    return eve::Result<PixelReplayReceipt>::success(std::move(receipt));
}

eve::Result<eve::SnapshotEnvelope> PixelReplayLog::snapshot(
    eve::PersistentId instanceId, const eve::SnapshotHashProvider& hashProvider) const {
    const std::uint64_t entryTick = entries_.empty() ? 0 : entries_.back().tick.value();
    const std::uint64_t checkpointTick = checkpoints_.empty() ? 0 : checkpoints_.back().tick.value();
    return eve::makeSnapshotEnvelope(
        std::string(kReplayType), replaySchema(), eve::SchemaVersion(kReplaySchemaVersion), instanceId,
        eve::Revision(entries_.size() + checkpoints_.size()),
        eve::SimulationTick(std::max(entryTick, checkpointTick)), replayPayload(entries_, checkpoints_),
        hashProvider);
}

eve::Result<void> PixelReplayLog::restoreSnapshot(
    const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider) {
    auto verified = eve::verifySnapshotEnvelope(snapshot, hashProvider);
    if (!verified) return verified;
    if (snapshot.type != kReplayType || snapshot.schema != replaySchema())
        return fail<void>(eve::DiagnosticCode::InvalidArgument, "replay snapshot type or schema does not match",
                          "schema");
    if (snapshot.schemaVersion.value() < 1 || snapshot.schemaVersion.value() > kReplaySchemaVersion)
        return fail<void>(eve::DiagnosticCode::UnknownVersion, "unsupported replay snapshot schema version",
                          "schemaVersion");

    auto root = object(snapshot.payload, "payload", {"checkpoints", "entries"});
    if (!root) return eve::Result<void>::failure(root.status());
    const auto* encodedEntries = root.value()->at("entries").getIf<eve::Value::Array>();
    const auto* encodedCheckpoints = root.value()->at("checkpoints").getIf<eve::Value::Array>();
    if (!encodedEntries || !encodedCheckpoints || encodedEntries->size() > kMaximumReplayEntries ||
        encodedCheckpoints->size() > kMaximumReplayCheckpoints)
        return fail<void>(eve::DiagnosticCode::ParseError, "replay arrays are invalid or exceed decode budget",
                          "payload");

    PixelReplayLog candidate;
    for (std::size_t index = 0; index < encodedEntries->size(); ++index) {
        const std::string path = "payload.entries[" + std::to_string(index) + "]";
        auto record = object((*encodedEntries)[index], path, {"command", "tick"});
        if (!record) return eve::Result<void>::failure(record.status());
        auto tick = decimal(record.value()->at("tick"), path + ".tick");
        auto command = parseCommand(record.value()->at("command"), path + ".command");
        if (!tick || !command)
            return fail<void>(eve::DiagnosticCode::ParseError, "invalid replay entry", path);
        auto appended = candidate.append({eve::SimulationTick(tick.value()), std::move(command).value()});
        if (!appended) return appended;
    }

    std::size_t totalChunks = 0;
    std::uint64_t previousCheckpointTick = 0;
    for (std::size_t index = 0; index < encodedCheckpoints->size(); ++index) {
        const std::string path = "payload.checkpoints[" + std::to_string(index) + "]";
        const bool versionOne = snapshot.schemaVersion.value() == 1;
        auto record = versionOne
                          ? object((*encodedCheckpoints)[index], path, {"revision", "tick", "worldDigest"})
                          : object((*encodedCheckpoints)[index], path,
                                   {"chunks", "revision", "tick", "worldDigest"});
        if (!record) return eve::Result<void>::failure(record.status());
        auto tick = decimal(record.value()->at("tick"), path + ".tick");
        auto revision = decimal(record.value()->at("revision"), path + ".revision");
        auto worldDigest = decimal(record.value()->at("worldDigest"), path + ".worldDigest");
        if (!tick || !revision || !worldDigest || tick.value() == 0 || tick.value() <= previousCheckpointTick)
            return fail<void>(eve::DiagnosticCode::ParseError, "invalid replay checkpoint", path);
        PixelReplayCheckpoint checkpoint;
        checkpoint.tick = eve::SimulationTick(tick.value());
        checkpoint.revision = revision.value();
        checkpoint.worldDigest = worldDigest.value();

        if (!versionOne) {
            const auto* chunks = record.value()->at("chunks").getIf<eve::Value::Array>();
            if (!chunks || chunks->size() > kMaximumReplayChunkDigests - totalChunks)
                return fail<void>(eve::DiagnosticCode::ParseError,
                                  "replay Chunk digests exceed decode budget", path + ".chunks");
            totalChunks += chunks->size();
            for (std::size_t chunkIndex = 0; chunkIndex < chunks->size(); ++chunkIndex) {
                const std::string chunkPath = path + ".chunks[" + std::to_string(chunkIndex) + "]";
                auto chunk = object((*chunks)[chunkIndex], chunkPath, {"digest", "x", "y"});
                if (!chunk) return eve::Result<void>::failure(chunk.status());
                auto x = integer(chunk.value()->at("x"), chunkPath + ".x");
                auto y = integer(chunk.value()->at("y"), chunkPath + ".y");
                auto digest = decimal(chunk.value()->at("digest"), chunkPath + ".digest");
                if (!x || !y || !digest)
                    return fail<void>(eve::DiagnosticCode::ParseError, "invalid replay Chunk digest", chunkPath);
                PixelChunkDigest decoded{x.value(), y.value(), digest.value()};
                if (!checkpoint.chunks.empty()) {
                    const auto& prior = checkpoint.chunks.back();
                    if (decoded.y < prior.y || (decoded.y == prior.y && decoded.x <= prior.x))
                        return fail<void>(eve::DiagnosticCode::ParseError,
                                          "replay Chunk digests must be unique canonical order", chunkPath);
                }
                checkpoint.chunks.push_back(decoded);
            }
        }
        previousCheckpointTick = tick.value();
        candidate.checkpoints_.push_back(std::move(checkpoint));
    }

    const std::uint64_t entryTick = candidate.entries_.empty() ? 0 : candidate.entries_.back().tick.value();
    const std::uint64_t checkpointTick = candidate.checkpoints_.empty()
                                             ? 0
                                             : candidate.checkpoints_.back().tick.value();
    if (snapshot.revision.value() != candidate.entries_.size() + candidate.checkpoints_.size() ||
        snapshot.tick.value() != std::max(entryTick, checkpointTick))
        return fail<void>(eve::DiagnosticCode::Conflict,
                          "replay payload count or Tick disagrees with envelope metadata", "payload");

    entries_ = std::move(candidate.entries_);
    checkpoints_ = std::move(candidate.checkpoints_);
    return eve::Result<void>::success();
}

}  // namespace eve::pixelworld_replay
