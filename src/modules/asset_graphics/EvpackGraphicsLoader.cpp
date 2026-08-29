#include "asset_graphics/EvpackGraphicsLoader.h"

#include "graphics/IMeshResourceFactory.h"

#include <bit>
#include <cmath>
#include <limits>

namespace eve::asset_graphics {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.graphics"));
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

float littleFloat(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<float>(little32(bytes, offset));
}

struct MeshStaging {
    std::vector<float>         positions;
    std::vector<float>         normals;
    std::vector<float>         texcoords;
    std::vector<std::uint32_t> indices;
};

Result<MeshStaging> decodeMesh(std::span<const std::uint8_t> bytes,
                               const GraphicsAssetLoadLimits& limits) {
    static constexpr std::uint8_t magic[] = {'E', 'V', 'M', 'E', 'S', 'H', 0, 1};
    if (bytes.size() < 24 || !std::equal(std::begin(magic), std::end(magic), bytes.begin()))
        return failure<MeshStaging>(DiagnosticCode::ParseError, "canonical mesh header is invalid");
    const std::uint32_t vertexCount = little32(bytes, 8);
    const std::uint32_t indexCount = little32(bytes, 12);
    const std::uint32_t flags = little32(bytes, 16);
    const std::uint32_t reserved = little32(bytes, 20);
    if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 || flags > 3 || reserved != 0)
        return failure<MeshStaging>(DiagnosticCode::ParseError, "canonical mesh metadata is invalid");
    if (vertexCount > limits.maximumVertices || indexCount > limits.maximumIndices ||
        vertexCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        indexCount > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        return failure<MeshStaging>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds graphics limits");
    const std::uint64_t floatsPerVertex = 3 + ((flags & 1u) ? 3 : 0) + ((flags & 2u) ? 2 : 0);
    const std::uint64_t payloadBytes = std::uint64_t(vertexCount) * floatsPerVertex * 4 +
                                       std::uint64_t(indexCount) * 4;
    if (payloadBytes > limits.maximumDecodedBytes || payloadBytes > bytes.size() - 24 ||
        payloadBytes != bytes.size() - 24)
        return failure<MeshStaging>(DiagnosticCode::InvalidArgument,
                                    "canonical mesh byte size is invalid or exceeds budget");

    MeshStaging result;
    result.positions.reserve(std::size_t(vertexCount) * 3);
    if (flags & 1u) result.normals.reserve(std::size_t(vertexCount) * 3);
    if (flags & 2u) result.texcoords.reserve(std::size_t(vertexCount) * 2);
    std::size_t cursor = 24;
    auto appendFloats = [&](std::vector<float>& output, std::uint32_t count) -> Result<void> {
        for (std::uint32_t index = 0; index < count; ++index) {
            const float value = littleFloat(bytes, cursor);
            cursor += 4;
            if (!std::isfinite(value))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "canonical mesh contains a non-finite value", {}, {},
                    "asset.graphics"));
            output.push_back(value);
        }
        return Result<void>::success();
    };
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        auto positions = appendFloats(result.positions, 3);
        if (!positions) return Result<MeshStaging>::failure(positions.status());
        if (flags & 1u) {
            auto normals = appendFloats(result.normals, 3);
            if (!normals) return Result<MeshStaging>::failure(normals.status());
        }
        if (flags & 2u) {
            auto texcoords = appendFloats(result.texcoords, 2);
            if (!texcoords) return Result<MeshStaging>::failure(texcoords.status());
        }
    }
    result.indices.reserve(indexCount);
    for (std::uint32_t index = 0; index < indexCount; ++index) {
        const std::uint32_t value = little32(bytes, cursor);
        cursor += 4;
        if (value >= vertexCount)
            return failure<MeshStaging>(DiagnosticCode::ParseError,
                                        "canonical mesh index exceeds vertex count");
        result.indices.push_back(value);
    }
    return Result<MeshStaging>::success(std::move(result));
}

}  // namespace

Result<LoadedGraphicsMesh> EvpackGraphicsLoader::loadMesh(
    const AssetRef& asset, const asset::EvpackCapabilities& capabilities,
    const GraphicsAssetLoadLimits& limits) const {
    auto payload = reader_.read(asset, "eve.mesh/1", capabilities, limits.maximumDecodedBytes);
    if (!payload) return Result<LoadedGraphicsMesh>::failure(payload.status());
    const asset::RuntimeAssetChunk* bulk = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Bulk) continue;
        if (bulk)
            return failure<LoadedGraphicsMesh>(DiagnosticCode::ParseError,
                                               "eve.mesh/1 must contain exactly one bulk chunk");
        bulk = &chunk;
    }
    if (!bulk)
        return failure<LoadedGraphicsMesh>(DiagnosticCode::NotFound,
                                           "eve.mesh/1 bulk chunk is missing");
    auto staging = decodeMesh(bulk->bytes, limits);
    if (!staging) return Result<LoadedGraphicsMesh>::failure(staging.status());
    auto uploaded = factory_.uploadMesh(
        staging.value().positions.data(),
        staging.value().normals.empty() ? nullptr : staging.value().normals.data(),
        staging.value().texcoords.empty() ? nullptr : staging.value().texcoords.data(),
        static_cast<int>(staging.value().positions.size() / 3), staging.value().indices.data(),
        static_cast<int>(staging.value().indices.size()));
    if (!uploaded) return Result<LoadedGraphicsMesh>::failure(uploaded.status());
    return Result<LoadedGraphicsMesh>::success(
        {asset, uploaded.value(), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_graphics
