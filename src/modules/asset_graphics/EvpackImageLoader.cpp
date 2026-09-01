#include "asset_graphics/EvpackImageLoader.h"

#include <algorithm>
#include <limits>

namespace eve::asset_graphics {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {},
                                                "asset.graphics.image"));
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

}  // namespace

Result<LoadedGraphicsImage> EvpackImageLoader::load(
    const AssetRef& image, const asset::EvpackCapabilities& capabilities,
    const ImageAssetLoadLimits& limits) const {
    auto payload = reader_.read(image, "eve.image/2", capabilities, limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedGraphicsImage>::failure(payload.status());
    const asset::RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Bulk) continue;
        if (bulk)
            return failure<LoadedGraphicsImage>(DiagnosticCode::ParseError,
                                                "eve.image/2 must contain exactly one bulk chunk");
        bulk = &chunk;
    }
    static constexpr std::uint8_t magic[] = {'E', 'V', 'I', 'M', 'G', 0, 1, 0};
    if (!bulk || bulk->bytes.size() < 24 ||
        !std::equal(std::begin(magic), std::end(magic), bulk->bytes.begin()))
        return failure<LoadedGraphicsImage>(DiagnosticCode::ParseError,
                                            "EVIMG bulk header is missing or invalid");
    const std::uint32_t width = little32(bulk->bytes, 8);
    const std::uint32_t height = little32(bulk->bytes, 12);
    const std::uint32_t flags = little32(bulk->bytes, 16);
    const std::uint32_t reserved = little32(bulk->bytes, 20);
    const std::uint64_t pixels = std::uint64_t(width) * height;
    if (width == 0 || height == 0 || width > limits.maximumDimension ||
        height > limits.maximumDimension || pixels > limits.maximumPixels || flags > 1 ||
        reserved != 0 || pixels > (std::numeric_limits<std::uint64_t>::max() - 24) / 4 ||
        pixels * 4 + 24 != bulk->bytes.size())
        return failure<LoadedGraphicsImage>(DiagnosticCode::InvalidArgument,
                                            "EVIMG dimensions, flags, or byte size are invalid");
    auto uploaded = factory_.uploadRgba8(width, height, bulk->bytes.data() + 24, flags == 1);
    if (!uploaded) return Result<LoadedGraphicsImage>::failure(uploaded.status());
    return Result<LoadedGraphicsImage>::success(
        {image, uploaded.value(), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_graphics
