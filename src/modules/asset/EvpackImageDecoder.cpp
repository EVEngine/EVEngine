#include "asset/EvpackImageDecoder.h"
#include "asset/RuntimeDefinition.h"

#include <algorithm>

namespace eve::asset {
namespace {
std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
}  // namespace

Result<DecodedEvpackImage> decodeEvpackImage(const EvpackResourceReader& reader, const AssetRef& image,
                                             const EvpackCapabilities&      capabilities,
                                             const EvpackImageDecodeLimits& limits) {
    auto payload = reader.read(image, "eve.image/3", capabilities, limits.maximumDecodedBytes);
    if (!payload && payload.error()->code() == DiagnosticCode::TypeMismatch)
        payload = reader.read(image, "eve.image/2", capabilities, limits.maximumDecodedBytes);
    if (!payload) return Result<DecodedEvpackImage>::failure(payload.status());
    const RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != EvpackChunkKind::Bulk) continue;
        if (bulk)
            return Result<DecodedEvpackImage>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "eve.image must contain exactly one bulk chunk", {}, {},
                                  "asset.image.decode"));
        bulk = &chunk;
    }
    const bool         explicitSchema = payload.value().schemaVersion.value() == 3;
    const std::uint8_t magic[]        = {'E', 'V', 'I', 'M', 'G', 0, std::uint8_t(explicitSchema ? 2 : 1), 0};
    const std::size_t  headerBytes    = explicitSchema ? 28 : 24;
    if (!bulk || bulk->bytes.size() < headerBytes ||
        !std::equal(std::begin(magic), std::end(magic), bulk->bytes.begin()))
        return Result<DecodedEvpackImage>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "EVIMG bulk header is missing or invalid", {}, {}, "asset.image.decode"));
    const std::uint32_t width = little32(bulk->bytes, 8), height = little32(bulk->bytes, 12);
    const std::uint32_t flags      = little32(bulk->bytes, 16);
    const std::uint32_t levels     = explicitSchema ? little32(bulk->bytes, 20) : 1;
    const std::uint32_t reserved   = little32(bulk->bytes, explicitSchema ? 24 : 20);
    const std::uint64_t pixelCount = std::uint64_t(width) * height;
    if (!width || !height || width > limits.maximumDimension || height > limits.maximumDimension ||
        pixelCount > limits.maximumPixels || flags > 1 || reserved || !levels || levels > 32 ||
        (explicitSchema && flags != 0))
        return Result<DecodedEvpackImage>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "EVIMG dimensions, flags, or byte size are invalid", {},
                              {}, "asset.image.decode"));
    std::uint64_t expected  = headerBytes;
    std::uint32_t fullCount = 0, w = width, h = height;
    for (;;) {
        if (fullCount < levels) {
            const std::uint64_t levelPixels = std::uint64_t(w) * h;
            if (expected > bulk->bytes.size() || levelPixels > (bulk->bytes.size() - expected) / 4)
                return Result<DecodedEvpackImage>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "EVIMG mip bytes are truncated", {}, {}, "asset.image.decode"));
            expected += levelPixels * 4;
        }
        ++fullCount;
        if (w == 1 && h == 1) break;
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    }
    if ((levels != 1 && levels != fullCount) || expected != bulk->bytes.size())
        return Result<DecodedEvpackImage>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                     "EVIMG mip count or packed byte size is invalid",
                                                                     {}, {}, "asset.image.decode"));
    if (explicitSchema) {
        const RuntimeAssetChunk* definition = nullptr;
        for (const auto& chunk : payload.value().chunks) {
            if (chunk.kind != EvpackChunkKind::Definition) continue;
            if (definition)
                return Result<DecodedEvpackImage>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "image contains duplicate definitions", {}, {}, "asset.image.decode"));
            definition = &chunk;
        }
        if (!definition)
            return Result<DecodedEvpackImage>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "image definition is absent", {}, {}, "asset.image.decode"));
        auto decoded = decodeRuntimeDefinition(definition->bytes);
        if (!decoded) return Result<DecodedEvpackImage>::failure(decoded.status());
        const auto* object         = decoded.value().getIf<Value::Object>();
        auto        integerMatches = [&](const char* key, std::uint32_t value) {
            if (!object) return false;
            const auto found = object->find(key);
            return found != object->end() && found->second.isInt64() && found->second.asInt() == value;
        };
        auto stringMatches = [&](const char* key, const char* value) {
            if (!object) return false;
            const auto found = object->find(key);
            return found != object->end() && found->second.isString() && found->second.asString() == value;
        };
        if (!integerMatches("schemaVersion", 3) || !integerMatches("width", width) ||
            !integerMatches("height", height) || !integerMatches("mipCount", levels) ||
            !stringMatches("schema", "eve.image") || !stringMatches("encoding", "rgba8") ||
            !stringMatches("blob", "chunk:1") || bulk->chunkId != 1)
            return Result<DecodedEvpackImage>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "image definition and mip bulk disagree", {}, {}, "asset.image.decode"));
        const auto  color       = object->find("color");
        const auto* colorObject = color == object->end() ? nullptr : color->second.getIf<Value::Object>();
        if (!colorObject || !colorObject->contains("transfer") || !colorObject->at("transfer").isString() ||
            colorObject->at("transfer").asString() != "linear")
            return Result<DecodedEvpackImage>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "explicit image transfer must be linear", {}, {}, "asset.image.decode"));
    }
    DecodedEvpackImage result{image, width, height, levels, flags == 1};
    result.pixels.assign(bulk->bytes.begin() + std::ptrdiff_t(headerBytes), bulk->bytes.end());
    result.variant = std::move(payload).takeValue().variant;
    return Result<DecodedEvpackImage>::success(std::move(result));
}

}  // namespace eve::asset
