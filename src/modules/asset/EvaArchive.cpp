#include "asset/EvaArchive.h"

#include "data/HashFunction.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include <zlib.h>
#include <utf8proc.h>

namespace eve::asset {
namespace {

constexpr std::uint32_t kLocalSignature       = 0x04034b50;
constexpr std::uint32_t kCentralSignature     = 0x02014b50;
constexpr std::uint32_t kZip64EndSignature    = 0x06064b50;
constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50;
constexpr std::uint32_t kEndSignature         = 0x06054b50;
constexpr std::uint16_t kUtf8Flag             = 0x0800;

template <class T>
Result<T> archiveFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.eva.archive"));
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift != 64; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}

bool rangeFits(std::size_t offset, std::size_t size, std::size_t extent) {
    return offset <= extent && size <= extent - offset;
}

bool get16(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint16_t& value) {
    if (!rangeFits(offset, 2, bytes.size())) return false;
    value = static_cast<std::uint16_t>(bytes[offset] | (std::uint16_t(bytes[offset + 1]) << 8));
    return true;
}
bool get32(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint32_t& value) {
    if (!rangeFits(offset, 4, bytes.size())) return false;
    value = 0;
    for (unsigned index = 0; index != 4; ++index) value |= std::uint32_t(bytes[offset + index]) << (index * 8);
    return true;
}
bool get64(std::span<const std::uint8_t> bytes, std::size_t offset, std::uint64_t& value) {
    if (!rangeFits(offset, 8, bytes.size())) return false;
    value = 0;
    for (unsigned index = 0; index != 8; ++index) value |= std::uint64_t(bytes[offset + index]) << (index * 8);
    return true;
}

std::uint32_t crcOf(std::span<const std::uint8_t> bytes) {
    uLong checksum = crc32(0, Z_NULL, 0);
    while (!bytes.empty()) {
        const auto count = static_cast<uInt>(std::min<std::size_t>(bytes.size(), std::numeric_limits<uInt>::max()));
        checksum = crc32(checksum, bytes.data(), count);
        bytes = bytes.subspan(count);
    }
    return static_cast<std::uint32_t>(checksum);
}

std::string sha256Text(std::span<const std::uint8_t> bytes) {
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")
        ->hash("sha256", reinterpret_cast<const char*>(bytes.data()), bytes.size(), digest);
    static constexpr char digits[] = "0123456789abcdef";
    std::string result = "sha256:";
    result.reserve(71);
    for (std::size_t index = 0; index < 32; ++index) {
        const auto byte = static_cast<unsigned char>(digest.data[index]);
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

bool validPath(std::string_view path, const EvaArchiveLimits& limits) {
    if (path.empty() || path.size() > limits.maximumPathBytes ||
        path.size() > std::numeric_limits<std::uint16_t>::max() || path.front() == '/' || path.back() == '/')
        return false;
    std::size_t segmentStart = 0;
    for (std::size_t index = 0; index <= path.size(); ++index) {
        if (index < path.size()) {
            const unsigned char byte = static_cast<unsigned char>(path[index]);
            if (byte == '\\' || byte == 0 || byte < 0x20 || byte == 0x7f) return false;
            if (byte != '/') continue;
        }
        const std::string_view segment = path.substr(segmentStart, index - segmentStart);
        if (segment.empty() || segment == "." || segment == "..") return false;
        segmentStart = index + 1;
    }
    for (std::size_t index = 0; index < path.size();) {
        const unsigned char lead = static_cast<unsigned char>(path[index]);
        if (lead < 0x80) { ++index; continue; }
        std::size_t continuation = 0;
        std::uint32_t codepoint = 0;
        if ((lead & 0xe0) == 0xc0) { continuation = 1; codepoint = lead & 0x1f; }
        else if ((lead & 0xf0) == 0xe0) { continuation = 2; codepoint = lead & 0x0f; }
        else if ((lead & 0xf8) == 0xf0) { continuation = 3; codepoint = lead & 0x07; }
        else return false;
        if (continuation >= path.size() - index) return false;
        for (std::size_t part = 1; part <= continuation; ++part) {
            const unsigned char byte = static_cast<unsigned char>(path[index + part]);
            if ((byte & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((continuation == 1 && codepoint < 0x80) || (continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        index += continuation + 1;
    }
    utf8proc_uint8_t* normalized = nullptr;
    const auto normalizedSize = utf8proc_map(
        reinterpret_cast<const utf8proc_uint8_t*>(path.data()),
        static_cast<utf8proc_ssize_t>(path.size()), &normalized,
        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE));
    if (normalizedSize < 0 || !normalized) return false;
    const bool canonical = static_cast<std::size_t>(normalizedSize) == path.size() &&
                           std::equal(path.begin(), path.end(),
                                      reinterpret_cast<const char*>(normalized));
    std::free(normalized);
    return canonical;
}

std::string unicodeCaseFold(std::string_view path) {
    utf8proc_uint8_t* folded = nullptr;
    const auto foldedSize = utf8proc_map(
        reinterpret_cast<const utf8proc_uint8_t*>(path.data()),
        static_cast<utf8proc_ssize_t>(path.size()), &folded,
        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD));
    if (foldedSize < 0 || !folded) return {};
    std::string result(reinterpret_cast<const char*>(folded),
                       static_cast<std::size_t>(foldedSize));
    std::free(folded);
    return result;
}

struct CentralRecord {
    std::string   path;
    std::uint32_t crc = 0;
    std::uint64_t decodedSize = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t localOffset = 0;
    std::uint16_t method = 0;
};

const Value* objectField(const Value::Object& object, std::string_view name);

std::vector<std::uint8_t> deflateRaw(std::span<const std::uint8_t> input) {
    if (input.empty() || input.size() > std::numeric_limits<uInt>::max()) return {};
    z_stream stream{};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    const uLong bound = deflateBound(&stream, static_cast<uLong>(input.size()));
    if (bound > std::numeric_limits<uInt>::max()) {
        deflateEnd(&stream);
        return {};
    }
    std::vector<std::uint8_t> output(static_cast<std::size_t>(bound));
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());
    const int status = deflate(&stream, Z_FINISH);
    const std::size_t size = stream.total_out;
    deflateEnd(&stream);
    if (status != Z_STREAM_END || size >= input.size()) return {};
    output.resize(size);
    return output;
}

Result<std::vector<std::uint8_t>> inflateRaw(std::span<const std::uint8_t> input,
                                             std::uint64_t decodedSize) {
    if (input.size() > std::numeric_limits<uInt>::max() ||
        decodedSize > std::numeric_limits<uInt>::max())
        return archiveFailure<std::vector<std::uint8_t>>(
            DiagnosticCode::InvalidArgument, "Deflate entry exceeds decoder limits");
    std::vector<std::uint8_t> output(static_cast<std::size_t>(decodedSize));
    std::uint8_t emptyOutput = 0;
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.empty() ? &emptyOutput : output.data();
    stream.avail_out = output.empty() ? 1u : static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::Failed,
                                                         "Deflate decoder initialization failed");
    const int status = inflate(&stream, Z_FINISH);
    const bool exact = status == Z_STREAM_END && stream.total_in == input.size() &&
                       stream.total_out == decodedSize;
    inflateEnd(&stream);
    if (!exact)
        return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                         "Deflate payload is malformed");
    return Result<std::vector<std::uint8_t>>::success(std::move(output));
}

Result<void> validateAssetDefinitions(const EvaManifest& manifest,
                                      const std::vector<EvaArchiveEntry>& entries,
                                      const EvaArchiveLimits& limits) {
    for (const auto& asset : manifest.assets) {
        if (!validPath(asset.definition, limits) || asset.definition == "manifest.json")
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "asset definition path is not canonical",
                                                           asset.definition, {}, "asset.eva.archive"));
        const auto found = std::lower_bound(entries.begin(), entries.end(), asset.definition,
                                            [](const EvaArchiveEntry& entry, std::string_view path) {
                                                return entry.path < path;
                                            });
        if (found == entries.end() || found->path != asset.definition)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                           "asset definition entry is missing",
                                                           asset.definition, {}, "asset.eva.archive"));
        if (sha256Text(found->bytes) != asset.contentHash)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::HashMismatch,
                                                           "asset definition content hash does not match",
                                                           asset.definition, {}, "asset.eva.archive"));
        if (found->bytes.size() > limits.maximumManifestBytes)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "asset definition exceeds metadata budget",
                asset.definition, {}, "asset.eva.archive"));
        auto parsed = Value::fromJson(std::string_view(
            reinterpret_cast<const char*>(found->bytes.data()), found->bytes.size()));
        if (!parsed) return Result<void>::failure(parsed.status());
        const auto* object = parsed.value().getIf<Value::Object>();
        const Value* schema = object ? objectField(*object, "schema") : nullptr;
        const Value* version = object ? objectField(*object, "schemaVersion") : nullptr;
        if (!schema || !schema->isString() || schema->asString() != asset.type ||
            !version || !version->isInt64() || version->asInt() <= 0 ||
            static_cast<std::uint64_t>(version->asInt()) != asset.schemaVersion.value())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::TypeMismatch,
                "asset definition schema identity does not match manifest", asset.definition,
                {}, "asset.eva.archive"));
    }
    return Result<void>::success();
}

