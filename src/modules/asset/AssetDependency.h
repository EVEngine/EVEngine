#pragma once

/** @file AssetDependency.h @brief Typed dependency closure validation for `.eva`. */

#include "asset/EvaManifest.h"

#include <span>

namespace eve::asset {

/** @brief One externally supplied asset available to a Cook dependency closure. */
struct AvailableAssetDependency {
    AssetRef    asset;
    std::string type;
    SchemaVersion schemaVersion;
};

/** @brief Observable result of validating a package dependency closure. */
struct EvaDependencyValidation {
    std::vector<PersistentId> presentAssets;
    std::vector<PersistentId> omittedOptionalAssets;
};

/** @brief Validate local/external references, expected types and required-edge acyclicity. */
[[nodiscard]] Result<EvaDependencyValidation> validateEvaDependencies(
    const EvaManifest& manifest, std::span<const AvailableAssetDependency> available = {});

}  // namespace eve::asset
