#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include "asset/CanonicalMesh.h"

namespace eve::asset {
Result<std::vector<std::uint8_t>> encodeCanonicalMesh(const CanonicalMeshData&   mesh,
                                                      const CanonicalMeshLimits& limits) {
    using Output = Result<std::vector<std::uint8_t>>;
    auto invalid = [] {
        return Output::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                 "invalid canonical mesh or byte budget", {}, {}, "asset.mesh"));
    };
    const auto count = mesh.positions.size() / 3;
    if (!count || mesh.positions.size() % 3 || count > limits.maximumVertices || mesh.indices.empty() ||
        mesh.indices.size() % 3 || mesh.indices.size() > limits.maximumIndices ||
        (!mesh.normals.empty() && mesh.normals.size() != mesh.positions.size()) || mesh.texcoords.size() > UINT32_MAX ||
        mesh.attributes.size() > UINT32_MAX)
        return invalid();
    const bool extended    = !mesh.attributes.empty();
    uint64_t   floats      = 3 + (mesh.normals.empty() ? 0 : 3) + uint64_t(mesh.texcoords.size()) * 2;
    auto       validValues = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
    };
    if (!validValues(mesh.positions) || !validValues(mesh.normals)) return invalid();
    for (const auto& [set, values] : mesh.texcoords)
        if (values.size() != count * 2 || !validValues(values)) return invalid();
    for (const auto& [name, attribute] : mesh.attributes) {
        if (name.empty() || name.size() > 63 || name == "POSITION" || name == "NORMAL" ||
            name.starts_with("TEXCOORD_") || !attribute.components || attribute.components > 4 ||
            attribute.values.size() != count * attribute.components || !validValues(attribute.values) ||
            !std::all_of(name.begin(), name.end(),
                         [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }))
            return invalid();
        floats += attribute.components;
    }
    for (auto index : mesh.indices)
        if (index >= count) return invalid();
    const uint64_t header =
        (extended ? 28ull : 24ull) + uint64_t(mesh.texcoords.size()) * 4 + uint64_t(mesh.attributes.size()) * 72;
    const uint64_t indexBytes = uint64_t(mesh.indices.size()) * 4;
    if (header > limits.maximumDecodedBytes || indexBytes > limits.maximumDecodedBytes - header ||
        floats > (limits.maximumDecodedBytes - header - indexBytes) / 4 / count)
        return invalid();
    const uint64_t size = header + uint64_t(count) * floats * 4 + indexBytes;
    if (size > std::numeric_limits<size_t>::max()) return invalid();
    try {
        std::vector<uint8_t> bytes;
        bytes.reserve(size_t(size));
        bytes.insert(bytes.end(), {'E', 'V', 'M', 'E', 'S', 'H', 0, uint8_t(extended ? 3 : 2)});
        auto put = [&](uint32_t value) {
            for (unsigned i = 0; i < 4; ++i) bytes.push_back(uint8_t(value >> (i * 8)));
        };
        put(uint32_t(count));
        put(uint32_t(mesh.indices.size()));
        put(mesh.normals.empty() ? 0 : 1);
        put(uint32_t(mesh.texcoords.size()));
        if (extended) put(uint32_t(mesh.attributes.size()));
        for (const auto& [set, values] : mesh.texcoords) put(set);
        for (const auto& [name, attribute] : mesh.attributes) {
            bytes.insert(bytes.end(), name.begin(), name.end());
            bytes.insert(bytes.end(), 64 - name.size(), 0);
            put(attribute.components);
            put(0);
        }
        auto stream = [&](const auto& values, size_t vertex, size_t components) {
            for (size_t j = 0; j < components; ++j) put(std::bit_cast<uint32_t>(values[vertex * components + j]));
        };
        for (size_t v = 0; v < count; ++v) {
            stream(mesh.positions, v, 3);
            if (!mesh.normals.empty()) stream(mesh.normals, v, 3);
            for (const auto& [set, values] : mesh.texcoords) stream(values, v, 2);
            for (const auto& [name, attribute] : mesh.attributes) stream(attribute.values, v, attribute.components);
        }
        for (auto index : mesh.indices) put(index);
        return Output::success(std::move(bytes));
    } catch (const std::bad_alloc&) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::Failed, "canonical mesh allocation failed", {}, {}, "asset.mesh"));
    }
}
}  // namespace eve::asset
