#include "editor/EditorAssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> assetError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

bool containsInsensitive(std::string value, std::string text) {
    auto lower = [](unsigned char character) { return static_cast<char>(std::tolower(character)); };
    std::transform(value.begin(), value.end(), value.begin(), lower);
    std::transform(text.begin(), text.end(), text.begin(), lower);
    return value.find(text) != std::string::npos;
}

}  // namespace

EditorResult<AssetRecord> MemoryAssetDatabase::publish(AssetRecord record, std::vector<AssetDependency> dependencies) {
    if (record.guid.empty() || record.logicalUri.empty() || record.typeId.empty())
        return assetError<AssetRecord>(EditorStatus::Rejected, "editor.asset.invalid-record",
                                       "Asset GUID, logical URI and type are required");
    const auto uriOwner = uriToGuid_.find(record.logicalUri);
    if (uriOwner != uriToGuid_.end() && uriOwner->second != record.guid)
        return assetError<AssetRecord>(EditorStatus::Conflict, "editor.asset.uri-conflict",
                                       "Another asset already owns this logical URI");
    for (const AssetDependency& dependency : dependencies) {
        if (dependency.from != record.guid || dependency.to.empty())
            return assetError<AssetRecord>(EditorStatus::Rejected, "editor.asset.invalid-dependency",
                                           "Published dependencies must originate from the product asset");
    }
    record.status       = AssetStatus::Ready;
    const auto existing = records_.find(record.guid);
    if (existing != records_.end() && existing->second.logicalUri != record.logicalUri)
        uriToGuid_.erase(existing->second.logicalUri);
    uriToGuid_.insert_or_assign(record.logicalUri, record.guid);
    records_.insert_or_assign(record.guid, record);
    std::erase_if(dependencies_, [&](const AssetDependency& dependency) { return dependency.from == record.guid; });
    dependencies_.insert(dependencies_.end(), dependencies.begin(), dependencies.end());
    ++generation_;
    return EditorResult<AssetRecord>::applied(std::move(record));
}

EditorResult<AssetRecord> MemoryAssetDatabase::find(const AssetGuid& guid) const {
    auto found = records_.find(guid);
    if (found == records_.end())
        return assetError<AssetRecord>(EditorStatus::NotFound, "editor.asset.not-found",
                                       "Asset is not indexed: " + guid.value());
    return EditorResult<AssetRecord>::applied(found->second);
}

EditorResult<AssetRecord> MemoryAssetDatabase::findByUri(const std::string& logicalUri) const {
    const auto guid = uriToGuid_.find(logicalUri);
    if (guid != uriToGuid_.end()) return find(guid->second);
    return assetError<AssetRecord>(EditorStatus::NotFound, "editor.asset.not-found",
                                   "Asset URI is not indexed: " + logicalUri);
}

EditorResult<AssetPage<AssetRecord>> MemoryAssetDatabase::query(const AssetQuery& query, std::size_t offset,
                                                                std::size_t                  limit,
                                                                std::optional<std::uint64_t> generation) const {
    if (generation && *generation != generation_)
        return assetError<AssetPage<AssetRecord>>(EditorStatus::Conflict, "editor.asset.page-expired",
                                                  "Asset index changed while paging");
    std::vector<AssetRecord> matches;
    for (const auto& [guid, record] : records_) {
        (void)guid;
        if (query.status && record.status != *query.status) continue;
        if (!query.typeIds.empty() &&
            std::find(query.typeIds.begin(), query.typeIds.end(), record.typeId) == query.typeIds.end())
            continue;
        if (!query.pathPrefixes.empty()) {
            bool prefix = false;
            for (const std::string& candidate : query.pathPrefixes)
                if (record.logicalUri.starts_with(candidate)) prefix = true;
            if (!prefix) continue;
        }
        bool tags = true;
        for (const std::string& tag : query.tagsAll)
            if (std::find(record.tags.begin(), record.tags.end(), tag) == record.tags.end()) tags = false;
        if (!tags) continue;
        if (!query.text.empty() && !containsInsensitive(record.logicalUri, query.text) &&
            !containsInsensitive(record.typeId, query.text))
            continue;
        matches.push_back(record);
    }
    std::sort(matches.begin(), matches.end(),
              [](const AssetRecord& left, const AssetRecord& right) { return left.logicalUri < right.logicalUri; });
    AssetPage<AssetRecord> page;
    page.generation = generation_;
    if (offset < matches.size()) {
        const std::size_t end = std::min(matches.size(), offset + limit);
        page.values.assign(matches.begin() + static_cast<std::ptrdiff_t>(offset),
                           matches.begin() + static_cast<std::ptrdiff_t>(end));
        page.nextOffset = end;
        page.hasMore    = end < matches.size();
    }
    return EditorResult<AssetPage<AssetRecord>>::applied(std::move(page));
}

