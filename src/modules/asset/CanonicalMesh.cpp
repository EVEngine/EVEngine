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

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}

void putFloat(std::vector<std::uint8_t>& out, float value) { put32(out, std::bit_cast<std::uint32_t>(value)); }

}  // namespace

Result<CanonicalMeshData> decodeCanonicalMesh(std::span<const std::uint8_t> bytes, const CanonicalMeshLimits& limits) {
    static constexpr std::uint8_t magic[] = {'E', 'V', 'M', 'E', 'S', 'H', 0};
    if (bytes.size() < 24 || !std::equal(std::begin(magic), std::end(magic), bytes.begin()) ||
        (bytes[7] != 1 && bytes[7] != 2 && bytes[7] != 3))
        return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "canonical mesh header is invalid");
    const auto vertexCount = little32(bytes, 8), indexCount = little32(bytes, 12);
    const auto flags = little32(bytes, 16), countField = little32(bytes, 20);
    const bool legacy  = bytes[7] == 1;
    const bool colored = bytes[7] == 3 && (flags & 2u);
    if (!vertexCount || !indexCount || indexCount % 3 ||
        flags > (legacy ? 3u : (bytes[7] == 3 ? 3u : 1u)) || (legacy && countField))
        return failure<CanonicalMeshData>(DiagnosticCode::ParseError, "canonical mesh metadata is invalid");
    if (vertexCount > limits.maximumVertices || indexCount > limits.maximumIndices)
        return failure<CanonicalMeshData>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds limits");
    const uint32_t uvCount         = legacy ? ((flags & 2u) ? 1u : 0u) : countField;
    const uint64_t header          = 24ull + (legacy ? 0ull : uint64_t(uvCount) * 4);
    const uint64_t floatsPerVertex =
        3ull + ((flags & 1u) ? 3 : 0) + uint64_t(uvCount) * 2 + (colored ? 4 : 0);
    // Bound the product through the budget before multiplying untrusted counts.
    const uint64_t indexBytes = uint64_t(indexCount) * 4;
    if (header > bytes.size() || header > limits.maximumDecodedBytes ||
        indexBytes > limits.maximumDecodedBytes - header ||
        floatsPerVertex > (limits.maximumDecodedBytes - header - indexBytes) / 4 / vertexCount)
        return failure<CanonicalMeshData>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds byte budget");
    const uint64_t expected = header + uint64_t(vertexCount) * floatsPerVertex * 4 + uint64_t(indexCount) * 4;
    if (expected != bytes.size() || expected > limits.maximumDecodedBytes)
        return failure<CanonicalMeshData>(DiagnosticCode::InvalidArgument, "canonical mesh byte size is invalid");
    CanonicalMeshData result;
    result.positions.reserve(std::size_t(vertexCount) * 3);
    if (flags & 1u) result.normals.reserve(std::size_t(vertexCount) * 3);
    if (colored) result.colors.reserve(std::size_t(vertexCount) * 4);
    uint32_t previous = 0;
    for (uint32_t set = 0; set < uvCount; ++set) {
        const auto id = legacy ? 0u : little32(bytes, 24 + size_t(set) * 4);
        if (set && id <= previous)
            return failure<CanonicalMeshData>(DiagnosticCode::ParseError,
                                              "UV set descriptors must be strictly increasing");
        previous = id;
        result.texcoords[id].reserve(size_t(vertexCount) * 2);
    }
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

namespace eve::asset {
Result<std::vector<std::uint8_t>> encodeCanonicalMesh(const CanonicalMeshData& mesh,
                                                       const CanonicalMeshLimits& limits) {
    if (mesh.positions.empty() || mesh.positions.size() % 3 || mesh.indices.empty() || mesh.indices.size() % 3)
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument, "canonical mesh arrays are incomplete");
    const auto vertices = mesh.positions.size() / 3;
    if ((!mesh.normals.empty() && mesh.normals.size() != mesh.positions.size()) ||
        (!mesh.colors.empty() && mesh.colors.size() != vertices * 4) ||
        vertices > limits.maximumVertices || mesh.indices.size() > limits.maximumIndices)
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument, "canonical mesh arrays exceed limits or do not match");
    const auto finite = [](const std::vector<float>& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    if (!finite(mesh.positions) || !finite(mesh.normals) || !finite(mesh.colors))
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument, "canonical mesh attribute is not finite");
    for (const auto& [set, values] : mesh.texcoords)
        if (values.size() != vertices * 2 || !finite(values))
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument, "canonical mesh UV array is invalid");
    for (auto index : mesh.indices) if (index >= vertices)
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument, "mesh index exceeds vertices");
    const std::uint64_t byteCount = 24ull + mesh.texcoords.size() * 4ull +
        vertices * (12ull + (mesh.normals.empty() ? 0ull : 12ull) + mesh.texcoords.size() * 8ull +
                    (mesh.colors.empty() ? 0ull : 16ull)) +
        mesh.indices.size() * 4ull;
    if (byteCount > limits.maximumDecodedBytes || byteCount > std::numeric_limits<std::size_t>::max())
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument, "canonical mesh exceeds byte budget");
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(byteCount));
    const auto put32 = [&out](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    const auto putFloat = [&put32](float value) { put32(std::bit_cast<std::uint32_t>(value)); };
    out.insert(out.end(), {'E', 'V', 'M', 'E', 'S', 'H', 0, 3});
    put32(static_cast<std::uint32_t>(vertices)); put32(static_cast<std::uint32_t>(mesh.indices.size()));
    put32((mesh.normals.empty() ? 0u : 1u) | (mesh.colors.empty() ? 0u : 2u));
    put32(static_cast<std::uint32_t>(mesh.texcoords.size()));
    for (const auto& [set, values] : mesh.texcoords) put32(set);
    for (std::size_t vertex = 0; vertex < vertices; ++vertex) {
        for (int axis = 0; axis < 3; ++axis) putFloat(mesh.positions[vertex * 3 + axis]);
        if (!mesh.normals.empty()) for (int axis = 0; axis < 3; ++axis) putFloat(mesh.normals[vertex * 3 + axis]);
        for (const auto& [set, values] : mesh.texcoords) { putFloat(values[vertex * 2]); putFloat(values[vertex * 2 + 1]); }
        if (!mesh.colors.empty())
            for (int channel = 0; channel < 4; ++channel) putFloat(mesh.colors[vertex * 4 + channel]);
    }
    for (auto index : mesh.indices) put32(index);
    return Result<std::vector<std::uint8_t>>::success(std::move(out));
}
}  // namespace eve::asset