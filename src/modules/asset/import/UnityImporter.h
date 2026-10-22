#pragma once
#include "common/Export.h"


/**
 * @file UnityImporter.h
 * @brief Direct Unity text-serialization adapter for TerrainData and Prefab assets.
 */

#include "asset/import/TerrainImporter.h"

namespace eve::asset_import {

/** @brief In-memory Unity project slice; keys are normalized project-relative paths. */
struct UnityProjectImportRequest {
    ImportPackageIdentity                            package;
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string                                      terrainDataPath;
    std::string       prefabPath;
    AssetImportLimits limits;
    /** @brief Optional `eve.unity-terrain-details/1`, /2 or /3 JSON exported through Unity's public TerrainData API.
     * When empty and terrainDataPath is set, the importer discovers
     * `<terrainDataPath>.eve-details.json` from files.
     */
    std::string terrainDetailsPath;
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
[[nodiscard]] EVENGINE_API_PLATFORM Result<PreparedAssetImport> prepareUnityProjectImport(
    const UnityProjectImportRequest& request);

}  // namespace eve::asset_import
