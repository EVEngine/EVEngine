#include "asset/import/ShaderImporter.h"
#include "asset/ShaderAsset.h"
#include "asset/import/ImportCommon.h"

namespace eve::asset_import {
Result<PreparedAssetImport> prepareShaderImport(const ImportPackageIdentity& package, std::string_view json) {
    if (json.size() > 8 * 1024 * 1024)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, "Shader source exceeds budget");
    auto definition = Value::fromJson(json);
    if (!definition) return Result<PreparedAssetImport>::failure(definition.status());
    auto shader = asset::decodeShaderAsset(definition.value());
    if (!shader) return Result<PreparedAssetImport>::failure(shader.status());
    auto manifest = detail::baseManifest(package, "eve.shader");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    auto reference = detail::assetRef(package.packageId.child("shader:default"));
    if (!reference) return Result<PreparedAssetImport>::failure(reference.status());
    auto canonical = definition.value().toJson();
    if (!canonical) return Result<PreparedAssetImport>::failure(canonical.status());
    PreparedAssetImport result;
    result.manifest                = std::move(manifest).takeValue();
    const std::string         path = "assets/" + reference.value().id().format() + "/asset.json";
    std::vector<std::uint8_t> bytes(canonical.value().begin(), canonical.value().end());
    result.manifest.assets.push_back(
        {reference.value(), "eve.shader", SchemaVersion(1), path, detail::sha256(bytes), {"shader"}});
    result.manifest.entrypoints.emplace("default", reference.value());
    result.entries.push_back({path, std::move(bytes)});
    result.sourceMappings.push_back({"shader:default", reference.value()});
    result.findings.push_back({"shader:default", "SPIR-V", ImportDisposition::PreservedSource,
                               "CPU stage framing admitted; GPU layout validation is still required"});
    auto report = detail::finalizeImportReport(result, package, "evengine", "1");
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    return Result<PreparedAssetImport>::success(std::move(result));
}
}  // namespace eve::asset_import
