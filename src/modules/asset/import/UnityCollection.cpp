#include "asset/import/UnityImporter.h"

#include "asset/import/ImportCommon.h"
#include "asset/import/UnitySourceInternal.h"

#include <regex>

namespace eve::asset_import {
namespace {

Result<void> mergeImport(PreparedAssetImport& output, PreparedAssetImport input, const UnitySourceAsset& source,
                         const AssetImportLimits& limits) {
    if (input.manifest.assets.size() > limits.maximumAssets ||
        output.manifest.assets.size() > limits.maximumAssets - input.manifest.assets.size())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Unity imported asset count exceeds budget", {}, {}, "asset.import"));
    for (auto& asset : input.manifest.assets) output.manifest.assets.push_back(std::move(asset));
    for (auto& dependency : input.manifest.dependencies) output.manifest.dependencies.push_back(std::move(dependency));
    for (auto& entry : input.entries)
        if (entry.path != "reports/import.json") output.entries.push_back(std::move(entry));
    for (auto& mapping : input.sourceMappings) {
        mapping.sourceObject = "unity:" + source.guid + "/" + mapping.sourceObject;
        output.sourceMappings.push_back(std::move(mapping));
    }
    for (auto& finding : input.findings) {
        finding.sourcePath = source.path;
        output.findings.push_back(std::move(finding));
    }
    for (const auto& [name, entry] : input.manifest.entrypoints)
        output.manifest.entrypoints.emplace(source.path + (name == "default" ? "" : "#" + name), entry);
    return Result<void>::success();
}

Result<PreparedAssetImport> importSource(const UnityProjectImportRequest& request, const UnitySourceAsset& source) {
    if (source.guid.empty())
        return Result<PreparedAssetImport>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "a .meta GUID is required for stable collection asset identity", source.path,
            {}, "asset.import"));
    const auto ext = detail::extension(source.path);
    if (ext == "anim") return prepareUnitySpriteAnimation(request, source);
    if (ext == "asset") {
        const auto&            bytes = request.files.at(source.path);
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (text.find("\nTexture2D:") != std::string_view::npos) return prepareUnityNativeTexture(request, source);
        if (text.find("\nTexture3D:") != std::string_view::npos)
            return prepareUnityNativeVolumeTexture(request, source);
        return prepareUnityNativeMesh(request, source);
    }
    if (ext == "fbx") return prepareUnityFbx(request, source);
    if (ext == "tvepreset") return prepareUnityVegetationPreset(request, source);
    if (source.kind == UnitySourceKind::Scene) return prepareUnityVegetationScene(request, source);
    if (source.kind == UnitySourceKind::Material) return prepareUnityMaterial(request, source);
    auto identity      = request.package;
    identity.packageId = request.package.packageId.child("unity:" + source.guid);
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tif" || ext == "tiff" || ext == "tga") {
        const auto&       meta = request.files.at(source.path + ".meta");
        const std::string text(meta.begin(), meta.end());
        const std::regex  srgb(R"((?:^|\n)\s*sRGBTexture:\s*0(?:\s|$))");
        const std::regex  normal(R"((?:^|\n)\s*textureType:\s*1(?:\s|$))");
        const bool        normalMap = std::regex_search(text, normal);
        const bool        linear    = normalMap || std::regex_search(text, srgb);
        auto              result = prepareImageImport({identity, "unity:" + source.guid, request.files.at(source.path),
                                          linear ? ImageColorSpace::Linear : ImageColorSpace::Srgb,
                                          normalMap ? "normal" : "color", request.limits});
        if (!result) return result;
        if (std::regex_search(text, std::regex(R"((?:^|\n)\s*spriteMode:\s*[12](?:\s|$))")))
            result.value().findings.push_back({source.path, "TextureImporter.sprite", ImportDisposition::Unsupported,
                                               "standalone Sprite/UI assets are not emitted; converted sprite "
                                               "animation clips retain their referenced slicing and pivots"});
        if (normalMap)
            result.value().findings.push_back(
                {source.path, "TextureImporter.normal", ImportDisposition::Unsupported,
                 "source normal pixels are linear; Unity normal processing settings are not baked"});
        return result;
    }
    if (ext == "gltf" || ext == "glb") {
        std::map<std::string, std::vector<std::uint8_t>> external;
        const auto                                       slash = source.path.find_last_of('/');
        const auto prefix = slash == std::string::npos ? std::string{} : source.path.substr(0, slash + 1);
        for (const auto& [path, bytes] : request.files)
            if (path.starts_with(prefix) && path != source.path && !path.ends_with(".meta"))
                external.emplace(path.substr(prefix.size()), bytes);
        auto result = prepareGltfImport(
            {identity, "unity:" + source.guid, request.files.at(source.path), std::move(external), request.limits});
        if (!result) return result;
        result.value().findings.push_back(
            {source.path, "Model.rendering", ImportDisposition::Unsupported,
             "mesh primitives imported; materials, skinning, animation and scene binding are not converted"});
        return result;
    }
    if (source.kind == UnitySourceKind::Prefab) {
        return prepareUnityPrefab(request, source.path);
    }
    return Result<PreparedAssetImport>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported,
                          "source retained; this Unity resource type does not yet have a canonical converter",
                          source.path, {}, "asset.import"));
}
}  // namespace

