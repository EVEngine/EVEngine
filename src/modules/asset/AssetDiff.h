#pragma once

/** @file AssetDiff.h @brief Deterministic semantic comparison of two `.eva` manifests. */

#include "asset/EvaManifest.h"

namespace eve::asset {

/** @brief Stable asset-level change classification. */
enum class EvaAssetChangeKind : std::uint8_t { Added, Removed, Changed };

/** @brief One canonical asset identity changed between source packages. */
struct EvaAssetChange {
    PersistentId       assetId;
    EvaAssetChangeKind kind = EvaAssetChangeKind::Changed;
    std::string        beforeType;
    SchemaVersion      beforeVersion;
    std::string        beforeHash;
    std::string        afterType;
    SchemaVersion      afterVersion;
    std::string        afterHash;
};

/** @brief Deterministically ordered semantic package difference. */
struct EvaPackageDiff {
    std::vector<EvaAssetChange> assets;
    std::uint32_t addedDependencies = 0;
    std::uint32_t removedDependencies = 0;
    bool entrypointsChanged = false;
    /** @brief True only when no canonical runtime-relevant manifest facts changed. */
    [[nodiscard]] bool empty() const noexcept {
        return assets.empty() && addedDependencies == 0 && removedDependencies == 0 &&
               !entrypointsChanged;
    }
};

/** @brief Compare two admitted manifests without using paths, timestamps or archive order. */
[[nodiscard]] Result<EvaPackageDiff> diffEvaManifests(const EvaManifest& before,
                                                      const EvaManifest& after);

}  // namespace eve::asset
