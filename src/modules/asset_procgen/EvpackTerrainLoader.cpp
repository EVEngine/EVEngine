#include "asset_procgen/EvpackTerrainLoader.h"

#include "common/Value.h"
#include "asset/RuntimeDefinition.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace eve::asset_procgen {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.procgen.terrain"));
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

float littleFloat(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<float>(little32(bytes, offset));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool numericEquals(const Value* value, double expected) {
    if (!value || !value->isNumeric()) return false;
    const double actual = value->isInt64() ? double(value->asInt()) : value->asDouble();
    return std::isfinite(actual) && actual == expected;
}

}  // namespace

Result<LoadedTerrain> EvpackTerrainLoader::load(
    const AssetRef& terrainRef, const asset::EvpackCapabilities& capabilities,
    const TerrainAssetLoadLimits& limits) const {
    auto payload = reader_.read(terrainRef, "eve.terrain/1", capabilities,
                                limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedTerrain>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    const asset::RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind == asset::EvpackChunkKind::Definition) {
            if (definition)
                return failure<LoadedTerrain>(DiagnosticCode::Conflict,
                                              "terrain has duplicate definition chunks");
            definition = &chunk;
        } else if (chunk.kind == asset::EvpackChunkKind::Bulk) {
            if (bulk)
                return failure<LoadedTerrain>(DiagnosticCode::Conflict,
                                              "terrain has duplicate bulk chunks");
            bulk = &chunk;
        }
    }
    static constexpr std::uint8_t magic[] = {'E', 'V', 'T', 'R', 'N', 0, 1, 0};
    if (!definition || !bulk || bulk->bytes.size() < 24 ||
        !std::equal(std::begin(magic), std::end(magic), bulk->bytes.begin()))
        return failure<LoadedTerrain>(DiagnosticCode::ParseError,
                                      "terrain definition or EVTRN payload is invalid");
    const std::uint32_t width = little32(bulk->bytes, 8);
    const std::uint32_t height = little32(bulk->bytes, 12);
    const float spacingX = littleFloat(bulk->bytes, 16);
    const float spacingZ = littleFloat(bulk->bytes, 20);
    const std::uint64_t samples = std::uint64_t(width) * height;
    if (width == 0 || height == 0 || width > limits.maximumDimension ||
        height > limits.maximumDimension || samples > limits.maximumSamples ||
        !std::isfinite(spacingX) || !std::isfinite(spacingZ) || spacingX <= 0.f ||
        spacingZ <= 0.f || samples > (std::numeric_limits<std::uint64_t>::max() - 24) / 4 ||
        samples * 4 + 24 != bulk->bytes.size())
        return failure<LoadedTerrain>(DiagnosticCode::InvalidArgument,
                                      "terrain dimensions, spacing, or payload size is invalid");
    asset::RuntimeDefinitionLimits definitionLimits;
    definitionLimits.maximumBytes = limits.maximumDecodedBytes;
    auto metadata = asset::decodeRuntimeDefinition(definition->bytes, definitionLimits);
    if (!metadata) return Result<LoadedTerrain>::failure(metadata.status());
    const auto* root = metadata.value().getIf<Value::Object>();
    const Value* schema = root ? field(*root, "schema") : nullptr;
    const Value* version = root ? field(*root, "schemaVersion") : nullptr;
    const Value* coordinate = root ? field(*root, "coordinateSystem") : nullptr;
    const Value* unit = root ? field(*root, "heightUnit") : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.terrain" ||
        !version || !version->isInt64() || version->asInt() != 1 || !coordinate ||
        !coordinate->isString() ||
        coordinate->asString() != "right-handed-x-right-y-up-minus-z-forward" ||
        !unit || !unit->isString() || unit->asString() != "meter" ||
        !numericEquals(root ? field(*root, "width") : nullptr, width) ||
        !numericEquals(root ? field(*root, "height") : nullptr, height) ||
        !numericEquals(root ? field(*root, "spacingX") : nullptr, spacingX) ||
        !numericEquals(root ? field(*root, "spacingZ") : nullptr, spacingZ))
        return failure<LoadedTerrain>(DiagnosticCode::ParseError,
                                      "terrain metadata does not match EVTRN payload");
    procgen::Heightmap heightmap(static_cast<int>(width), static_cast<int>(height));
    std::size_t cursor = 24;
    for (std::uint32_t z = 0; z < height; ++z) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const float value = littleFloat(bulk->bytes, cursor);
            cursor += 4;
            if (!std::isfinite(value))
                return failure<LoadedTerrain>(DiagnosticCode::ParseError,
                                              "terrain contains a non-finite height");
            heightmap.setHeight(static_cast<int>(x), static_cast<int>(z), value);
        }
    }
    // SpatialData currently has one cell-size axis. Preserve the exact map and reject
    // anisotropic sampling rather than silently distorting the procgen domain.
    if (spacingX != spacingZ)
        return failure<LoadedTerrain>(DiagnosticCode::Unsupported,
                                      "anisotropic terrain spacing is not supported by SpatialData");
    auto spatial = procgen::SpatialData::heightfield(heightmap, 0.f, 0.f, spacingX, 1.f);
    return Result<LoadedTerrain>::success(
        {terrainRef, std::move(heightmap), std::move(spatial), spacingX, spacingZ,
         std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_procgen
