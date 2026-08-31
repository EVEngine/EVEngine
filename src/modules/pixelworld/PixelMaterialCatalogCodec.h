#pragma once

#include "common/Result.h"
#include "pixelworld/PixelMaterial.h"

#include <string>
#include <string_view>

namespace eve::pixelworld {

/** @brief Encode schema `eve.pixelworld.material-catalog` version 1 canonically. */
[[nodiscard]] eve::Result<std::string> encodeMaterialCatalogJson(const MaterialCatalog& catalog);

/**
 * @brief Decode and validate schema `eve.pixelworld.material-catalog` version 1.
 * @remarks Unknown fields, versions, invalid references and malformed values are rejected
 * before a MaterialCatalog is returned; no live world is mutated by this operation.
 */
[[nodiscard]] eve::Result<MaterialCatalog> decodeMaterialCatalogJson(std::string_view json);

}  // namespace eve::pixelworld
