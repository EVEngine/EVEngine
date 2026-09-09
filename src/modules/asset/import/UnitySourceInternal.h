#pragma once

#include "asset/import/UnitySource.h"
#include "common/Utf8Validation.h"

#include <algorithm>

namespace eve::asset_import {
struct UnityProjectImportRequest;
/** @brief Convert an explicit SpriteRenderer timeline and referenced metadata into an owning canonical candidate. */
[[nodiscard]] Result<PreparedAssetImport> prepareUnitySpriteAnimation(const UnityProjectImportRequest& request,
                                                                      const UnitySourceAsset&          source);
/** @brief Prepare an owning collection candidate using a validated source index. */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityCollection(const UnityProjectImportRequest& request,
                                                                 const UnitySourceIndex&          index);
/** @brief Prepare one prefab without copying or re-indexing the source collection. */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityPrefab(const UnityProjectImportRequest& request,
                                                             const std::string&               path);
/** @brief Convert a static FBX with explicit Unity importer settings and hashed subasset identities. */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityFbx(const UnityProjectImportRequest& request,
                                                          const UnitySourceAsset&          source);
/** @brief Convert supported built-in Standard material properties to canonical PBR data. */
[[nodiscard]] Result<PreparedAssetImport> prepareUnityMaterial(const UnityProjectImportRequest& request,
                                                               const UnitySourceAsset&          source);
/** @brief Resolve renderer references after all collection assets are prepared; unpublished candidate only. */
[[nodiscard]] Result<void> bindUnityRenderers(const UnityProjectImportRequest& request, const UnitySourceIndex& index,
                                              PreparedAssetImport& output);
}  // namespace eve::asset_import

namespace eve::asset_import::unity_detail {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.import.unity"));
}

inline std::string foldAscii(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

inline bool validGuid(std::string_view value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
}

inline bool validPath(std::string_view path, const AssetImportLimits& limits) {
    if (path.empty() || path.size() > limits.maximumStringBytes || path.front() == '/' ||
        path.find_first_of("\\:<>\"|?*") != std::string_view::npos || !isValidUtf8(path, Utf8NullPolicy::Reject))
        return false;
    for (unsigned char c : path)
        if (c < 32 || c == 127) return false;
    std::size_t start = 0;
    while (start < path.size()) {
        auto end = path.find('/', start);
        if (end == std::string_view::npos) end = path.size();
        const auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        start = end + 1;
    }
    return path.back() != '/';
}

/** @brief Read an unindented scalar from .meta; rejects duplicate keys and binary metadata. */
[[nodiscard]] Result<std::string> metaScalar(std::span<const std::uint8_t> bytes, std::string_view key,
                                             std::string_view path);

}  // namespace eve::asset_import::unity_detail
