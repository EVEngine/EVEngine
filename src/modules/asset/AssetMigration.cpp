#include "asset/AssetMigration.h"

#include "data/HashFunction.h"

#include <algorithm>
#include <map>
#include <set>

namespace eve::asset {
namespace {

template <class T>
Result<T> migrationFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.migration"));
}

std::string sha256(std::span<const std::uint8_t> bytes) {
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")
        ->hash("sha256", reinterpret_cast<const char*>(bytes.data()), bytes.size(), digest);
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "sha256:";
    result.reserve(71);
    for (std::size_t index = 0; index < 32; ++index) {
        const auto byte = static_cast<std::uint8_t>(digest.data[index]);
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

Result<Value> migrateImageV1ToV2(const Value& input) {
    const auto* object = input.getIf<Value::Object>();
    if (!object)
        return migrationFailure<Value>(DiagnosticCode::ParseError,
                                       "eve.image definition must be an object");
    const auto schema = object->find("schema");
    const auto version = object->find("schemaVersion");
    const auto colorSpace = object->find("colorSpace");
    if (schema == object->end() || !schema->second.isString() || schema->second.asString() != "eve.image" ||
        version == object->end() || !version->second.isInt64() || version->second.asInt() != 1 ||
        colorSpace == object->end() || !colorSpace->second.isString() ||
        (colorSpace->second.asString() != "srgb" && colorSpace->second.asString() != "linear"))
        return migrationFailure<Value>(DiagnosticCode::ParseError,
                                       "eve.image/1 definition is malformed");
    Value::Object migrated = *object;
    migrated.erase("colorSpace");
    migrated["schemaVersion"] = Value(std::int64_t(2));
    migrated["color"] = Value(Value::Object{
        {"primaries", Value("srgb")},
        {"transfer", Value(colorSpace->second.asString())},
    });
    return Result<Value>::success(Value(std::move(migrated)));
}

Result<Value> migrateDefinition(std::string_view type, SchemaVersion from,
                                SchemaVersion current, const Value& definition) {
    if (from == current) return Result<Value>::success(definition);
    if (from.value() > current.value())
        return migrationFailure<Value>(DiagnosticCode::UnknownVersion,
                                       "asset definition is newer than this reader", std::string(type));
    if (from.value() + 1 != current.value())
        return migrationFailure<Value>(DiagnosticCode::Unsupported,
                                       "asset definition is older than the N-1 compatibility window",
                                       std::string(type));
    if (type == "eve.image" && from.value() == 1 && current.value() == 2)
        return migrateImageV1ToV2(definition);
    return migrationFailure<Value>(DiagnosticCode::Unsupported,
                                   "asset definition has no registered migration", std::string(type));
}

Result<void> refreshImportReport(EvaArchive& archive) {
    const auto path = archive.manifest.provenance.find("path");
    if (path == archive.manifest.provenance.end()) return Result<void>::success();
    if (!path->second.isString() || path->second.asString() != "reports/import.json")
        return migrationFailure<void>(DiagnosticCode::ParseError,
                                      "migration found an invalid import report path");
    const auto entry = std::lower_bound(archive.entries.begin(), archive.entries.end(),
                                        path->second.asString(),
                                        [](const EvaArchiveEntry& candidate, std::string_view value) {
                                            return candidate.path < value;
                                        });
    if (entry == archive.entries.end() || entry->path != path->second.asString())
        return migrationFailure<void>(DiagnosticCode::NotFound,
                                      "migration import report is missing", path->second.asString());
    auto parsed = Value::fromJson(std::string(entry->bytes.begin(), entry->bytes.end()));
    if (!parsed) return Result<void>::failure(parsed.status());
    auto* report = parsed.value().getIf<Value::Object>();
    if (!report)
        return migrationFailure<void>(DiagnosticCode::ParseError,
                                      "migration import report root is invalid", entry->path);
    Value::Array assets;
    for (const auto& asset : archive.manifest.assets)
        assets.emplace_back(Value::Object{{"asset", Value(asset.asset.format())},
                                          {"type", Value(asset.type + "/" +
                                                         std::to_string(asset.schemaVersion.value()))},
                                          {"contentHash", Value(asset.contentHash)}});
    (*report)["canonicalAssets"] = Value(std::move(assets));
    report->erase("importKey");
    auto facts = parsed.value().toJson();
    if (!facts) return Result<void>::failure(facts.status());
    const std::string key = sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(facts.value().data()), facts.value().size()));
    (*report)["importKey"] = Value(key);
    auto encoded = parsed.value().toJson();
    if (!encoded) return Result<void>::failure(encoded.status());
    entry->bytes.assign(encoded.value().begin(), encoded.value().end());
    archive.manifest.provenance["importKey"] = Value(key);
    return Result<void>::success();
}

}  // namespace

Result<SchemaVersion> currentAssetSchemaVersion(std::string_view type) {
    static const std::map<std::string_view, std::uint64_t> versions = {
        {"eve.image", 2},          {"eve.texture", 1},       {"eve.mesh", 1},
        {"eve.skeleton", 1},       {"eve.animation-clip", 1},{"eve.material", 1},
        {"eve.scene-template", 1}, {"eve.terrain", 1},       {"eve.terrain-material", 1},
        {"eve.pcg-graph", 1},      {"eve.instance-set", 1},  {"eve.audio", 1},
        {"eve.font", 1},
    };
    const auto found = versions.find(type);
    if (found == versions.end())
        return migrationFailure<SchemaVersion>(DiagnosticCode::Unsupported,
                                               "unknown canonical asset type", std::string(type));
    return Result<SchemaVersion>::success(SchemaVersion(found->second));
}

Result<EvaArchive> migrateEvaArchive(EvaArchive source, const EvaArchiveLimits& limits) {
    for (auto& asset : source.manifest.assets) {
        auto current = currentAssetSchemaVersion(asset.type);
        if (!current) return Result<EvaArchive>::failure(current.status());
        if (asset.schemaVersion.value() > current.value().value())
            return migrationFailure<EvaArchive>(DiagnosticCode::UnknownVersion,
                                                "asset definition is newer than this reader", asset.type);
        const auto entry = std::lower_bound(source.entries.begin(), source.entries.end(), asset.definition,
                                            [](const EvaArchiveEntry& candidate, std::string_view path) {
                                                return candidate.path < path;
                                            });
        if (entry == source.entries.end() || entry->path != asset.definition)
            return migrationFailure<EvaArchive>(DiagnosticCode::NotFound,
                                                "asset definition is missing", asset.definition);
        const std::string_view text(reinterpret_cast<const char*>(entry->bytes.data()), entry->bytes.size());
        auto parsed = Value::fromJson(text);
        if (!parsed) return Result<EvaArchive>::failure(parsed.status());
        auto migrated = migrateDefinition(asset.type, asset.schemaVersion, current.value(), parsed.value());
        if (!migrated) return Result<EvaArchive>::failure(migrated.status());
        auto encoded = migrated.value().toJson();
        if (!encoded) return Result<EvaArchive>::failure(encoded.status());
        entry->bytes.assign(encoded.value().begin(), encoded.value().end());
        asset.schemaVersion = current.value();
        asset.contentHash = sha256(entry->bytes);
    }
    auto report = refreshImportReport(source);
    if (!report) return Result<EvaArchive>::failure(report.status());
    auto rebuilt = buildEvaArchive(source.manifest, std::move(source.entries), limits);
    if (!rebuilt) return Result<EvaArchive>::failure(rebuilt.status());
    return parseEvaArchive(rebuilt.value(), limits);
}

}  // namespace eve::asset
