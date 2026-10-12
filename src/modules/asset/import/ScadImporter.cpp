#include "asset/import/StructuredModelCommon.h"

#include <bit>
#include <cmath>

namespace eve::asset_import {
Result<PreparedAssetImport> prepareScadImport(const ScadImportRequest& request) {
    using Output = Result<PreparedAssetImport>;
    auto invalid = [](std::string message) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::ParseError, std::move(message), {}, {}, "asset.import.scad"));
    };
    const auto& bytes = request.binaryStl;
    if (request.sourceBytes.empty() || request.sourceBytes.size() > request.limits.maximumSourceBytes ||
        bytes.size() < 84 || bytes.size() > request.limits.maximumSourceBytes)
        return invalid("SCAD source/binary STL is missing or outside budget");
    auto word = [&](std::size_t offset) {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
               (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    const auto count = word(80);
    if (count == 0 || 84ull + std::uint64_t(count) * 50 != bytes.size() ||
        std::uint64_t(count) * 3 > request.limits.maximumVerticesPerPrimitive ||
        std::uint64_t(count) * 3 > request.limits.maximumIndicesPerPrimitive ||
        std::uint64_t(count) * 84 + 24 > request.limits.maximumDecodedBytes)
        return invalid("binary STL triangle count/length/budget is invalid");
    structured_detail::MeshPart part;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto offset = std::size_t(84) + std::size_t(i) * 50;
        if (bytes[offset + 48] != 0 || bytes[offset + 49] != 0)
            return invalid("coloured/extended STL is unsupported; export plain binary STL");
        for (std::size_t j = 0; j < 3; ++j)
            if (!std::isfinite(std::bit_cast<float>(word(offset + j * 4)))) return invalid("nonfinite STL normal");
        std::array<std::array<double, 3>, 3> triangle{};
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            const auto   base = offset + 12 + vertex * 12;
            const double x    = std::bit_cast<float>(word(base));
            const double y    = std::bit_cast<float>(word(base + 4));
            const double z    = std::bit_cast<float>(word(base + 8));
            triangle[vertex]  = {x * 0.001, z * 0.001, -y * 0.001};
        }
        auto added = structured_detail::appendTriangle(part.mesh, triangle, request.limits);
        if (!added) return Output::failure(added.status());
    }
    return structured_detail::packageModel(
        request.package, request.sourceName, "scad", {{"default", std::move(part)}},
        {{request.sourceName, request.sourceBytes}},
        {{request.sourceName, "SCAD.geometry", ImportDisposition::Baked,
          "OpenSCAD binary STL triangulated; millimetres/Z-up converted to metres/Y-up"},
         {request.sourceName, "SCAD.nonGeometry", ImportDisposition::Unsupported,
          "STL does not retain colours, rig or animation; root source retained, external dependency closure not "
          "archived"}},
        request.limits);
}
}  // namespace eve::asset_import
