#include "asset/stylize/MeshVfxPackage.h"
#include <algorithm>
#include <cmath>
#include "asset/ShaderAsset.h"
#include "asset/import/ImportCommon.h"
#include "stylize/MeshVfxAsset.h"

namespace eve::asset_stylize {
Result<asset_import::PreparedAssetImport> prepareMeshVfxPackage(
    const asset_import::ImportPackageIdentity& package, const stylize::MeshVfxAsset& effect,
    const std::vector<std::pair<AssetRef, Value>>& shaders) {
    using namespace asset_import;
    auto manifest = asset_import::detail::baseManifest(package, "eve.mesh-vfx");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    if (effect.layers.size() > 32 || effect.trail || effect.trailBinding || !effect.animationTriggers.play.empty() ||
        !effect.animationTriggers.stop.empty() || !effect.animationTriggers.trailBreak.empty())
        return asset_import::detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported, "Package supports up to 32 mesh layers without gameplay attachments");
    for (const auto& layer : effect.layers) {
        const float cycle = layer.playback.fadeIn + layer.playback.duration + layer.playback.fadeOut;
        if (!std::isfinite(cycle) || (layer.playback.loop && cycle <= 0))
            return asset_import::detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                                      "Invalid playback cycle");
    }
    auto effectJson = effect.toJson();
    if (!effectJson) return Result<PreparedAssetImport>::failure(effectJson.status());
    auto effectRef = AssetRef::fromId(package.packageId.child("mesh-vfx:default"));
    if (!effectRef) return Result<PreparedAssetImport>::failure(effectRef.status());
    PreparedAssetImport result;
    result.manifest   = std::move(manifest).takeValue();
    const auto append = [&](const AssetRef& ref, std::string type, const std::string& json) {
        const std::string         path = "assets/" + ref.id().format() + "/asset.json";
        std::vector<std::uint8_t> bytes(json.begin(), json.end());
        result.manifest.assets.push_back(
            {ref, std::move(type), SchemaVersion(1), path, asset_import::detail::sha256(bytes), {}});
        result.entries.push_back({path, std::move(bytes)});
        result.sourceMappings.push_back({ref.format(), ref});
    };
    std::map<std::string, std::map<std::string, float>> defaults;
    for (const auto& [ref, value] : shaders) {
        if (ref.id().isNil() || ref == effectRef.value() || defaults.contains(ref.format()))
            return asset_import::detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                                      "Invalid shader identity");
        auto shader = asset::decodeShaderAsset(value);
        if (!shader) return Result<PreparedAssetImport>::failure(shader.status());
        if (shader.value().interface != asset::ShaderAssetInterface::Mesh3D)
            return asset_import::detail::failure<PreparedAssetImport>(DiagnosticCode::TypeMismatch,
                                                                      "Mesh effect requires mesh shaders");
        for (const auto& parameter : shader.value().parameters) {
            if (parameter.defaults.size() != 1)
                return asset_import::detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                                          "Mesh effect requires scalar parameters");
            defaults[ref.format()][parameter.name] = parameter.defaults[0];
        }
        defaults.try_emplace(ref.format());
        auto json = value.toJson();
        if (!json) return Result<PreparedAssetImport>::failure(json.status());
        append(ref, "eve.shader", json.value());
    }
    for (std::size_t i = 0; i < effect.layers.size(); ++i) {
        auto ref = AssetRef::parse(effect.layers[i].style);
        if (!ref) return Result<PreparedAssetImport>::failure(ref.status());
        if (std::none_of(shaders.begin(), shaders.end(),
                         [&](const auto& shader) { return shader.first == ref.value(); }))
            return asset_import::detail::failure<PreparedAssetImport>(DiagnosticCode::NotFound,
                                                                      "Required layer shader is missing");
        result.manifest.dependencies.push_back({effectRef.value(),
                                                ref.value(),
                                                asset::EvaDependencyKind::RuntimeRequired,
                                                "layers[" + std::to_string(i) + "].style",
                                                {},
                                                "eve.shader/1",
                                                {}});
    }
    auto playback = stylize::MeshVfxAssetInstance::create(effect, defaults);
    if (!playback) return Result<PreparedAssetImport>::failure(playback.status());
    append(effectRef.value(), "eve.stylize.mesh-vfx", effectJson.value());
    result.manifest.entrypoints.emplace("default", effectRef.value());
    result.findings.push_back({"mesh-vfx:default", "MeshVfx", ImportDisposition::Translated,
                               "Canonical playback and required external shader references retained"});
    auto report = asset_import::detail::finalizeImportReport(result, package, "evengine", "1");
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    auto bytes = asset::buildEvaArchive(result.manifest, result.entries);
    if (!bytes) return Result<PreparedAssetImport>::failure(bytes.status());
    return Result<PreparedAssetImport>::success(std::move(result));
}
}  // namespace eve::asset_stylize