Result<PreparedAssetImport> prepareUnityCollection(const UnityProjectImportRequest& request,
                                                   const UnitySourceIndex&          index) {
    auto manifest = detail::baseManifest(request.package, "eve.unity-collection/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport out;
    out.manifest = std::move(manifest).takeValue();
    out.findings = index.findings;
    UnityProjectImportRequest resolved      = request;
    std::uint64_t             expandedBytes = 0;
    for (const auto& source : index.assets) {
        if (source.kind != UnitySourceKind::Prefab) continue;
        const auto& bytes = request.files.at(source.path);
        if (std::string(bytes.begin(), bytes.end()).find("!u!1001 ") == std::string::npos) continue;
        auto expanded = expandUnityPrefab(request, index, source);
        if (!expanded) return Result<PreparedAssetImport>::failure(expanded.status());
        out.findings.insert(out.findings.end(), expanded.value().findings.begin(), expanded.value().findings.end());
        if (expanded.value().bytes.size() > request.limits.maximumDecodedBytes ||
            expandedBytes > request.limits.maximumDecodedBytes - expanded.value().bytes.size())
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "aggregate Prefab expansion budget exceeded", {}, {}, "asset.import"));
        expandedBytes += expanded.value().bytes.size();
        resolved.files[source.path] = std::move(expanded).takeValue().bytes;
    }
    for (const auto& source : index.assets) {
        if (source.kind == UnitySourceKind::Folder) continue;
        auto imported = importSource(resolved, source);
        if (!imported) {
            const auto* error = imported.status().primaryDiagnostic();
            const bool missingMaterialDependency =
                error && error->code() == DiagnosticCode::NotFound &&
                source.kind == UnitySourceKind::Material;
            if (!error || (error->code() != DiagnosticCode::Unsupported && !missingMaterialDependency))
                return Result<PreparedAssetImport>::failure(imported.status());
            out.findings.push_back(
                {source.path, missingMaterialDependency ? "resource.dependency" : "resource.conversion",
                 ImportDisposition::Unsupported, error->message()});
            continue;
        }
        auto merged = mergeImport(out, std::move(imported).takeValue(), source, request.limits);
        if (!merged) return Result<PreparedAssetImport>::failure(merged.status());
    }
    if (out.manifest.assets.empty())
        return Result<PreparedAssetImport>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported,
            "Unity collection has no convertible assets; use asset scan to inspect all resource types", {}, {},
            "asset.import"));
    auto bindings = bindUnityRenderers(resolved, index, out);
    if (!bindings) return Result<PreparedAssetImport>::failure(bindings.status());
    Value::Object sourceHashes;
    for (const auto& [path, data] : request.files) {
        out.entries.push_back({"sources/unity/" + path, data});
        sourceHashes[path] = Value(detail::sha256(data));
        if (!path.ends_with(".meta"))
            out.findings.push_back(
                {path, "source.archive", ImportDisposition::PreservedSource,
                 "original source and available .meta retained under sources/unity; not runtime conversion"});
    }
    out.manifest.provenance["sourceCoordinateSystem"] = Value("left-handed-x-right-y-up-z-forward");
    std::uint64_t total                               = 0;
    for (const auto& entry : out.entries) {
        if (entry.bytes.size() > request.limits.maximumDecodedBytes ||
            total > request.limits.maximumDecodedBytes - entry.bytes.size())
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Unity prepared output exceeds byte budget", {}, {}, "asset.import"));
        total += entry.bytes.size();
    }
    auto report = detail::finalizeImportReport(out, request.package, "unity", "source-files",
                                               {{"sourceHashes", Value(std::move(sourceHashes))}});
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    if (out.entries.back().bytes.size() > request.limits.maximumDecodedBytes ||
        total > request.limits.maximumDecodedBytes - out.entries.back().bytes.size())
        return Result<PreparedAssetImport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Unity import report exceeds prepared output budget", {},
                              {}, "asset.import"));
    return Result<PreparedAssetImport>::success(std::move(out));
}
}  // namespace eve::asset_import
