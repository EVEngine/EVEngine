#pragma once

/** @file LegacyAnimationImporter.h @brief Migration of legacy `*.anim.txt` fixtures to `.eva`. */

#include "asset_import/AssetImporter.h"

namespace eve::asset_import {

/** @brief Owning request for the non-production `EVA 1` animation text fixture. */
struct LegacyAnimationImportRequest {
    ImportPackageIdentity package;
    std::string            sourceName;
    std::string            text;
    AssetImportLimits      limits;
};

/**
 * @brief Convert a legacy text fixture into canonical skeleton and animation-clip assets.
 * @remarks Converts centimeters to meters and reflects source +Z into canonical -Z.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareLegacyAnimationImport(
    const LegacyAnimationImportRequest& request);

}  // namespace eve::asset_import
