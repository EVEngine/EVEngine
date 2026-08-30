#pragma once

/**
 * @file UnrealImporter.h
 * @brief UE5/M⁴ landscape adapter that does not execute or link Unreal Engine.
 */

#include "asset_import/TerrainImporter.h"

namespace eve::asset_import {

/** @brief Uncooked Unreal content slice plus a versioned data-only adapter descriptor. */
struct UnrealProjectImportRequest {
    ImportPackageIdentity                  package;
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string                            descriptorPath;
    AssetImportLimits                      limits;
};

/**
 * @brief Import an M⁴-style Landscape/material/grass slice without launching Unreal.
 * @return Canonical terrain, material, PCG and instance assets plus per-feature findings.
 * @remarks The descriptor schema is `eve.unreal-landscape-import` v1. It maps source
 * R16/texture files and data-only M⁴ parameters. Native Blueprint/RVT/VHFM/Nanite
 * behavior is never executed and must be listed as unsupported or baked.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareUnrealM4Import(
    const UnrealProjectImportRequest& request);

}  // namespace eve::asset_import