Result<void> validateContentAddressedBlobs(const std::vector<EvaArchiveEntry>& entries) {
    constexpr std::string_view prefix = "blobs/sha256/";
    for (const auto& entry : entries) {
        if (!entry.path.starts_with(prefix)) continue;
        const std::string_view digest = std::string_view(entry.path).substr(prefix.size());
        if (digest.size() != 64 ||
            !std::all_of(digest.begin(), digest.end(), [](unsigned char character) {
                return (character >= '0' && character <= '9') ||
                       (character >= 'a' && character <= 'f');
            }) || sha256Text(entry.bytes) != "sha256:" + std::string(digest))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::HashMismatch,
                "content-addressed blob path does not match its bytes", entry.path, {},
                "asset.eva.archive"));
    }
    return Result<void>::success();
}

const Value* objectField(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<void> validateImportReport(const EvaManifest& manifest,
                                  const std::vector<EvaArchiveEntry>& entries,
                                  const EvaArchiveLimits& limits) {
    const Value* pathValue = objectField(manifest.provenance, "path");
    if (!pathValue) return Result<void>::success();
    if (!pathValue->isString() || pathValue->asString() != "reports/import.json")
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "provenance report path must be reports/import.json",
            "$.provenance.path", {}, "asset.eva.archive"));
    const auto found = std::lower_bound(entries.begin(), entries.end(), pathValue->asString(),
                                        [](const EvaArchiveEntry& entry, std::string_view path) {
                                            return entry.path < path;
                                        });
    if (found == entries.end() || found->path != pathValue->asString())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "manifest provenance report is missing", pathValue->asString(), {},
            "asset.eva.archive"));
    if (found->bytes.size() > limits.maximumManifestBytes)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "import report exceeds admission budget", found->path, {},
            "asset.eva.archive"));
    auto parsed = Value::fromJson(std::string(found->bytes.begin(), found->bytes.end()));
    if (!parsed) return Result<void>::failure(parsed.status());
    const auto* report = parsed.value().getIf<Value::Object>();
    const Value* schema = report ? objectField(*report, "schema") : nullptr;
    const Value* version = report ? objectField(*report, "schemaVersion") : nullptr;
    const Value* importKey = report ? objectField(*report, "importKey") : nullptr;
    const Value* mappings = report ? objectField(*report, "sourceObjects") : nullptr;
    const auto* mappingArray = mappings ? mappings->getIf<Value::Array>() : nullptr;
    const Value* packageName = report ? objectField(*report, "packageName") : nullptr;
    const Value* packageVersion = report ? objectField(*report, "packageVersion") : nullptr;
    const Value* canonicalAssets = report ? objectField(*report, "canonicalAssets") : nullptr;
    const auto* canonicalAssetArray = canonicalAssets ? canonicalAssets->getIf<Value::Array>() : nullptr;
    const Value* findings = report ? objectField(*report, "findings") : nullptr;
    const auto* findingArray = findings ? findings->getIf<Value::Array>() : nullptr;
    const Value* counts = report ? objectField(*report, "counts") : nullptr;
    const auto* countObject = counts ? counts->getIf<Value::Object>() : nullptr;
    const Value* manifestKey = objectField(manifest.provenance, "importKey");
    if (!schema || !schema->isString() || schema->asString() != "eve.asset-import-report" ||
        !version || !version->isInt64() || version->asInt() != 1 || !importKey ||
        !importKey->isString() || !manifestKey || !manifestKey->isString() ||
        importKey->asString() != manifestKey->asString() || !mappingArray || !packageName ||
        !packageName->isString() || packageName->asString() != manifest.packageName ||
        !packageVersion || !packageVersion->isString() ||
        packageVersion->asString() != manifest.packageVersion || !canonicalAssetArray ||
        !findingArray || !countObject)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "import report envelope or import key is invalid", found->path, {},
            "asset.eva.archive"));
    Value::Object keyFacts = *report;
    keyFacts.erase("importKey");
    auto canonicalFacts = Value(std::move(keyFacts)).toJson();
    if (!canonicalFacts) return Result<void>::failure(canonicalFacts.status());
    const std::string computedKey = sha256Text(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(canonicalFacts.value().data()),
        canonicalFacts.value().size()));
    if (computedKey != importKey->asString())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::HashMismatch, "import report key does not match canonical facts", found->path, {},
            "asset.eva.archive"));
    std::set<PersistentId> local;
    for (const auto& asset : manifest.assets) local.emplace(asset.asset.id());
    std::set<std::pair<std::string, PersistentId>> uniqueMappings;
    for (std::size_t index = 0; index < mappingArray->size(); ++index) {
        const auto* mapping = (*mappingArray)[index].getIf<Value::Object>();
        const Value* source = mapping ? objectField(*mapping, "sourceObject") : nullptr;
        const Value* asset = mapping ? objectField(*mapping, "asset") : nullptr;
        if (!source || !source->isString() || source->asString().empty() || !asset ||
            !asset->isString())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "import source mapping is malformed", found->path, {},
                "asset.eva.archive"));
        auto reference = AssetRef::parse(asset->asString());
        if (!reference || !local.contains(reference.value().id()) ||
            !uniqueMappings.emplace(source->asString(), reference.value().id()).second)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "import source mapping is duplicate or targets a missing asset", asset->asString(), {},
                "asset.eva.archive"));
    }
    using AssetFact = std::tuple<PersistentId, std::string, std::string>;
    std::set<AssetFact> expectedAssets;
    for (const auto& asset : manifest.assets)
        expectedAssets.emplace(asset.asset.id(),
                               asset.type + "/" + std::to_string(asset.schemaVersion.value()),
                               asset.contentHash);
    std::set<AssetFact> reportedAssets;
    for (std::size_t index = 0; index < canonicalAssetArray->size(); ++index) {
        const auto* fact = (*canonicalAssetArray)[index].getIf<Value::Object>();
        const Value* asset = fact ? objectField(*fact, "asset") : nullptr;
        const Value* type = fact ? objectField(*fact, "type") : nullptr;
        const Value* hash = fact ? objectField(*fact, "contentHash") : nullptr;
        if (!asset || !asset->isString() || !type || !type->isString() || !hash ||
            !hash->isString())
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "import canonical asset fact is malformed",
                found->path, {}, "asset.eva.archive"));
        auto reference = AssetRef::parse(asset->asString());
        if (!reference ||
            !reportedAssets.emplace(reference.value().id(), type->asString(), hash->asString()).second)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "import canonical asset fact is duplicate or invalid",
                asset->asString(), {}, "asset.eva.archive"));
    }
    if (reportedAssets != expectedAssets)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::HashMismatch,
            "import canonical asset facts do not match the admitted manifest", found->path, {},
            "asset.eva.archive"));
    static constexpr std::array<std::string_view, 4> dispositions = {
        "translated", "baked", "preserved-source", "unsupported"};
    std::map<std::string, std::int64_t> computedCounts;
    for (const auto name : dispositions) computedCounts.emplace(name, 0);
    for (const auto& finding : *findingArray) {
        const auto* fact = finding.getIf<Value::Object>();
        const Value* disposition = fact ? objectField(*fact, "disposition") : nullptr;
        if (!disposition || !disposition->isString() ||
            !computedCounts.contains(disposition->asString()))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "import finding disposition is invalid",
                found->path, {}, "asset.eva.archive"));
        ++computedCounts[disposition->asString()];
    }
    if (countObject->size() != dispositions.size())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "import finding counts are incomplete",
            found->path, {}, "asset.eva.archive"));
    for (const auto name : dispositions) {
        const Value* count = objectField(*countObject, name);
        if (!count || !count->isInt64() || count->asInt() != computedCounts.at(std::string(name)))
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::HashMismatch, "import finding count does not match findings",
                std::string(name), {}, "asset.eva.archive"));
    }
    return Result<void>::success();
}

}  // namespace

