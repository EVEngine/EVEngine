#include "editor/EvaAssetDatabaseProjection.h"

#include <algorithm>

namespace eve::editor {
namespace {

DependencyKind projectionKind(asset::EvaDependencyKind kind) {
    switch (kind) {
        case asset::EvaDependencyKind::RuntimeRequired: return DependencyKind::Hard;
        case asset::EvaDependencyKind::RuntimeOptional: return DependencyKind::Soft;
        case asset::EvaDependencyKind::Build: return DependencyKind::Build;
        case asset::EvaDependencyKind::Editor: return DependencyKind::EditorOnly;
        case asset::EvaDependencyKind::Source: return DependencyKind::Source;
        case asset::EvaDependencyKind::Platform: return DependencyKind::Build;
    }
    return DependencyKind::Hard;
}

AssetGuid guid(const AssetRef& asset) { return AssetGuid(asset.id().format()); }

}  // namespace

EditorResult<std::vector<AssetRecord>> publishEvaAssetProjection(
    MemoryAssetDatabase& database, const asset::EvaManifest& manifest, std::string archiveUri,
    std::string importerId) {
    if (archiveUri.empty() || importerId.empty())
        return EditorResult<std::vector<AssetRecord>>::error(
            EditorStatus::Rejected, RuleId("editor.asset.eva-projection.invalid-source"),
            "Eva projection requires an archive URI and importer identity");
    std::vector<AssetPublication> publications;
    publications.reserve(manifest.assets.size());
    for (const asset::EvaAssetEntry& source : manifest.assets) {
        AssetPublication publication;
        publication.record.guid            = guid(source.asset);
        publication.record.logicalUri      = source.asset.format();
        publication.record.typeId          = source.type;
        publication.record.schemaVersion   = static_cast<std::uint32_t>(source.schemaVersion.value());
        publication.record.sourceUri       = archiveUri + "#" + source.definition;
        publication.record.sourceHash      = source.contentHash;
        publication.record.importerId      = importerId;
        publication.record.importerVersion = 1;
        publication.record.tags            = source.tags;
        publication.record.metadata = EditorValue::Object{
            {"packageId", EditorValue(manifest.packageId.format())},
            {"packageName", EditorValue(manifest.packageName)},
            {"packageVersion", EditorValue(manifest.packageVersion)},
            {"definition", EditorValue(source.definition)},
        };
        for (const asset::EvaDependency& sourceDependency : manifest.dependencies) {
            if (sourceDependency.from != source.asset) continue;
            publication.dependencies.push_back(
                {guid(sourceDependency.from), guid(sourceDependency.to), projectionKind(sourceDependency.kind),
                 PropertyPath(sourceDependency.path.empty() ? "dependency" : sourceDependency.path)});
        }
        std::sort(publication.dependencies.begin(), publication.dependencies.end(),
                  [](const AssetDependency& left, const AssetDependency& right) {
                      if (left.to != right.to) return left.to < right.to;
                      return left.sourceProperty < right.sourceProperty;
                  });
        publications.push_back(std::move(publication));
    }
    return database.publishBatch(std::move(publications));
}

}  // namespace eve::editor
