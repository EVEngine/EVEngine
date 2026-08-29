#include "editor/EditorMapDocument.h"

#include <cmath>
#include <set>

namespace eve::editor {

EditorResult<void> MapRoadMeshPublisher::publish(const MapDocumentTarget& document,
                                                  const StableId& road,
                                                  Revision expectedRevision,
                                                  IMapRoadMeshSink& sink,
                                                  int triangleBudget) const {
    if (expectedRevision != document.revision())
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.map.stale-road-preview"),
                                         "Map document changed before road mesh publication");
    const auto preview = document.previewRoad(road, triangleBudget);
    if (preview.status != EditorStatus::Applied)
        return EditorResult<void>::error(preview.status,
            preview.diagnostics.empty() ? RuleId("editor.map.road-preview-failed") : preview.diagnostics.front().rule,
            preview.diagnostics.empty() ? "Road preview failed" : preview.diagnostics.front().message);
    auto published = sink.publishRoad(document.targetId(), road, preview.documentRevision, preview.mesh);
    if (!published.accepted()) return published;
    if (document.revision() != expectedRevision)
        return EditorResult<void>::error(EditorStatus::Conflict, RuleId("editor.map.road-changed-during-publish"),
                                         "Map road changed while its generated mesh was being published");
    return EditorResult<void>::applied();
}

MapObjectImportPlan MapObjectImporter::plan(const MapDocumentTarget& document,
                                            const StableId& layer,
                                            const std::vector<MapObjectImportRecord>& records,
                                            const std::string& idPrefix) const {
    MapObjectImportPlan result;
    result.documentRevision = document.revision();
    if (layer.empty() || idPrefix.empty()) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.map.invalid-import-target"), DiagnosticSeverity::Error,
                                      "Map object import requires a target layer and stable id prefix"});
        return result;
    }
    std::set<std::string> sourceIds;
    std::set<StableId> existing;
    for (const auto& placement : document.mapPlacements()) existing.insert(placement.id);
    for (const auto& record : records) {
        if (record.sourceId.empty() || record.asset.empty() || !sourceIds.insert(record.sourceId).second) {
            result.status = EditorStatus::Conflict;
            result.diagnostics.push_back({RuleId("editor.map.invalid-import-object"), DiagnosticSeverity::Error,
                                          "Imported objects require unique source ids and asset references"});
            return result;
        }
        const StableId id(idPrefix + "/" + record.sourceId);
        if (existing.contains(id)) {
            result.status = EditorStatus::Conflict;
            result.diagnostics.push_back({RuleId("editor.map.import-id-conflict"), DiagnosticSeverity::Error,
                                          "Imported placement id already exists: " + id.value()});
            return result;
        }
        MapPlacementRecord placement{id, layer, record.asset, record.x, record.y, record.z,
            record.rotationX, record.rotationY, record.rotationZ,
            record.scaleX, record.scaleY, record.scaleZ};
        auto operation = document.makeSetPlacement(placement);
        if (!operation.value) {
            result.status = operation.status;
            result.diagnostics.insert(result.diagnostics.end(), operation.diagnostics.begin(), operation.diagnostics.end());
            return result;
        }
        result.operations.push_back(std::move(*operation.value));
        existing.insert(id);
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::editor