Result<std::vector<std::uint8_t>> buildEvaArchive(const EvaManifest& manifest,
                                                   std::vector<EvaArchiveEntry> entries,
                                                   const EvaArchiveLimits& limits) {
    auto manifestJson = serializeEvaManifest(manifest);
    if (!manifestJson) return Result<std::vector<std::uint8_t>>::failure(manifestJson.status());
    std::string manifestText = std::move(manifestJson).takeValue();
    auto validatedManifest = parseEvaManifest(manifestText);
    if (!validatedManifest) return Result<std::vector<std::uint8_t>>::failure(validatedManifest.status());
    if (manifestText.size() > limits.maximumManifestBytes)
        return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                         "manifest exceeds admission budget", "manifest.json");
    entries.push_back({"manifest.json", {manifestText.begin(), manifestText.end()}});
    if (entries.size() > limits.maximumEntries)
        return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                         "archive has too many entries");
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.path < right.path;
    });
    std::uint64_t total = 0;
    std::string previous;
    std::set<std::string> foldedPaths;
    for (const auto& entry : entries) {
        if (!validPath(entry.path, limits))
            return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                             "entry path is not canonical", entry.path);
        if (entry.path == previous)
            return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::Conflict,
                                                             "duplicate archive entry", entry.path);
        previous = entry.path;
        const std::string folded = unicodeCaseFold(entry.path);
        if (folded.empty())
            return archiveFailure<std::vector<std::uint8_t>>(
                DiagnosticCode::InvalidArgument, "entry path cannot be case-folded", entry.path);
        if (!foldedPaths.emplace(folded).second)
            return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::Conflict,
                                                             "case-colliding archive entry", entry.path);
        if (entry.bytes.size() > limits.maximumEntryBytes || entry.bytes.size() > limits.maximumDecodedBytes ||
            total > limits.maximumDecodedBytes - entry.bytes.size())
            return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                             "decoded entry budget exceeded", entry.path);
        total += entry.bytes.size();
    }
    auto definitions = validateAssetDefinitions(validatedManifest.value(), entries, limits);
    if (!definitions) return Result<std::vector<std::uint8_t>>::failure(definitions.status());
    auto blobs = validateContentAddressedBlobs(entries);
    if (!blobs) return Result<std::vector<std::uint8_t>>::failure(blobs.status());
    auto report = validateImportReport(validatedManifest.value(), entries, limits);
    if (!report) return Result<std::vector<std::uint8_t>>::failure(report.status());

    std::vector<std::uint8_t> out;
    std::vector<CentralRecord> records;
    for (const auto& entry : entries) {
        auto compressed = deflateRaw(entry.bytes);
        const bool useDeflate = !compressed.empty();
        const auto payload = useDeflate ? std::span<const std::uint8_t>(compressed)
                                        : std::span<const std::uint8_t>(entry.bytes);
        CentralRecord record{entry.path, crcOf(entry.bytes), entry.bytes.size(), payload.size(),
                             out.size(), static_cast<std::uint16_t>(useDeflate ? 8 : 0)};
        put32(out, kLocalSignature); put16(out, 45); put16(out, kUtf8Flag); put16(out, record.method);
        put16(out, 0); put16(out, 0x21); put32(out, record.crc);
        put32(out, 0xffffffff); put32(out, 0xffffffff);
        put16(out, static_cast<std::uint16_t>(entry.path.size())); put16(out, 20);
        out.insert(out.end(), entry.path.begin(), entry.path.end());
        put16(out, 0x0001); put16(out, 16); put64(out, record.decodedSize);
        put64(out, record.compressedSize);
        out.insert(out.end(), payload.begin(), payload.end());
        records.push_back(std::move(record));
    }
    const std::uint64_t centralOffset = out.size();
    for (const auto& record : records) {
        put32(out, kCentralSignature); put16(out, 45); put16(out, 45); put16(out, kUtf8Flag);
        put16(out, record.method);
        put16(out, 0); put16(out, 0x21); put32(out, record.crc);
        put32(out, 0xffffffff); put32(out, 0xffffffff);
        put16(out, static_cast<std::uint16_t>(record.path.size())); put16(out, 28); put16(out, 0);
        put16(out, 0); put16(out, 0); put32(out, 0); put32(out, 0xffffffff);
        out.insert(out.end(), record.path.begin(), record.path.end());
        put16(out, 0x0001); put16(out, 24); put64(out, record.decodedSize);
        put64(out, record.compressedSize);
        put64(out, record.localOffset);
    }
    const std::uint64_t centralSize = out.size() - centralOffset;
    const std::uint64_t zip64EndOffset = out.size();
    put32(out, kZip64EndSignature); put64(out, 44); put16(out, 45); put16(out, 45);
    put32(out, 0); put32(out, 0); put64(out, records.size()); put64(out, records.size());
    put64(out, centralSize); put64(out, centralOffset);
    put32(out, kZip64LocatorSignature); put32(out, 0); put64(out, zip64EndOffset); put32(out, 1);
    put32(out, kEndSignature); put16(out, 0); put16(out, 0); put16(out, 0xffff); put16(out, 0xffff);
    put32(out, 0xffffffff); put32(out, 0xffffffff); put16(out, 0);
    if (out.size() > limits.maximumArchiveBytes)
        return archiveFailure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                         "encoded archive exceeds admission budget");
    return Result<std::vector<std::uint8_t>>::success(std::move(out));
}

