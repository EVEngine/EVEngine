#pragma once

#include "asset/CanonicalMesh.h"
#include "asset/import/StructuredModelImporter.h"

#include <array>

namespace eve::asset_import::structured_detail {
struct MeshPart {
    asset::CanonicalMeshData mesh;
    std::array<double, 4>    color{0.8, 0.8, 0.8, 1.0};
};
Result<void> appendTriangle(asset::CanonicalMeshData& mesh, const std::array<std::array<double, 3>, 3>& triangle,
                            const AssetImportLimits& limits);
Result<PreparedAssetImport> packageModel(const ImportPackageIdentity& identity, std::string_view sourceName,
                                         std::string_view format, std::map<std::string, MeshPart> parts,
                                         const std::map<std::string, std::vector<std::uint8_t>>& sources,
                                         std::vector<ImportFinding> findings, const AssetImportLimits& limits);
}  // namespace eve::asset_import::structured_detail
