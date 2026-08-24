#pragma once

#include "editor/EditorProperty.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::editor {

/** @brief Current indexing/import health of an asset record. */
enum class AssetStatus { Ready, Scanning, Importing, Stale, MissingSource, MissingArtifact, Warning, Error, Deleted };

/** @brief Value snapshot stored by the editor asset index. */
struct AssetRecord {
    AssetGuid                     guid;
    std::string                   logicalUri;
    std::string                   typeId;
    std::uint32_t                 schemaVersion = 1;
    std::string                   sourceUri;
    std::string                   sourceHash;
    std::string                   importerId;
    std::uint32_t                 importerVersion = 1;
    std::vector<std::string>      artifacts;
    std::vector<std::string>      tags;
    AssetStatus                   status = AssetStatus::Ready;
    EditorValue                   metadata;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Semantic relationship used for dependency closure and deletion planning. */
enum class DependencyKind { Hard, Soft, EditorOnly, Build, Source };

/** @brief Directed dependency between two stable asset identities. */
struct AssetDependency {
    AssetGuid      from;
    AssetGuid      to;
    DependencyKind kind = DependencyKind::Hard;
    PropertyPath   sourceProperty;
};

/** @brief Deterministic filter used by headless and UI asset queries. */
struct AssetQuery {
    std::vector<std::string>   typeIds;
    std::vector<std::string>   pathPrefixes;
    std::vector<std::string>   tagsAll;
    std::string                text;
    std::optional<AssetStatus> status;
};

/** @brief One page of values tied to an asset index generation. */
template <class T>
struct AssetPage {
    std::vector<T> values;
    std::size_t    nextOffset = 0;
    bool           hasMore    = false;
    std::uint64_t  generation = 0;
};

/** @brief Thread-neutral in-memory asset index used by editor hosts and tests. */
class MemoryAssetDatabase {
public:
    /** @brief Atomically publish one validated record and its dependencies. */
    EditorResult<AssetRecord> publish(AssetRecord record, std::vector<AssetDependency> dependencies = {});
    /** @brief Find one asset by stable GUID. */
    EditorResult<AssetRecord> find(const AssetGuid& guid) const;
    /** @brief Find one asset by logical content URI. */
    EditorResult<AssetRecord> findByUri(const std::string& logicalUri) const;
    /** @brief Query a deterministic page; stale generations return Conflict. */
    EditorResult<AssetPage<AssetRecord>> query(const AssetQuery& query, std::size_t offset, std::size_t limit,
                                               std::optional<std::uint64_t> generation = std::nullopt) const;
    /** @brief Query outgoing or incoming dependencies for one asset. */
    std::vector<AssetDependency> dependencies(const AssetGuid& guid, bool incoming = false) const;
    /** @brief Return the monotonic index generation. */
    std::uint64_t generation() const { return generation_; }

private:
    std::unordered_map<AssetGuid, AssetRecord, StrongEditorIdHash<AssetGuid>> records_;
    std::vector<AssetDependency>                                              dependencies_;
    std::uint64_t                                                             generation_ = 0;
};

/** @brief Validated importer output staged before publishing to AssetDB. */
struct ImportProduct {
    AssetRecord                  record;
    std::vector<AssetDependency> dependencies;
};

/** @brief Small coordinator that validates importer output before atomic index publication. */
class ImportCoordinator {
public:
    explicit ImportCoordinator(MemoryAssetDatabase* database) : database_(database) {}
    /** @brief Validate and publish a completed import product. */
    EditorResult<AssetRecord> publish(ImportProduct product);

private:
    MemoryAssetDatabase* database_ = nullptr;
};

}  // namespace eve::editor
