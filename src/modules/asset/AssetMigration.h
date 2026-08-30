#pragma once

/**
 * @file AssetMigration.h
 * @brief Transactional migrations for canonical definitions stored in `.eva`.
 */

#include "asset/EvaArchive.h"

namespace eve::asset {

/** @brief Return the current supported definition version for a canonical asset type. */
[[nodiscard]] Result<SchemaVersion> currentAssetSchemaVersion(std::string_view type);

/**
 * @brief Migrate every definition in an owning archive to its current schema.
 * @param source Source value consumed only after every migration and invariant validates.
 * @return A fully rebuilt owning candidate; the caller decides whether to publish it.
 * @remarks Supports the current version and N-1. Unknown newer versions and downgrade
 * requests are rejected. No files or registries are mutated by this operation.
 */
[[nodiscard]] Result<EvaArchive> migrateEvaArchive(EvaArchive source,
                                                   const EvaArchiveLimits& limits = {});

}  // namespace eve::asset
