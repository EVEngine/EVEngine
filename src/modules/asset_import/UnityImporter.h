#pragma once

/**
 * @file UnityImporter.h
 * @brief Direct Unity text-serialization adapter for TerrainData and Prefab assets.
 */

#include "asset_import/TerrainImporter.h"

namespace eve::asset_import {

/** @brief In-memory Unity project slice; keys are normalized project-relative paths. */
struct UnityProjectImportRequest {
    ImportPackageIdentity                  package;
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string                            terrainDataPath;
    std::string                            prefabPath;
    AssetImportLimits                      limits;
};

/**
 * @brief Parse Unity Force Text YAML and `.meta` GUIDs without running Unity.
 * @return Canonical terrain/PCG/instances and prefab scene-template candidate.
 * @remarks Supports TerrainData heightmap/layers/trees and GameObject/Transform.
 * MonoBehaviour and custom shader records are retained in the audit report as unsupported.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityProjectImport(
    const UnityProjectImportRequest& request);

}  // namespace eve::asset_import
