#pragma once
#include <utility>
#include <vector>
#include "common/ResourceRef.h"
#include "common/Value.h"
namespace eve::asset_import {
struct ImportPackageIdentity;
struct PreparedAssetImport;
}  // namespace eve::asset_import
namespace eve::stylize {
struct MeshVfxAsset;
}
namespace eve::asset_stylize {
/**
 * @brief Prepare a self-contained native effect package using canonical shader and MeshVfx definitions.
 * @param package Stable package identity; effect identity is packageId.child("mesh-vfx:default").
 * @param effect Borrowed authored effect; each style is a shader asset URI.
 * @param shaders Owning JSON definition snapshots keyed by their persistent shader identity; borrowed for this call.
 * @return Owning source archive candidate with required dependency edges, ready for AtomicAssetPackageStore.
 * @thread Worker-safe, no GPU creation or callbacks. All CPU validation completes before publication.
 * @remarks Uses the existing MeshVfx JSON schema and importer report. No new archive format or runtime registry.
 */
[[nodiscard]] Result<asset_import::PreparedAssetImport> prepareMeshVfxPackage(
    const asset_import::ImportPackageIdentity& package, const stylize::MeshVfxAsset& effect,
    const std::vector<std::pair<AssetRef, Value>>& shaders);
}  // namespace eve::asset_stylize
