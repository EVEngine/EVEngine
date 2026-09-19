#include "asset/import/UnitySourceInternal.h"

#include "asset/CanonicalVolumeTextureCook.h"
#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace eve::asset_import {
namespace {
std::string_view volumeField(std::string_view text, std::string_view key) {
    std::string_view found;
    bool             seen = false;
    while (!text.empty()) {
        const auto end   = text.find('\n');
        auto       line  = text.substr(0, end);
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos) line.remove_prefix(first);
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == ':') {
            if (seen) throw std::runtime_error("duplicate Texture3D field");
            seen = true;
            line.remove_prefix(key.size() + 1);
            const auto begin = line.find_first_not_of(" \t\r");
            found            = begin == std::string_view::npos ? std::string_view{} : line.substr(begin);
            const auto last  = found.find_last_not_of(" \t\r");
            if (last != std::string_view::npos) found = found.substr(0, last + 1);
        }
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    if (!seen) throw std::runtime_error("missing Texture3D field");
    return found;
}
uint64_t volumeNumber(std::string_view text, std::string_view key) {
    const auto value  = volumeField(text, key);
    uint64_t   out    = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("invalid Texture3D integer");
    return out;
}
}  // namespace

Result<PreparedAssetImport> prepareUnityNativeVolumeTexture(const UnityProjectImportRequest& request,
                                                            const UnitySourceAsset&          source) {
    const auto&            bytes = request.files.at(source.path);
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.find("\nTexture3D:") == std::string_view::npos)
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "native asset is not a text Texture3D", source.path, {}, "asset.import"));
    try {
        const auto width = volumeNumber(text, "m_Width"), height = volumeNumber(text, "m_Height"),
                   depth = volumeNumber(text, "m_Depth"), dataSize = volumeNumber(text, "m_DataSize");
        if (volumeNumber(text, "m_Format") != 5 || volumeNumber(text, "m_MipCount") != 1 ||
            volumeNumber(text, "m_ColorSpace") != 0 || volumeNumber(text, "size") != 0 ||
            !volumeField(text, "path").empty())
            return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "requires an embedded linear R8 Texture3D with one mip and no stream data", source.path, {}, "asset.import"));
        if (!width || !height || !depth || width > UINT32_MAX || height > UINT32_MAX || depth > UINT32_MAX ||
            width > request.limits.maximumDecodedBytes / height ||
            width * height > request.limits.maximumDecodedBytes / depth)
            throw std::runtime_error("Texture3D dimensions, bytes or budget are invalid");
        const uint64_t voxels = width * height * depth;
        const auto     hex    = volumeField(text, "_typelessdata");
        if (voxels != dataSize || voxels > request.limits.maximumSourceBytes || voxels > SIZE_MAX / 2 ||
            hex.size() != size_t(voxels * 2))
            throw std::runtime_error("Texture3D dimensions, bytes or budget are invalid");
        std::vector<uint8_t> r8;
        r8.resize(size_t(voxels));
        for (size_t i = 0; i < r8.size(); ++i) {
            unsigned   value  = 0;
            const auto parsed = std::from_chars(hex.data() + i * 2, hex.data() + i * 2 + 2, value, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + i * 2 + 2)
                throw std::runtime_error("invalid Texture3D hex");
            r8[i] = uint8_t(value);
        }
        auto manifest = detail::baseManifest(request.package, "eve.unity-texture3d/1");
        if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
        const auto id  = request.package.packageId.child("unity:" + source.guid);
        auto       ref = detail::assetRef(id);
        if (!ref) return Result<PreparedAssetImport>::failure(ref.status());
        const std::string root = "assets/" + id.format() + "/";
        Value::Object     definition{{"schema", "eve.volume-texture"},
                                     {"schemaVersion", int64_t(1)},
                                     {"width", int64_t(width)},
                                     {"height", int64_t(height)},
                                     {"depth", int64_t(depth)},
                                     {"encoding", "r8"},
                                     {"usage", "noise"},
                                     {"blob", root + "source.r8"}};
        auto              encoded = Value(std::move(definition)).toJson();
        if (!encoded) return Result<PreparedAssetImport>::failure(encoded.status());
        auto verified = asset::cookCanonicalVolumeTextureRgba8(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(encoded.value().data()), encoded.value().size()),
            r8, request.limits.maximumDecodedBytes);
        if (!verified) return Result<PreparedAssetImport>::failure(verified.status());
        PreparedAssetImport out;
        out.manifest                     = std::move(manifest).takeValue();
        const std::string definitionPath = root + "asset.json";
        out.manifest.assets.push_back(
            {ref.value(),
             "eve.volume-texture",
             SchemaVersion(1),
             definitionPath,
             detail::sha256(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(encoded.value().data()),
                                                     encoded.value().size())),
             {"volume-texture", "usage:noise", "source:unity"}});
        out.manifest.entrypoints.emplace("default", ref.value());
        out.manifest.provenance["sourceName"] = Value(source.path);
        out.manifest.provenance["sourceHash"] = Value(detail::sha256(bytes));
        out.entries.push_back({definitionPath, {encoded.value().begin(), encoded.value().end()}});
        out.entries.push_back({root + "source.r8", std::move(r8)});
        out.sourceMappings.push_back({"11700000", ref.value()});
        out.findings.push_back({source.path, "Texture3D.R8", ImportDisposition::Translated,
                                "embedded linear R8 volume retained with X-fastest voxel order"});
        auto report = detail::finalizeImportReport(out, request.package, "unity", "Texture3D-v3",
                                                   {{"sourceFormat", Value("R8")}});
        if (!report) return Result<PreparedAssetImport>::failure(report.status());
        return Result<PreparedAssetImport>::success(std::move(out));
    } catch (const std::exception& error) {
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), source.path, {}, "asset.import"));
    }
}

}  // namespace eve::asset_import
