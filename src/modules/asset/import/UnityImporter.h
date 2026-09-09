#pragma once

/**
 * @file UnityImporter.h
 * @brief Direct Unity text-serialization adapter for TerrainData and Prefab assets.
 */

#include "asset/import/TerrainImporter.h"

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
 * @brief Import selected Unity assets, or discover a collection when both selectors are empty.
 * @return An owning canonical candidate, or a structured diagnostic.
 * @remarks Selected import supports terrain data and GameObject/Transform hierarchy.
 * @remarks Collection import also converts supported images, static FBX/glTF meshes and Standard materials.
 * @remarks
 * Unconverted sources and components are explicitly reported.
 * @ownership The result owns its data; input is borrowed only during this call.
 * @thread Worker-safe while the request remains immutable.
 * @reentrancy No callbacks, Unity execution or filesystem mutation.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityProjectImport(const UnityProjectImportRequest& request);

}  // namespace eve::asset_import
