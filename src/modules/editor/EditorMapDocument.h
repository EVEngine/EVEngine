#pragma once

/** @file @brief Compatibility include for canonical map editing documents. */
#include "editor/EditorAuthority.h"
#include "map_editing/MapDocument.h"

namespace eve::editor {
using MapLayerRecord          = map_editing::MapLayerRecord;
using MapSplinePointRecord    = map_editing::MapSplinePointRecord;
using MapRoadRecord           = map_editing::MapRoadRecord;
using MapPlacementRecord      = map_editing::MapPlacementRecord;
using MapRoadPreviewResult    = map_editing::MapRoadPreviewResult;
using IMapRoadMeshSink        = map_editing::IMapRoadMeshSink;
using MapObjectImportRecord   = map_editing::MapObjectImportRecord;
using MapObjectImportPlan     = map_editing::MapObjectImportPlan;
using IMapStructureEditTarget = map_editing::IMapStructureEditTarget;
using MapDocumentTarget       = map_editing::MapDocumentTarget;
using MapRoadMeshPublisher    = map_editing::MapRoadMeshPublisher;
using MapObjectImporter       = map_editing::MapObjectImporter;
}  // namespace eve::editor
