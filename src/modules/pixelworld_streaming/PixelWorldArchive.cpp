#include "pixelworld_streaming/PixelWorldStreaming.h"

#include "asset/EvpackCompression.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <limits>
#include <string>
#include <type_traits>

namespace eve::pixelworld_streaming {
namespace {

constexpr std::string_view kArchiveType = "pixelworld.chunk-batch";
constexpr std::uint64_t kArchiveVersion = 3;
constexpr std::size_t kMaximumArchiveChunks = 65'536;
constexpr std::uint64_t kMaximumDecodedBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t kLegacyDecodedChunkBytes =
    std::size_t(eve::pixelworld::kPixelChunkSize * eve::pixelworld::kPixelChunkSize) * 5;
constexpr std::size_t kDecodedChunkBytes =
    std::size_t(eve::pixelworld::kPixelChunkSize * eve::pixelworld::kPixelChunkSize) * 7;

template <class T>
eve::Result<T> fail(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "pixelworld.chunk-archive"));
}

eve::LogicalId archiveSchema() {
    const auto parsed = eve::LogicalId::parse("pixelworld:chunk-batch");
    if (!parsed) std::terminate();
    return *parsed;
}

eve::asset::EvpackCodec assetCodec(PixelChunkArchiveCodec codec) {
    return codec == PixelChunkArchiveCodec::Zstd ? eve::asset::EvpackCodec::Zstd
                                                  : eve::asset::EvpackCodec::None;
}

std::string hex(std::span<const std::uint8_t> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = digits[bytes[index] >> 4];
        result[index * 2 + 1] = digits[bytes[index] & 0xf];
    }
    return result;
}

eve::Result<std::vector<std::uint8_t>> unhex(const eve::Value& value, std::size_t maximumBytes,
                                              std::string path) {
    const auto* text = value.getIf<std::string>();
    if (!text || text->size() % 2 != 0 || text->size() / 2 > maximumBytes)
        return fail<std::vector<std::uint8_t>>(eve::DiagnosticCode::ParseError,
                                               "invalid or oversized hexadecimal payload", std::move(path));
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    };
    std::vector<std::uint8_t> result(text->size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        const int high = nibble((*text)[index * 2]);
        const int low = nibble((*text)[index * 2 + 1]);
        if (high < 0 || low < 0)
            return fail<std::vector<std::uint8_t>>(eve::DiagnosticCode::ParseError,
                                                   "invalid hexadecimal digit", std::move(path));
        result[index] = std::uint8_t((high << 4) | low);
    }
    return eve::Result<std::vector<std::uint8_t>>::success(std::move(result));
}

template <class T>
void put(std::vector<std::uint8_t>& bytes, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    std::uint64_t encoded = static_cast<std::uint64_t>(static_cast<Unsigned>(value));
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes.push_back(std::uint8_t(encoded & 0xff));
        encoded >>= 8;
    }
}

template <class T>
T take(std::span<const std::uint8_t> bytes, std::size_t& cursor) {
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned encoded = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index)
        encoded |= Unsigned(bytes[cursor++]) << (index * 8);
    if constexpr (std::is_signed_v<T>) return std::bit_cast<T>(encoded);
    return encoded;
}

std::vector<std::uint8_t> encodeCells(const eve::pixelworld::PixelChunkSnapshot& chunk) {
    std::vector<std::uint8_t> result;
    result.reserve(kDecodedChunkBytes);
    for (const auto cell : chunk.cells) {
        put(result, std::uint16_t(cell.material));
        put(result, cell.temperature);
        put(result, cell.lifetime);
        put(result, cell.thermalRemainder);
    }
    return result;
}

eve::Result<const eve::Value::Object*> object(const eve::Value& value, std::string path,
                                               std::initializer_list<std::string_view> fields) {
    const auto* result = value.getIf<eve::Value::Object>();
    if (!result || result->size() != fields.size())
        return fail<const eve::Value::Object*>(eve::DiagnosticCode::ParseError,
                                               "archive object has unknown or missing fields", std::move(path));
    for (const auto field : fields)
        if (!result->contains(std::string(field)))
            return fail<const eve::Value::Object*>(eve::DiagnosticCode::ParseError,
                                                   "archive object has unknown or missing fields", std::move(path));
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
    return eve::Result<int>::success(int(*number));
}

eve::Result<eve::ContentId> digest(const std::vector<std::uint8_t>& decoded,
                                   const eve::SnapshotHashProvider& hashProvider) {
    if (!hashProvider)
        return fail<eve::ContentId>(eve::DiagnosticCode::Unsupported, "archive hash provider is required");
    return hashProvider(hex(decoded));
}

}  // namespace

