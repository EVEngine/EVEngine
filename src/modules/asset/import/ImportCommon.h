#pragma once

#include "asset/import/AssetImporter.h"

#include "common/Export.h"
#include "data/HashFunction.h"

#include <algorithm>
#include <cctype>

namespace eve::asset_import::detail {

inline std::string sha256(std::span<const std::uint8_t> bytes) {
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

inline Result<asset::EvaManifest> baseManifest(const ImportPackageIdentity& package,
                                                std::string_view importer) {
    if (package.packageId.isNil() || package.packageName.empty() || package.packageVersion.empty())
        return Result<asset::EvaManifest>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "package identity, name and version are required",
                                                                     {}, {}, "asset.import"));
    asset::EvaManifest manifest;
    manifest.packageId = package.packageId;
    manifest.packageName = package.packageName;
    manifest.packageVersion = package.packageVersion;
    manifest.provenance = package.provenance;
    manifest.provenance["importer"] = Value(std::string(importer));
    manifest.provenance["importerVersion"] = Value(std::int64_t(1));
    return Result<asset::EvaManifest>::success(std::move(manifest));
}

inline Result<AssetRef> assetRef(PersistentId id) {
    auto reference = AssetRef::fromId(id);
    if (!reference) return Result<AssetRef>::failure(reference.status());
    return Result<AssetRef>::success(std::move(reference).takeValue());
}

inline std::string extension(std::string_view name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string_view::npos) return {};
    std::string result(name.substr(dot + 1));
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

/** @brief Append the deterministic mandatory import report and bind it from provenance. */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> finalizeImportReport(PreparedAssetImport& prepared,
                                                const ImportPackageIdentity& package,
                                                std::string_view sourceEngine,
                                                std::string_view sourceVersion,
                                                Value::Object options = {});

}  // namespace eve::asset_import::detail
