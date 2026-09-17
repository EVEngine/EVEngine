#include <charconv>
#include <stdexcept>
#include <string_view>
#include "asset/SourceBc3.h"
#include "asset/SourcePng.h"
#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

namespace eve::asset_import {
namespace {
// Line parsing avoids regex recursion on multi-megabyte serialized pixel data.
std::string_view field(std::string_view text, std::string_view key) {
    std::string_view found;
    bool             seen = false;
    while (!text.empty()) {
        const auto end   = text.find('\n');
        auto       line  = text.substr(0, end);
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos) line.remove_prefix(first);
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == ':') {
            if (seen) throw std::runtime_error("duplicate Texture2D field");
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
    if (!seen) throw std::runtime_error("missing Texture2D field");
    return found;
}
std::uint64_t number(std::string_view text, std::string_view key) {
    const auto    value  = field(text, key);
    std::uint64_t out    = 0;
    const auto    parsed = std::from_chars(value.data(), value.data() + value.size(), out);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("invalid Texture2D integer");
    return out;
}
}  // namespace
Result<PreparedAssetImport> prepareUnityNativeTexture(const UnityProjectImportRequest& request,
                                                      const UnitySourceAsset&          source) {
    const auto&            bytes = request.files.at(source.path);
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.find("\nTexture2D:") == std::string_view::npos)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported, "native asset is not a text Texture2D",
                                                    source.path);
    try {
        const auto width = number(text, "m_Width"), height = number(text, "m_Height");
        const auto mips = number(text, "m_MipCount"), transfer = number(text, "m_ColorSpace");
        if (number(text, "m_TextureFormat") != 12 || number(text, "m_TextureDimension") != 2 ||
            number(text, "m_ImageCount") != 1 || transfer > 1 || number(text, "size") != 0 ||
            !field(text, "path").empty())
            return detail::failure<PreparedAssetImport>(
                DiagnosticCode::Unsupported, "requires embedded BC3 Texture2D with one image and known color space",
                source.path);
        if (!width || !height || width > 0xffffffffu || height > 0xffffffffu ||
            width > request.limits.maximumDecodedBytes / 4 / height || !mips || mips > 33)
            throw std::runtime_error("Texture2D dimensions or mip count exceed budget");
        std::uint64_t expected = 0, w = width, h = height;
        for (std::uint64_t i = 0; i < mips; ++i) {
            if (w == 1 && h == 1 && i + 1 < mips) throw std::runtime_error("excess Texture2D mip levels");
            expected += ((w + 3) / 4) * ((h + 3) / 4) * 16;
            w = std::max(std::uint64_t(1), w / 2);
            h = std::max(std::uint64_t(1), h / 2);
        }
        const auto hex = field(text, "_typelessdata");
        if (expected > request.limits.maximumDecodedBytes || expected != number(text, "m_CompleteImageSize") ||
            expected != number(text, "image data") || hex.size() % 2 || hex.size() / 2 != expected)
            throw std::runtime_error("Texture2D mip-chain size mismatch");
        std::vector<std::uint8_t> blocks(std::size_t(expected), 0);
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            unsigned   value  = 0;
            const auto parsed = std::from_chars(hex.data() + i * 2, hex.data() + i * 2 + 2, value, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != hex.data() + i * 2 + 2)
                throw std::runtime_error("invalid Texture2D hex");
            blocks[i] = std::uint8_t(value);
        }
        std::vector<std::uint8_t> pixels;
        std::size_t blockOffset = 0;
        w = width;
        h = height;
        for (std::uint64_t level = 0; level < mips; ++level) {
            const auto levelBytes = ((w + 3) / 4) * ((h + 3) / 4) * 16;
            auto decoded = asset::detail::decodeBc3Rgba8(
                std::span(blocks).subspan(blockOffset, std::size_t(levelBytes)), std::uint32_t(w), std::uint32_t(h),
                request.limits.maximumDecodedBytes);
            if (!decoded) return Result<PreparedAssetImport>::failure(decoded.status());
            const auto stride = std::size_t(w * 4);
            for (std::size_t y = 0; y < h / 2; ++y)
                for (std::size_t x = 0; x < stride; ++x)
                    std::swap(decoded.value()[y * stride + x], decoded.value()[(h - 1 - y) * stride + x]);
            if (decoded.value().size() > request.limits.maximumDecodedBytes - pixels.size())
                throw std::runtime_error("Texture2D decoded mip chain exceeds budget");
            pixels.insert(pixels.end(), decoded.value().begin(), decoded.value().end());
            blockOffset += std::size_t(levelBytes);
            w = std::max(std::uint64_t(1), w / 2);
            h = std::max(std::uint64_t(1), h / 2);
        }
        const auto basePixels = std::span(pixels).first(std::size_t(width * height * 4));
        auto png = asset::detail::encodeSourcePng(std::uint32_t(width), std::uint32_t(height), basePixels,
                                                  request.limits.maximumDecodedBytes);
        if (!png) return Result<PreparedAssetImport>::failure(png.status());
        auto identity      = request.package;
        identity.packageId = identity.packageId.child("unity:" + source.guid);
        // Serialized ColorSpace.Linear (1) selects sRGB sampling, as in Unity texture export metadata.
        auto out = prepareImageImport({identity, "unity:" + source.guid, std::move(png).takeValue(),
                                       transfer == 1 ? ImageColorSpace::Srgb : ImageColorSpace::Linear, "color",
                                       request.limits});
        if (!out) return out;
        auto& definitionEntry = out.value().entries.front();
        auto parsed = Value::fromJson(std::string(definitionEntry.bytes.begin(), definitionEntry.bytes.end()));
        if (!parsed || !parsed.value().isObject())
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                        "generated native texture definition is invalid", source.path);
        auto& object = *parsed.value().getIf<Value::Object>();
        const auto oldBlob = object.at("blob").asString();
        const auto newBlob = definitionEntry.path.substr(0, definitionEntry.path.rfind('/') + 1) + "source.rgba8-mips";
        object["encoding"] = Value("rgba8-mips");
        object["mipCount"] = Value(std::int64_t(mips));
        object["blob"] = Value(newBlob);
        auto encoded = parsed.value().toJson();
        if (!encoded) return Result<PreparedAssetImport>::failure(encoded.status());
        definitionEntry.bytes.assign(encoded.value().begin(), encoded.value().end());
        for (auto& entry : out.value().entries) {
            if (entry.path == oldBlob) {
                entry.path = newBlob;
                entry.bytes = std::move(pixels);
            }
        }
        out.value().manifest.assets.front().contentHash = detail::sha256(definitionEntry.bytes);
        out.value().findings.push_back({source.path, "Texture2D.BC3", ImportDisposition::Translated,
                                        "complete embedded BC3 mip chain decoded to RGBA8; source row order and "
                                        "transfer converted to canonical image semantics"});
        return out;
    } catch (const std::exception& error) {
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument, error.what(), source.path);
    }
}
}  // namespace eve::asset_import
