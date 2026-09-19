#include "asset/CanonicalMesh.h"
#include <algorithm>

#include <bit>
#include <cmath>
#include <limits>

namespace eve::asset {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.mesh"));
}

std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
           (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

float littleFloat(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<float>(little32(bytes, offset));
}

}  // namespace

Result<CanonicalMeshData> decodeCanonicalMesh(std::span<const std::uint8_t> bytes, const CanonicalMeshLimits& limits) {
    static constexpr std::uint8_t magic[] = {'E', 'V', 'M', 'E', 'S', 'H', 0};
    if (bytes.size() < 24 || !std::equal(std::begin(magic), std::end(magic), bytes.begin()) ||
        (bytes[7] != 1 && bytes[7] != 2 && bytes[7] != 3))
        return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "canonical mesh header is invalid");
    const auto vertexCount = little32(bytes, 8), indexCount = little32(bytes, 12);
    const auto flags = little32(bytes, 16), countField = little32(bytes, 20);
    const bool legacy = bytes[7] == 1;
    const uint32_t candidateUvCount = legacy ? ((flags & 2u) ? 1u : 0u) : countField;
    const uint64_t standardFloats =
        3ull + ((flags & 1u) ? 3ull : 0ull) + uint64_t(candidateUvCount) * 2ull + ((flags & 2u) ? 4ull : 0ull);
    const uint64_t standardSize = 24ull + (legacy ? 0ull : uint64_t(candidateUvCount) * 4ull) +
                                  uint64_t(vertexCount) * standardFloats * 4ull + uint64_t(indexCount) * 4ull;
    const bool standardV3 = bytes[7] == 3 && flags <= 3u && standardSize == bytes.size();
    const bool extended = bytes[7] == 3 && !standardV3;
    const bool colored = standardV3 && (flags & 2u);
    if (extended && bytes.size() < 28)
        return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "canonical attribute header truncated");
    if (!vertexCount || !indexCount || indexCount % 3 || flags > (legacy || standardV3 ? 3u : 1u) ||
        (legacy && countField))
        return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "canonical mesh metadata is invalid");
    if (vertexCount > limits.maximumVertices || indexCount > limits.maximumIndices)
        return failure<CanonicalMeshData>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds limits");
    const uint32_t uvCount         = legacy ? ((flags & 2u) ? 1u : 0u) : countField;
    const uint32_t attributeCount  = extended ? little32(bytes, 24) : 0;
    const uint64_t uvOffset        = extended ? 28ull : 24ull;
    const uint64_t attributeOffset = uvOffset + (legacy ? 0ull : uint64_t(uvCount) * 4);
    const uint64_t header          = attributeOffset + uint64_t(attributeCount) * 72;
    uint64_t       floatsPerVertex =
        3ull + ((flags & 1u) ? 3 : 0) + uint64_t(uvCount) * 2 + (colored ? 4ull : 0ull);
    if (header > bytes.size() || header > limits.maximumDecodedBytes)
        return failure<CanonicalMeshData>(DiagnosticCode::InvalidArgument, "canonical descriptors exceed budget");
    CanonicalMeshData result;
    std::string       previousName;
    for (uint32_t i = 0; i < attributeCount; ++i) {
        const auto  at = size_t(attributeOffset + uint64_t(i) * 72);
        std::string name;
        bool        ended = false;
        for (unsigned j = 0; j < 64; ++j) {
            const auto c = bytes[at + j];
            if (!c) {
                ended = true;
                continue;
            }
            if (ended || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "invalid attribute semantic");
            name.push_back(char(c));
        }
        const auto components = little32(bytes, at + 64);
        if (!ended || name.empty() || name <= previousName || name == "POSITION" || name == "NORMAL" ||
            name.starts_with("TEXCOORD_") || !components || components > 4 || little32(bytes, at + 68))
            return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "invalid attribute descriptor");
        previousName = name;
        floatsPerVertex += components;
        result.attributes.emplace(std::move(name), CanonicalMeshAttribute{components, {}});
    }
    // Bound the product through the budget before multiplying untrusted counts.
    const uint64_t indexBytes = uint64_t(indexCount) * 4;
    if (header > bytes.size() || header > limits.maximumDecodedBytes ||
        indexBytes > limits.maximumDecodedBytes - header ||
        floatsPerVertex > (limits.maximumDecodedBytes - header - indexBytes) / 4 / vertexCount)
        return failure<CanonicalMeshData>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds byte budget");
    const uint64_t expected = header + uint64_t(vertexCount) * floatsPerVertex * 4 + uint64_t(indexCount) * 4;
    if (expected != bytes.size() || expected > limits.maximumDecodedBytes)
        return failure<CanonicalMeshData>(DiagnosticCode::InvalidArgument, "canonical mesh byte size is invalid");
    result.positions.reserve(std::size_t(vertexCount) * 3);
    if (flags & 1u) result.normals.reserve(std::size_t(vertexCount) * 3);
    if (colored) result.colors.reserve(std::size_t(vertexCount) * 4);
    uint32_t previous = 0;
    for (uint32_t set = 0; set < uvCount; ++set) {
        const auto id = legacy ? 0u : little32(bytes, size_t(uvOffset) + size_t(set) * 4);
        if (set && id <= previous)
            return failure<CanonicalMeshData>(DiagnosticCode::ParseError,
                                              "UV set descriptors must be strictly increasing");
        previous = id;
        result.texcoords[id].reserve(size_t(vertexCount) * 2);
    }
    for (auto& [name, attribute] : result.attributes)
        attribute.values.reserve(size_t(vertexCount) * attribute.components);
    std::size_t cursor       = size_t(header);
    auto        appendFloats = [&](std::vector<float>& output, std::uint32_t count) -> Result<void> {
        for (std::uint32_t index = 0; index < count; ++index) {
            const float value = littleFloat(bytes, cursor);
            cursor += 4;
            if (!std::isfinite(value))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "canonical mesh contains a non-finite value", {}, {}, "asset.mesh"));
            output.push_back(value);
        }
        return Result<void>::success();
    };
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        auto positions = appendFloats(result.positions, 3);
        if (!positions) return Result<CanonicalMeshData>::failure(positions.status());
        if (flags & 1u) {
            auto normals = appendFloats(result.normals, 3);
            if (!normals) return Result<CanonicalMeshData>::failure(normals.status());
        }
        for (auto& [set, values] : result.texcoords) {
            auto texcoords = appendFloats(values, 2);
            if (!texcoords) return Result<CanonicalMeshData>::failure(texcoords.status());
        }
        for (auto& [name, attribute] : result.attributes) {
            auto values = appendFloats(attribute.values, attribute.components);
            if (!values) return Result<CanonicalMeshData>::failure(values.status());
        }
        if (colored) {
            auto colors = appendFloats(result.colors, 4);
            if (!colors) return Result<CanonicalMeshData>::failure(colors.status());
        }
    }
    result.indices.reserve(indexCount);
    for (std::uint32_t index = 0; index < indexCount; ++index) {
        const std::uint32_t value = little32(bytes, cursor);
        cursor += 4;
        if (value >= vertexCount)
            return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "canonical mesh index exceeds vertex count");
        result.indices.push_back(value);
    }
    return Result<CanonicalMeshData>::success(std::move(result));
}

}  // namespace eve::asset
