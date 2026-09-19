#include "asset/import/AssetImporter.h"

#include "asset/CanonicalImageCook.h"
#include "asset/SourceTga.h"
#include "asset/SourceTiff.h"
#include "asset/import/ImportCommon.h"

namespace eve::asset_import {
namespace {

struct ImageInfo {
    std::string encoding;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

std::uint32_t big32(const std::uint8_t* bytes) {
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
           (std::uint32_t(bytes[2]) << 8) | bytes[3];
}

Result<ImageInfo> inspectImage(std::span<const std::uint8_t> bytes, std::uint64_t maximumDecodedBytes) {
    if (bytes.size() >= 18 && bytes[1] == 0 && (bytes[2] == 2 || bytes[2] == 10) &&
        (bytes[16] == 24 || bytes[16] == 32)) {
        auto decoded = asset::detail::decodeTgaRgba8(bytes, maximumDecodedBytes);
        if (!decoded) return Result<ImageInfo>::failure(decoded.status());
        return Result<ImageInfo>::success({"tga", decoded.value().width, decoded.value().height});
    }
    if (bytes.size() >= 4 && ((bytes[0] == 'I' && bytes[1] == 'I') || (bytes[0] == 'M' && bytes[1] == 'M'))) {
        auto decoded = asset::detail::decodeTiffRgba8(bytes, maximumDecodedBytes);
        if (!decoded) return Result<ImageInfo>::failure(decoded.status());
        return Result<ImageInfo>::success({"tiff", decoded.value().width, decoded.value().height});
    }
    static constexpr std::uint8_t png[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (bytes.size() >= 24 && std::equal(std::begin(png), std::end(png), bytes.begin()) &&
        std::string_view(reinterpret_cast<const char*>(bytes.data() + 12), 4) == "IHDR") {
        ImageInfo info{"png", big32(bytes.data() + 16), big32(bytes.data() + 20)};
        if (info.width == 0 || info.height == 0)
            return Result<ImageInfo>::failure(Diagnostic::error(DiagnosticCode::ParseError, "PNG dimensions must be positive", {}, {}, "asset.import"));
        return Result<ImageInfo>::success(std::move(info));
    }
    if (bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8) {
        std::size_t cursor = 2;
        while (cursor + 4 <= bytes.size()) {
            if (bytes[cursor] != 0xff) return Result<ImageInfo>::failure(Diagnostic::error(DiagnosticCode::ParseError, "JPEG marker stream is malformed", {}, {}, "asset.import"));
            while (cursor < bytes.size() && bytes[cursor] == 0xff) ++cursor;
            if (cursor >= bytes.size()) break;
            const std::uint8_t marker = bytes[cursor++];
            if (marker == 0xd9 || marker == 0xda) break;
            if (marker >= 0xd0 && marker <= 0xd7) continue;
            if (cursor + 2 > bytes.size()) break;
            const std::uint16_t length = (std::uint16_t(bytes[cursor]) << 8) | bytes[cursor + 1];
            if (length < 2 || length > bytes.size() - cursor) break;
            const bool startOfFrame = (marker >= 0xc0 && marker <= 0xc3) ||
                                      (marker >= 0xc5 && marker <= 0xc7) ||
                                      (marker >= 0xc9 && marker <= 0xcb) ||
                                      (marker >= 0xcd && marker <= 0xcf);
            if (startOfFrame) {
                if (length < 7) break;
                ImageInfo info{"jpeg", (std::uint32_t(bytes[cursor + 5]) << 8) | bytes[cursor + 6],
                               (std::uint32_t(bytes[cursor + 3]) << 8) | bytes[cursor + 4]};
                if (info.width == 0 || info.height == 0) break;
                return Result<ImageInfo>::success(std::move(info));
            }
            cursor += length;
        }
        return Result<ImageInfo>::failure(Diagnostic::error(DiagnosticCode::ParseError, "JPEG dimensions are missing or malformed", {}, {}, "asset.import"));
    }
    return Result<ImageInfo>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "image importer supports PNG, JPEG and admitted TIFF/TGA encoded sources", {}, {}, "asset.import"));
}

}  // namespace

Result<PreparedAssetImport> prepareImageImport(const ImageImportRequest& request) {
    if (request.encodedBytes.empty() || request.sourceName.empty() || request.usage.empty())
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "image source name, bytes and usage are required", {}, {}, "asset.import"));
    if (request.encodedBytes.size() > request.limits.maximumSourceBytes ||
        request.sourceName.size() > request.limits.maximumStringBytes ||
        request.usage.size() > request.limits.maximumStringBytes)
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "image import budget is exceeded", {}, {}, "asset.import"));
    auto info = inspectImage(request.encodedBytes, request.limits.maximumDecodedBytes);
    if (!info) return Result<PreparedAssetImport>::failure(info.status());
    const std::uint64_t pixels = std::uint64_t(info.value().width) * info.value().height;
    if (pixels > request.limits.maximumDecodedBytes / 4)
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "decoded image budget is exceeded", {}, {}, "asset.import"));
    auto manifestResult = detail::baseManifest(request.package, "eve.image");
    if (!manifestResult) return Result<PreparedAssetImport>::failure(manifestResult.status());
    auto manifest = std::move(manifestResult).takeValue();
    const PersistentId id = request.package.packageId.child("image:default");
    auto reference = detail::assetRef(id);
    if (!reference) return Result<PreparedAssetImport>::failure(reference.status());
    const std::string root = "assets/" + id.format() + "/";
    const std::string blobPath = root + "source." + info.value().encoding;
    Value::Object definition;
    definition["schema"] = Value("eve.image");
    definition["schemaVersion"]  = Value(std::int64_t(3));
    definition["mipCount"]       = Value(std::int64_t(1));
    definition["width"] = Value(static_cast<std::int64_t>(info.value().width));
    definition["height"] = Value(static_cast<std::int64_t>(info.value().height));
    definition["encoding"] = Value(info.value().encoding);
    definition["color"] = Value(Value::Object{
        {"primaries", Value("srgb")},
        {"transfer", Value(request.colorSpace == ImageColorSpace::Srgb ? "srgb" : "linear")},
    });
    definition["usage"] = Value(request.usage);
    definition["rowOrientation"] = Value("top-down");
    definition["blob"] = Value(blobPath);
    auto encodedDefinition = Value(std::move(definition)).toJson();
    if (!encodedDefinition) return Result<PreparedAssetImport>::failure(encodedDefinition.status());
    const std::string definitionText = std::move(encodedDefinition).takeValue();
    if (info.value().encoding == "png") {
        auto verified = asset::cookCanonicalImageRgba8(
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(definitionText.data()),
                definitionText.size()),
            request.encodedBytes, request.limits.maximumDecodedBytes);
        if (!verified) return Result<PreparedAssetImport>::failure(verified.status());
    }
    const std::string definitionPath = root + "asset.json";
    manifest.assets.push_back(
        {std::move(reference).takeValue(),
         "eve.image",
         SchemaVersion(3),
         definitionPath,
         detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(definitionText.data()),
                                                      definitionText.size())),
         {"image", "usage:" + request.usage}});
    auto entryRef = detail::assetRef(id);
    if (!entryRef) return Result<PreparedAssetImport>::failure(entryRef.status());
    manifest.entrypoints.emplace("default", std::move(entryRef).takeValue());
    manifest.provenance["sourceName"] = Value(request.sourceName);
    manifest.provenance["sourceHash"] = Value(detail::sha256(request.encodedBytes));
    PreparedAssetImport result;
    result.manifest = std::move(manifest);
    result.entries.push_back({definitionPath, {definitionText.begin(), definitionText.end()}});
    result.entries.push_back({blobPath, request.encodedBytes});
    auto mappingRef = detail::assetRef(id);
    if (!mappingRef) return Result<PreparedAssetImport>::failure(mappingRef.status());
    result.sourceMappings.push_back({request.sourceName, std::move(mappingRef).takeValue()});
    result.findings.push_back({request.sourceName, "Image." + info.value().encoding,
                               ImportDisposition::Translated,
                               "encoded source and explicit colour semantics retained"});
    auto report = detail::finalizeImportReport(
        result, request.package, "image", info.value().encoding,
        {{"colorSpace", Value(request.colorSpace == ImageColorSpace::Srgb ? "srgb" : "linear")},
         {"usage", Value(request.usage)}});
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    return Result<PreparedAssetImport>::success(std::move(result));
}

}  // namespace eve::asset_import
