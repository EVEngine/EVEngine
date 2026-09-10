#pragma once

#include "asset/import/AssetImporter.h"

namespace eve::asset_import::gltf {
/**
 * @brief Append owning canonical skeleton, skin and clip assets to an unpublished candidate.
 * @param request Borrowed input, valid for this synchronous call.
 * @param root Borrowed parsed document, unchanged during this call.
 * @param buffers Borrowed decoded buffers, valid for this call only.
 * @param output Unpublished candidate; discard it on failure.
 * @return Structured admission failure or success. No callbacks, IO or retained borrows.
 * @thread Worker-safe with exclusive access to output.
 */
[[nodiscard]] Result<void> appendAnimation(const GltfImportRequest& request, const Value::Object& root,
                                           const std::vector<std::span<const std::uint8_t>>& buffers,
                                           PreparedAssetImport&                              output);
}  // namespace eve::asset_import::gltf
