#pragma once

#include "asset/import/AssetImporter.h"

// Private importer implementation contract; no runtime consumers.
namespace eve::asset_import::gltf {
/** @brief Owning parsed document; keep alive while resolved buffer spans are used. */
struct Document {
    Value                     root;
    std::vector<std::uint8_t> binaryChunk;
};
/** @brief Borrowed validated accessor; its source document/resources must outlive synchronous reads. */
struct Accessor {
    std::span<const std::uint8_t> buffer;
    std::size_t                   offset = 0, stride = 0;
    std::uint32_t                 count = 0, componentType = 0, components = 0;
    bool                          normalized = false;
};
/** @brief Find an internal JSON field.
 * @ownership Borrowed, nullable observer of object-owned storage.
 * @lifetime Valid only until the object is mutated or destroyed; never retained by the importer.
 */
const Value* member(const Value::Object& object, std::string_view name);
/** @brief Validate an optional integer field.
 * @ownership Borrowed nullable field owned by the input document.
 * @lifetime Must remain valid for this call only; no pointer is retained.
 */
[[nodiscard]] Result<std::uint64_t> unsignedValue(const Value* value, std::string path, bool required = true,
                                                  std::uint64_t fallback = 0);
/** @brief Read a uint32 after the caller has bounded the byte range. */
std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset);
/** @brief Append little-endian uint32 to owned storage. */
void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value);
/** @brief Append IEEE FLOAT to owned storage. */
void putFloat(std::vector<std::uint8_t>& bytes, float value);
/** @brief Parse a bounded document into an owning result. */
[[nodiscard]] Result<Document> parseDocument(const GltfImportRequest& request);
/** @brief Resolve immutable spans borrowed from document/request; both must outlive their use. */
[[nodiscard]] Result<std::vector<std::span<const std::uint8_t>>> resolveBuffers(const Value::Object&     root,
                                                                                const Document&          document,
                                                                                const GltfImportRequest& request);
/** @brief Return encoded component byte size, or zero for an unknown type. */
std::uint32_t componentSize(std::uint32_t type);
/** @brief Validate an accessor retaining only buffer spans borrowed for this import. */
[[nodiscard]] Result<Accessor> accessorAt(const Value::Object&                              root,
                                          const std::vector<std::span<const std::uint8_t>>& buffers,
                                          std::uint64_t accessorIndex, const AssetImportLimits& limits);
/** @brief Read one bounded finite FLOAT component. */
[[nodiscard]] Result<float> readFloat(const Accessor& accessor, std::uint32_t element, std::uint32_t component);
/** @brief Read one bounded unsigned scalar index. */
[[nodiscard]] Result<std::uint32_t> readIndex(const Accessor& accessor, std::uint32_t element);
}  // namespace eve::asset_import::gltf
