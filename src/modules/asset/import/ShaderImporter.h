#pragma once

#include "asset/import/AssetImporter.h"

namespace eve::asset_import {
/**
 * @brief Prepare a canonical shader definition for the existing atomic `.eva` publisher.
 * @param package Owning identity metadata borrowed only during this call.
 * @param json Untrusted self-contained `eve.shader/1` JSON; maximum 8 MiB.
 * @return Owning validated import candidate; stable asset identity is packageId.child("shader:default").
 * @thread Worker-safe. No callbacks, file access or GPU allocation. Failure publishes nothing.
 */
[[nodiscard]] Result<PreparedAssetImport> prepareShaderImport(const ImportPackageIdentity& package,
                                                              std::string_view             json);
}  // namespace eve::asset_import