std::vector<AssetDependency> MemoryAssetDatabase::dependencies(const AssetGuid& guid, bool incoming) const {
    std::vector<AssetDependency> result;
    for (const AssetDependency& dependency : dependencies_)
        if ((incoming && dependency.to == guid) || (!incoming && dependency.from == guid)) result.push_back(dependency);
    return result;
}

EditorResult<AssetRecord> ImportCoordinator::publish(ImportProduct product) {
    if (!database_)
        return assetError<AssetRecord>(EditorStatus::Failed, "editor.import.missing-database",
                                       "Import coordinator has no asset database");
    if (product.record.sourceUri.empty() || product.record.importerId.empty())
        return assetError<AssetRecord>(EditorStatus::Rejected, "editor.import.invalid-product",
                                       "Import product requires source URI and importer identity");
    return database_->publish(std::move(product.record), std::move(product.dependencies));
}

EditorResult<ImportTicket> ImportCoordinator::begin(const AssetGuid& asset, std::string sourceHash,
                                                    std::string importerId,
                                                    std::uint32_t importerVersion) {
    if (asset.empty() || sourceHash.empty() || importerId.empty() || importerVersion == 0)
        return assetError<ImportTicket>(EditorStatus::Rejected, "editor.import.invalid-request",
                                        "Import request requires asset, source hash and versioned importer");
    const std::uint64_t generation = ++generations_[asset];
    return EditorResult<ImportTicket>::applied(
        {asset, generation, std::move(sourceHash), std::move(importerId), importerVersion});
}

EditorResult<AssetRecord> ImportCoordinator::publish(const ImportTicket& ticket,
                                                     ImportProduct product) {
    if (!database_)
        return assetError<AssetRecord>(EditorStatus::Failed, "editor.import.missing-database",
                                       "Import coordinator has no asset database");
    const auto generation = generations_.find(ticket.asset);
    if (generation == generations_.end() || generation->second != ticket.generation)
        return assetError<AssetRecord>(EditorStatus::Conflict, "editor.import.stale-product",
                                       "Importer product was superseded by a newer source generation");
    AssetRecord& record = product.record;
    if (record.guid != ticket.asset || record.sourceHash != ticket.sourceHash ||
        record.importerId != ticket.importerId || record.importerVersion != ticket.importerVersion)
        return assetError<AssetRecord>(EditorStatus::Conflict, "editor.import.ticket-mismatch",
                                       "Importer product identity does not match its request ticket");
    if (record.sourceUri.empty() || record.logicalUri.empty() || record.typeId.empty())
        return assetError<AssetRecord>(EditorStatus::Rejected, "editor.import.incomplete-product",
                                       "Importer product lacks source, logical URI or asset type");
    std::set<std::string> artifacts;
    for (const std::string& artifact : record.artifacts)
        if (artifact.empty() || !artifacts.insert(artifact).second)
            return assetError<AssetRecord>(EditorStatus::Rejected, "editor.import.invalid-artifact",
                                           "Importer artifacts must be unique and non-empty");
    if (record.artifacts.empty())
        return assetError<AssetRecord>(EditorStatus::Rejected, "editor.import.missing-artifact",
                                       "Importer product must contain at least one published artifact");
    if (std::any_of(record.diagnostics.begin(), record.diagnostics.end(),
                    [](const EditorDiagnostic& diagnostic) {
                        return diagnostic.severity == DiagnosticSeverity::Error;
                    }))
        return assetError<AssetRecord>(EditorStatus::Rejected, "editor.import.product-errors",
                                       "Importer product contains error diagnostics");
    auto published = database_->publish(std::move(record), std::move(product.dependencies));
    if (published.accepted()) generations_.erase(ticket.asset);
    return published;
}

}  // namespace eve::editor
