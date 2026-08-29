#pragma once

/**
 * @file EvaAssetDatabaseProjection.h
 * @brief Editor-side atomic projection of canonical `.eva` manifests.
 */

#include "asset/EvaManifest.h"
#include "editor/EditorAssetDatabase.h"

namespace eve::editor {

/**
 * @brief Project every asset and dependency in one validated `.eva` manifest as one database generation.
 * @param database Destination editor index; it never becomes an asset payload authority.
 * @param manifest Validated canonical source-package manifest.
 * @param archiveUri Stable project URI of the published `.eva` file.
 * @param importerId Importer/tool identity recorded on every derived index row.
 * @return All owning published records; failure leaves the database generation and contents unchanged.
 * @thread Uses the thread-affinity contract of `MemoryAssetDatabase`.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] EditorResult<std::vector<AssetRecord>> publishEvaAssetProjection(
    MemoryAssetDatabase& database, const asset::EvaManifest& manifest, std::string archiveUri,
    std::string importerId);

}  // namespace eve::editor