eve::Result<eve::SnapshotEnvelope> archiveChunkBatch(
    const eve::pixelworld::PixelChunkBatch& batch, eve::PersistentId instanceId,
    PixelChunkArchiveCodec codec, const eve::SnapshotHashProvider& hashProvider) {
    if (codec != PixelChunkArchiveCodec::None && codec != PixelChunkArchiveCodec::Zstd)
        return fail<eve::SnapshotEnvelope>(eve::DiagnosticCode::Unsupported, "unknown Chunk archive codec");
    if (batch.chunks.size() > kMaximumArchiveChunks)
        return fail<eve::SnapshotEnvelope>(eve::DiagnosticCode::InvalidArgument,
                                           "Chunk archive exceeds record budget", "chunks");
    eve::Value::Array records;
    std::optional<std::pair<int, int>> previous;
    for (const auto& chunk : batch.chunks) {
        const std::pair<int, int> key{chunk.y, chunk.x};
        if (previous && !(previous.value() < key))
            return fail<eve::SnapshotEnvelope>(eve::DiagnosticCode::InvalidArgument,
                                               "Chunks must be unique canonical y/x order", "chunks");
        previous = key;
        if (chunk.revision == 0 || chunk.revision > batch.sourceRevision ||
            (chunk.removed && !chunk.cells.empty()) ||
            (!chunk.removed && chunk.cells.size() !=
                                   std::size_t(eve::pixelworld::kPixelChunkSize * eve::pixelworld::kPixelChunkSize)))
            return fail<eve::SnapshotEnvelope>(eve::DiagnosticCode::InvalidArgument,
                                               "invalid Chunk revision or cell shape", "chunks");
        const auto decoded = chunk.removed ? std::vector<std::uint8_t>{} : encodeCells(chunk);
        auto contentHash = digest(decoded, hashProvider);
        if (!contentHash) return eve::Result<eve::SnapshotEnvelope>::failure(contentHash.status());
        const auto physical = chunk.removed ? PixelChunkArchiveCodec::None : codec;
        auto stored = eve::asset::compressEvpackChunk(assetCodec(physical), decoded);
        if (!stored) return eve::Result<eve::SnapshotEnvelope>::failure(stored.status());
        records.emplace_back(eve::Value::Object{
            {"codec", eve::Value(int(physical))},
            {"contentHash", eve::Value(contentHash.value().format())},
            {"decodedSize", eve::Value(std::to_string(decoded.size()))},
            {"removed", eve::Value(chunk.removed)},
            {"revision", eve::Value(std::to_string(chunk.revision))},
            {"stored", eve::Value(hex(stored.value()))},
            {"x", eve::Value(chunk.x)},
            {"y", eve::Value(chunk.y)},
        });
    }
    eve::Value payload(eve::Value::Object{
        {"catalogFingerprint", eve::Value(std::to_string(batch.catalogFingerprint))},
        {"chunks", eve::Value(std::move(records))},
        {"fullResync", eve::Value(batch.fullResync)},
        {"sourceLastEditSequence", eve::Value(std::to_string(batch.sourceLastEditSequence))},
        {"sourceSeed", eve::Value(std::to_string(batch.sourceSeed))},
    });
    return eve::makeSnapshotEnvelope(std::string(kArchiveType), archiveSchema(),
                                     eve::SchemaVersion(kArchiveVersion), instanceId,
                                     eve::Revision(batch.sourceRevision), batch.sourceTick,
                                     std::move(payload), hashProvider);
}

