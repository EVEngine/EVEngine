#pragma once

/** @file GtsTerrainLodPackage.h @brief Canonical asset packaging for generated GTS terrain LODs. */

#include "asset/import/AssetImporter.h"
#include "procgen/GtsTerrainLod.h"

namespace eve::asset_procgen {
/**
 * @brief Prepare a complete `.eva` source package containing every non-empty GTS tile LOD as `eve.mesh/2`.
 * @param package Stable caller-owned package identity copied into the result.
 * @param lods Borrowed immutable generated LOD set consumed synchronously.
 * @param terrainName Filesystem-safe source terrain name used for audit mappings and mesh labels.
 * @param limits Allocation and output limits applied before candidate publication.
 * @return Owned import candidate ready for `buildEvaArchive`, or a structured failure with no partial result.
 * @thread Worker-safe; performs no filesystem, graphics, script or callback operation.
 */
[[nodiscard]] Result<asset_import::PreparedAssetImport> prepareGtsTerrainLodPackage(
    const asset_import::ImportPackageIdentity& package, const procgen::GtsTerrainLodSet& lods,
    const std::string& terrainName, const asset_import::AssetImportLimits& limits = {});
}