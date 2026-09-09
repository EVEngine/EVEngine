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
        return detail::failure<void>(DiagnosticCode::InvalidArgument, "Unity imported asset count exceeds budget");
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
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported, "a .meta GUID is required for stable collection asset identity", source.path);
    const auto ext = detail::extension(source.path);
    if (ext == "anim") return prepareUnitySpriteAnimation(request, source);
    if (ext == "fbx") return prepareUnityFbx(request, source);
    if (source.kind == UnitySourceKind::Material) return prepareUnityMaterial(request, source);
    auto identity      = request.package;
    identity.packageId = request.package.packageId.child("unity:" + source.guid);
    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
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
    return detail::failure<PreparedAssetImport>(
        DiagnosticCode::Unsupported,
        "source retained; this Unity resource type does not yet have a canonical converter", source.path);
}
}  // namespace

Result<PreparedAssetImport> prepareUnityCollection(const UnityProjectImportRequest& request,
                                                   const UnitySourceIndex&          index) {
    auto manifest = detail::baseManifest(request.package, "eve.unity-collection/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport out;
    out.manifest = std::move(manifest).takeValue();
    out.findings = index.findings;
    for (const auto& source : index.assets) {
        if (source.kind == UnitySourceKind::Folder) continue;
        auto imported = importSource(request, source);
        if (!imported) {
            const auto* error = imported.status().primaryDiagnostic();
            if (!error || error->code() != DiagnosticCode::Unsupported)
                return Result<PreparedAssetImport>::failure(imported.status());
            out.findings.push_back(
                {source.path, "resource.conversion", ImportDisposition::Unsupported, error->message()});
            continue;
        }
        auto merged = mergeImport(out, std::move(imported).takeValue(), source, request.limits);
        if (!merged) return Result<PreparedAssetImport>::failure(merged.status());
    }
    if (out.manifest.assets.empty())
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported,
            "Unity collection has no convertible assets; use asset scan to inspect all resource types");
    auto bindings = bindUnityRenderers(request, index, out);
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
            return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                        "Unity prepared output exceeds byte budget");
        total += entry.bytes.size();
    }
    auto report = detail::finalizeImportReport(out, request.package, "unity", "source-files",
                                               {{"sourceHashes", Value(std::move(sourceHashes))}});
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    if (out.entries.back().bytes.size() > request.limits.maximumDecodedBytes ||
        total > request.limits.maximumDecodedBytes - out.entries.back().bytes.size())
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                    "Unity import report exceeds prepared output budget");
    return Result<PreparedAssetImport>::success(std::move(out));
}
}  // namespace eve::asset_import