Result<EvaArchive> parseEvaArchive(std::span<const std::uint8_t> bytes, const EvaArchiveLimits& limits) {
    if (bytes.size() > limits.maximumArchiveBytes || bytes.size() < 98)
        return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "archive size is outside admission limits");
    std::uint32_t signature = 0;
    std::uint16_t comment = 0;
    std::size_t eocd = std::numeric_limits<std::size_t>::max();
    const std::size_t earliest = bytes.size() > 22 + std::numeric_limits<std::uint16_t>::max()
                                     ? bytes.size() - 22 - std::numeric_limits<std::uint16_t>::max()
                                     : 0;
    for (std::size_t candidate = bytes.size() - 22;; --candidate) {
        if (get32(bytes, candidate, signature) && signature == kEndSignature &&
            get16(bytes, candidate + 20, comment) &&
            std::size_t(comment) == bytes.size() - candidate - 22) {
            eocd = candidate;
            break;
        }
        if (candidate == earliest) break;
    }
    if (eocd == std::numeric_limits<std::size_t>::max() || eocd < 20)
        return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "canonical ZIP end record is missing");
    const std::size_t locator = eocd - 20;
    std::uint64_t zip64Offset = 0;
    if (!get32(bytes, locator, signature) || signature != kZip64LocatorSignature ||
        !get64(bytes, locator + 8, zip64Offset) || zip64Offset > locator)
        return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "ZIP64 locator is invalid");
    std::uint64_t entryCount = 0, centralSize = 0, centralOffset = 0;
    if (!get32(bytes, static_cast<std::size_t>(zip64Offset), signature) || signature != kZip64EndSignature ||
        !get64(bytes, static_cast<std::size_t>(zip64Offset) + 32, entryCount) ||
        !get64(bytes, static_cast<std::size_t>(zip64Offset) + 40, centralSize) ||
        !get64(bytes, static_cast<std::size_t>(zip64Offset) + 48, centralOffset) ||
        entryCount > limits.maximumEntries || centralOffset > bytes.size() ||
        centralSize > bytes.size() - centralOffset || centralOffset + centralSize != zip64Offset)
        return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "ZIP64 central directory is invalid");

    std::vector<CentralRecord> records;
    std::set<std::string> names;
    std::set<std::string> foldedNames;
    std::uint64_t decodedTotal = 0;
    std::size_t cursor = static_cast<std::size_t>(centralOffset);
    for (std::uint64_t index = 0; index < entryCount; ++index) {
        std::uint16_t flags = 0, method = 0, nameSize = 0, extraSize = 0, commentSize = 0, disk = 0;
        std::uint32_t crc = 0, external = 0;
        if (!get32(bytes, cursor, signature) || signature != kCentralSignature ||
            !get16(bytes, cursor + 8, flags) || !get16(bytes, cursor + 10, method) ||
            !get32(bytes, cursor + 16, crc) || !get16(bytes, cursor + 28, nameSize) ||
            !get16(bytes, cursor + 30, extraSize) || !get16(bytes, cursor + 32, commentSize) ||
            !get16(bytes, cursor + 34, disk) || !get32(bytes, cursor + 38, external) ||
            flags != kUtf8Flag || (method != 0 && method != 8) || commentSize != 0 || disk != 0 || external != 0 ||
            !rangeFits(cursor + 46, std::size_t(nameSize) + extraSize, bytes.size()))
            return archiveFailure<EvaArchive>(DiagnosticCode::Unsupported,
                                              "unsupported or malformed ZIP entry");
        std::string path(reinterpret_cast<const char*>(bytes.data() + cursor + 46), nameSize);
        const std::string folded = unicodeCaseFold(path);
        if (!validPath(path, limits) || !names.emplace(path).second || folded.empty() ||
            !foldedNames.emplace(folded).second)
            return archiveFailure<EvaArchive>(DiagnosticCode::InvalidArgument,
                                              "duplicate or non-canonical entry path", path);
        const std::size_t extra = cursor + 46 + nameSize;
        std::uint16_t extraId = 0, zip64Size = 0;
        std::uint64_t decodedSize = 0, compressedSize = 0, localOffset = 0;
        if (extraSize != 28 || !get16(bytes, extra, extraId) || extraId != 1 ||
            !get16(bytes, extra + 2, zip64Size) || zip64Size != 24 ||
            !get64(bytes, extra + 4, decodedSize) || !get64(bytes, extra + 12, compressedSize) ||
            !get64(bytes, extra + 20, localOffset) ||
            localOffset >= centralOffset ||
            decodedSize > limits.maximumEntryBytes || decodedSize > limits.maximumDecodedBytes ||
            decodedTotal > limits.maximumDecodedBytes - decodedSize)
            return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "ZIP64 entry sizes are invalid", path);
        decodedTotal += decodedSize;
        records.push_back({std::move(path), crc, decodedSize, compressedSize, localOffset, method});
        cursor += 46 + nameSize + extraSize;
    }
    if (cursor != zip64Offset)
        return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "central directory length mismatch");

    EvaArchive archive;
    for (const auto& record : records) {
        const auto local = static_cast<std::size_t>(record.localOffset);
        std::uint16_t flags = 0, method = 0, nameSize = 0, extraSize = 0;
        if (!get32(bytes, local, signature) || signature != kLocalSignature ||
            !get16(bytes, local + 6, flags) || !get16(bytes, local + 8, method) ||
            !get16(bytes, local + 26, nameSize) || !get16(bytes, local + 28, extraSize) ||
            flags != kUtf8Flag || method != record.method || (method != 0 && method != 8) || extraSize != 20 ||
            !rangeFits(local + 30, std::size_t(nameSize) + extraSize, bytes.size()))
            return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "local ZIP header is invalid", record.path);
        const std::string localName(reinterpret_cast<const char*>(bytes.data() + local + 30), nameSize);
        std::uint16_t extraId = 0, zip64Size = 0;
        std::uint64_t decodedSize = 0, compressedSize = 0;
        const std::size_t extra = local + 30 + nameSize;
        if (localName != record.path || !get16(bytes, extra, extraId) || extraId != 1 ||
            !get16(bytes, extra + 2, zip64Size) || zip64Size != 16 ||
            !get64(bytes, extra + 4, decodedSize) || !get64(bytes, extra + 12, compressedSize) ||
            decodedSize != record.decodedSize || compressedSize != record.compressedSize)
            return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "local and central ZIP records disagree",
                                              record.path);
        const std::size_t payload = extra + extraSize;
        if (record.compressedSize > bytes.size() ||
            !rangeFits(payload, static_cast<std::size_t>(record.compressedSize), bytes.size()) ||
            payload + static_cast<std::size_t>(record.compressedSize) > centralOffset)
            return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "entry payload is truncated", record.path);
        auto payloadBytes = bytes.subspan(payload, static_cast<std::size_t>(record.compressedSize));
        std::vector<std::uint8_t> decoded;
        if (record.method == 0) {
            if (record.compressedSize != record.decodedSize)
                return archiveFailure<EvaArchive>(DiagnosticCode::ParseError,
                                                  "stored entry sizes disagree", record.path);
            decoded.assign(payloadBytes.begin(), payloadBytes.end());
        } else {
            auto inflated = inflateRaw(payloadBytes, record.decodedSize);
            if (!inflated) return Result<EvaArchive>::failure(inflated.status());
            decoded = std::move(inflated).takeValue();
        }
        if (crcOf(decoded) != record.crc)
            return archiveFailure<EvaArchive>(DiagnosticCode::HashMismatch, "entry CRC does not match", record.path);
        EvaArchiveEntry entry{record.path, std::move(decoded)};
        if (record.path == "manifest.json") {
            if (record.decodedSize > limits.maximumManifestBytes)
                return archiveFailure<EvaArchive>(DiagnosticCode::InvalidArgument, "manifest exceeds budget",
                                                  record.path);
            std::string json(entry.bytes.begin(), entry.bytes.end());
            auto manifest = parseEvaManifest(json);
            if (!manifest) return Result<EvaArchive>::failure(manifest.status());
            archive.manifest = std::move(manifest).takeValue();
        } else {
            archive.entries.push_back(std::move(entry));
        }
    }
    if (!names.contains("manifest.json"))
        return archiveFailure<EvaArchive>(DiagnosticCode::ParseError, "root manifest.json is required");
    std::sort(archive.entries.begin(), archive.entries.end(), [](const auto& left, const auto& right) {
        return left.path < right.path;
    });
    auto definitions = validateAssetDefinitions(archive.manifest, archive.entries, limits);
    if (!definitions) return Result<EvaArchive>::failure(definitions.status());
    auto blobs = validateContentAddressedBlobs(archive.entries);
    if (!blobs) return Result<EvaArchive>::failure(blobs.status());
    auto report = validateImportReport(archive.manifest, archive.entries, limits);
    if (!report) return Result<EvaArchive>::failure(report.status());
    return Result<EvaArchive>::success(std::move(archive));
}

}  // namespace eve::asset
