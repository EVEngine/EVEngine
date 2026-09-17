#include "asset/graphics/EvpackImageLoader.h"
#include "asset/RuntimeDefinition.h"

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
    auto decoded = asset::decodeEvpackImage(reader_, image, capabilities, limits);
    if (!decoded) return Result<LoadedGraphicsImage>::failure(decoded.status());
    auto& value    = decoded.value();
    auto  uploaded = value.levels == 1
                         ? factory_.uploadRgba8(value.width, value.height, value.pixels.data(), value.srgb)
                         : factory_.uploadRgba8MipChain(value.width, value.height, value.levels, value.pixels);
    if (!uploaded) return Result<LoadedGraphicsImage>::failure(uploaded.status());
    return Result<LoadedGraphicsImage>::success({image, uploaded.value(), std::move(decoded).takeValue().variant});
}
Result<LoadedGraphicsVolumeTexture> EvpackVolumeTextureLoader::load(const AssetRef&                     volume,
                                                                    const asset::EvpackCapabilities&    capabilities,
                                                                    const VolumeTextureAssetLoadLimits& limits) const {
    auto payload = reader_.read(volume, "eve.volume-texture/1", capabilities, limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedGraphicsVolumeTexture>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    const asset::RuntimeAssetChunk* bulk       = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind == asset::EvpackChunkKind::Definition) {
            if (definition)
                return failure<LoadedGraphicsVolumeTexture>(DiagnosticCode::ParseError,
                                                            "volume texture contains duplicate definitions");
            definition = &chunk;
        } else if (chunk.kind == asset::EvpackChunkKind::Bulk) {
            if (bulk)
                return failure<LoadedGraphicsVolumeTexture>(DiagnosticCode::ParseError,
                                                            "volume texture contains duplicate bulk chunks");
            bulk = &chunk;
        }
    }
    const uint8_t magic[]{'E', 'V', 'V', 'O', 'L', 0, 1, 0};
    if (!definition || !bulk || bulk->chunkId != 1 || bulk->bytes.size() < 24 ||
        !std::equal(std::begin(magic), std::end(magic), bulk->bytes.begin()))
        return failure<LoadedGraphicsVolumeTexture>(DiagnosticCode::ParseError,
                                                    "EVVOL definition or bulk header is missing or invalid");
    const uint32_t width = little32(bulk->bytes, 8), height = little32(bulk->bytes, 12),
                   depth = little32(bulk->bytes, 16), reserved = little32(bulk->bytes, 20);
    if (!width || !height || !depth || width > limits.maximumDimension || height > limits.maximumDimension ||
        depth > limits.maximumDimension || uint64_t(width) > std::numeric_limits<uint64_t>::max() / height ||
        uint64_t(width) * height > std::numeric_limits<uint64_t>::max() / depth)
        return failure<LoadedGraphicsVolumeTexture>(DiagnosticCode::InvalidArgument,
                                                    "EVVOL dimensions, reserved field or packed bytes are invalid");
    const uint64_t voxels = uint64_t(width) * height * depth;
    if (voxels > limits.maximumVoxels || reserved != 0 || limits.maximumDecodedBytes < 24 ||
        voxels > (limits.maximumDecodedBytes - 24) / 4 || bulk->bytes.size() != 24 + voxels * 4)
        return failure<LoadedGraphicsVolumeTexture>(DiagnosticCode::InvalidArgument,
                                                    "EVVOL dimensions, reserved field or packed bytes are invalid");
    auto decoded = asset::decodeRuntimeDefinition(definition->bytes);
    if (!decoded) return Result<LoadedGraphicsVolumeTexture>::failure(decoded.status());
    const auto* object       = decoded.value().getIf<Value::Object>();
    auto        exactInteger = [&](const char* key, uint32_t expected) {
        if (!object) return false;
        const auto found = object->find(key);
        return found != object->end() && found->second.isInt64() && found->second.asInt() == expected;
    };
    auto exactString = [&](const char* key, const char* expected) {
        if (!object) return false;
        const auto found = object->find(key);
        return found != object->end() && found->second.isString() && found->second.asString() == expected;
    };
    if (!object || object->size() != 8 || !exactInteger("schemaVersion", 1) || !exactInteger("width", width) ||
        !exactInteger("height", height) || !exactInteger("depth", depth) ||
        !exactString("schema", "eve.volume-texture") || !exactString("encoding", "rgba8") ||
        !exactString("usage", "noise") || !exactString("blob", "chunk:1"))
        return failure<LoadedGraphicsVolumeTexture>(DiagnosticCode::ParseError,
                                                    "volume definition and EVVOL bulk disagree");
    auto uploaded = factory_.uploadRgba8Volume(width, height, depth, std::span<const uint8_t>(bulk->bytes).subspan(24));
    if (!uploaded) return Result<LoadedGraphicsVolumeTexture>::failure(uploaded.status());
    return Result<LoadedGraphicsVolumeTexture>::success(
        {volume, uploaded.value(), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_graphics
