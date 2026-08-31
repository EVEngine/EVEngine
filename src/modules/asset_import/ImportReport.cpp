#include "asset_import/ImportCommon.h"

#include <algorithm>
#include <tuple>

namespace eve::asset_import::detail {
namespace {

std::string dispositionName(ImportDisposition disposition) {
    switch (disposition) {
    case ImportDisposition::Translated: return "translated";
    case ImportDisposition::Baked: return "baked";
    case ImportDisposition::PreservedSource: return "preserved-source";
    case ImportDisposition::Unsupported: return "unsupported";
    }
    return "unsupported";
}

}  // namespace

Result<void> finalizeImportReport(PreparedAssetImport& prepared,
                                  const ImportPackageIdentity& package,
                                  std::string_view sourceEngine,
                                  std::string_view sourceVersion,
                                  Value::Object options) {
    if (sourceEngine.empty() || prepared.manifest.assets.empty())
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "import report requires source engine and canonical assets");
    std::sort(prepared.sourceMappings.begin(), prepared.sourceMappings.end(),
              [](const auto& left, const auto& right) {
                  if (left.sourceObject != right.sourceObject)
                      return left.sourceObject < right.sourceObject;
                  return left.asset.id() < right.asset.id();
              });
    for (std::size_t index = 1; index < prepared.sourceMappings.size(); ++index) {
        if (prepared.sourceMappings[index - 1].sourceObject ==
                prepared.sourceMappings[index].sourceObject &&
            prepared.sourceMappings[index - 1].asset == prepared.sourceMappings[index].asset)
            return failure<void>(DiagnosticCode::AlreadyExists,
                                 "duplicate import source mapping",
                                 prepared.sourceMappings[index].sourceObject);
    }
    std::sort(prepared.findings.begin(), prepared.findings.end(), [](const auto& left, const auto& right) {
        return std::tie(left.sourcePath, left.feature, left.disposition, left.message) <
               std::tie(right.sourcePath, right.feature, right.disposition, right.message);
    });

    Value::Object report;
    report["schema"] = Value("eve.asset-import-report");
    report["schemaVersion"] = Value(std::int64_t(1));
    report["sourceEngine"] = Value(std::string(sourceEngine));
    report["sourceVersion"] = Value(std::string(sourceVersion));
    report["packageName"] = Value(package.packageName);
    report["packageVersion"] = Value(package.packageVersion);
    report["importer"] = prepared.manifest.provenance.contains("importer")
                             ? prepared.manifest.provenance.at("importer")
                             : Value("unknown");
    report["importerVersion"] = prepared.manifest.provenance.contains("importerVersion")
                                    ? prepared.manifest.provenance.at("importerVersion")
                                    : Value(std::int64_t(0));
    report["options"] = Value(std::move(options));

    Value::Array mappings;
    for (const auto& mapping : prepared.sourceMappings)
        mappings.emplace_back(Value::Object{{"sourceObject", Value(mapping.sourceObject)},
                                            {"asset", Value(mapping.asset.format())}});
    report["sourceObjects"] = Value(std::move(mappings));

    Value::Object counts{{"translated", Value(std::int64_t(0))},
                         {"baked", Value(std::int64_t(0))},
                         {"preserved-source", Value(std::int64_t(0))},
                         {"unsupported", Value(std::int64_t(0))}};
    Value::Array findings;
    for (const auto& finding : prepared.findings) {
        const std::string disposition = dispositionName(finding.disposition);
        counts[disposition] = Value(counts.at(disposition).asInt() + 1);
        findings.emplace_back(Value::Object{{"sourcePath", Value(finding.sourcePath)},
                                            {"feature", Value(finding.feature)},
                                            {"disposition", Value(disposition)},
                                            {"message", Value(finding.message)}});
    }
    report["counts"] = Value(std::move(counts));
    report["findings"] = Value(std::move(findings));

    Value::Array assets;
    for (const auto& asset : prepared.manifest.assets)
        assets.emplace_back(Value::Object{{"asset", Value(asset.asset.format())},
                                          {"type", Value(asset.type + "/" +
                                                         std::to_string(asset.schemaVersion.value()))},
                                          {"contentHash", Value(asset.contentHash)}});
    report["canonicalAssets"] = Value(std::move(assets));
    auto keySeed = Value(report).toJson();
    if (!keySeed) return Result<void>::failure(keySeed.status());
    const std::string importKey = sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(keySeed.value().data()), keySeed.value().size()));
    report["importKey"] = Value(importKey);
    auto encoded = Value(std::move(report)).toJson();
    if (!encoded) return Result<void>::failure(encoded.status());
    prepared.entries.push_back({"reports/import.json",
                                {encoded.value().begin(), encoded.value().end()}});
    prepared.manifest.provenance["path"] = Value("reports/import.json");
    prepared.manifest.provenance["importKey"] = Value(importKey);
    prepared.manifest.provenance["sourceEngine"] = Value(std::string(sourceEngine));
    prepared.manifest.provenance["sourceVersion"] = Value(std::string(sourceVersion));
    return Result<void>::success();
}

}  // namespace eve::asset_import::detail
