#pragma once

#include "editor/EditorAssetDatabase.h"

namespace eve::editor {

/** @brief One environment-map asset card independent of a concrete browser UI. */
struct EnvironmentAssetCard {
    AssetGuid asset;
    std::string logicalUri;
    std::string previewArtifact;
    std::string layout;
    int width = 0;
    int height = 0;
    int faceCount = 0;
    AssetStatus status = AssetStatus::Error;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Generation-qualified environment asset browser page. */
struct EnvironmentAssetPage {
    std::vector<EnvironmentAssetCard> values;
    std::size_t nextOffset = 0;
    bool hasMore = false;
    std::uint64_t generation = 0;
};

/** @brief Queries and validates cubemap/equirectangular assets from the shared AssetDB. */
class EnvironmentAssetBrowser {
public:
    explicit EnvironmentAssetBrowser(const MemoryAssetDatabase* database) : database_(database) {}
    /** @brief Query deterministic cards; stale index generations return Conflict. */
    EditorResult<EnvironmentAssetPage> query(std::string text, std::size_t offset,
                                             std::size_t limit,
                                             std::optional<std::uint64_t> generation = std::nullopt) const;
    /** @brief Resolve a selected environment map and reject unusable import metadata. */
    EditorResult<EnvironmentAssetCard> select(const AssetGuid& asset) const;
private:
    EditorResult<EnvironmentAssetCard> card(const AssetRecord& record) const;
    const MemoryAssetDatabase* database_ = nullptr;
};

}  // namespace eve::editor
