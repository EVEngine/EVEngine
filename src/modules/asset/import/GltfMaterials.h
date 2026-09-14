#pragma once
#include "asset/import/AssetImporter.h"
namespace eve::asset_import::gltf {
/** @brief Whether this adapter translates the named standard material/texture extension. */
bool supportsMaterialExtension(std::string_view name);
/** @brief Append owned material/image assets and required bindings to an unpublished candidate.
 * @param request Borrowed source, valid for this synchronous call.
 * @param root Borrowed immutable JSON document.
 * @param buffers Borrowed source buffers, never retained.
 * @param output Exclusively owned unpublished candidate; discard on failure.
 * @return Checked admission result. Worker-safe; no IO or callbacks.
 */
[[nodiscard]] Result<void> appendMaterials(const GltfImportRequest& request, const Value::Object& root,
                                           const std::vector<std::span<const std::uint8_t>>& buffers,
                                           PreparedAssetImport&                              output);
}  // namespace eve::asset_import::gltf