eve::Result<eve::pixelworld::PixelChunkBatch> decodeChunkBatchArchive(
    const eve::SnapshotEnvelope& snapshot, const eve::SnapshotHashProvider& hashProvider) {
    auto verified = eve::verifySnapshotEnvelope(snapshot, hashProvider);
    if (!verified) return eve::Result<eve::pixelworld::PixelChunkBatch>::failure(verified.status());
    if (snapshot.type != kArchiveType || snapshot.schema != archiveSchema())
        return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::InvalidArgument,
                                                      "Chunk archive type or schema does not match", "schema");
    if (snapshot.schemaVersion.value() < 1 || snapshot.schemaVersion.value() > kArchiveVersion)
        return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::UnknownVersion,
                                                      "unsupported Chunk archive version", "schemaVersion");
    const bool versionOne = snapshot.schemaVersion.value() == 1;
    const bool versionTwo = snapshot.schemaVersion.value() == 2;
    auto root = versionOne
                    ? object(snapshot.payload, "payload", {"catalogFingerprint", "chunks", "sourceSeed"})
                    : versionTwo
                          ? object(snapshot.payload, "payload", {"catalogFingerprint", "chunks",
                                                                  "sourceLastEditSequence", "sourceSeed"})
                          : object(snapshot.payload, "payload", {"catalogFingerprint", "chunks",
                                                                  "fullResync", "sourceLastEditSequence",
                                                                  "sourceSeed"});
    if (!root) return eve::Result<eve::pixelworld::PixelChunkBatch>::failure(root.status());
    auto catalog = decimal(root.value()->at("catalogFingerprint"), "payload.catalogFingerprint");
    auto seed = decimal(root.value()->at("sourceSeed"), "payload.sourceSeed");
    auto sequence = versionOne
                        ? eve::Result<std::uint64_t>::success(0)
                        : decimal(root.value()->at("sourceLastEditSequence"),
                                  "payload.sourceLastEditSequence");
    const auto* records = root.value()->at("chunks").getIf<eve::Value::Array>();
    if (!catalog || !seed || !sequence || !records || records->size() > kMaximumArchiveChunks)
        return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::ParseError,
                                                      "invalid Chunk archive payload", "payload");

    eve::pixelworld::PixelChunkBatch batch;
    batch.catalogFingerprint = catalog.value();
    batch.sourceSeed = seed.value();
    batch.sourceRevision = snapshot.revision.value();
    batch.sourceTick = snapshot.tick;
    batch.sourceLastEditSequence = sequence.value();
    if (!versionOne && !versionTwo) {
        const auto* fullResync = root.value()->at("fullResync").getIf<bool>();
        if (!fullResync)
            return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::ParseError,
                                                          "invalid fullResync flag", "payload.fullResync");
        batch.fullResync = *fullResync;
    }
    std::optional<std::pair<int, int>> previous;
    std::uint64_t decodedTotal = 0;
    for (std::size_t index = 0; index < records->size(); ++index) {
        const std::string path = "payload.chunks[" + std::to_string(index) + "]";
        auto record = object((*records)[index], path,
                             {"codec", "contentHash", "decodedSize", "removed", "revision",
                              "stored", "x", "y"});
        if (!record) return eve::Result<eve::pixelworld::PixelChunkBatch>::failure(record.status());
        auto x = integer(record.value()->at("x"), path + ".x");
        auto y = integer(record.value()->at("y"), path + ".y");
        auto revision = decimal(record.value()->at("revision"), path + ".revision");
        auto decodedSize = decimal(record.value()->at("decodedSize"), path + ".decodedSize");
        const auto* removed = record.value()->at("removed").getIf<bool>();
        auto codecValue = integer(record.value()->at("codec"), path + ".codec");
        const auto* hashText = record.value()->at("contentHash").getIf<std::string>();
        if (!x || !y || !revision || !decodedSize || !removed || !codecValue || !hashText ||
            revision.value() == 0 || revision.value() > batch.sourceRevision ||
            codecValue.value() < 0 || codecValue.value() > int(PixelChunkArchiveCodec::Zstd))
            return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::ParseError,
                                                          "invalid Chunk archive record", path);
        const std::pair<int, int> key{y.value(), x.value()};
        if (previous && !(previous.value() < key))
            return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::ParseError,
                                                          "Chunk records are not canonical", path);
        previous = key;
        const bool currentCells = decodedSize.value() == kDecodedChunkBytes;
        const bool legacyCells = decodedSize.value() == kLegacyDecodedChunkBytes;
        const std::uint64_t expectedSize = *removed ? 0 : decodedSize.value();
        if ((!*removed && !currentCells && !legacyCells) || (*removed && decodedSize.value() != 0) ||
            decodedTotal > kMaximumDecodedBytes - expectedSize)
            return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::ParseError,
                                                          "Chunk decoded size exceeds budget", path);
        decodedTotal += expectedSize;
        auto stored = unhex(record.value()->at("stored"), kDecodedChunkBytes + 1024, path + ".stored");
        if (!stored) return eve::Result<eve::pixelworld::PixelChunkBatch>::failure(stored.status());
        const auto codec = static_cast<PixelChunkArchiveCodec>(codecValue.value());
        if (*removed && (codec != PixelChunkArchiveCodec::None || !stored.value().empty()))
            return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::ParseError,
                                                          "removed Chunk must have empty uncompressed payload", path);
        auto decoded = eve::asset::decompressEvpackChunk(assetCodec(codec), stored.value(),
                                                         expectedSize, kMaximumDecodedBytes);
        if (!decoded) return eve::Result<eve::pixelworld::PixelChunkBatch>::failure(decoded.status());
        const auto expectedHash = eve::ContentId::parse(*hashText);
        auto actualHash = digest(decoded.value(), hashProvider);
        if (!expectedHash || !actualHash || actualHash.value() != *expectedHash)
            return fail<eve::pixelworld::PixelChunkBatch>(eve::DiagnosticCode::HashMismatch,
                                                          "decoded Chunk content hash does not match", path);

        eve::pixelworld::PixelChunkSnapshot chunk;
        chunk.x = x.value();
        chunk.y = y.value();
        chunk.revision = revision.value();
        chunk.removed = *removed;
        if (!*removed) {
            chunk.cells.resize(eve::pixelworld::kPixelChunkSize * eve::pixelworld::kPixelChunkSize);
            std::size_t cursor = 0;
            for (auto& cell : chunk.cells) {
                cell.material = eve::pixelworld::MaterialId(take<std::uint16_t>(decoded.value(), cursor));
                cell.temperature = take<std::int16_t>(decoded.value(), cursor);
                cell.lifetime = take<std::uint8_t>(decoded.value(), cursor);
                if (currentCells)
                    cell.thermalRemainder = take<std::uint16_t>(decoded.value(), cursor);
            }
        }
        batch.chunks.push_back(std::move(chunk));
    }
    return eve::Result<eve::pixelworld::PixelChunkBatch>::success(std::move(batch));
}

}  // namespace eve::pixelworld_streaming
